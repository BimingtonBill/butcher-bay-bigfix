#pragma once
#include "config.h"
#include <cstdint>

struct VideoRect { int left = 0, top = 0, right = 0, bottom = 0; };
// Physical pixel bounds; ignores stock bogus PAR, including the NTSC exception.
VideoRect desiredVideoRect(VideoRect bounds, unsigned pictureWidth, unsigned pictureHeight);
// The video paint uses 640x480 UI coordinates stretched over the GL viewport.
VideoRect desiredVideoUiRect(VideoRect bounds, unsigned pictureWidth, unsigned pictureHeight,
                             unsigned viewportWidth, unsigned viewportHeight);

enum class VideoModule { System, World, OpenGl };
struct VideoHookSite {
    const char* name;
    VideoModule module;
    std::uint32_t rva;
    std::vector<std::uint8_t> expected;
};
const std::vector<VideoHookSite>& videoHookSites();
bool verifyVideoPrologue(const std::uint8_t* image, std::size_t size, const VideoHookSite& site);
// Shared fix/trace installer, already-loaded modules only. Fix-only installs
// paint/fit detours and resolves GL functions; tracing adds observation hooks.
// Returns number of hooks created (not enabled).
std::size_t installVideoTraceHooks(HMODULE module, VideoModule kind, const Settings& settings);
// Called by the existing swap detour; never installs another swap hook.
void videoTraceSwap(HDC dc, BOOL result) noexcept;
void videoTraceProgramBind(unsigned target, unsigned program) noexcept;
