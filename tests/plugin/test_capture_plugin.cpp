#include "config.h"
#include <fstream>
#include <iostream>
#include <string>

// Exercise the installed GDI detour and background CSV writer without a game,
// window, OpenGL context or GPU. Invalid HDCs are still forwarded to Windows.
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) return 2;
        const auto dir = std::filesystem::absolute(argv[1]).parent_path() /
            (L"capture-plugin-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        const auto asi = dir / L"BigFix.asi";
        std::filesystem::copy_file(argv[1], asi);
        {
            std::ofstream ini(dir / L"BigFix.ini");
            ini << "[BigFix]\nTextureSizeFix=0\nCapturePng=0\nDumpFrameTimes=1\n";
        }
        const auto gdi = LoadLibraryW(L"gdi32.dll");
        if (!gdi) return 2;
        using Swap = BOOL (WINAPI*)(HDC);
        const auto swap = reinterpret_cast<Swap>(GetProcAddress(gdi, "SwapBuffers"));
        if (!swap) return 2;
        SetLastError(0);
        const auto originalResult = swap(nullptr);
        const auto originalError = GetLastError();
        if (!LoadLibraryW(asi.c_str())) return 2;
        // 601 calls produce 600 intervals and the first complete snapshot.
        for (int i = 0; i < 601; ++i) {
            SetLastError(0);
            const auto result = swap(nullptr);
            const auto error = GetLastError();
            if (result != originalResult || error != originalError) {
                std::cerr << "SwapBuffers result/last-error changed\n"; return 1;
            }
        }
        const auto csv = dir / L"frametimes.csv";
        const auto deadline = GetTickCount64() + 5000;
        while (!std::filesystem::exists(csv) && GetTickCount64() < deadline) Sleep(10);
        std::ifstream input(csv);
        std::string line;
        if (!std::getline(input, line) || line != "frame,ms") {
            std::cerr << "Background frame-time CSV missing\n"; return 1;
        }
        size_t count = 0;
        while (std::getline(input, line)) {
            const auto comma = line.find(',');
            if (comma == std::string::npos || std::stoull(line.substr(0, comma)) != ++count ||
                std::stod(line.substr(comma + 1)) < 0) return 1;
        }
        if (count != 600) { std::cerr << "Expected 600 intervals, got " << count << '\n'; return 1; }
        std::cout << "Capture plugin: GDI forwarding and asynchronous 600-interval CSV PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
