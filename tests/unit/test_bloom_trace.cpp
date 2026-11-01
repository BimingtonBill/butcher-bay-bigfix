#include <doctest/doctest.h>
#include "bloom_trace.h"
#include "config.h"
#include <vector>
#include <stdexcept>

namespace {
std::vector<std::string> output;
unsigned queries = 0;
GLenum unit = 0x84C2;
GLuint program = 7;
void sink(std::string_view line) { output.emplace_back(line); }
void WINAPI integer(GLenum name, GLint* out) {
    ++queries;
    if (name == 0x84E0) *out = unit;
    else if (name == GL_VIEWPORT) { out[0]=0; out[1]=0; out[2]=1280; out[3]=720; }
    else if (name == GL_TEXTURE_BINDING_2D) *out = unit - 0x84C0 + 10;
    else *out = 1;
}
GLboolean WINAPI enabled(GLenum) { return GL_TRUE; }
void WINAPI active(GLenum value) { unit = value; }
void WINAPI level(GLenum, GLint, GLenum name, GLint* out) {
    *out = name == GL_TEXTURE_INTERNAL_FORMAT ? GL_RGBA8 : 256;
}
void WINAPI getProgram(GLenum, GLenum, GLint* out) { *out = program; }
void WINAPI env(GLenum, GLuint index, GLfloat* out) {
    out[0]=float(index); out[1]=2; out[2]=3; out[3]=4;
}
BloomApi api() {
    BloomApi a;
    a.getInteger=integer; a.isEnabled=enabled; a.activeTexture=active;
    a.getTexLevel=level; a.getProgram=getProgram; a.getEnv=env;
    return a;
}
void WINAPI failedLevel(GLenum,GLint,GLenum,GLint*) { throw std::runtime_error("injected query failure"); }
}

TEST_CASE("bloom_config_is_opt_in") {
    CHECK_FALSE(Settings{}.traceBloom);
    std::vector<std::string> warnings;
    CHECK(parseSettings("[BigFix]\nTraceBloom=1\n", &warnings).traceBloom);
    CHECK(warnings.empty());
}

TEST_CASE("bloom_trace_disabled_does_not_query_or_log") {
    queries=0; output.clear();
    BloomTrace trace(false,sink);
    trace.upload(api(),0x8804,"123456789","123456789");
    trace.draw(api(),"draw",nullptr);
    trace.swap(0,nullptr,TRUE);
    CHECK(queries == 0);
    CHECK(output.empty());
}

TEST_CASE("bloom_trace_associates_crc_with_context_program_and_restores_active_texture") {
    output.clear(); unit=0x84C2; program=7;
    BloomTrace first(true,sink), second(true,sink);
    first.upload(api(),0x8804,"123456789","rewritten");
    first.draw(api(),"draw",nullptr);
    first.swap(100,nullptr,TRUE);
    REQUIRE(output.size() >= 3);
    CHECK(output[0].find("sourceCRC=CBF43926") != std::string::npos);
    CHECK(output[1].find("fp=7:CBF43926") != std::string::npos);
    CHECK(output[1].find("env7=7,2,3,4") != std::string::npos);
    CHECK(output[1].find("tex3=13:256x256:8058") != std::string::npos);
    CHECK(unit == 0x84C2);
    output.clear();
    second.draw(api(),"other",nullptr);
    second.swap(100,nullptr,TRUE);
    CHECK(output[0].find("fp=7:unknown") != std::string::npos);
    output.clear();
    first.upload(api(),0x8804,"changed","changed");
    first.draw(api(),"reuse",nullptr);
    first.swap(200,nullptr,TRUE);
    CHECK(output[1].find("CBF43926") == std::string::npos);
}

TEST_CASE("bloom_trace_keeps_frame_tail_and_samples_without_queries_between_frames") {
    output.clear(); BloomTrace trace(true,sink);
    for(unsigned i=0;i<60;++i) trace.draw(api(),("draw"+std::to_string(i)).c_str(),nullptr);
    trace.swap(100,nullptr,TRUE);
    REQUIRE(output.size() == 49);
    CHECK(output.front().find("draw12") != std::string::npos);
    CHECK(output[47].find("draw59") != std::string::npos);
    CHECK(output.back().find("omitted=12") != std::string::npos);
    trace.swap(200,nullptr,TRUE); trace.swap(300,nullptr,TRUE);
    queries=0; output.clear();
    trace.draw(api(),"unsampled",nullptr); trace.swap(400,nullptr,TRUE);
    CHECK(queries == 0); CHECK(output.empty());
    trace.swap(1200,nullptr,TRUE);
    trace.draw(api(),"sampled",nullptr); trace.swap(1300,nullptr,TRUE);
    CHECK(queries > 0); CHECK_FALSE(output.empty());
}

TEST_CASE("bloom_trace_restores_server_unit_and_contains_diagnostic_exceptions") {
    output.clear(); unit=0x84C2;
    BloomTrace trace(true,sink);
    auto failing=api(); failing.getTexLevel=failedLevel;
    CHECK_NOTHROW(trace.draw(failing,"failure",nullptr));
    CHECK(unit==0x84C2);
    queries=0;
    trace.draw(api(),"disabled_after_failure",nullptr);
    CHECK(queries==0);
}
