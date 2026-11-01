// Derived from TCoRFix v1.4 by Nezarn (MIT).
#include "gl_hooks.h"
#include "gl_extensions.h"
#include "shader_rewrite.h"
#include "log.h"
#include "video_trace.h"
#include "shadow_trace.h"
#include "eyeshine_variants.h"
#include "eyeshine_fix.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace {
std::atomic<bool> bloomEnabled{false};
constexpr GLenum maxTextureUnits = 0x84E2, maxTextureImageUnits = 0x8872;
constexpr GLenum fragmentProgram = 0x8804, samplesPassed = 0x8914;
template<class... Args> void diagnostic(const char* format, Args... args) noexcept {
    try { logf(format, args...); } catch (...) { /* Diagnostics cannot change GL results. */ }
}
bool validProc(PROC proc) {
    const auto value = reinterpret_cast<std::uintptr_t>(proc);
    return value > 3 && value != ~std::uintptr_t(0);
}
template<class T> T resolve(GlApi& api, const char* name) {
    const auto proc = api.getProcAddress(name);
    return validProc(proc) ? reinterpret_cast<T>(proc) : nullptr;
}
template<class T> T requireProc(T& proc, GlApi& api, const char* name) {
    if (!proc) proc = resolve<T>(api, name);
    if (!proc) throw std::runtime_error(std::string("GL entry point unavailable: ") + name);
    return proc;
}
void dumpProgram(const std::filesystem::path& dir, std::string_view source) noexcept {
    try {
        if (dir.empty()) throw std::runtime_error("plugin directory unavailable");
        char name[16];
        std::snprintf(name, sizeof(name), "%08X.fp", shaderProgramCrc32(source));
        const auto folder = dir / L"shader_dump";
        std::filesystem::create_directories(folder);
        std::ofstream out(folder / name, std::ios::binary | std::ios::trunc);
        out.write(source.data(), static_cast<std::streamsize>(source.size()));
        if (!out) throw std::runtime_error("shader dump write failed");
    } catch (...) { logLineNoThrow("Shader dump failed; continuing program upload"); }
}

