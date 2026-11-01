#include "msaa.h"
#include "log.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <gl/GL.h>
#include <mutex>
#include <psapi.h>
#include <stdexcept>

namespace
{
constexpr int sampleBuffers = 0x2041, samplesArb = 0x2042;
using ChooseArb = BOOL(WINAPI *)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
using QueryArb = BOOL(WINAPI *)(HDC, int, int, UINT, const int *, int *);
struct Api
{
    decltype(&wglGetProcAddress) proc = nullptr;
    decltype(&wglCreateContext) create = nullptr;
    decltype(&wglMakeCurrent) current = nullptr;
    decltype(&wglGetCurrentContext) context = nullptr;
    decltype(&wglGetCurrentDC) dc = nullptr;
    decltype(&wglDeleteContext) destroy = nullptr;
    decltype(&glEnable) enable = nullptr;
    decltype(&glGetIntegerv) integer = nullptr;
    decltype(&glGetString) string = nullptr;
} api;
Settings settings;
bool installed = false;
std::mutex installMutex;
// Consumed by SetPixelFormat; bootstrap format selection is fresh for each DC.
struct Pending
{
    HDC dc = nullptr;
    HWND window = nullptr;
    int format = 0;
};
thread_local Pending pending;
thread_local ChooseArb gameChooser = nullptr;
bool valid(PROC p)
{
    const auto n = reinterpret_cast<uintptr_t>(p);
    return n > 3 && n != ~uintptr_t(0);
}
template <class T> std::vector<T> withoutSamples(const T *attributes)
{
    std::vector<T> result;
    size_t i = 0;
    if (attributes)
        for (; i < 512 && attributes[i]; i += 2)
            if (attributes[i] != T(sampleBuffers) && attributes[i] != T(samplesArb))
                result.insert(result.end(), {attributes[i], attributes[i + 1]});
    if (i == 512)
        throw std::runtime_error("MSAA unterminated attribute list");
    result.push_back(0);
    return result;
}
bool traceLine()
{
    static std::atomic<unsigned> lines{0};
    return settings.tracePixelFormat && lines.fetch_add(1, std::memory_order_relaxed) < 2048;
}
void tracePfd(const char *stage, HDC dc, int format, const PIXELFORMATDESCRIPTOR *p)
{
    if (!p || !traceLine())
        return;
    logf(
        "PixelFormat %s dc=%p hwnd=%p format=%d PFD size=%u version=%u flags=%08lx type=%u color=%u rgba=%u/%u/%u/%u "
        "shifts=%u/%u/%u/%u depth=%u stencil=%u accum=%u rgbaAccum=%u/%u/%u/%u aux=%u layer=%u masks=%08lx/%08lx/%08lx",
        stage, dc, WindowFromDC(dc), format, p->nSize, p->nVersion, p->dwFlags, p->iPixelType, p->cColorBits,
        p->cRedBits, p->cGreenBits, p->cBlueBits, p->cAlphaBits, p->cRedShift, p->cGreenShift, p->cBlueShift,
        p->cAlphaShift, p->cDepthBits, p->cStencilBits, p->cAccumBits, p->cAccumRedBits, p->cAccumGreenBits,
        p->cAccumBlueBits, p->cAccumAlphaBits, p->cAuxBuffers, p->iLayerType, p->dwLayerMask, p->dwVisibleMask,
        p->dwDamageMask);
}
GameRequest readRequest(const int *a)
{
    GameRequest r;
    if (a)
        for (size_t i = 0; i < 512 && a[i]; i += 2)
        {
            r.attributes.push_back(a[i]);
            r.attributes.push_back(a[i + 1]);
        }
    r.attributes.push_back(0);
    return r;
}
int attribute(const std::vector<int> &attributes, int key, int missing = 0)
{
    for (size_t i = 0; i + 1 < attributes.size() && attributes[i]; i += 2)
        if (attributes[i] == key)
            return attributes[i + 1];
    return missing;
}
// The renderer needs depth/stencil but does not use accumulation buffers.
// Some drivers expose 32-bit depth only without MSAA; 24-bit depth retains
// the precision of the known working game format. Keep all other constraints.
std::vector<int> compatibleAttributes(const std::vector<int> &attributes)
{
    std::vector<int> result;
    for (size_t i = 0; i + 1 < attributes.size() && attributes[i]; i += 2)
    {
        const auto key = attributes[i];
        if (key >= 0x201d && key <= 0x2021)
            continue; // accumulation total and component sizes
        result.insert(result.end(), {key, key == 0x2022 ? (std::min)(attributes[i + 1], 24) : attributes[i + 1]});
    }
    result.push_back(0);
    return result;
}
void traceRequest(HDC dc, const GameRequest &r, const FLOAT *f)
{
    if (!traceLine())
        return;
    logf("PixelFormat requested ARB dc=%p hwnd=%p", dc, WindowFromDC(dc));
    for (size_t i = 0; i + 1 < r.attributes.size() && r.attributes[i]; i += 2)
        if (traceLine())
            logf("PixelFormat requested int 0x%x=%d", r.attributes[i], r.attributes[i + 1]);
    if (f)
        for (size_t i = 0; i < 512 && f[i]; i += 2)
            if (traceLine())
                logf("PixelFormat requested float 0x%x=%g", int(f[i]), double(f[i + 1]));
}

// Lazy, disposable hidden context: never called by the installer/DllMain and
// never calls SetPixelFormat on the real window. Use its monitor/adapter.
struct Bootstrap
{
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
    HDC previousDc = api.dc();
    HGLRC previous = api.context();
    bool bound = false;
    ChooseArb choose = nullptr;
    QueryArb query = nullptr;
    bool init(HDC target)
    {
        static std::once_flag registered;
        static HINSTANCE instance = nullptr;
        static bool ready = false;
        std::call_once(registered, [] {
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&installMsaaHooks), &instance);
            WNDCLASSW c{};
            c.style = CS_OWNDC;
            c.lpfnWndProc = DefWindowProcW;
            c.hInstance = instance;
            c.lpszClassName = L"BigFixMsaaBootstrap";
            ready = RegisterClassW(&c) != 0;
        });
        if (!ready)
            return false;
        RECT rect{};
        GetWindowRect(WindowFromDC(target), &rect);
        window = CreateWindowExW(0, L"BigFixMsaaBootstrap", L"", WS_POPUP, rect.left, rect.top, 1, 1, nullptr, nullptr,
                                 instance, nullptr);
        if (!window || !(dc = GetDC(window)))
            return false;
        PIXELFORMATDESCRIPTOR p{};
        p.nSize = sizeof(p);
        p.nVersion = 1;
        p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
        p.iPixelType = PFD_TYPE_RGBA;
        p.cColorBits = 24;
        const int format = ChoosePixelFormat(dc, &p);
        if (!format || !SetPixelFormat(dc, format, &p) || !(rc = api.create(dc)))
            return false;
        // A failed wglMakeCurrent can unbind the previous context too.
        bound = true;
        if (!api.current(dc, rc))
            return false;
        auto c = api.proc("wglChoosePixelFormatARB"), q = api.proc("wglGetPixelFormatAttribivARB");
        if (valid(c))
            choose = reinterpret_cast<ChooseArb>(c);
        if (valid(q))
            query = reinterpret_cast<QueryArb>(q);
        return choose && query;
    }
    ~Bootstrap()
    {
        if (bound && !api.current(previousDc, previous))
        {
            logLineNoThrow("MSAA bootstrap failed to restore previous context");
            api.current(nullptr, nullptr);
        }
        if (rc)
            api.destroy(rc);
        if (dc)
            ReleaseDC(window, dc);
        if (window)
            DestroyWindow(window);
    }
};
MsaaFormat select(HDC dc, const GameRequest &request, const FLOAT *floats, int original)
{
    Bootstrap bootstrap;
    if (!bootstrap.init(dc))
    {
        logf("MSAA request %dx could not be honoured: bootstrap/ARB pixel formats unavailable; "
             "fallback to original format=%d (samples unverified)",
             settings.antiAliasing, original);
        return {original, 0};
    }
    // Sample attributes can be expressed in either WGL list. Only our integer
    // list owns them; preserve all other floating-point constraints.
    const auto floatAttributes = withoutSamples(floats);
    bool verified = false;
    const auto result = chooseMsaaFormat(request, settings.antiAliasing, original, [&](const std::vector<int> &attrs) {
        const int desired = attribute(attrs, samplesArb);
        const char *policy = attrs == buildAttribs(request, desired) ? "original constraints" :
                                                                    "fallback depth<=24/no accumulation";
        int formats[256]{};
        UINT count = 0;
        BOOL ok = bootstrap.choose(dc, attrs.data(), floats ? floatAttributes.data() : nullptr, 256, formats, &count);
        if (desired == 0 && (!ok || count == 0))
        {
            // Drivers without ARB_multisample reject even SAMPLE_BUFFERS=0.
            // Retain every non-AA constraint, retry without extension tokens.
            const auto plain = withoutSamples(attrs.data());
            count = 0;
            ok = bootstrap.choose(dc, plain.data(), floats ? floatAttributes.data() : nullptr, 256, formats, &count);
        }
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        const int keys[]{sampleBuffers, samplesArb, 0x2014, 0x2022, 0x2023, 0x2011,
                         0x2015, 0x2017, 0x2019, 0x201b};
        unsigned queryFailures = 0, sampleMismatch = 0, framebufferMismatch = 0;
        if (ok)
            for (UINT i = 0; i < (std::min)(count, 256u); ++i)
            {
                int v[10]{};
                if (formats[i] <= 0)
                    continue;
                if (!bootstrap.query(dc, formats[i], 0, 10, keys, v))
                {
                    if (desired > 0 || !bootstrap.query(dc, formats[i], 0, 8, keys + 2, v + 2))
                    {
                        ++queryFailures;
                        continue;
                    }
                    v[0] = v[1] = 0; // no multisample query on a non-multisample driver
                }
                if ((desired > 0 && (v[0] != 1 || v[1] != desired)) || (desired == 0 && v[0] != 0))
                {
                    ++sampleMismatch;
                    continue;
                }
                const int doubleBuffer = attribute(attrs, 0x2011, -1);
                bool enough = doubleBuffer < 0 || v[5] == doubleBuffer;
                for (int j : {2, 3, 4, 6, 7, 8, 9})
                    enough = enough && v[j] >= attribute(attrs, keys[j]);
                if (!enough)
                {
                    ++framebufferMismatch;
                    continue;
                }
                logf("MSAA requested=%d selected=%d format=%d dc=%p reason=%s; sampleBuffers=%d "
                     "colorRGB=%d rgba=%d/%d/%d/%d depth=%d stencil=%d doubleBuffer=%d",
                     settings.antiAliasing, v[1], formats[i], dc, policy, v[0], v[2], v[6], v[7], v[8], v[9],
                     v[3], v[4], v[5]);
                if (traceLine())
                    logf("PixelFormat chosen ARB format=%d samples=%d", formats[i], v[1]);
                verified = true;
                return formats[i];
            }
        logf("MSAA %dx unavailable: %s (BOOL=%d count=%u queryFailures=%u sampleMismatch=%u "
             "framebufferMismatch=%u error=%lu); fallback to compatible constraints, lower samples or game format",
             desired, policy, ok, count, queryFailures, sampleMismatch, framebufferMismatch, error);
        return 0;
    });
    if (result.samples != settings.antiAliasing)
        logf("MSAA request %dx could not be honoured; fallback selected=%dx format=%d%s", settings.antiAliasing,
             result.samples, result.format, verified ? "" : " (original format; samples unverified)");
    return result;
}
MsaaFormat selectLegacy(HDC dc, int original)
{
    // ChoosePixelFormat treats the game's PFD as a best-match request. WGL
    // attributes are constraints: translating depth=32 or a missing window flag
    // literally can reject every format, even without multisampling.
    PIXELFORMATDESCRIPTOR effective{};
    if (original <= 0 || !DescribePixelFormat(dc, original, sizeof(effective), &effective))
    {
        logf("MSAA request %dx could not be honoured: effective pixel format unavailable; "
             "fallback to original format=%d (samples unverified)", settings.antiAliasing, original);
        return {original, 0};
    }
    tracePfd("effective legacy", dc, original, &effective);
    return select(dc, gameRequest(effective), nullptr, original);
}
int WINAPI chooseHook(HDC dc, const PIXELFORMATDESCRIPTOR *p) noexcept
{
    const int original = ChoosePixelFormat(dc, p);
    const DWORD error = GetLastError();
    int chosen = original;
    try
    {
        tracePfd("requested ChoosePixelFormat", dc, original, p);
        if (settings.antiAliasing > 0 && p && !GetPixelFormat(dc))
            chosen = selectLegacy(dc, original).format;
        pending = {dc, WindowFromDC(dc), chosen};
    }
    catch (...)
    {
        logLineNoThrow("MSAA chooser failed; using original pixel format");
    }
    SetLastError(error);
    return chosen;
}
BOOL WINAPI setHook(HDC dc, int format, const PIXELFORMATDESCRIPTOR *p) noexcept
{
    PIXELFORMATDESCRIPTOR actual{};
    const auto originalP = p;
    try
    {
        tracePfd("requested SetPixelFormat", dc, format, p);
        const bool selected = pending.dc == dc && pending.window == WindowFromDC(dc) && pending.format == format;
        pending = {};
        if (settings.antiAliasing > 0 && !GetPixelFormat(dc))
        {
            if (!selected && p)
                format = selectLegacy(dc, format).format;
            if (DescribePixelFormat(dc, format, sizeof(actual), &actual))
                p = &actual;
        }
    }
    catch (...)
    {
        p = originalP;
        logLineNoThrow("MSAA SetPixelFormat preparation failed; keeping game descriptor");
    }
    // Exactly one call on the real window, even if it fails.
    const BOOL result = SetPixelFormat(dc, format, p);
    const DWORD error = GetLastError();
    try
    {
        if (!result && settings.antiAliasing > 0)
            logf("MSAA request %dx could not be honoured: SetPixelFormat failed format=%d error=%lu",
                 settings.antiAliasing, format, error);
        if (DescribePixelFormat(dc, format, sizeof(actual), &actual))
            tracePfd("chosen", dc, format, &actual);
        if (traceLine())
            logf("PixelFormat SetPixelFormat dc=%p format=%d BOOL=%d actual=%d error=%lu", dc, format, result,
                 GetPixelFormat(dc), error);
    }
    catch (...)
    {
        logLineNoThrow("MSAA format trace failed");
    }
    SetLastError(error);
    return result;
}
BOOL WINAPI arbHook(HDC dc, const int *ints, const FLOAT *floats, UINT max, int *formats, UINT *count) noexcept
{
    // The renderer may call its saved chooser with the old context unbound.
    // Select through a fresh bootstrap first; retain exactly the game's own
    // resolved entry point only for unchanged/off and failure forwarding.
    auto original = gameChooser;
    if (api.context())
    {
        const auto proc = api.proc("wglChoosePixelFormatARB");
        if (valid(proc))
            original = reinterpret_cast<ChooseArb>(proc);
    }
    try
    {
        const auto request = readRequest(ints);
        traceRequest(dc, request, floats);
        if (settings.antiAliasing > 0 && max && formats && count && !GetPixelFormat(dc))
        {
            const auto result = select(dc, request, floats, 0);
            if (result.format > 0)
            {
                formats[0] = result.format;
                *count = 1;
                pending = {dc, WindowFromDC(dc), result.format};
                return TRUE;
            }
            logLineNoThrow("MSAA ARB formats exhausted; fallback to game chooser without sample attributes");
            const auto plain = withoutSamples(request.attributes.data());
            const auto plainFloats = withoutSamples(floats);
            if (original && original(dc, plain.data(), floats ? plainFloats.data() : nullptr, max, formats, count) &&
                *count)
                return TRUE;
        }
    }
    catch (...)
    {
        logLineNoThrow("MSAA ARB chooser failed; using original game request");
    }
    return original ? original(dc, ints, floats, max, formats, count) : FALSE;
}
PROC WINAPI procHook(LPCSTR name) noexcept
{
    const auto result = api.proc(name);
    if (name && !std::strcmp(name, "wglChoosePixelFormatARB") && valid(result))
    {
        gameChooser = reinterpret_cast<ChooseArb>(result);
        return reinterpret_cast<PROC>(&arbHook);
    }
    return result;
}
BOOL WINAPI currentHook(HDC dc, HGLRC rc) noexcept
{
    // Avoid per-frame logging when the engine rebinds the same context.
    thread_local HGLRC reportedContext = nullptr;
    thread_local HDC reportedDc = nullptr;
    const BOOL result = api.current(dc, rc);
    const DWORD error = GetLastError();
    try
    {
        if (result && rc)
        {
            const bool report = settings.antiAliasing > 0 && (rc != reportedContext || dc != reportedDc);
            const auto renderer = reinterpret_cast<const char *>(api.string(GL_RENDERER));
            // Software OpenGL 1.1 has no multisample enums: do not inject errors.
            if (renderer && std::strcmp(renderer, "GDI Generic"))
            {
                GLint buffers = 0, samples = 0;
                api.integer(0x80a8, &buffers);
                api.integer(0x80a9, &samples);
                if (settings.antiAliasing > 0 && buffers > 0 && samples > 0)
                    api.enable(0x809d);
                if (report)
                {
                    logf("MSAA context requested=%d actual=%d sampleBuffers=%d format=%d dc=%p rc=%p renderer=%s",
                         settings.antiAliasing, samples, buffers, GetPixelFormat(dc), dc, rc, renderer);
                    if (buffers == 0 || samples != settings.antiAliasing)
                        logf("MSAA request %dx could not be honoured by current context; actual=%dx sampleBuffers=%d",
                             settings.antiAliasing, samples, buffers);
                }
                if (traceLine())
                    logf("PixelFormat context dc=%p rc=%p format=%d sampleBuffers=%d samples=%d MSAA-enable=%d", dc, rc,
                         GetPixelFormat(dc), buffers, samples, settings.antiAliasing > 0 && buffers > 0 && samples > 0);
            }
            else if (report)
                logf("MSAA request %dx could not be honoured/verified: renderer=%s", settings.antiAliasing,
                     renderer ? renderer : "unavailable");
            reportedContext = rc;
            reportedDc = dc;
        }
        else if (result)
        {
            reportedContext = nullptr;
            reportedDc = nullptr;
        }
    }
    catch (...)
    {
        logLineNoThrow("MSAA context trace failed");
    }
    SetLastError(error);
    return result;
}
struct Slot
{
    void **address;
    void *original;
    void *detour;
};
void replace(const Slot &s, bool rollback = false)
{
    DWORD old = 0;
    if (!VirtualProtect(s.address, sizeof(void *), PAGE_READWRITE, &old))
        throw std::runtime_error("MSAA IAT protection");
    const auto previous = InterlockedCompareExchangePointer(s.address, rollback ? s.original : s.detour,
                                                            rollback ? s.detour : s.original);
    DWORD ignored = 0;
    const auto restored = VirtualProtect(s.address, sizeof(void *), old, &ignored);
    if (previous != (rollback ? s.detour : s.original) || !restored)
        throw std::runtime_error("MSAA IAT mismatch/protection restore");
}
} // namespace

