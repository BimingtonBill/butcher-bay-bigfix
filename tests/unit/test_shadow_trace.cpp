#include <doctest/doctest.h>
#include "shadow_trace.h"
#include "gl_hooks.h"
#include "log.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
GLenum active=0x84C3;
GLint bound=77;
unsigned queries=0, activations=0, uploads=0;
void WINAPI integer(GLenum name, GLint* value) {
    ++queries;
    if (name==GL_VIEWPORT || name==GL_SCISSOR_BOX) { value[0]=0;value[1]=0;value[2]=1600;value[3]=900; }
    else if (name==0x84E0) *value=active;
    else if (name==0x8872) *value=8;
    else if (name==GL_TEXTURE_BINDING_2D) *value=100+active-0x84C0;
    else if (name==GL_DEPTH_BITS) *value=24;
    else *value=0;
}
void WINAPI floating(GLenum,GLfloat* v) { v[0]=0;v[1]=1; }
void WINAPI activate(GLenum value) { active=value;++activations; }
HGLRC WINAPI context() { return reinterpret_cast<HGLRC>(1); }
GLboolean WINAPI isEnabled(GLenum) { return GL_TRUE; }
void WINAPI getProgram(GLenum target,GLenum name,GLint* value) { CHECK(target==0x8804);CHECK(name==0x8677);*value=bound; }
void WINAPI env(GLenum,GLuint index,GLfloat* v) { CHECK(index==4);v[0]=1.0f/2048;v[1]=1.0f/1024;v[2]=v[3]=0; }
void WINAPI level(GLenum,GLint,GLenum name,GLint* value) { *value=name==GL_TEXTURE_WIDTH?2048:name==GL_TEXTURE_HEIGHT?1024:GL_RGBA8; }
void WINAPI parameter(GLenum,GLenum,GLint* value) { *value=GL_NEAREST; }
void WINAPI upload(GLenum,GLenum,GLsizei,const void*) { ++uploads; }
PROC WINAPI resolve(LPCSTR name) {
    if (!std::strcmp(name,"glActiveTextureARB")) return reinterpret_cast<PROC>(&activate);
    if (!std::strcmp(name,"glGetProgramivARB")) return reinterpret_cast<PROC>(&getProgram);
    if (!std::strcmp(name,"glGetProgramEnvParameterfvARB")) return reinterpret_cast<PROC>(&env);
    if (!std::strcmp(name,"glProgramStringARB")) return reinterpret_cast<PROC>(&upload);
    return nullptr;
}
}
TEST_CASE("shadow_trace_is_opt_in_bounded_and_preserves_effective_texture_unit") {
    CHECK_FALSE(Settings{}.traceShadowPass);
    std::vector<std::string> warnings;
    CHECK(parseSettings("[BigFix]\nTraceShadowPass=1",&warnings).traceShadowPass);
    CHECK(warnings.empty());
    ShadowTraceApi api;
    api.integer=&integer;api.floating=&floating;api.context=&context;api.enabled=&isEnabled;
    api.proc=&resolve;api.level=&level;api.parameter=&parameter;
    const auto path=std::filesystem::temp_directory_path()/("BigFix-shadow-"+std::to_string(GetCurrentProcessId())+".log");
    logInit(path);
    configureShadowTrace(api,false);
    queries=activations=uploads=0;
    shadowTraceProgram(0x8804,"ShadowMaskTexel NormalMapTexel texture[6]");
    shadowTraceSwap(TRUE,true);shadowTraceDraw("glBegin",GL_TRIANGLES,3);
    CHECK(queries==0);
    configureShadowTrace(api,true);
    // Trace-only mode must intercept program upload even with every rewrite disabled.
    Settings settings;settings.enable20PlusPlusAnyGpu=false;settings.traceShadowPass=true;
    GlApi gl;gl.getProcAddress=&resolve;
    GlHooks hooks(gl,settings);
    CHECK(hooks.getProcAddress("glProgramStringARB")!=reinterpret_cast<PROC>(&upload));
    const char source[]="ShadowMaskTexel NormalMapTexel texture[6]";
    hooks.programString(0x8804,0x8875,sizeof(source)-1,source);
    CHECK(uploads==1);
    CHECK_FALSE(shadowTraceSampling());
    shadowTraceSwap(FALSE,true);CHECK_FALSE(shadowTraceSampling());
    shadowTraceSwap(TRUE,true);CHECK(shadowTraceSampling());
    for (int f=0;f<4;++f) {
        shadowTraceDraw("glBegin",GL_TRIANGLES,3);
        CHECK(active==0x84C3); // No persistent texture-unit change.
        shadowTraceEvent("glCopyTexSubImage2D test");
        shadowTraceSwap(TRUE,true); // Holding F11 must not rearm forever.
    }
    CHECK_FALSE(shadowTraceSampling());
    CHECK(activations==4*9);
    // Idle frames and F11 must not spend the compact budget before capture.
    for (int frame=0;frame<600;++frame) {
        shadowTraceSwap(TRUE,false);shadowTraceSwap(TRUE,true);
        for(int n=0;n<300;++n) {
            shadowTraceEvent("glBindTexture test");shadowTraceEvent("glCopyPixels test");
            shadowTraceDraw("glDrawElements",GL_TRIANGLES,3);
        }
    }
    for(unsigned n=1;n<=600;++n) {
        shadowTraceCapture(n,true);shadowTraceSwap(TRUE,false);
    }
    configureShadowTrace(api,false);logClose();
    std::ifstream input(path);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
    std::size_t pos=0,count=0;
    while ((pos=text.find("TraceShadowPass ",pos))!=text.npos) { ++count;++pos; }
    CHECK(count==2000);
    CHECK(text.find("2000-line limit reached")!=text.npos);
    CHECK(text.find("detail limit reached")!=text.npos);
    CHECK(text.rfind("frameRecord")>text.find("detail limit reached"));
    CHECK(text.find("class=2")!=text.npos);
    CHECK(text.find("u5=105:2048x1024")!=text.npos);
    CHECK(text.find("u6=106:2048x1024")!=text.npos);
    CHECK(text.find("env4=0.00048828125,0.0009765625")!=text.npos);
    CHECK(text.find("glCopyPixels test")!=text.npos);
    std::filesystem::remove(path);
}

