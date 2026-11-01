#include "raw_mouse.h"
#include "log.h"
#include <MinHook.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace {
std::int64_t saturatedAdd(std::int64_t value,long delta) {
    constexpr auto maximum=(std::numeric_limits<std::int64_t>::max)();
    constexpr auto minimum=(std::numeric_limits<std::int64_t>::min)();
    if(delta>0 && value>maximum-delta) return maximum;
    if(delta<0 && value<minimum-delta) return minimum;
    return value+delta;
}
long narrowDelta(std::int64_t value) {
    return static_cast<long>((std::clamp)(value,std::int64_t(LONG_MIN),std::int64_t(LONG_MAX)));
}
}
void RawDeltaAccumulator::add(long dx,long dy) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if(focused_) { x_=saturatedAdd(x_,dx); y_=saturatedAdd(y_,dy); }
}
std::pair<long,long> RawDeltaAccumulator::take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto result=std::make_pair(narrowDelta(x_),narrowDelta(y_)); x_=y_=0;
    return result;
}
void RawDeltaAccumulator::discard() {
    const std::lock_guard<std::mutex> lock(mutex_); x_=y_=0;
}
void RawDeltaAccumulator::setFocused(bool focused) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if(focused_!=focused) { x_=y_=0; focused_=focused; }
}
MouseBatchResult stripDirectInputMotion(MouseRecord* records,DWORD count,HRESULT status,bool enabled) {
    if(!enabled || (status!=S_OK && status!=S_FALSE)) return {status,count};
    DWORD retained=0;
    for(DWORD i=0;i<count;++i)
        if(records[i].offset!=0 && records[i].offset!=4) records[retained++]=records[i];
    // DI_BUFFEROVERFLOW == S_FALSE contains valid records. The original poll
    // accepts only S_OK, otherwise discarding even the buttons and wheel.
    return {S_OK,retained};
}
bool InputTraceLimiter::allow(std::uint64_t milliseconds) {
    if(lines_>=1500 || milliseconds<next_) return false;
    ++lines_; next_=milliseconds+100; return true;
}
const PatchSite& rawMousePollSite() {
    static const PatchSite site{"mouse_poll", L"MSystem.dll", 0x9BAC0,
        {0x4c,0x8b,0xdc,0x48,0x81,0xec,0x38,0x07,0,0,0x48,0x83,0x79,0x28,0,0x4d,0x89,0x6b,0xd0,0x4c,0x8b,0xe9}, {}};
    return site;
}

namespace {
using Poll=void (*)(void*);
using GetData=HRESULT (WINAPI*)(void*,DWORD,MouseRecord*,DWORD*,DWORD);
Poll originalPoll=nullptr;
GetData originalGetData=nullptr;
void* getDataTarget=nullptr;
std::mutex installMutex;
bool rawEnabled=false, traceEnabled=false;
RawDeltaAccumulator deltas;
std::atomic<HWND> inputWindow{nullptr};
std::atomic<void*> inputDevice{nullptr};
std::atomic<std::uint64_t> packets{0};
constexpr wchar_t windowProperty[]=L"BigFix.RawMouse.WindowState";
struct WindowState { WNDPROC original; bool wide; };

LRESULT CALLBACK inputWindowProc(HWND window,UINT message,WPARAM w,LPARAM l) noexcept {
    auto* state=static_cast<WindowState*>(GetPropW(window,windowProperty));
    if(!state) return DefWindowProcW(window,message,w,l);
    const auto saved=*state;
    try {
        if(inputWindow.load()==window || message==WM_NCDESTROY) switch(message) {
        case WM_INPUT: {
            deltas.setFocused(GetForegroundWindow()==window);
            // GetRawInputData consumes precisely the current message once. No
            // INPUTSINK/NOLEGACY: foreground-only, legacy buttons remain intact.
            RAWINPUT input{}; UINT size=sizeof(input);
            const auto bytes=GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER));
            if(GET_RAWINPUT_CODE_WPARAM(w)==RIM_INPUT && bytes!=UINT(-1) &&
               bytes>=offsetof(RAWINPUT,data)+sizeof(RAWMOUSE) && input.header.dwSize==bytes &&
               input.header.dwType==RIM_TYPEMOUSE && !(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)) {
                deltas.add(input.data.mouse.lLastX,input.data.mouse.lLastY);
                packets.fetch_add(1,std::memory_order_relaxed);
            }
            break;
        }
        case WM_KILLFOCUS: deltas.setFocused(false); break;
        case WM_SETFOCUS: deltas.setFocused(true); break;
        case WM_ACTIVATEAPP: deltas.setFocused(w!=0); break;
        case WM_ACTIVATE: deltas.setFocused(LOWORD(w)!=WA_INACTIVE); break;
        case WM_CAPTURECHANGED: case WM_DEVICECHANGE: deltas.discard(); break;
        case WM_NCDESTROY: {
            HWND expected=window;
            if(inputWindow.compare_exchange_strong(expected,nullptr)) deltas.setFocused(false);
            RemovePropW(window,windowProperty); delete state;
            break;
        }
        }
    } catch(...) { logLineNoThrow("RawMouse window callback failed; preserving window chain"); }
    // Includes WM_INPUT default cleanup and WM_NCDESTROY. In borderless mode
    // this chains to the existing subclass, after our focus reset has run.
    return saved.wide ? CallWindowProcW(saved.original,window,message,w,l)
                      : CallWindowProcA(saved.original,window,message,w,l);
}

