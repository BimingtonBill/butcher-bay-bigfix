#include <windows.h>
#include "mode_stub.h"
namespace {
bool rejectCreation = false;
int focusLosses = 0;
int focusGains = 0;
int pendingMode = -1;
LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE && rejectCreation) return FALSE;
    if (msg == WM_KILLFOCUS) {
        ++focusLosses;
        pendingMode = 0; // Model the RE'd deferred switch, not only minimisation.
        ShowWindow(window, SW_MINIMIZE);
    }
    if (msg == WM_SETFOCUS) ++focusGains;
    return DefWindowProcW(window, msg, w, l);
}
}
extern "C" __declspec(dllexport) HWND bigfixCreateWindow(BOOL wide, HWND parent) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSA a{}; a.style = CS_OWNDC; a.hInstance = instance;
    a.lpfnWndProc = windowProc; a.lpszClassName = "MOS Win32/OpenGL";
    RegisterClassA(&a);
    WNDCLASSW w{}; w.style = CS_OWNDC; w.hInstance = instance;
    w.lpfnWndProc = windowProc; w.lpszClassName = L"MOS Win32/OpenGL W";
    RegisterClassW(&w);
    const DWORD style = parent ? WS_CHILD | WS_CAPTION : 0x16cf0000;
    return wide ? CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, w.lpszClassName,
                                 L"BigFix window harness", style, 100, 100, 640, 480, parent, nullptr, instance, nullptr)
                : CreateWindowExA(WS_EX_APPWINDOW | WS_EX_TOPMOST, a.lpszClassName,
                                 "BigFix window harness", style, 100, 100, 640, 480, parent, nullptr, instance, nullptr);
}
extern "C" __declspec(dllexport) BOOL bigfixPosition(HWND window) {
    return SetWindowPos(window, HWND_TOPMOST, 50, 50, 800, 600, SWP_NOACTIVATE | SWP_NOZORDER);
}
extern "C" __declspec(dllexport) int bigfixFocusLosses() { return focusLosses; }
extern "C" __declspec(dllexport) int bigfixFocusGains() { return focusGains; }
extern "C" __declspec(dllexport) int bigfixPendingMode() { return pendingMode; }
extern "C" __declspec(dllexport) void bigfixResetPendingMode() { pendingMode = -1; }
extern "C" __declspec(dllexport) BOOL bigfixEnumA(DWORD i, DEVMODEA* mode) { return EnumDisplaySettingsA(nullptr, i, mode); }
extern "C" __declspec(dllexport) BOOL bigfixEnumW(DWORD i, DEVMODEW* mode) { return EnumDisplaySettingsW(nullptr, i, mode); }
extern "C" __declspec(dllexport) LONG bigfixChangeA(DEVMODEA* mode) { return ChangeDisplaySettingsA(mode, CDS_TEST); }
extern "C" __declspec(dllexport) LONG bigfixChangeW(DEVMODEW* mode) { return ChangeDisplaySettingsW(mode, CDS_TEST); }
extern "C" __declspec(dllexport) LONG bigfixChangeExA(DEVMODEA* mode) { return ChangeDisplaySettingsExA(nullptr, mode, nullptr, CDS_TEST, nullptr); }
extern "C" __declspec(dllexport) LONG bigfixChangeExW(DEVMODEW* mode) { return ChangeDisplaySettingsExW(nullptr, mode, nullptr, CDS_TEST, nullptr); }
extern "C" __declspec(dllexport) void bigfixSelectMode(int* values) {
    CDisplayContext context;
    context.CDisplayContext::ModeList_Find(640, 480, 16, 85);
    bigfixReadModeRequest(values);
}

extern "C" __declspec(dllexport) void bigfixRejectCreation(BOOL reject) { rejectCreation = reject != FALSE; }
