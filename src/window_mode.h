#pragma once
#include "config.h"
#include "display_modes.h"
#include <string_view>

struct Rect { long l, t, r, b; };
DWORD borderlessStyle(DWORD gameStyle);
bool shouldSuppressModeChange(WindowMode mode, const DEVMODEW* requested, Mode desktop);
using MonitorInfoFn = BOOL (WINAPI*)(HMONITOR, LPMONITORINFO);
Rect borderlessRect(HMONITOR monitor, MonitorInfoFn query = &GetMonitorInfoW);
bool bypassGameWindowMessage(WindowMode mode, UINT message, WPARAM parameter);
Mode renderModeRequest(WindowMode policy, Mode requested, Mode desktop);
void configureWindowMode(const Settings& settings);
// Fingerprints are computed by deferred module handlers, never by DllMain.
void installWindowModeHooks(HMODULE renderer, const Settings& settings,
                            std::string_view rendererHash, std::string_view systemHash);
