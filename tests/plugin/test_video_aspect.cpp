#include <string>
#include "video_trace.h"
#include "log.h"
#include <MinHook.h>
#include <gl/GL.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
using Fit = void (*)(void*, const VideoRect*, const VideoRect*, const unsigned*, float, const void*);
std::uint8_t* image;
Fit videoCall;
unsigned picture[]{640,480};
float inputAspect=1;
VideoRect recorded{}, incoming{};
float recordedAspect;
unsigned calls=0;
bool otherCaller=false;
std::array<int,6> clipContext{}; // offset, then actual clipping bounds; NOT a RECT
const unsigned color=0x80808080;
void* utilVtable[64]{};
struct Object { void** vtable; } util{utilVtable}, renderer{utilVtable};
struct Context { void** vtable=nullptr; char pad[0x3c]{}; float screen=1, pixel=1; } context;
void* getRenderer(void*) { return &renderer; }
void* getContext(void*) { return &context; }
void rectangle(void*,const VideoRect* clip,const VideoRect* dst,const void* c) {
    check(clip==reinterpret_cast<VideoRect*>(clipContext.data()) && c==&color,"clip/color ABI changed");
    recorded=*dst;
}
// Model the original 39AD0 arithmetic (including truncation), independently of
// the production rectangle helper. Its vtable submission is the observed F8.
void fit(void* u,const VideoRect* clip,const VideoRect* dst,const unsigned* size,float aspect,const void* c) {
    ++calls; incoming=*dst; recordedAspect=aspect;
    check(size==picture,"picture pointer changed");
    const float ratio=aspect/(context.screen*context.pixel);
    float x=0,y=0;
    if(ratio<1) x=(dst->right-dst->left)*(1-ratio)*0.5f;
    else y=(dst->bottom-dst->top)*(1-1/ratio)*0.5f;
    const VideoRect out{int(dst->left+x),int(dst->top+y),int(dst->right-x),int(dst->bottom-y)};
    reinterpret_cast<void (*)(void*,const VideoRect*,const VideoRect*,const void*)>(utilVtable[0xf8/8])(u,clip,&out,c);
}
void paint(void*,void* u) {
    const VideoRect dst{0,0,640,480};
    const auto call=otherCaller ? reinterpret_cast<Fit>(image+0x39ad0) : videoCall;
    call(u,reinterpret_cast<VideoRect*>(clipContext.data()),&dst,picture,inputAspect,&color);
}
void seed(const VideoHookSite& site,void* handler,unsigned stackSize) {
    auto* target=image+site.rva; DWORD old;
    check(VirtualProtect(target,64,PAGE_EXECUTE_READWRITE,&old)!=0,"seed protection");
    std::memcpy(target,site.expected.data(),site.expected.size());
    auto* p=target+site.expected.size();
    const unsigned char add[]{0x48,0x81,0xc4}; std::memcpy(p,add,3);std::memcpy(p+3,&stackSize,4);
    p[7]=0x48;p[8]=0xb8;std::memcpy(p+9,&handler,8);p[17]=0xff;p[18]=0xe0;
}
void seedVideoCaller() {
    // Preserve the six Windows x64 arguments and CALL at 7645D, returning at
    // the recorded 76463 discriminator. This exercises the actual detour gate.
    auto* p=image+0x76440; DWORD old;
    check(VirtualProtect(p,80,PAGE_EXECUTE_READWRITE,&old)!=0,"caller protection");
    const unsigned char prefix[]{0x48,0x83,0xec,0x38,
        0x48,0x8b,0x44,0x24,0x60,0x48,0x89,0x44,0x24,0x20,
        0x48,0x8b,0x44,0x24,0x68,0x48,0x89,0x44,0x24,0x28};
    std::memcpy(p,prefix,sizeof(prefix));std::memset(p+sizeof(prefix),0x90,29-sizeof(prefix));
    const unsigned char call[]{0xff,0x15,0x1d,0,0,0};std::memcpy(image+0x7645d,call,6);
    const unsigned char ret[]{0x48,0x83,0xc4,0x38,0xc3};std::memcpy(image+0x76463,ret,5);
    auto* target=image+0x39ad0;std::memcpy(image+0x76480,&target,8);
    videoCall=reinterpret_cast<Fit>(p);
}
bool equal(VideoRect a,VideoRect b) { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
void prepareFrame(int w,int h) {
    glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0,0,w,h);glClearColor(.2f,.3f,.4f,.5f);glColorMask(0,1,0,1);
    glScissor(1,2,3,4);glEnable(GL_SCISSOR_TEST);
}
void verifyClear(bool fixed) {
    unsigned char rgb[3]{};glReadPixels(0,0,1,1,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    // Microsoft's software OpenGL 1.1 (CI runners have no GPU) does not honour the
    // scissored clear the same way; the pixel check needs a real driver.
    const auto renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if(renderer && std::string(renderer)=="GDI Generic") std::cout<<"pixel check skipped on GDI Generic\n";
    else check(rgb[0]==(fixed?0:255) && rgb[1]==rgb[0] && rgb[2]==rgb[0],"black clear missing or settings-off mutated buffer");
    GLfloat c[4]{};GLboolean mask[4]{};GLint scissor[4]{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE,c);glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetIntegerv(GL_SCISSOR_BOX,scissor);
    check(c[0]==.2f && c[1]==.3f && c[2]==.4f && c[3]==.5f,"clear color leaked");
    check(!mask[0] && mask[1] && !mask[2] && mask[3],"color mask leaked");
    check(glIsEnabled(GL_SCISSOR_TEST) && scissor[0]==1 && scissor[1]==2 && scissor[2]==3 && scissor[3]==4,"scissor leaked");
    check(glGetError()==GL_NO_ERROR,"GL error injected");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) return 2;
        const std::wstring mode=argv[2];
        const bool fixed=mode!=L"off" && mode!=L"off_trace", trace=mode==L"trace" || mode==L"off_trace";
        const auto dir=std::filesystem::temp_directory_path()/(L"BigFix-aspect-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);logInit(dir/L"BigFix.log");
        image=reinterpret_cast<std::uint8_t*>(LoadLibraryW(argv[1]));check(image!=nullptr,"fixture load");
        for(const auto& s:videoHookSites()) {
            if(std::string(s.name)=="video_paint") seed(s,reinterpret_cast<void*>(&paint),0x278);
            if(std::string(s.name)=="aspect_fit") seed(s,reinterpret_cast<void*>(&fit),0xb8);
            if(std::string(s.name)=="rectangle_submit") seed(s,reinterpret_cast<void*>(&rectangle),s.rva==0x38e90?0x118:0x48);
        }
        seedVideoCaller();FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        utilVtable[0x50/8]=reinterpret_cast<void*>(&getRenderer);utilVtable[0x48/8]=reinterpret_cast<void*>(&getContext);
        utilVtable[0xf8/8]=image+0x38e90;
        WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BigFixAspectTest";
        check(RegisterClassW(&wc)!=0,"window class");
        const auto window=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        check(window!=nullptr,"hidden GL window");const auto dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=24;
        check(SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd)!=0,"pixel format");
        const auto rc=wglCreateContext(dc);check(rc && wglMakeCurrent(dc,rc),"GL context");
        check(MH_Initialize()==MH_OK,"MinHook init");
        const auto settings=parseSettings(std::string("VideoAspectFix=")+(fixed?"1":"0")+"\nTraceVideo="+(trace?"1":"0"));
        const auto count=installVideoTraceHooks(reinterpret_cast<HMODULE>(image),VideoModule::World,settings);
        check(count==(trace?3u:fixed?2u:0u),"video fix hooks not installed independently of TraceVideo");
        installVideoTraceHooks(GetModuleHandleW(L"opengl32.dll"),VideoModule::OpenGl,settings);
        if(count) check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable");
        struct Case { int w,h; unsigned pw,ph;float aspect;VideoRect want; };
        for(const auto& c:{Case{2560,1440,640,480,1,{80,0,560,480}},
            Case{2560,1440,720,486,1,{80,0,560,480}},
            Case{2560,1440,640,360,4.f/3,{0,0,640,480}},
            Case{1920,1200,640,480,1,{53,0,586,480}},
            Case{1920,1200,640,360,4.f/3,{0,24,640,456}},
            Case{2520,1080,640,480,1,{137,0,502,480}},
            Case{1440,1080,640,360,4.f/3,{0,60,640,420}}}) {
            picture[0]=c.pw;picture[1]=c.ph;inputAspect=c.aspect;prepareFrame(c.w,c.h);
            reinterpret_cast<void (*)(void*,void*)>(image+0x755d0)(nullptr,&util);
            if(fixed) check(equal(recorded,c.want),"recorded video geometry not corrected");
            else check(equal(incoming,{0,0,640,480}) && recordedAspect==inputAspect,"settings off changed geometry/aspect");
            verifyClear(fixed);videoTraceSwap(dc,TRUE);
        }
        // Stale/non-unit engine aspect is cancelled locally, never overwritten.
        context.screen=1.5f;context.pixel=1.25f;picture[0]=640;picture[1]=480;inputAspect=1;
        prepareFrame(2560,1440);reinterpret_cast<void (*)(void*,void*)>(image+0x755d0)(nullptr,&util);
        if(fixed) check(equal(recorded,{80,0,560,480}),"display units not cancelled");
        check(context.screen==1.5f && context.pixel==1.25f,"global display aspect changed");verifyClear(fixed);
        // Shared helper from menus (no paint), and another caller in paint,
        // must forward unchanged and must not clear.
        context.screen=context.pixel=1;const VideoRect dst{0,0,640,480};
        prepareFrame(2560,1440);videoCall(&util,reinterpret_cast<VideoRect*>(clipContext.data()),&dst,picture,1,&color);
        check(equal(incoming,dst) && recordedAspect==1,"non-video fit changed");verifyClear(false);
        otherCaller=true;prepareFrame(2560,1440);
        reinterpret_cast<void (*)(void*,void*)>(image+0x755d0)(nullptr,&util);
        check(equal(incoming,dst) && recordedAspect==1,"another paint caller changed");verifyClear(false);
        check(calls==10,"fit not forwarded exactly once");
        MH_Uninitialize();wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);ReleaseDC(window,dc);DestroyWindow(window);
        logClose();std::filesystem::remove_all(dir);
        std::cout<<"Video aspect: recorded geometry, off, isolation, black every frame, GL state PASS\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
