#include <doctest/doctest.h>
#include "patch_table.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

TEST_CASE("reference_msystem_borderless_branch_and_renderer_selection_import") {
    const auto* root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) {
        MESSAGE("SKIPPED: RIDDICK_DIR is unset; no borderless reference binaries read");
        return;
    }
    const auto directory = std::filesystem::path(root) / L"System/Win64_AMD64";
    const auto system = directory / L"MSystem.dll";
    CHECK(sha256File(system) == "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500");
    std::ifstream stream(system, std::ios::binary);
    const std::vector<uint8_t> pe{std::istreambuf_iterator<char>(stream), {}};
    for (const auto* site : {&borderlessModeSelectionSite(), &windowedModeSelectionSite()}) {
        const auto offset = rvaToFileOffset(pe.data(), pe.size(), site->rva);
        REQUIRE(offset.has_value());
        REQUIRE(*offset + site->expected.size() <= pe.size());
        CHECK(std::equal(site->expected.begin(), site->expected.end(), pe.begin() + *offset));
        std::vector<uint8_t> image(site->rva + site->expected.size());
        std::copy_n(pe.begin() + *offset, site->expected.size(), image.begin() + site->rva);
        CHECK(applyPatch(image.data(), image.size(), *site) == PatchResult::Applied);
        image[site->rva] ^= 1;
        const auto before = image;
        CHECK(applyPatch(image.data(), image.size(), *site) == PatchResult::Mismatch);
        CHECK(image == before);
    }

    // Verify the dispatch thunk actually uses the imported selector hooked by
    // name. All reads are raw PE bytes; no game image is loaded or executed.
    std::ifstream renderer(directory / L"RndrGL.dll", std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(renderer), {}};
    const auto thunk = rvaToFileOffset(bytes.data(), bytes.size(), 0x65d68);
    REQUIRE(thunk.has_value());
    const std::vector<uint8_t> jump{0xff, 0x25, 0x3a, 0x8a, 0x02, 0x00}; // -> IAT 0x8e7a8
    REQUIRE(*thunk + jump.size() <= bytes.size());
    CHECK(std::equal(jump.begin(), jump.end(), bytes.begin() + *thunk));
}
