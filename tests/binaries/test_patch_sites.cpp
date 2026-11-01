#include <doctest/doctest.h>
#include "patch_table.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

TEST_CASE("reference_rndrgl_fingerprint_site_and_mutation") {
    const wchar_t* root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) {
        MESSAGE("SKIPPED: RIDDICK_DIR is unset; no reference binaries read");
        return;
    }
    const auto path = std::filesystem::path(root) / L"System/Win64_AMD64/RndrGL.dll";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    const std::vector<uint8_t> pe{std::istreambuf_iterator<char>(input), {}};
    CHECK(sha256File(path) == "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348");
    CHECK(identify(sha256File(path)) == Build::EU11_x64);
    const auto& sites = sitesFor(Build::EU11_x64);
    REQUIRE(sites.size() == 1);
    for (const auto& site : sites) {
        const auto offset = rvaToFileOffset(pe.data(), pe.size(), site.rva);
        REQUIRE(offset.has_value());
        REQUIRE(*offset <= pe.size());
        REQUIRE(site.expected.size() <= pe.size() - *offset);
        CHECK(std::equal(site.expected.begin(), site.expected.end(), pe.begin() + *offset));
        // applyPatch takes an image-layout buffer, not raw file offsets.
        std::vector<uint8_t> image(size_t(site.rva) + site.expected.size());
        std::copy_n(pe.begin() + *offset, site.expected.size(), image.begin() + site.rva);
        image[site.rva] ^= 1;
        const auto before = image;
        CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Mismatch);
        CHECK(image == before);
    }
}
