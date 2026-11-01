#include <doctest/doctest.h>
#include "widescreen.h"
#include <cmath>
#include <limits>

TEST_CASE("widescreen settings default on HUD and original FOV") {
    std::vector<std::string> warnings;
    auto s=parseSettings("HudAspectFix=0\nFOV=100.5\nTraceFov=1",&warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(s.hudAspectFix);
    CHECK(s.fov==100.5);
    CHECK(s.traceFov);
    CHECK(Settings{}.hudAspectFix);
    CHECK(Settings{}.fov==0);
    CHECK_FALSE(Settings{}.traceFov);
    for(const auto* bad:{"nan","inf","-1","59.9","120.1","100oops"}) {
        warnings.clear();
        CHECK(parseSettings(std::string("FOV=")+bad,&warnings).fov==0);
        CHECK(warnings.size()==1);
    }
    CHECK(parseSettings("FOV=60").fov==60);
    CHECK(parseSettings("FOV=120").fov==120);
    CHECK(parseSettings("FOV=0").fov==0);
}

TEST_CASE("four three canvas mapping preserves centre and pixel proportions") {
    const auto m=hudCanvasMapping(2560,1440);
    CHECK(m.scaleX==doctest::Approx(.75));
    CHECK(m.scaleY==1);
    CHECK(m.offsetX==80);
    CHECK(m.offsetY==0);
    CHECK(4*m.scaleX==3*m.scaleY);
    CHECK(m.offsetX+320*m.scaleX==320);
    for(const auto size:{std::pair{640,480},std::pair{1920,1080},std::pair{3440,1440},std::pair{1280,1024}}) {
        const auto map=hudCanvasMapping(size.first,size.second);
        CHECK(map.offsetX+320*map.scaleX==doctest::Approx(320));
        CHECK(map.offsetY+240*map.scaleY==doctest::Approx(240));
        CHECK(size.first/640.0*map.scaleX==doctest::Approx(size.second/480.0*map.scaleY));
    }
    CHECK(hudCanvasMapping(0,480).scaleX==1);
}

TEST_CASE("HUD matrix mapping includes translation and both coordinate bases") {
    std::array<float,16> m{4,1,2,0, 3,6,1,0, 0,0,1,0, -1280,-1440,20,1};
    const auto before=m;
    mapHudMatrix(m,hudCanvasMapping(2560,1440));
    for(int i=0;i<4;++i) {
        CHECK(m[i]==doctest::Approx(before[i]*.75));
        CHECK(m[4+i]==before[4+i]);
        CHECK(m[12+i]==doctest::Approx(before[12+i]+80*before[i]));
    }
}

TEST_CASE("FOV option preserves relative tangent magnification and rejects invalid input") {
    CHECK(relativeGameplayFov(90,100)==doctest::Approx(100));
    CHECK(relativeGameplayFov(60,0)==60);
    CHECK(relativeGameplayFov(45,90)==45);
    constexpr double radians=3.14159265358979323846/180;
    const auto zoom=relativeGameplayFov(45,110);
    CHECK(std::tan(zoom*radians/2)/std::tan(110*radians/2)==doctest::Approx(std::tan(45*radians/2)));
    CHECK(relativeGameplayFov(90,200)==90);
    CHECK(relativeGameplayFov(-5,100)==-5);
    CHECK(relativeGameplayFov(90,std::numeric_limits<double>::infinity())==90);
}

TEST_CASE("authored dialogue external and special cameras are excluded") {
    CHECK(isGameplayCamera(0,0));
    CHECK_FALSE(isGameplayCamera(1u<<23,0));
    CHECK_FALSE(isGameplayCamera(1u<<26,0));
    CHECK_FALSE(isGameplayCamera(0,2));
}

TEST_CASE("menu background corners cover the uploaded projection including an off-centre clip") {
    // Engine frustum helper is based on the requested rect; a cropped view
    // has an off-axis projection. Both axes must align to the actual view.
    std::array<float,15> quad{0,0,10, -10,-10,10, 10,-10,10, 10,10,10, -10,10,10};
    const auto original=quad;
    const std::array<float,16> projection{1000,0,0,0, 0,800,0,0, 100,-80,1,1, 0,0,-1,0};
    REQUIRE(alignMenuQuad(quad,projection,2560,1440));
    for(int i=1;i<5;++i) {
        const auto j=i*3;
        const double x=(quad[j]*projection[0]+quad[j+2]*projection[8])/(quad[j+2]*projection[11]);
        const double y=(quad[j+1]*projection[5]+quad[j+2]*projection[9])/(quad[j+2]*projection[11]);
        CHECK(std::abs(x)==doctest::Approx(1280));
        CHECK(std::abs(y)==doctest::Approx(720));
        CHECK(quad[j+2]==original[j+2]);
    }
    CHECK(quad[0]==original[0]);
    const auto aligned=quad;
    CHECK_FALSE(alignMenuQuad(quad,projection,0,1440));
    CHECK(quad==aligned);
    auto invalid=projection;invalid[0]=0;
    CHECK_FALSE(alignMenuQuad(quad,invalid,2560,1440));
    CHECK(quad==aligned);
}
