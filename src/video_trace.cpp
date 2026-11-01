#include "video_trace.h"
#include "widescreen.h"
#include "shadow_trace.h"
#include "eyeshine_variants.h"
#include "eyeshine_fix.h"
#include "bloom_trace.h"
#include "log.h"
#include <MinHook.h>
#include <gl/GL.h>
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <mutex>

namespace {
double videoAspect(unsigned w, unsigned h) {
    return w == 720 && h == 486 ? 4.0/3.0 : double(w)/h;
}
VideoRect fitRect(VideoRect b, double aspect) {
    if (!(aspect>0) || !std::isfinite(aspect) || b.right <= b.left || b.bottom <= b.top) return {};
    const double width = double(b.right)-b.left, height = double(b.bottom)-b.top;
    const double fittedW = (std::min)(width, height*aspect);
    const double fittedH = (std::min)(height, width/aspect);
    return {int(b.left+(width-fittedW)/2), int(b.top+(height-fittedH)/2),
            int(b.right-(width-fittedW)/2), int(b.bottom-(height-fittedH)/2)};
}
}
VideoRect desiredVideoRect(VideoRect b, unsigned w, unsigned h) {
    return w && h ? fitRect(b,videoAspect(w,h)) : VideoRect{};
}
VideoRect desiredVideoUiRect(VideoRect b, unsigned w, unsigned h, unsigned viewportW, unsigned viewportH) {
    return w && h && viewportW && viewportH ?
        fitRect(b,videoAspect(w,h)*(640.0/480.0)*viewportH/viewportW) : VideoRect{};
}

const std::vector<VideoHookSite>& videoHookSites() {
    // Complete instruction boundaries, read from the EU 1.1 files, no relocations.
    static const std::vector<VideoHookSite> sites{
        {"decoder_start", VideoModule::System, 0xEFFF0,
         {0x48,0x8b,0xc4,0x48,0x81,0xec,0x28,0x02,0,0,0x48,0xc7,0x44,0x24,0x48,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0xf8}},
        {"decoder_close", VideoModule::System, 0xF0630,
         {0x48,0x83,0xec,0x58,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x50}},
        {"decoder_owner", VideoModule::System, 0xF1E50,
         {0x48,0x89,0x4c,0x24,0x08,0x48,0x8b,0xc4,0x48,0x81,0xec,0x88,0,0,0,0x48,0xc7,0x44,0x24,0x50,0xfe,0xff,0xff,0xff}},
        {"video_paint", VideoModule::World, 0x755D0,
         {0x48,0x8b,0xc4,0x48,0x81,0xec,0x78,0x02,0,0,0x48,0xc7,0x84,0x24,0x40,0x01,0,0,0xfe,0xff,0xff,0xff}},
        {"aspect_fit", VideoModule::World, 0x39AD0,
         {0x48,0x8b,0xc4,0x48,0x81,0xec,0xb8,0,0,0,0x48,0x89,0x58,0xf8,0x48,0x89,0x68,0xf0,0x48,0x89,0x70,0xe8}},
        {"rectangle_submit", VideoModule::World, 0x38E90,
         {0x48,0x8b,0xc4,0x48,0x81,0xec,0x18,0x01,0,0,0x44,0x8b,0x5a,0x10,0x48,0x89,0x58,0xf8}},
        {"rectangle_dispatch", VideoModule::World, 0x3CD50,
         {0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x48,0x8b,0xc4,0x48,0x81,0xec,0xc8,0x03,0,0}}
    };
    return sites;
}

bool verifyVideoPrologue(const std::uint8_t* base, std::size_t size, const VideoHookSite& site) {
    return base && !site.expected.empty() && site.rva <= size && site.expected.size() <= size-site.rva &&
        std::equal(site.expected.begin(), site.expected.end(), base+site.rva);
}

