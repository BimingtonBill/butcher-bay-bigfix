cmake_minimum_required(VERSION 3.20)
project(BigFix VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "BigFix requires a 64-bit Windows target.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Test executables stand in for the 2004 game, which never enables CET shadow
# stacks; fixture detours rewrite return paths that a shadow stack rejects.
# Newer MSVC toolsets mark executables CET-compatible by default.
if(MSVC)
    add_link_options("$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/CETCOMPAT:NO>")
endif()

include(FetchContent)
# The pinned dependencies declare old policy baselines. Keep them compatible
# with CMake 4 and retain CMP0091 so they honor the static CRT selection.
set(CMAKE_POLICY_VERSION_MINIMUM 3.20)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG 8af6b4acae5a9388fd742b56fa79ece89d96f823
)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook doctest)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin_main.cpp")
add_library(bigfix_core STATIC ${CORE_SOURCES})
target_include_directories(bigfix_core PUBLIC src)
target_link_libraries(bigfix_core PUBLIC bcrypt minhook version dxguid)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/installer/dsoal-release.json" BIGFIX_DSOAL_METADATA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/installer/dsoal-release.json")
string(JSON BIGFIX_DSOAL_SHA256 GET "${BIGFIX_DSOAL_METADATA}" files 0 sha256)
target_compile_definitions(bigfix_core PRIVATE "BIGFIX_DSOAL_SHA256=\"${BIGFIX_DSOAL_SHA256}\"")

add_library(BigFix SHARED src/plugin_main.cpp)
target_link_libraries(BigFix PRIVATE bigfix_core)
set_target_properties(BigFix PROPERTIES PREFIX "" SUFFIX ".asi")

enable_testing()
add_library(bigfix_widescreen_image SHARED tests/plugin/widescreen_image.cpp)
add_executable(bigfix_widescreen tests/plugin/test_widescreen.cpp)
target_link_libraries(bigfix_widescreen PRIVATE bigfix_core)
add_dependencies(bigfix_widescreen bigfix_widescreen_image bigfix_gl_stub)
foreach(scenario on off mismatch callers menu_mismatch all_mismatch foreign_util)
    add_test(NAME bigfix_widescreen_${scenario} COMMAND bigfix_widescreen $<TARGET_FILE:bigfix_widescreen_image> $<TARGET_FILE:bigfix_gl_stub> ${scenario})
    set_tests_properties(bigfix_widescreen_${scenario} PROPERTIES TIMEOUT 30)
endforeach()
add_library(bigfix_msaa_stub SHARED tests/plugin/msaa_stub.cpp)
target_link_libraries(bigfix_msaa_stub PRIVATE opengl32 gdi32 user32)
add_executable(bigfix_msaa_plugin tests/plugin/test_msaa_plugin.cpp)
target_link_libraries(bigfix_msaa_plugin PRIVATE bigfix_core opengl32 gdi32 user32)
add_dependencies(bigfix_msaa_plugin bigfix_msaa_stub)
foreach(scenario on off advanced cold advanced_cold lower none no_arb no_extension advanced_no_extension coexist set_only make_failure rgb24 advanced_depth32 bad_stencil quiet_none)
    add_test(NAME bigfix_msaa_${scenario} COMMAND bigfix_msaa_plugin $<TARGET_FILE:bigfix_msaa_stub> ${scenario})
    set_tests_properties(bigfix_msaa_${scenario} PROPERTIES TIMEOUT 30)
endforeach()
add_executable(bigfix_raw_mouse tests/plugin/test_raw_mouse.cpp)
target_link_libraries(bigfix_raw_mouse PRIVATE bigfix_core user32)
add_dependencies(bigfix_raw_mouse bigfix_video_image)
foreach(scenario on off mismatch trace off_trace registration_failure limit)
    add_test(NAME bigfix_raw_mouse_${scenario} COMMAND bigfix_raw_mouse
        $<TARGET_FILE:bigfix_video_image> ${scenario})
    set_tests_properties(bigfix_raw_mouse_${scenario} PROPERTIES TIMEOUT 30)