TEST_CASE("shadow_frame_records_only_capture_frames_even_during_F11_and_writer_drain") {
    ShadowTraceApi api;
    api.integer=&integer;api.context=&context;
    const auto path=std::filesystem::temp_directory_path()/("BigFix-shadow-record-"+std::to_string(GetCurrentProcessId())+".log");
    logInit(path);configureShadowTrace(api,true);
    shadowTraceSwap(TRUE,false);
    shadowTraceSwap(TRUE,true);
    shadowTraceSwap(FALSE,true); // failed swap must not consume a frame
    for(int n=0;n<20;++n) {
        if(n<8) shadowTraceCapture(n+1,true); // overlapping F11/burst emits once
        shadowTraceSwap(TRUE,true);
    }
    configureShadowTrace(api,false);logClose();
    std::ifstream input(path);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
    size_t count=0,pos=0;
    while((pos=text.find("frameRecord ",pos))!=text.npos) {++count;++pos;}
    CHECK(count==8);
    CHECK(text.find("frame=2 ")!=text.npos);
    CHECK(text.find("cap=1 variant=0 ")!=text.npos);
    CHECK(text.find("cap=8 variant=0 ")!=text.npos);
    CHECK(text.find("cap=0 ")==text.npos);
    std::filesystem::remove(path);
}

TEST_CASE("burst_records_include_first_capture_counters_union_writer_tail_and_reset") {
    ShadowTraceApi api;api.integer=&integer;api.context=&context;
    const auto path=std::filesystem::temp_directory_path()/("BigFix-shadow-burst-"+std::to_string(GetCurrentProcessId())+".log");
    logInit(path);configureShadowTrace(api,true);
    // Counters precede the capture decision at swap: don't lose the first frame.
    for(unsigned n=1;n<=24;++n) {
        shadowTraceMaskCopy();shadowTraceMaskCopy();
        shadowTraceMaskDraw({0,360,1920,1080},{79,360,1388,114},true);
        shadowTraceMaskDraw({1900,350,100,100},{1900,350,50,50},false);
        shadowTraceMaskDraw({0,0,1,1},{0,0,1,1},false); // equal isn't a mismatch
        shadowTraceCapture(n,true);
        shadowTraceSwap(TRUE,false);
    }
    shadowTraceCapture(0,true);shadowTraceSwap(TRUE,false); // writer draining, no PNG
    shadowTraceSwap(TRUE,false); // idle, no line
    configureShadowTrace(api,false);
    shadowTraceMaskCopy();shadowTraceMaskDraw({0,0,1,1},{0,0,2,2},true);
    shadowTraceCapture(25,true);shadowTraceSwap(TRUE,false);
    logClose();
    std::ifstream input(path);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
    size_t count=0,pos=0;
    while((pos=text.find("frameRecord ",pos))!=text.npos) {++count;++pos;}
    CHECK(count==24);
    CHECK(text.find("frame=0 ")!=text.npos);
    CHECK(text.find("frame=23 ")!=text.npos);
    CHECK(text.find("frame=25 ")==text.npos);
    for(unsigned n=1;n<=24;++n)
        CHECK(text.find("cap="+std::to_string(n)+" variant=0 vp=0,0,1600,900 masks=2 widened=2 restricted=1 union=0,350,2000,1090")!=text.npos);
    CHECK(text.find("cap=0 ")==text.npos);
    std::filesystem::remove(path);
}
