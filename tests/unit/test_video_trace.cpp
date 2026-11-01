#include <doctest/doctest.h>
#include "video_trace.h"
#include <MinHook.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "patch_table.h"

TEST_CASE("video_aspect_rect_uses_picture_and_stock_ntsc_policy") {
    struct Case { VideoRect bounds; unsigned w, h; VideoRect want; };
    for (const auto& c : {
        Case{{0,0,1920,1080},640,480,{240,0,1680,1080}},
        Case{{0,0,1440,1080},640,360,{0,135,1440,945}},
        Case{{0,0,2520,1080},640,480,{540,0,1980,1080}},
        Case{{0,0,1920,1080},640,360,{0,0,1920,1080}},
        Case{{0,0,1920,1080},720,486,{240,0,1680,1080}},
        Case{{0,0,1920,1200},640,480,{160,0,1760,1200}},
        Case{{0,0,1920,1200},640,360,{0,60,1920,1140}},
        Case{{0,0,1920,1200},720,486,{160,0,1760,1200}},
        Case{{0,0,2520,1080},640,360,{300,0,2220,1080}},
        Case{{0,0,2520,1080},720,486,{540,0,1980,1080}},
        Case{{0,0,1440,1080},640,480,{0,0,1440,1080}},
        Case{{0,0,1440,1080},720,486,{0,0,1440,1080}},
        Case{{10,20,1930,1100},640,480,{250,20,1690,1100}},
        Case{{0,0,1920,1080},0,480,{0,0,0,0}},
        Case{{20,20,10,10},640,480,{0,0,0,0}}}) {
        const auto actual = desiredVideoRect(c.bounds, c.w, c.h);
        CHECK(actual.left == c.want.left); CHECK(actual.top == c.want.top);
        CHECK(actual.right == c.want.right); CHECK(actual.bottom == c.want.bottom);
    }
}

TEST_CASE("video_prologue_verifier_rejects_mutations_and_out_of_bounds") {
    REQUIRE(videoHookSites().size() == 7);
    for (const auto& site : videoHookSites()) {
        std::vector<std::uint8_t> image(site.rva + site.expected.size());
        std::copy(site.expected.begin(), site.expected.end(), image.begin() + site.rva);
        CHECK(verifyVideoPrologue(image.data(), image.size(), site));
        for (std::size_t i = 0; i < site.expected.size(); ++i) {
            image[site.rva+i] ^= 1;
            CHECK_FALSE(verifyVideoPrologue(image.data(), image.size(), site));
            image[site.rva+i] ^= 1;
        }
        CHECK_FALSE(verifyVideoPrologue(image.data(), image.size()-1, site));
        CHECK_FALSE(verifyVideoPrologue(nullptr, image.size(), site));
    }
}

TEST_CASE("video_internal_prologues_match_reference_files_without_loading_them") {
    const auto root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR unset"); return; }
    for (const auto kind : {VideoModule::System, VideoModule::World}) {
        const auto path = std::filesystem::path(root) / (kind == VideoModule::System ?
            L"System/Win64_AMD64/MSystem.dll" : L"Content/GameWorld_Win64_AMD64.dll");
        std::ifstream input(path, std::ios::binary); REQUIRE(input.is_open());
        const std::vector<std::uint8_t> pe{std::istreambuf_iterator<char>(input), {}};
        for (const auto& site : videoHookSites()) {
            if (site.module != kind) continue;
            CAPTURE(site.name);
            const auto offset = rvaToFileOffset(pe.data(), pe.size(), site.rva);
            REQUIRE(offset.has_value());
            REQUIRE(site.expected.size() <= pe.size() - *offset);
            CHECK(std::equal(site.expected.begin(), site.expected.end(), pe.begin()+*offset));
        }
    }
}

TEST_CASE("trace_video_setting_is_opt_in_and_not_written_to_defaults") {
    CHECK_FALSE(parseSettings("").traceVideo);
    CHECK(parseSettings("[BigFix]\nTraceVideo=1\n").traceVideo);
    CHECK_FALSE(parseSettings("[BigFix]\nTraceVideo=0\n").traceVideo);
    std::vector<std::string> warnings;
    CHECK_FALSE(parseSettings("TraceVideo=2", &warnings).traceVideo);
    REQUIRE(warnings.size() == 1);
}

TEST_CASE("video_off_installs_no_hooks_even_with_an_invalid_module") {
    Settings settings = parseSettings("VideoAspectFix=0\nTraceVideo=0\n");
    for (const auto kind : {VideoModule::System, VideoModule::World, VideoModule::OpenGl})
        CHECK(installVideoTraceHooks(reinterpret_cast<HMODULE>(1), kind, settings) == 0);
}

TEST_CASE("video_aspect_setting_defaults_on_and_accepts_off_without_disabling_trace") {
    std::vector<std::string> warnings;
    const auto settings = parseSettings("VideoAspectFix=0\nTraceVideo=1\n", &warnings);
    CHECK(warnings.empty());
    CHECK(settings.traceVideo);
    CHECK_FALSE(settings.videoAspectFix);
    CHECK(parseSettings("").videoAspectFix);
    CHECK(parseSettings("VideoAspectFix=1").videoAspectFix);
}

TEST_CASE("video_ui_rect_compensates_for_stretched_640x480_coordinates") {
    const auto r=desiredVideoUiRect({0,0,640,480},640,480,2560,1440);
    CHECK(r.left==80);CHECK(r.top==0);CHECK(r.right==560);CHECK(r.bottom==480);
    const auto invalid=desiredVideoUiRect({0,0,640,480},640,480,0,1440);
    CHECK(invalid.left==0);CHECK(invalid.top==0);CHECK(invalid.right==0);CHECK(invalid.bottom==0);
}
