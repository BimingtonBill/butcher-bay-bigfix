#include "module_watch.h"
#include "background_audio.h"
#include "capture.h"
#include "fingerprint.h"
#include "gl_hooks.h"
#include "msaa.h"
#include "log.h"
#include "patch_table.h"
#include "window_mode.h"
#include "video_trace.h"
#include "eyeshine_variants.h"
#include "shadow_trace.h"
#include "raw_mouse.h"
#include "widescreen.h"
#include <MinHook.h>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
struct Handler {
    std::function<void(HMODULE)> callback;
    bool handled = false;
    bool afterLoaderReturn = false;
};
struct Registry {
    std::mutex mutex;
    std::map<std::wstring, Handler> handlers;
};
Registry& registry() {
    static Registry value;
    return value;
}
std::wstring folded(std::wstring_view name) {
    std::wstring result(name);
    for (auto& c : result) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return result;
}

decltype(&LoadLibraryA) originalA = nullptr;
decltype(&LoadLibraryW) originalW = nullptr;
decltype(&LoadLibraryExA) originalExA = nullptr;
decltype(&LoadLibraryExW) originalExW = nullptr;

void scanLoadedModules(bool startup = false) {
    // Only registered, unhandled names need checking. This also catches import
    // dependencies, which can be mapped without a separate LoadLibrary call.
    std::vector<std::wstring> names;
    auto& state = registry();
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        for (const auto& entry : state.handlers)
            if (!entry.second.handled && !(startup && entry.second.afterLoaderReturn)) names.push_back(entry.first);
    }
    for (const auto& name : names) {
        HMODULE held = nullptr;
        // Unlike GetModuleHandle, this holds a reference throughout dispatch.
        if (GetModuleHandleExW(0, name.c_str(), &held)) {
            onModuleLoaded(name, held);
            FreeLibrary(held);
        }
    }
}

void notifyLoaded(HMODULE module, DWORD flags) {
    // Data/resource mappings have tagged handles and must never be patched.
    if (!module || (flags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                            LOAD_LIBRARY_AS_IMAGE_RESOURCE | DONT_RESOLVE_DLL_REFERENCES))) return;
    processDeferredModuleWork();
}

void enableLoadedHooks() {
    // Newly mapped modules are handled only after the original loader returned.
    // Activate their hooks before returning the module to the game's caller.
    const auto status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) logf("Enable newly loaded module hooks failed: %s", MH_StatusToString(status));
}

HMODULE WINAPI loadA(LPCSTR name) noexcept {
    HMODULE result = nullptr;
    DWORD error = GetLastError();
    try {
        result = originalA(name);
        error = GetLastError();
        notifyLoaded(result, 0);
    } catch (...) { logLineNoThrow("LoadLibraryA hook failed; preserving loader result"); }
    SetLastError(error);
    return result;
}
HMODULE WINAPI loadW(LPCWSTR name) noexcept {
    HMODULE result = nullptr;
    DWORD error = GetLastError();
    try {
        result = originalW(name);
        error = GetLastError();
        notifyLoaded(result, 0);
    } catch (...) { logLineNoThrow("LoadLibraryW hook failed; preserving loader result"); }
    SetLastError(error);
    return result;
}
HMODULE WINAPI loadExA(LPCSTR name, HANDLE file, DWORD flags) noexcept {
    HMODULE result = nullptr;
    DWORD error = GetLastError();
    try {
        result = originalExA(name, file, flags);
        error = GetLastError();
        notifyLoaded(result, flags);
    } catch (...) { logLineNoThrow("LoadLibraryExA hook failed; preserving loader result"); }
    SetLastError(error);
    return result;
}
HMODULE WINAPI loadExW(LPCWSTR name, HANDLE file, DWORD flags) noexcept {
    HMODULE result = nullptr;
    DWORD error = GetLastError();
    try {
        result = originalExW(name, file, flags);
        error = GetLastError();
        notifyLoaded(result, flags);
    } catch (...) { logLineNoThrow("LoadLibraryExW hook failed; preserving loader result"); }
    SetLastError(error);
    return result;
}

