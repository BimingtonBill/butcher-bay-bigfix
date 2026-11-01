// Extension names derived from TCoRFix v1.4 by Nezarn (MIT).
#include <doctest/doctest.h>
#include "gl_extensions.h"

TEST_CASE("token_match_not_substring") {
    CHECK(filterExtensions("GL_ARB_multitexture_foo GL_EXT_bgra", false) == "GL_EXT_bgra");
}
TEST_CASE("required_order_preserved") {
    CHECK(filterExtensions("GL_NV_vertex_program GL_EXT_bgra GL_ARB_multitexture GL_EXT_bgra", false) ==
          "GL_ARB_multitexture GL_EXT_bgra GL_NV_vertex_program");
}
TEST_CASE("force_adds_nv_first_without_duplicates") {
    CHECK(filterExtensions("GL_NV_fragment_program GL_EXT_bgra GL_NV_fragment_program2", true) ==
          "GL_NV_fragment_program GL_NV_fragment_program_option GL_NV_fragment_program2 GL_NV_occlusion_query GL_NV_copy_depth_to_color GL_EXT_bgra");
}
TEST_CASE("long_driver_list_ok") {
    const std::string driver = std::string(20000, 'x') + " GL_EXT_bgra ";
    CHECK(filterExtensions(driver, false) == "GL_EXT_bgra");
    CHECK(filterExtensions("", false).empty());
}
TEST_CASE("all_26_required_extensions_are_kept_exactly") {
    const std::string required = "GL_ARB_fragment_program GL_ARB_multitexture GL_ARB_texture_compression GL_ARB_texture_cube_map GL_ARB_texture_env_combine GL_ARB_texture_env_dot3 GL_ARB_vertex_buffer_object GL_ARB_vertex_program GL_EXT_bgra GL_EXT_secondary_color GL_EXT_stencil_two_side GL_EXT_stencil_wrap GL_EXT_texture_compression_s3tc GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias GL_NV_copy_depth_to_color GL_NV_fence GL_NV_fragment_program GL_NV_fragment_program_option GL_NV_fragment_program2 GL_NV_occlusion_query GL_NV_register_combiners GL_NV_register_combiners2 GL_NV_texture_shader GL_NV_vertex_array_range GL_NV_vertex_program";
    CHECK(filterExtensions(required, false) == required);
}