namespace {
std::atomic<bool> enabled{false};
std::atomic<bool> traceFov{false};
std::atomic<bool> aspectFix{false};
std::atomic<unsigned> lines{0}, detailLines{0}, summaryLines{0};
std::atomic<std::uint64_t> ids{0}, movieEpoch{0};
decltype(&wglGetCurrentContext) currentContext = nullptr;
decltype(&wglGetCurrentDC) currentDC = nullptr;
decltype(&glGetIntegerv) getInteger = nullptr;
decltype(&glGetFloatv) getFloat = nullptr;
decltype(&glGetBooleanv) getBoolean = nullptr;
decltype(&glIsEnabled) isEnabled = nullptr;
decltype(&glClearColor) setClearColor = nullptr;
decltype(&glColorMask) setColorMask = nullptr;
decltype(&glDisable) disableGl = nullptr;
decltype(&glEnable) enableGl = nullptr;
decltype(&glClear) clearGl = nullptr;
std::uintptr_t worldBase = 0;

// Faults in diagnostic reads must not fault the game. No SEH around game calls.
bool copyMemory(void* to, const void* from, std::size_t size) noexcept {
    __try { if (!from) return false; std::memcpy(to, from, size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T read(const void* p, std::size_t offset = 0) noexcept {
    T value{};
    copyMemory(&value, reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(p)+offset), sizeof(value));
    return value;
}
void* slot(void* object, unsigned offset) noexcept { return read<void*>(read<void*>(object), offset); }

// CStr's +48h const char* accessor, independently seen at F1F97 and 17D96C.
const char* stringData(void* object) noexcept {
    __try {
        const auto getter = reinterpret_cast<const char* (*)(void*)>(slot(object, 0x48));
        return getter ? getter(object) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void stringCopy(char* to, std::size_t size, void* object) noexcept {
    const auto data = stringData(object);
    std::size_t i = 0;
    for (; i+1 < size; ++i) {
        char c = 0;
        if (!copyMemory(&c, data ? data+i : nullptr, 1) || !c) break;
        to[i] = c < 32 ? '?' : c; // A diagnostic can never inject extra lines.
    }
    to[i] = 0;
}

struct Counts {
    unsigned paints=0, fits=0, rectangles=0, dispatches=0;
    unsigned viewport=0, scissor=0, projection=0, clears=0, uploads=0;
    unsigned begins=0, ends=0, arrays=0, elements=0, vertices=0, binds=0, programs=0;
};
struct Frame {
    unsigned depth=0, fitDepth=0, eventLines=0, internalLines=0, verticesInBegin=0;
    std::uint64_t paint=0, submission=0, frame=0, seenEpoch=0, nextSample=0, skipped=0;
    bool sample=true, pending=false, menu=false, inBegin=false;
    GLenum matrixMode=GL_MODELVIEW;
    GLint viewport[4]{};
    Counts counts;
    struct Array { GLint size=0; GLenum type=0; GLsizei stride=0; const void* pointer=nullptr; } vertexArray, texcoordArray;
    // Keep one bounded frame before deciding whether it is the first menu
    // frame. Otherwise 1Hz sampling could silently miss the skip transition.
    std::array<std::array<char,2560>,44> events{};
    unsigned buffered=0;
    std::uint64_t lastPaint=0, firstSubmission=0, lastSubmission=0;
};
thread_local Frame frame;
thread_local void* owner = nullptr;
struct OwnerMetadata {
    void* decoder=nullptr;
    void* entry=nullptr;
    char file[192]{};
    unsigned textures[3]{};
};
std::mutex metadataMutex;
std::array<OwnerMetadata,32> metadata{};
std::size_t nextMetadata=0;

// 1500 detail + 250 frame summaries + remaining lifecycle/changes, total <=2000.
void emit(const char* text, bool detail, bool summary) noexcept {
    if (detail && detailLines.fetch_add(1) >= 1500) return;
    if (summary && summaryLines.fetch_add(1) >= 250) return;
    const auto line = lines.fetch_add(1);
    if (line >= 1999) {
        if (line == 1999) logLineNoThrow("VideoTrace: 2000-line limit reached; remaining events suppressed");
        return;
    }
    const DWORD error = GetLastError();
    try { logLine(text); } catch (...) { }
    SetLastError(error);
}
void trace(bool detail, bool summary, const char* format, ...) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || lines.load()>=2000) return;
    const DWORD error=GetLastError();
    char text[2300]{}, prefixed[2560]{};
    va_list args; va_start(args, format); std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    std::snprintf(prefixed,sizeof(prefixed),"VideoTrace t=%llu tid=%lu ctx=%p frame=%llu paint=%llu lastPaint=%llu submit=%llu active=%u %s",
             GetTickCount64(), GetCurrentThreadId(), currentContext ? currentContext() : nullptr,
             frame.frame, frame.paint, frame.lastPaint, frame.submission, frame.depth, text);
    if(detail) {
        if(frame.buffered<frame.events.size()) {
            std::memcpy(frame.events[frame.buffered].data(),prefixed,sizeof(prefixed)); ++frame.buffered;
        }
    } else emit(prefixed,false,summary);
    SetLastError(error);
}
bool observing() noexcept { return enabled.load(std::memory_order_relaxed) && (frame.depth || frame.pending || frame.menu); }
bool detail() noexcept {
    if (!observing() || frame.eventLines >= 32 || detailLines.load()>=1500 || lines.load()>=1999) return false;
    ++frame.eventLines;
    return true;
}
bool internalDetail() noexcept {
    if(!observing() || frame.internalLines>=12 || detailLines.load()>=1500 || lines.load()>=1999) return false;
    ++frame.internalLines;
    return true;
}
void viewportSnapshot() noexcept {
    // Never query GL between glBegin/glEnd (would inject GL_INVALID_OPERATION).
    if (!frame.inBegin && getInteger && currentContext && currentContext()) getInteger(GL_VIEWPORT, frame.viewport);
}
void stackText(char* out, std::size_t size) noexcept {
    void* stack[7]{};
    const auto n = CaptureStackBackTrace(2, 7, stack, nullptr);
    std::size_t used = 0;
    for (USHORT i=0; i<n && used+1<size; ++i) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(stack[i], &region, sizeof(region))) continue;
        char module[MAX_PATH]{};
        GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase), module, MAX_PATH);
        const auto name = std::strrchr(module, '\\');
        const int count = std::snprintf(out+used, size-used, "%s%s+%llX", used ? "," : "", name ? name+1 : module,
            reinterpret_cast<std::uintptr_t>(stack[i])-reinterpret_cast<std::uintptr_t>(region.AllocationBase));
        if (count < 0 || std::size_t(count) >= size-used) break;
        used += count;
    }
}
void draw(const char* kind, GLenum mode, int count) noexcept {
    viewportSnapshot();
    if (detail()) {
        char stack[600]{}; stackText(stack, sizeof(stack));
        trace(true,false,"%s mode=%X count=%d viewport=%d,%d,%d,%d stack=%s", kind,mode,count,
            frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3],stack);
    }
}
void arraySample(const Frame::Array& array, unsigned index, const char* kind) noexcept {
    if(array.type!=GL_FLOAT || array.size<2 || array.size>4 || array.stride<0 || !array.pointer) return;
    const auto stride=array.stride ? std::size_t(array.stride) : std::size_t(array.size)*sizeof(float);
    const auto start=reinterpret_cast<std::uintptr_t>(array.pointer);
    if(index>(std::numeric_limits<std::uintptr_t>::max)()/stride || start>(std::numeric_limits<std::uintptr_t>::max)()-index*stride) return;
    float value[4]{};
    const auto valid=copyMemory(value,reinterpret_cast<const void*>(start+index*stride),array.size*sizeof(float));
    if(detail()) trace(true,false,"%s index=%u readable=%d components=%d value=%.7g,%.7g,%.7g,%.7g (client pointers only; VBO offsets unreadable)",kind,index,valid,array.size,value[0],value[1],value[2],value[3]);
}
void arrays(GLint first, GLsizei count, GLenum type=0, const void* indices=nullptr) noexcept {
    if(!observing() || first<0 || count<0) return;
    for(GLsizei i=0;i<(std::min)(count,GLsizei(4));++i) {
        unsigned index=unsigned(first)+unsigned(i);
        if(type) {
            std::size_t bytes=type==GL_UNSIGNED_INT ? 4 : type==GL_UNSIGNED_SHORT ? 2 : type==GL_UNSIGNED_BYTE ? 1 : 0;
            index=0;
            if(!bytes || !copyMemory(&index,reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(indices)+i*bytes),bytes)) break;
        }
        arraySample(frame.vertexArray,index,"clientVertex");
        arraySample(frame.texcoordArray,index,"clientUV");
    }
}

