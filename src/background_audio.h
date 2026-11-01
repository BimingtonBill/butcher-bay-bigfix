#pragma once

#include "config.h"
#include <windows.h>

// Add DSBCAPS_GLOBALFOCUS (0x8000) to a secondary buffer's descriptor flags
// when enabled. Primary-buffer descriptors and every other flag are untouched,
// and the call is idempotent.
DWORD adjustBufferFlags(DWORD flags, bool enable);

// Hook DirectSoundCreate8 in an already-loaded dsound module, then the returned
// device's IDirectSound8::CreateSoundBuffer vtable slot (+0x18). Does nothing
// unless BackgroundAudio, TraceAudio or TraceEax is on. Safe to call once per module.
void installBackgroundAudio(HMODULE dsound, const Settings& settings);
