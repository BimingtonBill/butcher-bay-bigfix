#include <doctest/doctest.h>
#include "capture.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include <map>
#include <cstring>

TEST_CASE("file_name_format") {
    CaptureMeta meta{};
    meta.time.tm_year = 126; meta.time.tm_mon = 9; meta.time.tm_mday = 29;
    meta.time.tm_hour = 8; meta.time.tm_min = 7; meta.time.tm_sec = 6;
    meta.w = 2560; meta.h = 1440; meta.shaderMode = "5"; meta.index = 12;
    CHECK(captureFileName(meta) == "BigFix_20261029-080706_2560x1440_sm5_12.png");
    meta.shaderMode = "x";
    CHECK(captureFileName(meta) == "BigFix_20261029-080706_2560x1440_smx_12.png");
}

TEST_CASE("png_roundtrip_flips_rows") {
    const auto dir = std::filesystem::temp_directory_path() /
        (L"bigfix-capture-\u00ff-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto file = dir / L"test.png";
    const std::vector<uint8_t> bottomUp{0,0,255, 255,255,255, 255,0,0, 0,255,0};
    REQUIRE(writePngRgb(file, 2, 2, bottomUp));
    FILE* input = nullptr;
    REQUIRE(_wfopen_s(&input, file.c_str(), L"rb") == 0);
    int w = 0, h = 0, channels = 0;
    auto* decoded = stbi_load_from_file(input, &w, &h, &channels, 3);
    fclose(input);
    REQUIRE(decoded != nullptr);
    CHECK(w == 2); CHECK(h == 2);
    const std::vector<uint8_t> topDown{255,0,0, 0,255,0, 0,0,255, 255,255,255};
    CHECK(std::vector<uint8_t>(decoded, decoded + 12) == topDown);
    stbi_image_free(decoded);
    CHECK_FALSE(writePngRgb(file, 0, 2, bottomUp));
    CHECK_FALSE(writePngRgb(file, 2, 2, {1,2,3}));
    CHECK_FALSE(writePngRgb(dir / L"missing/test.png", 2, 2, bottomUp));
    std::filesystem::remove_all(dir);
}

TEST_CASE("capture_press_reports_background_rejection_once_and_requires_a_fresh_press") {
    bool previous = false;
    CHECK(captureKeyDecision(false, false, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(true, false, previous) == CaptureKeyDecision::RejectBackground);
    CHECK(captureKeyDecision(true, false, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(true, true, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(false, true, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(true, true, previous) == CaptureKeyDecision::Capture);
    CHECK(captureKeyDecision(true, true, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(false, false, previous) == CaptureKeyDecision::None);
    CHECK(captureKeyDecision(true, true, previous) == CaptureKeyDecision::Capture);
}

namespace {
std::map<GLenum, GLint> pack;
void WINAPI captureInteger(GLenum name, GLint* out) {
    if (name == GL_VIEWPORT) { out[0] = 7; out[1] = 11; out[2] = 3; out[3] = 2; }
    else *out = pack.at(name);
}
void WINAPI captureStore(GLenum name, GLint value) { pack[name] = value; }
void WINAPI captureBuffer(GLenum name) { pack[GL_READ_BUFFER] = name; }
void WINAPI capturePixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
    CHECK(x == 7); CHECK(y == 11); CHECK(w == 3); CHECK(h == 2);
    CHECK(format == GL_RGB); CHECK(type == GL_UNSIGNED_BYTE);
    CHECK(pack[GL_READ_BUFFER] == GL_BACK);
    CHECK(pack[GL_PACK_ALIGNMENT] == 1);
    CHECK(pack[GL_PACK_ROW_LENGTH] == 0);
    CHECK(pack[GL_PACK_SKIP_ROWS] == 0); CHECK(pack[GL_PACK_SKIP_PIXELS] == 0);
    std::memset(data, 42, 18);
}
}
TEST_CASE("capture_uses_viewport_size") {
    pack = {{GL_PACK_ALIGNMENT,8}, {GL_PACK_ROW_LENGTH,9}, {GL_PACK_SKIP_ROWS,1},
            {GL_PACK_SKIP_PIXELS,2}, {GL_READ_BUFFER,GL_FRONT}};
    const auto saved = pack;
    GlApi api{};
    api.getIntegerv = captureInteger; api.pixelStorei = captureStore;
    api.readBuffer = captureBuffer; api.readPixels = capturePixels;
    const auto image = captureViewport(api);
    CHECK(image.w == 3); CHECK(image.h == 2);
    CHECK(image.rgb == std::vector<uint8_t>(18, 42));
    CHECK(pack == saved);
    GlApi missing{};
    CHECK(captureViewport(missing).rgb.empty());
}

TEST_CASE("frametime_ring_wraps") {
    FrameTimeRing ring;
    CHECK(ring.values().empty());
    for (int i = 0; i < 10007; ++i) ring.push(i + 0.25);
    const auto values = ring.values();
    REQUIRE(values.size() == 10000);
    CHECK(values.front() == 7.25);
    CHECK(values.back() == 10006.25);
    CHECK(values[500] == 507.25);
}
