#pragma once

#include <filesystem>
#include <string_view>

// Start a fresh log, replacing <stem>.prev<extension> with the last launch.
// I/O failures throw; the previous log is never silently truncated.
void logInit(const std::filesystem::path& file);
// Startup logging is optional: try the primary path, then %TEMP%/BigFix.log.
// If both fail, logLine/logf become no-ops until the next initialization.
void logInitBestEffort(const std::filesystem::path& file) noexcept;
void logClose();
void logLine(std::string_view text);
void logf(const char* fmt, ...);
// For exception boundaries: best-effort logging, including before logInit.
void logLineNoThrow(std::string_view text) noexcept;
