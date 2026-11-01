#include "window_mode.h"
#include "fingerprint.h"
#include "log.h"
#include "patch_table.h"
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
class PhysicalPixels {
    DPI_AWARENESS_CONTEXT previous_;
public:
    PhysicalPixels() : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        if (!previous_) throw std::runtime_error("PMv2 thread awareness unavailable");
    }
    ~PhysicalPixels() { SetThreadDpiAwarenessContext(previous_); }
};
}

DWORD borderlessStyle(DWORD style) {
    return (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) | WS_POPUP;
}
bool shouldSuppressModeChange(WindowMode policy, const DEVMODEW* request, Mode desktop) {
    if (policy != WindowMode::Borderless || !request) return false;
    return ((request->dmFields & DM_PELSWIDTH) && request->dmPelsWidth != desktop.w) ||
           ((request->dmFields & DM_PELSHEIGHT) && request->dmPelsHeight != desktop.h) ||
           ((request->dmFields & DM_BITSPERPEL) && request->dmBitsPerPel != desktop.bpp) ||
           ((request->dmFields & DM_DISPLAYFREQUENCY) && request->dmDisplayFrequency != desktop.hz);
}
Rect borderlessRect(HMONITOR monitor, MonitorInfoFn query) {
    const PhysicalPixels physical;
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!query(monitor, &info)) throw std::runtime_error("physical monitor rectangle unavailable");
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
}
bool bypassGameWindowMessage(WindowMode policy, UINT message, WPARAM parameter) {
    return policy == WindowMode::Borderless &&
        (message == WM_KILLFOCUS || (message == WM_SYSCOMMAND && (parameter & 0xfff0) == SC_MINIMIZE));
}
Mode renderModeRequest(WindowMode policy, Mode request, Mode desktop) {
    return policy == WindowMode::Borderless ? desktop : request;
}

