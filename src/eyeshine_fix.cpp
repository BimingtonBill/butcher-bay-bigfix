#include "eyeshine_fix.h"
#include "eyeshine_variants.h"
#include "eyeshine_mask.h"
#include "shadow_trace.h"
#include "log.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <cmath>

std::optional<EyeshineRect> eyeshineReducedRegion(const EyeshineRect& v,GLint w,GLint h) noexcept {
    // GameWorld+5870, SbzEngine+39950: top-left anchored view, uniformly
    // reduced between .75 and 1, with each dimension rounded down to 4 pixels.
    if(w<=0 || h<=0 || v[0]!=0 || v[1]<=0 || v[2]<=0 || v[3]<=0 ||
       v[2]>=w || v[3]>=h || std::int64_t(v[1])+v[3]!=h ||
       v[2]<4*(std::int64_t(w)*3/16) || v[3]<4*(std::int64_t(h)*3/16) ||
       std::abs(std::int64_t(v[2])*h-std::int64_t(v[3])*w)>4*std::int64_t((std::max)(w,h))) return {};
    return v;
}

std::optional<EyeshineRect> eyeshineRectIntersection(const EyeshineRect& a,const EyeshineRect& b) noexcept {
    if(a[2]<=0 || a[3]<=0 || b[2]<=0 || b[3]<=0) return {};
    const auto x=(std::max)(a[0],b[0]),y=(std::max)(a[1],b[1]);
    const auto right=(std::min)(std::int64_t(a[0])+a[2],std::int64_t(b[0])+b[2]);
    const auto top=(std::min)(std::int64_t(a[1])+a[3],std::int64_t(b[1])+b[3]);
    if(right<=x || top<=y) return {};
    return EyeshineRect{x,y,GLint(right-x),GLint(top-y)};
}
void EyeshineScissorTracker::copy(GLint texture,const EyeshineRect& rect) {
    if(!texture) return;
    if(rect[2]>0 && rect[3]>0) copies[texture]=rect;
    else copies.erase(texture);
}
std::optional<EyeshineRect> EyeshineScissorTracker::draw(GLint texture,bool scissorOn,const EyeshineRect& current) const noexcept {
    const auto it=copies.find(texture);
    if(it==copies.end()) return {};
    // Empty overlap must reject every fragment, rather than leave the draw unbounded.
    const auto intersection=eyeshineRectIntersection(current,it->second).value_or(EyeshineRect{0,0,0,0});
    if(scissorOn && intersection==current) return {};
    return intersection;
}

void EyeshineCopyTracker::pixels(HGLRC current,GLint sx,GLint sy,GLsizei w,GLsizei h,GLenum type) noexcept {
    context=type==0x886E && current && w>0 && h>0 ? current : nullptr;
    x=sx;y=sy;width=w;height=h;
}
bool EyeshineCopyTracker::texture(HGLRC current,GLenum target,GLint level,GLint sx,GLint sy,GLsizei w,GLsizei h) noexcept {
    const bool match=context && current==context && target==GL_TEXTURE_2D && level==0 &&
        x==sx && y==sy && width==w && height==h;
    context=nullptr;return match;
}