void decoderEvent(const char* event, void* decoder, int outcome) noexcept {
    OwnerMetadata data;
    data.decoder=decoder;
    // Cache diagnostic identity, never playback state. Close often happens on
    // a different path/thread after the owner-open hook has returned.
    try {
        const std::lock_guard<std::mutex> lock(metadataMutex);
        for(const auto& saved:metadata) if(saved.decoder==decoder) { data=saved; break; }
        if(owner) {
            data.entry=owner;
            stringCopy(data.file,sizeof(data.file),static_cast<char*>(owner)+0x28);
            for(unsigned i=0;i<3;++i) data.textures[i]=read<unsigned>(owner,0x68+4*i);
            auto* destination=&metadata[nextMetadata%metadata.size()];
            for(auto& saved:metadata) if(saved.decoder==decoder) { destination=&saved; break; }
            *destination=data; ++nextMetadata;
        }
    } catch (...) { }
    trace(false,false,"%s decoder=%p owner=%p file=%s outcome=%d initialized=%u frame=%ux%u picture=%ux%u offset=%u,%u fps=%u/%u rawPAR=%u/%u engineTex=%u,%u,%u",
        event,decoder,data.entry,data.file,outcome,read<unsigned char>(decoder,0x2f0),
        read<unsigned>(decoder,0x240),read<unsigned>(decoder,0x244),
        read<unsigned>(decoder,0x248),read<unsigned>(decoder,0x24c),
        read<unsigned>(decoder,0x250),read<unsigned>(decoder,0x254),
        read<unsigned>(decoder,0x258),read<unsigned>(decoder,0x25c),
        read<unsigned>(decoder,0x260),read<unsigned>(decoder,0x264),
        data.textures[0],data.textures[1],data.textures[2]);
}
using Start = bool (*)(void*,void*);
using Close = void (*)(void*);
using Owner = void (*)(void*);
using Paint = void (*)(void*,void*);
using Fit = void (*)(void*,const VideoRect*,const VideoRect*,const unsigned*,float,const void*);
using Rectangle = void (*)(void*,const VideoRect*,const VideoRect*,const void*);
using Dispatch = void (*)(void*,std::uintptr_t,const VideoRect*,const VideoRect*,const void*,std::uintptr_t,std::uintptr_t,std::uintptr_t);
Start originalStart = nullptr;
Close originalClose = nullptr;
Owner originalOwner = nullptr;
Paint originalPaint = nullptr;
Fit originalFit = nullptr;
Rectangle originalRectangle = nullptr;
Dispatch originalDispatch = nullptr;

bool startHook(void* decoder, void* sound) {
    decoderEvent("decoder_start entry",decoder,-1);
    const auto result = originalStart(decoder,sound);
    movieEpoch.fetch_add(1);
    decoderEvent("decoder_start exit",decoder,result);
    return result;
}
void closeHook(void* decoder) {
    decoderEvent("decoder_close entry",decoder,-1);
    originalClose(decoder);
    decoderEvent("decoder_close exit",decoder,read<unsigned char>(decoder,0x2f0)==0);
}
void ownerHook(void* entry) {
    const auto previous = owner; owner=entry;
    struct Restore { void* previous; ~Restore() { owner=previous; } } restore{previous};
    originalOwner(entry);
}
void paintHook(void* object, void* util) {
    const auto previous = frame.paint;
    frame.paint=++ids; frame.lastPaint=frame.paint; ++frame.depth; ++frame.counts.paints;
    frame.pending=true;
    const auto epoch = movieEpoch.load();
    if (epoch != frame.seenEpoch) { frame.sample=true; frame.seenEpoch=epoch; }
    viewportSnapshot();
    if (internalDetail()) {
        char name[192]{}; stringCopy(name,sizeof(name),static_cast<char*>(object)+0x1d0);
        RECT client{}; const auto dc=currentDC ? currentDC() : nullptr;
        GetClientRect(WindowFromDC(dc),&client);
        trace(true,false,"paint entry object=%p vtable=%p texture=%s util=%p utilVtable=%p fitCallee=%p PIXELASPECT=%.7g viewport=%d,%d,%d,%d client=%ldx%ld",
            object,read<void*>(object),name,util,read<void*>(util),slot(util,0x100),read<float>(object,0x238),
            frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3],client.right,client.bottom);
    }
    struct Exit {
        std::uint64_t previous;
        ~Exit() { if (internalDetail()) trace(true,false,"paint exit"); --frame.depth; frame.paint=previous; }
    } exit{previous};
    originalPaint(object,util);
}

