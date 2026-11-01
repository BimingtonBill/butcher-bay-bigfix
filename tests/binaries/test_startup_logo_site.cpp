#include <doctest/doctest.h>
#include "patch_table.h"
#include "eyeshine_variants.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

TEST_CASE("reference_world_startup_command_matches_without_loading_game_code") {
    const auto* root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR is unset"); return; }
    const auto path = std::filesystem::path(root) / L"Content/GameWorld_Win64_AMD64.dll";
    CHECK(sha256File(path) == "749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7");
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    const std::vector<uint8_t> pe{std::istreambuf_iterator<char>(input), {}};
    const auto& site = skipLogoVideosSite();
    const auto offset = rvaToFileOffset(pe.data(), pe.size(), site.rva);
    REQUIRE(offset.has_value());
    REQUIRE(*offset <= pe.size());
    REQUIRE(site.expected.size() <= pe.size() - *offset);
    CHECK(std::equal(site.expected.begin(), site.expected.end(), pe.begin() + *offset));
    const auto scale=rvaToFileOffset(pe.data(),pe.size(),eyeshineScaleRva);
    REQUIRE(scale.has_value());
    REQUIRE(*scale+eyeshineScaleBytes.size()<=pe.size());
    CHECK(std::equal(eyeshineScaleBytes.begin(),eyeshineScaleBytes.end(),pe.begin()+*scale));
}

TEST_CASE("reference_classes_eyeshine_gate_matches_without_loading_game_code") {
    const auto* root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR is unset"); return; }
    const auto path = std::filesystem::path(root) / L"Content/GameClasses_Win64_AMD64.dll";
    CHECK(sha256File(path) == "263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773");
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    const std::vector<uint8_t> pe{std::istreambuf_iterator<char>(input), {}};
    const auto& site = forceEyeshineSite();
    const auto offset = rvaToFileOffset(pe.data(), pe.size(), site.rva);
    REQUIRE(offset.has_value());
    REQUIRE(site.expected.size() <= pe.size() - *offset);
    CHECK(std::equal(site.expected.begin(), site.expected.end(), pe.begin() + *offset));
    CHECK(site.replacement.size() == site.expected.size());
}
