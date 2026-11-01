#include "gl_hooks.h"
#include "shadow_trace.h"
#include "video_trace.h"
#include "log.h"
#include <MinHook.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
template<class T> T proc(HMODULE m,const char* name) {
    const auto p=GetProcAddress(m,name);check(p!=nullptr,name);return reinterpret_cast<T>(p);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==3,"arguments");
        const std::wstring mode=argv[2];
        const bool on=mode!=L"off";
        const auto path=std::filesystem::temp_directory_path()/(L"BigFix-shadow-plugin-"+std::to_wstring(GetCurrentProcessId())+L".log");
        logInit(path);
        const auto gl=LoadLibraryW(argv[1]);check(gl!=nullptr,"load stub");
        proc<void (*)()>(gl,"bigfixShadowFixture")();
        check(MH_Initialize()==MH_OK,"MinHook init");
        Settings settings;settings.traceShadowPass=on;settings.traceVideo=mode==L"coexist";
        settings.eyeshineFix=false; // This fixture isolates trace installation.
        settings.enable20PlusPlusAnyGpu=false;settings.fixMiscBugs=false;settings.videoAspectFix=false;
        installGlHooks(gl,settings,{});installShadowTraceHooks(gl,settings);
        const auto count=installVideoTraceHooks(gl,VideoModule::OpenGl,settings);
        check(count==(settings.traceVideo?25u:on?10u:0u),"shared export count");
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        const auto get=proc<PROC (WINAPI*)(LPCSTR)>(gl,"wglGetProcAddress");
        reinterpret_cast<void (WINAPI*)(GLenum,GLuint)>(get("glBindProgramARB"))(0x8804,77);
        const char source[]="!!ARBfp1.0\n# ShadowMaskTexel NormalMapTexel texture[6]\nEND";
        reinterpret_cast<void (WINAPI*)(GLenum,GLenum,GLsizei,const void*)>(get("glProgramStringARB"))(0x8804,0x8875,sizeof(source)-1,source);
        shadowTraceSwap(TRUE,true);
        for(int i=0;i<200;++i) proc<decltype(&glBindTexture)>(gl,"glBindTexture")(GL_TEXTURE_2D,42);
        shadowTraceEvent("glStencilFunc reserved-after-bind-flood");
        reinterpret_cast<void (WINAPI*)(GLenum,GLuint)>(get("glBindProgramARB"))(0x8804,77);
        proc<decltype(&glViewport)>(gl,"glViewport")(0,0,1600,900);
        proc<decltype(&glCopyTexSubImage2D)>(gl,"glCopyTexSubImage2D")(GL_TEXTURE_2D,0,0,0,0,0,1600,900);
        proc<decltype(&glCopyTexImage2D)>(gl,"glCopyTexImage2D")(GL_TEXTURE_2D,0,GL_RGBA8,0,0,1600,900,0);
        proc<decltype(&glCopyPixels)>(gl,"glCopyPixels")(0,0,1600,900,0x886E);
        proc<decltype(&glBegin)>(gl,"glBegin")(GL_TRIANGLES);
        proc<decltype(&glEnd)>(gl,"glEnd")();
        proc<decltype(&glDrawArrays)>(gl,"glDrawArrays")(GL_TRIANGLES,0,3);
        proc<decltype(&glDrawElements)>(gl,"glDrawElements")(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,nullptr);
        shadowTraceSwap(TRUE,false);
        check(proc<unsigned (*)()>(gl,"bigfixCopyCalls")()==3,"copies not forwarded once");
        check(proc<unsigned (*)()>(gl,"bigfixIllegalQueries")()==0,"query inside begin/end");
        check(proc<GLenum (*)()>(gl,"bigfixActiveTexture")()==0x84C3,"active texture changed");
        check(proc<GLuint (*)()>(gl,"bigfixBoundProgram")()==77,"program changed");
        logClose();std::ifstream input(path);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
        if(on) for(const auto word:{"class=2","glBindProgramARB target=8804 program=77 class=2","glCopyTexSubImage2D","glCopyTexImage2D","glCopyPixels","format/type=886E","glDrawElements","glBegin","u5=105:2048x1024","u6=106:2048x1024", "glStencilFunc reserved-after-bind-flood", "state@glCopyPixels", "zoom=", "rasterRGBA=", "colorMask=", "stencil=", "alpha=", "blend=", "mapColorStencil="})
            check(text.find(word)!=text.npos,word);
        else check(text.find("TraceShadowPass")==text.npos,"disabled trace output");
        MH_Uninitialize();std::filesystem::remove(path);
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