// Read context fields through the *same* two virtual getters the original fit
// uses. SEH protects diagnostics; these are getters with no renderer mutation.
void* displayContext(void* util) noexcept {
    __try {
        const auto rendererGetter = reinterpret_cast<void* (*)(void*)>(slot(util,0x50));
        auto* renderer = rendererGetter ? rendererGetter(util) : nullptr;
        const auto contextGetter = reinterpret_cast<void* (*)(void*)>(slot(renderer,0x48));
        return contextGetter ? contextGetter(renderer) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void fitHook(void* util, const VideoRect* clip, const VideoRect* destination, const unsigned* picture, float aspect, const void* color) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-worldBase;
    ++frame.counts.fits;
    const bool scoped = frame.depth != 0;
    if (scoped) ++frame.fitDepth;
    if (internalDetail()) {
        const auto c=read<VideoRect>(clip), bounds=read<VideoRect>(clip,8), d=read<VideoRect>(destination);
        const auto w=read<unsigned>(picture), h=read<unsigned>(picture,4);
        auto* context=displayContext(util);
        const auto desired=desiredVideoRect({frame.viewport[0],frame.viewport[1],frame.viewport[0]+frame.viewport[2],frame.viewport[1]+frame.viewport[3]},w,h);
        trace(true,false,"fit entry callerRVA=%llX util=%p clipOffset=%d,%d clipBounds=%d,%d,%d,%d dst=%d,%d,%d,%d picture=%ux%u PIXELASPECT=%.7g display=%p screen(+44)=%.7g pixel(+48)=%.7g desiredPhysical=%d,%d,%d,%d",
            caller,util,
            c.left,c.top,bounds.left,bounds.top,bounds.right,bounds.bottom,d.left,d.top,d.right,d.bottom,w,h,aspect,
            context,read<float>(context,0x44),read<float>(context,0x48),desired.left,desired.top,desired.right,desired.bottom);
    }
    VideoRect corrected;
    // The shared helper is also used by menus. Require both the live paint
    // scope AND the verified video call site; decoder lifetime is irrelevant.
    if (aspectFix && scoped && caller==0x76463 && !frame.inBegin && currentContext && currentContext() &&
        getInteger && getFloat && getBoolean && isEnabled && setClearColor && setColorMask && disableGl && enableGl && clearGl) {
        viewportSnapshot();
        const auto d=read<VideoRect>(destination);
        const auto w=read<unsigned>(picture),h=read<unsigned>(picture,4);
        auto* context=displayContext(util);
        const float displayAspect=read<float>(context,0x44)*read<float>(context,0x48);
        if(w && h && d.right>d.left && d.bottom>d.top && frame.viewport[2]>0 && frame.viewport[3]>0 &&
            displayAspect>0 && std::isfinite(displayAspect)) {
            corrected=desiredVideoUiRect(d,w,h,unsigned(frame.viewport[2]),unsigned(frame.viewport[3]));
            destination=&corrected;
            // Cancel the original fit in its own normalised display units.
            // It still owns texture scale/offset, clipping, shader and draw.
            aspect=displayAspect;
            if(internalDetail()) trace(true,false,"aspect correction dst=%d,%d,%d,%d PIXELASPECT=%.7g; black clear before video draw",
                corrected.left,corrected.top,corrected.right,corrected.bottom,aspect);
            // The preceding background is not a contract to clear the drawable.
            // Clear color only, every video paint; preserve masks/scissor/color.
            GLfloat savedColor[4]{};GLboolean savedMask[4]{};
            getFloat(GL_COLOR_CLEAR_VALUE,savedColor);getBoolean(GL_COLOR_WRITEMASK,savedMask);
            const auto savedScissor=isEnabled(GL_SCISSOR_TEST);
            disableGl(GL_SCISSOR_TEST);setColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);setClearColor(0,0,0,1);
            clearGl(GL_COLOR_BUFFER_BIT);
            setClearColor(savedColor[0],savedColor[1],savedColor[2],savedColor[3]);
            setColorMask(savedMask[0],savedMask[1],savedMask[2],savedMask[3]);
            if(savedScissor) enableGl(GL_SCISSOR_TEST);
        }
    }
    struct Exit { bool scoped; ~Exit() { if (internalDetail()) trace(true,false,"fit exit (actual fitted geometry in rectangle_submit)"); if (scoped) --frame.fitDepth; } } exit{scoped};
    originalFit(util,clip,destination,picture,aspect,color);
}
void rectangleHook(void* util,const VideoRect* clip,const VideoRect* destination,const void* color) {
    const auto previous=frame.submission;
    if (frame.depth) {
        frame.submission=++ids; ++frame.counts.rectangles;
        if(!frame.firstSubmission) frame.firstSubmission=frame.submission;
        frame.lastSubmission=frame.submission;
        if (internalDetail()) {
            const auto r=read<VideoRect>(destination), cl=read<VideoRect>(clip), bounds=read<VideoRect>(clip,8);
            trace(true,false,"rectangle_submit kind=%s callerRVA=%llX util=%p rect=%d,%d,%d,%d clipOffset=%d,%d clipBounds=%d,%d,%d,%d color=%08X",
                frame.fitDepth ? "fitted_video" : "background_or_fallback",reinterpret_cast<std::uintptr_t>(_ReturnAddress())-worldBase,
                util,r.left,r.top,r.right,r.bottom,cl.left,cl.top,bounds.left,bounds.top,bounds.right,bounds.bottom,read<unsigned>(color));
        }
    } else if (frame.menu && internalDetail()) {
        const auto r=read<VideoRect>(destination);
        trace(true,false,"non_video rectangle_submit util=%p rect=%d,%d,%d,%d",util,r.left,r.top,r.right,r.bottom);
    }
    struct Restore { std::uint64_t previous; ~Restore() { frame.submission=previous; } } restore{previous};
    originalRectangle(util,clip,destination,color);
}
void dispatchHook(void* util,std::uintptr_t texture,const VideoRect* clip,const VideoRect* dst,const void* color,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c) {
    if (observing()) {
        ++frame.counts.dispatches;
        if (internalDetail()) trace(true,false,"rectangle_dispatch util=%p arg2=%llu dst=%p (synchronous submit scope; deferred GL has submit=0)",util,texture,dst);
    }
    originalDispatch(util,texture,clip,dst,color,a,b,c);
}

