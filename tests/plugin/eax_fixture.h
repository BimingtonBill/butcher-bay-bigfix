#pragma once
#include <windows.h>
#include <array>

// Wire constants/layouts established in research/re/audio-eax.md. The fixture
// models the traced engine selection; it never loads engine code.
namespace eax {
inline constexpr GUID context{0x1d4870ad,0x0def,0x43c0,{0xa4,0x0c,0x52,0x36,0x32,0x29,0x63,0x42}};
inline constexpr std::array<GUID,4> slots{{
    {0xc4d79f1e,0xf1ac,0x436b,{0xa8,0x1d,0xa7,0x38,0xe7,0x04,0x54,0x69}},
    {0x08c00e96,0x74be,0x4491,{0x93,0xaa,0xe8,0xad,0x35,0xa4,0x91,0x17}},
    {0x1d433b88,0xf0f6,0x4637,{0x91,0x9f,0x60,0xe7,0xe0,0x6b,0x5e,0xdd}},
    {0xefff08ea,0xc7d8,0x44ab,{0x93,0xad,0x6d,0xbd,0x5f,0x91,0x00,0x64}}}};
inline constexpr GUID source{0x1b86b823,0x22df,0x4eae,{0x8b,0x3c,0x12,0x78,0xce,0x54,0x42,0x27}};
inline constexpr GUID reverb{0x0cf95c8f,0xa3cc,0x4849,{0xb0,0xb6,0x83,0x2e,0xcc,0x18,0x22,0xdf}};
inline constexpr HRESULT unsupported = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
struct Slot { GUID effect=reverb; LONG volume=0; LONG lock=1; DWORD flags=0; };
struct Sends { GUID dry{}; GUID wet{}; };
struct Room {
    ULONG environment=26;
    float size=7.5f, diffusion=1.f;
    LONG room=-1000, roomHF=-100, roomLF=0;
    float decay=1.49f, decayHF=.83f, decayLF=1.f;
    LONG reflections=-2602;
    float reflectionDelay=.007f, reflectionPan[3]{};
    LONG reverbLevel=200;
    float reverbDelay=.011f, reverbPan[3]{}, echoTime=.25f, echoDepth=0.f;
    float modulationTime=.25f, modulationDepth=0.f, airAbsorptionHF=-5.f;
    float hfReference=5000.f, lfReference=250.f, roomRolloff=0.f;
    ULONG flags=0x3f;
};
static_assert(sizeof(Slot)==28 && sizeof(Room)==112 && sizeof(Sends)==32 && sizeof(GUID)==16);
}