const char* patchResultText(PatchResult result) {
    switch (result) {
    case PatchResult::Applied: return "Applied";
    case PatchResult::AlreadyApplied: return "AlreadyApplied";
    case PatchResult::Mismatch: return "Mismatch";
    case PatchResult::BadAddress: return "BadAddress";
    }
    return "Unknown";
}
void rendererLoaded(HMODULE module, const Settings& settings, const std::string& systemHash) {
    logExecutableFingerprintOnce();
    Build build = Build::Unknown;
    std::string hash;
    try {
        hash = sha256Module(module);
        build = identify(hash);
        logf("RndrGL.dll sha256=%s build=%s", hash.c_str(), build == Build::EU11_x64 ? "EU11_x64" : "Unknown");
    } catch (...) { logLineNoThrow("RndrGL.dll fingerprint failed; using byte verification for Unknown build"); }
    for (const auto& site : sitesFor(build)) {
        if (folded(site.module) != L"rndrgl.dll") continue;
        if (!settings.textureSizeFix) {
            logf("RndrGL.dll %s: Skipped (TextureSizeFix=0)", site.name);
            continue;
        }
        try { logf("RndrGL.dll %s: %s", site.name, patchResultText(applyPatchLive(module, site))); }
        catch (...) {
            // A cache/protection failure may happen after the bytes were written.
            logf("RndrGL.dll %s: Error (patch write/protection/cache failure)", site.name);
        }
    }
    installWindowModeHooks(module, settings, hash, systemHash);
    installMsaaHooks(module, settings);
}

void checkHook(MH_STATUS status, const char* operation) {
    if (status != MH_OK) throw std::runtime_error(std::string(operation) + ": " + MH_StatusToString(status));
}
}

void setModuleHandler(std::wstring name, std::function<void(HMODULE)> handler, bool afterLoaderReturn) {
    auto& state = registry();
    const auto key = folded(name);
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (handler) state.handlers.insert_or_assign(key, Handler{std::move(handler), false, afterLoaderReturn});
    else state.handlers.erase(key);
}

void onModuleLoaded(std::wstring_view baseName, HMODULE module) noexcept {
    try {
        if (!module) return;
        std::function<void(HMODULE)> callback;
        auto& state = registry();
        {
            const std::lock_guard<std::mutex> lock(state.mutex);
            const auto found = state.handlers.find(folded(baseName));
            if (found == state.handlers.end() || found->second.handled) return;
            callback = found->second.callback;
            found->second.handled = true; // Before invoking: recursive/concurrent loads must not repeat it.
        }
        callback(module); // Never hold the registry mutex across arbitrary handlers.
    } catch (...) {
        try { logf("Module %.*ls handler failed; continuing original load", static_cast<int>(baseName.size()), baseName.data()); }
        catch (...) { /* Log may not be available. */ }
    }
}

void logExecutableFingerprintOnce() {
    static std::once_flag exeLogged;
    std::call_once(exeLogged, [] {
        try { logf("exe sha256=%s", sha256Module(GetModuleHandleW(nullptr)).c_str()); }
        catch (...) { logLineNoThrow("exe sha256 unavailable"); }
    });
}

void processDeferredModuleWork() {
    // CNG/driver initialization can itself load DLLs. Never re-enter a once flag
    // or dispatch another patch while its original handler is still running.
    static thread_local bool processing = false;
    if (processing) return;
    processing = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{processing};
    scanLoadedModules();
    enableLoadedHooks();
}