// WGL function pointers depend on the context/driver. Retain each dispatch table
// and its immutable extension string, even after the context is deleted.
struct Context {
    GlApi api;
    GlHooks hooks;
    Context(const GlApi& exports, const Settings& settings, const std::filesystem::path& dir)
        : api(exports), hooks(api, settings, dir) {}
};
struct Runtime {
    GlApi exports;
    Settings settings;
    std::filesystem::path dir;
    std::mutex mutex;
    // Active lookups only. Context allocations intentionally live until process
    // exit, preserving all extension pointers even after HGLRC deletion/reuse.
    std::map<HGLRC, Context*> contexts;
};
Runtime& runtime() {
    // Returned pointers also remain valid during CRT teardown. DllMain pins ASI.
    static auto* value = new Runtime;
    return *value;
}
GlHooks* currentHooks() {
    auto& state = runtime();
    const auto context = state.exports.getCurrentContext();
    if (!context) return nullptr;
    const std::lock_guard<std::mutex> lock(state.mutex);
    auto& entry = state.contexts[context];
    if (!entry) entry = new Context(state.exports, state.settings, state.dir);
    return &entry->hooks;
}
BOOL WINAPI deleteContextHook(HGLRC context) noexcept {
    auto& state = runtime();
    const BOOL result = state.exports.deleteContext(context);
    const DWORD error = GetLastError();
    if (result) {
        eyeshineVariantContextDeleted(context);
        eyeshineFixContextDeleted(context);
        try {
            const std::lock_guard<std::mutex> lock(state.mutex);
            state.contexts.erase(context);
        } catch (...) { logLineNoThrow("GL context cache retirement failed"); }
    }
    SetLastError(error);
    return result;
}
template<class F> void invoke(F&& call) noexcept {
    try { if (auto* hooks = currentHooks()) call(*hooks); }
    catch (...) { logLineNoThrow("GL hook failed; exception contained"); }
}
const GLubyte* WINAPI getStringHook(GLenum name) noexcept {
    try { if (auto* hooks = currentHooks()) return hooks->getString(name); }
    catch (...) {
        logLineNoThrow("GL string filter failed");
        if (name == GL_EXTENSIONS) return reinterpret_cast<const GLubyte*>("");
    }
    return runtime().exports.getString(name);
}
void WINAPI getIntegervHook(GLenum name, GLint* data) noexcept {
    try { if (auto* hooks = currentHooks()) { hooks->getIntegerv(name, data); return; } }
    catch (...) { logLineNoThrow("GL integer hook failed; forwarding original query"); }
    runtime().exports.getIntegerv(name, data);
}
void WINAPI colorHook(GLfloat r, GLfloat g, GLfloat b, GLfloat a) noexcept {
    try { if (auto* hooks = currentHooks()) { hooks->color4f(r, g, b, a); return; } }
    catch (...) { logLineNoThrow("GL color remap unavailable; forwarding glColor4f"); }
    runtime().exports.color4f(r, g, b, a);
}
PROC WINAPI getProcHook(LPCSTR name) noexcept {
    try { if (auto* hooks = currentHooks()) return hooks->getProcAddress(name); }
    catch (...) { logLineNoThrow("WGL procedure hook failed; forwarding original lookup"); }
    return runtime().exports.getProcAddress(name);
}
void WINAPI programHook(GLenum target, GLenum format, GLsizei length, const void* text) noexcept {
    try { if (auto* hooks = currentHooks()) { hooks->programString(target, format, length, text); return; } }
    catch (...) { logLineNoThrow("GL program rewrite failed; forwarding original program"); }
    auto& api = runtime().exports;
    if (const auto original = resolve<decltype(api.programString)>(api, "glProgramStringARB")) original(target, format, length, text);
}
void WINAPI bindProgramHook(GLenum target, GLuint program) noexcept {
    try { if (auto* hooks = currentHooks()) { hooks->bindProgram(target, program); return; } }
    catch (...) { logLineNoThrow("Video program bind trace failed; forwarding bind"); }
    if (const auto original = resolve<decltype(GlApi::bindProgram)>(runtime().exports, "glBindProgramARB")) original(target, program);
}
void WINAPI genHook(GLsizei n, GLuint* ids) noexcept { invoke([&](GlHooks& h) { h.genOcclusionQueries(n, ids); }); }
void WINAPI deleteHook(GLsizei n, const GLuint* ids) noexcept { invoke([&](GlHooks& h) { h.deleteOcclusionQueries(n, ids); }); }
GLboolean WINAPI isHook(GLuint id) noexcept {
    try { if (auto* hooks = currentHooks()) return hooks->isOcclusionQuery(id); }
    catch (...) { logLineNoThrow("GL query hook failed"); }
    return GL_FALSE;
}
void WINAPI beginHook(GLuint id) noexcept { invoke([&](GlHooks& h) { h.beginOcclusionQuery(id); }); }
void WINAPI endHook() noexcept { invoke([](GlHooks& h) { h.endOcclusionQuery(); }); }
void WINAPI queryIvHook(GLuint id, GLenum name, GLint* params) noexcept { invoke([&](GlHooks& h) { h.getOcclusionQueryiv(id, name, params); }); }
void WINAPI queryUivHook(GLuint id, GLenum name, GLuint* params) noexcept { invoke([&](GlHooks& h) { h.getOcclusionQueryuiv(id, name, params); }); }

template<auto Member> void bind(GlApi& api, PROC proc) {
    api.*Member = reinterpret_cast<std::remove_reference_t<decltype(api.*Member)>>(proc);
}
struct ProcHook {
    const char* requested;
    const char* replacement;
    PROC hook;
    void (*bind)(GlApi&, PROC);
    bool anyGpuOnly;
};
const ProcHook procHooks[] = {
    {"glProgramStringARB", "glProgramStringARB", reinterpret_cast<PROC>(&programHook), &bind<&GlApi::programString>, false},
    {"glGenOcclusionQueriesNV", "glGenQueriesARB", reinterpret_cast<PROC>(&genHook), &bind<&GlApi::genQueries>, true},
    {"glDeleteOcclusionQueriesNV", "glDeleteQueriesARB", reinterpret_cast<PROC>(&deleteHook), &bind<&GlApi::deleteQueries>, true},
    {"glIsOcclusionQueryNV", "glIsQueryARB", reinterpret_cast<PROC>(&isHook), &bind<&GlApi::isQuery>, true},
    {"glBeginOcclusionQueryNV", "glBeginQueryARB", reinterpret_cast<PROC>(&beginHook), &bind<&GlApi::beginQuery>, true},
    {"glEndOcclusionQueryNV", "glEndQueryARB", reinterpret_cast<PROC>(&endHook), &bind<&GlApi::endQuery>, true},
    {"glGetOcclusionQueryivNV", "glGetQueryObjectivARB", reinterpret_cast<PROC>(&queryIvHook), &bind<&GlApi::getQueryObjectiv>, true},
    {"glGetOcclusionQueryuivNV", "glGetQueryObjectuivARB", reinterpret_cast<PROC>(&queryUivHook), &bind<&GlApi::getQueryObjectuiv>, true}
};
}

