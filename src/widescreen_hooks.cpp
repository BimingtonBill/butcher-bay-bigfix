#include "widescreen.h"
#include "log.h"
#include <MinHook.h>
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
bool copy(void* to,const void* from,std::size_t size) noexcept {
    __try { if(!from || !to) return false; std::memcpy(to,from,size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool read(const void* object,std::size_t offset,T& result) noexcept {
    return object && copy(&result,static_cast<const char*>(object)+offset,sizeof(result));
}
using Overlay=void (*)(void*,void*,void*,void*);
using Rebuild=void (*)(void*);
using Camera=void* (*)(void*,void*); // tail call to memcpy returns the saved viewport
using ClientData=void* (*)(void*);
using ViewVertices=const float* (*)(void*);
Overlay originalHud=nullptr,originalSubtitles=nullptr;
Rebuild originalRebuild=nullptr;
Camera originalCamera=nullptr;
ClientData originalClientData=nullptr;
ViewVertices originalViewVertices=nullptr;
std::atomic<bool> menuCallerVerified{false};
std::atomic<std::uintptr_t> worldBase{0},classesBase{0},rendererBase{0};
std::atomic<double> configuredFov{0};
std::atomic<bool> traceEnabled{false};
std::atomic<unsigned long long> frame{0};
std::atomic<unsigned> traceLines{0};
std::atomic<unsigned> preGameplayLines{0};
std::atomic<unsigned long long> firstGameplayFrame{~0ull};
std::mutex installMutex;
bool traceBudget() noexcept {
    if(firstGameplayFrame.load()==~0ull && preGameplayLines.fetch_add(1)>=96) return false;
    const auto line=traceLines.fetch_add(1);
    if(line<399) return true;
    if(line==399) logLineNoThrow("TraceFov 400-line limit reached; further uploads suppressed");
    return false;
}

struct HudScope;
thread_local HudScope* activeHud=nullptr;
struct HudScope {
    HudScope* previous=activeHud;
    void* util=nullptr;
    std::array<float,16> baseline{};
    bool mapped=false;
    void apply() noexcept {
        // Rebuilt matrix already includes the engine's clip/depth/projection.
        // Precompose logical coordinates so glyphs, shadows, CPU clipping and
        // queued vertices use the same mapping. Never change the GL viewport.
        mapped=false;
        std::array<int,4> rect{};
        if(!read(util,0x150,rect) || !read(util,0xbc,baseline)) return;
        const long long w=static_cast<long long>(rect[2])-rect[0],h=static_cast<long long>(rect[3])-rect[1];
        if(w<=0 || h<=0 || w>32768 || h>32768) return;
        const auto mapping=hudCanvasMapping(static_cast<int>(w),static_cast<int>(h));
        auto matrix=baseline;
        mapHudMatrix(matrix,mapping);
        mapped=copy(static_cast<char*>(util)+0xbc,matrix.data(),sizeof(matrix));
    }
    explicit HudScope(void* receiver) noexcept {
        for(auto* scope=previous;scope;scope=scope->previous) if(scope->util==receiver) return;
        std::uintptr_t table=0;
        if(!originalRebuild || !worldBase || !read(receiver,0,table) || table!=worldBase+0x1e59c8) return;
        util=receiver; activeHud=this; apply();
    }
    ~HudScope() {
        if(!util) return;
        if(mapped) copy(static_cast<char*>(util)+0xbc,baseline.data(),sizeof(baseline));
        activeHud=previous;
    }
};
void rebuild(void* util) {
    originalRebuild(util);
    for(auto* scope=activeHud;scope;scope=scope->previous) if(scope->util==util) { scope->apply(); break; }
}
void hud(void* object,void* client,void* renderer,void* util) {
    const HudScope scope(util); originalHud(object,client,renderer,util);
}
void subtitles(void* object,void* client,void* renderer,void* util) {
    const HudScope scope(util); originalSubtitles(object,client,renderer,util);
}

struct CameraScope { bool gameplay=false; };
thread_local CameraScope* activeCamera=nullptr;
void* clientData(void* object) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    auto* result=originalClientData(object);
    if(activeCamera && caller==classesBase+0x20e2ac) {
        unsigned flags=0; unsigned char mode=0;
        activeCamera->gameplay=result && read(object,0x150,flags) && read(result,0x10f4,mode) && isGameplayCamera(flags,mode);
    }
    return result;
}
const float* menuVertices(void* viewport) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto* original=originalViewVertices(viewport); // Updates the engine projection first.
    if(!menuCallerVerified || caller!=worldBase+0x48f62) return original;
    std::array<float,16> projection{};
    std::array<int,4> view{},clip{};
    thread_local std::array<float,15> vertices{};
    if(!copy(vertices.data(),original,sizeof(vertices)) || !read(viewport,0x58,projection) ||
       !read(viewport,0x44,view) || !read(viewport,0x34,clip)) return original;
    if(clip[2]<clip[0] || clip[3]<clip[1]) return original;
    for(int i=0;i<4;++i) view[i]=(std::clamp)(view[i],clip[i%2],clip[i%2+2]);
    const long long w=static_cast<long long>(view[2])-view[0],h=static_cast<long long>(view[3])-view[1];
    if(w<=0 || h<=0 || w>32768 || h>32768 ||
       !alignMenuQuad(vertices,projection,static_cast<int>(w),static_cast<int>(h))) return original;
    // The sole caller copies all five vertices to its own stack immediately.
    // No engine-owned array, global viewport or input coordinates are changed.
    return vertices.data();
}
void cameraTrace(void* viewport,bool gameplay,float before,float after) noexcept {
    if(!traceEnabled) return;
    const auto n=frame.load();
    // Reserve the projection budget; camera records only in sampled frames.
    const auto first=firstGameplayFrame.load();
    if(n>1 && !(n>=first && n-first<2) && n%120!=0) return;
    static thread_local unsigned long long last=~0ull;
    if(last==n) return; last=n;
    if(!traceBudget()) return;
    std::array<int,4> rect{}; read(viewport,0x44,rect);
    try { logf("TraceFov camera frame=%llu tid=%lu gameplay=%d authored=%.7g output=%.7g rect=%d,%d,%d,%d hands=unresolved",
        n,GetCurrentThreadId(),gameplay,before,after,rect[0],rect[1],rect[2],rect[3]); } catch(...) {}
}
void* camera(void* client,void* output) {
    CameraScope scope;
    const auto previous=activeCamera; activeCamera=&scope;
    struct Restore { CameraScope* previous; ~Restore() { activeCamera=previous; } } restore{previous};
    auto* result=originalCamera(client,output);
    if(scope.gameplay) {
        auto unseen=~0ull;
        firstGameplayFrame.compare_exchange_strong(unseen,frame.load());
    }
    unsigned mode=1; float authored=0;
    if(!read(output,0,mode) || !read(output,4,authored)) return result;
    float corrected=authored;
    if(scope.gameplay && mode==0) {
        corrected=static_cast<float>(relativeGameplayFov(authored,configuredFov.load()));
        if(corrected!=authored) {
            // Keep the engine camera, culling and saved viewport coherent. The
            // saved copy is NOT proof of an independent hands projection.
            std::array<unsigned char,0x13c> snapshot{};
            if(read(output,0,snapshot)) {
                std::memcpy(snapshot.data()+4,&corrected,sizeof(corrected)); snapshot[0x54]=1;
                if(copy(static_cast<char*>(client)+0x78c,snapshot.data(),snapshot.size()))
                    copy(output,snapshot.data(),snapshot.size());
                else corrected=authored;
            } else corrected=authored;
        }
    }
    cameraTrace(output,scope.gameplay,authored,corrected);
    return result;
}
bool verify(HMODULE module,const PatchSite& site) noexcept {
    MODULEINFO info{};
    if(!module || !K32GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info)) ||
       site.expected.empty() || site.rva>info.SizeOfImage || site.expected.size()>info.SizeOfImage-site.rva) return false;
    std::array<unsigned char,128> bytes{};
    return site.expected.size()<=bytes.size() && copy(bytes.data(),static_cast<char*>(info.lpBaseOfDll)+site.rva,site.expected.size()) &&
        std::equal(site.expected.begin(),site.expected.end(),bytes.begin());
}
}