decltype(&glViewport) originalViewport=nullptr;
decltype(&glScissor) originalScissor=nullptr;
decltype(&glMatrixMode) originalMatrixMode=nullptr;
decltype(&glLoadMatrixf) originalLoadMatrixf=nullptr;
decltype(&glLoadMatrixd) originalLoadMatrixd=nullptr;
decltype(&glMultMatrixf) originalMultMatrixf=nullptr;
decltype(&glMultMatrixd) originalMultMatrixd=nullptr;
decltype(&glLoadIdentity) originalLoadIdentity=nullptr;
decltype(&glOrtho) originalOrtho=nullptr;
decltype(&glClear) originalClear=nullptr;
decltype(&glTexImage2D) originalTexImage=nullptr;
decltype(&glTexSubImage2D) originalTexSubImage=nullptr;
decltype(&glBindTexture) originalBindTexture=nullptr;
decltype(&glBegin) originalBegin=nullptr;
decltype(&glEnd) originalEnd=nullptr;
decltype(&glDrawArrays) originalDrawArrays=nullptr;
decltype(&glDrawElements) originalDrawElements=nullptr;
decltype(&glCallList) originalCallList=nullptr;
decltype(&glVertex2f) originalVertex2f=nullptr;
decltype(&glVertex3f) originalVertex3f=nullptr;
decltype(&glVertex2i) originalVertex2i=nullptr;
decltype(&glVertex3fv) originalVertex3fv=nullptr;
decltype(&glTexCoord2f) originalTexCoord2f=nullptr;
decltype(&glVertexPointer) originalVertexPointer=nullptr;
decltype(&glTexCoordPointer) originalTexCoordPointer=nullptr;

