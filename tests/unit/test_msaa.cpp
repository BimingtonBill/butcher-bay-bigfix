#include "msaa.h"
#include "../data/msaa_game_pfd.h"
#include <algorithm>
#include <doctest/doctest.h>

namespace
{
int attribute(const std::vector<int> &a, int key)
{
    for (size_t i = 0; i + 1 < a.size() && a[i]; i += 2)
        if (a[i] == key)
            return a[i + 1];
    return -1;
}
} // namespace
TEST_CASE("samples_attribute_built")
{
    const GameRequest request{
        {0x2001, 1, 0x2010, 1, 0x2011, 1, 0x2014, 24, 0x2022, 24, 0x2023, 8, 0x2041, 0, 0x2042, 0, 0x2099, 17, 0}};
    const auto a = buildAttribs(request, 4);
    CHECK(attribute(a, 0x2041) == 1);
    CHECK(attribute(a, 0x2042) == 4);
    CHECK(attribute(a, 0x2023) == 8); // stencil shadows
    CHECK(attribute(a, 0x2022) == 24);
    CHECK(attribute(a, 0x2014) == 24);
    CHECK(attribute(a, 0x2011) == 1);
    CHECK(attribute(a, 0x2099) == 17); // unrelated ARB constraints survive
    CHECK(std::count(a.begin(), a.end(), 0x2042) == 1);
    CHECK(a.back() == 0);
}
TEST_CASE("msaa_falls_back_when_unavailable")
{
    std::vector<int> attempts;
    const GameRequest request{{0x2023, 8, 0}};
    const auto chooser = [&](const std::vector<int> &a) {
        CHECK(attribute(a, 0x2023) == 8);
        const int n = attribute(a, 0x2042);
        attempts.push_back(n);
        return n == 2 ? 72 : 0;
    };
    const auto selected = chooseMsaaFormat(request, 8, 11, chooser);
    CHECK(selected.format == 72);
    CHECK(selected.samples == 2);
    CHECK(attempts == std::vector<int>{8, 4, 2});
    attempts.clear();
    const auto none = chooseMsaaFormat(request, 4, 11, [&](const auto &a) {
        attempts.push_back(attribute(a, 0x2042));
        return 0;
    });
    CHECK(none.format == 11); // real original path still starts the game
    CHECK(none.samples == 0);
    CHECK(attempts == std::vector<int>{4, 2, 0});
    const auto off = chooseMsaaFormat(request, 8, 11, [](const auto &a) { return attribute(a, 0x2041) == 0 ? 19 : 0; });
    CHECK(off.format == 19);
    CHECK(off.samples == 0);
}
TEST_CASE("zero_means_untouched")
{
    unsigned calls = 0;
    const auto result = chooseMsaaFormat({}, 0, 13, [&](const auto &) {
        ++calls;
        return 99;
    });
    CHECK(result.format == 13);
    CHECK(result.samples == 0);
    CHECK(calls == 0);
}
TEST_CASE("msaa_pfd_preserves_stencil_and_optional_buffers_without_accumulation")
{
    PIXELFORMATDESCRIPTOR p{};
    p.nSize = sizeof(p);
    p.nVersion = 1;
    p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_STEREO;
    p.iPixelType = PFD_TYPE_RGBA;
    p.cColorBits = 32;
    p.cAlphaBits = 8;
    p.cDepthBits = 24;
    p.cStencilBits = 8;
    p.cAccumBits = 16;
    p.cAuxBuffers = 2;
    const auto a = buildAttribs(gameRequest(p), 2);
    CHECK(attribute(a, 0x2023) == 8);
    CHECK(attribute(a, 0x2022) == 24);
    CHECK(attribute(a, 0x201b) == 8);
    for (int key : {0x201d, 0x201e, 0x201f, 0x2020, 0x2021})
        CHECK(attribute(a, key) == -1);
    CHECK(attribute(a, 0x2024) == 2);
    CHECK(attribute(a, 0x2011) == 1);
    CHECK(attribute(a, 0x2012) == 1);
}
TEST_CASE("msaa_effective_game_format_matches_hardware_at_4x_and_0x")
{
    const auto requested = requestedGamePfd();
    const auto effective = effectiveGamePfd();
    CHECK(requested.cDepthBits == 32);
    CHECK((requested.dwFlags & PFD_DRAW_TO_WINDOW) == 0);
    for (const int available : {4, 0})
    {
        std::vector<int> attempts;
        const auto selected = chooseMsaaFormat(gameRequest(effective), 4, 12, [&](const auto &a) {
            const int samples = attribute(a, 0x2042);
            attempts.push_back(samples);
            // Model the real driver's constraints: no depth>24, window drawable,
            // and MSAA formats without the legacy format's accumulation buffer.
            if (attribute(a, 0x2022) > 24 || attribute(a, 0x2001) != 1 ||
                attribute(a, 0x201d) > 0 || samples > available)
                return 0;
            CHECK(attribute(a, 0x2023) == 8); // WGL minimum; retain stencil shadows
            CHECK(attribute(a, 0x2022) == 24);
            CHECK(attribute(a, 0x2014) == 24); // WGL excludes the separate alpha bits
            for (int key : {0x2015, 0x2017, 0x2019, 0x201b})
                CHECK(attribute(a, key) == 8);
            CHECK(attribute(a, 0x2011) == 1);
            CHECK(attribute(a, 0x2013) == 0x202b); // RGBA
            CHECK(attribute(a, 0x2003) == 0x2027); // full acceleration
            return samples ? 104 : 112;
        });
        CHECK(selected.format == (available ? 104 : 112));
        CHECK(selected.samples == available);
        CHECK(attempts == (available ? std::vector<int>{4} : std::vector<int>{4, 2, 0}));
    }
}
TEST_CASE("legacy_rgba_total_does_not_become_a_32_bit_rgb_requirement")
{
    const auto a = buildAttribs(gameRequest(effectiveGamePfd()), 4);
    const auto selected = chooseMsaaFormat(gameRequest(effectiveGamePfd()), 4, 12, [](const auto &attrs) {
        return attribute(attrs, 0x2014) <= 24 && attribute(attrs, 0x201b) == 8 ? 44 : 0;
    });
    CHECK(attribute(a, 0x2014) == 24);
    CHECK(selected.format == 44);
    CHECK(selected.samples == 4);
}
TEST_CASE("msaa_retries_depth24_without_accumulation_before_reducing_samples")
{
    const GameRequest request{{0x2001, 1, 0x2010, 1, 0x2011, 1, 0x2014, 24, 0x201b, 8,
                               0x2022, 32, 0x2023, 8, 0x201d, 64, 0x201e, 16, 0x2099, 17, 0}};
    std::vector<int> attempts;
    const auto selected = chooseMsaaFormat(request, 4, 12, [&](const auto &attrs) {
        const int samples = attribute(attrs, 0x2042);
        attempts.push_back(samples);
        CHECK(attribute(attrs, 0x2023) == 8);
        CHECK(attribute(attrs, 0x201b) == 8);
        CHECK(attribute(attrs, 0x2011) == 1);
        CHECK(attribute(attrs, 0x2099) == 17);
        return attribute(attrs, 0x2022) == 24 && attribute(attrs, 0x201d) == -1 &&
                       attribute(attrs, 0x201e) == -1 ? 144 : 0;
    });
    CHECK(selected.format == 144);
    CHECK(selected.samples == 4);
    CHECK(attempts == std::vector<int>{4, 4});
}
TEST_CASE("msaa_preserves_strict_formats_and_never_reduces_stencil_or_other_requirements")
{
    const GameRequest request{{0x2022, 32, 0x2023, 8, 0x201b, 8, 0x2011, 1, 0x2012, 1,
                               0x2024, 2, 0x2003, 0x2027, 0x2099, 17, 0}};
    unsigned calls = 0;
    const auto strict = chooseMsaaFormat(request, 4, 12, [&](const auto &attrs) {
        ++calls;
        CHECK(attribute(attrs, 0x2022) == 32);
        return 144;
    });
    CHECK(calls == 1);
    CHECK(strict.format == 144);
    CHECK(strict.samples == 4);
    calls = 0;
    const auto none = chooseMsaaFormat(request, 4, 12, [&](const auto &attrs) {
        ++calls;
        CHECK(attribute(attrs, 0x2022) >= 24);
        CHECK(attribute(attrs, 0x2023) == 8);
        CHECK(attribute(attrs, 0x201b) == 8);
        CHECK(attribute(attrs, 0x2011) == 1);
        CHECK(attribute(attrs, 0x2012) == 1);
        CHECK(attribute(attrs, 0x2024) == 2);
        CHECK(attribute(attrs, 0x2003) == 0x2027);
        CHECK(attribute(attrs, 0x2099) == 17);
        return 0;
    });
    CHECK(calls == 6); // strict/compatible at 4x, 2x and none
    CHECK(none.format == 12);
    CHECK(none.samples == 0);
}
TEST_CASE("msaa_settings_validate_samples_and_opt_in_trace")
{
    CHECK(parseSettings("").antiAliasing == 0);
    CHECK_FALSE(parseSettings("").tracePixelFormat);
    for (const int n : {0, 2, 4, 8})
    {
        std::vector<std::string> warnings;
        const auto s = parseSettings("[BigFix]\nAntiAliasing=" + std::to_string(n) + "\nTracePixelFormat=1", &warnings);
        CHECK(s.antiAliasing == n);
        CHECK(s.tracePixelFormat);
        CHECK(warnings.empty());
    }
    for (const auto *value : {"1", "3", "16", "-2", "4junk", ""})
    {
        std::vector<std::string> warnings;
        CHECK(parseSettings(std::string("AntiAliasing=") + value, &warnings).antiAliasing == 0);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("Invalid value") != std::string::npos);
    }
}
