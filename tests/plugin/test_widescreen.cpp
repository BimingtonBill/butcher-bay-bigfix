#include "widescreen.h"
#include "video_trace.h"
#include "log.h"
#include <MinHook.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
unsigned char* image=nullptr;
bool hudOn=false;
unsigned hudCalls=0,textCalls=0;
using Overlay=void (*)(void*,void*,void*,void*);
template<class T> void put(void* p,std::size_t offset,const T& value) { std::memcpy(static_cast<char*>(p)+offset,&value,sizeof(value)); }
template<class T> T get(void* p,std::size_t offset) { T value{}; std::memcpy(&value,static_cast<char*>(p)+offset,sizeof(value)); return value; }
std::array<float,16> baseline{4,0,0,0, 0,3,0,0, 0,0,1,0, -1280,-720,20,1};
void rebuild(void* u) {
    baseline[0]*=2;baseline[5]*=2;baseline[12]-=16;
    put(u,0xbc,baseline);
}
void text(void*,void*,void*,void* u) {
    ++textCalls;
    const auto m=get<std::array<float,16>>(u,0xbc);
    check(m[0]==baseline[0]*(hudOn?.75f:1.f),"subtitle mapping/deduplication");
    check(m[12]==baseline[12]+(hudOn?80*baseline[0]:0),"subtitle centring");
}
void hud(void* a,void* b,void* c,void* u) {
    ++hudCalls;
    reinterpret_cast<Overlay>(image+0x61af0)(a,b,c,u);
    // Exercise scale-setter rebuilds inside a scope; the mapping must survive.
    reinterpret_cast<void (*)(void*)>(image+0x36bb0)(u);
    text(a,b,c,u);
}
void* data(void* p) { return p; }
const float* vertices(void* p) { return reinterpret_cast<const float*>(static_cast<char*>(p)+0x9c); }
std::array<unsigned char,0x1400> object{};
void* camera(void* client,void* output) {
    reinterpret_cast<void* (*)(void*)>(image+0x20e299)(object.data());
    std::memcpy(static_cast<char*>(client)+0x78c,output,0x13c);
    return static_cast<char*>(client)+0x78c;
}
void seed(const PatchSite& site,void* handler,unsigned stack) {
    auto* target=image+site.rva; DWORD old=0;
    check(VirtualProtect(target,128,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"fixture protection");
    std::memcpy(target,site.expected.data(),site.expected.size());
    if(!handler) return;
    auto* code=target+site.expected.size();
    if(stack) { const unsigned char add[]{0x48,0x81,0xc4}; std::memcpy(code,add,3); std::memcpy(code+3,&stack,4); code+=7; }
    if(site.rva==0x57790) *code++=0x5b; // restore the original pushed RBX
    code[0]=0x48; code[1]=0xb8; std::memcpy(code+2,&handler,8); code[10]=0xff; code[11]=0xe0;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==4,"arguments");
        const std::wstring scenario=argv[3];
        const bool off=scenario==L"off",mismatch=scenario==L"mismatch",callers=scenario==L"callers";
        const bool menuMismatch=scenario==L"menu_mismatch",allMismatch=scenario==L"all_mismatch",foreignUtil=scenario==L"foreign_util";
        hudOn=!off && !mismatch && !foreignUtil;
        const auto log=std::filesystem::temp_directory_path()/(L"BigFix-widescreen-"+std::to_wstring(GetCurrentProcessId())+L".log");
        logInit(log);
        const auto module=LoadLibraryW(argv[1]); check(module!=nullptr,"artificial PE");
        image=reinterpret_cast<unsigned char*>(module);
        const auto array=reinterpret_cast<unsigned char*>(GetProcAddress(module,"widescreenImage"));
        check(array && array<=image+0x9750 && array+0x300000>=image+0x220000,"artificial PE layout covers every patched RVA");
        for(const auto& item:widescreenSites()) {
            const auto& site=item.patch; void* handler=nullptr; unsigned stack=0;
            switch(site.rva) {
            case 0x36bb0: handler=reinterpret_cast<void*>(&rebuild);stack=0x78;break;
            case 0x136ef0: handler=reinterpret_cast<void*>(&camera);stack=0xd8;break;
            case 0x61af0: handler=reinterpret_cast<void*>(&text);break;
            case 0x21c500: handler=reinterpret_cast<void*>(&hud);stack=0x198;break;
            case 0x1e33b0: handler=reinterpret_cast<void*>(&data);stack=0x38;break;
            case 0x57790: handler=reinterpret_cast<void*>(&vertices);stack=0x20;break;
            }
            seed(site,handler,stack);
        }
        if(mismatch) image[0x36bb0]^=1;
        if(callers) image[0x20e3ab]^=1;
        if(menuMismatch) image[0x48f55]^=1;
        if(allMismatch) for(const auto& item:widescreenSites()) image[item.patch.rva]^=1;
        check(MH_Initialize()==MH_OK,"MinHook init");
        Settings s; s.hudAspectFix=!off; s.fov=off?0:100; s.traceFov=true;
        const auto world=installWidescreenHooks(module,WidescreenModule::World,s);
        if(allMismatch) {
            check(world==0,"mismatched world hooks installed");
            check(installWidescreenHooks(module,WidescreenModule::Classes,s)==0,"mismatched classes hooks installed");
            check(installWidescreenHooks(module,WidescreenModule::System,s)==0,"mismatched menu hook installed");
            installWidescreenHooks(module,WidescreenModule::Renderer,s);
            traceFovProjection(baseline.data(),image+0x975a,nullptr);
            for(const auto& item:widescreenSites()) check(MH_EnableHook(image+item.patch.rva)==MH_ERROR_NOT_CREATED,"mismatch created detour");
            MH_Uninitialize();logClose();
            std::ifstream mismatchLog(log);const std::string messages{std::istreambuf_iterator<char>(mismatchLog),{}};
            check(messages.find("prologue mismatch")!=messages.npos,"mismatched prologue was not logged");
            check(messages.find("projection upload bytes mismatch")!=messages.npos,"mismatched projection was not logged");
            mismatchLog.close();std::filesystem::remove(log);return 0;
        }
        check(world==(off?1u:mismatch?1u:2u),"world hook gate");
        const auto classes=installWidescreenHooks(module,WidescreenModule::Classes,s);
        check(classes==(off?1u:callers?2u:3u),"classes hook gate");
        installWidescreenHooks(module,WidescreenModule::Renderer,s);
        check(installWidescreenHooks(module,WidescreenModule::System,s)==(off?0u:1u),"menu hook gate");
        const auto gl=LoadLibraryW(argv[2]);check(gl!=nullptr,"artificial GL");
        auto glSettings=s;glSettings.videoAspectFix=false;glSettings.eyeshineFix=false;
        check(installVideoTraceHooks(gl,VideoModule::OpenGl,glSettings)==1,"TraceFov-only shared GL hook");
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        // Fixture caller returns from the exact verified instruction address.
        // Never execute original game instructions beyond this artificial ABI.
        const unsigned char entry[]{0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xc1};
        std::memcpy(image+0x20e299,entry,sizeof(entry));
        const unsigned char leave[]{0x48,0x83,0xc4,0x20,0x5b,0xc3};
        std::memcpy(image+0x20e2ac,leave,sizeof(leave));
        if(mismatch) { // Original rebuild body is valid even when its prologue check fails.
            const auto handler=reinterpret_cast<void*>(&rebuild); auto* p=image+0x36bb0;
            p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&handler,8);p[10]=0xff;p[11]=0xe0;
        }
        const unsigned char menuEntry[]{0x48,0x83,0xec,0x28,0x90};
        std::memcpy(image+0x48f58,menuEntry,sizeof(menuEntry));
        const unsigned char menuLeave[]{0x48,0x83,0xc4,0x28,0xc3};
        std::memcpy(image+0x48f62,menuLeave,sizeof(menuLeave));
        // The game import thunk is replaced by an artificial relative jump.
        DWORD thunkProtection=0;
        check(VirtualProtect(image+0x189886,16,PAGE_EXECUTE_READWRITE,&thunkProtection)!=FALSE,"menu thunk protection");
        image[0x189886]=0xe9;
        const int jump=0x57790-(0x189886+5);std::memcpy(image+0x189887,&jump,4);
        // Exercise the actual shared GL observer at the verified renderer call.
        DWORD uploadProtection=0;
        check(VirtualProtect(image+0x9750,32,PAGE_EXECUTE_READWRITE,&uploadProtection)!=FALSE,"upload protection");
        const unsigned char uploadEntry[]{0x48,0x83,0xec,0x28};
        std::memcpy(image+0x9750,uploadEntry,sizeof(uploadEntry));
        std::memcpy(image+0x975a,menuLeave,sizeof(menuLeave));
        const auto loadMatrix=GetProcAddress(gl,"glLoadMatrixf");check(loadMatrix!=nullptr,"fake matrix export");
        put(image,0x8ebe0,loadMatrix);
        FlushInstructionCache(GetCurrentProcess(),image,0x300000);
        std::array<unsigned char,0x248> util{};
        put(util.data(),0,reinterpret_cast<std::uintptr_t>(image)+(foreignUtil?0x1e59d0:0x1e59c8));
        put(util.data(),0x150,std::array<int,4>{0,0,2560,1440});
        put(util.data(),0xbc,baseline);
        reinterpret_cast<Overlay>(image+0x21c500)(nullptr,nullptr,nullptr,util.data());
        check(hudCalls==1 && textCalls==2,"original calls forwarded once");
        check(get<std::array<float,16>>(util.data(),0xbc)==baseline,"HUD matrix restored exactly");
        // Full-screen/background draws and unrelated callers retain this matrix.
        check(get<float>(util.data(),0xbc)==baseline[0],"background stays full width");
        std::array<unsigned char,0x13c> menu{};
        const std::array<float,16> projection{1000,0,0,0, 0,800,0,0, 100,-80,1,1, 0,0,-1,0};
        const std::array<float,15> corners{0,0,10, -10,-10,10, 10,-10,10, 10,10,10, -10,10,10};
        put(menu.data(),0x58,projection);put(menu.data(),0x9c,corners);
        put(menu.data(),0x44,std::array<int,4>{0,0,2560,1440});
        put(menu.data(),0x34,std::array<int,4>{0,0,2560,1440});
        const auto menuBefore=menu;
        const auto getter=reinterpret_cast<const float* (*)(void*)>(image+0x48f58);
        auto expectedCorners=corners;
        if(!off && !menuMismatch) check(alignMenuQuad(expectedCorners,projection,2560,1440),"menu expected math");
        check(std::memcmp(getter(menu.data()),expectedCorners.data(),sizeof(corners))==0,"menu caller projection alignment");
        check(menu==menuBefore,"menu viewport/engine vertices must not be modified");
        const auto direct=reinterpret_cast<const float* (*)(void*)>(image+0x57790);
        check(direct(menu.data())==reinterpret_cast<const float*>(menu.data()+0x9c),"unrelated frustum caller changed");
        put(object.data(),0x1a0,static_cast<void*>(object.data()));
        std::array<unsigned char,0xa00> client{};
        for(const unsigned flags:{0u,1u<<23,1u<<26}) {
            put(object.data(),0x150,flags);
            for(const unsigned char special:{0,2}) {
                put(object.data(),0x10f4,special);
                for(const float authored:{90.f,45.f}) {
                    std::array<unsigned char,0x13c> viewport{};
                    put(viewport.data(),4,authored);
                    const auto before=viewport;
                    const auto returned=reinterpret_cast<void* (*)(void*,void*)>(image+0x136ef0)(client.data(),viewport.data());
                    const bool modified=!off && !callers && flags==0 && special==0;
                    const auto expected=static_cast<float>(modified?relativeGameplayFov(authored,100):authored);
                    check(std::abs(get<float>(viewport.data(),4)-expected)<.001f,"camera classification/relative FOV");
                    check(returned==client.data()+0x78c,"camera return ABI");
                    check(std::memcmp(client.data()+0x78c,viewport.data(),viewport.size())==0,"saved/culling viewport coherent");
                    for(std::size_t i=0;i<viewport.size();++i) if(i<4 || (i>=8 && i!=0x54)) check(viewport[i]==before[i],"unrelated viewport fields changed");
                }
            }
        }
        const auto caller=image+0x975a;
        const auto traceCalls=reinterpret_cast<unsigned (*)()>(GetProcAddress(gl,"bigfixTraceCalls"));
        check(traceCalls!=nullptr,"GL forward counter");
        const auto beforeUpload=traceCalls();
        reinterpret_cast<void (*)(const float*)>(image+0x9750)(baseline.data());
        check(traceCalls()==beforeUpload+4,"projection observer changed original GL forwarding");
        for(unsigned n=0;n<1500;++n) {
            for(unsigned upload=0;upload<50;++upload) traceFovProjection(baseline.data(),caller,nullptr);
            widescreenFrameEnd(TRUE); // At most the sampled frames are logged.
        }
        MH_Uninitialize(); logClose();
        std::ifstream in(log); const std::string text{std::istreambuf_iterator<char>(in),{}};
        check(text.find("TraceFov upload frame=")!=text.npos,"projection trace missing");
        check(text.find("matrix=")!=text.npos,"projection matrix missing");
        if(!callers) check(text.find("400-line limit")!=text.npos,"projection trace limit missing");
        unsigned traceCount=0;
        for(std::size_t pos=0;(pos=text.find("TraceFov ",pos))!=text.npos;pos+=9) ++traceCount;
        check(traceCount<=400,"projection trace exceeded hard cap");
        if(callers) check(traceCount<=96,"unidentified camera consumed reserved gameplay trace budget");
        in.close();
        std::filesystem::remove(log);
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
