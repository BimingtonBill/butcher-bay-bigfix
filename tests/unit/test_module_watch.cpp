#include <doctest/doctest.h>
#include "module_watch.h"
#include "config.h"
#include "log.h"
#include <MinHook.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
struct Handler {
    std::wstring name;
    Handler(std::wstring value, std::function<void(HMODULE)> callback) : name(std::move(value)) {
        setModuleHandler(name, std::move(callback));
    }
    ~Handler() { setModuleHandler(name, {}); }
};
struct Hooks {
    ~Hooks() { MH_Uninitialize(); }
};
}

TEST_CASE("dispatch_is_case_insensitive_and_once") {
    int count = 0;
    const auto module = GetModuleHandleW(nullptr);
    Handler handler(L"rndrgl.dll", [&](HMODULE value) { CHECK(value == module); ++count; });
    onModuleLoaded(L"RNDRGL.DLL", module);
    onModuleLoaded(L"RndrGL.dll", module);
    CHECK(count == 1);
}

TEST_CASE("dispatch_ignores_null_and_unknown_modules") {
    int count = 0;
    Handler handler(L"test-null.dll", [&](HMODULE) { ++count; });
    onModuleLoaded(L"test-null.dll", nullptr);
    onModuleLoaded(L"unknown.dll", GetModuleHandleW(nullptr));
    CHECK(count == 0);
    onModuleLoaded(L"test-null.dll", GetModuleHandleW(nullptr));
    CHECK(count == 1);
}

TEST_CASE("dispatch_is_once_under_concurrency_and_allows_reentrancy") {
    std::atomic<int> count{0};
    Handler handler(L"test-threads.dll", [&](HMODULE module) {
        ++count;
        onModuleLoaded(L"TEST-THREADS.DLL", module);
        setModuleHandler(L"test-reentrant.dll", {});
    });
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i)
        threads.emplace_back([] { onModuleLoaded(L"test-threads.dll", GetModuleHandleW(nullptr)); });
    for (auto& thread : threads) thread.join();
    CHECK(count == 1);
}

TEST_CASE("dispatch_contains_handler_exceptions_and_does_not_retry") {
    int count = 0;
    Handler handler(L"test-throw.dll", [&](HMODULE) { ++count; throw std::runtime_error("test"); });
    CHECK_NOTHROW(onModuleLoaded(L"test-throw.dll", GetModuleHandleW(nullptr)));
    CHECK_NOTHROW(onModuleLoaded(L"test-throw.dll", GetModuleHandleW(nullptr)));
    CHECK(count == 1);
}

TEST_CASE("renderer_handler_logs_disabled_and_mismatched_sites_without_writing") {
    const auto dir = std::filesystem::temp_directory_path() /
        (L"BigFix-modules-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto file = dir / L"BigFix.log";
    logInit(file);
    Settings settings;
    settings.textureSizeFix = false;
    configureModuleWatch(settings);
    onModuleLoaded(L"RNDRGL.DLL", GetModuleHandleW(nullptr));
    settings.textureSizeFix = true;
    configureModuleWatch(settings);
    onModuleLoaded(L"rndrgl.dll", GetModuleHandleW(nullptr));
    std::ifstream input(file);
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    CHECK(text.find("texture_size_clamp: Skipped (TextureSizeFix=0)") != std::string::npos);
    CHECK(text.find("texture_size_clamp: Mismatch") != std::string::npos);
    input.close();
    setModuleHandler(L"rndrgl.dll", {});
    setModuleHandler(L"opengl32.dll", {});
    logClose();
    std::filesystem::remove_all(dir);
}

TEST_CASE("load_library_hooks_scan_loaded_modules_and_preserve_loader_behaviour") {
    REQUIRE(MH_Initialize() == MH_OK);
    Hooks hooks;
    int existing = 0;
    Handler kernel(L"kernel32.dll", [&](HMODULE h) { CHECK(h == GetModuleHandleW(L"kernel32.dll")); ++existing; });
    installModuleWatch();
    REQUIRE(MH_EnableHook(MH_ALL_HOOKS) == MH_OK);
    CHECK(existing == 1);

    // Each exported API must dispatch, even when the library is already resident.
    // Replacement of a registry entry deliberately starts a new once-only handler.
    auto checkLoad = [](auto load) {
        int calls = 0;
        Handler handler(L"version.dll", [&](HMODULE) {
            ++calls;
            SetLastError(ERROR_ACCESS_DENIED);
            throw std::runtime_error("hook handler failure");
        });
        HMODULE result = nullptr;
        CHECK_NOTHROW(result = load());
        REQUIRE(result != nullptr);
        CHECK(calls == 1);
        CHECK(GetLastError() != ERROR_ACCESS_DENIED);
        FreeLibrary(result);
    };
    checkLoad([] { SetLastError(0); return LoadLibraryA("version.dll"); });
    checkLoad([] { SetLastError(0); return LoadLibraryW(L"version.dll"); });
    checkLoad([] { SetLastError(0); return LoadLibraryExA("version.dll", nullptr, 0); });
    checkLoad([] { SetLastError(0); return LoadLibraryExW(L"version.dll", nullptr, 0); });

    int resources = 0;
    Handler resource(L"version.dll", [&](HMODULE) { ++resources; });
    for (DWORD flag : {DWORD(LOAD_LIBRARY_AS_DATAFILE), DWORD(LOAD_LIBRARY_AS_IMAGE_RESOURCE),
                       DWORD(DONT_RESOLVE_DLL_REFERENCES)}) {
        const auto h = LoadLibraryExW(L"version.dll", nullptr, flag);
        REQUIRE(h != nullptr);
        CHECK(resources == 0);
        FreeLibrary(h);
    }

    int dependencies = 0;
    REQUIRE(GetModuleHandleW(L"bigfix_watch_dependency.dll") == nullptr);
    Handler dependency(L"bigfix_watch_dependency.dll", [&](HMODULE) { ++dependencies; });
    const auto parent = LoadLibraryW(BIGFIX_TEST_PARENT_PATH);
    REQUIRE(parent != nullptr);
    CHECK(GetModuleHandleW(L"bigfix_watch_dependency.dll") != nullptr);
    CHECK(dependencies == 1);
    const auto value = reinterpret_cast<int (*)()>(GetProcAddress(parent, "bigfixParentValue"));
    REQUIRE(value != nullptr);
    CHECK(value() == 7);
    FreeLibrary(parent);

    SetLastError(0);
    CHECK(LoadLibraryW(L"BigFix-deliberately-nonexistent-test-library.dll") == nullptr);
    CHECK(GetLastError() == ERROR_MOD_NOT_FOUND);
}
