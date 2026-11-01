#include "eyeshine_variants.h"
#include "log.h"
#include <MinHook.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
extern bool eyeshineScaleFingerprint;
namespace {
void check(bool value,const char* text){if(!value) throw std::runtime_error(text);}
using Scale=float* (*)(void*,float*);
struct FixtureCode {
    unsigned char* target;
    std::array<unsigned char,64> original{};
    DWORD protection=0;
    explicit FixtureCode(unsigned char* p):target(p) {
        std::memcpy(original.data(),p,original.size());
        check(VirtualProtect(p,original.size(),PAGE_EXECUTE_READWRITE,&protection)!=0,"fixture protection");
    }
    ~FixtureCode() {
        MH_Uninitialize();
        // +5870 can overlap the synthetic DLL's own static CRT, unlike the
        // larger video RVAs. Restore it before DLL/CRT process teardown.
        std::memcpy(target,original.data(),original.size());
        FlushInstructionCache(GetCurrentProcess(),target,original.size());
        DWORD ignored=0;VirtualProtect(target,original.size(),protection,&ignored);
    }
};
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==3,"fixture/scenario arguments");const std::wstring mode=argv[2];
        const auto log=std::filesystem::temp_directory_path()/(L"BigFix-scale-"+std::to_wstring(GetCurrentProcessId())+L".log");logInit(log);
        const auto image=LoadLibraryW(argv[1]);check(image!=nullptr,"synthetic image");
        auto* target=reinterpret_cast<unsigned char*>(image)+eyeshineScaleRva;
        FixtureCode restore(target);
        std::memcpy(target,eyeshineScaleBytes.data(),eyeshineScaleBytes.size());
        const unsigned char body[]{0xC7,0x02,0,0,0x40,0x3F,0xC7,0x42,4,0,0,0x40,0x3F,0xC3};
        std::memcpy(target+eyeshineScaleBytes.size(),body,sizeof(body));
        if(mode==L"bytes") target[6]=8;
        FlushInstructionCache(GetCurrentProcess(),target,64);
        eyeshineScaleFingerprint=mode!=L"fingerprint";
        const auto gl=LoadLibraryW(L"opengl32.dll");check(gl!=nullptr,"GL exports");
        Settings settings;settings.traceShadowPass=true;settings.eyeshineFixVariant=6;
        configureEyeshineVariants(gl,settings);check(eyeshineVariantsEnabled(),"variant dispatch");
        check(MH_Initialize()==MH_OK,"MinHook init");
        auto hookSettings=settings;if(mode==L"off") {hookSettings.traceShadowPass=false;hookSettings.eyeshineFixVariant=0;}
        installEyeshineScaleHook(image,hookSettings);
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        unsigned char self[0x20a0]{};float out[2]{};const auto call=reinterpret_cast<Scale>(target);
        check(call(self,out)==out,"return pointer changed");
        const bool on=mode==L"on";
        check(out[0]==(on?1.f:.75f) && out[1]==out[0],"scale hook gate");
        // F9 can turn a successfully installed scale hook back off immediately.
        eyeshineVariantSwap(TRUE,true);check(eyeshineVariant()==7,"cycle six to scissor fix");
        check(call(self,out)==out,"rollback return pointer");check(out[0]==.75f && out[1]==.75f,"scale rollback");
        logClose();std::filesystem::remove(log);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
