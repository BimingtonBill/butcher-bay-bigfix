// Real hidden native GL context; fixture exports stand in for engine draw calls.
// No game module is loaded or executed.
#include "shadow_trace.h"
#include "eyeshine_fix.h"
#include "eyeshine_variants.h"
#include "gl_hooks.h"
#include "video_trace.h"
#include "log.h"
#include <MinHook.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
template<class T> T exported(HMODULE module,const char* name) {
    auto p=GetProcAddress(module,name);check(p!=nullptr,name);return reinterpret_cast<T>(p);
}
template<class T> T extension(const char* name) {
    auto p=wglGetProcAddress(name);check(p!=nullptr,name);return reinterpret_cast<T>(p);
}
EyeshineRect currentScissor() { EyeshineRect r;glGetIntegerv(GL_SCISSOR_BOX,r.data());return r; }
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==3,"arguments");
        const auto logPath=std::filesystem::temp_directory_path()/(L"BigFixScissorTest-"+std::to_wstring(GetCurrentProcessId())+L".log");
        logInit(logPath);
        const std::wstring mode=argv[2];
        Settings settings;settings.videoAspectFix=false;settings.fixMiscBugs=false;
        settings.enable20PlusPlusAnyGpu=false;settings.eyeshineFix=mode!=L"off";
        settings.traceBloom=mode==L"bloom";
        settings.traceShadowPass=mode==L"coexist" || mode==L"variant0" || mode==L"variant7" || settings.traceBloom;
        settings.eyeshineFixVariant=mode==L"variant7" || settings.traceBloom?7:0;
        if(mode==L"variant7") settings.eyeshineFix=false;
        const bool corrected=mode==L"on" || mode==L"variant7" || settings.traceBloom;
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
        cls.lpszClassName=L"BigFixScissorFixture";cls.style=CS_OWNDC;
        check(RegisterClassW(&cls)!=0,"register hidden window");
        auto window=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,cls.hInstance,nullptr);
        check(window!=nullptr,"hidden window");auto dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;
        pfd.cColorBits=32;pfd.cDepthBits=24;pfd.cStencilBits=8;
        auto format=ChoosePixelFormat(dc,&pfd);check(format && SetPixelFormat(dc,format,&pfd),"pixel format");
        auto context=wglCreateContext(dc);check(context && wglMakeCurrent(dc,context),"native context");
        auto gl=GetModuleHandleW(L"opengl32.dll"),fixture=LoadLibraryW(argv[1]);check(fixture!=nullptr,"fixture DLL");
        check(MH_Initialize()==MH_OK,"MinHook init");
        installGlHooks(gl,settings,{});installShadowTraceHooks(gl,settings);
        check(installVideoTraceHooks(fixture,VideoModule::OpenGl,settings)==(mode==L"off"?0:6),"draw hook set");
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        shadowTraceArm(1);
        const auto gen=extension<void(WINAPI*)(GLsizei,GLuint*)>("glGenProgramsARB");
        const auto bind=extension<void(WINAPI*)(GLenum,GLuint)>("glBindProgramARB");
        const auto upload=extension<void(WINAPI*)(GLenum,GLenum,GLsizei,const void*)>("glProgramStringARB");
        const auto activate=extension<void(WINAPI*)(GLenum)>("glActiveTextureARB");
        GLuint program=0;gen(1,&program);bind(0x8804,program);
        const char* source="!!ARBfp1.0\n# ShadowMaskTexel NormalMapTexel texture[6]\nMOV result.color, {1,1,1,1};\nEND";
        upload(0x8804,0x8875,GLsizei(std::strlen(source)),source);glEnable(0x8804);
        check(glGetError()==GL_NO_ERROR,"native program upload");
        GLuint textures[2]{};glGenTextures(2,textures);
        activate(0x84C5);glBindTexture(GL_TEXTURE_2D,textures[0]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        activate(0x84C3);glBindTexture(GL_TEXTURE_2D,textures[0]);
        const auto arrays=exported<decltype(&glDrawArrays)>(fixture,"glDrawArrays");
        const auto elements=exported<decltype(&glDrawElements)>(fixture,"glDrawElements");
        const auto begin=exported<decltype(&glBegin)>(fixture,"glBegin");
        const auto end=exported<decltype(&glEnd)>(fixture,"glEnd");
        const auto recorded=exported<void(*)(GLint*)>(fixture,"bigfixRecordedScissor");
        const EyeshineRect light{79,360,1388,114},region{0,360,1920,1080};
        const auto scissor=[](const EyeshineRect& r){glScissor(r[0],r[1],r[2],r[3]);};
        const auto verify=[&](const EyeshineRect& expected){EyeshineRect r;recorded(r.data());check(r==expected,"scissor seen by draw");check(currentScissor()==region,"engine scissor not restored");GLint unit=0;glGetIntegerv(0x84E0,&unit);check(unit==0x84C3,"active texture leaked");};
        const auto copy=[&](const EyeshineRect& r){
            // Engine copy path: u0 destination, alpha-only mask, scissor disabled.
            // PopAttrib changes effective state without calling hooked glScissor.
            activate(0x84C0);glBindTexture(GL_TEXTURE_2D,textures[0]);
            scissor(r);glPushAttrib(GL_SCISSOR_BIT|GL_COLOR_BUFFER_BIT);
            scissor(region);glPopAttrib();
            glDisable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);
            glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,32,32);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_SCISSOR_TEST);
            activate(0x84C3);scissor(region);
        };
        glEnable(GL_SCISSOR_TEST);scissor(region);
        arrays(GL_POINTS,0,0);verify(region);
        copy(light);arrays(GL_POINTS,0,0);verify(corrected?light:region);
        GLint filter=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&filter);
        check(filter==GL_LINEAR,"sampler policy changed");
        elements(GL_POINTS,0,GL_UNSIGNED_SHORT,nullptr);verify(corrected?light:region);
        begin(GL_POINTS);end();verify(corrected?light:region);
        // Partial overlap and empty overlap both restrict; no draw may expand.
        scissor({80,360,1388,114});arrays(GL_POINTS,0,0);
        EyeshineRect partial;recorded(partial.data());
        check(partial==(corrected?EyeshineRect{80,360,1387,114}:EyeshineRect{80,360,1388,114}),"partial overlap");
        check(currentScissor()==EyeshineRect{80,360,1388,114},"partial state restore");
        scissor({2000,360,100,114});arrays(GL_POINTS,0,0);recorded(partial.data());
        check(partial==(corrected?EyeshineRect{0,0,0,0}:EyeshineRect{2000,360,100,114}),"disjoint draw");
        scissor(region);
        copy({743,474,617,966});arrays(GL_POINTS,0,0);verify(corrected?EyeshineRect{743,474,617,966}:region);
        // OFF: the current scissor already equals the copied light rectangle.
        copy(light);scissor(light);arrays(GL_POINTS,0,0);check(currentScissor()==light,"OFF scissor changed");
        EyeshineRect r;recorded(r.data());check(r==light,"OFF draw changed");
        glDisable(GL_SCISSOR_TEST);scissor(region);arrays(GL_POINTS,0,0);verify(corrected?light:region);
        check(!glIsEnabled(GL_SCISSOR_TEST),"disabled engine scissor not restored");glEnable(GL_SCISSOR_TEST);
        activate(0x84C5);glBindTexture(GL_TEXTURE_2D,textures[1]);activate(0x84C3);
        arrays(GL_POINTS,0,0);verify(region);
        activate(0x84C5);glBindTexture(GL_TEXTURE_2D,textures[0]);activate(0x84C3);
        // The same GL texture/program numeric names in another context carry no metadata.
        auto other=wglCreateContext(dc);check(other && wglMakeCurrent(dc,other),"second context");
        bind(0x8804,program);upload(0x8804,0x8875,GLsizei(std::strlen(source)),source);glEnable(0x8804);
        activate(0x84C5);glBindTexture(GL_TEXTURE_2D,textures[0]);activate(0x84C3);
        glEnable(GL_SCISSOR_TEST);scissor(region);arrays(GL_POINTS,0,0);verify(region);
        check(wglMakeCurrent(dc,context) && wglDeleteContext(other),"return to first context");
        arrays(GL_POINTS,0,0);verify(corrected?light:region);
        // Redefining another texture must not retire a still-fresh mask.
        activate(0x84C0);glBindTexture(GL_TEXTURE_2D,textures[1]);
        glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,0,0,32,32,0);activate(0x84C3);
        arrays(GL_POINTS,0,0);verify(corrected?light:region);
        // A newer non-mask copy to this texture invalidates just that mask.
        activate(0x84C0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,32,32);activate(0x84C3);
        arrays(GL_POINTS,0,0);verify(region);
        copy(light);
        // Reuploading an unrelated program under the same name retires classification.
        const char* unrelated="!!ARBfp1.0\nMOV result.color, {1,1,1,1};\nEND";
        upload(0x8804,0x8875,GLsizei(std::strlen(unrelated)),unrelated);
        arrays(GL_POINTS,0,0);verify(region);
        upload(0x8804,0x8875,GLsizei(std::strlen(source)),source);
        eyeshineFixFrameEnd();arrays(GL_POINTS,0,0);verify(region);
        // Complete per-frame accounting through the real hooks, independent of
        // variant selection and detailed tracing. Three bursts of eight frames.
        shadowTraceSwap(TRUE,false);
        for(unsigned n=1;n<=24;++n) {
            copy(light);arrays(GL_POINTS,0,0);verify(corrected?light:region);
            elements(GL_POINTS,0,GL_UNSIGNED_SHORT,nullptr);verify(corrected?light:region);
            begin(GL_POINTS);end();verify(corrected?light:region);
            shadowTraceCapture(n,true);shadowTraceSwap(TRUE,false);eyeshineFixFrameEnd();
        }
        if(mode==L"variant7") {
            eyeshineVariantSwap(TRUE,true);check(eyeshineVariant()==8,"F9 seven to whole-region");
            eyeshineVariantSwap(TRUE,false);eyeshineVariantSwap(TRUE,true);check(eyeshineVariant()==0,"F9 eight to original");
            copy(light);arrays(GL_POINTS,0,0);verify(region);
        }
        copy(light);eyeshineFixContextDeleted(context);arrays(GL_POINTS,0,0);verify(region);
        if(settings.traceBloom) bloomTraceSwap(dc,TRUE);
        check(glGetError()==GL_NO_ERROR,"GL error (including illegal query within Begin/End)");
        MH_Uninitialize();wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);
        logClose();
        std::ifstream input(logPath);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
        if(settings.traceBloom) {
            check(text.find("TraceBloom upload")!=text.npos,"native bloom uploads missing");
            check(text.find("event=glCopyTexSubImage2D")!=text.npos,"native bloom copy observation missing");
            check(text.find("MH_ERROR_ALREADY_CREATED")==text.npos,"competing native copy detours");
        }
        if(settings.traceShadowPass) {
            size_t records=0,pos=0;
            while((pos=text.find("frameRecord ",pos))!=text.npos) {++records;++pos;}
            check(records==24+((mode==L"variant7" || settings.traceBloom)?1:0),"capture records plus initial variant change");
            for(unsigned n=1;n<=24;++n) {
                const auto expected="cap="+std::to_string(n)+" variant="+(corrected?"7":"0")+
                    " vp=0,0,32,32 masks=1 widened=3 restricted="+(corrected?"3":"0")+" union=0,360,1920,1080";
                check(text.find(expected)!=text.npos,"mask/draw counters before restriction and frame reset");
            }
        } else check(text.find("frameRecord ")==text.npos,"record must be opt in");
        std::filesystem::remove(logPath);
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
