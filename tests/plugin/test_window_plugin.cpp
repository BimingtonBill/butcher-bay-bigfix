#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <regex>
#include <string>
#include <vector>
#ifdef BIGFIX_WINDOW_HARNESS
#include "window_mode.h"
#include "log.h"
void configureWindowPolicyFixture(std::wstring_view scenario);
bool windowPolicyFixtureMatches(bool applied);
#endif

namespace {
bool rendererImportsOriginal(HMODULE renderer) {
    const auto* base = reinterpret_cast<const unsigned char*>(renderer);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry) {
        if (!entry->OriginalFirstThunk) continue;
        const auto dependency = GetModuleHandleA(reinterpret_cast<const char*>(base + entry->Name));
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry->OriginalFirstThunk);
        auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            if (slots->u1.Function != reinterpret_cast<ULONG_PTR>(GetProcAddress(dependency, name))) return false;
        }
    }
    return true;
}
void check(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
template<class T> T proc(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    check(address != nullptr, name);
    return reinterpret_cast<T>(address);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
#ifdef BIGFIX_WINDOW_HARNESS
        if (argc != 6) return 2;
        const std::wstring_view scenario(argv[5]);
#else
        if (argc != 5) return 2;
#endif
        const bool early = std::wstring(argv[3]) == L"early";
        const std::wstring policy(argv[4]);
        const bool requestedBorderless = policy == L"borderless";
#ifdef BIGFIX_WINDOW_HARNESS
        const bool borderless = requestedBorderless && scenario == L"success";
#else
        const bool borderless = false; // Unknown real-ASI fixture must retain game behavior.
#endif
        const char* policyName = requestedBorderless ? "borderless" : policy == L"fullscreen" ? "fullscreen" : "windowed";
        const auto dir = std::filesystem::absolute(argv[1]).parent_path() /
                         (L"window-plugin-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(dir);
        const auto plugin = dir / L"BigFix.asi";
        std::filesystem::copy_file(argv[1], plugin);
        { std::ofstream ini(dir / L"BigFix.ini");
          ini << "[BigFix]\nTextureSizeFix=0\nDisplayModeFilter=0\nWindowMode=" << policyName << '\n'; }
        const auto originalDpi = GetThreadDpiAwarenessContext();
        HMODULE renderer = early ? LoadLibraryW(argv[2]) : nullptr;
#ifdef BIGFIX_WINDOW_HARNESS
        logInit(dir / L"BigFix.log");
#else
        check(LoadLibraryW(plugin.c_str()) != nullptr, "load ASI");
#endif
        // Reacquire after DllMain: deferred fingerprint work runs on this loader return.
        renderer = LoadLibraryW(argv[2]);
        check(renderer != nullptr, "load renderer harness");
#ifdef BIGFIX_WINDOW_HARNESS
        configureWindowPolicyFixture(scenario);
        Settings settings;
        settings.windowMode = WindowMode::Borderless;
        installWindowModeHooks(renderer, settings,
            scenario == L"fingerprint" ? "mismatch" : "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348",
            "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500");
        check(windowPolicyFixtureMatches(borderless), "selection fixture bytes not applied/preserved/rolled back");
#endif
        if (requestedBorderless && !borderless) {
            check(rendererImportsOriginal(renderer), "failed prerequisites changed renderer IAT");
            check(AreDpiAwarenessContextsEqual(originalDpi, GetThreadDpiAwarenessContext()), "failed prerequisites changed DPI awareness");
        }
        const auto create = proc<HWND (*)(BOOL, HWND)>(renderer, "bigfixCreateWindow");
        const auto position = proc<BOOL (*)(HWND)>(renderer, "bigfixPosition");
        const auto losses = proc<int (*)()>(renderer, "bigfixFocusLosses");
        const auto gains = proc<int (*)()>(renderer, "bigfixFocusGains");
        DEVMODEW desktop{}; desktop.dmSize = sizeof(desktop);
        check(EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktop), "desktop snapshot");
        int request[4]{};
        proc<void (*)(int*)>(renderer, "bigfixSelectMode")(request);
        check(request[0] == (borderless ? int(desktop.dmPelsWidth) : 640) &&
              request[1] == (borderless ? int(desktop.dmPelsHeight) : 480) &&
              request[2] == (borderless ? 0x40 : 16) &&
              request[3] == (borderless ? int(desktop.dmDisplayFrequency) : 85), "native render request not enforced at selection boundary");
        if (borderless) {
            const auto reject = proc<void (*)(BOOL)>(renderer, "bigfixRejectCreation");
            reject(TRUE);
            check(create(FALSE, nullptr) == nullptr, "rejected window creation must fail");
            reject(FALSE);
            std::ifstream log(dir / L"BigFix.log");
            const std::string text{std::istreambuf_iterator<char>(log), {}};
            check(text.find("Borderless window style:") == std::string::npos, "failed creation claimed borderless readiness");
            check(text.find("Borderless CreateWindowEx failed") != std::string::npos, "missing window creation failure diagnostic");
        }
        for (BOOL wide : {FALSE, TRUE}) {
            HWND window = create(wide, nullptr);
            check(window != nullptr, "create real window");
            const DWORD style = DWORD(GetWindowLongPtrW(window, GWL_STYLE));
            const DWORD exstyle = DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE));
            check((style & WS_CAPTION) == (borderless ? 0u : DWORD(WS_CAPTION)), "real CreateWindowEx style unchanged");
            check(bool(style & WS_POPUP) == borderless, "popup policy");
            check(bool(exstyle & WS_EX_TOPMOST) == !borderless, "topmost policy");
            if (borderless) {
                check(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2), "window not PMv2 aware");
                MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
                check(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor), "monitor info");
                RECT rect{}; GetWindowRect(window, &rect);
                check(EqualRect(&rect, &monitor.rcMonitor), "window does not cover physical monitor");
                check(position(window), "renderer SetWindowPos");
                GetWindowRect(window, &rect);
                check(EqualRect(&rect, &monitor.rcMonitor), "SetWindowPos escaped monitor rect");
                check(!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST), "SetWindowPos forced topmost");
            }
            HWND child = create(wide, window);
            check(child && (GetWindowLongPtrW(child, GWL_STYLE) & WS_CAPTION), "child window incorrectly rewritten");
            HWND auxiliary = create(wide, nullptr);
            check(auxiliary && (GetWindowLongPtrW(auxiliary, GWL_STYLE) & WS_CAPTION), "second top-level window incorrectly rewritten");
            DestroyWindow(auxiliary); DestroyWindow(child);
            // Auxiliary windows deliberately retain their original focus path.
            // Start the main-window probe with an empty deferred-mode queue.
            proc<void (*)()>(renderer, "bigfixResetPendingMode")();
            const int before = losses();
            SendMessageW(window, WM_KILLFOCUS, 0, 0);
            check(losses() == before + (borderless ? 0 : 1), "game focus-loss branch was not bypassed/preserved");
            check(bool(IsIconic(window)) == !borderless, "focus loss minimisation policy");
            if (borderless) {
                check(proc<int (*)()>(renderer, "bigfixPendingMode")() == -1, "focus loss queued a mode change");
                SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                check(!IsIconic(window), "system minimise bypass failed");
            }
            const int gained = gains();
            SendMessageW(window, WM_SETFOCUS, 0, 0);
            check(gains() == gained + 1, "focus regain input handling lost");
            DestroyWindow(window); // Next loop proves destruction/recreation, including W path.
        }
        DEVMODEA a{}; a.dmSize = sizeof(a);
        a.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT; a.dmPelsWidth = a.dmPelsHeight = 1;
        DEVMODEW w{}; w.dmSize = sizeof(w);
        w.dmFields = a.dmFields; w.dmPelsWidth = w.dmPelsHeight = 1;
        for (const auto name : {"bigfixChangeA", "bigfixChangeExA"})
            check(proc<LONG (*)(DEVMODEA*)>(renderer, name)(&a) ==
                  (borderless ? DISP_CHANGE_SUCCESSFUL : ChangeDisplaySettingsA(&a, CDS_TEST)), "A display suppression/passthrough");
        for (const auto name : {"bigfixChangeW", "bigfixChangeExW"})
            check(proc<LONG (*)(DEVMODEW*)>(renderer, name)(&w) ==
                  (borderless ? DISP_CHANGE_SUCCESSFUL : ChangeDisplaySettingsW(&w, CDS_TEST)), "W display suppression/passthrough");
        DEVMODEA enumerated{}; enumerated.dmSize = sizeof(enumerated);
        const auto enumerate = proc<BOOL (*)(DWORD, DEVMODEA*)>(renderer, "bigfixEnumA");
        check(enumerate(0, &enumerated), "mode enumeration");
        if (borderless) {
            check(enumerated.dmPelsWidth == desktop.dmPelsWidth && enumerated.dmPelsHeight == desktop.dmPelsHeight, "native mode not first");
            check(!enumerate(1, &enumerated), "borderless exposed a smaller render mode");
            check(enumerate(ENUM_CURRENT_SETTINGS, &enumerated), "special enumeration query broken");
            DEVMODEW wide{}; wide.dmSize = sizeof(wide);
            const auto enumWide = proc<BOOL (*)(DWORD, DEVMODEW*)>(renderer, "bigfixEnumW");
            check(enumWide(0, &wide) && wide.dmPelsWidth == desktop.dmPelsWidth && wide.dmPelsHeight == desktop.dmPelsHeight,
                  "native wide enumeration");
            check(!enumWide(1, &wide), "wide enumeration exposed a smaller render mode");
            std::ifstream log(dir / L"BigFix.log");
            const std::string text{std::istreambuf_iterator<char>(log), {}};
            const std::regex modes(R"(Display modes: \d+ -> (\d+) \(cap=512\), desktop=)");
            std::smatch match;
            check(std::regex_search(text, match, modes) && match[1] == "1", "missing native-only display-mode diagnostic");
            check(std::distance(std::sregex_iterator(text.begin(), text.end(), modes), std::sregex_iterator{}) == 1,
                  "display-mode diagnostic must appear once across A/W enumeration");
            check(text.find("window style") != std::string::npos && text.find("suppressed mode change: 1x1") != std::string::npos, "missing decision diagnostics");
        }
        if (requestedBorderless && !borderless) {
            std::ifstream log(dir / L"BigFix.log");
            const std::string text{std::istreambuf_iterator<char>(log), {}};
            check(text.find("Borderless unavailable:") != std::string::npos &&
                  text.find("; using game's own mode") != std::string::npos, "missing fail-closed borderless diagnostic");
#ifdef BIGFIX_WINDOW_HARNESS
            const char* reason = scenario == L"bytes" ? "MSystem selection byte mismatch" :
                scenario == L"address" ? "MSystem selection patch address unavailable" :
                scenario == L"write_failure" ? "selection patch write/protection/cache failure" :
                "EU 1.1 renderer/system fingerprints required";
            check(text.find(reason) != std::string::npos, "missing prerequisite failure reason");
#endif
            check(text.find("Borderless renderer IAT hooks ready:") == std::string::npos &&
                  text.find("Borderless window style:") == std::string::npos, "unverified borderless hooks were committed");
        }
        std::cout << "Real renderer IAT CreateWindowExA/W, geometry, DPI, focus, native selection, display A/W/Ex: PASS ("
                  << policyName << ", " << (early ? "early" : "late") << ")\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
