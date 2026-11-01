// Query/color mappings derived from TCoRFix v1.4 by Nezarn (MIT).
#include <doctest/doctest.h>
#include "gl_hooks.h"
#include <fstream>
#include <iterator>
#include <cstring>

namespace {
GLenum seenName;
GLuint seenId;
GLsizei seenLength;
std::string seenProgram;
int colors, attribs;
const char* driver = "GL_ARB_multitexture_foo GL_EXT_bgra";
const GLubyte* WINAPI getString(GLenum name) { return reinterpret_cast<const GLubyte*>(name == GL_EXTENSIONS ? driver : "fake driver"); }
void WINAPI getInteger(GLenum name, GLint* out) { seenName = name; *out = 32; }
void WINAPI beginQuery(GLenum target, GLuint id) { seenName = target; seenId = id; }
void WINAPI endQuery(GLenum target) { seenName = target; }
void WINAPI genQueries(GLsizei n, GLuint* ids) { seenLength = n; ids[0] = 17; }
void WINAPI deleteQueries(GLsizei n, const GLuint* ids) { seenLength = n; seenId = ids[0]; }
GLboolean WINAPI isQuery(GLuint id) { seenId = id; return GL_TRUE; }
void WINAPI queryIv(GLuint id, GLenum name, GLint* out) { seenId = id; seenName = name; *out = 19; }
void WINAPI queryUiv(GLuint id, GLenum name, GLuint* out) { seenId = id; seenName = name; *out = 23; }
void WINAPI color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    ++colors;
    CHECK(r == 1); CHECK(g == 2); CHECK(b == 3); CHECK(a == 4);
}
void WINAPI attrib(GLuint index, GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    ++attribs; seenId = index;
    CHECK(r == 1); CHECK(g == 2); CHECK(b == 3); CHECK(a == 4);
}
void WINAPI program(GLenum target, GLenum, GLsizei length, const void* text) {
    seenName = target; seenLength = length;
    seenProgram.assign(static_cast<const char*>(text), length);
}
PROC WINAPI getProc(LPCSTR name) {
    if (!std::strcmp(name, "glProgramStringARB")) return reinterpret_cast<PROC>(&program);
    if (!std::strcmp(name, "glVertexAttrib4f")) return reinterpret_cast<PROC>(&attrib);
    if (!std::strcmp(name, "glBeginQueryARB")) return reinterpret_cast<PROC>(&beginQuery);
    if (!std::strcmp(name, "glEndQueryARB")) return reinterpret_cast<PROC>(&endQuery);
    if (!std::strcmp(name, "glGenQueriesARB")) return reinterpret_cast<PROC>(&genQueries);
    if (!std::strcmp(name, "glDeleteQueriesARB")) return reinterpret_cast<PROC>(&deleteQueries);
    if (!std::strcmp(name, "glIsQueryARB")) return reinterpret_cast<PROC>(&isQuery);
    if (!std::strcmp(name, "glGetQueryObjectivARB")) return reinterpret_cast<PROC>(&queryIv);
    if (!std::strcmp(name, "glGetQueryObjectuivARB")) return reinterpret_cast<PROC>(&queryUiv);
    return nullptr;
}
GlApi fake() {
    GlApi api{};
    api.getString = &getString; api.getIntegerv = &getInteger;
    api.color4f = &color; api.getProcAddress = &getProc;
    return api;
}
}

