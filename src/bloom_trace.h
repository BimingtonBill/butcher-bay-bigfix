#pragma once
#include <windows.h>
#include <gl/GL.h>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <cstdint>

// Queries are injected for synthetic tests; no static GL imports or game RVAs.
struct BloomApi {
    decltype(&glGetIntegerv) getInteger = nullptr;
    decltype(&glGetFloatv) getFloat = nullptr;
    decltype(&glIsEnabled) isEnabled = nullptr;
    decltype(&glGetTexLevelParameteriv) getTexLevel = nullptr;
    decltype(&wglGetCurrentContext) context = nullptr;
    decltype(&wglGetCurrentDC) dc = nullptr;
    void (WINAPI* activeTexture)(GLenum) = nullptr;
    void (WINAPI* getProgram)(GLenum, GLenum, GLint*) = nullptr;
    void (WINAPI* getEnv)(GLenum, GLuint, GLfloat*) = nullptr;
    void (WINAPI* getAttrib)(GLuint, GLenum, GLfloat*) = nullptr;
    bool framebufferQuery = false;
    bool blendEquationQuery = false;
};

std::uint32_t shaderProgramCrc32(std::string_view source) noexcept;

// Owned by GlHooks: one instance per context, serialized by WGL context rules.
// Sample first three frames then 1Hz; keep last 48 draw/copy events per frame.
// 8192 emitted records process-wide and 256 uploads/context, 512 tracked programs.
class BloomTrace {
public:
    using Sink = void (*)(std::string_view);
    explicit BloomTrace(bool enabled, Sink sink = nullptr);
    void upload(const BloomApi&, GLenum target, std::string_view source, std::string_view submitted) noexcept;
    void draw(const BloomApi&, const char* event, const void* caller) noexcept;
    void swap(std::uint64_t now, HDC dc, BOOL result) noexcept;
private:
    bool enabled_, sample_ = true;
    Sink sink_;
    std::uint64_t frame_ = 0, nextSample_ = 0, events_ = 0;
    unsigned uploads_ = 0;
    std::map<std::pair<GLenum, GLuint>, std::pair<std::uint32_t, std::uint32_t>> programs_;
    std::array<std::string,48> tail_;
    void emit(std::string_view);
};

// Bridges into the existing context dispatch and draw/swap detours. No new
// competing detours; calls are no-ops unless TraceBloom was enabled at startup.
void bloomTraceDraw(const char* event, const void* caller) noexcept;
void bloomTraceSwap(HDC dc, BOOL result) noexcept;