namespace {
// Only RndrGL's import slots are replaced. Calls from the host, overlay and
// other modules retain their Win32 semantics; no return-address heuristics.
constexpr char findName[] = "?ModeList_Find@CDisplayContext@@UEAAHHHHH@Z";
using FindMode = int (*)(void*, int, int, int, int);
FindMode originalFind = nullptr;
DEVMODEA desktopA{};
DEVMODEW desktopW{};
Mode desktop{};
uintptr_t rendererBase = 0;
size_t rendererSize = 0;
std::atomic<HWND> mainWindow{nullptr};
const auto creatingWindow = reinterpret_cast<HWND>(uintptr_t(1));
struct MainReservation {
    ~MainReservation() {
        HWND expected = creatingWindow;
        mainWindow.compare_exchange_strong(expected, nullptr);
    }
};
constexpr wchar_t windowProperty[] = L"BigFix.Borderless.WindowState";
struct WindowState { WNDPROC original; bool wide; HMONITOR monitor; };

LRESULT forward(const WindowState& state, HWND window, UINT message, WPARAM w, LPARAM l) {
    return state.wide ? CallWindowProcW(state.original, window, message, w, l)
                      : CallWindowProcA(state.original, window, message, w, l);
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
    auto* state = static_cast<WindowState*>(GetPropW(window, windowProperty));
    if (!state) return DefWindowProcW(window, message, w, l);
    if (bypassGameWindowMessage(WindowMode::Borderless, message, w)) {
        // Bypass the entire RE'd loss handler, not just ShowWindow: it also
        // sets suspended state and queues mode 0 at RndrGL+0x49816.
        if (message == WM_KILLFOCUS && GetCapture() == window) ReleaseCapture();
        return 0;
    }
    if (message == WM_NCDESTROY) {
        const auto original = *state;
        RemovePropW(window, windowProperty);
        HWND expected = window;
        mainWindow.compare_exchange_strong(expected, nullptr);
        delete state;
        return forward(original, window, message, w, l);
    }
    // WM_SETFOCUS reaches the game so it still clears its input state.
    return forward(*state, window, message, w, l);
}

template<class Name> bool mainCandidate(Name name, HINSTANCE instance, DWORD style, HWND parent, HMENU menu) {
    if (parent || menu || mainWindow.load() || !(style & WS_VISIBLE) ||
        (style != 0x90000000 && style != 0x16cf0000)) return false;
    // The wrapper also makes child/helper windows. Require a renderer-owned
    // CS_OWNDC class and a main-window style, and own at most one HWND at a time.
    WNDCLASSEXW wide{}; wide.cbSize = sizeof(wide);
    WNDCLASSEXA ansi{}; ansi.cbSize = sizeof(ansi);
    WNDPROC procedure = nullptr;
    UINT classStyle = 0;
    if constexpr (std::is_same_v<Name, LPCWSTR>) {
        if (!GetClassInfoExW(instance, name, &wide)) return false;
        procedure = wide.lpfnWndProc; classStyle = wide.style;
    } else {
        if (!GetClassInfoExA(instance, name, &ansi)) return false;
        procedure = ansi.lpfnWndProc; classStyle = ansi.style;
    }
    const auto address = reinterpret_cast<uintptr_t>(procedure);
    return (classStyle & CS_OWNDC) && address >= rendererBase && address - rendererBase < rendererSize;
}

template<class Api, class Name> HWND create(Api api, DWORD exstyle, Name name, Name title, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) noexcept {
    try {
        if (!mainCandidate(name, instance, style, parent, menu))
            return api(exstyle, name, title, style, x, y, width, height, parent, menu, instance, parameter);
        HWND expected = nullptr;
        if (!mainWindow.compare_exchange_strong(expected, creatingWindow))
            return api(exstyle, name, title, style, x, y, width, height, parent, menu, instance, parameter);
        const MainReservation reservation;
        const PhysicalPixels physical;
        const auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        const auto rect = borderlessRect(monitor);
        const DWORD popup = borderlessStyle(style);
        const DWORD extended = exstyle & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
        auto state = std::make_unique<WindowState>();
        state->wide = std::is_same_v<Name, LPCWSTR>;
        state->monitor = monitor;
        // Create hidden, then subclass before showing: focus messages emitted
        // during visible creation must not reach the fullscreen loss handler.
        HWND window = api(extended, name, title, popup & ~WS_VISIBLE, rect.l, rect.t,
                          rect.r - rect.l, rect.b - rect.t, parent, menu, instance, parameter);
        if (!window) {
            logLineNoThrow("Borderless CreateWindowEx failed");
            return nullptr;
        }
        state->original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (!SetPropW(window, windowProperty, state.get())) {
            DestroyWindow(window);
            logLineNoThrow("Borderless subclass property failed");
            return nullptr;
        }
        SetLastError(0);
        const auto previous = state->wide ? SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc))
                                          : SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc));
        if (!previous && GetLastError()) {
            RemovePropW(window, windowProperty);
            DestroyWindow(window);
            logLineNoThrow("Borderless WndProc subclass failed");
            return nullptr;
        }
        state->original = reinterpret_cast<WNDPROC>(previous);
        state.release(); // WM_NCDESTROY owns cleanup, including recreated windows.
        mainWindow.store(window);
        ShowWindow(window, SW_SHOWNORMAL);
        // Logging must not throw back into the fallback CreateWindowEx path
        // after ownership/subclassing has already succeeded.
        try {
            logf("Borderless window style: %08lx -> %08lx, exstyle %08lx -> %08lx, rect=(%ld,%ld)-(%ld,%ld)",
                 style, popup, exstyle, extended, rect.l, rect.t, rect.r, rect.b);
        } catch (...) { logLineNoThrow("Borderless window-style diagnostic failed"); }
        return window;
    } catch (...) {
        logLineNoThrow("Borderless creation failed; preserving original CreateWindowEx call");
        return api(exstyle, name, title, style, x, y, width, height, parent, menu, instance, parameter);
    }
}
HWND WINAPI createA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                    HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) noexcept {
    return create(&CreateWindowExA, ex, cls, title, style, x, y, w, h, parent, menu, instance, param);
}
HWND WINAPI createW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h,
                    HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) noexcept {
    return create(&CreateWindowExW, ex, cls, title, style, x, y, w, h, parent, menu, instance, param);
}
BOOL WINAPI position(HWND window, HWND after, int x, int y, int w, int h, UINT flags) noexcept {
    try {
        if (window == mainWindow.load()) {
            const auto* state = static_cast<WindowState*>(GetPropW(window, windowProperty));
            if (state) {
                const PhysicalPixels physical;
                const auto rect = borderlessRect(state->monitor);
                return SetWindowPos(window, HWND_NOTOPMOST, rect.l, rect.t, rect.r - rect.l, rect.b - rect.t,
                                    flags & ~(SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER));
            }
        }
    } catch (...) { logLineNoThrow("Borderless SetWindowPos failed; preserving original call"); }
    return SetWindowPos(window, after, x, y, w, h, flags);
}