std::size_t installWidescreenHooks(HMODULE module,WidescreenModule kind,const Settings& settings) {
    const std::lock_guard<std::mutex> lock(installMutex);
    traceEnabled=settings.traceFov;
    configuredFov=settings.fov;
    if(!module || (!settings.hudAspectFix && settings.fov==0 && !settings.traceFov)) return 0;
    std::size_t count=0;
    if(kind==WidescreenModule::World) worldBase=reinterpret_cast<std::uintptr_t>(module);
    if(kind==WidescreenModule::Classes) classesBase=reinterpret_cast<std::uintptr_t>(module);
    // Validate semantic callers as well as entry prologues before enabling FOV.
    bool cameraCallers=true,menuLayout=true;
    for(const auto& item:widescreenSites()) if(item.module==kind && item.patch.name[0]=='!') {
        const bool valid=verify(module,item.patch);
        if(std::strstr(item.patch.name,"menu")) {
            menuLayout=menuLayout && valid;
            if(kind==WidescreenModule::World) menuCallerVerified=valid;
        } else cameraCallers=cameraCallers && valid;
        if(!valid) logLineNoThrow("Widescreen semantic caller bytes mismatch; associated feature skipped");
    }
    for(const auto& item:widescreenSites()) {
        const auto& site=item.patch;
        if(item.module!=kind || site.name[0]=='!') continue;
        void* detour=nullptr; void** original=nullptr;
        if(site.rva==0x36bb0 && settings.hudAspectFix) { detour=reinterpret_cast<void*>(&rebuild); original=reinterpret_cast<void**>(&originalRebuild); }
        if(site.rva==0x61af0 && settings.hudAspectFix) { detour=reinterpret_cast<void*>(&subtitles); original=reinterpret_cast<void**>(&originalSubtitles); }
        if(site.rva==0x21c500 && settings.hudAspectFix) { detour=reinterpret_cast<void*>(&hud); original=reinterpret_cast<void**>(&originalHud); }
        if(site.rva==0x136ef0 && (settings.fov!=0 || settings.traceFov)) { detour=reinterpret_cast<void*>(&camera); original=reinterpret_cast<void**>(&originalCamera); }
        if(site.rva==0x1e33b0 && cameraCallers && (settings.fov!=0 || settings.traceFov)) { detour=reinterpret_cast<void*>(&clientData); original=reinterpret_cast<void**>(&originalClientData); }
        if(site.rva==0x57790 && settings.hudAspectFix && menuLayout) { detour=reinterpret_cast<void*>(&menuVertices); original=reinterpret_cast<void**>(&originalViewVertices); }
        if(site.rva==0x9754 && settings.traceFov) {
            if(verify(module,site)) rendererBase=reinterpret_cast<std::uintptr_t>(module);
            else logLineNoThrow("TraceFov projection upload bytes mismatch; skipped");
            continue;
        }
        if(!original || *original) continue;
        if(!verify(module,site)) {
            try { logf("Widescreen %s RVA=%X: prologue mismatch/bad address; skipped",site.name,site.rva); } catch(...) {}
            continue;
        }
        auto* target=reinterpret_cast<char*>(module)+site.rva;
        const auto status=MH_CreateHook(target,detour,original);
        if(status==MH_OK) { ++count; try { logf("Widescreen %s RVA=%X: verified; hook created",site.name,site.rva); } catch(...) {} }
        else { *original=nullptr; logLineNoThrow("Widescreen hook creation failed; skipped"); }
    }
    return count;
}

