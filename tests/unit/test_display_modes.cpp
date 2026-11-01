#include <doctest/doctest.h>
#include "display_modes.h"
#include "config.h"
#include "log.h"
#include <MinHook.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <tuple>

namespace {
auto key(Mode mode) { return std::make_tuple(mode.w, mode.h, mode.bpp, mode.hz); }
bool contains(const std::vector<Mode>& modes, Mode mode) {
    return std::any_of(modes.begin(), modes.end(), [=](Mode value) { return key(value) == key(mode); });
}
std::vector<Mode> rigModes() {
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() / "data/modes_rig_2026-10-29.txt");
    REQUIRE(file.is_open());
    std::vector<Mode> modes;
    Mode mode{};
    while (file >> mode.w >> mode.h >> mode.bpp >> mode.hz) modes.push_back(mode);
    REQUIRE(file.eof());
    return modes;
}
template<class D> Mode tupleOf(const D& mode) {
    return {mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel, mode.dmDisplayFrequency};
}
template<class D, class F> std::vector<D> enumerate(F api) {
    std::vector<D> modes;
    for (DWORD i = 0; ; ++i) {
        D mode{};
        mode.dmSize = sizeof(mode);
        if (!api(nullptr, i, &mode)) break;
        modes.push_back(mode);
    }
    return modes;
}
template<class D, class F, class Name> void checkCachedModes(
    F api, Name primary, const std::vector<D>& real, const std::vector<Mode>& expected) {
    for (DWORD i = 0; i < expected.size(); ++i) {
        D result{};
        result.dmSize = sizeof(result);
        REQUIRE(api(nullptr, i, &result));
        CHECK(key(tupleOf(result)) == key(expected[i]));
        const auto source = std::find_if(real.begin(), real.end(), [&](const D& value) {
            return key(tupleOf(value)) == key(expected[i]);
        });
        REQUIRE(source != real.end());
        // A real DEVMODE, including fields/flags/orientation, must survive intact.
        CHECK(std::memcmp(&result, &*source, sizeof(result)) == 0);
        D named{};
        named.dmSize = sizeof(named);
        REQUIRE(api(primary, i, &named));
        CHECK(std::memcmp(&named, &result, sizeof(result)) == 0);
    }
    D end{};
    end.dmSize = sizeof(end);
    CHECK_FALSE(api(nullptr, static_cast<DWORD>(expected.size()), &end));
    CHECK_FALSE(api(primary, 512, &end));
}
}

TEST_CASE("rig_list_shrinks_below_cap") {
    const auto all = rigModes();
    REQUIRE(all.size() == 803);
    const auto result = filterModes(all, {2560, 1440, 32, 180}, 512);
    REQUIRE_FALSE(result.empty());
    CHECK(result.size() <= 512);
    std::set<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> unique;
    for (auto mode : result) {
        CHECK(mode.bpp == 32);
        unique.insert(key(mode));
    }
    CHECK(unique.size() == result.size());
    std::cout << "Rig mode filter: " << all.size() << " -> " << result.size() << '\n';
}

TEST_CASE("native_mode_always_present") {
    for (uint32_t hz : {179u, 180u}) {
        const Mode desktop{2560, 1440, 32, hz};
        CHECK(contains(filterModes(rigModes(), desktop, 512), desktop));
    }
}

TEST_CASE("small_list_unchanged") {
    const Mode desktop{1920, 1080, 32, 60};
    const auto result = filterModes({desktop, desktop}, desktop, 512);
    REQUIRE(result.size() == 1);
    CHECK(key(result.front()) == key(desktop));
}

TEST_CASE("desktop_kept_when_cap_tiny") {
    const Mode desktop{1280, 720, 32, 60};
    const std::vector<Mode> all{{640, 480, 32, 60}, desktop,
        {1920, 1080, 32, 60}, {2560, 1440, 32, 60}, {3840, 2160, 32, 60}};
    const auto result = filterModes(all, desktop, 3);
    REQUIRE(result.size() == 3);
    CHECK(contains(result, desktop));
    CHECK(contains(result, {2560, 1440, 32, 60}));
    CHECK(contains(result, {3840, 2160, 32, 60}));
    CHECK(filterModes(all, desktop, 0).empty());
    const auto one = filterModes(all, desktop, 1);
    REQUIRE(one.size() == 1);
    CHECK(key(one.front()) == key(desktop));
}

TEST_CASE("ordering_is_ascending_area") {
    const auto modes = filterModes(rigModes(), {2560, 1440, 32, 179}, 512);
    CHECK(std::is_sorted(modes.begin(), modes.end(), [](Mode a, Mode b) {
        return uint64_t(a.w) * a.h < uint64_t(b.w) * b.h;
    }));
    // Area multiplication must not wrap at 32 bits.
    const auto large = filterModes({{70000, 70000, 32, 60}, {65536, 65536, 32, 60}},
                                   {1920, 1080, 32, 60}, 512);
    REQUIRE(large.size() == 3);
    CHECK(large.back().w == 70000);
}

