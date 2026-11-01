#include "log.h"
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {
std::mutex logMutex;
std::ofstream logFile;
bool loggingDisabled = false;
}

void logInit(const std::filesystem::path& file) {
    std::lock_guard<std::mutex> lock(logMutex);
    logFile.close();
    logFile.clear();
    loggingDisabled = false;
    if (std::filesystem::exists(file)) {
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("BigFix log is not a regular file");
        const auto previous = file.parent_path() / (file.stem().wstring() + L".prev" + file.extension().wstring());
        if (!MoveFileExW(file.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::system_error(GetLastError(), std::system_category(), "rotate BigFix log");
    }
    std::ofstream opened(file, std::ios::trunc);
    if (!opened) throw std::runtime_error("Cannot open BigFix log");
    logFile = std::move(opened);
}

void logInitBestEffort(const std::filesystem::path& file) noexcept {
    try { logInit(file); return; } catch (...) { }
    try {
        std::vector<wchar_t> temp(32768);
        const auto length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        if (!length || length >= temp.size()) throw std::runtime_error("temporary log path unavailable");
        const auto fallback = std::filesystem::path(std::wstring(temp.data(), length)) / L"BigFix.log";
        logInit(fallback);
        logf("Log path: %s (fallback; primary log unavailable)", fallback.u8string().c_str());
        return;
    } catch (...) { }
    try {
        const std::lock_guard<std::mutex> lock(logMutex);
        logFile.close();
        logFile.clear();
        loggingDisabled = true;
    } catch (...) { }
}

void logLineNoThrow(std::string_view text) noexcept {
    try { logLine(text); } catch (...) { /* Logging must not escape a hook or DllMain. */ }
}

void logClose() {
    const std::lock_guard<std::mutex> lock(logMutex);
    logFile.close();
    logFile.clear();
}

void logLine(std::string_view text) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (loggingDisabled) return;
    if (!logFile.is_open()) throw std::runtime_error("BigFix log is not initialized");
    const auto now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0) throw std::runtime_error("Cannot get log timestamp");
    do {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        logFile << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "] " << line << '\n';
        logFile.flush();
        if (!logFile) throw std::runtime_error("Cannot write BigFix log");
        if (newline == std::string_view::npos) break;
        text.remove_prefix(newline + 1);
    } while (!text.empty());
}

void logf(const char* fmt, ...) {
    if (!fmt) throw std::invalid_argument("Null log format");
    va_list args;
    va_start(args, fmt);
    const int size = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (size < 0) throw std::runtime_error("Cannot format log line");
    std::vector<char> buffer(static_cast<std::size_t>(size) + 1);
    va_start(args, fmt);
    const int written = std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);
    if (written != size) throw std::runtime_error("Cannot format log line");
    logLine(std::string_view(buffer.data(), static_cast<std::size_t>(size)));
}
