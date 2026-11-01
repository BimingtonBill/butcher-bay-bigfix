#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct Settings;
struct Mode { uint32_t w, h, bpp, hz; };

// Only 32-bpp modes; cap=0 returns none. A 32-bpp desktop survives truncation.
std::vector<Mode> filterModes(const std::vector<Mode>& all, Mode desktop, size_t cap = 512);
// After MH_Initialize, before loading the renderer. Resolves loaded user32 only.
void installDisplayModeHooks(const Settings& settings);
