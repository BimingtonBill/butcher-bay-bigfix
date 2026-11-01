#include <doctest/doctest.h>
#include "log.h"

#include <windows.h>
#include <fstream>
#include <iterator>
#include <regex>
#include <thread>
#include <vector>

namespace {
struct LogDirectory {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        (L"BigFix-log-" + std::to_wstring(GetCurrentProcessId()));
    LogDirectory() { std::filesystem::create_directories(dir); }
    ~LogDirectory() {
        logClose(); // Release the real file before removing the fixture.
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};
std::string readLog(const std::filesystem::path& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}
}

TEST_CASE("log_close_releases_file_and_contains_boundary_logging_failures") {
    LogDirectory temp;
    const auto file = temp.dir / L"BigFix.log";
    logInit(file);
    logLine("before close");
    logClose();
    CHECK_NOTHROW(std::filesystem::rename(file, temp.dir / L"closed.log"));
    CHECK_THROWS(logLine("closed"));
    CHECK_NOTHROW(logLineNoThrow("closed boundary"));
}

TEST_CASE("log_rotates_previous_launch_and_flushes_current_lines") {
    LogDirectory temp;
    const auto file = temp.dir / L"BigFix.log";
    const auto previous = temp.dir / L"BigFix.prev.log";
    { std::ofstream out(previous); out << "obsolete session\n"; }
    { std::ofstream out(file); out << "previous session\n"; }
    logInit(file);
    logLine("first line");
    CHECK(readLog(file).find("first line\n") != std::string::npos);
    logf("value=%d %s", 42, "formatted");
    const auto text = readLog(file);
    CHECK(text.find("previous session") == std::string::npos);
    CHECK(readLog(previous) == "previous session\n");
    CHECK(std::regex_search(text, std::regex(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] first line\n)")));
    CHECK(text.find("value=42 formatted\n") != std::string::npos);
    logInit(file);
    logLine("second session");
    CHECK(readLog(file).find("first line") == std::string::npos);
    CHECK(readLog(file).find("second session\n") != std::string::npos);
    CHECK(readLog(previous) == text);
    CHECK(std::distance(std::filesystem::directory_iterator(temp.dir),
                        std::filesystem::directory_iterator{}) == 2);
}

TEST_CASE("log_first_launch_creates_no_previous_log") {
    LogDirectory temp;
    logInit(temp.dir / L"BigFix.log");
    logLine("new launch");
    CHECK_FALSE(std::filesystem::exists(temp.dir / L"BigFix.prev.log"));
    CHECK(readLog(temp.dir / L"BigFix.log").find("new launch") != std::string::npos);
}

TEST_CASE("log_serializes_complete_lines_from_threads") {
    LogDirectory temp;
    const auto file = temp.dir / L"BigFix.log";
    logInit(file);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < 25; ++i) logf("worker=%d line=%d", t, i);
        });
    }
    for (auto& thread : threads) thread.join();
    std::ifstream in(file);
    std::string line;
    int count = 0;
    const std::regex pattern(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] worker=[0-3] line=[0-9]+)");
    while (std::getline(in, line)) { CHECK(std::regex_match(line, pattern)); ++count; }
    CHECK(count == 100);
}

TEST_CASE("log_init_reports_failure") {
    LogDirectory temp;
    CHECK_THROWS(logInit(temp.dir / L"missing" / L"BigFix.log"));
}

TEST_CASE("log_prefixes_every_physical_line") {
    LogDirectory temp;
    const auto file = temp.dir / L"BigFix.log";
    logInit(file);
    logLine("first\r\nsecond\n");
    CHECK(std::regex_match(readLog(file), std::regex(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] first\n\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] second\n)")));
}