TEST_CASE("uhd_120hz_display_with_dsr_modes") {
    // A 4K 120 Hz TV/monitor with driver super-resolution modes: many resolutions,
    // 11 refresh rates each, every entry listed three times (as on the dev rig).
    const std::vector<std::pair<uint32_t, uint32_t>> sizes{
        {640, 480}, {720, 480}, {720, 576}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720},
        {1280, 768}, {1280, 800}, {1280, 960}, {1280, 1024}, {1360, 768}, {1366, 768},
        {1440, 900}, {1600, 900}, {1600, 1024}, {1600, 1200}, {1680, 1050}, {1920, 1080},
        {1920, 1200}, {1920, 1440}, {2048, 1536}, {2560, 1080}, {2560, 1440}, {2560, 1600},
        {3200, 1800}, {3840, 2160}, {5120, 2880}, {7680, 4320}};
    std::vector<Mode> all;
    for (auto [w, h] : sizes)
        for (uint32_t hz : {23u, 24u, 25u, 29u, 30u, 50u, 59u, 60u, 100u, 119u, 120u})
            for (int copy = 0; copy < 3; ++copy) all.push_back({w, h, 32, hz});
    REQUIRE(all.size() > 512);
    const Mode desktop{3840, 2160, 32, 120};
    // Unfiltered, the game's first 512 entries would end before 3840x2160.
    CHECK_FALSE(contains(std::vector<Mode>(all.begin(), all.begin() + 512), desktop));
    const auto result = filterModes(all, desktop, 512);
    CHECK(result.size() <= 512);
    CHECK(contains(result, desktop));
    CHECK(contains(result, {2560, 1440, 32, 120}));  // lower resolution at the desktop refresh
    CHECK(contains(result, {1920, 1080, 32, 60}));   // and at 60 Hz
    CHECK(contains(result, {7680, 4320, 32, 120}));  // super-resolution modes stay selectable
}

TEST_CASE("keeps_only_desktop_60_and_maximum_refresh_at_32bpp") {
    const auto result = filterModes({{1280, 720, 16, 240}, {1280, 720, 32, 75},
        {1280, 720, 32, 60}, {1280, 720, 32, 144}, {1280, 720, 32, 180},
        {1280, 720, 32, 180}, {800, 600, 32, 75}}, {1920, 1080, 32, 144}, 512);
    REQUIRE(result.size() == 5);
    CHECK(contains(result, {800, 600, 32, 75}));
    CHECK(contains(result, {1280, 720, 32, 60}));
    CHECK(contains(result, {1280, 720, 32, 144}));
    CHECK(contains(result, {1280, 720, 32, 180}));
    CHECK(contains(result, {1920, 1080, 32, 144}));
}

