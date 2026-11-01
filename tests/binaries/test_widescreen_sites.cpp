#include <doctest/doctest.h>
#include "widescreen.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

TEST_CASE("reference_widescreen_sites_and_semantic_callers") {
    const auto* root=_wgetenv(L"RIDDICK_DIR");
    if(!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR unset; no widescreen binaries read"); return; }
    for(const auto kind:{WidescreenModule::World,WidescreenModule::Classes,WidescreenModule::Renderer,WidescreenModule::System}) {
        const auto module=kind==WidescreenModule::World ? L"Content/GameWorld_Win64_AMD64.dll":
            kind==WidescreenModule::Classes ? L"Content/GameClasses_Win64_AMD64.dll":
            kind==WidescreenModule::Renderer ? L"System/Win64_AMD64/RndrGL.dll":L"System/Win64_AMD64/MSystem.dll";
        const auto path=std::filesystem::path(root)/module;
        const auto hash=kind==WidescreenModule::World ? "749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7":
            kind==WidescreenModule::Classes ? "263c03a430f070ebf5f32c59b598b9e8e98a9d06d07172f76f6786bc0dfc7773":
            kind==WidescreenModule::Renderer ? "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348":
            "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500";
        REQUIRE(sha256File(path)==hash);
        std::ifstream in(path,std::ios::binary);
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}};
        for(const auto& item:widescreenSites()) if(item.module==kind) {
            const auto& site=item.patch; CAPTURE(site.name);
            const auto offset=rvaToFileOffset(bytes.data(),bytes.size(),site.rva);
            REQUIRE(offset.has_value()); REQUIRE(*offset+site.expected.size()<=bytes.size());
            CHECK(std::equal(site.expected.begin(),site.expected.end(),bytes.begin()+*offset));
        }
    }
}
