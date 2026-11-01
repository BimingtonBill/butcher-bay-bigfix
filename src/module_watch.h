#pragma once

#include "config.h"
#include <functional>
#include <string>
#include <string_view>

// Replacing a handler resets its once-only state. Empty removes it.
void setModuleHandler(std::wstring name, std::function<void(HMODULE)> handler, bool afterLoaderReturn = false);
// Dispatches at most once per registered name; contains handler exceptions.
void onModuleLoaded(std::wstring_view baseName, HMODULE module) noexcept;
// Register renderer patches and GL hook creation. Dumps use the ASI directory.
void configureModuleWatch(const Settings& settings, const std::filesystem::path& pluginDir = {});
// Run deferred fingerprints/patches after a loader return or from SwapBuffers.
void processDeferredModuleWork();
void logExecutableFingerprintOnce();
// After MH_Initialize: create hooks and scan loaded modules, without loading any.
// Caller enables MH_ALL_HOOKS before returning from DLL_PROCESS_ATTACH.
void installModuleWatch();
