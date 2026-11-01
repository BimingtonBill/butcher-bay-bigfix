#include "gl_hooks.h"
#include "log.h"
#include "msaa.h"
#include "../data/msaa_game_pfd.h"
#include <MinHook.h>
#include <cstring>
#include <fstream>
#include <gl/GL.h>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
using Arb = BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
using QueryArb = BOOL(WINAPI *)(HDC, int, int, UINT, const int *, int *);
decltype(&wglGetProcAddress) driverProc = nullptr;
decltype(&wglMakeCurrent) driverCurrent = nullptr;
Arb driverChoose = nullptr;
QueryArb driverQuery = nullptr;
int maxSamples = 8;
bool noArb = false, noExtension = false, rgb24Only = false, depth24Only = false, badStencil = false;
BOOL WINAPI restrictedQuery(HDC dc, int format, int layer, UINT count, const int *keys, int *values)
{
    const BOOL result = driverQuery(dc, format, layer, count, keys, values);
    // A broken chooser must never cause us to accept a format without stencil.
    if (result && badStencil)
    {
        const int key = 0x2041;
        int buffers = 0;
        if (driverQuery(dc, format, layer, 1, &key, &buffers) && buffers)
            for (UINT i = 0; i < count; ++i)
                if (keys[i] == 0x2023) values[i] = 0;
    }
    return result;
}
BOOL WINAPI failBootstrapCurrent(HDC dc, HGLRC rc)
{
    wchar_t name[64]{};
    if (dc && GetClassNameW(WindowFromDC(dc), name, 64) && std::wstring(name) == L"BigFixMsaaBootstrap")
    {
        driverCurrent(nullptr, nullptr); // WGL failure can unbind the old context.
        return FALSE;
    }
    return driverCurrent(dc, rc);
}
BOOL WINAPI restrictedChoose(HDC dc, const int *ints, const FLOAT *floats, UINT max, int *formats, UINT *count)
{
    int samples = 0;
    for (size_t i = 0; ints && ints[i]; i += 2)
    {
        if (ints[i] == 0x2042)
            samples = ints[i + 1];
        if ((rgb24Only && ints[i] == 0x2014 && ints[i + 1] > 24) ||
            (depth24Only && ((ints[i] == 0x2022 && ints[i + 1] > 24) ||
                             (ints[i] == 0x201d && ints[i + 1] > 0))))
        {
            *count = 0;
            return TRUE;
        }
        if (noExtension && ints[i] == 0x2041)
        {
            *count = 0;
            return FALSE;
        }
    }
    if (samples > maxSamples)
    {
        *count = 0;
        return TRUE;
    } // successful call, no formats
    return driverChoose(dc, ints, floats, max, formats, count);
}
PROC WINAPI restrictedProc(LPCSTR name)
{
    const auto proc = driverProc(name);
    if (!std::strcmp(name, "wglGetPixelFormatAttribivARB") && proc)
    {
        driverQuery = reinterpret_cast<QueryArb>(proc);
        return reinterpret_cast<PROC>(&restrictedQuery);
    }
    if (!std::strcmp(name, "wglChoosePixelFormatARB"))
    {
        if (noArb)
            return reinterpret_cast<PROC>(1); // WGL's documented failure sentinel
        if (proc)
        {
            driverChoose = reinterpret_cast<Arb>(proc);
            return reinterpret_cast<PROC>(&restrictedChoose);
        }
    }
    return proc;
}
struct Window
{
    HWND wnd = CreateWindowExW(0, L"BigFixMsaaTest", L"hidden", WS_POPUP, 0, 0, 32, 32, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    HDC dc = GetDC(wnd);
    HGLRC rc = nullptr;
    ~Window()
    {
        if (rc)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(rc);
        }
        ReleaseDC(wnd, dc);
        DestroyWindow(wnd);
    }
};
} // namespace
int wmain(int argc, wchar_t **argv)
{
    try
    {
        check(argc == 3, "arguments");
        const std::wstring mode = argv[2];
        const bool off = mode == L"off",
                   advanced = mode == L"advanced" || mode == L"advanced_cold" || mode == L"advanced_no_extension" ||
                              mode == L"advanced_depth32";
        const bool cold = mode == L"cold" || mode == L"advanced_cold";
        const bool limited = mode == L"lower" || mode == L"none" || mode == L"no_arb" || mode == L"no_extension" ||
                             mode == L"advanced_no_extension" || mode == L"make_failure" || mode == L"quiet_none" ||
                             mode == L"bad_stencil";
        const bool restricted = limited || mode == L"rgb24" || mode == L"advanced_depth32";
        const bool coexist = mode == L"coexist";
        WNDCLASSW cls{};
        cls.style = CS_OWNDC;
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"BigFixMsaaTest";
        check(RegisterClassW(&cls) != 0, "register class");
        const auto path = std::filesystem::temp_directory_path() /
                          (L"BigFix-msaa-" + std::to_wstring(GetCurrentProcessId()) + L".log");
        logInit(path);
        auto stub = LoadLibraryW(argv[1]);
        check(stub != nullptr, "load artificial renderer");
        const auto choose =
            reinterpret_cast<int (*)(HDC, const PIXELFORMATDESCRIPTOR *)>(GetProcAddress(stub, "msaaChoose"));
        const auto set =
            reinterpret_cast<BOOL (*)(HDC, int, const PIXELFORMATDESCRIPTOR *)>(GetProcAddress(stub, "msaaSet"));
        const auto current = reinterpret_cast<BOOL (*)(HDC, HGLRC)>(GetProcAddress(stub, "msaaCurrent"));
        const auto proc = reinterpret_cast<PROC (*)(const char *)>(GetProcAddress(stub, "msaaProc"));
        check(choose && set && current && proc, "stub exports");
        Settings settings;
        settings.antiAliasing = off ? 0 : 4;
        settings.tracePixelFormat = mode != L"quiet_none";
        if (restricted || coexist)
        {
            check(MH_Initialize() == MH_OK, "MinHook init");
            if (coexist)
                installGlHooks(GetModuleHandleW(L"opengl32.dll"), settings, {});
            else
            {
                maxSamples = limited && mode != L"bad_stencil" ? (mode == L"lower" ? 2 : 0) : 4;
                noArb = mode == L"no_arb";
                noExtension = mode == L"no_extension" || mode == L"advanced_no_extension";
                rgb24Only = mode == L"rgb24";
                depth24Only = mode == L"advanced_depth32";
                badStencil = mode == L"bad_stencil";
                check(MH_CreateHook(reinterpret_cast<void *>(&wglGetProcAddress),
                                    reinterpret_cast<void *>(&restrictedProc),
                                    reinterpret_cast<void **>(&driverProc)) == MH_OK,
                      "restrict driver hook");
                if (mode == L"make_failure")
                    check(MH_CreateHook(reinterpret_cast<void *>(&wglMakeCurrent),
                                        reinterpret_cast<void *>(&failBootstrapCurrent),
                                        reinterpret_cast<void **>(&driverCurrent)) == MH_OK,
                          "bootstrap current failure hook");
            }
            check(MH_EnableHook(MH_ALL_HOOKS) == MH_OK, "enable hooks");
        }
        installMsaaHooks(stub, settings);
        const auto p = requestedGamePfd();
        Window previous;
        const int baseline = ChoosePixelFormat(previous.dc, &p);
        check(baseline > 0 && SetPixelFormat(previous.dc, baseline, &p), "baseline format");
        previous.rc = wglCreateContext(previous.dc);
        check(previous.rc && wglMakeCurrent(previous.dc, previous.rc), "baseline context");
        const auto renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const bool generic = renderer && std::string(renderer) == "GDI Generic";
        for (int recreation = 0; recreation < 2; ++recreation)
        {
            Window target;
            check(wglMakeCurrent(previous.dc, previous.rc) != FALSE, "restore baseline");
            int format = 0;
            auto arb = reinterpret_cast<Arb>(proc("wglChoosePixelFormatARB"));
            if (cold)
                check(wglMakeCurrent(nullptr, nullptr) != FALSE, "release previous context");
            if (advanced && arb)
            {
                const int a[]{0x2001, 1,  0x2010, 1, 0x2011, 1, 0x2013, 0x202b, 0x2014, 24,
                              0x2022, depth24Only ? 32 : 24, 0x2023, 8, 0x201d, depth24Only ? 64 : 0,
                              0x2041, 1, 0x2042, 16, 0};
                const FLOAT f[]{FLOAT(0x201b), 0, FLOAT(0x2042), 16, 0};
                UINT count = 0;
                check(arb(target.dc, a, f, 1, &format, &count) && count == 1, "advanced format");
            }
            else
                format = mode == L"set_only" ? ChoosePixelFormat(target.dc, &p) : choose(target.dc, &p);
            check(wglGetCurrentContext() == (cold ? nullptr : previous.rc) &&
                      wglGetCurrentDC() == (cold ? nullptr : previous.dc),
                  "bootstrap leaked current context/DC");
            check(format > 0 && GetPixelFormat(target.dc) == 0, "format selected before real SetPixelFormat");
            if (off)
                check(format == ChoosePixelFormat(target.dc, &p), "off changed selection");
            check(set(target.dc, format, &p) != FALSE, "real SetPixelFormat");
            if (mode != L"set_only")
                check(GetPixelFormat(target.dc) == format, "SetPixelFormat changed chosen index");
            else
                format = GetPixelFormat(target.dc);
            PIXELFORMATDESCRIPTOR actual{};
            check(DescribePixelFormat(target.dc, format, sizeof(actual), &actual) > 0, "describe");
            check(actual.cStencilBits >= 8 && actual.cDepthBits >= 24 && actual.cColorBits >= 24 &&
                      (actual.dwFlags & PFD_DOUBLEBUFFER),
                  "framebuffer attributes lost");
            target.rc = wglCreateContext(target.dc);
            check(target.rc && wglMakeCurrent(target.dc, target.rc), "create real context");
            if (!generic)
                glDisable(0x809d); // GL defaults to enabled; prove the hook enables it.
            check(current(target.dc, target.rc) != FALSE, "real context current");
            if (!generic)
            {
                GLint buffers = 0, samples = 0;
                glGetIntegerv(0x80a8, &buffers);
                glGetIntegerv(0x80a9, &samples);
                std::cout << "recreation=" << recreation << " format=" << format << " samples=" << samples
                          << " stencil=" << int(actual.cStencilBits) << "\n";
                if (!off)
                {
                    const int expected = badStencil ? 0 : (limited ? maxSamples : 4);
                    check(samples == expected && buffers == (expected ? 1 : 0),
                          "MSAA not allocated/fallen back on hardware");
                    check(bool(glIsEnabled(0x809d)) == bool(expected), "MSAA enable does not match actual format");
                }
            }
            else
                std::cout << "MSAA hardware checks skipped on GDI Generic\n";
        }
        logClose();
        std::ifstream log(path);
        const std::string text{std::istreambuf_iterator<char>(log), {}};
        log.close();
        check(text.find("MSAA renderer IAT hooks ready") != std::string::npos, "hooks not installed");
        if (settings.tracePixelFormat)
        {
            check(text.find("PixelFormat requested") != std::string::npos, "request trace missing");
            check(text.find("PixelFormat chosen") != std::string::npos, "chosen format trace missing");
        }
        if (!off && !generic && mode != L"no_arb" && mode != L"make_failure")
        {
            check(text.find("MSAA requested=4 selected=") != std::string::npos, "selection log missing");
            check(text.find("rgba=") != std::string::npos && text.find("reason=") != std::string::npos,
                  "actual attributes and selection reason missing");
            check(text.find("MSAA context requested=4") != std::string::npos, "actual context samples missing");
        }
        if (mode == L"quiet_none")
            check(text.find("could not be honoured") != std::string::npos, "AA failure hidden without tracing");
        if (depth24Only && !generic)
            check(text.find("fallback depth<=24/no accumulation") != std::string::npos, "compatibility reason missing");
        if (limited)
            check(text.find("fallback") != std::string::npos, "fallback not logged");
        if (mode == L"none" && !generic)
            check(text.find("MSAA requested=4 selected=0") != std::string::npos,
                  "0x ARB sanity attempt did not find a format");
        std::filesystem::remove(path);
        FreeLibrary(stub);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
