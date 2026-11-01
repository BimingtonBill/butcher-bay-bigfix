#include <doctest/doctest.h>
#include "patch_table.h"
#include <algorithm>
#include <cstring>

TEST_CASE("startup_command_name_match_is_exact_and_preserves_story_video_names") {
    const auto& site = skipLogoVideosSite();
    const std::string original = "cg_rootmenu \"intro\"";
    REQUIRE(site.rva == 0x1e80e0);
    REQUIRE(site.expected.size() == original.size() + 1);
    CHECK(std::string(site.expected.begin(), site.expected.end() - 1) == original);
    REQUIRE(site.replacement.size() == site.expected.size());
    CHECK(std::string(reinterpret_cast<const char*>(site.replacement.data())) == "cg_rootmenu \"main\"");
    for (const auto* name : {"Intro.ogg", "Outtro.ogg", "Eyeshine.ogg", "video_intro", "main"}) {
        std::vector<uint8_t> image(site.rva + site.expected.size(), 0);
        std::copy_n(name, strlen(name), image.begin() + site.rva);
        const auto before = image;
        CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Mismatch);
        CHECK(image == before);
    }
    for (size_t i = 0; i < site.expected.size(); ++i) {
        std::vector<uint8_t> image(site.rva + site.expected.size(), 0);
        std::copy(site.expected.begin(), site.expected.end(), image.begin() + site.rva);
        image[site.rva + i] ^= 1;
        const auto before = image;
        CHECK(applyPatch(image.data(), image.size(), site) == PatchResult::Mismatch);
        CHECK(image == before);
    }
}
