#include "capture.h"
#include "log.h"
#include <MinHook.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class T> T proc(HMODULE module, const char* name) {
    const auto value = GetProcAddress(module, name);
    check(value != nullptr, name);
    return reinterpret_cast<T>(value);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) return 2;
        const auto dir = std::filesystem::temp_directory_path() /
            (L"BigFix-first-frame-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(dir);
        const auto log = dir / L"BigFix.log";
        logInit(log);
        const auto gl = LoadLibraryW(argv[1]);
        check(gl != nullptr, "GL stub load");
        const auto swap = proc<BOOL (WINAPI*)(HDC)>(gl, "SwapBuffers");
        const auto context = proc<void (*)(ULONG_PTR)>(gl, "bigfixSetContext");
        const auto result = proc<void (*)(BOOL)>(gl, "bigfixSetSwapResult");
        const auto viewport = proc<void (*)(int, int)>(gl, "bigfixSetViewport");
        Settings settings;
        settings.capturePng = settings.dumpFrameTimes = false; // Readiness is independent of optional capture.
        check(MH_Initialize() == MH_OK, "MinHook init");
        installCaptureHooks(gl, settings, dir);
        check(MH_EnableHook(MH_ALL_HOOKS) == MH_OK, "enable swap hook");
        const auto text = [&] { std::ifstream in(log); return std::string(std::istreambuf_iterator<char>(in), {}); };
        result(FALSE);
        check(!swap(nullptr) && GetLastError() == 1234, "failed swap forwarding");
        check(text().find("First frame presented:") == std::string::npos, "failed swap claimed a presented frame");
        result(TRUE); context(0);
        check(swap(nullptr) && GetLastError() == 1234, "no-context swap forwarding");
        check(text().find("First frame presented:") == std::string::npos, "no-context swap claimed a frame");
        context(1); viewport(0, 0); swap(nullptr);
        check(text().find("First frame presented:") == std::string::npos, "empty viewport claimed a frame");
        viewport(320, 200);
        check(swap(nullptr) && GetLastError() == 1234, "successful swap forwarding");
        viewport(640, 480); swap(nullptr);
        const auto output = text();
        const auto first = output.find("First frame presented: 320x200");
        check(first != std::string::npos, "first successful viewport missing");
        check(output.find("First frame presented:", first + 1) == std::string::npos, "readiness logged more than once");
        MH_Uninitialize(); logClose();
        std::filesystem::remove_all(dir);
        std::cout << "First frame: successful presentation, valid context/viewport, once, LastError PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