template<class D> LONG change(const D* requested, DWORD flags) noexcept {
    const DWORD error = GetLastError();
    try {
        if (requested) {
            DEVMODEW normalized{};
            normalized.dmFields = requested->dmFields;
            normalized.dmPelsWidth = requested->dmPelsWidth; normalized.dmPelsHeight = requested->dmPelsHeight;
            normalized.dmBitsPerPel = requested->dmBitsPerPel; normalized.dmDisplayFrequency = requested->dmDisplayFrequency;
            if (shouldSuppressModeChange(WindowMode::Borderless, &normalized, desktop))
                logf("Borderless suppressed mode change: %lux%lu %lubpp @%luHz fields=%08lx flags=%08lx",
                     normalized.dmPelsWidth, normalized.dmPelsHeight, normalized.dmBitsPerPel,
                     normalized.dmDisplayFrequency, normalized.dmFields, flags);
            else logf("Borderless desktop mode retained: %lux%lu %lubpp @%luHz flags=%08lx",
                      desktop.w, desktop.h, desktop.bpp, desktop.hz, flags);
        } else logf("Borderless desktop restore retained (NULL DEVMODE), flags=%08lx", flags);
    } catch (...) { logLineNoThrow("Borderless mode-change diagnostic failed; desktop still retained"); }
    // Even desktop/NULL restores are no-ops: NULL can restore a registry mode
    // different from the actual desktop captured at startup. Never touch the
    // display or registry (including teardown RndrGL+0x45B87).
    SetLastError(error);
    return DISP_CHANGE_SUCCESSFUL;
}
LONG WINAPI changeA(DEVMODEA* m, DWORD f) noexcept { return change(m, f); }
LONG WINAPI changeW(DEVMODEW* m, DWORD f) noexcept { return change(m, f); }
LONG WINAPI changeExA(LPCSTR, DEVMODEA* m, HWND, DWORD f, LPVOID) noexcept { return change(m, f); }
LONG WINAPI changeExW(LPCWSTR, DEVMODEW* m, HWND, DWORD f, LPVOID) noexcept { return change(m, f); }

void logNativeDisplayMode() {
    static std::once_flag logged;
    std::call_once(logged, [] {
        // This path supplies one real desktop descriptor directly, without an
        // indexed OS list or the global filter cache.
        logf("Display modes: 1 -> 1 (cap=512), desktop=%ux%u %ubpp @%uHz (borderless native-only)",
             desktop.w, desktop.h, desktop.bpp, desktop.hz);
    });
}

template<class D, class Name, class Api> BOOL enumerate(Api api, Name device, DWORD index, D* result, const D& native) noexcept {
    if (device || index == ENUM_CURRENT_SETTINGS || index == ENUM_REGISTRY_SETTINGS ||
        !result || result->dmSize < offsetof(D, dmICMMethod) || result->dmSize > sizeof(D))
        return api(device, index, result);
    // The sole fullscreen descriptor is physical native mode, before the
    // renderer's 512-call cutoff, independently of DisplayModeFilter setting.
    if (index) return FALSE;
    try {
        // Log through a non-template helper so ANSI and Unicode share one line.
        logNativeDisplayMode();
    } catch (...) { logLineNoThrow("Borderless display-mode diagnostic failed"); }
    // Windows can return a legacy dmSize (124 for A on the test rig). The
    // renderer reuses that DEVMODE on its next indexed call. Accept the full
    // display-field prefix and never write beyond the supplied structure size.
    std::memcpy(result, &native, (std::min)(size_t(result->dmSize), sizeof(D)));
    return TRUE;
}
BOOL WINAPI enumA(LPCSTR device, DWORD index, DEVMODEA* result) noexcept {
    return enumerate(&EnumDisplaySettingsA, device, index, result, desktopA);
}
BOOL WINAPI enumW(LPCWSTR device, DWORD index, DEVMODEW* result) noexcept {
    return enumerate(&EnumDisplaySettingsW, device, index, result, desktopW);
}
int findMode(void* context, int w, int h, int format, int hz) noexcept {
    // 32-bpp CImage format 0x40 verified at MSystem+0x5C891. We refuse to
    // install these hooks unless the actual desktop is 32 bpp.
    const auto request = renderModeRequest(WindowMode::Borderless,
        {uint32_t(w), uint32_t(h), uint32_t(format), uint32_t(hz)}, desktop);
    try { logf("Borderless render selection: %dx%d format=%x @%d -> %ux%u 32bpp @%u (fullscreen)",
               w, h, format, hz, request.w, request.h, request.hz); }
    catch (...) { logLineNoThrow("Borderless render-selection diagnostic failed"); }
    return originalFind(context, int(request.w), int(request.h), 0x40, int(request.hz));
}
BOOL WINAPI dpiAware() noexcept { return TRUE; } // configureWindowMode already selected PMv2; never downgrade it.

