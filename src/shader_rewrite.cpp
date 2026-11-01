// Derived from TCoRFix v1.4 by Nezarn (MIT).
#include "shader_rewrite.h"
#include <map>

namespace {
const std::map<std::string, std::string> shaderPatches
{
    {"OPTION NV_fragment_program2;", "TEMP tcorfix;"},
    {"SHORT TEMP", "TEMP"},
    {"SHORT OUTPUT", "OUTPUT"},
    {"MULH", "MUL"},
    {"MADH", "MAD"},
    {"MUL_SAT depth, |depth|, depthtolerance.a;", "ABS tcorfix, depth;\nMUL_SAT depth, tcorfix, depthtolerance.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};"},
    {"NRMH NormalMapTexel.rgb, NormalMapTexel;", "DP3 tcorfix.a, NormalMapTexel, NormalMapTexel;\nRSQ tcorfix.a, tcorfix.a;\nMUL NormalMapTexel.rgb, NormalMapTexel, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};"},
    {"NRMH TSLV.rgb, IPTSLV;", "DP3 tcorfix.a, IPTSLV, IPTSLV;\nRSQ tcorfix.a, tcorfix.a;\nMUL TSLV.rgb, IPTSLV, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};"},
    {"NRMH TSEV.rgb, IPTSEV;", "DP3 tcorfix.a, IPTSEV, IPTSEV;\nRSQ tcorfix.a, tcorfix.a;\nMUL TSEV.rgb, IPTSEV, tcorfix.a;\nMOV tcorfix, {0.0, 0.0, 0.0, 0.0};"}
};
bool identifier(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
void replaceWhole(std::string& source, std::string_view from, std::string_view to) {
    size_t pos = 0;
    while ((pos = source.find(from, pos)) != std::string::npos) {
        const auto end = pos + from.size();
        const auto line = source.rfind('\n', pos);
        const auto comment = source.find('#', line == std::string::npos ? 0 : line + 1);
        // _SAT is an opcode modifier used by the shipped NV programs. Keep it
        // while replacing the half opcode, as TCoRFix's table does.
        const bool saturated = (from == "MULH" || from == "MADH") && source.compare(end, 4, "_SAT") == 0 &&
            (end + 4 == source.size() || !identifier(source[end + 4]));
        if ((pos && identifier(source[pos - 1])) ||
            (end < source.size() && identifier(from.back()) && identifier(source[end]) && !saturated) || comment < pos) {
            pos = end;
            continue;
        }
        source.replace(pos, from.size(), to);
        pos += to.size();
    }
}
}

std::string rewriteFragmentProgram(std::string_view src, bool any20PlusPlus, bool flickerWorkaround) {
    std::string result(src);
    if (any20PlusPlus) for (const auto& patch : shaderPatches) replaceWhole(result, patch.first, patch.second);
    if (flickerWorkaround) replaceWhole(result, "MUL r0.rgb, r0, ShadowMaskTexel.a;", "#MUL r0.rgb, r0, ShadowMaskTexel.a;");
    return result;
}