TEST_CASE("max_texture_units_remapped") {
    auto api = fake(); Settings settings; GlHooks hooks(api, settings);
    GLint value = 0;
    hooks.getIntegerv(0x84E2, &value);
    CHECK(seenName == 0x8872); CHECK(value == 32);
    hooks.getIntegerv(GL_MAX_TEXTURE_SIZE, &value); CHECK(seenName == GL_MAX_TEXTURE_SIZE);
    settings.fixMiscBugs = false; GlHooks disabled(api, settings);
    disabled.getIntegerv(0x84E2, &value); CHECK(seenName == 0x84E2);
}
TEST_CASE("extensions_pointer_stays_valid_and_non_extensions_pass_through") {
    auto api = fake(); Settings settings; settings.enable20PlusPlusAnyGpu = false;
    GlHooks hooks(api, settings);
    const auto first = hooks.getString(GL_EXTENSIONS);
    CHECK(std::string(reinterpret_cast<const char*>(first)) == "GL_EXT_bgra");
    for (int i = 0; i < 100; ++i) CHECK(hooks.getString(GL_EXTENSIONS) == first);
    CHECK(hooks.getString(GL_VENDOR) == api.getString(GL_VENDOR));
}
TEST_CASE("occlusion_nv_maps_to_arb") {
    auto api = fake(); GlHooks hooks(api, Settings{});
    const char* names[] = {"glGenOcclusionQueriesNV", "glDeleteOcclusionQueriesNV", "glIsOcclusionQueryNV",
        "glBeginOcclusionQueryNV", "glEndOcclusionQueryNV", "glGetOcclusionQueryivNV", "glGetOcclusionQueryuivNV"};
    for (auto name : names) REQUIRE(hooks.getProcAddress(name) != nullptr);
    hooks.beginOcclusionQuery(7); CHECK(seenName == 0x8914); CHECK(seenId == 7);
    hooks.endOcclusionQuery(); CHECK(seenName == 0x8914);
    GLuint id = 0; hooks.genOcclusionQueries(1, &id); CHECK(id == 17); CHECK(seenLength == 1);
    hooks.deleteOcclusionQueries(1, &id); CHECK(seenId == 17);
    CHECK(hooks.isOcclusionQuery(9) == GL_TRUE); CHECK(seenId == 9);
    GLint result = 0; hooks.getOcclusionQueryiv(7, 0x8866, &result); CHECK(result == 19); CHECK(seenName == 0x8866);
    hooks.getOcclusionQueryuiv(8, 0x8867, &id); CHECK(id == 23); CHECK(seenId == 8); CHECK(seenName == 0x8867);
}
TEST_CASE("any_gpu_color_updates_conventional_and_generic_inputs_even_before_program_lookup") {
    auto api = fake(); Settings settings; colors = attribs = 0;
    GlHooks hooks(api, settings); hooks.color4f(1, 2, 3, 4);
    CHECK(attribs == 1); CHECK(colors == 1); CHECK(seenId == 3);
    settings.enable20PlusPlusAnyGpu = false; GlHooks disabled(api, settings);
    disabled.color4f(1, 2, 3, 4); CHECK(colors == 2); CHECK(attribs == 1);
}
TEST_CASE("proc_resolution_is_per_name_gated_and_rejects_wgl_sentinels") {
    auto api = fake(); Settings settings; settings.enable20PlusPlusAnyGpu = false;
    settings.eyeshineFix=false;
    GlHooks hooks(api, settings);
    CHECK(hooks.getProcAddress("glBeginOcclusionQueryNV") == nullptr);
    CHECK(hooks.getProcAddress("glProgramStringARB") == reinterpret_cast<PROC>(&program));
    CHECK(hooks.getProcAddress("glUnknown") == nullptr);
    settings.enable20PlusPlusAnyGpu = true;
    for (auto sentinel : {std::intptr_t(0), std::intptr_t(1), std::intptr_t(2), std::intptr_t(3), std::intptr_t(-1)}) {
        static PROC invalid; invalid = reinterpret_cast<PROC>(sentinel);
        api.getProcAddress = [](LPCSTR) -> PROC { return invalid; };
        GlHooks missing(api, settings);
        CHECK(missing.getProcAddress("glBeginOcclusionQueryNV") == invalid);
    }
}
TEST_CASE("program_hook_uses_explicit_length_and_only_rewrites_fragment_target") {
    auto api = fake(); GlHooks hooks(api, Settings{});
    REQUIRE(hooks.getProcAddress("glProgramStringARB") != nullptr);
    const char source[] = {'M','U','L','H',' ','x',';','X'};
    hooks.programString(0x8804, 0x8875, 7, source);
    CHECK(seenProgram == "MUL x;"); CHECK(seenLength == 6);
    hooks.programString(0x8620, 0x8875, 7, source);
    CHECK(seenProgram == "MULH x;"); CHECK(seenLength == 7);
    const char binary[] = "MULH x;\0MULH y;";
    hooks.programString(0x8804, 0x8875, sizeof(binary) - 1, binary);
    CHECK(seenProgram == std::string("MUL x;\0MUL y;", 13));
}
TEST_CASE("shader_dump_is_module_relative_crc32_and_contains_original_bytes") {
    const auto dir = std::filesystem::temp_directory_path() / (L"BigFix-shaders-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(dir);
    auto api = fake(); Settings settings; settings.dumpShaders = true;
    GlHooks hooks(api, settings, dir);
    REQUIRE(hooks.getProcAddress("glProgramStringARB") != nullptr);
    hooks.programString(0x8804, 0x8875, 9, "123456789trailing");
    std::ifstream input(dir / L"shader_dump/CBF43926.fp", std::ios::binary);
    REQUIRE(input.is_open());
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    CHECK(bytes == "123456789"); input.close();
    std::filesystem::remove_all(dir);
}