void configureModuleWatch(const Settings& settings, const std::filesystem::path& pluginDir) {
    struct SystemFingerprint { std::mutex mutex; std::string hash; };
    const auto system = std::make_shared<SystemFingerprint>();
    setModuleHandler(L"msystem.dll", [system, settings](HMODULE module) {
        logExecutableFingerprintOnce();
        const std::lock_guard<std::mutex> lock(system->mutex);
        try {
            system->hash = sha256Module(module);
            logf("MSystem.dll sha256=%s", system->hash.c_str());
        } catch (...) { logLineNoThrow("MSystem.dll fingerprint failed; mode policy unavailable"); }
        installVideoTraceHooks(module, VideoModule::System, settings);
        installRawMouse(settings);
        installWidescreenHooks(module,WidescreenModule::System,settings);
    }, true);
    setModuleHandler(L"GameClasses_Win64_AMD64.dll", [settings](HMODULE module) {
        installWidescreenHooks(module,WidescreenModule::Classes,settings);
        if(settings.testForceEyeshine) {
            try { logf("GameClasses TestForceEyeshine: %s", patchResultText(applyPatchLive(module, forceEyeshineSite()))); }
            catch (...) { logLineNoThrow("GameClasses TestForceEyeshine: Error (patch write/protection/cache failure)"); }
        }
    }, true);
    setModuleHandler(L"GameWorld_Win64_AMD64.dll", [settings](HMODULE module) {
        installWidescreenHooks(module,WidescreenModule::World,settings);
        try {installEyeshineScaleHook(module,settings);}
        catch(...) {logLineNoThrow("Eyeshine full-resolution probe unavailable: hook/fingerprint failure");}
        if (settings.skipLogoVideos) {
            try {
                const auto result = applyPatchLive(module, skipLogoVideosSite());
                logf("GameWorld SkipLogoVideos: %s", patchResultText(result));
                if (result == PatchResult::Applied) {
                    // The whole startup chain is bypassed, including its static
                    // screens. Log its five videos once; no global file/decoder hook.
                    for (const auto* name : {"vug_new.ogg", "universal_ntsc.ogg", "Tigon.ogg", "logo_mixed.ogg", "Sbz.ogg"})
                        logf("Skipped startup video: %s (startup sequence bypass)", name);
                }
            } catch (...) { logLineNoThrow("GameWorld SkipLogoVideos: Error (patch write/protection/cache failure)"); }
        } else logLine("GameWorld SkipLogoVideos: Skipped (SkipLogoVideos=0)");
        installVideoTraceHooks(module, VideoModule::World, settings);
    }, true);
    setModuleHandler(L"rndrgl.dll", [settings, system](HMODULE module) {
        // Import dependencies are resident before the renderer's LoadLibrary
        // returns. Dispatch MSystem first even for an explicit renderer notice.
        onModuleLoaded(L"msystem.dll", GetModuleHandleW(L"MSystem.dll"));
        std::string hash;
        { const std::lock_guard<std::mutex> lock(system->mutex); hash = system->hash; }
        rendererLoaded(module, settings, hash);
        installWidescreenHooks(module,WidescreenModule::Renderer,settings);
    }, true);
    setModuleHandler(L"opengl32.dll", [settings, pluginDir](HMODULE module) {
        installGlHooks(module, settings, pluginDir);
        installCaptureHooks(module, settings, pluginDir);
        installShadowTraceHooks(module, settings);
        installVideoTraceHooks(module, VideoModule::OpenGl, settings);
    });
    setModuleHandler(L"gdi32.dll", [settings, pluginDir](HMODULE module) { installCaptureHooks(module, settings, pluginDir); });
    setModuleHandler(L"dsound.dll", [settings](HMODULE module) { installBackgroundAudio(module, settings); });
}

void installModuleWatch() {
    // Resolve only already-loaded kernel32; MH_CreateHookApi does not LoadLibrary.
    // Track our own targets so a partial installation cannot disturb other hooks.
    struct Hook { const char* name; LPVOID detour; LPVOID* original; LPVOID target = nullptr; };
    Hook hooks[] = {
        {"LoadLibraryA", reinterpret_cast<LPVOID>(&loadA), reinterpret_cast<LPVOID*>(&originalA)},
        {"LoadLibraryW", reinterpret_cast<LPVOID>(&loadW), reinterpret_cast<LPVOID*>(&originalW)},
        {"LoadLibraryExA", reinterpret_cast<LPVOID>(&loadExA), reinterpret_cast<LPVOID*>(&originalExA)},
        {"LoadLibraryExW", reinterpret_cast<LPVOID>(&loadExW), reinterpret_cast<LPVOID*>(&originalExW)}
    };
    size_t created = 0;
    try {
        for (auto& hook : hooks) {
            checkHook(MH_CreateHookApiEx(L"kernel32.dll", hook.name, hook.detour, hook.original, &hook.target), hook.name);
            ++created;
        }
    } catch (...) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(hooks[i].target);
        throw;
    }
    // Only lightweight hook installers run under DllMain. Fingerprint-dependent
    // handlers stay pending until an original loader return or SwapBuffers.
    scanLoadedModules(true);
}
