#include "fingerprint.h"
#include "version.h"
#include <bcrypt.h>
#include <MinHook.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
decltype(&BCryptOpenAlgorithmProvider) originalOpen = nullptr;
unsigned hashInitializations = 0;
NTSTATUS WINAPI countOpen(BCRYPT_ALG_HANDLE* handle, LPCWSTR algorithm, LPCWSTR provider, ULONG flags) {
    ++hashInitializations;
    return originalOpen(handle, algorithm, provider, flags);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || argc > 4) return 2;
        const std::wstring scenario = argc == 4 ? argv[3] : L"normal";
        const bool fallback = scenario != L"normal";
        const bool disabled = scenario == L"disabled";
        if (!LoadLibraryW(argv[2])) return 2; // Already-loaded renderer must also defer fingerprints.
        if (MH_Initialize() != MH_OK ||
            MH_CreateHook(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"bcrypt.dll"), "BCryptOpenAlgorithmProvider")), reinterpret_cast<void*>(&countOpen),
                          reinterpret_cast<void**>(&originalOpen)) != MH_OK || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return 2;
        const auto source = std::filesystem::absolute(argv[1]);
        const auto dir = source.parent_path() /
            (L"plugin-startup-\u00ff '" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir / L"different-cwd");
        const auto plugin = dir / L"BigFix.asi";
        std::filesystem::copy_file(source, plugin);
        { std::ofstream ini(dir / L"BigFix.ini"); ini << "[BigFix]\nTextureSizeFix=0\nCaptureKey=F11\nUnknownTestKey=1\n"; }
        { std::ofstream log(dir / L"BigFix.log"); log << "previous launch\n"; }
        { std::ofstream log(dir / L"BigFix.prev.log"); log << "obsolete launch\n"; }
        auto logPath = dir / L"BigFix.log";
        if (fallback) {
            const auto temp = dir / L"temp";
            std::filesystem::create_directories(temp);
            SetEnvironmentVariableW(L"TEMP", temp.c_str());
            SetEnvironmentVariableW(L"TMP", temp.c_str());
            if (scenario == L"rotation") {
                std::filesystem::remove(dir / L"BigFix.prev.log");
                std::filesystem::create_directory(dir / L"BigFix.prev.log");
            } else {
                std::filesystem::remove(logPath);
                std::filesystem::create_directory(logPath); // Unusable primary log path.
            }
            logPath = temp / L"BigFix.log";
            if (disabled) std::filesystem::create_directory(logPath);
        }
        std::filesystem::current_path(dir / L"different-cwd");
        // Hold an outer loader lock: a queued initialization thread cannot run
        // until after the assertions. LoadLibrary/DllMain recurse on this lock.
        using Lock = LONG (WINAPI*)(ULONG, ULONG*, ULONG_PTR*);
        using Unlock = LONG (WINAPI*)(ULONG, ULONG_PTR);
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto lock = reinterpret_cast<Lock>(GetProcAddress(ntdll, "LdrLockLoaderLock"));
        const auto unlock = reinterpret_cast<Unlock>(GetProcAddress(ntdll, "LdrUnlockLoaderLock"));
        ULONG disposition = 0;
        ULONG_PTR cookie = 0;
        if (!lock || !unlock || lock(0, &disposition, &cookie) < 0) return 2;
        struct Guard { Unlock unlock; ULONG_PTR cookie; void release() { if (cookie) { unlock(0, cookie); cookie = 0; } } ~Guard() { release(); } } guard{unlock, cookie};
        const auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA");
        std::array<unsigned char, 16> before{};
        std::memcpy(before.data(), reinterpret_cast<const void*>(load), before.size());
        // No game is loaded. The ASI stays resident until this test process exits.
        const auto module = LoadLibraryW(plugin.c_str());
        if (!module) { std::cerr << "Cannot load BigFix.asi\n"; return 1; }
        if (std::memcmp(before.data(), reinterpret_cast<const void*>(load), before.size()) == 0) {
            std::cerr << "LoadLibrary hooks are not active when DllMain returns\n";
            return 1;
        }
        if (disabled) {
            std::cout << "Both log paths unusable: startup hooks remain active PASS\n";
            return 0;
        }
        if (hashInitializations != 0) {
            std::cerr << "Hashing/CNG initialization happened in DllMain\n"; return 1;
        }
        if (read(logPath).find("exe sha256=") != std::string::npos) return 1;
        guard.release();
        // A subsequent original loader return dispatches deferred modules.
        const auto kernel = LoadLibraryW(L"kernel32.dll");
        if (!kernel) return 2;
        FreeLibrary(kernel);
        const auto text = read(logPath);
        if (!hashInitializations || text.find("RndrGL.dll sha256=") == std::string::npos ||
            text.find("MSystem.dll sha256=") == std::string::npos) {
            std::cerr << "Deferred renderer fingerprint missing\n"; return 1;
        }
        const auto exe = std::filesystem::absolute(argv[0]);
        for (const auto& expected : {
                 std::string("BigFix ") + kBigFixVersion,
                 std::string("exe=") + exe.u8string(),
                 std::string("exe sha256=") + sha256File(exe),
                 std::string("Windows "), std::string("TextureSizeFix=0"),
                 std::string("FixMiscBugs=1"), std::string("Enable20PlusPlusAnyGpu=1"),
                 std::string("EyeshineFlickerWorkaround=0"), std::string("DisplayModeFilter=1"),
                 std::string("CapturePng=1"), std::string("DumpShaders=0"), std::string("DumpFrameTimes=0"),
                 std::string("WindowMode=borderless"), std::string("CaptureKey=F11"),
                 std::string("settings warning: "), std::string("UnknownTestKey"),
                 std::string("Module watch ready")}) {
            if (text.find(expected) == std::string::npos) {
                std::cerr << "Missing startup diagnostic: " << expected << '\n' << text;
                return 1;
            }
        }
        if (fallback) {
            const auto first = text.substr(0, text.find('\n'));
            if (first.find(logPath.u8string()) == std::string::npos) {
                std::cerr << "First fallback line must identify log path\n"; return 1;
            }
            std::cout << "Log failure: fallback path identified and hooks active PASS\n";
            return 0;
        }
        if (read(dir / L"BigFix.prev.log") != "previous launch\n" ||
            text.find("previous launch") != std::string::npos ||
            std::filesystem::exists(std::filesystem::current_path() / L"BigFix.log") ||
            std::filesystem::exists(std::filesystem::current_path() / L"BigFix.ini")) {
            std::cerr << "Module-relative settings/log or rotation failed\n";
            return 1;
        }
        std::cout << "Plugin startup: hooks active when DllMain returns, module-relative settings/log and header PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
