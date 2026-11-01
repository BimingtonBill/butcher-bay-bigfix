#include "module_watch.h"
#include "patch_table.h"
#include "log.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) return 2;
        const std::wstring mode = argv[2];
        const auto dir = std::filesystem::temp_directory_path() /
            (L"BigFix-logos-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        logInit(dir / L"BigFix.log");
        const auto fixture = LoadLibraryW(argv[1]);
        check(fixture != nullptr, "data-only fixture load");
        const auto& site = skipLogoVideosSite();
        auto* text = reinterpret_cast<uint8_t*>(fixture) + site.rva;
        DWORD previous = 0;
        check(VirtualProtect(text, 256, PAGE_READWRITE, &previous) != 0, "fixture protection");
        std::memcpy(text, site.expected.data(), site.expected.size());
        const char story[] = "*VIDEO_INTRO\0*VIDEO_OUTTRO\0*VIDEO_EYESHINE\0";
        std::memcpy(text + 64, story, sizeof(story));
        if (mode == L"mismatch") text[13] ^= 1;
        std::vector<uint8_t> before(text, text + 256);
        Settings settings;
        settings.skipLogoVideos = mode != L"off";
        settings.videoAspectFix = false;
        configureModuleWatch(settings, dir);
        onModuleLoaded(L"GameWorld_Win64_AMD64.dll", fixture);
        onModuleLoaded(L"GameWorld_Win64_AMD64.dll", fixture); // once-only dispatch
        if (mode == L"on") {
            check(std::strcmp(reinterpret_cast<char*>(text), "cg_rootmenu \"main\"") == 0, "startup not redirected to title");
            check(std::memcmp(text + site.expected.size(), before.data() + site.expected.size(),
                              before.size() - site.expected.size()) == 0, "adjacent/story data changed");
        } else check(std::memcmp(text, before.data(), before.size()) == 0, "off/mismatch wrote bytes");
        logClose();
        std::ifstream input(dir / L"BigFix.log");
        const std::string log{std::istreambuf_iterator<char>(input), {}};
        for (const auto* name : {"vug_new.ogg", "universal_ntsc.ogg", "Tigon.ogg", "logo_mixed.ogg", "Sbz.ogg"}) {
            const std::string message = std::string("Skipped startup video: ") + name;
            const auto first = log.find(message);
            check((first != std::string::npos) == (mode == L"on"), "skip log missing or misleading");
            if (first != std::string::npos)
                check(log.find(message, first + 1) == std::string::npos, "skip logged more than once");
        }
        if (mode == L"mismatch") check(log.find("Mismatch") != std::string::npos, "mismatch not diagnosed");
        input.close();
        FreeLibrary(fixture);
        std::filesystem::remove_all(dir);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
