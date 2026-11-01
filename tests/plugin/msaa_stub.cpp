#include <windows.h>
extern "C"
{
    __declspec(dllexport) int msaaChoose(HDC dc, const PIXELFORMATDESCRIPTOR *p)
    {
        return ChoosePixelFormat(dc, p);
    }
    __declspec(dllexport) BOOL msaaSet(HDC dc, int format, const PIXELFORMATDESCRIPTOR *p)
    {
        return SetPixelFormat(dc, format, p);
    }
    __declspec(dllexport) BOOL msaaCurrent(HDC dc, HGLRC rc)
    {
        return wglMakeCurrent(dc, rc);
    }
    __declspec(dllexport) PROC msaaProc(const char *name)
    {
        return wglGetProcAddress(name);
    }
}
