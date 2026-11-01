#include <doctest/doctest.h>
#include "config.h"

#include <fstream>
#include <iterator>

namespace {
struct ConfigDirectory {
    std::filesystem::path oldCwd = std::filesystem::current_path();
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        (L"BigFix-config-" + std::to_wstring(GetCurrentProcessId()));
    ConfigDirectory() { std::filesystem::create_directories(dir / L"elsewhere"); }
    ~ConfigDirectory() {
        std::filesystem::current_path(oldCwd);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

void checkDefaults(const Settings& s) {
    CHECK(s.textureSizeFix);
    CHECK(s.fixMiscBugs);
    CHECK(s.enable20PlusPlusAnyGpu);
    CHECK(s.eyeshineFix);
    CHECK(s.eyeshineFixVariant == 0);
    CHECK_FALSE(s.eyeshineFlickerWorkaround);
    CHECK(s.displayModeFilter);
    CHECK(s.windowMode == WindowMode::Borderless);
    CHECK(s.captureVk == VK_F12);
    CHECK(s.capturePng);
    CHECK(s.captureBurst == 1);
    CHECK_FALSE(s.dumpShaders);
    CHECK_FALSE(s.dumpFrameTimes);
    CHECK(s.frameCap == 0.0);
    CHECK_FALSE(s.traceVideo);
    CHECK_FALSE(s.traceShadowPass);
    CHECK_FALSE(s.traceBloom);
    CHECK(s.videoAspectFix);
    CHECK(s.skipLogoVideos);
    CHECK_FALSE(s.testForceEyeshine);
    CHECK_FALSE(s.backgroundAudio);
    CHECK_FALSE(s.traceAudio);
    CHECK_FALSE(s.traceEax);
    CHECK(s.antiAliasing == 0);
    CHECK_FALSE(s.tracePixelFormat);
    CHECK(s.rawMouse);
    CHECK_FALSE(s.traceInput);
}
}

TEST_CASE("defaults_when_empty") {
    checkDefaults(Settings{});
    checkDefaults(parseSettings(""));
}

TEST_CASE("skip_logos_defaults_on_and_can_be_disabled") {
    CHECK(parseSettings("SkipLogoVideos=1").skipLogoVideos);
    CHECK_FALSE(parseSettings("[BigFix]\nSkipLogoVideos=0").skipLogoVideos);
    std::vector<std::string> warnings;
    CHECK(parseSettings("SkipLogoVideos=2", &warnings).skipLogoVideos);
    REQUIRE(warnings.size() == 1);
}

TEST_CASE("raw_mouse_config_defaults_and_opt_out") {
    CHECK(parseSettings("").rawMouse);
    CHECK_FALSE(parseSettings("").traceInput);
    std::vector<std::string> warnings;
    const auto settings=parseSettings("[BigFix]\nRawMouse=0\nTraceInput=1\n", &warnings);
    CHECK_FALSE(settings.rawMouse);
    CHECK(settings.traceInput);
    CHECK(warnings.empty());
}

TEST_CASE("tcorfix_aliases") {
    const auto s = parseSettings("[General]\nFix Texture Size=0\n"
        "Enable 2.0++ on any GPU=0\nFix Misc Bugs=0\nEyeshine Flicker Workaround=1\n");
    CHECK_FALSE(s.textureSizeFix);
    CHECK_FALSE(s.enable20PlusPlusAnyGpu);
    CHECK_FALSE(s.fixMiscBugs);
    CHECK(s.eyeshineFlickerWorkaround);
}

TEST_CASE("bigfix_keys_win_over_aliases") {
    for (const auto text : {
        "[General]\nFix Texture Size=0\n[BigFix]\nTextureSizeFix=1\n",
        "[BigFix]\nTextureSizeFix=1\n[General]\nFix Texture Size=0\n"}) {
        CHECK(parseSettings(text).textureSizeFix);
    }
}

TEST_CASE("window_mode_values") {
    CHECK(parseSettings("[BigFix]\nWindowMode=BoRdErLeSs").windowMode == WindowMode::Borderless);
    CHECK(parseSettings("[BigFix]\nWindowMode=FULLSCREEN").windowMode == WindowMode::Fullscreen);
    CHECK(parseSettings("[BigFix]\nWindowMode=windowed").windowMode == WindowMode::Windowed);
    std::vector<std::string> warnings;
    CHECK(parseSettings("[BigFix]\nWindowMode=invalid", &warnings).windowMode == WindowMode::Borderless);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("WindowMode") != std::string::npos);
}

TEST_CASE("bigfix_settings_trim_comments_and_case") {
    std::vector<std::string> warnings;
    const auto s = parseSettings(" ; comment\r\n [bIgFiX] \r\n TextureSizeFix = 0 ; off\r\n"
        "FixMiscBugs=0\nEnable20PlusPlusAnyGpu=0\nEyeshineFlickerWorkaround=1\n"
        "DisplayModeFilter=0\nCapturePng=0\nDumpShaders=1\nDumpFrameTimes=1\nCaptureKey=f9\n", &warnings);
    CHECK_FALSE(s.textureSizeFix);
    CHECK_FALSE(s.fixMiscBugs);
    CHECK_FALSE(s.enable20PlusPlusAnyGpu);
    CHECK(s.eyeshineFlickerWorkaround);
    CHECK_FALSE(s.displayModeFilter);
    CHECK_FALSE(s.capturePng);
    CHECK(s.dumpShaders);
    CHECK(s.dumpFrameTimes);
    CHECK(s.captureVk == VK_F9);
    CHECK(warnings.empty());
}

TEST_CASE("invalid_and_unknown_settings_warn_without_changing_defaults") {
    std::vector<std::string> warnings;
    const auto s = parseSettings("[BigFix]\nTextureSizeFix=garbage\nCaptureKey=F25\n"
        "DumpFrameTimes=2\nUnknown=1\nbroken line\n", &warnings);
    checkDefaults(s);
    CHECK(warnings.size() == 5);
}

TEST_CASE("background_audio_settings") {
    CHECK(parseSettings("[BigFix]\nBackgroundAudio=1\nTraceAudio=1").backgroundAudio);
    CHECK(parseSettings("[BigFix]\nBackgroundAudio=1\nTraceAudio=1").traceAudio);
    CHECK(parseSettings("[BigFix]\nBackgroundAudio=1").backgroundAudio);
    CHECK_FALSE(parseSettings("[BigFix]\nBackgroundAudio=1").traceAudio);
    CHECK_FALSE(parseSettings("[BigFix]\nTraceAudio=1").backgroundAudio);
    CHECK(parseSettings("[BigFix]\nTraceEax=1").traceEax);
    CHECK_FALSE(parseSettings("[BigFix]\nTraceEax=1").backgroundAudio);
    CHECK_FALSE(parseSettings("[BigFix]\nBackgroundAudio=0\nTraceAudio=0").backgroundAudio);
    CHECK_FALSE(parseSettings("[BigFix]\nBackgroundAudio=0\nTraceAudio=0").traceAudio);
    std::vector<std::string> warnings;
    const auto invalid = parseSettings("[BigFix]\nBackgroundAudio=yes\nTraceAudio=2", &warnings);
    checkDefaults(invalid);
    CHECK(warnings.size() == 2);
}

TEST_CASE("capture_key_function_key_boundaries") {
    CHECK(parseSettings("CaptureKey=F1").captureVk == VK_F1);
    CHECK(parseSettings("CaptureKey=F24").captureVk == VK_F24);
    CHECK(parseSettings("CaptureKey=F12junk").captureVk == VK_F12);
}

TEST_CASE("frame_cap_values") {
    CHECK(parseSettings("[BigFix]\nFrameCap=60\n").frameCap == doctest::Approx(60.0));
    CHECK(parseSettings("[BigFix]\nFrameCap=0\n").frameCap == 0.0);
    CHECK(parseSettings("FrameCap=59.94\n").frameCap == doctest::Approx(59.94));
    CHECK(parseSettings("[BigFix]\nframecap=144\n").frameCap == doctest::Approx(144.0));
    // A cap above any refresh rate is a diagnostic, not a mistake.
    CHECK(parseSettings("[BigFix]\nFrameCap=2000\n").frameCap == doctest::Approx(2000.0));

    std::vector<std::string> warnings;
    const auto settings = parseSettings("[BigFix]\nFrameCap=fast\n", &warnings);
    CHECK(settings.frameCap == 0.0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings.front().find("FrameCap") != std::string::npos);
    // Below 1 FPS (and negatives) is invalid and keeps the default (off).
    CHECK(parseSettings("[BigFix]\nFrameCap=-60\n").frameCap == 0.0);
    CHECK(parseSettings("[BigFix]\nFrameCap=0.5\n").frameCap == 0.0);
    CHECK(parseSettings("[BigFix]\nFrameCap=1\n").frameCap == 1.0);
}

TEST_CASE("config_path_is_module_relative") {
    ConfigDirectory temp;
    { std::ofstream out(temp.dir / L"BigFix.ini"); out << "[BigFix]\nTextureSizeFix=0\n"; }
    { std::ofstream out(temp.dir / L"elsewhere" / L"BigFix.ini"); out << "TextureSizeFix=1\n"; }
    std::filesystem::current_path(temp.dir / L"elsewhere");
    std::vector<std::string> warnings;
    CHECK_FALSE(loadSettings(temp.dir, warnings).textureSizeFix);
    CHECK(warnings.empty());
}

TEST_CASE("missing_config_writes_complete_defaults_next_to_module") {
    ConfigDirectory temp;
    std::filesystem::current_path(temp.dir / L"elsewhere");
    std::vector<std::string> warnings;
    checkDefaults(loadSettings(temp.dir, warnings));
    REQUIRE(std::filesystem::exists(temp.dir / L"BigFix.ini"));
    CHECK_FALSE(std::filesystem::exists(std::filesystem::current_path() / L"BigFix.ini"));
    std::ifstream in(temp.dir / L"BigFix.ini");
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    CHECK(text.find("TraceVideo") == std::string::npos);
    CHECK(text.find("TraceAudio") == std::string::npos);
    for (const auto key : {"TextureSizeFix=1", "FixMiscBugs=1", "Enable20PlusPlusAnyGpu=1",
         "EyeshineFix=1", "EyeshineFixVariant=0", "EyeshineFlickerWorkaround=0", "DisplayModeFilter=1", "VideoAspectFix=1", "SkipLogoVideos=1", "BackgroundAudio=0",
         "WindowMode=borderless", "CaptureKey=F12", "CapturePng=1", "FrameCap=0", "DumpShaders=0", "DumpFrameTimes=0"}) {
        CHECK(text.find(key) != std::string::npos);
    }
    checkDefaults(parseSettings(text, &warnings));
    CHECK(warnings.empty());
}

TEST_CASE("config_io_failure_is_reported") {
    ConfigDirectory temp;
    std::vector<std::string> warnings;
    checkDefaults(loadSettings(temp.dir / L"does-not-exist", warnings));
    CHECK_FALSE(warnings.empty());
}

TEST_CASE("module_directory_uses_the_supplied_module") {
    ConfigDirectory temp;
    std::filesystem::current_path(temp.dir);
    wchar_t path[32768]{};
    const auto self = GetModuleHandleW(nullptr);
    REQUIRE(GetModuleFileNameW(self, path, 32768) != 0);
    CHECK(moduleDir(self) == std::filesystem::path(path).parent_path());
    CHECK_THROWS(moduleDir(reinterpret_cast<HMODULE>(1)));
}

TEST_CASE("capture_burst_values") {
    CHECK(parseSettings("").captureBurst == 1);
    CHECK(parseSettings("[BigFix]\nCaptureBurst=8\n").captureBurst == 8);
    CHECK(parseSettings("[BigFix]\nCaptureBurst=16\n").captureBurst == 16);
    for (const char* bad : {"0", "17", "-1", "4x", ""}) {
        std::vector<std::string> warnings;
        CHECK(parseSettings(std::string("[BigFix]\nCaptureBurst=") + bad + "\n", &warnings).captureBurst == 1);
        CHECK_FALSE(warnings.empty());
    }
}
