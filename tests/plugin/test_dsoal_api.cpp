// Dependency compatibility and synthetic loopback only. Never loads a game
// module or plays a sound through an output device.
#include <windows.h>
#include <dsound.h>
#include "eax_fixture.h"
#include "fingerprint.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> T proc(HMODULE module,const char* name) {
    const auto p=GetProcAddress(module,name); check(p!=nullptr,name); return reinterpret_cast<T>(p);
}
struct Release { template<class T> void operator()(T* p) const { if(p) p->Release(); } };
template<class T> using Com=std::unique_ptr<T,Release>;
using Create=HRESULT(WINAPI*)(LPCGUID,IDirectSound8**,IUnknown*);

// OpenAL C ABI declarations: no link-time dependency, headers or binary assets
// in the repository. Constants are the published OpenAL/loopback API tokens.
using GetProc=void* (__cdecl*)(void*,const char*);
template<class T> T alProc(GetProc get,const char* name) {
    const auto p=get(nullptr,name); check(p!=nullptr,name); return reinterpret_cast<T>(p);
}
double renderImpulse(HMODULE module,bool wet,float decay) {
    auto get=proc<GetProc>(module,"alcGetProcAddress");
    auto open=alProc<void* (__cdecl*)(const char*)>(get,"alcLoopbackOpenDeviceSOFT");
    auto create=proc<void* (__cdecl*)(void*,const int*)>(module,"alcCreateContext");
    auto current=proc<char (__cdecl*)(void*)>(module,"alcMakeContextCurrent");
    auto destroy=proc<void (__cdecl*)(void*)>(module,"alcDestroyContext");
    auto close=proc<char (__cdecl*)(void*)>(module,"alcCloseDevice");
    void* device=open(nullptr); check(device!=nullptr,"loopback open failed");
    const int attrs[]={0x1007,48000,0x1990,0x1501,0x1991,0x1406,0x20003,4,0};
    void* context=create(device,attrs); check(context!=nullptr,"loopback context failed");
    check(current(context)!=0,"loopback make current failed");
    auto alGet=proc<void* (__cdecl*)(const char*)>(module,"alGetProcAddress");
    auto eaxSet=reinterpret_cast<int (__cdecl*)(const GUID*,unsigned,unsigned,void*,unsigned)>(alGet("EAXSet"));
    auto eaxGet=reinterpret_cast<int (__cdecl*)(const GUID*,unsigned,unsigned,void*,unsigned)>(alGet("EAXGet"));
    check(eaxSet!=nullptr,"loopback EAXSet missing");
    auto genBuffers=proc<void (__cdecl*)(int,unsigned*)>(module,"alGenBuffers");
    auto genSources=proc<void (__cdecl*)(int,unsigned*)>(module,"alGenSources");
    auto bufferData=proc<void (__cdecl*)(unsigned,int,const void*,int,int)>(module,"alBufferData");
    auto sourcei=proc<void (__cdecl*)(unsigned,int,int)>(module,"alSourcei");
    auto play=proc<void (__cdecl*)(unsigned)>(module,"alSourcePlay");
    auto render=alProc<void (__cdecl*)(void*,void*,int)>(get,"alcRenderSamplesSOFT");
    auto error=proc<int (__cdecl*)()>(module,"alGetError");
    unsigned buffer=0,source=0; genBuffers(1,&buffer); genSources(1,&source);
    std::vector<short> impulse(48000); impulse[0]=30000;
    bufferData(buffer,0x1101,impulse.data(),static_cast<int>(impulse.size()*2),48000);
    sourcei(source,0x1009,buffer);
    eax::Slot slot;
    if(eaxSet(&eax::slots[0],0x10001,0,&slot,sizeof(slot))!=0) {
        GUID effect{};
        check(eaxGet && eaxGet(&eax::slots[0],0x10002,0,&effect,16)==0 && effect==eax::reverb,"loopback existing-reverb fallback failed");
    }
    eax::Room room; room.decay=decay;
    check(eaxSet(&eax::slots[0],1,0,&room,sizeof(room))==0,"loopback room set failed");
    eax::Sends sends{{},wet ? eax::slots[0] : GUID_NULL};
    check(eaxSet(&eax::source,0x1b,source,&sends,sizeof(sends))==0,"loopback source set failed");
    check(error()==0,"OpenAL setup failed"); play(source);
    std::vector<float> samples(96000*2); render(device,samples.data(),96000);
    check(error()==0,"loopback render failed");
    double energy=0;
    for(size_t i=0;i<samples.size();++i) {
        check(std::isfinite(samples[i]),"nonfinite loopback sample");
        if(i>=4800) energy+=static_cast<double>(samples[i])*samples[i];
    }
    auto deleteSources=proc<void (__cdecl*)(int,const unsigned*)>(module,"alDeleteSources");
    auto deleteBuffers=proc<void (__cdecl*)(int,const unsigned*)>(module,"alDeleteBuffers");
    deleteSources(1,&source); deleteBuffers(1,&buffer);
    current(nullptr); destroy(context); close(device);
    return energy;
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        if((argc!=3 && argc!=4) || !*argv[1]) { std::cout<<"SKIP: configure BIGFIX_DSOAL_DIR with the verified pair\n"; return 77; }
        const bool child=argc==4;
        const std::filesystem::path input(argv[1]);
        const std::wstring scenario(argv[2]);
        if(!std::filesystem::is_regular_file(input/L"dsound.dll") ||
            (!std::filesystem::is_regular_file(input/L"dsoal-aldrv.dll") && !(child && scenario==L"missing"))) return 77;
        check(sha256File(input/L"dsound.dll")=="3aff3479a359ba2124b51a93d002cf241f849f8e9bacc3d225bfe23e03b31df4","unpinned DSOAL input");
        if(!(child && scenario==L"missing")) check(sha256File(input/L"dsoal-aldrv.dll")=="072a09cdd7dd6fef0cf4ee9709991400c65971ee6ac701642fffe330eebbc3a3","unpinned OpenAL input");
        const auto temp=child ? input : std::filesystem::temp_directory_path()/(L"bigfix-dsoal-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(temp/L"elsewhere");
        const auto oldCwd=std::filesystem::current_path();
        struct Cleanup { std::filesystem::path path,cwd; ~Cleanup(){ std::filesystem::current_path(cwd); if(!path.empty()) {std::error_code ec; std::filesystem::remove_all(path,ec);} } } cleanup{child ? std::filesystem::path{} : temp,oldCwd};
        if(!child) {
            std::filesystem::copy_file(input/L"dsound.dll",temp/L"dsound.dll");
            if(scenario!=L"missing") std::filesystem::copy_file(input/L"dsoal-aldrv.dll",temp/L"dsoal-aldrv.dll");
            wchar_t self[32768]{}; GetModuleFileNameW(nullptr,self,32768);
            const auto executable=temp/L"api.exe";
            std::filesystem::copy_file(self,executable);
            std::wstring command=L"\""+executable.wstring()+L"\" \""+temp.wstring()+L"\" "+scenario+L" --child";
            STARTUPINFOW startup{}; startup.cb=sizeof(startup);
            startup.dwFlags=STARTF_USESTDHANDLES;
            startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
            PROCESS_INFORMATION process{};
            check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,(temp/L"elsewhere").c_str(),&startup,&process)!=0,"fixture subprocess failed");
            const auto wait=WaitForSingleObject(process.hProcess,30000);
            if(wait!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
            DWORD code=1; GetExitCodeProcess(process.hProcess,&code);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            return static_cast<int>(code);
        }
        const auto config=temp/L"alsoft.ini";
        {std::ofstream file(config); file<<"[eax]\nenable="<<(scenario==L"off" ? "false" : "true")<<"\n";}
        SetEnvironmentVariableW(L"ALSOFT_CONF",config.c_str());
        SetEnvironmentVariableW(L"ALSOFT_LOGLEVEL",L"0");
        SetEnvironmentVariableW(L"DSOAL_LOGLEVEL",L"0");
        std::filesystem::current_path(temp/L"elsewhere");
        HMODULE module=LoadLibraryW((temp/L"dsound.dll").c_str());
        if(scenario==L"missing") {
            if(module) {
                IDirectSound8* device=nullptr;
                const auto result=proc<Create>(module,"DirectSoundCreate8")(nullptr,&device,nullptr);
                Com<IDirectSound8> owner(device);
                check(FAILED(result),"missing renderer unexpectedly created a device");
            }
            std::cout<<"Missing renderer rejected\n"; return 0;
        }
        check(module!=nullptr,"DSOAL load failed from unrelated CWD");
        wchar_t loaded[32768]{};
        GetModuleFileNameW(module,loaded,32768);
        check(std::filesystem::equivalent(loaded,temp/L"dsound.dll"),"wrong DirectSound module loaded");
        if(scenario==L"loopback") {
            const auto backend=GetModuleHandleW(L"dsoal-aldrv.dll"); check(backend!=nullptr,"renderer not loaded");
            const auto dry=renderImpulse(backend,false,1.49f);
            const auto shortRoom=renderImpulse(backend,true,.4f);
            const auto longRoom=renderImpulse(backend,true,3.f);
            std::cout<<"Late impulse energy dry="<<dry<<" short="<<shortRoom<<" long="<<longRoom<<"\n";
            check(shortRoom>dry+1e-9 && longRoom>dry+1e-9 && std::abs(longRoom-shortRoom)>1e-9,"no distinct wet reverb energy");
            FreeLibrary(module); return 0;
        }
        IDirectSound8* rawDevice=nullptr;
        check(proc<Create>(module,"DirectSoundCreate8")(nullptr,&rawDevice,nullptr)==S_OK,"real factory failed");
        Com<IDirectSound8> device(rawDevice);
        HWND window=CreateWindowExW(0,L"STATIC",L"BigFix API fixture",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        check(window!=nullptr,"fixture window failed");
        check(device->SetCooperativeLevel(window,DSSCL_PRIORITY)==S_OK,"cooperative level failed");
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
        DSBUFFERDESC desc{}; desc.dwSize=sizeof(desc); desc.dwFlags=DSBCAPS_LOCHARDWARE|DSBCAPS_CTRL3D; desc.dwBufferBytes=64; desc.lpwfxFormat=&format;
        std::vector<Com<IDirectSoundBuffer>> voices;
        for(unsigned i=0;i<256;++i) {
            IDirectSoundBuffer* raw=nullptr;
            if(device->CreateSoundBuffer(&desc,&raw,nullptr)!=S_OK) break;
            voices.emplace_back(raw);
        }
        std::cout<<"Hardware 3D voice capacity="<<voices.size()<<"\n";
        check(voices.size()>=64,"insufficient verified voice capacity (64)");
        auto buffer=std::move(voices.front()); voices.clear();
        IKsPropertySet* rawProp=nullptr;
        check(buffer->QueryInterface(IID_IKsPropertySet,reinterpret_cast<void**>(&rawProp))==S_OK,"real property QI failed");
        Com<IKsPropertySet> prop(rawProp);
        ULONG bits=0;
        const auto support=prop->QuerySupport(eax::context,1,&bits);
        if(scenario==L"off") { check(FAILED(support) || (bits&3)!=3,"disabled EAX falsely supported"); }
        else {
            check(support==S_OK && (bits&3)==3,"EAX context unavailable");
            for(const auto& slot:eax::slots) check(prop->QuerySupport(slot,0x10001,&bits)==S_OK && (bits&3)==3,"FXSlot unsupported");
            check(prop->QuerySupport(eax::source,1,&bits)==S_OK && (bits&3)==3,"Source unsupported");
            eax::Slot slot;
            const auto slotResult=prop->Set(eax::slots[0],0x10001,nullptr,0,&slot,sizeof(slot));
            GUID effect{}; ULONG returned=0;
            check(prop->Get(eax::slots[0],0x10002,nullptr,0,&effect,16,&returned)==S_OK && returned==16 && effect==eax::reverb,"real effect getter failed");
            std::cout<<"Slot Set HRESULT=0x"<<std::hex<<static_cast<unsigned long>(slotResult)<<std::dec<<"; reverb Get fallback verified\n";
            eax::Room room;
            for(float decay:{.4f,3.f,1.49f}) { room.decay=decay; check(prop->Set(eax::slots[0],1,nullptr,0,&room,112)==S_OK,"real room setter failed"); }
            eax::Sends sends{{},eax::slots[0]};
            check(prop->Set(eax::source,0x1b,nullptr,0,&sends,32)==S_OK,"real source setter failed");
        }
        prop.reset(); buffer.reset(); device.reset(); DestroyWindow(window);
        const auto backend=GetModuleHandleW(L"dsoal-aldrv.dll"); check(backend!=nullptr,"backend absent");
        GetModuleFileNameW(backend,loaded,32768);
        check(std::filesystem::equivalent(loaded,temp/L"dsoal-aldrv.dll"),"wrong renderer loaded");
        FreeLibrary(module);
        std::cout<<"Pinned dependency API fixture PASS (no game, no playback)\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
}
