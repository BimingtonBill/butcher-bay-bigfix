#pragma once
#include "config.h"
#include <gl/GL.h>
#include <string_view>
#include <array>
#include <cstdint>

// Diagnostic-only: no glGetError, readback, texture writes, or persistent GL state changes.
// F11 requests four detailed frames. Compact records are capture/variant-only. The 2000-line
// process cap reserves 500 lines for compact records after detail is exhausted.
struct ShadowTraceApi {
    decltype(&glGetIntegerv) integer = nullptr;
    decltype(&glGetFloatv) floating = nullptr;
    decltype(&glGetBooleanv) boolean = nullptr;
    decltype(&glIsEnabled) enabled = nullptr;
    decltype(&glGetTexLevelParameteriv) level = nullptr;
    decltype(&glGetTexParameteriv) parameter = nullptr;
    decltype(&wglGetCurrentContext) context = nullptr;
    decltype(&wglGetCurrentDC) dc = nullptr;
    decltype(&wglGetProcAddress) proc = nullptr;
};
void configureShadowTrace(const ShadowTraceApi& api, bool enabled) noexcept;
// Owns shared copy exports for TraceShadowPass and the separately gated EyeshineFix.
void installShadowTraceHooks(HMODULE module, const Settings& settings);
void shadowTraceSwap(BOOL result, bool trigger) noexcept;
void shadowTraceArm(unsigned frames) noexcept;
// Accumulate before capture is requested so the first PNG's frame is complete.
void shadowTraceMaskCopy() noexcept;
void shadowTraceRegionPass() noexcept;
void shadowTraceRegionCopy() noexcept;
void shadowTraceMaskDraw(const std::array<GLint,4>& current,
                         const std::array<GLint,4>& copied, bool restricted) noexcept;
// Render thread only: index is nonzero after successful PNG readback/queueing.
void shadowTraceCapture(std::uint64_t index, bool writing) noexcept;
void shadowTraceProgram(GLenum target, std::string_view source) noexcept;
void shadowTraceProgramBind(GLenum target, GLuint id) noexcept;
void shadowTraceEvent(const char* format, ...) noexcept;
void shadowTraceDraw(const char* kind, GLenum mode, GLsizei count) noexcept;
void shadowTraceClear(GLbitfield mask) noexcept;
bool shadowTraceSampling() noexcept;
