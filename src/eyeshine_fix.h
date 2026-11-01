#pragma once
#include <windows.h>
#include <gl/GL.h>
#include <array>
#include <map>
#include <optional>
#include <string_view>

using EyeshineRect = std::array<GLint,4>; // x, y, width, height in GL window coordinates
std::optional<EyeshineRect> eyeshineReducedRegion(const EyeshineRect& viewport,GLint drawableWidth,GLint drawableHeight) noexcept;
std::optional<EyeshineRect> eyeshineRectIntersection(const EyeshineRect& a,const EyeshineRect& b) noexcept;
// Copy metadata belongs to a context and to the texture subsequently sampled on u5.
struct EyeshineScissorTracker {
    std::map<GLint,EyeshineRect> copies;
    void copy(GLint texture,const EyeshineRect& rect);
    std::optional<EyeshineRect> draw(GLint texture,bool scissorOn,const EyeshineRect& current) const noexcept;
    void reset() noexcept { copies.clear(); }
};

// Recognize the NV packed-depth producer by operations, never by a GL name.
// The token is consumed by the next texture copy, including a nonmatching one.
struct EyeshineCopyTracker {
    HGLRC context = nullptr;
    GLint x = 0, y = 0;
    GLsizei width = 0, height = 0;
    void pixels(HGLRC current, GLint sx, GLint sy, GLsizei w, GLsizei h, GLenum type) noexcept;
    bool texture(HGLRC current, GLenum target, GLint level, GLint sx, GLint sy, GLsizei w, GLsizei h) noexcept;
};

// The copy export detours are owned once, alongside TraceShadowPass.
void configureEyeshineFix(HMODULE gl, bool enabled) noexcept;
void disableEyeshineFix() noexcept; // Fail closed if any required detour is unavailable.
void eyeshineCopyTexture(GLenum target, GLint level, GLint dx, GLint dy,
                        GLint x, GLint y, GLsizei w, GLsizei h) noexcept;
void eyeshineOtherCopy(GLenum target, GLint level) noexcept;
void eyeshineFixProgram(GLenum target,std::string_view source) noexcept;
void eyeshineFixBeforeDraw() noexcept;
void eyeshineFixAfterDraw() noexcept;
void eyeshineFixAfterList() noexcept;
void eyeshineFixBeforeClear(GLbitfield mask) noexcept;
void eyeshineFixBeforePixels(GLenum type) noexcept;
// Updates source/destination together, preserving the engine's texture mapping.
bool eyeshineFixCopy(GLenum target,GLint level,GLint& dx,GLint& dy,
                     GLint& x,GLint& y,GLsizei& w,GLsizei& h) noexcept;
void eyeshineFixFrameEnd() noexcept;
void eyeshineFixContextDeleted(HGLRC context) noexcept;
