#pragma once
#include "config.h"
#include <functional>

// A terminated WGL integer list. Keep unknown attributes from advanced requests.
struct GameRequest
{
    std::vector<int> attributes;
};
struct MsaaFormat
{
    int format;
    int samples;
};
// Translate a DescribePixelFormat result, not the game's best-match request.
// Translate RGB bits separately from alpha; exclude accumulation buffers.
GameRequest gameRequest(const PIXELFORMATDESCRIPTOR &descriptor);
std::vector<int> buildAttribs(const GameRequest &request, int samples);
using PixelFormatChooser = std::function<int(const std::vector<int> &)>;
// At each sample count, try the request then depth<=24/no accumulation.
// Preserve colour, alpha, stencil, double buffering and unknown constraints.
MsaaFormat chooseMsaaFormat(const GameRequest &request, int samples, int original, const PixelFormatChooser &chooser);

// Renderer-only IAT hooks, verified against loaded exports; no GL DLL loading.
void installMsaaHooks(HMODULE renderer, const Settings &settings) noexcept;
