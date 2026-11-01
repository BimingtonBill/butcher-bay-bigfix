#include <doctest/doctest.h>
#include "patch_table.h"
#include <algorithm>
#include <limits>

namespace {
PatchSite smallSite() { return {"test", L"test.dll", 2, {1, 2, 3}, {1, 9, 3}}; }
void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (8 * i));
}
std::vector<uint8_t> syntheticPe() {
    std::vector<uint8_t> pe(0x500);
    pe[0] = 'M'; pe[1] = 'Z'; put32(pe, 0x3c, 0x80);
    pe[0x80] = 'P'; pe[0x81] = 'E';
    pe[0x84] = 0x64; pe[0x85] = 0x86; // AMD64
    pe[0x86] = 1; // one section
    pe[0x94] = 0xf0; // PE32+ optional header size
    pe[0x98] = 0x0b; pe[0x99] = 2;
    put32(pe, 0x98 + 60, 0x200); // SizeOfHeaders
    const size_t section = 0x188;
    put32(pe, section + 8, 0x400); // VirtualSize (tail has no file bytes)
    put32(pe, section + 12, 0x1000);
    put32(pe, section + 16, 0x300);
    put32(pe, section + 20, 0x200);
    return pe;
}
uint8_t liveBytes[] = {1, 2, 3};
}

TEST_CASE("applies_when_bytes_match") {
    std::vector<uint8_t> image{0, 0, 1, 2, 3, 0};
    CHECK(applyPatch(image.data(), image.size(), smallSite()) == PatchResult::Applied);
    CHECK(image == std::vector<uint8_t>{0, 0, 1, 9, 3, 0});
}
TEST_CASE("mismatch_never_writes") {
    std::vector<uint8_t> image{0, 0, 1, 4, 3, 0};
    const auto before = image;
    CHECK(applyPatch(image.data(), image.size(), smallSite()) == PatchResult::Mismatch);
    CHECK(image == before);
}
TEST_CASE("already_applied_detected") {
    std::vector<uint8_t> image{0, 0, 1, 9, 3, 0};
    const auto before = image;
    CHECK(applyPatch(image.data(), image.size(), smallSite()) == PatchResult::AlreadyApplied);
    CHECK(image == before);
}
TEST_CASE("out_of_range_rva_is_bad_address") {
    std::vector<uint8_t> image{0, 0, 1, 2};
    const auto before = image;
    auto site = smallSite();
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::BadAddress);
    site.rva = (std::numeric_limits<uint32_t>::max)();
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::BadAddress);
    CHECK(applyPatch(nullptr, 99, smallSite()) == PatchResult::BadAddress);
    CHECK(image == before);
}
TEST_CASE("invalid_patch_lengths_never_write") {
    std::vector<uint8_t> image{0, 0, 1, 2, 3};
    const auto before = image;
    auto site = smallSite(); site.replacement.pop_back();
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::BadAddress);
    site.expected.clear(); site.replacement.clear();
    CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::BadAddress);
    CHECK(image == before);
}
TEST_CASE("known_and_unknown_builds_offer_byte_verified_texture_patch") {
    for (auto build : {Build::EU11_x64, Build::Unknown}) {
        const auto& sites = sitesFor(build);
        REQUIRE(sites.size() == 1);
        const auto& site = sites.front();
        CHECK(std::string(site.name) == "texture_size_clamp");
        CHECK(std::wstring(site.module) == L"RndrGL.dll");
        CHECK(site.rva == 0x84b3);
        std::vector<uint8_t> image(0x84b9);
        const std::vector<uint8_t> original{0, 8, 0, 0, 0x7e, 0x0a};
        std::copy(original.begin(), original.end(), image.begin() + 0x84b3);
        CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Applied);
        CHECK(std::vector<uint8_t>(image.end() - 6, image.end()) ==
              std::vector<uint8_t>{0, 8, 0, 0, 0xeb, 0x0a});
    }
}
TEST_CASE("pe_rva_maps_raw_sections_and_headers_but_not_virtual_tail") {
    auto pe = syntheticPe();
    CHECK(rvaToFileOffset(pe.data(), pe.size(), 0x1010) == 0x210);
    CHECK(rvaToFileOffset(pe.data(), pe.size(), 0x12ff) == 0x4ff);
    CHECK(rvaToFileOffset(pe.data(), pe.size(), 0x100) == 0x100);
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x1300));
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x5000));
}
TEST_CASE("malformed_or_truncated_pe_is_rejected") {
    auto pe = syntheticPe();
    CHECK_FALSE(rvaToFileOffset(nullptr, 0, 0));
    for (size_t size : {0u, 63u, 0x81u, 0x190u, 0x210u})
        CHECK_FALSE(rvaToFileOffset(pe.data(), size, 0x1010));
    pe[0] = 0;
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x1010));
    pe = syntheticPe(); pe[0x80] = 0;
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x1010));
    pe = syntheticPe(); put32(pe, 0x3c, 0xfffffff0);
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x1010));
    pe = syntheticPe(); put32(pe, 0x188 + 20, 0xfffffff0);
    CHECK_FALSE(rvaToFileOffset(pe.data(), pe.size(), 0x1010));
}
TEST_CASE("live_patch_verifies_own_module_and_restores_protection") {
    auto module = GetModuleHandleW(nullptr);
    auto site = smallSite();
    site.rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(liveBytes) - reinterpret_cast<uintptr_t>(module));
    MEMORY_BASIC_INFORMATION before{}, after{};
    REQUIRE(VirtualQuery(liveBytes, &before, sizeof(before)) != 0);
    CHECK(applyPatchLive(module, site) == PatchResult::Applied);
    CHECK(liveBytes[1] == 9);
    CHECK(applyPatchLive(module, site) == PatchResult::AlreadyApplied);
    site.expected = {7, 7, 7}; site.replacement = {8, 8, 8};
    CHECK(applyPatchLive(module, site) == PatchResult::Mismatch);
    CHECK(liveBytes[1] == 9);
    REQUIRE(VirtualQuery(liveBytes, &after, sizeof(after)) != 0);
    CHECK(after.Protect == before.Protect);
    liveBytes[1] = 2;
    CHECK(applyPatchLive(nullptr, smallSite()) == PatchResult::BadAddress);
    CHECK(applyPatchLive(reinterpret_cast<HMODULE>(1), smallSite()) == PatchResult::BadAddress);
}
