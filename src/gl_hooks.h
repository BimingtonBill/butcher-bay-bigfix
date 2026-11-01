// Derived from TCoRFix v1.4 by Nezarn (MIT).
#pragma once
#include "config.h"
#include "bloom_trace.h"
#include <gl/GL.h>

// No static opengl32 import: only resolve exports of an already loaded module.
struct GlApi {
    BloomApi bloom;
    const GLubyte* (WINAPI* getString)(GLenum) = nullptr;
    void (WINAPI* getIntegerv)(GLenum, GLint*) = nullptr;
    void (WINAPI* readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (WINAPI* pixelStorei)(GLenum, GLint) = nullptr;
    void (WINAPI* readBuffer)(GLenum) = nullptr;
    void (WINAPI* color4f)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    PROC (WINAPI* getProcAddress)(LPCSTR) = nullptr;
    HGLRC (WINAPI* getCurrentContext)() = nullptr;
    BOOL (WINAPI* deleteContext)(HGLRC) = nullptr;
    void (WINAPI* programString)(GLenum, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* bindProgram)(GLenum, GLuint) = nullptr;
    void (WINAPI* vertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (WINAPI* genQueries)(GLsizei, GLuint*) = nullptr;
    void (WINAPI* deleteQueries)(GLsizei, const GLuint*) = nullptr;
    GLboolean (WINAPI* isQuery)(GLuint) = nullptr;
    void (WINAPI* beginQuery)(GLenum, GLuint) = nullptr;
    void (WINAPI* endQuery)(GLenum) = nullptr;
    void (WINAPI* getQueryObjectiv)(GLuint, GLenum, GLint*) = nullptr;
    void (WINAPI* getQueryObjectuiv)(GLuint, GLenum, GLuint*) = nullptr;
};

// One instance per GL context. Runtime instances and their strings are retained
// for process lifetime; tests inject a GlApi without a GPU or Windows GL driver.
// WGL permits a context to be current on only one thread at a time, serializing
// access to that context's mutable dispatch table and initial string creation.
class GlHooks {
public:
    GlHooks(GlApi& api, Settings settings, std::filesystem::path pluginDir = {});
    const GLubyte* getString(GLenum name);
    void getIntegerv(GLenum name, GLint* data);
    void color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    PROC getProcAddress(LPCSTR name);
    void programString(GLenum target, GLenum format, GLsizei length, const void* text);
    void bindProgram(GLenum target, GLuint program);
    void traceBloomDraw(const char* event, const void* caller);
    void traceBloomSwap(HDC dc, BOOL result);
    void genOcclusionQueries(GLsizei n, GLuint* ids);
    void deleteOcclusionQueries(GLsizei n, const GLuint* ids);
    GLboolean isOcclusionQuery(GLuint id);
    void beginOcclusionQuery(GLuint id);
    void endOcclusionQuery();
    void getOcclusionQueryiv(GLuint id, GLenum name, GLint* params);
    void getOcclusionQueryuiv(GLuint id, GLenum name, GLuint* params);
private:
    GlApi& api_;
    Settings settings_;
    std::filesystem::path pluginDir_;
    std::string extensions_;
    bool extensionsReady_ = false;
    BloomTrace bloom_;
    bool bloomReady_ = false;
    const BloomApi& bloomApi();
};

// Creates (does not enable) GL export hooks; never loads a DLL.
void installGlHooks(HMODULE module, const Settings& settings, const std::filesystem::path& pluginDir);
