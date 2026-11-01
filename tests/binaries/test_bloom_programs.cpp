#include <doctest/doctest.h>
#include "bloom_trace.h"
#include "shader_rewrite.h"
#include "patch_table.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

TEST_CASE("reference_bloom_shaders_crc_and_existing_rewrites_preserve_math") {
    const auto root=_wgetenv(L"RIDDICK_DIR");
    if(!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR is unset; no bloom shaders read"); return; }
    const std::pair<const wchar_t*,std::uint32_t> programs[]{
        {L"XREngine_Blur0.fp",0xCC2FE5F9},{L"XREngine_Blur1.fp",0xBE7E5F79},
        {L"WClientMod_ScreenFX0.fp",0xDB80471D},{L"WClientMod_ScreenFX1.fp",0xE3D4D1B5},
        {L"WClientMod_ScreenFX2.fp",0x62E11CB4},{L"WClientMod_ScreenFX3.fp",0x478D5FF9},
        {L"WClientMod_ScreenFX4.fp",0x2590A258}
    };
    for(const auto& p:programs) {
        const auto file=std::filesystem::path(root)/L"Gl/ARB_Fragment_Program"/p.first;
        CAPTURE(file.u8string());
        std::ifstream in(file,std::ios::binary); REQUIRE(in.is_open());
        const std::string bytes{std::istreambuf_iterator<char>(in),{}};
        CHECK(shaderProgramCrc32(bytes)==p.second);
        CHECK(rewriteFragmentProgram(bytes,true,false)==bytes);
        CHECK(rewriteFragmentProgram(bytes,true,true)==bytes);
    }
}

TEST_CASE("reference_bloom_numeric_constants_in_x86_and_x64_are_read_only") {
    const auto root=_wgetenv(L"RIDDICK_DIR");
    if(!root || !*root) { MESSAGE("SKIPPED: RIDDICK_DIR is unset; no bloom binaries read"); return; }
    struct Constant {const wchar_t* module;std::uint32_t rva;float expected;};
    const Constant constants[]{
        {L"System/Win64_AMD64/MXR.dll",0x1FB0BC,255.0f},
        {L"System/Win32_x86_SSE2/MXR.dll",0x15AC8C,1.0f/255},
        {L"System/Win64_AMD64/MXR.dll",0x1FFC28,160.0f/255},
        {L"System/Win32_x86_SSE2/MXR.dll",0x15CBE4,160.0f/255},
        {L"Content/GameWorld_Win64_AMD64.dll",0x1DE200,256.0f},
        {L"Content/GameWorld_Win32_x86_SSE2.dll",0x149C5C,256.0f},
        {L"Content/GameWorld_Win64_AMD64.dll",0x1DE204,128.0f},
        {L"Content/GameWorld_Win32_x86_SSE2.dll",0x149C68,128.0f},
        {L"Content/GameWorld_Win64_AMD64.dll",0x1DE208,112.0f},
        {L"Content/GameWorld_Win32_x86_SSE2.dll",0x149C64,112.0f},
    };
    for(const auto& c:constants) {
        const auto file=std::filesystem::path(root)/c.module;
        CAPTURE(file.u8string()); CAPTURE(c.rva);
        std::ifstream in(file,std::ios::binary); REQUIRE(in.is_open());
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};
        const auto offset=rvaToFileOffset(bytes.data(),bytes.size(),c.rva);
        REQUIRE(offset.has_value()); REQUIRE(*offset+sizeof(float)<=bytes.size());
        float value=0; std::memcpy(&value,bytes.data()+*offset,sizeof(value));
        CHECK(value==c.expected);
    }
}
