#include <doctest/doctest.h>
#include "config.h"
#include "eyeshine_variants.h"
#include <string>

TEST_CASE("variant_setting_accepts_scissor_probe_and_is_bounded") {
    CHECK(Settings{}.eyeshineFixVariant==0);
    for (int n=0;n<=8;++n) {
        std::vector<std::string> warnings;
        auto s=parseSettings("[BigFix]\nEyeshineFixVariant="+std::to_string(n),&warnings);
        CHECK(s.eyeshineFixVariant==n); CHECK(warnings.empty()); CHECK(s.eyeshineFix);
    }
    for (auto value:{"-1","9","1x",""}) {
        std::vector<std::string> warnings;
        CHECK(parseSettings(std::string("EyeshineFixVariant=")+value,&warnings).eyeshineFixVariant==0);
        CHECK(warnings.size()==1);
    }
}
TEST_CASE("variant_hotkey_cycles_once_per_press_including_off") {
    EyeshineVariantCycle c;
    for (int n=1;n<=9;++n) {
        CHECK(c.press(true)); CHECK(c.value==n%9);
        CHECK_FALSE(c.press(true)); CHECK_FALSE(c.press(false));
    }
}
TEST_CASE("ab_burst_setting_is_opt_in_and_accepts_only_existing_variants") {
    for(int n=0;n<=8;++n) {
        std::vector<std::string> warnings;
        const auto s=parseSettings("EyeshineAbBurst="+std::to_string(n),&warnings);
        CHECK(warnings.empty()); CHECK(s.eyeshineAbBurst==n);
    }
    CHECK(Settings{}.eyeshineAbBurst==0);
    for(auto value:{"-1","9","8x",""}) {
        std::vector<std::string> warnings;
        CHECK(parseSettings(std::string("EyeshineAbBurst=")+value,&warnings).eyeshineAbBurst==0);
        CHECK(warnings.size()==1);
    }
}
TEST_CASE("ab_sequence_labels_whole_rendered_frames_and_restores_selection") {
    for(int count:{1,2,7,8,16}) {
        EyeshineAbSequence b;
        REQUIRE(b.begin(count,8,3));
        CHECK_FALSE(b.begin(2,7,0));
        int variant=0;
        for(int n=0;n<count;++n) {
            CHECK(variant==(n%2?8:0));
            CHECK(b.remaining==count-n);
            variant=b.advance();
        }
        CHECK(variant==3);CHECK(b.remaining==0);
    }
    for(int invalid:{-1,0,9}) {EyeshineAbSequence b;CHECK_FALSE(b.begin(8,invalid,0));}
}
TEST_CASE("mask_copy_mapping_accounts_for_offset_and_half_texel_bounds") {
    auto m=eyeshineMaskMapping(4096,2048,0,968,0,360,1920,1080);
    REQUIRE(m.valid);
    CHECK(m.map[0]==doctest::Approx(1.0/4096));
    CHECK(m.map[3]==doctest::Approx(608.0/2048));
    CHECK(m.bounds[0]==doctest::Approx(.5/4096));
    CHECK(m.bounds[3]==doctest::Approx(2047.5/2048));
    CHECK_FALSE(eyeshineMaskMapping(4096,2048,0,968,0,360,1920,1081).valid);
    CHECK_FALSE(eyeshineMaskMapping(0,2048,0,0,0,0,1,1).valid);
    CHECK_FALSE(eyeshineMaskMapping(4096,2048,-1,0,0,0,1,1).valid);
}
TEST_CASE("variant_shader_keeps_lighting_and_only_instruments_matching_programs") {
    const std::string src="!!ARBfp1.0\nOUTPUT oCol = result.color;\nTEMP smtc0;\nTEMP smtc1;\n"
        "# ShadowMaskTexel NormalMapTexel texture[6]\nADD smtc0.xy, smtc0, 0.000001;\n"
        "MAD smtc1.xy, SMOfs0, PixelUV, smtc0;\nMUL r0.rgb, r0, ShadowMaskTexel.a;\nEND";
    auto out=instrumentEyeshineProgram(src);
    CHECK(out.find("program.local[0]")!=out.npos);
    CHECK(out.find("fragment.position")!=out.npos);
    CHECK(out.find("MAX BigFixCoord.xy, smtc1")!=out.npos);
    CHECK(out.find("MUL r0.rgb, r0, ShadowMaskTexel.a;")!=out.npos);
    CHECK(instrumentEyeshineProgram(out)==out);
    CHECK(instrumentEyeshineProgram("!!ARBfp1.0\nEND")=="!!ARBfp1.0\nEND");
    CHECK(instrumentEyeshineProgram(src+"\nPARAM occupied = program.local[0];")==src+"\nPARAM occupied = program.local[0];");
}