bool attachWindow(HWND window) {
    if(!window) return false;
    if(inputWindow.load()==window) return true;
    // The poll's HWND is the one used by SetCooperativeLevel, not whichever
    // process happens to be foreground. Subclass on its owning thread only.
    DWORD pid=0;
    if(GetWindowThreadProcessId(window,&pid)!=GetCurrentThreadId() || pid!=GetCurrentProcessId()) return false;
    // A video restart can temporarily keep both HWNDs alive, or switch back to
    // the previous one. Reuse its chain rather than subclassing ourselves.
    std::unique_ptr<WindowState> state;
    LONG_PTR previous=0;
    if(!GetPropW(window,windowProperty)) {
        state=std::make_unique<WindowState>();
        state->wide=IsWindowUnicode(window)!=FALSE;
        state->original=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
        if(!state->original || !SetPropW(window,windowProperty,state.get())) return false;
        SetLastError(0);
        previous=state->wide ? SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&inputWindowProc))
                             : SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&inputWindowProc));
        if(!previous && GetLastError()) { RemovePropW(window,windowProperty); return false; }
        state->original=reinterpret_cast<WNDPROC>(previous);
    }
    const RAWINPUTDEVICE mouse{1,2,0,window};
    if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse))) {
        if(state) {
            if(state->wide) SetWindowLongPtrW(window,GWLP_WNDPROC,previous);
            else SetWindowLongPtrA(window,GWLP_WNDPROC,previous);
            RemovePropW(window,windowProperty);
        }
        return false;
    }
    state.release(); // Window owns the state until WM_NCDESTROY.
    deltas.discard(); deltas.setFocused(GetForegroundWindow()==window);
    inputWindow.store(window);
    logLineNoThrow("RawMouse foreground relative input registered; existing WndProc chained");
    return true;
}

struct PollState {
    void* device;
    bool replace=false, synthetic=false, observed=false, valid=false;
    HRESULT status=E_FAIL;
    DWORD count=0;
    std::pair<long,long> raw{};
};
thread_local PollState* activePoll=nullptr;
struct PollScope {
    PollState* previous;
    explicit PollScope(PollState& current) : previous(activePoll) { activePoll=&current; }
    ~PollScope() { activePoll=previous; }
};

HRESULT WINAPI deviceData(void* device,DWORD size,MouseRecord* records,DWORD* count,DWORD flags) {
    auto* poll=activePoll;
    if(!poll || poll->device!=device) return originalGetData(device,size,records,count,flags);
    if(poll->synthetic) {
        if(size!=16 || flags!=0 || !records || !count || *count<2) return E_INVALIDARG;
        const DWORD timestamp=GetTickCount();
        records[0]={0,static_cast<DWORD>(poll->raw.first),timestamp,0};
        records[1]={4,static_cast<DWORD>(poll->raw.second),timestamp,0};
        *count=2; return S_OK;
    }
    const DWORD capacity=count ? *count:0;
    const auto status=originalGetData(device,size,records,count,flags);
    poll->observed=true; poll->status=status; poll->count=count ? *count:0;
    // Honor queries, peek semantics and malformed/changed formats verbatim.
    poll->valid=size==16 && flags==0 && records && count && *count<=capacity;
    if(!poll->valid) return status;
    const auto result=stripDirectInputMotion(records,*count,status,poll->replace);
    *count=result.count; return result.status;
}

bool ensureDeviceHook(void* device) {
    auto** table=*static_cast<void***>(device);
    void* target=table[0x50/sizeof(void*)];
    const std::lock_guard<std::mutex> lock(installMutex);
    if(getDataTarget) return getDataTarget==target;
    // This is an OS COM implementation, identified ONLY via the verified mouse
    // poll. Other devices sharing the method are forwarded by the TLS scope.
    if(MH_CreateHook(target,reinterpret_cast<void*>(&deviceData),reinterpret_cast<void**>(&originalGetData))!=MH_OK) return false;
    if(MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target); originalGetData=nullptr; return false; }
    getDataTarget=target; return true;
}

