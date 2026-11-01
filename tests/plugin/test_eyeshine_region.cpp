// Real GL stencil/alpha/copy pixels through the production MinHook boundaries.
// No game module is loaded; the small drawable stands in for a 1440p frame.
#include "eyeshine_fix.h"
#include "eyeshine_variants.h"
#include "shadow_trace.h"
#include "gl_hooks.h"
#include "video_trace.h"
#include "log.h"
#include <MinHook.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
template<class T> T get(HMODULE m,const char* n) {auto p=GetProcAddress(m,n);check(p!=nullptr,n);return reinterpret_cast<T>(p);}
EyeshineRect scissor() {EyeshineRect r{};glGetIntegerv(GL_SCISSOR_BOX,r.data());return r;}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==3,"fixture and scenario");
        const std::wstring mode=argv[2];
        const bool fixed=mode==L"on" || mode==L"variant8";
        auto log=std::filesystem::temp_directory_path()/(L"BigFix-region-"+std::to_wstring(GetCurrentProcessId())+L".log");logInit(log);
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.style=CS_OWNDC;cls.lpszClassName=L"BigFixRegion";
        check(RegisterClassW(&cls)!=0,"register");auto win=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,cls.hInstance,nullptr);
        auto dc=GetDC(win);PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_SUPPORT_OPENGL|PFD_DRAW_TO_WINDOW;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cAlphaBits=8;p.cDepthBits=24;p.cStencilBits=8;
        auto pf=ChoosePixelFormat(dc,&p);check(pf && SetPixelFormat(dc,pf,&p),"pixel format");auto rc=wglCreateContext(dc);check(rc && wglMakeCurrent(dc,rc),"context");
        auto gl=GetModuleHandleW(L"opengl32.dll"),fixture=LoadLibraryW(argv[1]);check(fixture!=nullptr,"fixture");
        const auto arrays=get<decltype(&glDrawArrays)>(fixture,"glDrawArrays");
        const auto begin=get<decltype(&glBegin)>(fixture,"glBegin");const auto end=get<decltype(&glEnd)>(fixture,"glEnd");
        const auto recorded=get<void(*)(GLint*)>(fixture,"bigfixRecordedScissor");
        Settings s;s.eyeshineFix=mode!=L"off";s.traceShadowPass=mode==L"variant8" || mode==L"variant0";s.eyeshineFixVariant=mode==L"variant8"?8:0;
        s.eyeshineAbBurst=mode==L"variant8"?8:0;
        s.videoAspectFix=false;s.enable20PlusPlusAnyGpu=false;s.fixMiscBugs=false;
        check(MH_Initialize()==MH_OK,"MinHook");installGlHooks(gl,s,{});installShadowTraceHooks(gl,s);installVideoTraceHooks(fixture,VideoModule::OpenGl,s);
        check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"hooks");
        const EyeshineRect region{0,8,24,24},light{8,8,4,4};
        const auto box=[](const EyeshineRect& r){glScissor(r[0],r[1],r[2],r[3]);};
        // Populate stencil and RGB while full resolution is active (fix must be idle).
        glViewport(0,0,32,32);glDisable(GL_SCISSOR_TEST);glClearColor(.25f,.5f,.75f,.25f);glClearStencil(127);glStencilMask(255);glClearDepth(.375);
        glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);glScissor(16,0,16,32);glClearStencil(128);glClear(GL_STENCIL_BUFFER_BIT);
        GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        std::array<unsigned char,32*32*4> pixels{};pixels.fill(63);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glViewport(0,8,24,24);box(light);glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_LEQUAL,128,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        glDisable(GL_DEPTH_TEST);glColorMask(0,0,0,1);glColor4f(0,0,0,0);
        // Attribute restoration and a list can change the box without glScissor.
        glPushAttrib(GL_SCISSOR_BIT);box(region);glPopAttrib();
        arrays(GL_POINTS,0,0);EyeshineRect observed{};recorded(observed.data());
        if(observed!=(fixed?region:light)) std::cerr<<"observed "<<observed[0]<<','<<observed[1]<<','<<observed[2]<<','<<observed[3]<<" variant "<<eyeshineVariant()<<" fixed "<<fixed<<'\n';
        check(observed==(fixed?region:light),"stencil/alpha draw must cover region");check(scissor()==light,"draw state restore");
        begin(GL_POINTS);end();check(glGetError()==GL_NO_ERROR,"Begin/End queries");
        const auto list=glGenLists(1);glNewList(list,GL_COMPILE);glScissor(8,8,4,4);glEndList();box(region);glCallList(list);check(scissor()==light,"list seed");
        glDisable(GL_SCISSOR_TEST);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,8,8,8,8,4,4);
        check(!glIsEnabled(GL_SCISSOR_TEST) && scissor()==light,"copy state restore");
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(fixed) {
            for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
                const auto a=pixels[(y*32+x)*4+3];
                check(a==(y>=8 && x<24 ? (x<16?255:0):63),"full mask pixels, including outside old light bounds");
            }
            glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(int i=0;i<32*32;++i) check(pixels[i*4]>=63 && pixels[i*4]<=64 && pixels[i*4+1]>=127 && pixels[i*4+1]<=128 && pixels[i*4+2]>=191 && pixels[i*4+2]<=192,"RGB preserved by alpha replay");
            std::array<unsigned char,32*32> stencil{};glReadPixels(0,0,32,32,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,stencil.data());
            for(int y=0;y<32;++y) for(int x=0;x<32;++x) check(stencil[y*32+x]==(x<16?127:128),"stencil preserved");
            GLfloat depth=0;glReadPixels(20,20,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);check(depth>.37499f && depth<.37501f,"depth preserved");
        } else check(pixels[(20*32+4)*4+3]==63,"baseline copy changed");
        // The game's mask uses a different texture origin. Expanding source
        // must translate destination by the same delta (including positive y).
        std::array<unsigned char,64*64*4> shifted{};shifted.fill(63);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,shifted.data());
        arrays(GL_POINTS,0,0);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,12,24,8,8,4,4);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,shifted.data());
        if(fixed) for(int y=0;y<64;++y) for(int x=0;x<64;++x)
            check(shifted[(y*64+x)*4+3]==(y>=24 && y<48 && x>=4 && x<28 ? (x<20?255:0):63),"expanded copy preserves nonzero origin mapping");
        // SpecNormal must use the same region, and restore a disabled/narrow
        // engine scissor. The binding is queried on u5, independent of copy unit.
        const auto activate=reinterpret_cast<void(WINAPI*)(GLenum)>(wglGetProcAddress("glActiveTextureARB"));
        const auto gen=reinterpret_cast<void(WINAPI*)(GLsizei,GLuint*)>(wglGetProcAddress("glGenProgramsARB"));
        const auto bind=reinterpret_cast<void(WINAPI*)(GLenum,GLuint)>(wglGetProcAddress("glBindProgramARB"));
        const auto upload=reinterpret_cast<void(WINAPI*)(GLenum,GLenum,GLsizei,const void*)>(wglGetProcAddress("glProgramStringARB"));
        check(activate && gen && bind && upload,"ARB fixture requirements");
        GLuint program=0;gen(1,&program);bind(0x8804,program);
        const char* source="!!ARBfp1.0\n# ShadowMaskTexel NormalMapTexel texture[6]\nMOV result.color, {1,1,1,1};\nEND";
        upload(0x8804,0x8875,GLsizei(std::strlen(source)),source);glEnable(0x8804);glDisable(GL_STENCIL_TEST);
        activate(0x84C5);glBindTexture(GL_TEXTURE_2D,texture);activate(0x84C3);
        glEnable(GL_SCISSOR_TEST);box(light);arrays(GL_POINTS,0,0);recorded(observed.data());
        check(observed==(fixed?region:light) && scissor()==light,"lighting region and restoration");
        const auto elements=get<decltype(&glDrawElements)>(fixture,"glDrawElements");
        elements(GL_POINTS,0,GL_UNSIGNED_SHORT,nullptr);recorded(observed.data());check(observed==(fixed?region:light),"indexed lighting region");
        // Geometry-only lists run under the widened effective state. A list
        // that changes state retains that state for the next queried draw.
        const auto callList=get<decltype(&glCallList)>(fixture,"glCallList");
        glNewList(list,GL_COMPILE);glBegin(GL_POINTS);glEnd();glEndList();
        callList(list);recorded(observed.data());check(observed==(fixed?region:light) && scissor()==light,"display-list region");
        glNewList(list,GL_COMPILE);glScissor(4,8,3,3);glEndList();callList(list);check(scissor()==EyeshineRect{4,8,3,3},"state-list result preserved");
        arrays(GL_POINTS,0,0);recorded(observed.data());check(observed==(fixed?region:EyeshineRect{4,8,3,3}),"effective state after display list");
        while(glGetError()!=GL_NO_ERROR) {}
        glNewList(list,GL_COMPILE);glVertex2f(0,0);glEndList();
        glBegin(GL_POINTS);glCallList(list);glEnd();const auto rawError=glGetError();
        begin(GL_POINTS);callList(list);end();const auto hookedError=glGetError();
        if(rawError||hookedError) std::cerr<<"raw "<<rawError<<" hooked "<<hookedError<<"\n";
        check(hookedError==rawError,"vertex list inside Begin/End");
        glDisable(GL_SCISSOR_TEST);glNewList(list,GL_COMPILE);glEnable(GL_SCISSOR_TEST);glScissor(5,9,2,2);glEndList();
        callList(list);check(glIsEnabled(GL_SCISSOR_TEST) && scissor()==EyeshineRect{5,9,2,2},"state list keeps its scissor enable");
        glEnable(GL_SCISSOR_TEST);box(EyeshineRect{4,8,3,3});
        if(mode==L"variant8") {
            for(int n=0;n<600;++n) {shadowTraceCapture(0,true);shadowTraceSwap(TRUE,false);}
            check(eyeshineAbBegin(8) && eyeshineVariant()==0,"A/B baseline arm");
            eyeshineAbSwap(TRUE);check(eyeshineVariant()==0,"no completed sample must not advance");
            for(unsigned n=0;n<8;++n) {
                check(eyeshineVariant()==(n%2?8:0),"A/B whole-frame variant");
                box(light);arrays(GL_POINTS,0,0);recorded(observed.data());
                check(observed==(n%2?region:light),"A/B variant affects draws before capture");
                eyeshineAbCapture(101+n);shadowTraceCapture(101+n,true);shadowTraceSwap(TRUE,false);
                eyeshineVariantSwap(TRUE,true); // F9 is ignored throughout the burst.
                eyeshineAbSwap(TRUE);
            }
            check(eyeshineVariant()==8,"A/B restores configured selection");
            check(eyeshineAbBegin(8),"A/B rearm");eyeshineAbSwap(FALSE);check(eyeshineVariant()==8,"A/B failed-swap rollback");
        }
        glDisable(0x8804);glEnable(GL_STENCIL_TEST);glStencilMask(255);glClearStencil(0);box(light);
        get<decltype(&glClear)>(fixture,"glClear")(GL_STENCIL_BUFFER_BIT);
        unsigned char cleared=255;glReadPixels(2,20,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&cleared);
        check(cleared==(fixed?0:127) && scissor()==light,"per-light stencil clear region");
        check(glGetError()==GL_NO_ERROR,"GL errors");
        MH_Uninitialize();wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);ReleaseDC(win,dc);DestroyWindow(win);logClose();
        if(mode==L"variant8") {
            std::ifstream in(log);const std::string text{std::istreambuf_iterator<char>(in),{}};in.close();
            for(unsigned n=0;n<8;++n) {
                const auto label="cap="+std::to_string(101+n)+" variant="+(n%2?"8":"0");
                check(text.find("EyeshineAbBurst "+label)!=text.npos,"unbudgeted capture variant label");
                check(text.find("frameRecord "+label)!=text.npos,"frameRecord uses rendered variant");
            }
            size_t records=0,pos=0;while((pos=text.find("frameRecord ",pos))!=text.npos) {++records;++pos;}
            check(records<=10,"idle/writer swaps exhausted compact reserve");
        }
        std::filesystem::remove(log);return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
