#include "gl_hooks.h"
#include "video_trace.h"
#include "shadow_trace.h"
#include "capture.h"
#include "log.h"
#include <MinHook.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
template<class T> T proc(HMODULE module,const char* name) {
    const auto p=GetProcAddress(module,name); check(p!=nullptr,name); return reinterpret_cast<T>(p);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) return 2;
        const auto dir=std::filesystem::temp_directory_path()/(L"BigFix-bloom-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir); logInit(dir/L"trace.log");
        Settings s; s.traceBloom=std::wstring_view(argv[2])!=L"off";
        s.traceVideo=std::wstring_view(argv[2])==L"combined" || std::wstring_view(argv[2])==L"coexist";
        s.traceShadowPass=std::wstring_view(argv[2])==L"coexist";
        s.eyeshineFix=s.traceShadowPass; // Isolate bloom except in the coexistence scenario.
        s.enable20PlusPlusAnyGpu=false; s.eyeshineFlickerWorkaround=false;
        s.fixMiscBugs=false; s.videoAspectFix=false; s.capturePng=false;
        const auto gl=LoadLibraryW(argv[1]); check(gl!=nullptr,"stub load failed");
        check(MH_Initialize()==MH_OK,"MinHook init");
        installGlHooks(gl,s,dir); installCaptureHooks(gl,s,dir);
        installShadowTraceHooks(gl,s);
        const auto created=installVideoTraceHooks(gl,VideoModule::OpenGl,s);
        check(created==(s.traceVideo ? 25u : s.traceShadowPass ? 10u : s.traceBloom ? 4u : 0u),"observation hook count");
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        const auto getProc=proc<decltype(GlApi::getProcAddress)>(gl,"wglGetProcAddress");
        const auto bind=reinterpret_cast<decltype(GlApi::bindProgram)>(getProc("glBindProgramARB"));
        const auto upload=reinterpret_cast<decltype(GlApi::programString)>(getProc("glProgramStringARB"));
        check(bind && upload,"ARB functions"); bind(0x8804,7); upload(0x8804,0x8875,9,"123456789");
        proc<decltype(&glBegin)>(gl,"glBegin")(GL_QUADS);
        proc<decltype(&glVertex2f)>(gl,"glVertex2f")(1,2);
        proc<decltype(&glEnd)>(gl,"glEnd")();
        proc<decltype(&glDrawArrays)>(gl,"glDrawArrays")(GL_QUADS,0,4);
        proc<decltype(&glDrawElements)>(gl,"glDrawElements")(GL_QUADS,4,GL_UNSIGNED_SHORT,nullptr);
        proc<decltype(&glCopyTexSubImage2D)>(gl,"glCopyTexSubImage2D")(GL_TEXTURE_2D,0,0,0,0,0,256,256);
        check(proc<unsigned (*)()>(gl,"bigfixTraceCalls")()==105,"original draw/copy calls changed or duplicated");
        check(proc<unsigned (*)()>(gl,"bigfixCopyCalls")()==1,"copy not forwarded once");
        check(proc<unsigned (*)()>(gl,"bigfixIllegalQueries")()==0,"queried GL inside begin/end");
        check(proc<GLenum (*)()>(gl,"bigfixActiveTexture")()==0x84C2,"active texture not restored");
        SetLastError(99);
        check(proc<BOOL (WINAPI*)(HDC)>(gl,"SwapBuffers")(nullptr)==TRUE,"swap return changed");
        check(GetLastError()==1234,"swap last error changed");
        if(s.traceShadowPass) {
            const char source[]="!!ARBfp1.0\n# ShadowMaskTexel NormalMapTexel texture[6]\nEND";
            upload(0x8804,0x8875,sizeof(source)-1,source);
            shadowTraceArm(1);
            proc<decltype(&glDrawArrays)>(gl,"glDrawArrays")(GL_QUADS,0,4);
            proc<decltype(&glCopyTexImage2D)>(gl,"glCopyTexImage2D")(GL_TEXTURE_2D,0,GL_RGBA8,0,0,256,256,0);
            check(proc<BOOL (WINAPI*)(HDC)>(gl,"SwapBuffers")(nullptr)==TRUE,"coexist swap failed");
            check(proc<unsigned (*)()>(gl,"bigfixCopyCalls")()==2,"coexist copies not forwarded once");
            check(proc<unsigned (*)()>(gl,"bigfixIllegalQueries")()==0,"coexist query inside begin/end");
            check(proc<GLenum (*)()>(gl,"bigfixActiveTexture")()==0x84C2,"coexist active texture changed");
        }
        if(std::wstring_view(argv[2])==L"limit") {
            BloomTrace bounded(true);
            for(std::uint64_t i=0;i<10000;++i) bounded.swap(i*1001,nullptr,TRUE);
        }
        logClose();
        std::ifstream in(dir/L"trace.log"); const std::string text{std::istreambuf_iterator<char>(in),{}};
        if(s.traceBloom) {
            check(text.find("sourceCRC=CBF43926")!=std::string::npos,"upload CRC missing");
            check(text.find("fp=7:CBF43926")!=std::string::npos,"bound CRC missing");
            check(text.find("env7=7,2,3,4")!=std::string::npos,"env params missing");
            check(text.find("glCopyTexSubImage2D(target=DE1")!=std::string::npos,"copy target missing");
            check(text.find("tex3=13:256x256:8058")!=std::string::npos,"texture format missing");
            if(s.traceShadowPass) {
                check(text.find("TraceShadowPass")!=std::string::npos,"shadow trace missing");
                check(text.find("class=2")!=std::string::npos,"shadow program classification missing");
                check(text.find("event=glCopyTexImage2D")!=std::string::npos,"bloom shared copy observation missing");
                check(text.find("MH_ERROR_ALREADY_CREATED")==std::string::npos,"competing copy detours");
            }
            if(std::wstring_view(argv[2])==L"limit") {
                check(text.find("TraceBloom limit reached;")!=std::string::npos,"missing process-wide budget cutoff");
                std::size_t count=0,pos=0;
                while((pos=text.find("TraceBloom frame=",pos))!=std::string::npos) {++count;++pos;}
                check(count<=8192,"output exceeded process-wide budget");
            }
        } else check(text.find("TraceBloom")==std::string::npos,"off switch emitted bloom trace");
        std::cout<<"Bloom GL detours, forwarding, CRC, queries, swap state PASS\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
