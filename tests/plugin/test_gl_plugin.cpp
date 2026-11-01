// Extension expectations derived from TCoRFix v1.4 by Nezarn (MIT).
#include "gl_hooks.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace {
template<class T> T proc(HMODULE module, const char* name) {
    const auto result = GetProcAddress(module, name);
    if (!result) throw std::runtime_error(name);
    return reinterpret_cast<T>(result);
}
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4) return 2;
        const bool early = std::wstring_view(argv[3]) == L"early";
        const auto dir = std::filesystem::absolute(argv[1]).parent_path() / (L"gl-plugin-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        const auto asi = dir / L"BigFix.asi";
        std::filesystem::copy_file(argv[1], asi);
        { std::ofstream ini(dir / L"BigFix.ini"); ini << "[BigFix]\nTextureSizeFix=0\n"; }
        check(!GetModuleHandleW(L"opengl32.dll"), "GL must not be imported or preloaded");
        HMODULE gl = early ? LoadLibraryW(argv[2]) : nullptr;
        check(LoadLibraryW(asi.c_str()) != nullptr, "ASI load failed");
        if (!early) {
            check(!GetModuleHandleW(L"opengl32.dll"), "DllMain must not load GL");
            gl = LoadLibraryW(argv[2]);
        }
        check(gl != nullptr, "stub load failed");
        const auto getString = proc<decltype(GlApi::getString)>(gl, "glGetString");
        const auto getInteger = proc<decltype(GlApi::getIntegerv)>(gl, "glGetIntegerv");
        const auto getProc = proc<decltype(GlApi::getProcAddress)>(gl, "wglGetProcAddress");
        const auto color = proc<decltype(GlApi::color4f)>(gl, "glColor4f");
        const auto setContext = proc<void (*)(ULONG_PTR)>(gl, "bigfixSetContext");
        const char* first = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
        const std::string expected = "GL_NV_fragment_program GL_NV_fragment_program_option GL_NV_fragment_program2 GL_NV_occlusion_query GL_NV_copy_depth_to_color GL_EXT_bgra";
        check(first && first == expected, "GL extension hook not active on first call after LoadLibrary");
        GLint units = 0; getInteger(0x84E2, &units); check(units == 32, "texture unit remap failed");
        color(1, 2, 3, 4); check(proc<GLuint (*)()>(gl, "bigfixAttribute")() == 3, "color remap failed");
        const auto beginPrimitive = proc<void (WINAPI*)(GLenum)>(gl, "glBegin");
        const auto endPrimitive = proc<void (WINAPI*)()>(gl, "glEnd");
        for (BOOL alias : {FALSE, TRUE}) {
            proc<void (*)(BOOL)>(gl, "bigfixAliasColor")(alias);
            // Colour is legal inside Begin/End; hook must not query GL state.
            beginPrimitive(GL_POINTS);
            color(-0.5f, 0.25f, 2.0f, 0.75f);
            endPrimitive();
            GLfloat conventional[4]{}, generic[4]{};
            proc<void (WINAPI*)(GLenum, GLfloat*)>(gl, "glGetFloatv")(GL_CURRENT_COLOR, conventional);
            proc<void (*)(GLfloat*)>(gl, "bigfixAttrib3")(generic);
            const GLfloat expectedColor[]{-0.5f, 0.25f, 2.0f, 0.75f};
            for (int i = 0; i < 4; ++i)
                check(conventional[i] == expectedColor[i] && generic[i] == expectedColor[i],
                      "colour lost with separate or aliased conventional/generic storage");
        }
        check(proc<unsigned (*)()>(gl, "bigfixIllegalQueries")() == 0, "colour hook queried state inside Begin/End");
        const auto begin = reinterpret_cast<void (WINAPI*)(GLuint)>(getProc("glBeginOcclusionQueryNV"));
        check(begin != nullptr, "NV query wrapper missing"); begin(7);
        check(proc<GLuint (*)()>(gl, "bigfixQuery")() == 7, "NV-to-ARB dispatch failed");
        const auto end = reinterpret_cast<void (WINAPI*)()>(getProc("glEndOcclusionQueryNV"));
        const auto gen = reinterpret_cast<void (WINAPI*)(GLsizei, GLuint*)>(getProc("glGenOcclusionQueriesNV"));
        const auto del = reinterpret_cast<void (WINAPI*)(GLsizei, const GLuint*)>(getProc("glDeleteOcclusionQueriesNV"));
        const auto is = reinterpret_cast<GLboolean (WINAPI*)(GLuint)>(getProc("glIsOcclusionQueryNV"));
        const auto iv = reinterpret_cast<void (WINAPI*)(GLuint, GLenum, GLint*)>(getProc("glGetOcclusionQueryivNV"));
        const auto uiv = reinterpret_cast<void (WINAPI*)(GLuint, GLenum, GLuint*)>(getProc("glGetOcclusionQueryuivNV"));
        check(end && gen && del && is && iv && uiv, "one or more query wrappers missing");
        end(); check(proc<GLuint (*)()>(gl, "bigfixQuery")() == 0, "end query mapping failed");
        GLuint id = 0; gen(1, &id); check(id == 17 && is(id) == GL_TRUE, "gen/is query mapping failed");
        del(1, &id); check(proc<GLuint (*)()>(gl, "bigfixQuery")() == id, "delete query mapping failed");
        GLint result = 0; iv(7, 0x8866, &result); check(result == 19, "signed query result mapping failed");
        uiv(8, 0x8867, &id); check(id == 23, "unsigned query result mapping failed");
        const auto program = reinterpret_cast<decltype(GlApi::programString)>(getProc("glProgramStringARB"));
        check(program != nullptr, "program hook missing");
        const char bytes[] = {'M','U','L','H',' ','x',';','X'};
        program(0x8804, 0x8875, 7, bytes);
        check(std::string(proc<const char* (*)()>(gl, "bigfixProgram")()) == "MUL x;", "program wrapper failed");
        setContext(2);
        const char* second = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
        check(second && std::string(second).find("GL_ARB_multitexture") != std::string::npos, "second context not filtered separately");
        check(std::string(first) == expected, "first context string invalidated");
        // The function pointer can be reused after changing context; ARB pointers
        // must resolve in that context rather than use the first context's table.
        begin(8); check(proc<GLuint (*)()>(gl, "bigfixQuery")() == 108, "cross-context query dispatch failed");
        setContext(1); check(reinterpret_cast<const char*>(getString(GL_EXTENSIONS)) == first, "extension pointer changed");
        setContext(0); check(getString(GL_EXTENSIONS) == nullptr, "no-context result changed");
        const auto deleteContext = proc<BOOL (WINAPI*)(HGLRC)>(gl, "wglDeleteContext");
        check(deleteContext(reinterpret_cast<HGLRC>(1)) == TRUE, "context deletion failed");
        setContext(1); // Windows may recycle a deleted HGLRC value.
        const char* recreated = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
        check(recreated && std::string(recreated).find("GL_NV_fence") != std::string::npos, "recycled context reused a stale extension cache");
        check(std::string(first) == expected, "deleting context invalidated a previously returned string");
        std::cout << "GL plugin " << (early ? "already-loaded" : "later-loaded") << ": immediate hooks, contexts, shader/query/color forwarding PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
