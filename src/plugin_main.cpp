#include <windows.h>
#include <winternl.h>
#include <MinHook.h>
#include "config.h"
#include "display_modes.h"
#include "fingerprint.h"
#include "log.h"
#include "module_watch.h"
#include "version.h"
#include "window_mode.h"
#include <stdexcept>
#include <vector>

namespace {
void logHeader(const Settings& settings, const std::vector<std::string>& warnings) {
    logf("BigFix %s", kBigFixVersion);
    try {
        std::vector<wchar_t> path(32768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) throw std::runtime_error("exe path unavailable");
        logf("exe=%s", std::filesystem::path(std::wstring(path.data(), length)).u8string().c_str());
    } catch (...) { logLineNoThrow("exe path unavailable"); }
    using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto getVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (getVersion && getVersion(&os) >= 0)
        logf("Windows %lu.%lu build %lu", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    else logLine("Windows version unavailable (RtlGetVersion)");
    const char* mode = settings.windowMode == WindowMode::Borderless ? "borderless" :
                       settings.windowMode == WindowMode::Fullscreen ? "fullscreen" : "windowed";
    logf("settings: TextureSizeFix=%d FixMiscBugs=%d Enable20PlusPlusAnyGpu=%d EyeshineFlickerWorkaround=%d",
         settings.textureSizeFix, settings.fixMiscBugs, settings.enable20PlusPlusAnyGpu, settings.eyeshineFlickerWorkaround);
    logf("settings: DisplayModeFilter=%d WindowMode=%s CaptureKey=F%u CapturePng=%d DumpShaders=%d DumpFrameTimes=%d",
         settings.displayModeFilter, mode, settings.captureVk - VK_F1 + 1, settings.capturePng, settings.dumpShaders, settings.dumpFrameTimes);
    logf("settings: VideoAspectFix=%d TraceVideo=%d SkipLogoVideos=%d",
         settings.videoAspectFix, settings.traceVideo, settings.skipLogoVideos);
    logf("settings: EyeshineFix=%d", settings.eyeshineFix);
    logf("settings: HudAspectFix=%d FOV=%.7g TraceFov=%d",settings.hudAspectFix,settings.fov,settings.traceFov);
    logf("settings: EyeshineFixVariant=%d", settings.eyeshineFixVariant);
    logf("settings: EyeshineAbBurst=%d CaptureBurst=%d", settings.eyeshineAbBurst,settings.captureBurst);
    for (const auto& warning : warnings) logf("settings warning: %s", warning.c_str());
}

void initializePlugin(HMODULE self) noexcept {
    try {
        const auto dir = moduleDir(self);
        logInitBestEffort(dir / L"BigFix.log");
        std::vector<std::string> warnings;
        const auto settings = loadSettings(dir, warnings);
        logHeader(settings, warnings);
        configureModuleWatch(settings, dir);
        const auto status = MH_Initialize();
        if (status != MH_OK) throw std::runtime_error(MH_StatusToString(status));
        installDisplayModeHooks(settings);
        installModuleWatch();
        const auto enabled = MH_EnableHook(MH_ALL_HOOKS);
        if (enabled != MH_OK) throw std::runtime_error(MH_StatusToString(enabled));
        logLine("Module watch ready");
    } catch (...) { logLineNoThrow("BigFix initialization failed; game continues without remaining fixes"); }
}
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) noexcept {
    try {
        if (reason == DLL_PROCESS_ATTACH) {
            DisableThreadLibraryCalls(self);
            // Hooks reference this image for the process lifetime.
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                    reinterpret_cast<LPCWSTR>(self), &pinned)) {
                logLineNoThrow("BigFix could not pin plugin; initialization skipped");
                return TRUE;
            }
            initializePlugin(self);
        }
    } catch (...) { logLineNoThrow("BigFix DllMain failed; game continues"); }
    return TRUE;
}
