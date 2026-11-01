#include <doctest/doctest.h>
#include "config.h"
#include "eyeshine_fix.h"
#include <limits>

TEST_CASE("reduced_region_uses_bottom_left_offset_and_accepts_scale_transition") {
    CHECK(eyeshineReducedRegion({0,360,1920,1080},2560,1440)==EyeshineRect{0,360,1920,1080});
    CHECK(eyeshineReducedRegion({0,160,2272,1280},2560,1440)==EyeshineRect{0,160,2272,1280});
    for(const EyeshineRect v: {EyeshineRect{0,0,2560,1440},EyeshineRect{0,0,1920,1080},EyeshineRect{320,180,1920,1080},EyeshineRect{0,720,1280,720},EyeshineRect{0,360,2560,1080},EyeshineRect{0,8,0,24}})
        CHECK_FALSE(eyeshineReducedRegion(v,2560,1440));
    CHECK_FALSE(eyeshineReducedRegion({0,8,24,24},0,0));
}

TEST_CASE("mask_scissor_intersects_fresh_draws_without_expanding_coverage") {
    EyeshineScissorTracker t;
    const EyeshineRect light{79,360,1388,114}, region{0,360,1920,1080};
    CHECK_FALSE(t.draw(5135,true,region)); // no copy this frame
    t.copy(5135,light);
    REQUIRE(t.draw(5135,true,region));
    CHECK(*t.draw(5135,true,region)==light);
    CHECK_FALSE(t.draw(5134,true,region)); // depth/another light texture
    CHECK(t.draw(5135,false,region)==light);
    CHECK_FALSE(t.draw(5135,true,light)); // Eyeshine OFF: no writes
    CHECK(t.draw(5135,true,{80,360,1388,114})==EyeshineRect{80,360,1387,114});
    CHECK(t.draw(5135,true,{2000,360,100,114})==EyeshineRect{0,0,0,0}); // reject all fragments
    CHECK_FALSE(t.draw(5135,true,{100,370,100,50})); // narrower
    t.copy(5135,{743,474,617,966});
    CHECK(t.draw(5135,true,region)==EyeshineRect{743,474,617,966});
    t.copy(5135,{0,0,0,20});CHECK_FALSE(t.draw(5135,true,region));
    t.copy(5135,light);t.reset();CHECK_FALSE(t.draw(5135,true,region));
}
TEST_CASE("rect_intersection_handles_edges_empty_and_integer_overflow") {
    CHECK(eyeshineRectIntersection({0,0,10,10},{5,3,20,4})==EyeshineRect{5,3,5,4});
    CHECK_FALSE(eyeshineRectIntersection({0,0,10,10},{10,0,2,2}));
    CHECK_FALSE(eyeshineRectIntersection({0,0,-1,10},{0,0,2,2}));
    const auto hi=(std::numeric_limits<GLint>::max)();
    CHECK(eyeshineRectIntersection({hi-5,0,10,10},{hi-2,0,3,10})==EyeshineRect{hi-2,0,3,10});
}

TEST_CASE("eyeshine_scissor_fix_defaults_on_and_workaround_remains_independent") {
    CHECK(Settings{}.eyeshineFix);
    CHECK_FALSE(Settings{}.eyeshineFlickerWorkaround);
    std::vector<std::string> warnings;
    const auto settings=parseSettings("[BigFix]\nEyeshineFix=0\nEyeshineFlickerWorkaround=1",&warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(settings.eyeshineFix);
    CHECK(settings.eyeshineFlickerWorkaround);
}
TEST_CASE("packed_copy_recognizer_consumes_tokens_and_fails_closed") {
    EyeshineCopyTracker t;
    const auto a=reinterpret_cast<HGLRC>(1), b=reinterpret_cast<HGLRC>(2);
    const auto consume=[&](HGLRC c,GLenum target=GL_TEXTURE_2D,GLint level=0,GLint x=0,GLsizei w=2560) {
        return t.texture(c,target,level,x,0,w,1440);
    };
    CHECK_FALSE(consume(a));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK(consume(a)); CHECK_FALSE(consume(a));
    t.pixels(a,0,0,2560,1440,GL_COLOR); CHECK_FALSE(consume(a));
    t.pixels(nullptr,0,0,2560,1440,0x886E); CHECK_FALSE(consume(a));
    t.pixels(a,0,0,0,1440,0x886E); CHECK_FALSE(consume(a));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK_FALSE(consume(b)); CHECK_FALSE(consume(a));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK_FALSE(consume(a,GL_TEXTURE_1D));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK_FALSE(consume(a,GL_TEXTURE_2D,1));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK_FALSE(consume(a,GL_TEXTURE_2D,0,1));
    t.pixels(a,0,0,2560,1440,0x886E); CHECK_FALSE(consume(a,GL_TEXTURE_2D,0,0,1920));
}