void tracePoll(const PollState& poll,HWND window) noexcept {
    if(!traceEnabled || !poll.observed) return;
    try {
        static std::mutex mutex;
        static InputTraceLimiter limiter;
        static std::uint64_t polls=0, overflows=0;
        static std::int64_t totalX=0,totalY=0;
        const std::lock_guard<std::mutex> lock(mutex);
        ++polls; overflows+=poll.status==S_FALSE;
        totalX=saturatedAdd(totalX,poll.raw.first); totalY=saturatedAdd(totalY,poll.raw.second);
        if(!limiter.allow(GetTickCount64())) return;
        LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
        logf("TraceInput poll qpc=%lld tid=%lu hwnd=%p device=%p hr=%08lx records=%lu overflow=%u raw=%ld,%ld polls=%llu overflows=%llu packets=%llu applied_total=%lld,%lld",
            qpc.QuadPart,GetCurrentThreadId(),window,poll.device,static_cast<unsigned long>(poll.status),poll.count,
            poll.status==S_FALSE,poll.raw.first,poll.raw.second,polls,overflows,packets.load(),totalX,totalY);
    } catch(...) { /* Diagnostics must never escape into the engine. */ }
}

void mousePoll(void* mouse) noexcept {
    bool called=false;
    try {
        void* device=nullptr; HWND window=nullptr;
        std::memcpy(&device,static_cast<char*>(mouse)+0x28,sizeof(device));
        std::memcpy(&window,static_cast<char*>(mouse)+0x18,sizeof(window));
        if(rawEnabled && inputDevice.exchange(device)!=device) deltas.discard();
        if(!device || !ensureDeviceHook(device)) {
            deltas.discard();
            static std::atomic<bool> warned{false};
            if(!warned.exchange(true)) logLineNoThrow("RawMouse mouse DI boundary unavailable; preserving original poll");
            called=true; originalPoll(mouse); return;
        }
        const bool replace=rawEnabled && attachWindow(window);
        if(rawEnabled && !replace) {
            deltas.discard();
            static std::atomic<bool> warned{false};
            if(!warned.exchange(true)) logLineNoThrow("RawMouse window unavailable; preserving original DirectInput path");
        }
        PollState poll{device,replace}; const PollScope scope(poll);
        called=true; originalPoll(mouse);
        if(replace && poll.valid) {
            deltas.setFocused(GetForegroundWindow()==window);
            poll.raw=deltas.take();
            if(poll.raw.first || poll.raw.second) {
                // A full 100-record non-motion batch leaves no room for raw
                // axes. Use a second, synthetic-only pass through the original
                // decoder; this NEVER reads DI again, even after overflow/loss.
                // Its 0xFB/0xFA notifications and sensitivity stay game-owned.
                poll.synthetic=true; originalPoll(mouse);
            }
        }
        tracePoll(poll,window);
    } catch(...) {
        logLineNoThrow("RawMouse poll callback failed; preserving original poll when not yet called");
        if(!called) originalPoll(mouse);
    }
}
}

std::size_t installRawMouseHooks(HMODULE module,const Settings& settings) {
    if(!settings.rawMouse && !settings.traceInput) return 0;
    const std::lock_guard<std::mutex> lock(installMutex);
    if(originalPoll) return 0;
    const auto& site=rawMousePollSite(); MODULEINFO info{};
    if(!module || !K32GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info)) ||
       site.rva>info.SizeOfImage || site.expected.size()>info.SizeOfImage-site.rva) {
        logLineNoThrow("RawMouse mouse_poll RVA=9BAC0: bad module/address; skipped"); return 0;
    }
    auto* target=static_cast<unsigned char*>(info.lpBaseOfDll)+site.rva;
    std::array<unsigned char,22> bytes{}; SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),target,bytes.data(),bytes.size(),&read) || read!=bytes.size() ||
       !std::equal(site.expected.begin(),site.expected.end(),bytes.begin())) {
        logLineNoThrow("RawMouse mouse_poll RVA=9BAC0: prologue mismatch; skipped"); return 0;
    }
    rawEnabled=settings.rawMouse; traceEnabled=settings.traceInput;
    const auto status=MH_CreateHook(target,reinterpret_cast<void*>(&mousePoll),reinterpret_cast<void**>(&originalPoll));
    if(status!=MH_OK) { originalPoll=nullptr; logLineNoThrow("RawMouse mouse_poll hook creation failed; skipped"); return 0; }
    logLineNoThrow("RawMouse mouse_poll RVA=9BAC0: verified prologue; hook created");
    return 1;
}
void installRawMouse(const Settings& settings) {
    installRawMouseHooks(GetModuleHandleW(L"MSystem.dll"),settings);
}
