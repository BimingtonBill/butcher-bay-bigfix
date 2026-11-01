#include "eyeshine_variants.h"
#include "eyeshine_fix.h"
#include "eyeshine_mask.h"
#include "shadow_trace.h"
#include "log.h"
#include "fingerprint.h"
#include <MinHook.h>
#include <psapi.h>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>

bool EyeshineAbSequence::begin(int frames,int variant,int original) noexcept {
    if(remaining || frames<1 || frames>16 || variant<1 || variant>eyeshineVariantCount) return false;
    remaining=total=frames;target=variant;saved=original;return true;
}
int EyeshineAbSequence::advance() noexcept {
    if(remaining<=0 || --remaining==0) return saved;
    return (total-remaining)%2?target:0;
}

bool EyeshineVariantCycle::press(bool down) noexcept {
    const bool changed=down && !held;
    held=down;
    if(changed) value=(value+1)%(eyeshineVariantCount+1);
    return changed;
}
EyeshineMaskMapping eyeshineMaskMapping(GLint tw,GLint th,GLint dx,GLint dy,
                                      GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    EyeshineMaskMapping m;
    if(tw<=0 || th<=0 || w<=0 || h<=0 || dx<0 || dy<0 || x<0 || y<0 || w>tw || h>th || dx>tw-w || dy>th-h) return m;
    m.valid=true;
    m.map={1.f/tw,1.f/th,(float(dx)-float(x))/tw,(float(dy)-float(y))/th};
    m.bounds={(dx+.5f)/tw,(dy+.5f)/th,(dx+float(w)-.5f)/tw,(dy+float(h)-.5f)/th};
    return m;
}
std::string instrumentEyeshineProgram(std::string_view source) {
    // Only the shipped soft-stencil SpecNormal family. Do not take local slots
    // from programs that already use them, or instrument unknown layouts.
    const auto center=source.find("ADD smtc0.xy, smtc0, 0.000001;");
    const auto declarations=source.find("OUTPUT oCol");
    if(source.find("ShadowMaskTexel")==source.npos || source.find("NormalMapTexel")==source.npos ||
       source.find("texture[6]")==source.npos || source.find("program.local[")!=source.npos ||
       center==source.npos || declarations==source.npos) return std::string(source);
    std::string result(source);
    const std::string clamp="\nMAX BigFixCoord.xy, smtc1, BigFixBounds;\n"
        "MIN BigFixCoord.xy, BigFixCoord, BigFixBounds.zwzw;\n"
        "LRP smtc1.xy, BigFixFlags.y, BigFixCoord, smtc1;";
    for(std::size_t pos=0;(pos=result.find("MAD smtc1.xy, SMOfs",pos))!=result.npos;) {
        pos=result.find(';',pos);
        if(pos==result.npos) return std::string(source);
        result.insert(++pos,clamp);pos+=clamp.size();
    }
    const std::string code="\nMAD BigFixCoord.xy, BigFixPosition, BigFixMap, BigFixMap.zwzw;\n"
        "LRP smtc0.xy, BigFixFlags.x, BigFixCoord, smtc0;\n"
        "MAX BigFixCoord.xy, smtc0, BigFixBounds;\n"
        "MIN BigFixCoord.xy, BigFixCoord, BigFixBounds.zwzw;\n"
        "LRP smtc0.xy, BigFixFlags.y, BigFixCoord, smtc0;";
    result.insert(center+std::string_view("ADD smtc0.xy, smtc0, 0.000001;").size(),code);
    result.insert(declarations,"ATTRIB BigFixPosition = fragment.position;\n"
        "PARAM BigFixMap = program.local[0];\nPARAM BigFixBounds = program.local[1];\n"
        "PARAM BigFixFlags = program.local[2];\nTEMP BigFixCoord;\n");
    return result;
}

