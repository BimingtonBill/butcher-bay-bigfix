#include "config.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace {
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

struct BoolKey {
    const char* name;
    const char* alias;
    bool Settings::* member;
};
constexpr BoolKey boolKeys[] = {
    {"texturesizefix", "fix texture size", &Settings::textureSizeFix},
    {"fixmiscbugs", "fix misc bugs", &Settings::fixMiscBugs},
    {"enable20plusplusanygpu", "enable 2.0++ on any gpu", &Settings::enable20PlusPlusAnyGpu},
    {"eyeshinefix", "", &Settings::eyeshineFix},
    {"eyeshineflickerworkaround", "eyeshine flicker workaround", &Settings::eyeshineFlickerWorkaround},
    {"displaymodefilter", "", &Settings::displayModeFilter},
    {"capturepng", "", &Settings::capturePng},
    {"dumpshaders", "", &Settings::dumpShaders},
    {"dumpframetimes", "", &Settings::dumpFrameTimes},
    {"hudaspectfix", "", &Settings::hudAspectFix},
    {"tracefov", "", &Settings::traceFov},
    {"tracevideo", "", &Settings::traceVideo},
    {"traceshadowpass", "", &Settings::traceShadowPass},
    {"tracebloom", "", &Settings::traceBloom},
    {"videoaspectfix", "", &Settings::videoAspectFix},
    {"skiplogovideos", "", &Settings::skipLogoVideos},
    {"testforceeyeshine", "", &Settings::testForceEyeshine},
    {"backgroundaudio", "", &Settings::backgroundAudio},
    {"traceaudio", "", &Settings::traceAudio},
    {"traceeax", "", &Settings::traceEax},
    {"tracepixelformat", "", &Settings::tracePixelFormat},
    {"rawmouse", "", &Settings::rawMouse},
    {"traceinput", "", &Settings::traceInput},
};

constexpr const char* defaultsIni =
    "[BigFix]\n"
    "TextureSizeFix=1\n"
    "FixMiscBugs=1\n"
    "Enable20PlusPlusAnyGpu=1\n"
    "EyeshineFix=1\n"
    "EyeshineFixVariant=0\n"
    "EyeshineFlickerWorkaround=0\n"
    "DisplayModeFilter=1\n"
    "HudAspectFix=1\n"
    "FOV=0\n"
    "VideoAspectFix=1\n"
    "SkipLogoVideos=1\n"
    "BackgroundAudio=0\n"
    "TraceEax=0\n"
    "AntiAliasing=0\n"
    "RawMouse=1\n"
    "WindowMode=borderless\n"
    "CaptureKey=F12\n"
    "CapturePng=1\n"
    "FrameCap=0\n"
    "DumpShaders=0\n"
    "DumpFrameTimes=0\n";
}

