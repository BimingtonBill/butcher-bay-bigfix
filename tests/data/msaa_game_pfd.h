#pragma once
#include <windows.h>

// Pixel-format fields reported by the real game on an RTX 3090. ChoosePixelFormat
// returned format 12 despite the best-match request's depth=32 and missing window flag.
inline PIXELFORMATDESCRIPTOR requestedGamePfd()
{
    PIXELFORMATDESCRIPTOR p{};
    p.nSize = sizeof(p);
    p.nVersion = 1;
    p.dwFlags = 0x00000021;
    p.iPixelType = PFD_TYPE_RGBA;
    p.cColorBits = 32;
    p.cAlphaBits = 8;
    p.cDepthBits = 32;
    p.cStencilBits = 8;
    return p;
}

inline PIXELFORMATDESCRIPTOR effectiveGamePfd()
{
    auto p = requestedGamePfd();
    p.dwFlags = 0x00008225;
    p.cRedBits = p.cGreenBits = p.cBlueBits = 8;
    p.cDepthBits = 24;
    p.cAccumBits = 64;
    return p;
}