GameRequest gameRequest(const PIXELFORMATDESCRIPTOR &p)
{
    GameRequest r;
    const auto add = [&](int k, int v) { r.attributes.insert(r.attributes.end(), {k, v}); };
    add(0x2001, !!(p.dwFlags & PFD_DRAW_TO_WINDOW));
    add(0x2002, !!(p.dwFlags & PFD_DRAW_TO_BITMAP));
    add(0x2003, (p.dwFlags & PFD_GENERIC_FORMAT) ? ((p.dwFlags & PFD_GENERIC_ACCELERATED) ? 0x2026 : 0x2025)
                                               : 0x2027);
    add(0x2010, !!(p.dwFlags & PFD_SUPPORT_OPENGL));
    add(0x200f, !!(p.dwFlags & PFD_SUPPORT_GDI));
    if (!(p.dwFlags & PFD_DOUBLEBUFFER_DONTCARE))
        add(0x2011, !!(p.dwFlags & PFD_DOUBLEBUFFER));
    if (!(p.dwFlags & PFD_STEREO_DONTCARE))
        add(0x2012, !!(p.dwFlags & PFD_STEREO));
    add(0x2013, p.iPixelType == PFD_TYPE_RGBA ? 0x202b : 0x202c);
    // PFD colour totals can include alpha (e.g. the game's RGBA32 format).
    // WGL_COLOR_BITS_ARB explicitly excludes alpha. Prefer the actual RGB
    // component sizes supplied by DescribePixelFormat, keeping alpha separate.
    const int rgb = p.cRedBits + p.cGreenBits + p.cBlueBits;
    add(0x2014, p.iPixelType == PFD_TYPE_RGBA && rgb ? rgb : p.cColorBits);
    add(0x2015, p.cRedBits);
    add(0x2017, p.cGreenBits);
    add(0x2019, p.cBlueBits);
    add(0x201b, p.cAlphaBits);
    // Legacy formats may advertise accumulation buffers that MSAA formats lack.
    // They are not required by the renderer and must not constrain selection.
    if (!(p.dwFlags & PFD_DEPTH_DONTCARE))
        add(0x2022, p.cDepthBits);
    add(0x2023, p.cStencilBits);
    add(0x2024, p.cAuxBuffers);
    if (p.iLayerType == PFD_OVERLAY_PLANE)
        add(0x2008, 1);
    if (p.iLayerType == PFD_UNDERLAY_PLANE)
        add(0x2009, 1);
    r.attributes.push_back(0);
    return r;
}
std::vector<int> buildAttribs(const GameRequest &request, int samples)
{
    std::vector<int> result;
    for (size_t i = 0; i + 1 < request.attributes.size() && request.attributes[i]; i += 2)
        if (request.attributes[i] != sampleBuffers && request.attributes[i] != samplesArb)
            result.insert(result.end(), {request.attributes[i], request.attributes[i + 1]});
    result.insert(result.end(), {sampleBuffers, samples > 0 ? 1 : 0, samplesArb, samples, 0});
    return result;
}
MsaaFormat chooseMsaaFormat(const GameRequest &request, int samples, int original, const PixelFormatChooser &chooser)
{
    if (samples <= 0)
        return {original, 0};
    const GameRequest compatible{compatibleAttributes(request.attributes)};
    for (int n : {8, 4, 2, 0})
        if (n <= samples)
        {
            const int format = chooser(buildAttribs(request, n));
            if (format > 0)
                return {format, n};
            if (compatible.attributes != request.attributes)
            {
                const int fallback = chooser(buildAttribs(compatible, n));
                if (fallback > 0)
                    return {fallback, n};
            }
        }
    return {original, 0};
}

