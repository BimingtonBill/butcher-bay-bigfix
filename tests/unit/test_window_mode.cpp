#include <doctest/doctest.h>
#include "window_mode.h"
#include "patch_table.h"
#include <algorithm>

TEST_CASE("style_strips_caption_and_frame") {
    const DWORD style = borderlessStyle(0x16cf0000);
    CHECK(style == (WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS));
    CHECK((style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) == 0);
    CHECK(borderlessStyle(0x90000000) == 0x90000000);
}
TEST_CASE("suppress_only_in_borderless") {
    DEVMODEW request{}; request.dmSize = sizeof(request);
    request.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    request.dmPelsWidth = 640; request.dmPelsHeight = 480;
    request.dmBitsPerPel = 32; request.dmDisplayFrequency = 60;
    CHECK(shouldSuppressModeChange(WindowMode::Borderless, &request, {2560, 1440, 32, 144}));
    CHECK_FALSE(shouldSuppressModeChange(WindowMode::Fullscreen, &request, {2560, 1440, 32, 144}));
    CHECK_FALSE(shouldSuppressModeChange(WindowMode::Windowed, &request, {2560, 1440, 32, 144}));
}
TEST_CASE("desktop_mode_change_not_suppressed") {
    DEVMODEW request{}; request.dmSize = sizeof(request);
    request.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    request.dmPelsWidth = 2560; request.dmPelsHeight = 1440;
    request.dmBitsPerPel = 32; request.dmDisplayFrequency = 144;
    CHECK_FALSE(shouldSuppressModeChange(WindowMode::Borderless, &request, {2560, 1440, 32, 144}));
    CHECK_FALSE(shouldSuppressModeChange(WindowMode::Borderless, nullptr, {2560, 1440, 32, 144}));
    // An omitted field is not a request to change it to zero.
    request.dmFields = DM_DISPLAYFREQUENCY;
    request.dmPelsWidth = request.dmPelsHeight = request.dmBitsPerPel = 0;
    CHECK_FALSE(shouldSuppressModeChange(WindowMode::Borderless, &request, {2560, 1440, 32, 144}));
    request.dmDisplayFrequency = 60;
    CHECK(shouldSuppressModeChange(WindowMode::Borderless, &request, {2560, 1440, 32, 144}));
}
namespace {
BOOL WINAPI monitorAt150Percent(HMONITOR, LPMONITORINFO info) {
    // Like Windows, this fake virtualizes coordinates for an unaware caller.
    const bool physical = AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                                     DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    info->rcMonitor = physical ? RECT{-2560, -240, 0, 1200} : RECT{-1707, -160, 0, 800};
    info->rcWork = {-1707, -160, 0, 760};
    return TRUE;
}
}
TEST_CASE("borderless_rect_uses_physical_pixels") {
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    const auto rect = borderlessRect(nullptr, monitorAt150Percent);
    CHECK(rect.l == -2560); CHECK(rect.t == -240);
    CHECK(rect.r - rect.l == 2560); CHECK(rect.b - rect.t == 1440);
    CHECK(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_UNAWARE));
    SetThreadDpiAwarenessContext(previous);
}
TEST_CASE("borderless_focus_loss_bypasses_game_mode_queue_but_regain_forwards") {
    CHECK(bypassGameWindowMessage(WindowMode::Borderless, WM_KILLFOCUS, 0));
    CHECK(bypassGameWindowMessage(WindowMode::Borderless, WM_SYSCOMMAND, SC_MINIMIZE | 2));
    for (UINT msg : {WM_SETFOCUS, WM_ACTIVATEAPP, WM_CLOSE, WM_NCDESTROY})
        CHECK_FALSE(bypassGameWindowMessage(WindowMode::Borderless, msg, 0));
    CHECK_FALSE(bypassGameWindowMessage(WindowMode::Borderless, WM_SYSCOMMAND, SC_CLOSE));
    for (auto mode : {WindowMode::Fullscreen, WindowMode::Windowed}) {
        CHECK_FALSE(bypassGameWindowMessage(mode, WM_KILLFOCUS, 0));
        CHECK_FALSE(bypassGameWindowMessage(mode, WM_SYSCOMMAND, SC_MINIMIZE));
    }
}
TEST_CASE("borderless_native_selection_ignores_saved_dimensions_format_and_refresh") {
    const auto request = renderModeRequest(WindowMode::Borderless, {640, 480, 16, 85}, {2560, 1440, 32, 144});
    CHECK(request.w == 2560); CHECK(request.h == 1440); CHECK(request.bpp == 32); CHECK(request.hz == 144);
    for (auto mode : {WindowMode::Fullscreen, WindowMode::Windowed}) {
        const auto unchanged = renderModeRequest(mode, {640, 480, 16, 85}, {2560, 1440, 32, 144});
        CHECK(unchanged.w == 640); CHECK(unchanged.h == 480); CHECK(unchanged.bpp == 16); CHECK(unchanged.hz == 85);
    }
}
TEST_CASE("desktop_string_selection_patch_is_verified_before_forcing_numeric_route") {
    const auto& site = borderlessModeSelectionSite();
    REQUIRE(site.rva == 0x77885);
    std::vector<uint8_t> image(site.rva + 12);
    const std::vector<uint8_t> original{0x85, 0xc0, 0x75, 0x08, 0x41, 0x8b, 0xdf, 0xe9, 0xb2, 0x01, 0x00, 0x00};
    std::copy(original.begin(), original.end(), image.begin() + site.rva);
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Applied);
    CHECK(image[site.rva + 2] == 0xeb);
    image[site.rva] ^= 1;
    const auto before = image;
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Mismatch);
    CHECK(image == before);
}
TEST_CASE("windowed_policy_uses_original_mode_zero_even_for_saved_fullscreen_options") {
    const auto& site = windowedModeSelectionSite();
    std::vector<uint8_t> image(site.rva + site.expected.size());
    std::copy(site.expected.begin(), site.expected.end(), image.begin() + site.rva);
    REQUIRE(applyPatch(image.data(), image.size(), site) == PatchResult::Applied);
    // TEST EAX,EAX; NOP; NOP; MOV EBX,R15D selects original synthetic mode 0.
    CHECK(image[site.rva + 2] == 0x90);
    CHECK(image[site.rva + 3] == 0x90);
}
