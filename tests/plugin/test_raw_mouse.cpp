// Synthetic mapped poll + fake DI7 device. No game code is loaded or run.
#include <windows.h>
#include <MinHook.h>
#include "raw_mouse.h"
#include "log.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
HWND foreground=nullptr;
bool registrationFails=false;
unsigned registrations=0, forwards=0;
RAWINPUT packet{};
ULONGLONG traceClock=0;
bool recursiveForward=false;
LRESULT WINAPI guardedForward(WNDPROC procedure,HWND window,UINT message,WPARAM w,LPARAM l) {
    static thread_local unsigned depth=0;
    // Detect a recursive WndProc chain without overflowing the harness stack.
    if(depth>=4) { recursiveForward=true; return 0; }
    ++depth; const auto result=CallWindowProcW(procedure,window,message,w,l); --depth; return result;
}
ULONGLONG WINAPI fakeMilliseconds() { traceClock+=100; return traceClock; }
UINT WINAPI fakeRawData(HRAWINPUT,UINT command,LPVOID data,PUINT size,UINT header) {
    if(command!=RID_INPUT || header!=sizeof(RAWINPUTHEADER) || *size<sizeof(packet)) return UINT(-1);
    std::memcpy(data,&packet,sizeof(packet)); *size=sizeof(packet); return sizeof(packet);
}
HWND WINAPI fakeForeground() { return foreground; }
BOOL WINAPI fakeRegister(PCRAWINPUTDEVICE devices,UINT count,UINT size) {
    if(registrationFails || count!=1 || size!=sizeof(RAWINPUTDEVICE) || devices->usUsagePage!=1 ||
       devices->usUsage!=2 || devices->dwFlags!=0 || devices->hwndTarget!=foreground) return FALSE;
    ++registrations; return TRUE;
}
}
// Only the OS packet source/focus/registration are faked; production subclass,
// packet validation, accumulator, MinHook and DI batch transformation stay real.
#define GetRawInputData fakeRawData
#define GetForegroundWindow fakeForeground
#define RegisterRawInputDevices fakeRegister
#define GetTickCount64 fakeMilliseconds
#define CallWindowProcW guardedForward
#include "../../src/raw_mouse.cpp"
#undef GetRawInputData
#undef GetForegroundWindow
#undef RegisterRawInputDevices
#undef GetTickCount64
#undef CallWindowProcW

namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
using GetData=HRESULT (WINAPI*)(void*,DWORD,MouseRecord*,DWORD*,DWORD);
struct Device { void** vtable; };
std::vector<MouseRecord> batch, delivered;
HRESULT diStatus=S_OK;
unsigned reads=0, reacquires=0;
long appliedX=0, appliedY=0;
__declspec(noinline) HRESULT WINAPI getData(void*,DWORD size,MouseRecord* records,DWORD* count,DWORD flags) {
    ++reads;
    check(size==16 && flags==0,"DI7 ABI/flags changed");
    const auto n=(std::min)(*count,DWORD(batch.size()));
    std::memcpy(records,batch.data(),n*16); *count=n;
    return diStatus;
}
LRESULT CALLBACK baseWindow(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_INPUT) { ++forwards; return 0; }
    return DefWindowProcW(window,message,w,l);
}
void pollBody(void* mouse) {
    void* device=nullptr; std::memcpy(&device,static_cast<char*>(mouse)+0x28,8);
    MouseRecord records[100]{}; DWORD count=100;
    auto** table=*static_cast<void***>(device);
    const auto result=reinterpret_cast<GetData>(table[0x50/8])(device,16,records,&count,0);
    if(result==HRESULT(0x8007001e) || result==HRESULT(0x8007000c)) ++reacquires;
    if(result!=S_OK) return; // Actual engine overflow-drop branch.
    for(DWORD i=0;i<count;++i) {
        if(records[i].offset==0) appliedX+=static_cast<long>(records[i].data);
        else if(records[i].offset==4) appliedY+=static_cast<long>(records[i].data);
        else delivered.push_back(records[i]);
    }
}
void raw(HWND window,long x,long y,USHORT flags=0) {
    packet={}; packet.header.dwType=RIM_TYPEMOUSE; packet.header.dwSize=sizeof(packet);
    packet.data.mouse.usFlags=flags; packet.data.mouse.lLastX=x; packet.data.mouse.lLastY=y;
    SendMessageW(window,WM_INPUT,RIM_INPUT,1);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) return 2;
        const std::wstring scenario=argv[2];
        const auto dir=std::filesystem::temp_directory_path()/(L"BigFix-input-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir); logInit(dir/L"BigFix.log");
        const auto image=LoadLibraryW(argv[1]); check(image!=nullptr,"fixture load");
        const auto& site=rawMousePollSite(); auto* entry=reinterpret_cast<unsigned char*>(image)+site.rva;
        DWORD previous=0; check(VirtualProtect(entry,96,PAGE_EXECUTE_READWRITE,&previous)!=0,"fixture protect");
        std::memcpy(entry,site.expected.data(),site.expected.size());
        auto* code=entry+site.expected.size();
        // Undo prologue's r13 save and 0x738-byte stack allocation, tail-call fake.
        const unsigned char restore[]{0x4c,0x8b,0xac,0x24,0x08,0x07,0,0,0x48,0x81,0xc4,0x38,0x07,0,0,0x48,0xb8};
        std::memcpy(code,restore,sizeof(restore)); void* body=reinterpret_cast<void*>(&pollBody);
        std::memcpy(code+sizeof(restore),&body,8); code[25]=0xff;code[26]=0xe0;
        if(scenario==L"mismatch") entry[0]^=1;
        FlushInstructionCache(GetCurrentProcess(),entry,96);
        std::array<unsigned char,96> before{}; std::memcpy(before.data(),entry,96);
        check(MH_Initialize()==MH_OK,"MinHook init");
        Settings settings; settings.rawMouse=scenario!=L"off" && scenario!=L"off_trace";
        settings.traceInput=scenario==L"trace" || scenario==L"off_trace" || scenario==L"limit";
        const auto installed=installRawMouseHooks(image,settings);
        if(scenario==L"mismatch" || scenario==L"off") {
            check(installed==0,"off/mismatch hook installed");
            check(std::memcmp(before.data(),entry,96)==0,"off/mismatch bytes written");
            if(scenario==L"mismatch") { MH_Uninitialize(); logClose(); std::filesystem::remove_all(dir); return 0; }
        } else check(installed==1,"poll hook not created");
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        WNDCLASSW cls{}; cls.lpfnWndProc=baseWindow; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"BigFixInputFixture";
        check(RegisterClassW(&cls)!=0,"class");
        HWND window=CreateWindowW(cls.lpszClassName,L"input",WS_OVERLAPPEDWINDOW,0,0,200,200,nullptr,nullptr,cls.hInstance,nullptr);
        check(window!=nullptr,"window"); foreground=window;
        std::array<void*,11> methods{}; methods[10]=reinterpret_cast<void*>(&getData); Device device{methods.data()}, other{methods.data()};
        std::array<unsigned char,0x80> mouse{};
        std::memcpy(mouse.data()+0x18,&window,8); void* devicePointer=&device; std::memcpy(mouse.data()+0x28,&devicePointer,8);
        auto poll=reinterpret_cast<void (*)(void*)>(entry);
        registrationFails=scenario==L"registration_failure";
        poll(mouse.data()); // Installs packet receiver and device-specific boundary.
        const bool rawOn=settings.rawMouse && scenario!=L"registration_failure";
        check(registrations==(rawOn ? 1u:0u),"raw registration flags/window/off state");
        batch={{0,999,12,1},{12,128,13,2},{4,DWORD(-999),14,3},{8,DWORD(-120),15,4}};
        raw(window,123,-456); poll(mouse.data());
        check(appliedX==(rawOn ? 123:999) && appliedY==(rawOn ? -456:-999),"motion missing or doubled");
        check(delivered.size()==2 && delivered[0].offset==12 && delivered[0].data==128 && delivered[1].data==DWORD(-120),"button/wheel changed");
        check(delivered[0].timestamp==13 && delivered[1].sequence==4,"non-motion metadata changed");
        appliedX=appliedY=0; delivered.clear();
        batch.assign(100,MouseRecord{12,128,16,5}); diStatus=S_FALSE;
        for(int i=0;i<2000;++i) raw(window,7,-9);
        const auto beforeReads=reads; poll(mouse.data());
        check(reads==beforeReads+1,"DI drained or repeated during synthetic motion pass");
        check(appliedX==(rawOn ? 14000:0) && appliedY==(rawOn ? -18000:0),"overflow lost raw burst");
        check(delivered.size()==(rawOn ? 100:0),"overflow valid button batch lost/changed off");
        MouseRecord untouched[100]{}; DWORD n=100;
        check(reinterpret_cast<GetData>(methods[10])(&other,16,untouched,&n,0)==S_FALSE && n==100,"unrelated device changed");
        check(untouched[0].offset==12 && untouched[0].data==128,"unrelated batch mutated");
        if(rawOn) {
            batch.clear();diStatus=S_OK;appliedX=appliedY=0;
            raw(window,4,5); SendMessageW(window,WM_KILLFOCUS,0,0); foreground=nullptr;
            raw(window,600,700); poll(mouse.data());
            foreground=window; SendMessageW(window,WM_SETFOCUS,0,0); poll(mouse.data());
            check(appliedX==0 && appliedY==0,"focus return applied stale motion");
            raw(window,7,8,MOUSE_MOVE_ABSOLUTE); poll(mouse.data());
            check(appliedX==0 && appliedY==0,"absolute packet treated as relative");
            raw(window,6,-7); diStatus=HRESULT(0x8007001e); poll(mouse.data());
            check(reacquires==1 && appliedX==6 && appliedY==-7,"DI lost acquisition lost raw or reacquire");
            raw(window,10,20); SendMessageW(window,WM_CAPTURECHANGED,0,0); diStatus=S_OK;poll(mouse.data());
            check(appliedX==6 && appliedY==-7,"capture change retained motion");
            check(forwards>=2000,"WM_INPUT did not chain to existing subclass");
            const auto oldWindow=window;
            raw(oldWindow,10,20);
            window=CreateWindowW(cls.lpszClassName,L"recreated",WS_OVERLAPPEDWINDOW,0,0,200,200,nullptr,nullptr,cls.hInstance,nullptr);
            foreground=window;std::memcpy(mouse.data()+0x18,&window,8); poll(mouse.data());
            check(registrations==2,"recreated window not registered");
            foreground=oldWindow;std::memcpy(mouse.data()+0x18,&oldWindow,8);poll(mouse.data());
            raw(oldWindow,1,2);poll(mouse.data());
            check(!recursiveForward,"returning to a live window created a recursive subclass");
            foreground=window;std::memcpy(mouse.data()+0x18,&window,8);poll(mouse.data());raw(window,3,4);
            DestroyWindow(oldWindow);poll(mouse.data());
            check(appliedX==10 && appliedY==-1,"recreated window motion wrong");
            raw(window,10,20); devicePointer=&other;std::memcpy(mouse.data()+0x28,&devicePointer,8);
            poll(mouse.data());
            check(appliedX==10 && appliedY==-1,"device recreation applied stale motion");
        }
        if(scenario==L"limit") for(int i=0;i<2000;++i) poll(mouse.data());
        DestroyWindow(window); MH_Uninitialize();logClose();
        std::ifstream in(dir/L"BigFix.log"); const std::string text{std::istreambuf_iterator<char>(in),{}};in.close();
        if(settings.traceInput) {
            check(text.find("TraceInput poll")!=std::string::npos,"trace missing poll");
            for(const auto field:{"hr=","records=","overflow=","raw="}) check(text.find(field)!=std::string::npos,"trace missing required field");
            const auto overflow=rawOn ? "hr=00000001 records=100 overflow=1 raw=14000,-18000" : "hr=00000001 records=100 overflow=1 raw=0,0";
            check(text.find(overflow)!=std::string::npos,"trace did not preserve actual DI result/count and applied raw burst");
            std::size_t lines=0,position=0;
            while((position=text.find("TraceInput poll",position))!=std::string::npos) { ++lines;++position; }
            check(lines<=1500,"trace exceeded line budget");
            if(scenario==L"limit") check(lines==1500,"trace limit scenario did not exhaust budget");
        } else check(text.find("TraceInput poll")==std::string::npos,"trace emitted while off");
        std::filesystem::remove_all(dir);return 0;
    } catch(const std::exception& e) { OutputDebugStringA(e.what()); fprintf(stderr,"%s\n",e.what());return 1; }
}
