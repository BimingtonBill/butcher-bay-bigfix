#pragma once

#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class WindowMode { Borderless, Fullscreen, Windowed };

struct Settings {
    bool textureSizeFix = true;
    bool fixMiscBugs = true;
    bool enable20PlusPlusAnyGpu = true;
    bool eyeshineFix = true;
    int eyeshineFixVariant = 0; // diagnostics override EyeshineFix: 0..8; F9 with TraceShadowPass
    int eyeshineAbBurst = 0; // diagnostic burst alternates original / this variant; 0 disables
    bool eyeshineFlickerWorkaround = false;
    bool displayModeFilter = true;
    bool capturePng = true;
    bool dumpShaders = false;
    bool dumpFrameTimes = false;
    double frameCap = 0.0; // maximum FPS; 0 = uncapped
    bool traceVideo = false;
    bool traceShadowPass = false;
    bool traceBloom = false;
    bool hudAspectFix = true;
    double fov = 0; // horizontal degrees at 4:3 equivalent; 0 = original
    bool traceFov = false;
    bool videoAspectFix = true;
    bool skipLogoVideos = true;
    bool testForceEyeshine = false; // test-only: allow Eyeshine before the game grants it
    bool backgroundAudio = false;
    bool traceAudio = false;
    bool traceEax = false;
    int antiAliasing = 0;
    bool tracePixelFormat = false;
    bool rawMouse = true;
    bool traceInput = false;
    WindowMode windowMode = WindowMode::Borderless;
    unsigned captureVk = VK_F12;
    int captureBurst = 1; // consecutive frames saved per capture key press, 1-16
};

Settings parseSettings(std::string_view iniText, std::vector<std::string>* warnings = nullptr);
std::filesystem::path moduleDir(HMODULE self);
Settings loadSettings(const std::filesystem::path& dir, std::vector<std::string>& warnings);
