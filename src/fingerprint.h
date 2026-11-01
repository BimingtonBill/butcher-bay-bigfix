#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

enum class Build { EU11_x64, Unknown };

// Hash failures throw; an unreadable image must never identify as a known build.
std::string sha256Buffer(const uint8_t* data, size_t size);
std::string sha256File(const std::filesystem::path& path);
std::string sha256Module(HMODULE module);
Build identify(std::string_view rndrglSha256);
