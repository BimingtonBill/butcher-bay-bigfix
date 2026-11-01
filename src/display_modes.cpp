#include "display_modes.h"
#include "config.h"
#include "log.h"
#include <MinHook.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>

namespace {
auto key(Mode mode) { return std::make_tuple(mode.w, mode.h, mode.bpp, mode.hz); }
bool same(Mode a, Mode b) { return key(a) == key(b); }
template<class D> Mode tupleOf(const D& mode) {
    return {mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel, mode.dmDisplayFrequency};
}
}

std::vector<Mode> filterModes(const std::vector<Mode>& all, Mode desktop, size_t cap) {
    if (!cap) return {};
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> maximum;
    for (const auto mode : all) {
        if (mode.bpp != 32) continue;
        auto& hz = maximum[{mode.w, mode.h}];
        hz = (std::max)(hz, mode.hz);
    }
    std::map<decltype(key(desktop)), Mode> unique;
    for (const auto mode : all) {
        if (mode.bpp == 32 && (mode.hz == desktop.hz || mode.hz == 60 ||
                              mode.hz == maximum.at({mode.w, mode.h})))
            unique.emplace(key(mode), mode);
    }
    // The current desktop may have a rounded refresh not in the indexed list.
    if (desktop.bpp == 32) unique.emplace(key(desktop), desktop);
    std::vector<Mode> result;
    for (const auto& entry : unique) result.push_back(entry.second);
    std::sort(result.begin(), result.end(), [](Mode a, Mode b) {
        return std::make_tuple(uint64_t(a.w) * a.h, a.w, a.h, a.hz) <
               std::make_tuple(uint64_t(b.w) * b.h, b.w, b.h, b.hz);
    });
    // Keep the largest modes, reserving one slot for the desktop if necessary.
    if (result.size() > cap) {
        const auto first = result.end() - cap;
        const bool desktopWouldBeLost = std::any_of(result.begin(), first,
            [=](Mode mode) { return same(mode, desktop); });
        if (desktopWouldBeLost) {
            result.erase(result.begin(), first + 1);
            result.insert(result.begin(), desktop);
        } else result.erase(result.begin(), first);
    }
    return result;
}

namespace {
decltype(&EnumDisplaySettingsA) originalA = nullptr;
decltype(&EnumDisplaySettingsW) originalW = nullptr;
std::once_flag logged;

struct PrimaryName {
    std::wstring wide;
    std::string ansi;
    PrimaryName() {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
            if (!(device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) continue;
            wide = device.DeviceName;
            // Windows display adapter names (\\.\DISPLAYn) contain ASCII only.
            for (wchar_t c : wide) ansi.push_back(static_cast<char>(c));
            break;
        }
    }
};
const PrimaryName& primaryName() {
    static const PrimaryName name;
    return name;
}
bool primary(LPCSTR device) {
    return !device || (!primaryName().ansi.empty() && _stricmp(device, primaryName().ansi.c_str()) == 0);
}
bool primary(LPCWSTR device) {
    return !device || (!primaryName().wide.empty() && _wcsicmp(device, primaryName().wide.c_str()) == 0);
}

template<class D, class F> std::vector<D> buildCache(F api) {
    D desktop{};
    desktop.dmSize = sizeof(desktop);
    if (!api(nullptr, ENUM_CURRENT_SETTINGS, &desktop) || desktop.dmBitsPerPel != 32)
        throw std::runtime_error("32-bpp desktop mode unavailable");
    std::vector<Mode> all;
    std::map<decltype(key(tupleOf(desktop))), D> real;
    for (DWORD index = 0; ; ++index) {
        D mode{};
        mode.dmSize = sizeof(mode);
        if (!api(nullptr, index, &mode)) break;
        all.push_back(tupleOf(mode));
        real.emplace(key(tupleOf(mode)), mode);
    }
    if (all.empty()) throw std::runtime_error("display-mode enumeration unavailable");
    // Even an unlisted/rounded desktop comes from a real API DEVMODE.
    real.emplace(key(tupleOf(desktop)), desktop);
    const auto filtered = filterModes(all, tupleOf(desktop));
    std::vector<D> result;
    for (const auto mode : filtered) result.push_back(real.at(key(mode)));
    std::call_once(logged, [&] {
        logf("Display modes: %zu -> %zu (cap=512), desktop=%lux%lu %lubpp @%luHz",
             all.size(), result.size(), desktop.dmPelsWidth, desktop.dmPelsHeight,
             desktop.dmBitsPerPel, desktop.dmDisplayFrequency);
    });
    return result;
}

template<class D, class F, class Name> BOOL enumerate(F api, Name device, DWORD index, D* mode) noexcept {
    const DWORD error = GetLastError();
    try {
        if (index == ENUM_CURRENT_SETTINGS || index == ENUM_REGISTRY_SETTINGS ||
            !mode || mode->dmSize != sizeof(D) || !primary(device))
            return api(device, index, mode);
        // Separate A/W snapshots preserve each API's actual full DEVMODE layout.
        // C++ static initialization serializes concurrent first calls; afterwards
        // the cache is immutable and enumeration indices remain stable.
        static const auto cache = buildCache<D>(api);
        if (index >= cache.size()) { SetLastError(error); return FALSE; }
        *mode = cache[index];
        SetLastError(error);
        return TRUE;
    } catch (...) {
        logLineNoThrow("Display-mode cache failed; passing through to Windows");
        SetLastError(error);
        return api(device, index, mode);
    }
}
BOOL WINAPI enumA(LPCSTR device, DWORD index, DEVMODEA* mode) noexcept {
    return enumerate(originalA, device, index, mode);
}
BOOL WINAPI enumW(LPCWSTR device, DWORD index, DEVMODEW* mode) noexcept {
    return enumerate(originalW, device, index, mode);
}
void checkHook(MH_STATUS status, const char* name) {
    if (status != MH_OK) throw std::runtime_error(std::string(name) + ": " + MH_StatusToString(status));
}
}

void installDisplayModeHooks(const Settings& settings) {
    if (!settings.displayModeFilter) return;
    struct Hook { const char* name; LPVOID detour; LPVOID* original; LPVOID target = nullptr; };
    Hook hooks[] = {
        {"EnumDisplaySettingsA", reinterpret_cast<LPVOID>(&enumA), reinterpret_cast<LPVOID*>(&originalA)},
        {"EnumDisplaySettingsW", reinterpret_cast<LPVOID>(&enumW), reinterpret_cast<LPVOID*>(&originalW)}
    };
    size_t created = 0;
    try {
        // user32 is already imported; resolving it must never load a DLL.
        for (auto& hook : hooks) {
            checkHook(MH_CreateHookApiEx(L"user32.dll", hook.name, hook.detour, hook.original, &hook.target), hook.name);
            ++created;
        }
        for (auto& hook : hooks) checkHook(MH_QueueEnableHook(hook.target), hook.name);
        checkHook(MH_ApplyQueued(), "enable display-mode hooks");
    } catch (...) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(hooks[i].target);
        throw;
    }
}