GlHooks::GlHooks(GlApi& api, Settings settings, std::filesystem::path dir)
    : api_(api), settings_(settings), pluginDir_(std::move(dir)), bloom_(settings.traceBloom) {}
const GLubyte* GlHooks::getString(GLenum name) {
    if (name != GL_EXTENSIONS) return api_.getString(name);
    if (!extensionsReady_) {
        const auto driver = api_.getString(name);
        if (!driver) return nullptr;
        extensions_ = filterExtensions(reinterpret_cast<const char*>(driver), settings_.enable20PlusPlusAnyGpu);
        extensionsReady_ = true;
        const auto vendor = api_.getString(GL_VENDOR), renderer = api_.getString(GL_RENDERER), version = api_.getString(GL_VERSION);
        diagnostic("GL context: vendor=%s renderer=%s version=%s; extensions filtered to %zu bytes",
             vendor ? reinterpret_cast<const char*>(vendor) : "unknown",
             renderer ? reinterpret_cast<const char*>(renderer) : "unknown",
             version ? reinterpret_cast<const char*>(version) : "unknown", extensions_.size());
    }
    return reinterpret_cast<const GLubyte*>(extensions_.c_str());
}
void GlHooks::getIntegerv(GLenum name, GLint* data) {
    api_.getIntegerv(settings_.fixMiscBugs && name == maxTextureUnits ? maxTextureImageUnits : name, data);
}
void GlHooks::color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    // TCoRFix feeds generic input 3 explicitly for the any-GPU program path.
    // ARB_vertex_program need not alias it with conventional colour: retain
    // glColor4f for fixed-function/raster colour and vertex.color consumers.
    // Resolve first so colorHook's failure forwarding calls glColor4f once.
    const auto attrib = settings_.enable20PlusPlusAnyGpu ?
        requireProc(api_.vertexAttrib4f, api_, "glVertexAttrib4f") : nullptr;
    api_.color4f(r, g, b, a);
    // Last on purpose: preserve the original generic value on NV aliasing
    // drivers too. Both commands are legal within glBegin/glEnd; no queries.
    if (attrib) attrib(3, r, g, b, a);
}
PROC GlHooks::getProcAddress(LPCSTR name) {
    if (!name) return api_.getProcAddress(name);
    if ((settings_.traceVideo || settings_.traceShadowPass || settings_.traceBloom) && !std::strcmp(name, "glBindProgramARB")) {
        const auto original = api_.getProcAddress(name);
        if (validProc(original)) {
            api_.bindProgram = reinterpret_cast<decltype(api_.bindProgram)>(original);
            return reinterpret_cast<PROC>(&bindProgramHook);
        }
        return original;
    }
    for (const auto& entry : procHooks) {
        if (std::strcmp(name, entry.requested)) continue;
        if (entry.anyGpuOnly ? !settings_.enable20PlusPlusAnyGpu :
            !(settings_.enable20PlusPlusAnyGpu || settings_.eyeshineFlickerWorkaround || settings_.dumpShaders || settings_.traceShadowPass || settings_.traceBloom || settings_.eyeshineFix || settings_.eyeshineFixVariant || settings_.eyeshineAbBurst)) break;
        const auto proc = api_.getProcAddress(entry.replacement);
        if (!validProc(proc)) {
            diagnostic("GL hook %s skipped: %s unavailable", name, entry.replacement);
            return api_.getProcAddress(name);
        }
        entry.bind(api_, proc);
        return entry.hook;
    }
    return api_.getProcAddress(name);
}
void GlHooks::programString(GLenum target, GLenum format, GLsizei length, const void* text) {
    const auto original = requireProc(api_.programString, api_, "glProgramStringARB");
    if (!text || length < 0) { original(target, format, length, text); return; }
    const std::string_view source(static_cast<const char*>(text), static_cast<size_t>(length));
    if (settings_.dumpShaders) dumpProgram(pluginDir_, source);
    if (target != fragmentProgram) {
        original(target, format, length, text);
        if(settings_.traceBloom) bloom_.upload(bloomApi(),target,source,source);
        return;
    }
    if (settings_.traceShadowPass) shadowTraceProgram(target, source);
    eyeshineFixProgram(target,source);
    const auto rewritten = eyeshineVariantProgram(rewriteFragmentProgram(source, settings_.enable20PlusPlusAnyGpu, settings_.eyeshineFlickerWorkaround));
    if (rewritten.size() > static_cast<size_t>((std::numeric_limits<GLsizei>::max)())) throw std::length_error("rewritten GL program too large");
    original(target, format, static_cast<GLsizei>(rewritten.size()), rewritten.data());
    if(settings_.traceBloom) bloom_.upload(bloomApi(),target,source,rewritten);
}
void GlHooks::bindProgram(GLenum target, GLuint program) {
    const auto original = requireProc(api_.bindProgram, api_, "glBindProgramARB");
    videoTraceProgramBind(target, program);
    original(target, program);
    if (settings_.traceShadowPass) shadowTraceProgramBind(target,program);
    if(settings_.traceBloom) bloom_.draw(bloomApi(),"glBindProgramARB",nullptr);
}