Settings parseSettings(std::string_view iniText, std::vector<std::string>* warnings) {
    struct Entry {
        std::string key;
        std::string originalKey;
        std::string value;
        int priority;
        std::size_t line;
    };
    std::vector<Entry> entries;
    std::string section;
    std::size_t lineNumber = 0;
    const auto warn = [&](std::size_t line, std::string_view message) {
        if (warnings) warnings->push_back("Line " + std::to_string(line) + ": " + std::string(message));
    };
    // A UTF-8 BOM is common in INIs saved by Windows editors.
    if (iniText.substr(0, 3) == "\xEF\xBB\xBF") iniText.remove_prefix(3);
    while (!iniText.empty()) {
        ++lineNumber;
        const auto newline = iniText.find('\n');
        auto line = iniText.substr(0, newline);
        iniText = newline == std::string_view::npos ? std::string_view{} : iniText.substr(newline + 1);
        line = trim(line.substr(0, line.find(';')));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || trim(line.substr(0, equals)).empty()) {
            warn(lineNumber, "Malformed setting: " + std::string(line));
            continue;
        }
        auto key = lower(trim(line.substr(0, equals)));
        const auto value = lower(trim(line.substr(equals + 1)));
        bool canonical = key == "windowmode" || key == "capturekey" || key == "framecap" || key == "antialiasing" ||
                         key == "captureburst" || key == "eyeshinefixvariant" || key == "eyeshineabburst" || key == "fov";
        bool alias = false;
        for (const auto& item : boolKeys) {
            canonical = canonical || key == item.name;
            alias = alias || (item.alias[0] != '\0' && key == item.alias);
        }
        if ((!canonical && !alias) ||
            (!section.empty() && section != "bigfix" && section != "general")) {
            warn(lineNumber, "Unknown setting: " + std::string(trim(line.substr(0, equals))));
            continue;
        }
        entries.push_back({std::move(key), std::string(trim(line.substr(0, equals))), value,
                           canonical ? (section == "bigfix" ? 2 : 1) : 0,
                           lineNumber});
    }

    Settings settings;
    // Resolve aliases first, then canonical keys; file order only breaks equal priority ties.
    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.priority < b.priority; });
    for (const auto& entry : entries) {
        bool valid = false;
        if (entry.key == "windowmode") {
            if (entry.value == "borderless") { settings.windowMode = WindowMode::Borderless; valid = true; }
            if (entry.value == "fullscreen") { settings.windowMode = WindowMode::Fullscreen; valid = true; }
            if (entry.value == "windowed") { settings.windowMode = WindowMode::Windowed; valid = true; }
        } else if (entry.key == "antialiasing") {
            int samples = 0;
            const auto end = entry.value.data() + entry.value.size();
            const auto parsed = std::from_chars(entry.value.data(), end, samples);
            if (parsed.ec == std::errc{} && parsed.ptr == end && (samples == 0 || samples == 2 || samples == 4 || samples == 8)) {
                settings.antiAliasing = samples;
                valid = true;
            }
        } else if (entry.key == "eyeshinefixvariant" || entry.key == "eyeshineabburst") {
            int variant=0;
            const auto end=entry.value.data()+entry.value.size();
            const auto parsed=std::from_chars(entry.value.data(),end,variant);
            if(parsed.ec==std::errc{} && parsed.ptr==end && variant>=0 && variant<=8) {
                if(entry.key=="eyeshineabburst") settings.eyeshineAbBurst=variant;
                else settings.eyeshineFixVariant=variant;
                valid=true;
            }
        } else if (entry.key == "captureburst") {
            int frames = 0;
            const auto end = entry.value.data() + entry.value.size();
            const auto parsed = std::from_chars(entry.value.data(), end, frames);
            if (parsed.ec == std::errc{} && parsed.ptr == end && frames >= 1 && frames <= 16) {
                settings.captureBurst = frames;
                valid = true;
            }
        } else if (entry.key == "capturekey") {
            if (entry.value.size() > 1 && entry.value.front() == 'f') {
                unsigned number = 0;
                const auto end = entry.value.data() + entry.value.size();
                const auto parsed = std::from_chars(entry.value.data() + 1, end, number);
                if (parsed.ec == std::errc{} && parsed.ptr == end && number >= 1 && number <= 24) {
                    settings.captureVk = VK_F1 + number - 1;
                    valid = true;
                }
            }
        } else if (entry.key == "fov") {
            char* end=nullptr;
            const auto value=std::strtod(entry.value.c_str(),&end);
            if(end!=entry.value.c_str() && *end==0 && std::isfinite(value) &&
               (value==0 || (value>=60 && value<=120))) { settings.fov=value; valid=true; }
        } else if (entry.key == "framecap") {
            // 0 turns the cap off; below 1 FPS is a typo that would freeze the
            // game for seconds per frame, so it is rejected like any bad value.
            char* end = nullptr;
            const double parsed = std::strtod(entry.value.c_str(), &end);
            if (end != entry.value.c_str() && *end == '\0' && std::isfinite(parsed) &&
                (parsed == 0.0 || parsed >= 1.0)) {
                settings.frameCap = parsed;
                valid = true;
            }
        } else {
            for (const auto& item : boolKeys) {
                if (entry.key != item.name && entry.key != item.alias) continue;
                if (entry.value == "0" || entry.value == "1") {
                    settings.*(item.member) = entry.value == "1";
                    valid = true;
                }
                break;
            }
        }
        if (!valid) warn(entry.line, "Invalid value for " + entry.originalKey + ": " + entry.value);
    }
    return settings;
}

std::filesystem::path moduleDir(HMODULE self) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) throw std::system_error(GetLastError(), std::system_category(), "GetModuleFileNameW");
        if (length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

Settings loadSettings(const std::filesystem::path& dir, std::vector<std::string>& warnings) {
    const auto file = dir / L"BigFix.ini";
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) {
        warnings.push_back("Cannot inspect BigFix.ini: " + error.message());
        return {};
    }
    if (!exists) {
        std::ofstream out(file);
        out << defaultsIni;
        out.close();
        if (!out) warnings.push_back("Cannot write default BigFix.ini");
        return {};
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        warnings.push_back("Cannot read BigFix.ini");
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    if (in.bad()) {
        warnings.push_back("Error reading BigFix.ini");
        return {};
    }
    return parseSettings(text, &warnings);
}