TEST_CASE("display_hooks_cache_real_ansi_and_unicode_modes_and_preserve_passthrough") {
    const auto wide = enumerate<DEVMODEW>(&EnumDisplaySettingsW);
    const auto ansi = enumerate<DEVMODEA>(&EnumDisplaySettingsA);
    REQUIRE_FALSE(wide.empty());
    REQUIRE_FALSE(ansi.empty());
    DEVMODEW currentW{}, registryW{};
    DEVMODEA currentA{}, registryA{};
    currentW.dmSize = registryW.dmSize = sizeof(DEVMODEW);
    currentA.dmSize = registryA.dmSize = sizeof(DEVMODEA);
    REQUIRE(EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &currentW));
    const auto registryResultW = EnumDisplaySettingsW(nullptr, ENUM_REGISTRY_SETTINGS, &registryW);
    REQUIRE(EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &currentA));
    const auto registryResultA = EnumDisplaySettingsA(nullptr, ENUM_REGISTRY_SETTINGS, &registryA);

    DISPLAY_DEVICEW deviceW{};
    deviceW.cb = sizeof(deviceW);
    bool found = false;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &deviceW, 0); ++i) {
        if (deviceW.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { found = true; break; }
    }
    REQUIRE(found);
    std::wstring primaryW(deviceW.DeviceName);
    std::string primaryA;
    for (wchar_t c : primaryW) primaryA.push_back(static_cast<char>(c)); // Win32 display names are ASCII.
    // Device matching is case-insensitive, like the underlying Windows API.
    std::transform(primaryW.begin(), primaryW.end(), primaryW.begin(), ::towlower);
    std::transform(primaryA.begin(), primaryA.end(), primaryA.begin(), ::tolower);
    std::vector<std::pair<std::wstring, DEVMODEW>> otherDevices;
    std::vector<std::pair<std::string, DEVMODEA>> otherDevicesA;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &deviceW, 0); ++i) {
        if (deviceW.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) continue;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(deviceW.DeviceName, 0, &mode))
            otherDevices.emplace_back(deviceW.DeviceName, mode);
    }
    DISPLAY_DEVICEA deviceA{};
    deviceA.cb = sizeof(deviceA);
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &deviceA, 0); ++i) {
        if (deviceA.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) continue;
        DEVMODEA mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsA(deviceA.DeviceName, 0, &mode))
            otherDevicesA.emplace_back(deviceA.DeviceName, mode);
    }

    const auto dir = std::filesystem::temp_directory_path() /
        (L"BigFix-display-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto log = dir / L"BigFix.log";
    logInit(log);
    REQUIRE(MH_Initialize() == MH_OK);
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() {
            MH_Uninitialize();
            logClose();
            std::error_code ignored;
            std::filesystem::remove_all(dir, ignored);
        }
    } cleanup{dir};
    Settings settings;
    settings.displayModeFilter = false;
    installDisplayModeHooks(settings);
    CHECK(enumerate<DEVMODEW>(&EnumDisplaySettingsW).size() == wide.size());
    CHECK(enumerate<DEVMODEA>(&EnumDisplaySettingsA).size() == ansi.size());
    settings.displayModeFilter = true;
    installDisplayModeHooks(settings);
    std::vector<Mode> tuplesW, tuplesA;
    for (const auto& mode : wide) tuplesW.push_back(tupleOf(mode));
    for (const auto& mode : ansi) tuplesA.push_back(tupleOf(mode));
    checkCachedModes(&EnumDisplaySettingsW, primaryW.c_str(), wide, filterModes(tuplesW, tupleOf(currentW), 512));
    checkCachedModes(&EnumDisplaySettingsA, primaryA.c_str(), ansi, filterModes(tuplesA, tupleOf(currentA), 512));
    for (DWORD index : {DWORD(ENUM_CURRENT_SETTINGS), DWORD(ENUM_REGISTRY_SETTINGS)}) {
        DEVMODEW w{}; w.dmSize = sizeof(w);
        DEVMODEA a{}; a.dmSize = sizeof(a);
        CHECK(EnumDisplaySettingsW(nullptr, index, &w) == (index == ENUM_CURRENT_SETTINGS ? TRUE : registryResultW));
        CHECK(EnumDisplaySettingsA(nullptr, index, &a) == (index == ENUM_CURRENT_SETTINGS ? TRUE : registryResultA));
        CHECK(std::memcmp(&w, index == ENUM_CURRENT_SETTINGS ? &currentW : &registryW, sizeof(w)) == 0);
        CHECK(std::memcmp(&a, index == ENUM_CURRENT_SETTINGS ? &currentA : &registryA, sizeof(a)) == 0);
        DEVMODEW namedW{}; namedW.dmSize = sizeof(namedW);
        DEVMODEA namedA{}; namedA.dmSize = sizeof(namedA);
        CHECK(EnumDisplaySettingsW(primaryW.c_str(), index, &namedW) ==
              (index == ENUM_CURRENT_SETTINGS ? TRUE : registryResultW));
        CHECK(EnumDisplaySettingsA(primaryA.c_str(), index, &namedA) ==
              (index == ENUM_CURRENT_SETTINGS ? TRUE : registryResultA));
        CHECK(std::memcmp(&namedW, &w, sizeof(w)) == 0);
        CHECK(std::memcmp(&namedA, &a, sizeof(a)) == 0);
    }
    DEVMODEW invalidW{}; invalidW.dmSize = sizeof(invalidW);
    DEVMODEA invalidA{}; invalidA.dmSize = sizeof(invalidA);
    CHECK_FALSE(EnumDisplaySettingsW(L"BigFix-nonexistent-display", 0, &invalidW));
    CHECK_FALSE(EnumDisplaySettingsA("BigFix-nonexistent-display", 0, &invalidA));
    // Exercise real secondary devices too, when attached to this machine.
    for (const auto& device : otherDevices) {
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        REQUIRE(EnumDisplaySettingsW(device.first.c_str(), 0, &mode));
        CHECK(std::memcmp(&mode, &device.second, sizeof(mode)) == 0);
    }
    for (const auto& device : otherDevicesA) {
        DEVMODEA mode{}; mode.dmSize = sizeof(mode);
        REQUIRE(EnumDisplaySettingsA(device.first.c_str(), 0, &mode));
        CHECK(std::memcmp(&mode, &device.second, sizeof(mode)) == 0);
    }
    std::ifstream input(log);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    const auto first = text.find("Display modes:");
    CHECK(first != std::string::npos);
    if (first != std::string::npos) CHECK(text.find("Display modes:", first + 1) == std::string::npos);
    CHECK(text.find("desktop=" + std::to_string(currentW.dmPelsWidth) + "x" +
                    std::to_string(currentW.dmPelsHeight)) != std::string::npos);
    std::cout << text;
}