struct ImportHook { const char* name; void* detour; };
struct Slot { void** address; void* original; void* detour; };
void replaceSlot(const Slot& slot, bool restore = false) {
    DWORD protection = 0;
    if (!VirtualProtect(slot.address, sizeof(void*), PAGE_READWRITE, &protection))
        throw std::runtime_error("IAT protection failed");
    auto* expected = restore ? slot.detour : slot.original;
    auto* replacement = restore ? slot.original : slot.detour;
    const auto previous = InterlockedCompareExchangePointer(slot.address, replacement, expected);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(slot.address, sizeof(void*), protection, &ignored);
    if (previous != expected || !restored) throw std::runtime_error("IAT pointer/protection verification failed");
}

void rollbackSelectionPolicy(HMODULE system, const PatchSite& site) noexcept {
    try {
        auto reverse = site;
        std::swap(reverse.expected, reverse.replacement);
        const auto result = applyPatchLive(system, reverse);
        if (result != PatchResult::Applied && result != PatchResult::AlreadyApplied)
            logLineNoThrow("Mode-selection rollback failed");
    } catch (...) { logLineNoThrow("Mode-selection rollback failed"); }
}

PatchResult applySelectionPolicy(WindowMode policy, std::string_view rendererHash, std::string_view systemHash) {
    // Numeric selection and VID_MODE=desktop must both work before any IAT
    // change. Exact fingerprints and applyPatchLive's complete byte check gate
    // the policy. A failed prerequisite leaves all renderer imports untouched.
    const char* name = policy == WindowMode::Borderless ? "Borderless" : "Windowed";
    const auto unavailable = [=](const char* reason) noexcept {
        try { logf("%s unavailable: %s; using game's own mode", name, reason); }
        catch (...) { }
    };
    const auto system = GetModuleHandleW(L"MSystem.dll");
    if (!system || rendererHash != "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348" ||
        systemHash != "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500") {
        unavailable("EU 1.1 renderer/system fingerprints required");
        return PatchResult::Mismatch;
    }
    const auto& site = policy == WindowMode::Borderless ? borderlessModeSelectionSite() : windowedModeSelectionSite();
    PatchResult result;
    try { result = applyPatchLive(system, site); }
    catch (...) {
        // Protection/cache failures can throw after writing the bytes.
        rollbackSelectionPolicy(system, site);
        unavailable("selection patch write/protection/cache failure");
        return PatchResult::BadAddress;
    }
    if (result == PatchResult::Mismatch) unavailable("MSystem selection byte mismatch");
    else if (result == PatchResult::BadAddress) unavailable("MSystem selection patch address unavailable");
    else {
        try { logf("%s MSystem+0x77885 %s: %s", name, site.name,
                   result == PatchResult::Applied ? "Applied" : "AlreadyApplied"); }
        catch (...) { logLineNoThrow("Mode-selection diagnostic failed"); }
    }
    return result;
}
}

void configureWindowMode(const Settings& settings) {
    if (settings.windowMode != WindowMode::Borderless) return;
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        logLine("Borderless process DPI awareness: per-monitor v2");
    else logf("Borderless process DPI awareness already fixed/unavailable (error=%lu); window calls use PMv2 thread context", GetLastError());
}

