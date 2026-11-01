// Derived from TCoRFix v1.4 by Nezarn (MIT).
#include "gl_extensions.h"
#include <algorithm>
#include <array>

namespace {
constexpr std::array<std::string_view, 26> requiredExtensions = {
    "GL_ARB_fragment_program",
    "GL_ARB_multitexture",
    "GL_ARB_texture_compression",
    "GL_ARB_texture_cube_map",
    "GL_ARB_texture_env_combine",
    "GL_ARB_texture_env_dot3",
    "GL_ARB_vertex_buffer_object",
    "GL_ARB_vertex_program",
    "GL_EXT_bgra",
    "GL_EXT_secondary_color",
    "GL_EXT_stencil_two_side",
    "GL_EXT_stencil_wrap",
    "GL_EXT_texture_compression_s3tc",
    "GL_EXT_texture_filter_anisotropic",
    "GL_EXT_texture_lod_bias",
    "GL_NV_copy_depth_to_color",
    "GL_NV_fence",
    "GL_NV_fragment_program",
    "GL_NV_fragment_program_option",
    "GL_NV_fragment_program2",
    "GL_NV_occlusion_query",
    "GL_NV_register_combiners",
    "GL_NV_register_combiners2",
    "GL_NV_texture_shader",
    "GL_NV_vertex_array_range",
    "GL_NV_vertex_program"
};
constexpr std::array<std::string_view, 5> forcedExtensions = {
    "GL_NV_fragment_program", "GL_NV_fragment_program_option", "GL_NV_fragment_program2",
    "GL_NV_occlusion_query", "GL_NV_copy_depth_to_color"
};
bool containsToken(std::string_view list, std::string_view token) {
    size_t pos = 0;
    while ((pos = list.find(token, pos)) != std::string_view::npos) {
        const auto end = pos + token.size();
        if ((pos == 0 || list[pos - 1] == ' ') && (end == list.size() || list[end] == ' ')) return true;
        pos = end;
    }
    return false;
}
}

std::string filterExtensions(std::string_view driverList, bool force20PlusPlus) {
    std::string result;
    const auto append = [&](std::string_view name) {
        if (!result.empty()) result += ' ';
        result += name;
    };
    if (force20PlusPlus) for (auto name : forcedExtensions) append(name);
    for (auto name : requiredExtensions) {
        if (force20PlusPlus && std::find(forcedExtensions.begin(), forcedExtensions.end(), name) != forcedExtensions.end()) continue;
        if (containsToken(driverList, name)) append(name);
    }
    return result;
}
