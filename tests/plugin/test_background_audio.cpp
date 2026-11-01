// Drive the real ASI against a fake DirectSound8 DLL: load the plugin, let it
// hook the fake's export through the module watch, then call the device vtable
// and check the descriptor that reaches the fake's CreateSoundBuffer.
#include <windows.h>
#include <dsound.h>
#include "eax_fixture.h"
#include <cstring>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
template<class T> T proc(HMODULE module, const char* name) {
    const auto result = GetProcAddress(module, name);
    if (!result) throw std::runtime_error(std::string("missing export: ") + name);
    return reinterpret_cast<T>(result);
}
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
using CreateSoundBufferFn = HRESULT(STDMETHODCALLTYPE*)(IDirectSound8*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN);
using DirectSoundCreate8Fn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);

constexpr DWORD secondaryFlags = 0x500E0; // MSystem+0xACC31 streamed descriptor
constexpr DWORD primaryFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;

struct Release { template<class T> void operator()(T* p) const { if (p) p->Release(); } };
template<class T> using Com = std::unique_ptr<T, Release>;

// Independent model of the statically traced engine gates, not game execution.
int selection(DirectSoundCreate8Fn create, DSBUFFERDESC descriptor, bool use3d, bool useEax) {
    IDirectSound8* rawDevice=nullptr;
    const auto hr=create(nullptr,&rawDevice,nullptr);
    Com<IDirectSound8> device(rawDevice);
    if (hr!=S_OK || !device || !use3d || !useEax) return -1;
    descriptor.dwFlags=DSBCAPS_LOCHARDWARE|DSBCAPS_CTRL3D;
    descriptor.dwBufferBytes=64;
    WAVEFORMATEX format{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
    descriptor.lpwfxFormat=&format;
    std::vector<Com<IDirectSoundBuffer>> voices;
    for (int i=0;i<4;++i) {
        IDirectSoundBuffer* raw=nullptr;
        const auto result=device->CreateSoundBuffer(&descriptor,&raw,nullptr);
        Com<IDirectSoundBuffer> buffer(raw);
        if (result!=S_OK || !buffer) return -1;
        IDirectSound3DBuffer* spatial=nullptr;
        if (buffer->QueryInterface(IID_IDirectSound3DBuffer,reinterpret_cast<void**>(&spatial))!=S_OK) return -1;
        Com<IDirectSound3DBuffer> spatialOwner(spatial);
        IKsPropertySet* prop=nullptr;
        if (spatial->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&prop))!=S_OK) return -1;
        Com<IKsPropertySet> propertyOwner(prop);
        voices.push_back(std::move(buffer));
    }
    voices.clear();
    IDirectSoundBuffer* raw=nullptr;
    if (device->CreateSoundBuffer(&descriptor,&raw,nullptr)!=S_OK) return -1;
    Com<IDirectSoundBuffer> buffer(raw);
    IKsPropertySet* property=nullptr;
    if (buffer->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&property))!=S_OK) return -1;
    Com<IKsPropertySet> prop(property);
    ULONG support=0;
    if (prop->QuerySupport(eax::context,1,&support)!=S_OK || (support&3)!=3) return -1;
    for (const auto& slot:eax::slots)
        if (prop->QuerySupport(slot,0x10001,&support)!=S_OK || (support&3)!=3) return -1;
    if (prop->QuerySupport(eax::source,1,&support)!=S_OK || (support&3)!=3) return -1;
    int selected=-1;
    for (int i=0;i<4;++i) {
        eax::Slot packet;
        const auto before=packet;
        const auto result=prop->Set(eax::slots[i],0x10001,nullptr,0,&packet,sizeof(packet));
        check(std::memcmp(&before,&packet,sizeof(packet))==0,"slot input mutated");
        if (result==S_OK) { selected=i; break; }
        GUID effect{}; ULONG returned=0;
        if (prop->Get(eax::slots[i],0x10002,nullptr,0,&effect,sizeof(effect),&returned)==S_OK &&
            returned==16 && effect==eax::reverb) { selected=i; break; }
    }
    if (selected<0) return -1;
    eax::Room room;
    const auto originalRoom=room;
    for (int i=0;i<3;++i) {
        room=i==1 ? originalRoom : eax::Room{};
        if (i==1) { room.decay=3.2f; room.room=-500; }
        const auto before=room;
        check(prop->Set(eax::slots[selected],1,nullptr,0,&room,sizeof(room))==S_OK,"room set failed");
        check(std::memcmp(&before,&room,sizeof(room))==0,"room input mutated");
    }
    eax::Sends sends{{},eax::slots[selected]};
    check(prop->Set(eax::source,0x1b,nullptr,0,&sends,sizeof(sends))==S_OK,"source send failed");
    check(prop->Set(eax::source,0x1b,nullptr,0,&sends,31)==E_INVALIDARG,"bad send size HRESULT changed");
    check(prop->Set(eax::source,0x1b,&sends,1,&sends,sizeof(sends))==eax::unsupported,"instance arguments changed");
    check(prop->Set(eax::slots[selected],1,nullptr,0,&room,111)==E_INVALIDARG,"bad room size HRESULT changed");
    check(prop->Set(eax::slots[selected],0x10001,nullptr,0,&sends,27)==E_INVALIDARG,"bad slot size HRESULT changed");
    ULONG returned=0; GUID effect{};
    check(prop->Get(eax::slots[selected],0x10002,nullptr,0,&effect,15,&returned)==E_INVALIDARG,"bad effect size HRESULT changed");
    IDirectSoundBuffer* duplicate=nullptr;
    check(device->DuplicateSoundBuffer(buffer.get(),&duplicate)==S_OK,"duplicate failed");
    Com<IDirectSoundBuffer> duplicateOwner(duplicate);
    IKsPropertySet* duplicateProp=nullptr;
    check(duplicate->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&duplicateProp))==S_OK,"duplicate QI failed");
    Com<IKsPropertySet> duplicateProperties(duplicateProp);
    check(duplicateProp->Set(eax::source,0x1b,nullptr,0,&sends,sizeof(sends))==S_OK,"duplicate source set failed");
    IUnknown* identity=nullptr;
    check(prop->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity))==S_OK,"identity QI failed");
    check(identity==static_cast<IUnknown*>(buffer.get()),"COM identity changed");
    identity->Release();
    const auto count=prop->AddRef();
    check(prop->Release()+1==count,"COM refcount changed");
    return selected;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4) return 2;
        const std::wstring_view scenario(argv[3]);
        const bool enabled = scenario==L"on" || scenario==L"limit";
        const bool trace = scenario!=L"quiet";
        const auto dir = std::filesystem::absolute(argv[1]).parent_path() /
            (L"audio-plugin-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        const auto asi = dir / L"BigFix.asi";
        std::filesystem::copy_file(argv[1], asi, std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream ini(dir / L"BigFix.ini");
            ini << "[BigFix]\nTextureSizeFix=0\nCapturePng=0\nTraceAudio=" << (scenario==L"eax_only" || scenario==L"quiet" ? 0 : 1)
                << "\nTraceEax=" << trace << "\nBackgroundAudio="
                << (enabled ? 1 : 0) << "\n";
        }
        check(!GetModuleHandleW(L"dsound.dll"), "dsound must not be preloaded");
        check(LoadLibraryW(asi.c_str()) != nullptr, "ASI load failed");
        // The game resolves dsound after startup, so load the fake through the
        // plugin's LoadLibrary hooks rather than before them.
        HMODULE dsound = LoadLibraryW(argv[2]);
        check(dsound != nullptr, "fake dsound load failed");
        const auto create = proc<DirectSoundCreate8Fn>(dsound, "DirectSoundCreate8");
        IDirectSound8* device = nullptr;
        check(SUCCEEDED(create(nullptr, &device, nullptr)) && device != nullptr, "DirectSoundCreate8 failed");
        auto** vtable = *reinterpret_cast<void***>(device);
        check(vtable != nullptr, "device vtable missing");
        const auto createBuffer = reinterpret_cast<CreateSoundBufferFn>(vtable[3]);
        const auto lastFlags = proc<DWORD (*)()>(dsound, "stubLastFlags");
        const auto createCalls = proc<unsigned (*)()>(dsound, "stubCreateCalls");
        const auto lastChannels = proc<unsigned (*)()>(dsound, "stubLastChannels");
        const auto lastRate = proc<unsigned long (*)()>(dsound, "stubLastRate");
        const auto lastBits = proc<unsigned (*)()>(dsound, "stubLastBits");

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 44100;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = 176400;
        DSBUFFERDESC secondary{};
        secondary.dwSize = sizeof(DSBUFFERDESC);
        secondary.dwFlags = secondaryFlags;
        secondary.dwBufferBytes = 65536;
        secondary.lpwfxFormat = &format;
        LPDIRECTSOUNDBUFFER buffer = nullptr;
        check(SUCCEEDED(createBuffer(device, &secondary, &buffer, nullptr)), "secondary CreateSoundBuffer failed");
        check(createCalls() == 1, "secondary descriptor not forwarded");
        check(lastChannels() == 2 && lastRate() == 44100 && lastBits() == 16, "descriptor format changed");
        if (enabled)
            check(lastFlags() == (secondaryFlags | DSBCAPS_GLOBALFOCUS), "secondary buffer did not gain GLOBALFOCUS");
        else
            check(lastFlags() == secondaryFlags, "secondary buffer changed with BackgroundAudio=0");
        check(secondary.dwFlags == secondaryFlags, "the caller's descriptor was modified in place");
        buffer->Release();

        // The same hook must leave the primary descriptor exactly as sent.
        DSBUFFERDESC primary{};
        primary.dwSize = sizeof(DSBUFFERDESC);
        primary.dwFlags = primaryFlags;
        primary.dwBufferBytes = 0;
        check(SUCCEEDED(createBuffer(device, &primary, &buffer, nullptr)), "primary CreateSoundBuffer failed");
        check(createCalls() == 2, "primary descriptor not forwarded");
        check(lastFlags() == primaryFlags, "primary buffer flags changed");

        // A successful buffer QI must expose the same underlying COM identity.
        IKsPropertySet* properties = nullptr;
        check(buffer->QueryInterface(IID_IKsPropertySet, reinterpret_cast<void**>(&properties)) == S_OK,
              "IKsPropertySet QI failed");
        GUID unknown{0x12345678, 0, 0, {}};
        ULONG support = 0;
        check(properties->QuerySupport(unknown, 1, &support) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
              "unsupported property HRESULT changed");
        properties->Release();
        buffer->Release();
        device->Release();
        const auto configure=proc<void (*)(unsigned)>(dsound,"stubConfigure");
        const auto live=proc<unsigned (*)()>(dsound,"stubLiveBuffers");
        configure(1024);
        {
            IDirectSound8* d=nullptr; check(create(nullptr,&d,nullptr)==S_OK,"folded factory failed");
            Com<IDirectSound8> owner(d);
            IDirectSoundBuffer* b=nullptr; check(d->CreateSoundBuffer(&secondary,&b,nullptr)==S_OK,"folded buffer failed");
            Com<IDirectSoundBuffer> owned(b);
            IKsPropertySet* p=nullptr; check(b->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&p))==S_OK,"folded QI failed");
            Com<IKsPropertySet> properties(p);
            eax::Room room; ULONG count=99,bits=99;
            check(p->Get(unknown,0x777,nullptr,0,&room,112,&count)==E_NOTIMPL && count==99,"folded Get result changed");
            check(p->Set(unknown,0x777,nullptr,0,&room,112)==E_NOTIMPL,"folded Set result changed");
            check(p->QuerySupport(unknown,0x777,&bits)==E_NOTIMPL && bits==99,"folded support result changed");
        }
        configure(0);
        check(!GetProcAddress(dsound,"EAXDirectSoundCreate8"),"fixture must use the ordinary factory fallback");
        check(selection(create,secondary,false,true)==-1,"3D-off gate bypassed");
        check(selection(create,secondary,true,false)==-1,"EAX-off gate bypassed");
        for (const auto& entry:std::vector<std::pair<unsigned,int>>{{0,0},{1,-1},{2,-1},{4,-1},{8,0},{40,-1},{48,1},{520,-1},{64,-1},{128,-1},{256,-1}}) {
            configure(entry.first);
            check(selection(create,secondary,true,true)==entry.second,"selection model gate/fallback failed");
            check(live()==0,"failure cleanup/restart leaked a buffer");
        }
        configure(0);
        check(proc<unsigned (*)()>(dsound,"stubRoomMask")()==3,"changed rooms/default reset not forwarded");
        // Simultaneous devices use different method implementations. LastError
        // tags expose an accidental shared trampoline even if HRESULTs match.
        IDirectSound8 *a=nullptr,*b=nullptr;
        check(create(nullptr,&a,nullptr)==S_OK && create(nullptr,&b,nullptr)==S_OK,"two factories failed");
        Com<IDirectSound8> ownerA(a),ownerB(b);
        for (auto* current:{a,b}) {
            IDirectSoundBuffer* raw=nullptr;
            check(current->CreateSoundBuffer(&secondary,&raw,nullptr)==S_OK,"two-vtable buffer failed");
            Com<IDirectSoundBuffer> owned(raw);
            IKsPropertySet* p=nullptr;
            check(raw->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&p))==S_OK,"two-vtable property QI failed");
            Com<IKsPropertySet> propOwner(p);
            ULONG bits=0;
            check(p->QuerySupport(eax::context,1,&bits)==S_OK && bits==3,"two-vtable support changed");
            const DWORD tag=GetLastError();
            check(tag==0x901 || tag==0x902,"property LastError changed");
            if (current==a) support=tag;
            else check(support!=tag,"different vtables used the same trampoline");
            std::atomic<bool> failed{false};
            const auto queryWorker=[&] {
                for(unsigned i=0;i<30;++i) {
                    ULONG output=0;
                    if(p->QuerySupport(eax::context,1,&output)!=S_OK || output!=3 || GetLastError()!=tag) failed=true;
                }
            };
            std::thread first(queryWorker),second(queryWorker);
            first.join(); second.join();
            check(!failed,"concurrent property forwarding changed result or LastError");
            if (scenario==L"limit") for (unsigned i=0;i<1000;++i) p->QuerySupport(eax::source,1,&bits);
        }
        std::ifstream log(dir / L"BigFix.log");
        const std::string text((std::istreambuf_iterator<char>(log)), {});
        check((text.find("EAX QuerySupport") != std::string::npos)==trace, "EAX trace switch failed");
        check((text.find("Audio backend:") != std::string::npos)==trace, "backend trace switch failed");
        if (trace) {
            check(text.find("size=112")!=std::string::npos,"room payload not traced");
            check(text.find("hr=0x80070490")!=std::string::npos,"negative HRESULT not traced");
            for(const auto* method:{"Get","Set","QuerySupport"}) {
                bool found=false;
                for(size_t at=0;(at=text.find(std::string("EAX ")+method+" ",at))!=std::string::npos;++at) {
                    const auto end=text.find('\n',at);
                    if(text.substr(at,end-at).find("property=0x777")!=std::string::npos) found=true;
                }
                check(found,"folded property address misidentified the COM method");
            }
        }
        if (scenario==L"limit") {
            check(text.find("EAX trace limit reached (512 calls)")!=std::string::npos,"missing trace limit");
            size_t count=0,at=0;
            while ((at=text.find("EAX QuerySupport",at))!=std::string::npos) {++count; ++at;}
            check(count<=512,"EAX trace unbounded");
        }
        std::cout << "Background audio plugin " << (enabled ? "on" : "off")
                  << ": DirectSound8 vtable descriptor forwarding PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