namespace {
bool testing=false;
std::atomic<int> active{0};
EyeshineVariantCycle cycle;
EyeshineAbSequence ab;
int abTarget=0;
bool abCaptured=false;
struct Api {
    decltype(&glGetIntegerv) integer=nullptr;
    decltype(&glGetFloatv) floating=nullptr;
    decltype(&glGetBooleanv) boolean=nullptr;
    decltype(&glGetTexLevelParameteriv) level=nullptr;
    decltype(&glGetTexParameteriv) parameter=nullptr;
    decltype(&wglGetCurrentContext) context=nullptr;
    decltype(&wglGetProcAddress) proc=nullptr;
    decltype(&glIsEnabled) enabled=nullptr;
    decltype(&glPushAttrib) push=nullptr;
    decltype(&glPopAttrib) pop=nullptr;
    decltype(&glEnable) enable=nullptr;
    decltype(&glDisable) disable=nullptr;
    decltype(&glColorMask) colorMask=nullptr;
    decltype(&glPixelTransferf) transfer=nullptr;
    decltype(&glPixelZoom) zoom=nullptr;
    decltype(&glClear) clear=nullptr;
    decltype(&glStencilMask) stencilMask=nullptr;
    decltype(&glStencilFunc) stencilFunc=nullptr;
    decltype(&glStencilOp) stencilOp=nullptr;
    decltype(&glClearColor) clearColor=nullptr;
    decltype(&glScissor) scissor=nullptr;
    decltype(&glViewport) viewport=nullptr;
    decltype(&glMatrixMode) matrixMode=nullptr;
    decltype(&glPushMatrix) pushMatrix=nullptr;
    decltype(&glPopMatrix) popMatrix=nullptr;
    decltype(&glLoadIdentity) identity=nullptr;
    decltype(&glColor4ub) color=nullptr;
    decltype(&glRectf) rect=nullptr;
    decltype(&glPolygonMode) polygonMode=nullptr;
} api;
constexpr GLenum fp=0x8804, vp=0x8620, texture0=0x84C0, activeTexture=0x84E0;
template<class T> T proc(const char* name) {
    const auto p=api.proc(name);const auto n=reinterpret_cast<std::uintptr_t>(p);
    return n>3 && n!=~std::uintptr_t(0) ? reinterpret_cast<T>(p) : nullptr;
}
using ProgramQuery=void(WINAPI*)(GLenum,GLenum,GLint*);
using Local=void(WINAPI*)(GLenum,GLuint,const GLfloat*);
using Activate=void(WINAPI*)(GLenum);

struct CopyPhase { EyeshineCopyTracker pending; HGLRC context=nullptr; GLint depth=0;EyeshineConversion conversion; };
thread_local CopyPhase phase;
std::mutex mutex;
using Key=std::pair<HGLRC,GLint>;
std::map<Key,bool> programs;
std::map<Key,EyeshineMaskMapping> mappings;
GLint integer(GLenum name) { GLint n=0;api.integer(name,&n);return n; }
GLint programId() {
    const auto query=proc<ProgramQuery>("glGetProgramivARB");GLint n=0;
    if(query) query(fp,0x8677,&n);
    return n;
}
void note(const char* message) noexcept {
    // One application record per variant/context would be excessive in this
    // experiment; retain one per process and selected variant instead.
    static std::atomic<unsigned> seen{0};
    const unsigned bit=1u<<active.load();
    if(!(seen.fetch_or(bit)&bit)) {
        try { logf("EyeshineFixVariant=%d applied: %s",active.load(),message); } catch(...) {}
    }
}
void disableFragmentTests() {
    const GLenum caps[]{GL_ALPHA_TEST,GL_BLEND,GL_DEPTH_TEST,GL_COLOR_LOGIC_OP,GL_DITHER,fp};
    for(auto cap:caps) api.disable(cap);
}

}