void traceFovProjection(const float* matrix,const void* caller,HGLRC context) noexcept {
    if(!traceEnabled || !rendererBase || reinterpret_cast<std::uintptr_t>(caller)!=rendererBase+0x975a) return;
    // Log all uploads (in order) in each sampled frame; no deduplication hides a
    // second world/hands matrix. A hard shared cap prevents runaway logs.
    const auto n=frame.load();
    const auto first=firstGameplayFrame.load();
    if(n>1 && !(n>=first && n-first<2) && n%120!=0) return;
    static thread_local unsigned long long lastFrame=~0ull;
    static thread_local unsigned uploads=0;
    if(lastFrame!=n) { lastFrame=n; uploads=0; }
    if(uploads++>=48) return; // Menus cannot consume the gameplay budget.
    if(!traceBudget()) return;
    std::array<float,16> m{};
    if(!copy(m.data(),matrix,sizeof(m))) return;
    constexpr double degrees=360/3.14159265358979323846;
    const auto h=degrees*std::atan(1/std::abs(double(m[0]))),v=degrees*std::atan(1/std::abs(double(m[5])));
    try { logf("TraceFov upload frame=%llu tid=%lu ctx=%p caller=RndrGL+975A H=%.6g V=%.6g matrix=%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g/%.7g,%.7g,%.7g,%.7g",
        n,GetCurrentThreadId(),context,h,v,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]); } catch(...) {}
}
void widescreenFrameEnd(BOOL success) noexcept { if(success) ++frame; }