const BloomApi& GlHooks::bloomApi() {
    if(!bloomReady_) {
        auto& api=api_.bloom;
        api.getProgram=resolve<decltype(api.getProgram)>(api_,"glGetProgramivARB");
        api.getEnv=resolve<decltype(api.getEnv)>(api_,"glGetProgramEnvParameterfvARB");
        api.getAttrib=resolve<decltype(api.getAttrib)>(api_,"glGetVertexAttribfvARB");
        if(!api.getAttrib) api.getAttrib=resolve<decltype(api.getAttrib)>(api_,"glGetVertexAttribfv");
        api.activeTexture=resolve<decltype(api.activeTexture)>(api_,"glActiveTextureARB");
        api.framebufferQuery=validProc(api_.getProcAddress("glBindFramebufferEXT")) || validProc(api_.getProcAddress("glBindFramebuffer"));
        api.blendEquationQuery=validProc(api_.getProcAddress("glBlendEquationEXT")) || validProc(api_.getProcAddress("glBlendEquation"));
        bloomReady_=true;
    }
    return api_.bloom;
}
void GlHooks::traceBloomDraw(const char* event,const void* caller) {
    if(settings_.traceBloom) bloom_.draw(bloomApi(),event,caller);
}
void GlHooks::traceBloomSwap(HDC dc,BOOL result) {
    if(settings_.traceBloom) bloom_.swap(GetTickCount64(),dc,result);
}
void bloomTraceDraw(const char* event,const void* caller) noexcept {
    if(bloomEnabled.load(std::memory_order_relaxed)) invoke([&](GlHooks& h) { h.traceBloomDraw(event,caller); });
}
void bloomTraceSwap(HDC dc,BOOL result) noexcept {
    if(bloomEnabled.load(std::memory_order_relaxed)) invoke([&](GlHooks& h) { h.traceBloomSwap(dc,result); });
}
void GlHooks::genOcclusionQueries(GLsizei n, GLuint* ids) { requireProc(api_.genQueries, api_, "glGenQueriesARB")(n, ids); }
void GlHooks::deleteOcclusionQueries(GLsizei n, const GLuint* ids) { requireProc(api_.deleteQueries, api_, "glDeleteQueriesARB")(n, ids); }
GLboolean GlHooks::isOcclusionQuery(GLuint id) { return requireProc(api_.isQuery, api_, "glIsQueryARB")(id); }
void GlHooks::beginOcclusionQuery(GLuint id) { requireProc(api_.beginQuery, api_, "glBeginQueryARB")(samplesPassed, id); }
void GlHooks::endOcclusionQuery() { requireProc(api_.endQuery, api_, "glEndQueryARB")(samplesPassed); }
void GlHooks::getOcclusionQueryiv(GLuint id, GLenum name, GLint* params) { requireProc(api_.getQueryObjectiv, api_, "glGetQueryObjectivARB")(id, name, params); }
void GlHooks::getOcclusionQueryuiv(GLuint id, GLenum name, GLuint* params) { requireProc(api_.getQueryObjectuiv, api_, "glGetQueryObjectuivARB")(id, name, params); }