void configureEyeshineVariants(HMODULE gl,const Settings& settings) noexcept {
    configureEyeshineMask(gl);
    testing=false;active=0;cycle={};phase={};ab={};abCaptured=false;abTarget=settings.eyeshineAbBurst;
    if(!settings.traceShadowPass && settings.eyeshineFixVariant==0 && !abTarget) return;
    bool complete=true;
#define GET(member,name) api.member=reinterpret_cast<decltype(api.member)>(GetProcAddress(gl,name));complete=complete && api.member
    GET(integer,"glGetIntegerv");GET(floating,"glGetFloatv");GET(boolean,"glGetBooleanv");GET(level,"glGetTexLevelParameteriv");GET(parameter,"glGetTexParameteriv");GET(context,"wglGetCurrentContext");GET(proc,"wglGetProcAddress");
    GET(enabled,"glIsEnabled");GET(push,"glPushAttrib");GET(pop,"glPopAttrib");GET(enable,"glEnable");GET(disable,"glDisable");
    GET(colorMask,"glColorMask");GET(transfer,"glPixelTransferf");GET(zoom,"glPixelZoom");GET(clear,"glClear");
    GET(stencilMask,"glStencilMask");GET(stencilFunc,"glStencilFunc");GET(stencilOp,"glStencilOp");GET(clearColor,"glClearColor");
    GET(scissor,"glScissor");GET(viewport,"glViewport");GET(matrixMode,"glMatrixMode");GET(pushMatrix,"glPushMatrix");
    GET(popMatrix,"glPopMatrix");GET(identity,"glLoadIdentity");GET(color,"glColor4ub");GET(rect,"glRectf");GET(polygonMode,"glPolygonMode");
#undef GET
    testing=complete;
    if(!complete) {logLineNoThrow("EyeshineFixVariant unavailable: incomplete GL exports");return;}
    cycle.value=settings.eyeshineFixVariant;active=cycle.value;
    try {logf("EyeshineFixVariant=%d: F9 cycles 0=original 1=packed-state 2=stencil-clear 3=mask-alpha 4=window-coordinates 5=clamp-taps 6=full-resolution 7=light-scissor-diagnostic 8=whole-region; overrides EyeshineFix; EyeshineAbBurst=%d",cycle.value,abTarget);}catch(...){}
}
bool eyeshineVariantsEnabled() noexcept {return testing;}
int eyeshineVariant() noexcept {return active.load();}
void eyeshineVariantSwap(BOOL result,bool down) noexcept {
    if(!testing || !result) return;
    phase={}; // A copy phase never survives presentation.
    try {
        const auto context=api.context();
        const std::lock_guard<std::mutex> lock(mutex);
        for(auto it=mappings.begin();it!=mappings.end();) {
            if(it->first.first==context) it=mappings.erase(it);else ++it;
        }
    }catch(...) {}
    if(ab.remaining) {cycle.held=down;return;} // F9 cannot disturb an interleaved burst.
    if(cycle.press(down)) {
        active=cycle.value;
        try{logf("EyeshineFixVariant=%d selected (next frame); 0=original 7=light-scissor-diagnostic 8=whole-region",cycle.value);}catch(...){}
        shadowTraceArm(1);
    }
}
bool eyeshineAbConfigured() noexcept {return testing && abTarget>0;}
bool eyeshineAbBegin(int frames) noexcept {
    if(!eyeshineAbConfigured() || !ab.begin(frames,abTarget,cycle.value)) return false;
    active=0;abCaptured=false;
    try{logf("EyeshineAbBurst started: %d frames alternating 0/%d; first sample is next frame; restore=%d",frames,abTarget,ab.saved);}catch(...){}
    shadowTraceArm(2);
    return true;
}
void eyeshineAbCapture(std::uint64_t index) noexcept {
    if(!ab.remaining) return;
    abCaptured=true;
    try{logf("EyeshineAbBurst cap=%llu variant=%d sample=%d/%d",static_cast<unsigned long long>(index),active.load(),ab.total-ab.remaining+1,ab.total);}catch(...){}
}
void eyeshineAbCancel() noexcept {
    if(!ab.remaining) return;
    active=ab.saved;ab.remaining=0;abCaptured=false;
    try{logf("EyeshineFixVariant=%d selected (A/B cancelled; next frame)",active.load());}catch(...){}
}
void eyeshineAbSwap(BOOL result) noexcept {
    if(!ab.remaining) return;
    if(!result) {eyeshineAbCancel();return;}
    if(!abCaptured) return;
    abCaptured=false;active=ab.advance();
    try{logf("EyeshineFixVariant=%d selected (A/B %s; next frame)",active.load(),ab.remaining?"sample":"restored");}catch(...){}
}
std::string eyeshineVariantProgram(std::string_view source) {
    if(!testing) return std::string(source);
    auto result=instrumentEyeshineProgram(source);
    if(result!=source) {
        const std::lock_guard<std::mutex> lock(mutex);
        programs[{api.context(),programId()}]=true;
    } else {
        const std::lock_guard<std::mutex> lock(mutex);
        programs.erase({api.context(),programId()}); // A name can be reuploaded with unrelated code.
    }
    return result;
}
bool eyeshineVariantBeforePixels(GLenum type) noexcept {
    if(!testing || active!=1 || type!=0x886E || !api.context()) return false;
    const auto window=proc<void(WINAPI*)(GLfloat,GLfloat)>("glWindowPos2fARB");
    if(!window) return false;
    api.push(GL_ALL_ATTRIB_BITS);disableFragmentTests();api.disable(GL_STENCIL_TEST);api.disable(GL_SCISSOR_TEST);
    api.colorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);api.zoom(1,1);window(0,0);
    for(auto name:{GL_RED_SCALE,GL_GREEN_SCALE,GL_BLUE_SCALE,GL_ALPHA_SCALE,GL_DEPTH_SCALE}) api.transfer(name,1);
    for(auto name:{GL_RED_BIAS,GL_GREEN_BIAS,GL_BLUE_BIAS,GL_ALPHA_BIAS,GL_DEPTH_BIAS,GL_INDEX_SHIFT,GL_INDEX_OFFSET,GL_MAP_COLOR,GL_MAP_STENCIL}) api.transfer(name,0);
    note("neutral fragment/pixel state around packed copy; restored immediately");
    return true;
}
void eyeshineVariantAfterPixels(bool restore,GLint x,GLint y,GLsizei w,GLsizei h,GLenum type) noexcept {
    if(restore) api.pop();
    if(!testing) return;
    phase={};phase.pending.pixels(api.context(),x,y,w,h,type);
}
void eyeshineVariantOtherCopy() noexcept {phase={};}
void eyeshineVariantContextDeleted(HGLRC context) noexcept {
    if(!testing) return;
    try {
        const std::lock_guard<std::mutex> lock(mutex);
        for(auto it=programs.begin();it!=programs.end();) {
            if(it->first.first==context) it=programs.erase(it);else ++it;
        }
        for(auto it=mappings.begin();it!=mappings.end();) {
            if(it->first.first==context) it=mappings.erase(it);else ++it;
        }
        if(phase.context==context || phase.pending.context==context) phase={};
    }catch(...) {}
}
void eyeshineVariantTexture(GLenum target,GLint level,GLint dx,GLint dy,GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    if(!testing || !api.context()) return;
    const auto context=api.context();
    const auto texture=integer(GL_TEXTURE_BINDING_2D);
    if(phase.pending.context) {
        if(phase.pending.texture(context,target,level,x,y,w,h)) {phase.context=context;phase.depth=texture;}
        return;
    }
    if(context!=phase.context || target!=GL_TEXTURE_2D || level || !texture || texture==phase.depth) return;
    GLint tw=0,th=0,format=0,min=0,mag=0;
    api.level(target,0,GL_TEXTURE_WIDTH,&tw);api.level(target,0,GL_TEXTURE_HEIGHT,&th);api.level(target,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
    api.parameter(target,GL_TEXTURE_MIN_FILTER,&min);api.parameter(target,GL_TEXTURE_MAG_FILTER,&mag);
    if(format!=GL_RGBA8 || min!=GL_LINEAR || mag!=GL_LINEAR) return;
    const auto mapping=eyeshineMaskMapping(tw,th,dx,dy,x,y,w,h);
    if(!mapping.valid) return;
    try {const std::lock_guard<std::mutex> lock(mutex);mappings[{context,texture}]=mapping;}catch(...){return;}
    if(active==3 && phase.conversion.valid && rebuildEyeshineMask({x,y,w,h},phase.conversion))
        note("replayed observed stencil-to-alpha test across mask-copy rectangle; RGB/stencil preserved");
}
void eyeshineVariantDraw() noexcept {
    if(!testing || !api.context()) return;
    if(!api.enabled(fp)) {
        // Mode 6 has both equal/not-equal global conversions and per-light
        // threshold conversions. Reuse the last actual constant-alpha quad's
        // test; a hardcoded !=128 mask would invert some of these paths.
        if(active==3 && phase.context==api.context() && api.enabled(GL_STENCIL_TEST) &&
           !api.enabled(GL_DEPTH_TEST) && !api.enabled(GL_BLEND) && !api.enabled(GL_ALPHA_TEST) &&
           !api.enabled(GL_COLOR_ARRAY) && integer(GL_STENCIL_WRITEMASK)==0 &&
           integer(GL_STENCIL_FAIL)==GL_KEEP && integer(GL_STENCIL_PASS_DEPTH_FAIL)==GL_KEEP && integer(GL_STENCIL_PASS_DEPTH_PASS)==GL_KEEP) {
            GLfloat color[4]{};GLboolean mask[4]{};
            api.floating(GL_CURRENT_COLOR,color);api.boolean(GL_COLOR_WRITEMASK,mask);
            const auto func=integer(GL_STENCIL_FUNC);
            if(mask[3] && (color[3]==0 || color[3]==1) &&
               (func==GL_EQUAL || func==GL_NOTEQUAL || func==GL_LEQUAL || func==GL_GEQUAL))
                phase.conversion={true,GLenum(func),integer(GL_STENCIL_REF),GLuint(integer(GL_STENCIL_VALUE_MASK)),color[3]};
        }
        return;
    }
    try {
        const auto id=programId();const auto context=api.context();
        {const std::lock_guard<std::mutex> lock(mutex);if(!programs.count({context,id})) return;}
        phase={}; // Stop recognizing mask copies before later post-process scratch.
        const auto activate=proc<Activate>("glActiveTextureARB");
        const auto local=proc<Local>("glProgramLocalParameter4fvARB");
        if(!activate || !local) return;
        const auto unit=integer(activeTexture);activate(texture0+5);
        const auto texture=integer(GL_TEXTURE_BINDING_2D);activate(unit);
        EyeshineMaskMapping mapping;
        {const std::lock_guard<std::mutex> lock(mutex);const auto it=mappings.find({context,texture});if(it!=mappings.end()) mapping=it->second;}
        const GLfloat flags[]{mapping.valid && active==4 ? 1.f:0.f,mapping.valid && active==5 ? 1.f:0.f,0,0};
        local(fp,0,mapping.map.data());local(fp,1,mapping.bounds.data());local(fp,2,flags);
        if(flags[0]) note("window-position mask/depth lookup using observed texture-copy offset");
        if(flags[1]) note("center and 16 neighbor taps clamped to last mask-copy rectangle");
    }catch(...) {logLineNoThrow("EyeshineFixVariant draw setup failed");}
}
void eyeshineVariantClear(GLbitfield mask) noexcept {
    if(!testing || active!=2 || (mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT))!=
       (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT) || !api.context()) return;
    api.push(GL_STENCIL_BUFFER_BIT|GL_SCISSOR_BIT);api.disable(GL_SCISSOR_TEST);api.stencilMask(~0u);
    api.clear(GL_STENCIL_BUFFER_BIT);api.pop();
    note("extra full-drawable stencil clear with original clear value before combined frame clear");
}