static void installWindowModeHooksImpl(HMODULE renderer, const Settings& settings,
                                       std::string_view rendererHash, std::string_view systemHash) {
    if (settings.windowMode == WindowMode::Fullscreen) return;
    if (settings.windowMode == WindowMode::Windowed) {
        applySelectionPolicy(settings.windowMode, rendererHash, systemHash);
        return;
    }
    desktopA.dmSize = sizeof(desktopA); desktopW.dmSize = sizeof(desktopW);
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &desktopA) ||
        !EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktopW) || desktopW.dmBitsPerPel != 32)
        throw std::runtime_error("borderless requires a real 32-bpp desktop mode");
    desktop = {desktopW.dmPelsWidth, desktopW.dmPelsHeight, desktopW.dmBitsPerPel, desktopW.dmDisplayFrequency};
    MODULEINFO info{};
    if (!K32GetModuleInformation(GetCurrentProcess(), renderer, &info, sizeof(info)))
        throw std::runtime_error("renderer image unavailable");
    rendererBase = reinterpret_cast<uintptr_t>(renderer); rendererSize = info.SizeOfImage;
    const ImportHook hooks[] = {
        {"CreateWindowExA", reinterpret_cast<void*>(&createA)}, {"CreateWindowExW", reinterpret_cast<void*>(&createW)},
        {"SetWindowPos", reinterpret_cast<void*>(&position)},
        {"ChangeDisplaySettingsA", reinterpret_cast<void*>(&changeA)}, {"ChangeDisplaySettingsW", reinterpret_cast<void*>(&changeW)},
        {"ChangeDisplaySettingsExA", reinterpret_cast<void*>(&changeExA)}, {"ChangeDisplaySettingsExW", reinterpret_cast<void*>(&changeExW)},
        {"EnumDisplaySettingsA", reinterpret_cast<void*>(&enumA)}, {"EnumDisplaySettingsW", reinterpret_cast<void*>(&enumW)},
        {"SetProcessDPIAware", reinterpret_cast<void*>(&dpiAware)},
        {findName, reinterpret_cast<void*>(&findMode)}
    };
    // Walk PE imports by name, not fixed RVAs. For each slot independently
    // verify the loader-resolved address against the already-loaded export.
    auto* base = reinterpret_cast<unsigned char*>(renderer);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || size_t(dos->e_lfanew) > rendererSize - sizeof(IMAGE_NT_HEADERS64))
        throw std::runtime_error("invalid renderer PE header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        throw std::runtime_error("invalid renderer PE64 header");
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.VirtualAddress > rendererSize || directory.Size > rendererSize - directory.VirtualAddress)
        throw std::runtime_error("invalid renderer import directory");
    std::vector<Slot> slots;
    bool hasCreate = false, hasEnum = false;
    for (auto* entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry) {
        if (!entry->OriginalFirstThunk) continue;
        const auto dependency = GetModuleHandleA(reinterpret_cast<const char*>(base + entry->Name));
        if (!dependency) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + entry->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            for (const auto& hook : hooks) {
                if (std::strcmp(name, hook.name)) continue;
                auto* expected = reinterpret_cast<void*>(GetProcAddress(dependency, name));
                auto** slot = reinterpret_cast<void**>(&addresses->u1.Function);
                if (!expected || *slot != expected) throw std::runtime_error(std::string("IAT mismatch: ") + name);
                slots.push_back({slot, expected, hook.detour});
                if (hook.detour == reinterpret_cast<void*>(&findMode)) originalFind = reinterpret_cast<FindMode>(expected);
                if (hook.detour == reinterpret_cast<void*>(&createA) || hook.detour == reinterpret_cast<void*>(&createW)) hasCreate = true;
                if (hook.detour == reinterpret_cast<void*>(&enumA) || hook.detour == reinterpret_cast<void*>(&enumW)) hasEnum = true;
            }
        }
    }
    if (!hasCreate || !hasEnum || !originalFind) throw std::runtime_error("renderer lacks required borderless imports; no IAT writes");
    const auto selection = applySelectionPolicy(settings.windowMode, rendererHash, systemHash);
    if (selection != PatchResult::Applied && selection != PatchResult::AlreadyApplied) return;
    size_t applied = 0;
    try {
        configureWindowMode(settings);
        for (const auto& slot : slots) {
            // Count before writing so a protection-restore error also rolls back.
            ++applied;
            replaceSlot(slot);
        }
    } catch (...) {
        while (applied) {
            try { replaceSlot(slots[--applied], true); }
            catch (...) { logLineNoThrow("Borderless IAT rollback failed"); }
        }
        if (selection == PatchResult::Applied)
            rollbackSelectionPolicy(GetModuleHandleW(L"MSystem.dll"), borderlessModeSelectionSite());
        throw;
    }
    logf("Borderless renderer IAT hooks ready: %zu imports; desktop=%ux%u %ubpp @%uHz", slots.size(), desktop.w, desktop.h, desktop.bpp, desktop.hz);
}

void installWindowModeHooks(HMODULE renderer, const Settings& settings,
                            std::string_view rendererHash, std::string_view systemHash) {
    try { installWindowModeHooksImpl(renderer, settings, rendererHash, systemHash); }
    catch (const std::exception& e) {
        try { logf("%s unavailable: %s; using game's own mode",
                   settings.windowMode == WindowMode::Borderless ? "Borderless" : "Windowed", e.what()); }
        catch (...) { }
    } catch (...) { logLineNoThrow("Borderless unavailable: hook setup failed; using game's own mode"); }
}