void WINAPI viewportHook(GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    shadowTraceEvent("glViewport %d,%d,%d,%d",x,y,w,h);
    if (observing()) { ++frame.counts.viewport;
        if (detail()) trace(true,false,"glViewport %d,%d,%d,%d",x,y,w,h); }
    frame.viewport[0]=x; frame.viewport[1]=y; frame.viewport[2]=w; frame.viewport[3]=h;
    originalViewport(x,y,w,h);
}
void WINAPI scissorHook(GLint x,GLint y,GLsizei w,GLsizei h) noexcept {
    shadowTraceEvent("glScissor %d,%d,%d,%d",x,y,w,h);
    if (observing()) { ++frame.counts.scissor;
        if (detail()) trace(true,false,"glScissor %d,%d,%d,%d",x,y,w,h); }
    originalScissor(x,y,w,h);
}
void WINAPI matrixModeHook(GLenum mode) noexcept {
    frame.matrixMode=mode;
    if (detail()) trace(true,false,"glMatrixMode %X",mode);
    originalMatrixMode(mode);
}
template<class T> void matrix(const char* name,const T* data) noexcept {
    if (observing() && frame.matrixMode==GL_PROJECTION) {
        ++frame.counts.projection;
        if (detail()) {
            T m[16]{}; const auto valid=copyMemory(m,data,sizeof(m));
            trace(true,false,"%s projection valid=%d matrix=%.6g,%.6g,%.6g,%.6g/%.6g,%.6g,%.6g,%.6g/%.6g,%.6g,%.6g,%.6g/%.6g,%.6g,%.6g,%.6g",
                name,valid,double(m[0]),double(m[1]),double(m[2]),double(m[3]),double(m[4]),double(m[5]),double(m[6]),double(m[7]),
                double(m[8]),double(m[9]),double(m[10]),double(m[11]),double(m[12]),double(m[13]),double(m[14]),double(m[15]));
        }
    }
}
void WINAPI loadMatrixfHook(const GLfloat* m) noexcept {
    if(traceFov) traceFovProjection(m,_ReturnAddress(),currentContext ? currentContext():nullptr);
    matrix("glLoadMatrixf",m); originalLoadMatrixf(m);
}
void WINAPI loadMatrixdHook(const GLdouble* m) noexcept { matrix("glLoadMatrixd",m); originalLoadMatrixd(m); }
void WINAPI multMatrixfHook(const GLfloat* m) noexcept { matrix("glMultMatrixf",m); originalMultMatrixf(m); }
void WINAPI multMatrixdHook(const GLdouble* m) noexcept { matrix("glMultMatrixd",m); originalMultMatrixd(m); }
void WINAPI loadIdentityHook() noexcept {
    if (observing() && frame.matrixMode==GL_PROJECTION) { ++frame.counts.projection; if(detail()) trace(true,false,"glLoadIdentity projection"); }
    originalLoadIdentity();
}
void WINAPI orthoHook(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f) noexcept {
    if (observing()) { ++frame.counts.projection; if(detail()) trace(true,false,"glOrtho mode=%X %.7g,%.7g,%.7g,%.7g,%.7g,%.7g",frame.matrixMode,l,r,b,t,n,f); }
    originalOrtho(l,r,b,t,n,f);
}
void WINAPI clearHook(GLbitfield mask) noexcept {
    eyeshineVariantClear(mask);
    eyeshineFixBeforeClear(mask);
    shadowTraceClear(mask);
    if(observing()) { ++frame.counts.clears; if(detail()) {
        GLfloat color[4]{}; GLboolean write[4]{}; GLint scissor[4]{};
        const bool queried=!frame.inBegin && getFloat && getBoolean && getInteger && isEnabled;
        GLboolean scissorEnabled=GL_FALSE;
        if(queried) { getFloat(GL_COLOR_CLEAR_VALUE,color);getBoolean(GL_COLOR_WRITEMASK,write);getInteger(GL_SCISSOR_BOX,scissor);scissorEnabled=isEnabled(GL_SCISSOR_TEST); }
        trace(true,false,"glClear mask=%X viewport=%d,%d,%d,%d stateValid=%d rgba=%.6g,%.6g,%.6g,%.6g write=%u%u%u%u scissor=%u:%d,%d,%d,%d",mask,
            frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3],queried,color[0],color[1],color[2],color[3],write[0],write[1],write[2],write[3],
            scissorEnabled,scissor[0],scissor[1],scissor[2],scissor[3]);
    } }
    originalClear(mask);
    eyeshineFixAfterDraw();
}
void WINAPI texImageHook(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void* pixels) noexcept {
    shadowTraceEvent("glTexImage2D target=%X level=%d size=%dx%d internal=%X format=%X type=%X data=%d",target,level,w,h,internal,format,type,pixels!=nullptr);
    if(observing()) { ++frame.counts.uploads; if(detail()) trace(true,false,"glTexImage2D target=%X level=%d size=%dx%d internal=%X format=%X type=%X",target,level,w,h,internal,format,type); }
    originalTexImage(target,level,internal,w,h,border,format,type,pixels);
}
void WINAPI texSubImageHook(GLenum target,GLint level,GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,const void* pixels) noexcept {
    if(observing()) { ++frame.counts.uploads; if(detail()) trace(true,false,"glTexSubImage2D target=%X level=%d offset=%d,%d size=%dx%d format=%X type=%X",target,level,x,y,w,h,format,type); }
    originalTexSubImage(target,level,x,y,w,h,format,type,pixels);
}
void WINAPI bindTextureHook(GLenum target,GLuint id) noexcept {
    shadowTraceEvent("glBindTexture target=%X id=%u",target,id);
    if(observing()) { ++frame.counts.binds; if(detail()) trace(true,false,"glBindTexture target=%X id=%u",target,id); }
    originalBindTexture(target,id);
}
void WINAPI beginHook(GLenum mode) noexcept {
    eyeshineFixBeforeDraw();
    shadowTraceDraw("glBegin",mode,0); // Query before entering glBegin/glEnd.
    if(!frame.inBegin) bloomTraceDraw("glBegin",_ReturnAddress());
    if(observing()) { ++frame.counts.begins; draw("glBegin",mode,0); }
    frame.inBegin=true; frame.verticesInBegin=0;
    originalBegin(mode);
}
void WINAPI endHook() noexcept {
    originalEnd(); frame.inBegin=false;
    eyeshineFixAfterDraw();
    bloomTraceDraw("glEnd",_ReturnAddress());
    if(observing()) { ++frame.counts.ends; draw("glEnd",0,frame.verticesInBegin); }
}
void WINAPI drawArraysHook(GLenum mode,GLint first,GLsizei count) noexcept {
    eyeshineFixBeforeDraw();
    shadowTraceDraw("glDrawArrays",mode,count);
    if(!frame.inBegin) bloomTraceDraw("glDrawArrays",_ReturnAddress());
    if(observing()) { ++frame.counts.arrays; draw("glDrawArrays",mode,count); arrays(first,count); }
    originalDrawArrays(mode,first,count);
    eyeshineFixAfterDraw();
}
void WINAPI drawElementsHook(GLenum mode,GLsizei count,GLenum type,const void* indices) noexcept {
    eyeshineFixBeforeDraw();
    shadowTraceDraw("glDrawElements",mode,count);
    if(!frame.inBegin) bloomTraceDraw("glDrawElements",_ReturnAddress());
    if(observing()) { ++frame.counts.elements; draw("glDrawElements",mode,count); arrays(0,count,type,indices); }
    originalDrawElements(mode,count,type,indices);
    eyeshineFixAfterDraw();
}
void WINAPI callListHook(GLuint list) noexcept {
    // Inside glBegin/glEnd only vertex data is legal: observers would query
    // state and raise GL_INVALID_OPERATION, so forward untouched.
    if(frame.inBegin) { originalCallList(list); return; }
    eyeshineFixBeforeDraw();
    shadowTraceDraw("glCallList",0,0);
    originalCallList(list);
    eyeshineFixAfterList();
}
void vertex(float x,float y,float z) noexcept {
    if(observing()) { ++frame.counts.vertices; ++frame.verticesInBegin;
        if(frame.verticesInBegin<=4 && detail()) trace(true,false,"vertex %.7g,%.7g,%.7g",x,y,z); }
}
void WINAPI vertex2fHook(GLfloat x,GLfloat y) noexcept { vertex(x,y,0); originalVertex2f(x,y); }
void WINAPI vertex3fHook(GLfloat x,GLfloat y,GLfloat z) noexcept { vertex(x,y,z); originalVertex3f(x,y,z); }
void WINAPI vertex2iHook(GLint x,GLint y) noexcept { vertex(float(x),float(y),0); originalVertex2i(x,y); }
void WINAPI vertex3fvHook(const GLfloat* v) noexcept { const auto x=read<float>(v),y=read<float>(v,4),z=read<float>(v,8); vertex(x,y,z); originalVertex3fv(v); }
void WINAPI texCoord2fHook(GLfloat s,GLfloat t) noexcept { if(frame.verticesInBegin<4 && detail()) trace(true,false,"texcoord %.7g,%.7g",s,t); originalTexCoord2f(s,t); }
void WINAPI vertexPointerHook(GLint size,GLenum type,GLsizei stride,const void* pointer) noexcept {
    frame.vertexArray={size,type,stride,pointer};
    if(detail()) trace(true,false,"glVertexPointer size=%d type=%X stride=%d pointer=%p",size,type,stride,pointer);
    originalVertexPointer(size,type,stride,pointer);
}
void WINAPI texCoordPointerHook(GLint size,GLenum type,GLsizei stride,const void* pointer) noexcept {
    frame.texcoordArray={size,type,stride,pointer};
    if(detail()) trace(true,false,"glTexCoordPointer size=%d type=%X stride=%d pointer=%p",size,type,stride,pointer);
    originalTexCoordPointer(size,type,stride,pointer);
}