namespace {
using RenderScale=GLfloat* (*)(void*,GLfloat*);
RenderScale originalScale=nullptr;
GLfloat* renderScaleHook(void* self,GLfloat* out) noexcept {
    auto* result=originalScale(self,out);
    if(result && active==6) {
        shadowTraceEvent("Eyeshine render scale %.7g,%.7g -> 1,1",result[0],result[1]);
        result[0]=result[1]=1;
        note("GameWorld render-scale result forced to 1,1; engine owns full view/projection/copy updates");
    }
    return result;
}
}
void installEyeshineScaleHook(HMODULE world,const Settings& settings) {
    if(!settings.traceShadowPass && !(settings.eyeshineFixVariant || settings.eyeshineAbBurst)) return;
    // Function +5870 returns a two-float render scale. Validate the file and
    // the live instructions before installing the one process-lifetime detour.
    if(sha256Module(world)!="749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7") {
        logLineNoThrow("Eyeshine full-resolution probe unavailable: GameWorld fingerprint mismatch");return;
    }
    MODULEINFO info{};
    if(!K32GetModuleInformation(GetCurrentProcess(),world,&info,sizeof(info)) ||
       info.SizeOfImage<eyeshineScaleRva+eyeshineScaleBytes.size()) return;
    auto* target=static_cast<unsigned char*>(info.lpBaseOfDll)+eyeshineScaleRva;
    if(std::memcmp(target,eyeshineScaleBytes.data(),eyeshineScaleBytes.size())) {
        logLineNoThrow("Eyeshine full-resolution probe unavailable: live instruction mismatch");return;
    }
    const auto status=MH_CreateHook(target,reinterpret_cast<void*>(&renderScaleHook),reinterpret_cast<void**>(&originalScale));
    logf("Eyeshine full-resolution probe GameWorld+5870: %s",MH_StatusToString(status));
}
