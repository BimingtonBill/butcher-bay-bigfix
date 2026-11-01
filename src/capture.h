#pragma once
#include "gl_hooks.h"
#include <array>
#include <ctime>
#include <cstdint>

struct CaptureMeta {
    std::tm time{};
    int w = 0, h = 0;
    std::string shaderMode = "x";
    uint64_t index = 0;
};
std::string captureFileName(const CaptureMeta& meta);
// C++17 input buffer; pixels are tightly packed RGB, bottom row first.
bool writePngRgb(const std::filesystem::path& file, int w, int h,
                 const std::vector<uint8_t>& bottomUpRgb);
// Observe every press, including rejected background presses; refocusing while
// held never creates another press or grants permission to capture.
enum class CaptureKeyDecision { None, RejectBackground, Capture };
CaptureKeyDecision captureKeyDecision(bool down, bool foreground, bool& previousDown);
struct CaptureImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};
// Read the current viewport's back buffer, preserving GL pixel-pack state.
CaptureImage captureViewport(GlApi& api);
class FrameTimeRing {
public:
    void push(double milliseconds);
    std::vector<double> values() const;
private:
    std::array<double, 10000> data_{};
    size_t next_ = 0, size_ = 0;
};
// Creates hooks on already-loaded exports; no DLL loading or worker startup.
// The first swap with queued work starts a process-lifetime worker. CSV snapshots
// flush every 600 intervals; the final partial batch can be lost at process exit.
void installCaptureHooks(HMODULE module, const Settings& settings,
                         const std::filesystem::path& pluginDir);