struct Hook { const char* name; void* replacement; void** original; };
template<class T, class R> Hook hook(const char* name,R replacement,T* original) {
    const T compatible = replacement; // Includes signature checking, permits noexcept detours.
    return {name,reinterpret_cast<void*>(compatible),reinterpret_cast<void**>(original)};
}
bool create(void* target,const Hook& h) {
    const auto status=MH_CreateHook(target,h.replacement,h.original);
    trace(false,false,"hook %s: %s",h.name,MH_StatusToString(status));
    return status==MH_OK;
}
}

std::size_t installVideoTraceHooks(HMODULE module, VideoModule kind, const Settings& settings) {
    if (!settings.traceVideo && !settings.videoAspectFix && !(kind==VideoModule::OpenGl && (settings.traceFov || settings.traceShadowPass || settings.traceBloom || settings.eyeshineFix || settings.eyeshineFixVariant || settings.eyeshineAbBurst))) return 0;
    if(kind==VideoModule::System && !settings.traceVideo) return 0;
    enabled.store(settings.traceVideo);
    aspectFix=settings.videoAspectFix;
    std::size_t created=0;
    if(kind==VideoModule::OpenGl) {
        traceFov=settings.traceFov;
        currentContext=reinterpret_cast<decltype(currentContext)>(GetProcAddress(module,"wglGetCurrentContext"));
        currentDC=reinterpret_cast<decltype(currentDC)>(GetProcAddress(module,"wglGetCurrentDC"));
        getInteger=reinterpret_cast<decltype(getInteger)>(GetProcAddress(module,"glGetIntegerv"));
        getFloat=reinterpret_cast<decltype(getFloat)>(GetProcAddress(module,"glGetFloatv"));
        getBoolean=reinterpret_cast<decltype(getBoolean)>(GetProcAddress(module,"glGetBooleanv"));
        isEnabled=reinterpret_cast<decltype(isEnabled)>(GetProcAddress(module,"glIsEnabled"));
        setClearColor=reinterpret_cast<decltype(setClearColor)>(GetProcAddress(module,"glClearColor"));
        setColorMask=reinterpret_cast<decltype(setColorMask)>(GetProcAddress(module,"glColorMask"));
        disableGl=reinterpret_cast<decltype(disableGl)>(GetProcAddress(module,"glDisable"));
        enableGl=reinterpret_cast<decltype(enableGl)>(GetProcAddress(module,"glEnable"));
        clearGl=reinterpret_cast<decltype(clearGl)>(GetProcAddress(module,"glClear"));
        if(!settings.traceVideo && !settings.traceFov && !settings.traceShadowPass && !settings.traceBloom && !settings.eyeshineFix && !settings.eyeshineFixVariant && !settings.eyeshineAbBurst) return 0;
        const Hook hooks[]{
            hook("glViewport",&viewportHook,&originalViewport),hook("glScissor",&scissorHook,&originalScissor),
            hook("glMatrixMode",&matrixModeHook,&originalMatrixMode),hook("glLoadMatrixf",&loadMatrixfHook,&originalLoadMatrixf),
            hook("glLoadMatrixd",&loadMatrixdHook,&originalLoadMatrixd),hook("glMultMatrixf",&multMatrixfHook,&originalMultMatrixf),
            hook("glMultMatrixd",&multMatrixdHook,&originalMultMatrixd),hook("glLoadIdentity",&loadIdentityHook,&originalLoadIdentity),
            hook("glOrtho",&orthoHook,&originalOrtho),hook("glClear",&clearHook,&originalClear),
            hook("glTexImage2D",&texImageHook,&originalTexImage),hook("glTexSubImage2D",&texSubImageHook,&originalTexSubImage),
            hook("glBindTexture",&bindTextureHook,&originalBindTexture),hook("glBegin",&beginHook,&originalBegin),
            hook("glEnd",&endHook,&originalEnd),hook("glDrawArrays",&drawArraysHook,&originalDrawArrays),
            hook("glDrawElements",&drawElementsHook,&originalDrawElements),hook("glVertex2f",&vertex2fHook,&originalVertex2f),
            hook("glCallList",&callListHook,&originalCallList),
            hook("glVertex3f",&vertex3fHook,&originalVertex3f),hook("glVertex2i",&vertex2iHook,&originalVertex2i),
            hook("glVertex3fv",&vertex3fvHook,&originalVertex3fv),hook("glTexCoord2f",&texCoord2fHook,&originalTexCoord2f),
            hook("glVertexPointer",&vertexPointerHook,&originalVertexPointer),hook("glTexCoordPointer",&texCoordPointerHook,&originalTexCoordPointer)
        };
        unsigned drawHooks=0;
        const bool drawNeeded=settings.traceVideo || settings.traceShadowPass || settings.traceBloom || settings.eyeshineFix || settings.eyeshineFixVariant || settings.eyeshineAbBurst;
        bool clearReady=false;
        for(const auto& h:hooks) {
            if(!drawNeeded && std::strcmp(h.name,"glLoadMatrixf")) continue;
            // Draw exports are shared by all observers; copy exports belong to shadow_trace.
            const bool draw = !std::strcmp(h.name,"glBegin") || !std::strcmp(h.name,"glEnd") ||
                !std::strcmp(h.name,"glDrawArrays") || !std::strcmp(h.name,"glDrawElements");
            const bool shadow = settings.traceShadowPass || settings.eyeshineFix || settings.eyeshineFixVariant || settings.eyeshineAbBurst;
            const bool shadowState = !std::strcmp(h.name,"glViewport") || !std::strcmp(h.name,"glScissor") ||
                !std::strcmp(h.name,"glClear") || !std::strcmp(h.name,"glTexImage2D") || !std::strcmp(h.name,"glBindTexture") || !std::strcmp(h.name,"glCallList");
            const bool fov = settings.traceFov && !std::strcmp(h.name,"glLoadMatrixf");
            if (!settings.traceVideo && !draw && !(shadow && shadowState) && !fov) continue;
            const auto target=GetProcAddress(module,h.name);
            if(target) {
                const bool success=create(reinterpret_cast<void*>(target),h);created+=success;
                if(!std::strcmp(h.name,"glClear")) clearReady=success;
                if(success && (!std::strcmp(h.name,"glBegin") || !std::strcmp(h.name,"glEnd") ||
                    !std::strcmp(h.name,"glDrawArrays") || !std::strcmp(h.name,"glDrawElements") || !std::strcmp(h.name,"glCallList"))) ++drawHooks;
            }
            else trace(false,false,"hook %s: missing export; skipped",h.name);
        }
        if(drawNeeded && (drawHooks!=5 || !clearReady)) {
            disableEyeshineFix();
            logLineNoThrow("EyeshineFix skipped: incomplete draw hook set");
        }
        if(settings.traceBloom) logf("TraceBloom GL observation hooks created=%zu (4 draw hooks for bloom-only; copy hooks owned by shadow_trace)",created);
        trace(false,false,"trace: 32 GL + 12 reserved internal events/sampled frame; bursts then 1Hz; 2000 lines maximum; VideoAspectFix=%d; raw PAR ignored for desiredPhysical",settings.videoAspectFix);
        return created;
    }
    MODULEINFO info{};
    if (!module || !K32GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info))) {
        trace(false,false,"internal hooks: invalid module; skipped"); return 0;
    }
    if(kind==VideoModule::World) worldBase=reinterpret_cast<std::uintptr_t>(module);
    const Hook hooks[]{hook("decoder_start",&startHook,&originalStart),hook("decoder_close",&closeHook,&originalClose),
        hook("decoder_owner",&ownerHook,&originalOwner),hook("video_paint",&paintHook,&originalPaint),
        hook("aspect_fit",&fitHook,&originalFit),hook("rectangle_submit",&rectangleHook,&originalRectangle),
        hook("rectangle_dispatch",&dispatchHook,&originalDispatch)};
    const auto& sites=videoHookSites();
    for(std::size_t i=0;i<sites.size();++i) {
        const auto& site=sites[i]; if(site.module!=kind) continue;
        const bool fixSite=kind==VideoModule::World && (site.rva==0x755D0 || site.rva==0x39AD0);
        if(!settings.traceVideo && !fixSite) continue;
        // Bounded copy with SEH; require the exact prologues covered by binary tests.
        const auto* base=static_cast<const std::uint8_t*>(info.lpBaseOfDll);
        std::uint8_t bytes[64]{};
        if(site.rva>info.SizeOfImage || site.expected.size()>info.SizeOfImage-site.rva || site.expected.size()>sizeof(bytes) ||
            !copyMemory(bytes,base+site.rva,site.expected.size()) ||
            !std::equal(site.expected.begin(),site.expected.end(),bytes)) {
            trace(false,false,"hook %s RVA=%X: prologue mismatch/bad address; skipped",site.name,site.rva);
            if(settings.videoAspectFix && fixSite) logLineNoThrow(site.rva==0x755D0 ?
                "VideoAspectFix video_paint: prologue mismatch/bad address; skipped" :
                "VideoAspectFix aspect_fit: prologue mismatch/bad address; skipped");
            continue;
        }
        const bool installed=create(const_cast<std::uint8_t*>(base)+site.rva,hooks[i]);
        created+=installed;
        if(settings.videoAspectFix && fixSite) {
            char message[128]{};
            std::snprintf(message,sizeof(message),"VideoAspectFix %s: %s",site.name,installed ? "hook created" : "hook creation failed; skipped");
            logLineNoThrow(message);
        }
    }
    return created;
}

