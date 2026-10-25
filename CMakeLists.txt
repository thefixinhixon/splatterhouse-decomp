# splatterhouse - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(splatterhouse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Discover the SDK from the vendored submodule; install or REXSDK_DIR overrides.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/rexglue-sdk/CMakeLists.txt"
    AND NOT DEFINED REXSDK_DIR)
  set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/rexglue-sdk" CACHE PATH
      "Path to ReXGlue SDK source tree")
endif()

include(generated/rexglue.cmake)

# Sources
set(SPLATTERHOUSE_SOURCES
    src/main.cpp
    src/sh_60fps_hook.cpp
    src/sh_language_hook.cpp
    src/sh_unlock_hook.cpp
    src/sh_warp_hook.cpp
    src/sh_uf_diag.cpp
    src/sh_overlapped_fix.cpp
    src/sh_xctd_cache_fix.cpp
)

if(WIN32)
    add_executable(splatterhouse WIN32 ${SPLATTERHOUSE_SOURCES})
else()
    add_executable(splatterhouse ${SPLATTERHOUSE_SOURCES})
endif()

# GPU_PLUGINS xenos: builds and copies the Xenos graphics plugin (D3D12/Vulkan)
rexglue_setup_target(splatterhouse GPU_PLUGINS xenos)

# On Linux the SDK only stages the GPU plugin when the executable relinks and
# never stages the runtime libraries, so an SDK-only change would leave stale
# copies next to the executable. Stage them on every build.
if(UNIX AND NOT APPLE)
    set(_splatterhouse_staged_libs)
    foreach(_lib rexruntime rexgpu-xenos TracyClient)
        if(TARGET ${_lib})
            list(APPEND _splatterhouse_staged_libs $<TARGET_FILE:${_lib}>)
        endif()
    endforeach()
    add_custom_target(splatterhouse_stage_libs
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_splatterhouse_staged_libs}
            $<TARGET_FILE_DIR:splatterhouse>
        VERBATIM)
    add_dependencies(splatterhouse_stage_libs rexruntime rexgpu-xenos)
    add_dependencies(splatterhouse splatterhouse_stage_libs)
endif()

# generate_exception_handlers emits SEH_TRY/SEH_CATCH, but the SDK's generated
# precompiled header doesn't include the header that defines them.
if(TARGET splatterhouse_recomp)
    target_precompile_headers(splatterhouse_recomp PRIVATE <rex/platform/exceptions.h>)
endif()

# The host compiles the SDK's rex_app.cpp, which includes imgui.h directly.
target_include_directories(splatterhouse PRIVATE extern/rexglue-sdk/thirdparty/imgui)
