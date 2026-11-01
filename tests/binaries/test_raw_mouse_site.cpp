#include <doctest/doctest.h>
#include "raw_mouse.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

TEST_CASE("reference_raw_mouse_poll_prologue") {
    const auto* root=_wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR unset; no mouse reference binary read"); return; }
    const auto path=std::filesystem::path(root)/L"System/Win64_AMD64/MSystem.dll";
    REQUIRE(sha256File(path) == "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500");
    std::ifstream in(path,std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};
    const auto& site=rawMousePollSite();
    const auto offset=rvaToFileOffset(bytes.data(),bytes.size(),site.rva);
    REQUIRE(offset.has_value());
    REQUIRE(*offset+site.expected.size() <= bytes.size());
    CHECK(std::equal(site.expected.begin(),site.expected.end(),bytes.begin()+*offset));
}
