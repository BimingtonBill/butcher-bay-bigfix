#include "capture.h"
#include "frame_limiter.h"
#include "video_trace.h"
#include "widescreen.h"
#include "shadow_trace.h"
#include "eyeshine_variants.h"
#include "eyeshine_fix.h"
#include "bloom_trace.h"
#include "log.h"
#include "module_watch.h"
#include <MinHook.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../third_party/stb_image_write.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

namespace {
size_t rgbSize(int w, int h) {
    // stb uses signed int byte counts internally. Reject overflow before allocation.
    if (w <= 0 || h <= 0 || w > (std::numeric_limits<int>::max)() / 3) return 0;
    const auto stride = size_t(w) * 3;
    if (size_t(h) > size_t((std::numeric_limits<int>::max)() - h) / (stride + 1)) return 0;
    return stride * size_t(h);
}
struct PngOutput { FILE* file; bool ok = true; };
void pngBytes(void* context, void* data, int size) {
    auto& out = *static_cast<PngOutput*>(context);
    if (size < 0 || fwrite(data, 1, size_t(size), out.file) != size_t(size)) out.ok = false;
}
}

std::string captureFileName(const CaptureMeta& meta) {
    std::ostringstream name;
    name << "BigFix_" << std::put_time(&meta.time, "%Y%m%d-%H%M%S") << '_'
         << meta.w << 'x' << meta.h << "_sm" << meta.shaderMode << '_' << meta.index << ".png";
    return name.str();
}

bool writePngRgb(const std::filesystem::path& file, int w, int h,
                 const std::vector<uint8_t>& bottomUpRgb) {
    const auto size = rgbSize(w, h);
    if (!size || bottomUpRgb.size() != size) return false;
    // Do not use stb's process-global flip flag; callers may run concurrently.
    std::vector<uint8_t> topDown(size);
    const auto stride = size_t(w) * 3;
    for (int row = 0; row < h; ++row)
        std::copy_n(bottomUpRgb.data() + size_t(h - row - 1) * stride, stride,
                    topDown.data() + size_t(row) * stride);
    FILE* output = nullptr;
    if (_wfopen_s(&output, file.c_str(), L"wb") != 0) return false;
    PngOutput sink{output};
    const bool encoded = stbi_write_png_to_func(pngBytes, &sink, w, h, 3, topDown.data(), w * 3) != 0;
    const bool closed = fclose(output) == 0;
    if (!encoded || !sink.ok || !closed) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        return false;
    }
    return true;
}

CaptureKeyDecision captureKeyDecision(bool down, bool foreground, bool& previousDown) {
    const bool edge = down && !previousDown;
    previousDown = down; // A key held while refocusing must not trigger a capture.
    if (!edge) return CaptureKeyDecision::None;
    return foreground ? CaptureKeyDecision::Capture : CaptureKeyDecision::RejectBackground;
}

CaptureImage captureViewport(GlApi& api) {
    if (!api.getIntegerv || !api.readPixels || !api.pixelStorei || !api.readBuffer) return {};
    GLint viewport[4]{};
    api.getIntegerv(GL_VIEWPORT, viewport);
    const auto size = rgbSize(viewport[2], viewport[3]);
    if (!size) return {};
    CaptureImage image{viewport[2], viewport[3], std::vector<uint8_t>(size)};
    const GLenum names[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS};
    GLint saved[4]{};
    GLint buffer = GL_BACK;
    api.getIntegerv(GL_READ_BUFFER, &buffer);
    for (size_t i = 0; i < 4; ++i) api.getIntegerv(names[i], &saved[i]);
    api.readBuffer(GL_BACK);
    for (size_t i = 0; i < 4; ++i) api.pixelStorei(names[i], i == 0 ? 1 : 0);
    api.readPixels(viewport[0], viewport[1], image.w, image.h, GL_RGB, GL_UNSIGNED_BYTE, image.rgb.data());
    for (size_t i = 0; i < 4; ++i) api.pixelStorei(names[i], saved[i]);
    api.readBuffer(buffer);
    return image;
}

void FrameTimeRing::push(double milliseconds) {
    data_[next_] = milliseconds;
    next_ = (next_ + 1) % data_.size();
    size_ = (std::min)(size_ + 1, data_.size());
}
std::vector<double> FrameTimeRing::values() const {
    std::vector<double> result;
    result.reserve(size_);
    const auto start = (next_ + data_.size() - size_) % data_.size();
    for (size_t i = 0; i < size_; ++i) result.push_back(data_[(start + i) % data_.size()]);
    return result;
}