void installGlHooks(HMODULE module, const Settings& settings, const std::filesystem::path& pluginDir) {
    auto& state = runtime();
    state.settings = settings;
    bloomEnabled.store(settings.traceBloom,std::memory_order_relaxed);
    state.dir = pluginDir;
    state.exports.getCurrentContext = reinterpret_cast<decltype(state.exports.getCurrentContext)>(GetProcAddress(module, "wglGetCurrentContext"));
    if (!state.exports.getCurrentContext) throw std::runtime_error("opengl32 wglGetCurrentContext unavailable");
    if(settings.traceBloom) {
        auto& api=state.exports.bloom;
        api.getInteger=reinterpret_cast<decltype(api.getInteger)>(GetProcAddress(module,"glGetIntegerv"));
        api.getFloat=reinterpret_cast<decltype(api.getFloat)>(GetProcAddress(module,"glGetFloatv"));
        api.isEnabled=reinterpret_cast<decltype(api.isEnabled)>(GetProcAddress(module,"glIsEnabled"));
        api.getTexLevel=reinterpret_cast<decltype(api.getTexLevel)>(GetProcAddress(module,"glGetTexLevelParameteriv"));
        api.context=state.exports.getCurrentContext;
        api.dc=reinterpret_cast<decltype(api.dc)>(GetProcAddress(module,"wglGetCurrentDC"));
        logLineNoThrow("TraceBloom enabled: first 3 frames then 1Hz, last 48 bind/draw/copy events/frame; 8192 records process-wide; 2D units 0..3, FP env0..7; diagnostic only");
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(module), &pinned)) throw std::runtime_error("cannot pin opengl32");
    struct ExportHook { const char* name; LPVOID hook; LPVOID* original; bool enabled; LPVOID target = nullptr; };
    const bool procFixes = settings.enable20PlusPlusAnyGpu || settings.eyeshineFlickerWorkaround || settings.dumpShaders || settings.traceVideo || settings.traceShadowPass || settings.traceBloom || settings.eyeshineFix || settings.eyeshineFixVariant || settings.eyeshineAbBurst;
    std::array<ExportHook, 5> hooks = {{
        {"glGetString", reinterpret_cast<LPVOID>(&getStringHook), reinterpret_cast<LPVOID*>(&state.exports.getString), true},
        {"wglDeleteContext", reinterpret_cast<LPVOID>(&deleteContextHook), reinterpret_cast<LPVOID*>(&state.exports.deleteContext), true},
        {"glGetIntegerv", reinterpret_cast<LPVOID>(&getIntegervHook), reinterpret_cast<LPVOID*>(&state.exports.getIntegerv), settings.fixMiscBugs},
        {"wglGetProcAddress", reinterpret_cast<LPVOID>(&getProcHook), reinterpret_cast<LPVOID*>(&state.exports.getProcAddress), procFixes},
        {"glColor4f", reinterpret_cast<LPVOID>(&colorHook), reinterpret_cast<LPVOID*>(&state.exports.color4f), settings.enable20PlusPlusAnyGpu}
    }};
    try {
        for (auto& entry : hooks) {
            if (!entry.enabled) continue;
            const auto target = reinterpret_cast<LPVOID>(GetProcAddress(module, entry.name));
            const auto status = MH_CreateHook(target, entry.hook, entry.original);
            if (status != MH_OK) throw std::runtime_error(std::string(entry.name) + ": " + MH_StatusToString(status));
            entry.target = target;
        }
    } catch (...) {
        for (const auto& entry : hooks) if (entry.target) MH_RemoveHook(entry.target);
        throw;
    }
    logf("GL hooks created: extension filter; texture-unit fix=%d; 2.0++ any GPU=%d; flicker workaround=%d",
         settings.fixMiscBugs, settings.enable20PlusPlusAnyGpu, settings.eyeshineFlickerWorkaround);
    if (settings.enable20PlusPlusAnyGpu) logLine("GL 2.0++: glColor4f preserved and mirrored to glVertexAttrib4f(3,...)");
    if (settings.eyeshineFlickerWorkaround) logLine("Eyeshine workaround enabled: removes main-light character shadows");
}
