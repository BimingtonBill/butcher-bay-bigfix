#include <doctest/doctest.h>
#include "config.h"
#include "shader_rewrite.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>

TEST_CASE("reference_nv_programs_rewrite_idempotently_without_half_opcodes") {
    const auto root = _wgetenv(L"RIDDICK_DIR");
    if (!root || !*root) {
        MESSAGE("SKIPPED: RIDDICK_DIR is unset; no reference shaders read");
        return;
    }
    const auto folder = std::filesystem::path(root) / L"Gl/NV_Fragment_Program2";
    REQUIRE(std::filesystem::is_directory(folder));
    const std::regex unsupported(R"((^|[;\n])\s*(SHORT\s+(TEMP|OUTPUT)|MULH(_SAT)?\b|MADH(_SAT)?\b|NRMH\b|OPTION\s+NV_fragment_program2))");
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != L".fp") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        REQUIRE(input.is_open());
        const std::string source{std::istreambuf_iterator<char>(input), {}};
        const auto result = rewriteFragmentProgram(source, true, false);
        CHECK_FALSE(std::regex_search(result, unsupported));
        CHECK(rewriteFragmentProgram(result, true, false) == result);
        ++count;
    }
    CHECK(count > 0);
    MESSAGE("Read-only reference shader verification: ", count, " NV programs");
}