namespace {
using SwapFn = BOOL (WINAPI*)(HDC);
SwapFn originalSwap = nullptr;

struct CaptureJob {
    CaptureMeta meta;
    CaptureImage image;
    std::filesystem::path folder;
};
struct Runtime {
    std::mutex mutex;
    std::condition_variable wake;
    Settings settings;
    std::unique_ptr<FrameLimiter> limiter;
    std::filesystem::path pluginDir;
    GlApi api;
    FrameTimeRing times;
    LARGE_INTEGER last{}, frequency{};
    uint64_t frames = 0, index = 0;
    bool previousDown = false, workerStarted = false, pngBusy = false, firstFramePresented = false;
    int burstRemaining = 0; // frames still to read back for the current key press
    bool abPending = false; // arm only after this frame has been presented
    std::deque<CaptureJob> pendingPngs;
    std::vector<double> pendingTimes;
};
Runtime& runtime() {
    // The ASI is pinned by plugin_main. A process-lifetime worker owns these
    // objects: no destructor, worker join, or CSV write under the loader lock.
    static auto* state = new Runtime;
    return *state;
}

std::string shaderModeAtCapture(const std::filesystem::path& root) {
    std::ifstream file(root / L"Environment.cfg");
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        const auto first = key.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        key = key.substr(first, key.find_last_not_of(" \t\r") - first + 1);
        if (_stricmp(key.c_str(), "XR_SHADERMODE") != 0) continue;
        std::istringstream value(line.substr(eq + 1));
        unsigned mode = 0;
        if (value >> mode) return std::to_string(mode);
        break;
    }
    return "x";
}

void writeFrameTimes(const std::filesystem::path& dir, const std::vector<double>& times) {
    // Replace a complete snapshot atomically; never expose a truncated CSV.
    const auto temporary = dir / L"frametimes.csv.tmp";
    std::ofstream file(temporary, std::ios::trunc);
    file << "frame,ms\n" << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < times.size(); ++i) file << i + 1 << ',' << times[i] << '\n';
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), (dir / L"frametimes.csv").c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        logLineNoThrow("Frame-time CSV write failed");
}

DWORD WINAPI writer(void*) noexcept {
    auto& state = runtime();
    for (;;) {
        std::optional<CaptureJob> png;
        std::vector<double> times;
        std::filesystem::path dir;
        try {
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                state.wake.wait(lock, [&] { return !state.pendingPngs.empty() || !state.pendingTimes.empty(); });
                dir = state.pluginDir;
                if (!state.pendingPngs.empty()) {
                    png = std::move(state.pendingPngs.front());
                    state.pendingPngs.pop_front();
                }
                times.swap(state.pendingTimes);
            }
            if (png) {
                std::filesystem::create_directories(png->folder);
                const auto captureIndex=png->meta.index;
                auto path = png->folder / captureFileName(png->meta);
                // Avoid overwriting an earlier launch's screenshot in the same second.
                while (std::filesystem::exists(path)) {
                    ++png->meta.index;
                    path = png->folder / captureFileName(png->meta);
                }
                if(captureIndex!=png->meta.index)
                    logf("Capture index remap: cap=%llu file=%s",static_cast<unsigned long long>(captureIndex),path.u8string().c_str());
                if (writePngRgb(path, png->image.w, png->image.h, png->image.rgb))
                    logf("Capture saved: %s %dx%d", path.u8string().c_str(), png->image.w, png->image.h);
                else logLineNoThrow("Capture failed: PNG encoding or file write failed");
            }
            if (!times.empty()) writeFrameTimes(dir, times);
        } catch (const std::exception& error) {
            if (png) {
                try { logf("Capture failed: %s", error.what()); } catch (...) { }
            } else logLineNoThrow("Frame-time background write failed; game continues");
        } catch (...) {
            logLineNoThrow(png ? "Capture failed: unexpected background write exception" :
                                "Frame-time background write failed; game continues");
        }
        if (png) {
            const std::lock_guard<std::mutex> lock(state.mutex);
            // Busy until the whole burst is read back and written.
            state.pngBusy = state.burstRemaining > 0 || !state.pendingPngs.empty();
        }
    }
}

bool startWriter(Runtime& state) {
    if (state.workerStarted) return true;
    // Called only from SwapBuffers, never from DllMain/module registration.
    const auto thread = CreateThread(nullptr, 0, writer, nullptr, 0, nullptr);
    if (!thread) { logLineNoThrow("Capture writer thread creation failed"); return false; }
    CloseHandle(thread);
    state.workerStarted = true;
    return true;
}

