#include "shadow_trace.h"
#include "bloom_trace.h"
#include "eyeshine_fix.h"
#include "eyeshine_variants.h"
#include "log.h"
#include <MinHook.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace {
ShadowTraceApi api;
std::atomic<bool> enabled{false};
std::atomic<unsigned> lines{0};
std::atomic<unsigned> detailLines{0};
std::mutex programsMutex;
enum class Program { Other, Shadow, SpecNormal, ScreenFx };
std::map<std::pair<HGLRC, GLint>, Program> programs;
struct Frame {
    unsigned long long number = 0;
    unsigned remaining = 0, events = 0, copies = 0, binds = 0, draws = 0, fxDraws = 0, states = 0, stencilDraws = 0, textureBinds = 0, skippedEvents = 0, skippedDraws = 0;
    bool keyDown = false;
    unsigned maskCopies=0, widened=0, restricted=0, regionPasses=0, regionCopies=0;
    int lastVariant=0;
    std::uint64_t capture=0;
    std::array<std::int64_t,4> widenedUnion{};
};
thread_local Frame frame;
constexpr GLenum fragmentProgram = 0x8804, activeTexture = 0x84E0, texture0 = 0x84C0;
template<class T> T proc(const char* name) {
    const auto p = api.proc ? api.proc(name) : nullptr;
    const auto n = reinterpret_cast<std::uintptr_t>(p);
    return n > 3 && n != ~std::uintptr_t(0) ? reinterpret_cast<T>(p) : nullptr;
}
void write(const char* text, bool compact=false) noexcept {
    if(!compact) {
        const auto detail=detailLines.fetch_add(1);
        if(detail>=1500) return;
        if(detail==1499) text="detail limit reached; compact frame records retain reserved budget";
    }
    const auto line=lines.fetch_add(1);
    if (line >= 2000) return;
    if (line==1999) text="2000-line limit reached; restart for a fresh trace";
    try { logf("TraceShadowPass frame=%llu ctx=%p dc=%p %s", frame.number,
        api.context ? api.context() : nullptr, api.dc ? api.dc() : nullptr, text); } catch (...) {}
}
bool ready() { return enabled && api.integer && api.context && api.context(); }
GLint integer(GLenum name) { GLint value = 0; api.integer(name, &value); return value; }
GLfloat scalar(GLenum name) { GLfloat value[4]{};if(api.floating) api.floating(name,value);return value[0]; }
void fragmentState(const char* where) {
    GLboolean mask[4]{};GLfloat raster[4]{},color[4]{},rasterColor[4]{},projection[16]{};
    GLint sc[4]{};api.integer(GL_SCISSOR_BOX,sc);
    if(api.boolean) api.boolean(GL_COLOR_WRITEMASK,mask);
    if(api.floating) {
        api.floating(GL_CURRENT_RASTER_POSITION,raster);api.floating(GL_CURRENT_COLOR,color);
        api.floating(GL_CURRENT_RASTER_COLOR,rasterColor);api.floating(GL_PROJECTION_MATRIX,projection);
    }
    const auto on=[](GLenum cap){return api.enabled ? api.enabled(cap):0;};
    char text[2300];
    std::snprintf(text,sizeof(text),
        "state@%s variant=%d zoom=%.7g,%.7g rasterValid=%d raster=%.7g,%.7g,%.7g,%.7g rasterRGBA=%.7g,%.7g,%.7g,%.7g color=%.7g,%.7g,%.7g,%.7g colorMask=%d%d%d%d "
        "stencil=%d func=%X ref=%X valueMask=%X writeMask=%X op=%X,%X,%X clear=%X depth=%d func=%X write=%d "
        "alpha=%d func=%X ref=%.7g blend=%d srcDst=%X,%X logic=%d dither=%d sc=%d:%d,%d,%d,%d "
        "pixelScale=%.7g,%.7g,%.7g,%.7g bias=%.7g,%.7g,%.7g,%.7g depthScaleBias=%.7g,%.7g indexShiftOffset=%d,%d mapColorStencil=%d,%d proj=%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g",
        where,eyeshineVariant(),scalar(GL_ZOOM_X),scalar(GL_ZOOM_Y),integer(GL_CURRENT_RASTER_POSITION_VALID),raster[0],raster[1],raster[2],raster[3],
        rasterColor[0],rasterColor[1],rasterColor[2],rasterColor[3],color[0],color[1],color[2],color[3],mask[0],mask[1],mask[2],mask[3],
        on(GL_STENCIL_TEST),integer(GL_STENCIL_FUNC),integer(GL_STENCIL_REF),integer(GL_STENCIL_VALUE_MASK),integer(GL_STENCIL_WRITEMASK),
        integer(GL_STENCIL_FAIL),integer(GL_STENCIL_PASS_DEPTH_FAIL),integer(GL_STENCIL_PASS_DEPTH_PASS),integer(GL_STENCIL_CLEAR_VALUE),
        on(GL_DEPTH_TEST),integer(GL_DEPTH_FUNC),integer(GL_DEPTH_WRITEMASK),on(GL_ALPHA_TEST),integer(GL_ALPHA_TEST_FUNC),scalar(GL_ALPHA_TEST_REF),
        on(GL_BLEND),integer(GL_BLEND_SRC),integer(GL_BLEND_DST),on(GL_COLOR_LOGIC_OP),on(GL_DITHER),on(GL_SCISSOR_TEST),sc[0],sc[1],sc[2],sc[3],
        scalar(GL_RED_SCALE),scalar(GL_GREEN_SCALE),scalar(GL_BLUE_SCALE),scalar(GL_ALPHA_SCALE),scalar(GL_RED_BIAS),scalar(GL_GREEN_BIAS),scalar(GL_BLUE_BIAS),scalar(GL_ALPHA_BIAS),
        scalar(GL_DEPTH_SCALE),scalar(GL_DEPTH_BIAS),integer(GL_INDEX_SHIFT),integer(GL_INDEX_OFFSET),integer(GL_MAP_COLOR),integer(GL_MAP_STENCIL),
        projection[0],projection[1],projection[2],projection[3],projection[4],projection[5],projection[6],projection[7],
        projection[8],projection[9],projection[10],projection[11],projection[12],projection[13],projection[14],projection[15]);
    write(text);
}
GLint programId() {
    const auto get = proc<void (WINAPI*)(GLenum, GLenum, GLint*)>("glGetProgramivARB");
    GLint id = 0;
    if (get) get(fragmentProgram, 0x8677, &id); // PROGRAM_BINDING_ARB is a program query.
    return id;
}
Program programKind(GLint id) {
    const std::lock_guard<std::mutex> lock(programsMutex);
    const auto it = programs.find({api.context(), id});
    return it == programs.end() ? Program::Other : it->second;
}
void drawState(const char* kind, GLenum mode, GLsizei count, GLint id, Program program) {
    GLint vp[4]{}, sc[4]{};
    api.integer(GL_VIEWPORT, vp); api.integer(GL_SCISSOR_BOX, sc);
    const auto active = integer(activeTexture);
    const auto activate = proc<void (WINAPI*)(GLenum)>("glActiveTextureARB");
    const auto getEnv = proc<void (WINAPI*)(GLenum, GLuint, GLfloat*)>("glGetProgramEnvParameterfvARB");
    GLfloat uv[4]{};
    if (getEnv) getEnv(fragmentProgram, 4, uv);
    char textures[1100]{};
    std::size_t used = 0;
    // Fragment sampler slots include units beyond the fixed-function limit.
    const auto units = (std::min)(8, (std::max)(0, integer(0x8872)));
    if (activate && active >= texture0 && active < texture0 + 32) {
        for (int unit = 0; unit < units; ++unit) {
            activate(texture0 + unit);
            const auto tex = integer(GL_TEXTURE_BINDING_2D);
            const auto cube = integer(0x8514); // TEXTURE_BINDING_CUBE_MAP_ARB; required by the game.
            GLint w=0,h=0,format=0,min=0,mag=0,s=0,t=0;
            if (tex && api.level && api.parameter) {
                api.level(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
                api.level(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
                api.level(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
                api.parameter(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min);
                api.parameter(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&mag);
                api.parameter(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&s);
                api.parameter(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&t);
            }
            const auto n = std::snprintf(textures+used,sizeof(textures)-used,
                " u%d=%d:%dx%d/fmt%X/filter%X,%X/wrap%X,%X/cube%d",unit,tex,w,h,format,min,mag,s,t,cube);
            if (n < 0 || static_cast<std::size_t>(n) >= sizeof(textures)-used) break;
            used += n;
        }
        activate(active); // Restore server active texture, including when no texture is bound.
    }
    GLfloat range[2]{};
    if (api.floating) api.floating(GL_DEPTH_RANGE,range);
    char text[1800];
    std::snprintf(text,sizeof(text),
        "%s mode=%X count=%d program=%d class=%d vp=%d,%d,%d,%d sc=%d:%d,%d,%d,%d read=%X draw=%X active=%X depthBits=%d depthRange=%.8g,%.8g env4=%.8g,%.8g,%.8g,%.8g%s",
        kind,mode,count,id,int(program),vp[0],vp[1],vp[2],vp[3],
        api.enabled ? api.enabled(GL_SCISSOR_TEST) : 0,sc[0],sc[1],sc[2],sc[3],
        integer(GL_READ_BUFFER),integer(GL_DRAW_BUFFER),active,integer(GL_DEPTH_BITS),range[0],range[1],uv[0],uv[1],uv[2],uv[3],textures);
    write(text);
    fragmentState(kind);
}
decltype(&glCopyTexSubImage2D) copySub = nullptr;
decltype(&glCopyTexImage2D) copyImage = nullptr;
decltype(&glCopyPixels) copyPixels = nullptr;
decltype(&glDrawBuffer) drawBuffer = nullptr;
decltype(&glReadBuffer) readBuffer = nullptr;
void copyState(const char* kind, GLenum target, GLint level, GLint dx, GLint dy,
               GLint x, GLint y, GLsizei w, GLsizei h, GLenum format) noexcept {
    if (!shadowTraceSampling()) return;
    if (frame.copies++ >= 24) { ++frame.skippedEvents; return; }
    GLint vp[4]{},sc[4]{}; api.integer(GL_VIEWPORT,vp); api.integer(GL_SCISSOR_BOX,sc);
    GLfloat raster[4]{},scale=0,bias=0;
    if(api.floating) { api.floating(GL_CURRENT_RASTER_POSITION,raster);api.floating(GL_DEPTH_SCALE,&scale);api.floating(GL_DEPTH_BIAS,&bias); }
    char text[900];
    std::snprintf(text,sizeof(text),"%s target=%X level=%d dst=%d,%d src=%d,%d size=%dx%d format/type=%X unit=%X tex2D=%d read=%X draw=%X vp=%d,%d,%d,%d sc=%d:%d,%d,%d,%d rasterValid=%d raster=%.8g,%.8g,%.8g,%.8g depthScaleBias=%.8g,%.8g",
        kind,target,level,dx,dy,x,y,w,h,format,integer(activeTexture),integer(GL_TEXTURE_BINDING_2D),
        integer(GL_READ_BUFFER),integer(GL_DRAW_BUFFER),vp[0],vp[1],vp[2],vp[3],
        api.enabled ? api.enabled(GL_SCISSOR_TEST) : 0,sc[0],sc[1],sc[2],sc[3],integer(GL_CURRENT_RASTER_POSITION_VALID),raster[0],raster[1],raster[2],raster[3],scale,bias);
    write(text);
    fragmentState(kind);
}
void WINAPI copySubHook(GLenum t,GLint l,GLint dx,GLint dy,GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    char event[160]{};
    std::snprintf(event,sizeof(event),"glCopyTexSubImage2D(target=%X,level=%d,dst=%d,%d,src=%d,%d,size=%dx%d)",t,l,dx,dy,x,y,w,h);
    bloomTraceDraw(event,_ReturnAddress());
    eyeshineFixCopy(t,l,dx,dy,x,y,w,h);
    eyeshineVariantTexture(t,l,dx,dy,x,y,w,h);
    copyState("glCopyTexSubImage2D",t,l,dx,dy,x,y,w,h,0); copySub(t,l,dx,dy,x,y,w,h);
    eyeshineCopyTexture(t,l,dx,dy,x,y,w,h);
}
void WINAPI copyImageHook(GLenum t,GLint l,GLenum f,GLint x,GLint y,GLsizei w,GLsizei h,GLint b) noexcept {
    char event[160]{};
    std::snprintf(event,sizeof(event),"glCopyTexImage2D(target=%X,level=%d,internal=%X,src=%d,%d,size=%dx%d)",t,l,f,x,y,w,h);
    bloomTraceDraw(event,_ReturnAddress());
    eyeshineOtherCopy(t,l);
    eyeshineVariantOtherCopy();
    copyState("glCopyTexImage2D",t,l,0,0,x,y,w,h,f); copyImage(t,l,f,x,y,w,h,b);
}
void WINAPI copyPixelsHook(GLint x,GLint y,GLsizei w,GLsizei h,GLenum t) noexcept {
    copyState("glCopyPixels",0,0,0,0,x,y,w,h,t);
    const auto restore=eyeshineVariantBeforePixels(t);
    eyeshineFixBeforePixels(t);
    if(restore) copyState("glCopyPixels effective variant",0,0,0,0,x,y,w,h,t);
    copyPixels(x,y,w,h,t);
    eyeshineFixAfterDraw();
    eyeshineVariantAfterPixels(restore,x,y,w,h,t);
}
void WINAPI drawBufferHook(GLenum b) noexcept { shadowTraceEvent("glDrawBuffer %X",b); drawBuffer(b); }
void WINAPI readBufferHook(GLenum b) noexcept { shadowTraceEvent("glReadBuffer %X",b); readBuffer(b); }
// Export hooks are owned here once. Effective snapshots above also catch
// changes restored by glPopAttrib or extension entry points (WindowPos, ATI).
#define STATE1(name,T,fmt) decltype(&name) original_##name=nullptr; \
    void WINAPI trace_##name(T a) noexcept {shadowTraceEvent(#name " " fmt,a);original_##name(a);}
#define STATE2(name,T,U,fmt) decltype(&name) original_##name=nullptr; \
    void WINAPI trace_##name(T a,U b) noexcept {shadowTraceEvent(#name " " fmt,a,b);original_##name(a,b);}
#define STATE3(name,T,U,V,fmt) decltype(&name) original_##name=nullptr; \
    void WINAPI trace_##name(T a,U b,V c) noexcept {shadowTraceEvent(#name " " fmt,a,b,c);original_##name(a,b,c);}
STATE1(glStencilMask,GLuint,"%X")
STATE3(glStencilFunc,GLenum,GLint,GLuint,"%X ref=%X mask=%X")
STATE3(glStencilOp,GLenum,GLenum,GLenum,"%X,%X,%X")
STATE2(glPixelZoom,GLfloat,GLfloat,"%.7g,%.7g")
STATE2(glPixelTransferf,GLenum,GLfloat,"%X=%.7g")
STATE2(glPixelTransferi,GLenum,GLint,"%X=%d")
STATE2(glRasterPos2i,GLint,GLint,"%d,%d")
STATE2(glRasterPos2f,GLfloat,GLfloat,"%.7g,%.7g")
STATE2(glAlphaFunc,GLenum,GLclampf,"%X ref=%.7g")
STATE2(glBlendFunc,GLenum,GLenum,"%X,%X")
decltype(&glColorMask) originalColorMask=nullptr;
void WINAPI colorMaskHook(GLboolean r,GLboolean g,GLboolean b,GLboolean a) noexcept {
    shadowTraceEvent("glColorMask %d%d%d%d",r,g,b,a);originalColorMask(r,g,b,a);
}
#undef STATE1
#undef STATE2
#undef STATE3
}

void configureShadowTrace(const ShadowTraceApi& value, bool on) noexcept {
    api=value;
    if(on && !enabled.exchange(on)) {frame={};lines=0;detailLines=0;}
    enabled=on;
}
bool shadowTraceSampling() noexcept { return enabled && frame.remaining && detailLines.load()<1500 && lines.load()<2000 && ready(); }
void shadowTraceMaskCopy() noexcept {if(enabled) ++frame.maskCopies;}
void shadowTraceRegionPass() noexcept {if(enabled) ++frame.regionPasses;}
void shadowTraceRegionCopy() noexcept {if(enabled) ++frame.regionCopies;}
void shadowTraceMaskDraw(const std::array<GLint,4>& current,const std::array<GLint,4>& copied,bool restricted) noexcept {
    if(!enabled) return;
    if(restricted) ++frame.restricted;
    if(current==copied) return;
    ++frame.widened;
    if(current[2]<=0 || current[3]<=0) return;
    auto& u=frame.widenedUnion;
    const std::int64_t right=std::int64_t(current[0])+current[2],top=std::int64_t(current[1])+current[3];
    if(!u[2] || !u[3]) {u={current[0],current[1],current[2],current[3]};return;}
    const auto x=(std::min)(u[0],std::int64_t(current[0])),y=(std::min)(u[1],std::int64_t(current[1]));
    u={x,y,(std::max)(u[0]+u[2],right)-x,(std::max)(u[1]+u[3],top)-y};
}
void shadowTraceCapture(std::uint64_t index,bool) noexcept {
    if(!enabled) return;
    if(index) frame.capture=index;
}
void shadowTraceEvent(const char* format, ...) noexcept {
    if (!shadowTraceSampling()) return;
    // Copy/viewport events cannot be crowded out by material texture/program binds.
    if(std::strncmp(format,"glBindTexture",13)==0) {
        if(frame.textureBinds++>=4) {++frame.skippedEvents;return;}
    } else {
        const bool priority=std::strncmp(format,"glTex",5)!=0;
        if(priority ? frame.states++>=48 : frame.events++>=16) {++frame.skippedEvents;return;}
    }
    char text[1500]; va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
    write(text);
}
void shadowTraceProgram(GLenum target, std::string_view source) noexcept {
    if (!ready() || target != fragmentProgram) return;
    try {
        const auto id=programId();
        const bool shadow=source.find("ShadowMaskTexel")!=source.npos && source.find("texture[6]")!=source.npos;
        const auto kind=shadow ? (source.find("NormalMapTexel")!=source.npos ? Program::SpecNormal : Program::Shadow) :
            source.find("post processing effects")!=source.npos ? Program::ScreenFx : Program::Other;
        { const std::lock_guard<std::mutex> lock(programsMutex); programs[{api.context(),id}]=kind; }
        if (kind != Program::Other) {
            char text[180]; std::snprintf(text,sizeof(text),"upload program=%d class=%d bytes=%zu nv=%d (class 1=shadow 2=SpecNormal 3=ScreenFX)",
                id,int(kind),source.size(),source.find("NV_fragment_program2")!=source.npos); write(text);
        }
    } catch (...) {}
}
void shadowTraceDraw(const char* kind, GLenum mode, GLsizei count) noexcept {
    eyeshineVariantDraw();
    if (!shadowTraceSampling()) return;
    try {
        if (!api.enabled) return;
        // Volume and stencil-to-alpha draws often use the fixed pipeline.
        // Query before glBegin so diagnostics never cause illegal GL calls.
        if (api.enabled(GL_STENCIL_TEST) && frame.stencilDraws++<8) fragmentState("stencil-draw");
        if (!api.enabled(fragmentProgram)) return;
        const auto id=programId(); const auto type=programKind(id);
        if (type==Program::Other) return;
        if (type==Program::ScreenFx ? frame.fxDraws++ >= 4 : frame.draws++ >= 8) { ++frame.skippedDraws; return; }
        drawState(kind,mode,count,id,type);
    } catch (...) {}
}
void shadowTraceProgramBind(GLenum target, GLuint id) noexcept {
    if (!shadowTraceSampling() || target!=fragmentProgram) return;
    try {
        const auto kind=programKind(id);
        if (kind==Program::Other) return;
        if (frame.binds++ >= 8) { ++frame.skippedEvents; return; }
        char text[120];std::snprintf(text,sizeof(text),"glBindProgramARB target=%X program=%u class=%d",target,id,int(kind));write(text);
    } catch (...) {}
}
void shadowTraceClear(GLbitfield mask) noexcept {
    if (!shadowTraceSampling()) return;
    GLint sc[4]{};GLfloat color[4]{};GLboolean writeMask[4]{};
    api.integer(GL_SCISSOR_BOX,sc);
    if(api.floating) api.floating(GL_COLOR_CLEAR_VALUE,color);
    if(api.boolean) api.boolean(GL_COLOR_WRITEMASK,writeMask);
    shadowTraceEvent("glClear mask=%X rgba=%.7g,%.7g,%.7g,%.7g write=%d%d%d%d sc=%d:%d,%d,%d,%d stencilWrite=%X stencilClear=%X",
        mask,color[0],color[1],color[2],color[3],writeMask[0],writeMask[1],writeMask[2],writeMask[3],
        api.enabled ? api.enabled(GL_SCISSOR_TEST) : 0,sc[0],sc[1],sc[2],sc[3],integer(GL_STENCIL_WRITEMASK),integer(GL_STENCIL_CLEAR_VALUE));
}
void shadowTraceSwap(BOOL result, bool trigger) noexcept {
    if (!enabled || !result) return;
    if(frame.capture || frame.lastVariant!=eyeshineVariant()) {
        GLint vp[4]{};if(ready()) api.integer(GL_VIEWPORT,vp);
        const auto& u=frame.widenedUnion;
        char text[420];std::snprintf(text,sizeof(text),
            "frameRecord cap=%llu variant=%d vp=%d,%d,%d,%d masks=%u widened=%u restricted=%u union=%lld,%lld,%lld,%lld regionPasses=%u regionCopies=%u",
            static_cast<unsigned long long>(frame.capture),eyeshineVariant(),vp[0],vp[1],vp[2],vp[3],
            frame.maskCopies,frame.widened,frame.restricted,u[0],u[1],u[2],u[3],frame.regionPasses,frame.regionCopies);
        write(text,true);
    }
    frame.lastVariant=eyeshineVariant();
    frame.capture=0;
    frame.maskCopies=frame.widened=frame.restricted=0;frame.widenedUnion={};
    frame.regionPasses=frame.regionCopies=0;
    if (frame.remaining) {
        char text[240]; std::snprintf(text,sizeof(text),"end events=%u copies=%u stateEvents=%u textureBinds=%u programBinds=%u shadowDraws=%u stencilDraws=%u fxDraws=%u omittedEvents=%u omittedDraws=%u",
            frame.events,frame.copies,frame.states,frame.textureBinds,frame.binds,frame.draws,frame.stencilDraws,frame.fxDraws,frame.skippedEvents,frame.skippedDraws); write(text);
        --frame.remaining;
    }
    ++frame.number;
    if (trigger && !frame.keyDown) frame.remaining=4;
    frame.keyDown=trigger;
    frame.events=frame.copies=frame.binds=frame.draws=frame.fxDraws=frame.skippedEvents=frame.skippedDraws=0;
    frame.states=frame.textureBinds=frame.stencilDraws=0;
}
void shadowTraceArm(unsigned frames) noexcept {
    if(enabled) frame.remaining=frames;
}
void installShadowTraceHooks(HMODULE module, const Settings& settings) {
    const bool shadow = settings.traceShadowPass || settings.eyeshineFix || settings.eyeshineFixVariant || settings.eyeshineAbBurst;
    if (!shadow && !settings.traceBloom) return;
    configureEyeshineVariants(module,settings);
    configureEyeshineFix(module,settings.eyeshineFix);
    ShadowTraceApi value;
#define RESOLVE(member, name) value.member=reinterpret_cast<decltype(value.member)>(GetProcAddress(module,name))
    RESOLVE(integer,"glGetIntegerv"); RESOLVE(floating,"glGetFloatv"); RESOLVE(boolean,"glGetBooleanv");
    RESOLVE(enabled,"glIsEnabled"); RESOLVE(level,"glGetTexLevelParameteriv"); RESOLVE(parameter,"glGetTexParameteriv");
    RESOLVE(context,"wglGetCurrentContext"); RESOLVE(dc,"wglGetCurrentDC"); RESOLVE(proc,"wglGetProcAddress");
#undef RESOLVE
    configureShadowTrace(value,settings.traceShadowPass);
    struct Hook { const char* name; void* hook; void** original; };
    const Hook hooks[]{
        {"glCopyTexSubImage2D",reinterpret_cast<void*>(&copySubHook),reinterpret_cast<void**>(&copySub)},
        {"glCopyTexImage2D",reinterpret_cast<void*>(&copyImageHook),reinterpret_cast<void**>(&copyImage)},
        {"glCopyPixels",reinterpret_cast<void*>(&copyPixelsHook),reinterpret_cast<void**>(&copyPixels)},
        {"glDrawBuffer",reinterpret_cast<void*>(&drawBufferHook),reinterpret_cast<void**>(&drawBuffer)},
        {"glReadBuffer",reinterpret_cast<void*>(&readBufferHook),reinterpret_cast<void**>(&readBuffer)}};
    bool copiesReady=true;
    for (const auto& h:hooks) {
        if (!shadow && h.original!=reinterpret_cast<void**>(&copySub) &&
            h.original!=reinterpret_cast<void**>(&copyImage)) continue;
        const bool copy=h.original==reinterpret_cast<void**>(&copySub) ||
            h.original==reinterpret_cast<void**>(&copyPixels) || h.original==reinterpret_cast<void**>(&copyImage);
        if (!settings.traceShadowPass && !copy) continue;
        const auto target=GetProcAddress(module,h.name);
        if (!target) { if(copy) copiesReady=false; continue; }
        const auto result=MH_CreateHook(reinterpret_cast<void*>(target),h.hook,h.original);
        if (result!=MH_OK) {
            if(copy) copiesReady=false;
            try { logf("Shadow copy hook %s failed: %s",h.name,MH_StatusToString(result)); } catch (...) {}
        }
    }
    if(settings.traceShadowPass) {
#define STATE_HOOK(name) Hook{#name,reinterpret_cast<void*>(&trace_##name),reinterpret_cast<void**>(&original_##name)}
        const Hook states[]{STATE_HOOK(glStencilMask),STATE_HOOK(glStencilFunc),STATE_HOOK(glStencilOp),
            STATE_HOOK(glPixelZoom),STATE_HOOK(glPixelTransferf),STATE_HOOK(glPixelTransferi),
            STATE_HOOK(glRasterPos2i),STATE_HOOK(glRasterPos2f),STATE_HOOK(glAlphaFunc),STATE_HOOK(glBlendFunc),
            {"glColorMask",reinterpret_cast<void*>(&colorMaskHook),reinterpret_cast<void**>(&originalColorMask)}};
#undef STATE_HOOK
        for(const auto& h:states) {
            const auto target=GetProcAddress(module,h.name);
            if(!target) continue;
            const auto result=MH_CreateHook(reinterpret_cast<void*>(target),h.hook,h.original);
            if(result!=MH_OK) {try{logf("Shadow state hook %s failed: %s",h.name,MH_StatusToString(result));}catch(...) {}}
        }
    }
    if(!copiesReady && eyeshineVariantsEnabled()) {
        configureEyeshineVariants(module,Settings{});
        logLineNoThrow("EyeshineFixVariant unavailable: incomplete copy hook set");
    }
    if(!copiesReady) {
        disableEyeshineFix();
        logLineNoThrow("EyeshineFix skipped: incomplete copy hook set");
    }
    if(settings.traceShadowPass) write("ready: F11 arms four detailed frames; compact records only for cap>0 or variant change; F9 cycles variants and always logs selection; 1500 detail + 500 compact lines/process; no glGetError");
}