endforeach()
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/*.cpp" "tests/binaries/*.cpp")
add_executable(bigfix_tests ${UNIT_TEST_SOURCES})
target_link_libraries(bigfix_tests PRIVATE bigfix_core doctest::doctest)
add_test(NAME bigfix_tests COMMAND bigfix_tests)

# A real import dependency exercises loads that do not call LoadLibrary directly.
add_library(bigfix_watch_dependency SHARED tests/plugin/module_watch_dependency.cpp)
add_library(bigfix_watch_parent SHARED tests/plugin/module_watch_parent.cpp)
target_link_libraries(bigfix_watch_parent PRIVATE bigfix_watch_dependency)
add_dependencies(bigfix_tests bigfix_watch_parent)
target_compile_definitions(bigfix_tests PRIVATE "BIGFIX_TEST_PARENT_PATH=L\"$<TARGET_FILE:bigfix_watch_parent>\"")

add_executable(bigfix_plugin_startup tests/plugin/test_plugin_startup.cpp)
target_link_libraries(bigfix_plugin_startup PRIVATE bigfix_core)
add_dependencies(bigfix_plugin_startup BigFix)
add_test(NAME bigfix_plugin_startup COMMAND bigfix_plugin_startup $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_window_stub>)
set_tests_properties(bigfix_plugin_startup PROPERTIES TIMEOUT 20)
foreach(scenario fallback rotation disabled)
    add_test(NAME bigfix_plugin_log_${scenario} COMMAND bigfix_plugin_startup
        $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_window_stub> ${scenario})
    set_tests_properties(bigfix_plugin_log_${scenario} PROPERTIES TIMEOUT 20)
endforeach()

# Artificial mapped function bodies at the real video RVAs, plus fake GL. No
# game executable or DLL is loaded, even with RIDDICK_DIR set.
add_library(bigfix_video_image SHARED tests/plugin/video_image.cpp)
add_executable(bigfix_startup_logos tests/plugin/test_startup_logos.cpp)
target_link_libraries(bigfix_startup_logos PRIVATE bigfix_core)
add_dependencies(bigfix_startup_logos bigfix_video_image)
foreach(scenario on off mismatch)
    add_test(NAME bigfix_startup_logos_${scenario} COMMAND bigfix_startup_logos
        $<TARGET_FILE:bigfix_video_image> ${scenario})
    set_tests_properties(bigfix_startup_logos_${scenario} PROPERTIES TIMEOUT 20)
endforeach()
add_executable(bigfix_video_trace tests/plugin/test_video_trace.cpp)
target_link_libraries(bigfix_video_trace PRIVATE bigfix_core)
add_dependencies(bigfix_video_trace bigfix_video_image bigfix_gl_stub)
foreach(scenario on off mismatch limit priority)
    add_test(NAME bigfix_video_${scenario} COMMAND bigfix_video_trace
        $<TARGET_FILE:bigfix_video_image> $<TARGET_FILE:bigfix_gl_stub> ${scenario})
    set_tests_properties(bigfix_video_${scenario} PROPERTIES TIMEOUT 30)
endforeach()

# Recorded video geometry through real detours, with a hidden native GL context
# for black-buffer and state-restoration checks. No game code is loaded.
add_executable(bigfix_video_aspect tests/plugin/test_video_aspect.cpp)
target_link_libraries(bigfix_video_aspect PRIVATE bigfix_core opengl32 gdi32 user32)
add_dependencies(bigfix_video_aspect bigfix_video_image)
foreach(scenario on off trace off_trace)
    add_test(NAME bigfix_video_aspect_${scenario} COMMAND bigfix_video_aspect
        $<TARGET_FILE:bigfix_video_image> ${scenario})
    set_tests_properties(bigfix_video_aspect_${scenario} PROPERTIES TIMEOUT 30)
endforeach()

# A fake GL DLL exercises the actual ASI detours without a GPU or game process.
add_library(bigfix_gl_stub SHARED tests/plugin/gl_stub.cpp tests/plugin/gl_stub.def)
set_target_properties(bigfix_gl_stub PROPERTIES OUTPUT_NAME opengl32
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gl_stub")
add_executable(bigfix_gl_plugin tests/plugin/test_gl_plugin.cpp)
target_link_libraries(bigfix_gl_plugin PRIVATE bigfix_core)
add_dependencies(bigfix_gl_plugin BigFix bigfix_gl_stub)
add_executable(bigfix_shadow_trace tests/plugin/test_shadow_trace.cpp)
target_link_libraries(bigfix_shadow_trace PRIVATE bigfix_core)
add_dependencies(bigfix_shadow_trace bigfix_gl_stub)
foreach(scenario on off coexist)
    add_test(NAME bigfix_shadow_${scenario} COMMAND bigfix_shadow_trace $<TARGET_FILE:bigfix_gl_stub> ${scenario})
    set_tests_properties(bigfix_shadow_${scenario} PROPERTIES TIMEOUT 30)
endforeach()
add_executable(bigfix_eyeshine_fix tests/plugin/test_eyeshine_fix.cpp)
add_library(bigfix_eyeshine_gl_fixture SHARED tests/plugin/eyeshine_gl_fixture.cpp tests/plugin/eyeshine_gl_fixture.def)
target_link_libraries(bigfix_eyeshine_gl_fixture PRIVATE opengl32)
add_executable(bigfix_eyeshine_variants tests/plugin/test_eyeshine_variants.cpp)
add_executable(bigfix_eyeshine_scale tests/plugin/test_eyeshine_scale.cpp tests/plugin/eyeshine_scale_fixture.cpp)
target_link_libraries(bigfix_eyeshine_scale PRIVATE bigfix_core)
add_dependencies(bigfix_eyeshine_scale bigfix_video_image)
foreach(scenario on off fingerprint bytes)
    add_test(NAME bigfix_eyeshine_scale_${scenario} COMMAND bigfix_eyeshine_scale $<TARGET_FILE:bigfix_video_image> ${scenario})
    set_tests_properties(bigfix_eyeshine_scale_${scenario} PROPERTIES TIMEOUT 20)
endforeach()
target_link_libraries(bigfix_eyeshine_variants PRIVATE bigfix_core opengl32 gdi32 user32)
foreach(variant RANGE 0 6)
    add_test(NAME bigfix_eyeshine_variant_${variant} COMMAND bigfix_eyeshine_variants ${variant})
    set_tests_properties(bigfix_eyeshine_variant_${variant} PROPERTIES TIMEOUT 30)
endforeach()
target_link_libraries(bigfix_eyeshine_fix PRIVATE bigfix_core opengl32 gdi32 user32)
add_dependencies(bigfix_eyeshine_fix bigfix_eyeshine_gl_fixture)
foreach(scenario off coexist variant0 variant7 bloom)
    add_test(NAME bigfix_eyeshine_${scenario} COMMAND bigfix_eyeshine_fix $<TARGET_FILE:bigfix_eyeshine_gl_fixture> ${scenario})
    set_tests_properties(bigfix_eyeshine_${scenario} PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endforeach()
add_executable(bigfix_eyeshine_region tests/plugin/test_eyeshine_region.cpp)
target_link_libraries(bigfix_eyeshine_region PRIVATE bigfix_core opengl32 gdi32 user32)
add_dependencies(bigfix_eyeshine_region bigfix_eyeshine_gl_fixture)
foreach(scenario on off variant0 variant8)
    add_test(NAME bigfix_eyeshine_region_${scenario} COMMAND bigfix_eyeshine_region $<TARGET_FILE:bigfix_eyeshine_gl_fixture> ${scenario})
    set_tests_properties(bigfix_eyeshine_region_${scenario} PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endforeach()
foreach(phase early late)
    add_test(NAME bigfix_gl_plugin_${phase} COMMAND bigfix_gl_plugin
        $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_gl_stub> ${phase})
    set_tests_properties(bigfix_gl_plugin_${phase} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(bigfix_bloom_trace tests/plugin/test_bloom_trace.cpp)
target_link_libraries(bigfix_bloom_trace PRIVATE bigfix_core)
add_dependencies(bigfix_bloom_trace bigfix_gl_stub)
foreach(scenario on off combined coexist limit)
    add_test(NAME bigfix_bloom_${scenario} COMMAND bigfix_bloom_trace $<TARGET_FILE:bigfix_gl_stub> ${scenario})
    set_tests_properties(bigfix_bloom_${scenario} PROPERTIES TIMEOUT 30)
endforeach()

# A fake DirectSound8 DLL exercises the background-audio export and vtable
# hooks without an audio device or game process.
add_library(bigfix_dsound_stub SHARED tests/plugin/dsound_stub.cpp tests/plugin/dsound_stub.def)
target_link_libraries(bigfix_dsound_stub PRIVATE dxguid)
set_target_properties(bigfix_dsound_stub PROPERTIES OUTPUT_NAME DSOUND
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dsound_stub")
add_executable(bigfix_background_audio_plugin tests/plugin/test_background_audio.cpp)
target_link_libraries(bigfix_background_audio_plugin PRIVATE bigfix_core)
add_dependencies(bigfix_background_audio_plugin BigFix bigfix_dsound_stub)
foreach(scenario on off eax_only quiet limit)
    add_test(NAME bigfix_background_audio_${scenario} COMMAND bigfix_background_audio_plugin
        $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_dsound_stub> ${scenario})
    set_tests_properties(bigfix_background_audio_${scenario} PROPERTIES TIMEOUT 20)
endforeach()

set(BIGFIX_DSOAL_DIR "" CACHE PATH "Optional directory containing the pinned Win64 DSOAL pair (no game files)")
add_executable(bigfix_dsoal_api tests/plugin/test_dsoal_api.cpp)
target_link_libraries(bigfix_dsoal_api PRIVATE bigfix_core user32)
foreach(scenario on off missing loopback)
    add_test(NAME bigfix_dsoal_${scenario} COMMAND bigfix_dsoal_api "${BIGFIX_DSOAL_DIR}" ${scenario})
    set_tests_properties(bigfix_dsoal_${scenario} PROPERTIES TIMEOUT 40 SKIP_RETURN_CODE 77)
endforeach()

# No game DLL is loaded: this renderer imports the same Win32 APIs and the
# decorated MSystem selection boundary found by static RE.
add_library(bigfix_mode_stub SHARED tests/plugin/mode_stub.cpp)
set_target_properties(bigfix_mode_stub PROPERTIES OUTPUT_NAME MSystem)
add_library(bigfix_window_stub SHARED tests/plugin/window_stub.cpp)
target_link_libraries(bigfix_window_stub PRIVATE bigfix_mode_stub user32)
set_target_properties(bigfix_window_stub PROPERTIES OUTPUT_NAME RndrGL
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/window_stub")
add_executable(bigfix_window_plugin tests/plugin/test_window_plugin.cpp)
target_link_libraries(bigfix_window_plugin PRIVATE user32)
add_dependencies(bigfix_window_plugin BigFix bigfix_window_stub)
add_dependencies(bigfix_plugin_startup bigfix_window_stub)
foreach(phase early late)
    foreach(policy borderless fullscreen windowed)
        add_test(NAME bigfix_window_${policy}_${phase} COMMAND bigfix_window_plugin
            $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_window_stub> ${phase} ${policy})
        set_tests_properties(bigfix_window_${policy}_${phase} PROPERTIES TIMEOUT 20)
    endforeach()
endforeach()
add_executable(bigfix_capture_plugin tests/plugin/test_capture_plugin.cpp)
target_link_libraries(bigfix_capture_plugin PRIVATE bigfix_core)
add_dependencies(bigfix_capture_plugin BigFix)
add_test(NAME bigfix_capture_plugin COMMAND bigfix_capture_plugin $<TARGET_FILE:BigFix>)
set_tests_properties(bigfix_capture_plugin PROPERTIES TIMEOUT 60)

add_executable(bigfix_first_frame tests/plugin/test_first_frame.cpp)
target_link_libraries(bigfix_first_frame PRIVATE bigfix_core)
add_dependencies(bigfix_first_frame bigfix_gl_stub)
add_test(NAME bigfix_first_frame COMMAND bigfix_first_frame $<TARGET_FILE:bigfix_gl_stub>)
set_tests_properties(bigfix_first_frame PROPERTIES TIMEOUT 20)

# The production installer with controlled fingerprint inputs and a live patch
# address in the harness, so native-window coverage does not require game code.
add_executable(bigfix_window_prerequisites tests/plugin/test_window_plugin.cpp tests/plugin/window_policy_fixture.cpp)
target_include_directories(bigfix_window_prerequisites PRIVATE src)
target_compile_definitions(bigfix_window_prerequisites PRIVATE BIGFIX_WINDOW_HARNESS)
target_link_libraries(bigfix_window_prerequisites PRIVATE bigfix_core user32)
add_dependencies(bigfix_window_prerequisites BigFix bigfix_window_stub)
foreach(scenario success fingerprint bytes address write_failure)
    add_test(NAME bigfix_window_prerequisite_${scenario} COMMAND bigfix_window_prerequisites
        $<TARGET_FILE:BigFix> $<TARGET_FILE:bigfix_window_stub> late borderless ${scenario})
    set_tests_properties(bigfix_window_prerequisite_${scenario} PROPERTIES TIMEOUT 20)
endforeach()