void videoTraceProgramBind(unsigned target,unsigned program) noexcept {
    if(observing()) { ++frame.counts.programs; if(detail()) trace(true,false,"program bind target=%X id=%u",target,program); }
}

void videoTraceSwap(HDC dc,BOOL result) noexcept {
    if(!enabled.load(std::memory_order_relaxed)) return;
    viewportSnapshot();
    const auto& c=frame.counts;
    const bool firstMenu=frame.menu && !frame.pending;
    if(firstMenu) frame.sample=true;
    if(frame.sample) {
        for(unsigned i=0;i<frame.buffered;++i) emit(frame.events[i].data(),true,false);
        trace(false,true,"SwapBuffers dc=%p result=%d skippedSwaps=%llu video=%u submissions=%llu..%llu viewport=%d,%d,%d,%d paint=%u fit=%u rect=%u dispatch=%u vp=%u scissor=%u projection=%u clear=%u upload=%u bind=%u program=%u begin=%u end=%u arrays=%u elements=%u vertices=%u",
        dc,result,frame.skipped,frame.pending,frame.firstSubmission,frame.lastSubmission,frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3],
        c.paints,c.fits,c.rectangles,c.dispatches,c.viewport,c.scissor,c.projection,c.clears,c.uploads,c.binds,c.programs,c.begins,c.ends,c.arrays,c.elements,c.vertices);
    }
    frame.skipped=frame.sample ? 0 : frame.skipped+1;
    const auto now=GetTickCount64();
    frame.sample=frame.frame<3 || now>=frame.nextSample || (frame.pending && !frame.menu);
    if(frame.sample) frame.nextSample=now+1000;
    // Keep one non-video frame after each video frame for deferred rendering and
    // the menu transition, without treating decoder lifetime as visibility.
    frame.menu=frame.pending;
    if(firstMenu) frame.sample=true;
    frame.pending=false; frame.eventLines=0; frame.internalLines=0; frame.buffered=0; frame.counts={};
    frame.firstSubmission=frame.lastSubmission=0; ++frame.frame;
}