namespace {
bool enabled=false;
decltype(&wglGetCurrentContext) currentContext=nullptr;
decltype(&wglGetCurrentDC) currentDC=nullptr;
decltype(&wglGetProcAddress) getProc=nullptr;
decltype(&glGetIntegerv) integer=nullptr;
decltype(&glGetBooleanv) queryBoolean=nullptr;
decltype(&glGetFloatv) queryFloat=nullptr;
decltype(&glGetTexLevelParameteriv) queryLevel=nullptr;
decltype(&glIsEnabled) isEnabled=nullptr;
decltype(&glScissor) scissor=nullptr;
decltype(&glEnable) enable=nullptr;
decltype(&glDisable) disable=nullptr;
struct Context { EyeshineScissorTracker masks;std::set<GLint> programs;EyeshineConversion conversion; };
std::map<HGLRC,Context> contexts;
std::mutex mutex;
struct Restore { HGLRC context=nullptr;EyeshineRect rect{};bool scissorOn=false;EyeshineRect applied{}; };
thread_local Restore restore;
thread_local bool replaying=false,expandedCopy=false;
std::atomic<bool> reported{false};
std::atomic<bool> diagnosticReported{false};
std::atomic<bool> unavailableReported{false};
constexpr GLenum fp=0x8804,activeTexture=0x84E0,texture0=0x84C0;
template<class T> T proc(const char* name) {
    const auto p=getProc(name);const auto n=reinterpret_cast<std::uintptr_t>(p);
    return n>3 && n!=~std::uintptr_t(0) ? reinterpret_cast<T>(p) : nullptr;
}
GLint value(GLenum name) { GLint n=0;integer(name,&n);return n; }
GLint programId() {
    auto query=proc<void(WINAPI*)(GLenum,GLenum,GLint*)>("glGetProgramivARB");
    GLint id=0;if(query) query(fp,0x8677,&id);return id;
}
bool selected() {return enabled && eyeshineVariantsEnabled() && eyeshineVariant()==7;}
bool wholeRegionSelected() {return enabled && (!eyeshineVariantsEnabled() || eyeshineVariant()==8);}
std::optional<EyeshineRect> reducedRegion() {
    if(!currentContext() || replaying) return {};
    RECT client{};const auto window=WindowFromDC(currentDC());
    if(!window || !GetClientRect(window,&client)) return {};
    EyeshineRect vp{};integer(GL_VIEWPORT,vp.data());
    return eyeshineReducedRegion(vp,client.right-client.left,client.bottom-client.top);
}
void regionScope(const EyeshineRect& r) {
    EyeshineRect old{};integer(GL_SCISSOR_BOX,old.data());
    const bool on=isEnabled(GL_SCISSOR_TEST)!=GL_FALSE;
    if(on && old==r) return;
    restore={currentContext(),old,on,r};scissor(r[0],r[1],r[2],r[3]);
    if(!on) enable(GL_SCISSOR_TEST);
    shadowTraceRegionPass();
}
void observeConversion() {
    if(isEnabled(fp) || isEnabled(GL_DEPTH_TEST) || isEnabled(GL_BLEND) || isEnabled(GL_ALPHA_TEST) ||
       isEnabled(GL_COLOR_ARRAY) || value(GL_STENCIL_WRITEMASK)!=0 ||
       value(GL_STENCIL_FAIL)!=GL_KEEP || value(GL_STENCIL_PASS_DEPTH_FAIL)!=GL_KEEP || value(GL_STENCIL_PASS_DEPTH_PASS)!=GL_KEEP) return;
    GLboolean mask[4]{};GLfloat color[4]{};queryBoolean(GL_COLOR_WRITEMASK,mask);queryFloat(GL_CURRENT_COLOR,color);
    const auto func=value(GL_STENCIL_FUNC);
    if(mask[3] && (color[3]==0 || color[3]==1) &&
       (func==GL_EQUAL || func==GL_NOTEQUAL || func==GL_LEQUAL || func==GL_GEQUAL)) {
        const std::lock_guard<std::mutex> lock(mutex);
        contexts[currentContext()].conversion={true,GLenum(func),value(GL_STENCIL_REF),GLuint(value(GL_STENCIL_VALUE_MASK)),color[3]};
    }
}
}
void configureEyeshineFix(HMODULE gl,bool on) noexcept {
    enabled=false;restore={};
    if(!on && !eyeshineVariantsEnabled()) return;
#define RESOLVE(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(gl,name))
    RESOLVE(currentContext,"wglGetCurrentContext");RESOLVE(getProc,"wglGetProcAddress");
    RESOLVE(currentDC,"wglGetCurrentDC");RESOLVE(queryFloat,"glGetFloatv");RESOLVE(queryLevel,"glGetTexLevelParameteriv");
    RESOLVE(integer,"glGetIntegerv");RESOLVE(isEnabled,"glIsEnabled");RESOLVE(scissor,"glScissor");
    RESOLVE(queryBoolean,"glGetBooleanv");RESOLVE(enable,"glEnable");RESOLVE(disable,"glDisable");
#undef RESOLVE
    enabled=currentContext && currentDC && getProc && integer && queryBoolean && queryFloat && queryLevel && isEnabled && scissor && enable && disable && configureEyeshineMask(gl);
    logLineNoThrow(enabled ? "EyeshineFix: whole reduced region stencil/alpha/copy/lighting ready; 0=original 7=clipped diagnostic 8=whole-region" :
        "EyeshineFix skipped: required GL exports unavailable");
}
void disableEyeshineFix() noexcept {enabled=false;}
void eyeshineFixProgram(GLenum target,std::string_view source) noexcept {
    if(!enabled || target!=fp || !currentContext()) return;
    try {
        const auto id=programId();if(!id) return;
        const bool spec=source.find("ShadowMaskTexel")!=source.npos && source.find("NormalMapTexel")!=source.npos &&
            source.find("texture[6]")!=source.npos;
        const std::lock_guard<std::mutex> lock(mutex);
        auto& programs=contexts[currentContext()].programs;
        if(spec) programs.insert(id);else programs.erase(id);
    }catch(...) {logLineNoThrow("EyeshineFix program recognition failed");}
}
void eyeshineCopyTexture(GLenum target,GLint level,GLint dx,GLint dy,GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    if(!enabled || !currentContext() || target!=GL_TEXTURE_2D || level!=0) return;
    try {
        const auto texture=value(GL_TEXTURE_BINDING_2D);
        EyeshineRect rect{};
        GLboolean mask[4]{};queryBoolean(GL_COLOR_WRITEMASK,mask);
        // The engine's stencil-to-alpha mask copy disables scissoring. Copies
        // ignore scissor testing; the stored box still describes the light.
        const bool maskCopy=w>0 && h>0 && dx>=0 && dy>=0 && x>=0 && y>=0 &&
            !mask[0] && !mask[1] && !mask[2] && mask[3];
        if(maskCopy) {
            integer(GL_SCISSOR_BOX,rect.data());
            if(expandedCopy) {const auto region=reducedRegion();if(region) rect=*region;}
        }
        expandedCopy=false;
        const std::lock_guard<std::mutex> lock(mutex);
        contexts[currentContext()].masks.copy(texture,rect);
        if(texture && maskCopy && rect[2]>0 && rect[3]>0) shadowTraceMaskCopy();
    }catch(...) {logLineNoThrow("EyeshineFix copy tracking failed");}
}
void eyeshineOtherCopy(GLenum target,GLint level) noexcept {
    if(!enabled || !currentContext() || target!=GL_TEXTURE_2D || level!=0) return;
    try {
        const auto texture=value(GL_TEXTURE_BINDING_2D);
        const std::lock_guard<std::mutex> lock(mutex);
        contexts[currentContext()].masks.copies.erase(texture);
    }catch(...) {}
}
void eyeshineFixBeforeDraw() noexcept {
    if(!enabled || replaying || restore.context || !currentContext()) return;
    try {
        const auto region=wholeRegionSelected()?reducedRegion():std::nullopt;
        if(region && isEnabled(GL_STENCIL_TEST)) {observeConversion();regionScope(*region);}
        if(!isEnabled(fp)) return;
        const auto context=currentContext();const auto id=programId();
        {const std::lock_guard<std::mutex> lock(mutex);const auto it=contexts.find(context);
            if(it==contexts.end() || !it->second.programs.count(id) || it->second.masks.copies.empty()) return;}
        const auto activate=proc<void(WINAPI*)(GLenum)>("glActiveTextureARB");if(!activate) return;
        const auto unit=value(activeTexture);activate(texture0+5);
        const auto mask=value(GL_TEXTURE_BINDING_2D);activate(unit);
        EyeshineRect engine{};integer(GL_SCISSOR_BOX,engine.data());
        if(restore.context==context) engine=restore.rect;
        const bool scissorOn=isEnabled(GL_SCISSOR_TEST)!=GL_FALSE;
        std::optional<EyeshineRect> light;EyeshineRect copied{};
        {const std::lock_guard<std::mutex> lock(mutex);
            const auto& masks=contexts.at(context).masks;
            const auto it=masks.copies.find(mask);if(it==masks.copies.end()) return;
            copied=it->second;light=masks.draw(mask,scissorOn,engine);}
        const bool restrict=selected() && light.has_value();
        shadowTraceMaskDraw(engine,copied,restrict);
        if(region && copied==*region) {if(!restore.context) regionScope(*region);return;}
        if(!restrict) return;
        restore={context,engine,scissorOn,*light};scissor((*light)[0],(*light)[1],(*light)[2],(*light)[3]);
        if(!scissorOn) enable(GL_SCISSOR_TEST);
        shadowTraceEvent("EyeshineFix light scissor u5=%d %d,%d,%d,%d -> %d,%d,%d,%d",mask,
            engine[0],engine[1],engine[2],engine[3],(*light)[0],(*light)[1],(*light)[2],(*light)[3]);
        if(!diagnosticReported.exchange(true)) logLineNoThrow("EyeshineFixVariant=7 applied: diagnostic SpecNormal clamp to fresh u5 mask scissor; engine state restored after draw");
    }catch(...) {logLineNoThrow("EyeshineFix draw setup failed");}
}
void eyeshineFixBeforeClear(GLbitfield mask) noexcept {
    if(!wholeRegionSelected() || replaying || restore.context || !(mask&GL_STENCIL_BUFFER_BIT)) return;
    if(const auto region=reducedRegion()) regionScope(*region);
}
void eyeshineFixBeforePixels(GLenum type) noexcept {
    if(!wholeRegionSelected() || replaying || restore.context || type!=0x886E) return;
    if(const auto region=reducedRegion()) regionScope(*region);
}
bool eyeshineFixCopy(GLenum target,GLint level,GLint& dx,GLint& dy,GLint& x,GLint& y,GLsizei& w,GLsizei& h) noexcept {
    expandedCopy=false;
    if(!wholeRegionSelected() || target!=GL_TEXTURE_2D || level!=0 || w<=0 || h<=0) return false;
    const auto region=reducedRegion();if(!region) return false;
    GLboolean mask[4]{};queryBoolean(GL_COLOR_WRITEMASK,mask);
    if(mask[0] || mask[1] || mask[2] || !mask[3]) return false;
    EyeshineConversion conversion;
    try {const std::lock_guard<std::mutex> lock(mutex);conversion=contexts[currentContext()].conversion;contexts[currentContext()].conversion={};}catch(...) {return false;}
    const auto& r=*region;
    const auto newDx=std::int64_t(dx)+r[0]-x,newDy=std::int64_t(dy)+r[1]-y;
    GLint tw=0,th=0;queryLevel(target,0,GL_TEXTURE_WIDTH,&tw);queryLevel(target,0,GL_TEXTURE_HEIGHT,&th);
    if(!conversion.valid || newDx<0 || newDy<0 || newDx+r[2]>tw || newDy+r[3]>th) {
        if(!unavailableReported.exchange(true)) logLineNoThrow("EyeshineFix whole-region copy skipped: missing observed conversion or insufficient texture extent; capture a TraceShadowPass sample");
        shadowTraceEvent("EyeshineFix whole-region copy unavailable: recipe=%d dst=%lld,%lld size=%dx%d tex=%dx%d",conversion.valid,newDx,newDy,r[2],r[3],tw,th);
        return false;
    }
    replaying=true;const bool rebuilt=rebuildEyeshineMask(r,conversion);replaying=false;
    if(!rebuilt) return false;
    dx=GLint(newDx);dy=GLint(newDy);x=r[0];y=r[1];w=r[2];h=r[3];expandedCopy=true;
    shadowTraceRegionCopy();
    if(!reported.exchange(true)) logLineNoThrow("EyeshineFix applied: full Eyeshine region stencil work and observed alpha conversion, expanded mask copy and lighting; engine state restored");
    return true;
}
void eyeshineFixAfterDraw() noexcept {
    if(!restore.context) return;
    const auto saved=restore;restore={};
    if(currentContext()==saved.context) {
        scissor(saved.rect[0],saved.rect[1],saved.rect[2],saved.rect[3]);
        if(!saved.scissorOn) disable(GL_SCISSOR_TEST);
    }
}
void eyeshineFixAfterList() noexcept {
    if(restore.context && currentContext()==restore.context) {
        const auto saved=restore;restore={};
        EyeshineRect after{};integer(GL_SCISSOR_BOX,after.data());
        // A state-setting list owns its resulting state. Geometry-only lists
        // need the same temporary scope/restoration as direct draws.
        if(after==saved.applied) {
            scissor(saved.rect[0],saved.rect[1],saved.rect[2],saved.rect[3]);
            if(!saved.scissorOn) disable(GL_SCISSOR_TEST);
        }
        return;
    }
    eyeshineFixAfterDraw();
}
void eyeshineFixFrameEnd() noexcept {
    if(!enabled || !currentContext()) return;
    try {const std::lock_guard<std::mutex> lock(mutex);const auto it=contexts.find(currentContext());
        if(it!=contexts.end()) {it->second.masks.reset();it->second.conversion={};}}catch(...) {}
    expandedCopy=false;
}
void eyeshineFixContextDeleted(HGLRC context) noexcept {
    try {const std::lock_guard<std::mutex> lock(mutex);contexts.erase(context);}catch(...) {}
    if(restore.context==context) restore={};
}