void beforeSwap() {
    auto& state = runtime();
    GlApi api;
    std::filesystem::path root;
    CaptureMeta meta;
    bool capture = false;
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        shadowTraceCapture(0,state.pngBusy);
        if (state.settings.dumpFrameTimes) {
            LARGE_INTEGER now{};
            if (QueryPerformanceCounter(&now)) {
                if (!state.frequency.QuadPart) QueryPerformanceFrequency(&state.frequency);
                if (state.last.QuadPart && state.frequency.QuadPart) {
                    state.times.push(double(now.QuadPart - state.last.QuadPart) * 1000.0 / state.frequency.QuadPart);
                    if (++state.frames % 600 == 0 && startWriter(state)) {
                        state.pendingTimes = state.times.values(); // Coalesce slow-disk snapshots.
                        state.wake.notify_one();
                    }
                }
                state.last = now;
            }
        }
        if (!state.settings.capturePng) return;
        if (state.burstRemaining > 0) {
            // Later frames of a burst: consecutive swaps, no key needed.
            api = state.api;
            root = state.pluginDir.parent_path().parent_path();
            meta.index = ++state.index;
            capture = true;
        }
    }
    if (!capture) {
        const std::lock_guard<std::mutex> lock(state.mutex);
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        const bool down = (GetAsyncKeyState(static_cast<int>(state.settings.captureVk)) & 0x8000) != 0;
        const auto decision = captureKeyDecision(down, foregroundProcess == GetCurrentProcessId(), state.previousDown);
        if (decision == CaptureKeyDecision::None) return;
        logf("Capture key down: foreground=%d", decision == CaptureKeyDecision::Capture ? 1 : 0);
        if (decision == CaptureKeyDecision::RejectBackground) {
            logLineNoThrow("Capture failed: foreground window is not owned by this process");
            return;
        }
        api = state.api;
        if (state.pngBusy) {
            logLineNoThrow("Capture failed: previous PNG capture still busy");
        } else if (!api.getCurrentContext || !api.getCurrentContext()) {
            logLineNoThrow("Capture failed: no current GL context");
        } else if (!startWriter(state)) {
            logLineNoThrow("Capture failed: writer thread creation failed");
        } else {
            root = state.pluginDir.parent_path().parent_path();
            state.burstRemaining = state.settings.captureBurst;
            state.pngBusy = true;
            if(eyeshineAbConfigured()) {
                state.abPending=true;
                return; // The current image was rendered before the key press.
            }
            meta.index = ++state.index;
            capture = true;
        }
    }
    if (!capture) return;
    try {
        const auto now = std::time(nullptr);
        localtime_s(&meta.time, &now);
        meta.shaderMode = shaderModeAtCapture(root);
        auto image = captureViewport(api); // GL must stay on the current render thread.
        meta.w = image.w; meta.h = image.h;
        const std::lock_guard<std::mutex> lock(state.mutex);
        if (image.rgb.empty()) {
            state.burstRemaining = 0;
            eyeshineAbCancel();
            state.pngBusy = !state.pendingPngs.empty();
            logLineNoThrow("Capture failed: GL readback unavailable or invalid viewport");
        } else {
            --state.burstRemaining;
            const auto captureIndex=meta.index;
            state.pendingPngs.push_back(CaptureJob{std::move(meta), std::move(image), root / L"ScreenShots"});
            shadowTraceCapture(captureIndex,true);
            eyeshineAbCapture(captureIndex);
            state.wake.notify_one();
        }
    } catch (const std::exception& error) {
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.burstRemaining = 0;
        eyeshineAbCancel();
        state.pngBusy = !state.pendingPngs.empty();
        logf("Capture failed: %s", error.what());
    } catch (...) {
        const std::lock_guard<std::mutex> lock(state.mutex);
        state.burstRemaining = 0;
        eyeshineAbCancel();
        state.pngBusy = !state.pendingPngs.empty();
        logLineNoThrow("Capture failed: unexpected readback exception");
    }
}

void paceFrame() {
    auto& state = runtime();
    FrameLimiter* limiter = nullptr;
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        limiter = state.limiter.get();
    }
    // Waited outside the lock: the wait is most of the frame and the mutex also
    // guards the capture-worker handoff.
    if (limiter) limiter->waitBeforeSwap();
}

