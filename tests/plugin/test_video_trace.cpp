#include "video_trace.h"
#include "capture.h"
#include "log.h"
#include <MinHook.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
template<class T> T proc(HMODULE m,const char* name) { auto p=GetProcAddress(m,name); check(p!=nullptr,name); return reinterpret_cast<T>(p); }
std::uint8_t* image=nullptr;
HMODULE gl=nullptr;
unsigned starts=0,closes=0,paints=0,fits=0,rects=0,dispatches=0;
bool floodBackground=false;
std::array<unsigned char,0x400> decoder{};
std::array<unsigned char,0x100> entry{};
std::array<unsigned char,0x250> object{};
void* stringVtable[20]{};
void* utilVtable[64]{};
struct Context { void** vtable; char pad[0x3c]; float screen=4.0f/3, pixel=1; } context{};
struct Receiver { void** vtable; } util{utilVtable}, renderer{utilVtable};
const char* getText(void* s) { return *reinterpret_cast<const char**>(static_cast<char*>(s)+8); }
void* rendererGetter(void*) { return &renderer; }
void* contextGetter(void*) { return &context; }
template<class T> void put(void* p,std::size_t offset,T value) { std::memcpy(static_cast<char*>(p)+offset,&value,sizeof(value)); }

void render() {
    proc<decltype(&glViewport)>(gl,"glViewport")(0,0,1920,1080);
    proc<decltype(&glScissor)>(gl,"glScissor")(240,0,1440,1080);
    proc<decltype(&glMatrixMode)>(gl,"glMatrixMode")(GL_PROJECTION);
    GLfloat m[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    proc<decltype(&glLoadMatrixf)>(gl,"glLoadMatrixf")(m);
    proc<decltype(&glOrtho)>(gl,"glOrtho")(0,640,480,0,-1,1);
    proc<decltype(&glClear)>(gl,"glClear")(GL_COLOR_BUFFER_BIT);
    proc<decltype(&glBindTexture)>(gl,"glBindTexture")(GL_TEXTURE_2D,42);
    proc<decltype(&glTexImage2D)>(gl,"glTexImage2D")(GL_TEXTURE_2D,0,GL_LUMINANCE,1024,512,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,nullptr);
    proc<decltype(&glTexSubImage2D)>(gl,"glTexSubImage2D")(GL_TEXTURE_2D,0,0,0,512,256,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,nullptr);
    proc<decltype(&glBegin)>(gl,"glBegin")(GL_QUADS);
    proc<decltype(&glTexCoord2f)>(gl,"glTexCoord2f")(0,0);
    proc<decltype(&glVertex2f)>(gl,"glVertex2f")(80,0);
    proc<decltype(&glVertex2i)>(gl,"glVertex2i")(560,0);
    proc<decltype(&glVertex3f)>(gl,"glVertex3f")(560,480,0);
    GLfloat v[3]{80,480,0}; proc<decltype(&glVertex3fv)>(gl,"glVertex3fv")(v);
    proc<decltype(&glEnd)>(gl,"glEnd")();
    const auto binder=reinterpret_cast<void (WINAPI*)(GLenum,GLuint)>(proc<PROC (WINAPI*)(LPCSTR)>(gl,"wglGetProcAddress")("glBindProgramARB"));
    binder(0x8804,77);
    proc<decltype(&glDrawArrays)>(gl,"glDrawArrays")(GL_QUADS,0,4);
    proc<decltype(&glDrawElements)>(gl,"glDrawElements")(GL_QUADS,4,GL_UNSIGNED_SHORT,nullptr);
}
bool start(void* d,void*) {
    ++starts;
    const unsigned fields[]{640,480,640,480,0,0,30,1,1777,1000};
    std::memcpy(static_cast<char*>(d)+0x240,fields,sizeof(fields));
    put(d,0x2f0,static_cast<unsigned char>(1)); return true;
}
void close(void* d) { ++closes; put(d,0x2f0,static_cast<unsigned char>(0)); }
void open(void*) { reinterpret_cast<bool (*)(void*,void*)>(image+0xefff0)(decoder.data(),nullptr); }
void dispatch(void*,std::uintptr_t,const VideoRect*,const VideoRect*,const void*,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c) {
    check(a==11 && b==22 && c==33,"dispatch stack arguments changed"); ++dispatches; render();
}
void rectangle(void* u,const VideoRect* clip,const VideoRect* dst,const void* color) {
    ++rects;
    reinterpret_cast<void (*)(void*,std::uintptr_t,const VideoRect*,const VideoRect*,const void*,std::uintptr_t,std::uintptr_t,std::uintptr_t)>(image+0x3cd50)(u,42,clip,dst,color,11,22,33);
}
void fit(void* u,const VideoRect* clip,const VideoRect*,const unsigned* size,float aspect,const void* color) {
    check(aspect==1 && size[0]==640 && size[1]==480 && *static_cast<const unsigned*>(color)==0xff808080,"fit ABI changed"); ++fits;
    const VideoRect fitted{80,0,560,480};
    reinterpret_cast<void (*)(void*,const VideoRect*,const VideoRect*,const void*)>(image+0x38e90)(u,clip,&fitted,color);
}
void paint(void*,void* u) {
    ++paints;
    if(floodBackground) for(int i=0;i<40;++i) proc<decltype(&glScissor)>(gl,"glScissor")(0,0,1920,1080);
    const VideoRect clip{},dst{0,0,640,480}; const unsigned size[]{640,480},color=0xff808080;
    reinterpret_cast<void (*)(void*,const VideoRect*,const VideoRect*,const unsigned*,float,const void*)>(image+0x39ad0)(u,&clip,&dst,size,1,&color);
}
void seed(const VideoHookSite& site,void* handler,unsigned stackSize) {
    auto* target=image+site.rva; DWORD previous=0;
    check(VirtualProtect(target,64,PAGE_EXECUTE_READWRITE,&previous)!=0,"fixture protection");
    std::memcpy(target,site.expected.data(),site.expected.size());
    auto* code=target+site.expected.size();
    // Undo only the fixture prefix's stack allocation, then tail-call the fake.
    const unsigned char add[]{0x48,0x81,0xc4}; std::memcpy(code,add,3); std::memcpy(code+3,&stackSize,4);
    code[7]=0x48;code[8]=0xb8; std::memcpy(code+9,&handler,8);code[17]=0xff;code[18]=0xe0;
    check(FlushInstructionCache(GetCurrentProcess(),target,64)!=0,"fixture cache");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4) return 2;
        const std::wstring mode=argv[3]; const bool on=mode!=L"off";
        floodBackground=mode==L"priority";
        const auto dir=std::filesystem::temp_directory_path()/(L"BigFix-video-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir); logInit(dir/L"BigFix.log");
        const auto fixture=LoadLibraryW(argv[1]); check(fixture!=nullptr,"fixture load"); image=reinterpret_cast<std::uint8_t*>(fixture);
        gl=LoadLibraryW(argv[2]); check(gl!=nullptr,"GL fixture load");
        void* handlers[]{reinterpret_cast<void*>(&start),reinterpret_cast<void*>(&close),reinterpret_cast<void*>(&open),reinterpret_cast<void*>(&paint),reinterpret_cast<void*>(&fit),reinterpret_cast<void*>(&rectangle),reinterpret_cast<void*>(&dispatch)};
        const unsigned stackSizes[]{0x228,0x58,0x88,0x278,0xb8,0x118,0x3c8};
        const auto& sites=videoHookSites(); check(sites.size()==7,"fixture site count");
        for(std::size_t i=0;i<sites.size();++i) seed(sites[i],handlers[i],stackSizes[i]);
        stringVtable[9]=reinterpret_cast<void*>(&getText);
        put(entry.data(),0x28,stringVtable); put(entry.data(),0x30,"Intro.ogg"); put(entry.data(),0x18,decoder.data());
        put(entry.data(),0x68,101u);put(entry.data(),0x6c,102u);put(entry.data(),0x70,103u);
        put(object.data(),0x1d0,stringVtable);put(object.data(),0x1d8,"*VIDEO_Intro.ogg");put(object.data(),0x238,1.0f);
        utilVtable[0x50/8]=reinterpret_cast<void*>(&rendererGetter); utilVtable[0x48/8]=reinterpret_cast<void*>(&contextGetter);
        utilVtable[0x100/8]=image+0x39ad0;utilVtable[0x120/8]=image+0x3cd50;
        check(MH_Initialize()==MH_OK,"MinHook init");
        Settings settings; settings.traceVideo=on; settings.videoAspectFix=false;
        settings.eyeshineFix=false; // Isolate trace export ownership.
        if(mode==L"mismatch") for(const auto& s:sites) image[s.rva]^=1;
        const auto system=installVideoTraceHooks(fixture,VideoModule::System,settings);
        const auto world=installVideoTraceHooks(fixture,VideoModule::World,settings);
        check(system==(on && mode!=L"mismatch" ? 3u:0u),"system hook count");
        check(world==(on && mode!=L"mismatch" ? 4u:0u),"world hook count");
        if(!on || mode==L"mismatch") for(const auto& s:sites)
            check(MH_EnableHook(image+s.rva)==MH_ERROR_NOT_CREATED,"off/mismatch installed an internal hook");
        const auto exports=installVideoTraceHooks(gl,VideoModule::OpenGl,settings);
        check(exports==(on ? 25u:0u),"GL hook count");
        if(!on) check(MH_EnableHook(reinterpret_cast<void*>(GetProcAddress(gl,"glViewport")))==MH_ERROR_NOT_CREATED,"off installed GL hook");
        if(mode==L"mismatch") { MH_Uninitialize();logClose();std::filesystem::remove_all(dir);return 0; }
        installGlHooks(gl,settings,dir);
        installCaptureHooks(gl,settings,dir); check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
        const auto swap=proc<BOOL (WINAPI*)(HDC)>(gl,"SwapBuffers");
        const auto callPaint=reinterpret_cast<void (*)(void*,void*)>(image+0x755d0);
        reinterpret_cast<void (*)(void*)>(image+0xf1e50)(entry.data());
        callPaint(object.data(),&util);
        // A deferred draw after paint returned must be labelled active=0.
        proc<decltype(&glDrawArrays)>(gl,"glDrawArrays")(GL_QUADS,0,4);
        check(swap(nullptr)!=0 && GetLastError()==1234,"swap forwarding or LastError changed");
        // Move beyond the initial burst, then end the video on an unsampled
        // frame. Its first menu draws must still be retained at the swap.
        for(int i=0;i<5;++i) { callPaint(object.data(),&util);swap(nullptr); }
        const GLfloat vertices[]{80,0,560,0,560,480,80,480},uv[]{0,0,1,0,1,1,0,1};
        const GLushort indices[]{0,1,2,3};
        proc<decltype(&glVertexPointer)>(gl,"glVertexPointer")(2,GL_FLOAT,0,vertices);
        proc<decltype(&glTexCoordPointer)>(gl,"glTexCoordPointer")(2,GL_FLOAT,0,uv);
        proc<decltype(&glDrawElements)>(gl,"glDrawElements")(GL_QUADS,4,GL_UNSIGNED_SHORT,indices);swap(nullptr); // menu frame
        reinterpret_cast<void (*)(void*)>(image+0xf0630)(decoder.data());
        check(starts==1 && closes==1 && paints==6 && fits==6 && rects==6 && dispatches==6,"original functions not forwarded exactly once");
        check(proc<unsigned (*)()>(gl,"bigfixIllegalQueries")()==0,"diagnostic GL query inside begin/end");
        check(proc<GLuint (*)()>(gl,"bigfixBoundProgram")()==77,"program bind not forwarded");
        const auto binder=reinterpret_cast<void (WINAPI*)(GLenum,GLuint)>(proc<PROC (WINAPI*)(LPCSTR)>(gl,"wglGetProcAddress")("glBindProgramARB"));
        proc<void (*)(ULONG_PTR)>(gl,"bigfixSetContext")(2);binder(0x8804,9);
        // With tracing off the driver pointer is returned unchanged; only the
        // trace wrapper re-resolves a previously returned pointer per context.
        check(proc<GLuint (*)()>(gl,"bigfixBoundProgram")()==(on ? 109u:9u),"program bind dispatch changed");
        proc<void (*)(ULONG_PTR)>(gl,"bigfixSetContext")(1);
        if(mode==L"limit") for(int i=0;i<5000;++i) {
            reinterpret_cast<void (*)(void*)>(image+0xf1e50)(entry.data());callPaint(object.data(),&util);swap(nullptr);
        }
        std::ifstream input(dir/L"BigFix.log"); const std::string text{std::istreambuf_iterator<char>(input),{}}; input.close();
        if(on) {
            if(mode==L"priority") {
                for(const auto word:{"paint entry","paint exit","fit entry","fit exit","rect=80,0,560,480","rectangle_dispatch"})
                    check(text.find(word)!=std::string::npos,word);
                MH_Uninitialize();logClose();std::filesystem::remove_all(dir);return 0;
            }
            for(const auto word:{"decoder_start entry","picture=640x480","rawPAR=1777/1000","file=Intro.ogg","engineTex=101,102,103","decoder_close entry",
                "paint entry","paint exit","fit entry","PIXELASPECT=1","screen(+44)=1.333333","rectangle_submit kind=fitted_video","rect=80,0,560,480","rectangle_dispatch",
                "glViewport","glScissor","glLoadMatrixf","glOrtho","glClear","glTexImage2D","size=1024x512","glTexSubImage2D","glBegin","glEnd",
                "glDrawArrays","glDrawElements","vertex 80,0,0","texcoord 0,0","stack=","SwapBuffers","video=0","program bind target=8804 id=77","rgba=0,0,0,1 write=1111",
                "clientVertex index=0 readable=1","clientUV index=3 readable=1"})
                check(text.find(word)!=std::string::npos,word);
            const auto closeAt=text.find("decoder_close entry");
            check(text.substr(closeAt,text.find('\n',closeAt)-closeAt).find("file=Intro.ogg")!=std::string::npos,"decoder close lost owner identity");
            std::size_t count=0,pos=0; while((pos=text.find("VideoTrace",pos))!=std::string::npos) {++count;pos+=10;}
            check(count<=2000,"trace line limit exceeded");
            if(mode==L"limit") check(text.find("2000-line limit reached")!=std::string::npos,"missing bounded trace notice");
        } else check(text.find("VideoTrace")==std::string::npos,"off wrote video diagnostics");
        MH_Uninitialize(); logClose();std::filesystem::remove_all(dir);
        std::wcout<<L"Video trace "<<mode<<L": forwarding, ABI, scope, bounds PASS\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
