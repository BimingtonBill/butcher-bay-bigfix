// Rewrite expectations derived from TCoRFix v1.4 by Nezarn (MIT).
#include <doctest/doctest.h>
#include "shader_rewrite.h"
#include "config.h"

TEST_CASE("rewrite_short_and_half_ops") {
    CHECK(rewriteFragmentProgram("SHORT TEMP a;\nSHORT OUTPUT b;\nMULH b, c, d;\nMADH b, c, d, e;", true, false) ==
          "TEMP a;\nOUTPUT b;\nMUL b, c, d;\nMAD b, c, d, e;");
}
TEST_CASE("nrmh_expanded") {
    CHECK(rewriteFragmentProgram("NRMH TSLV.rgb, IPTSLV;", true, false) ==
          "DP3 tcorfix.a, IPTSLV, IPTSLV;\nRSQ tcorfix.a, tcorfix.a;\nMUL TSLV.rgb, IPTSLV, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};");
    CHECK(rewriteFragmentProgram("NRMH TSEV.rgb, IPTSEV;", true, false) ==
          "DP3 tcorfix.a, IPTSEV, IPTSEV;\nRSQ tcorfix.a, tcorfix.a;\nMUL TSEV.rgb, IPTSEV, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};");
    CHECK(rewriteFragmentProgram("NRMH NormalMapTexel.rgb, NormalMapTexel;", true, false) ==
          "DP3 tcorfix.a, NormalMapTexel, NormalMapTexel;\nRSQ tcorfix.a, tcorfix.a;\nMUL NormalMapTexel.rgb, NormalMapTexel, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};");
}
TEST_CASE("half_saturated_opcodes_keep_the_sat_modifier") {
    CHECK(rewriteFragmentProgram("MULH_SAT a, b, c;\nMADH_SAT a, b, c, d;", true, false) ==
          "MUL_SAT a, b, c;\nMAD_SAT a, b, c, d;");
}
TEST_CASE("option_and_absolute_depth_rewritten") {
    CHECK(rewriteFragmentProgram("OPTION NV_fragment_program2;\nMUL_SAT depth, |depth|, depthtolerance.a;", true, false) ==
          "TEMP tcorfix;\nABS tcorfix, depth;\nMUL_SAT depth, tcorfix, depthtolerance.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};");
}
TEST_CASE("flicker_off_by_default_leaves_shadow_mul") {
    const std::string source = "MUL r0.rgb, r0, ShadowMaskTexel.a;";
    const Settings settings;
    CHECK_FALSE(settings.eyeshineFlickerWorkaround);
    CHECK(rewriteFragmentProgram(source, settings.enable20PlusPlusAnyGpu, settings.eyeshineFlickerWorkaround) == source);
    CHECK(rewriteFragmentProgram(source, false, true) == "#MUL r0.rgb, r0, ShadowMaskTexel.a;");
}
TEST_CASE("rewrite_idempotent_and_settings_independent") {
    const std::string source = "OPTION NV_fragment_program2;\nSHORT TEMP a;\nMULH a,b,c;\nMUL r0.rgb, r0, ShadowMaskTexel.a;";
    const auto once = rewriteFragmentProgram(source, true, true);
    CHECK(rewriteFragmentProgram(once, true, true) == once);
    CHECK(rewriteFragmentProgram(source, false, false) == source);
    CHECK(rewriteFragmentProgram("MY_MULH NRMH_EXTRA; # MULH comment\n", true, false) ==
          "MY_MULH NRMH_EXTRA; # MULH comment\n");
}