void afterSuccessfulSwap() {
    auto& state = runtime();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (state.firstFramePresented || !state.api.getIntegerv || !state.api.getCurrentContext ||
        !state.api.getCurrentContext()) return;
    GLint viewport[4]{};
    state.api.getIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    logf("First frame presented: %dx%d", viewport[2], viewport[3]);
    state.firstFramePresented = true;
}

BOOL WINAPI swapBuffers(HDC dc) noexcept {
    // Avoid counting twice if a driver's swap path recurses through this export.
    static thread_local bool swapping = false;
    if (swapping) return originalSwap(dc);
    swapping = true;
    const auto error = GetLastError();
    try {
        static std::once_flag deferred;
        std::call_once(deferred, [] {
            processDeferredModuleWork();
            logExecutableFingerprintOnce();
        });
        beforeSwap();
        paceFrame();
    }
    catch (...) { logLineNoThrow("Capture hook failed; forwarding swap"); }
    SetLastError(error);
    const auto result = originalSwap(dc);
    const auto swapError = GetLastError();
    if(result) eyeshineFixFrameEnd();
    videoTraceSwap(dc, result);
    widescreenFrameEnd(result);
    if (runtime().settings.traceShadowPass)
        shadowTraceSwap(result, (GetAsyncKeyState(VK_F11) & 0x8000) != 0);
    if (eyeshineVariantsEnabled()) {
        DWORD foregroundProcess=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
        eyeshineVariantSwap(result, foregroundProcess==GetCurrentProcessId() && (GetAsyncKeyState(VK_F9)&0x8000)!=0);
    }
    bloomTraceSwap(dc, result);
    {
        auto& state=runtime();
        const std::lock_guard<std::mutex> lock(state.mutex);
        if(state.abPending) {
            state.abPending=false;
            if(!result || !eyeshineAbBegin(state.burstRemaining)) {
                state.burstRemaining=0;state.pngBusy=!state.pendingPngs.empty();
                logLineNoThrow("EyeshineAbBurst failed to arm; no samples queued");
            }
        }
        if(!result && eyeshineAbConfigured()) {state.burstRemaining=0;state.pngBusy=!state.pendingPngs.empty();}
    }
    eyeshineAbSwap(result);
    if (result) {
        try { afterSuccessfulSwap(); }
        catch (...) { logLineNoThrow("First-frame diagnostic failed"); }
    }
    SetLastError(swapError);
    swapping = false;
    return result;
}
}

void installCaptureHooks(HMODULE module, const Settings& settings,
                         const std::filesystem::path& pluginDir) {
    // First-frame readiness is required even when optional captures are disabled.
    auto& state = runtime();
    const std::lock_guard<std::mutex> lock(state.mutex);
    state.settings = settings;
    state.pluginDir = pluginDir;
    // Both handlers pass their module; resolve only the exports it actually owns.
    if (GetProcAddress(module, "glReadPixels")) {
        state.api.getIntegerv = reinterpret_cast<decltype(GlApi::getIntegerv)>(GetProcAddress(module, "glGetIntegerv"));
        state.api.readPixels = reinterpret_cast<decltype(GlApi::readPixels)>(GetProcAddress(module, "glReadPixels"));
        state.api.pixelStorei = reinterpret_cast<decltype(GlApi::pixelStorei)>(GetProcAddress(module, "glPixelStorei"));
        state.api.readBuffer = reinterpret_cast<decltype(GlApi::readBuffer)>(GetProcAddress(module, "glReadBuffer"));
        state.api.getCurrentContext = reinterpret_cast<decltype(GlApi::getCurrentContext)>(GetProcAddress(module, "wglGetCurrentContext"));
    }
    const auto swap = GetProcAddress(module, "SwapBuffers");
    if (swap && !originalSwap) {
        // Created before the hook exists, so the swap thread always sees it.
        state.limiter = std::make_unique<FrameLimiter>(settings.frameCap, settings.dumpFrameTimes);
        if (state.limiter->enabled())
            logf("Frame cap: %.2f FPS (%.2f ms per frame)", settings.frameCap, state.limiter->periodMs());
        const auto status = MH_CreateHook(reinterpret_cast<LPVOID>(swap), reinterpret_cast<LPVOID>(&swapBuffers),
                                          reinterpret_cast<LPVOID*>(&originalSwap));
        if (status != MH_OK) { logf("Capture SwapBuffers hook failed: %s", MH_StatusToString(status)); return; }
        logLineNoThrow("Capture hook: gdi32!SwapBuffers (RndrGL import); frame-time flush every 600 intervals");
    }
}