void installMsaaHooks(HMODULE renderer, const Settings &options) noexcept
{
    if (!options.antiAliasing && !options.tracePixelFormat)
        return;
    try
    {
        const std::lock_guard<std::mutex> lock(installMutex);
        if (installed)
            return;
        const auto gl = GetModuleHandleW(L"opengl32.dll");
        if (!gl)
            throw std::runtime_error("MSAA: OpenGL not loaded");
#define RESOLVE(member, name)                                                                                          \
    api.member = reinterpret_cast<decltype(api.member)>(GetProcAddress(gl, name));                                     \
    if (!api.member)                                                                                                   \
    throw std::runtime_error(name " unavailable")
        RESOLVE(proc, "wglGetProcAddress");
        RESOLVE(create, "wglCreateContext");
        RESOLVE(current, "wglMakeCurrent");
        RESOLVE(context, "wglGetCurrentContext");
        RESOLVE(dc, "wglGetCurrentDC");
        RESOLVE(destroy, "wglDeleteContext");
        RESOLVE(enable, "glEnable");
        RESOLVE(integer, "glGetIntegerv");
        RESOLVE(string, "glGetString");
#undef RESOLVE
        MODULEINFO info{};
        if (!K32GetModuleInformation(GetCurrentProcess(), renderer, &info, sizeof(info)))
            throw std::runtime_error("MSAA image unavailable");
        auto *base = reinterpret_cast<unsigned char *>(renderer);
        const size_t size = info.SizeOfImage;
        const auto within = [&](size_t rva, size_t length) { return rva <= size && length <= size - rva; };
        const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        if (!within(0, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            !within(dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
            throw std::runtime_error("MSAA invalid PE");
        const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw std::runtime_error("MSAA invalid PE64");
        const auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress || !within(dir.VirtualAddress, dir.Size))
            throw std::runtime_error("MSAA invalid imports");
        struct Hook
        {
            const char *name;
            void *detour;
            bool found = false;
        };
        Hook hooks[]{{"ChoosePixelFormat", reinterpret_cast<void *>(&chooseHook)},
                     {"SetPixelFormat", reinterpret_cast<void *>(&setHook)},
                     {"wglGetProcAddress", reinterpret_cast<void *>(&procHook)},
                     {"wglMakeCurrent", reinterpret_cast<void *>(&currentHook)}};
        std::vector<Slot> slots;
        const auto stringAt = [&](size_t rva) -> const char * {
            if (!within(rva, 1) || !std::memchr(base + rva, 0, size - rva))
                throw std::runtime_error("MSAA invalid import string");
            return reinterpret_cast<const char *>(base + rva);
        };
        for (size_t offset = dir.VirtualAddress;
             within(offset, sizeof(IMAGE_IMPORT_DESCRIPTOR)) &&
             offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= size_t(dir.VirtualAddress) + dir.Size;
             offset += sizeof(IMAGE_IMPORT_DESCRIPTOR))
        {
            const auto *entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + offset);
            if (!entry->Name)
                break;
            if (!entry->OriginalFirstThunk)
                continue;
            const auto dep = GetModuleHandleA(stringAt(entry->Name));
            if (!dep)
                continue;
            for (size_t i = 0;; ++i)
            {
                const size_t nr = entry->OriginalFirstThunk + i * sizeof(IMAGE_THUNK_DATA64),
                             ar = entry->FirstThunk + i * sizeof(IMAGE_THUNK_DATA64);
                if (!within(nr, 8) || !within(ar, 8))
                    throw std::runtime_error("MSAA invalid import thunk");
                const auto *name = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + nr);
                if (!name->u1.AddressOfData)
                    break;
                if (IMAGE_SNAP_BY_ORDINAL64(name->u1.Ordinal))
                    continue;
                const auto *text = stringAt(size_t(name->u1.AddressOfData) + offsetof(IMAGE_IMPORT_BY_NAME, Name));
                for (auto &h : hooks)
                    if (!std::strcmp(text, h.name))
                    {
                        const auto expected = reinterpret_cast<void *>(GetProcAddress(dep, text));
                        auto **slot = reinterpret_cast<void **>(base + ar);
                        if (!expected || *slot != expected)
                            throw std::runtime_error("MSAA renderer IAT mismatch");
                        slots.push_back({slot, expected, h.detour});
                        h.found = true;
                    }
            }
        }
        for (const auto &h : hooks)
            if (!h.found)
                throw std::runtime_error("MSAA required renderer import missing; no writes");
        settings = options;
        size_t applied = 0;
        try
        {
            for (const auto &s : slots)
            {
                ++applied;
                replace(s);
            }
        }
        catch (...)
        {
            while (applied)
            {
                try
                {
                    replace(slots[--applied], true);
                }
                catch (...)
                {
                    logLineNoThrow("MSAA IAT rollback failed");
                }
            }
            throw;
        }
        installed = true;
        logf("MSAA renderer IAT hooks ready: %zu imports AntiAliasing=%d TracePixelFormat=%d", slots.size(),
             settings.antiAliasing, settings.tracePixelFormat);
    }
    catch (const std::exception &e)
    {
        logLineNoThrow(e.what());
    }
    catch (...)
    {
        logLineNoThrow("MSAA hook installation failed; using game pixel formats");
    }
}
