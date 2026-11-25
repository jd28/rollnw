include(FetchContent)

set(ROLLNW_CGLTF_DIR "${CMAKE_SOURCE_DIR}/external/cgltf-1.15")
if(NOT EXISTS "${ROLLNW_CGLTF_DIR}/cgltf.h")
    message(FATAL_ERROR "Vendored cgltf not found at ${ROLLNW_CGLTF_DIR}")
endif()

add_library(nw-render STATIC
    fog_renderer.cpp
    forward_plus_renderer.cpp
    frame_storage_arena.cpp
    model_asset.cpp
    model_texture_cache.cpp
    local_shadow_renderer.cpp
    model_gpu_backend_common.cpp
    model_instance_attachment.cpp
    model_renderer.cpp
    model_pipeline_desc.cpp
    pipeline_cache.cpp
    particle_renderer.cpp
    shadow_culling.cpp
    shadow_renderer.cpp
    shader_provider.cpp
    render_service.cpp
    texture_decode.cpp)
target_include_directories(nw-render PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(nw-render PUBLIC nw nw-gfx)

set(ROLLNW_SHADER_COMPILER_RUNTIME_LIBS "" PARENT_SCOPE)

# Slang is the shader compiler: it consumes the same HLSL sources DXC did and
# emits SPIR-V for the Vulkan backend, and it is the only option that ships
# prebuilt binaries for every platform this project targets. DXC publishes
# Windows-x64 and Linux-x86_64 only -- no macOS build and no arm64 build of
# any kind -- so the Apple port would otherwise require building a fork of
# LLVM 3.7 from source. See issues/closed/shader-toolchain-slang-migration.md.
set(ROLLNW_SLANG_VERSION "2026.22.1" CACHE STRING "Slang release to fetch")

# Per-platform asset name and SHA-256 of the official release archive.
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(ROLLNW_SLANG_ASSET "windows-aarch64.zip")
        set(ROLLNW_SLANG_SHA256 "7c50b36af8428533f524604d10cf84936dbd94d8536940d0611cb754ad4bea7c")
    else()
        set(ROLLNW_SLANG_ASSET "windows-x86_64.zip")
        set(ROLLNW_SLANG_SHA256 "e72344f7b0323cec8b039c534caaf4680e7ba629489fc796ea4c7f7adb7deb68")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(ROLLNW_SLANG_ASSET "macos-aarch64.tar.gz")
        set(ROLLNW_SLANG_SHA256 "1d5370a07be409b94685ea4bd84b7bd191313a6b1fae8795f451009bc0dc3fd7")
    else()
        message(FATAL_ERROR "nw-render: macOS requires Apple Silicon (arm64); Intel macOS (x86_64) is no longer supported")
    endif()
elseif(UNIX)
    # The glibc-tagged archives are built against an older glibc than the
    # untagged ones and are the portable choice for a dev/CI machine.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(ROLLNW_SLANG_ASSET "linux-aarch64-glibc-2.28.tar.gz")
        set(ROLLNW_SLANG_SHA256 "fb603ad10e8558874a298b2a701d212ed135f6e6236cf9a7011563f44de7312f")
    else()
        set(ROLLNW_SLANG_ASSET "linux-x86_64-glibc-2.28.tar.gz")
        set(ROLLNW_SLANG_SHA256 "0c0e243ac15b7878237e6854313f45c791408227d5bc634f3ad1ac487edbfaf5")
    endif()
else()
    message(FATAL_ERROR "nw-render: no Slang release known for this platform")
endif()

set(ROLLNW_SLANG_URL
    "https://github.com/shader-slang/slang/releases/download/v${ROLLNW_SLANG_VERSION}/slang-${ROLLNW_SLANG_VERSION}-${ROLLNW_SLANG_ASSET}"
    CACHE STRING "Slang archive URL")

FetchContent_Declare(slang_prebuilt
    URL      "${ROLLNW_SLANG_URL}"
    URL_HASH "SHA256=${ROLLNW_SLANG_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(slang_prebuilt)

find_path(SLANG_INCLUDE_DIR
    NAMES slang.h
    PATHS "${slang_prebuilt_SOURCE_DIR}/include"
    NO_DEFAULT_PATH)
if(NOT SLANG_INCLUDE_DIR)
    message(FATAL_ERROR "nw-render: slang.h not found in the Slang release archive")
endif()

# Windows links the import library and ships DLLs beside the binary; the
# Unix-likes link the shared object directly.
if(WIN32)
    set(ROLLNW_SLANG_LINK_LIB "${slang_prebuilt_SOURCE_DIR}/lib/slang.lib")
    file(GLOB ROLLNW_SLANG_RUNTIME
        "${slang_prebuilt_SOURCE_DIR}/bin/slang*.dll")
elseif(APPLE)
    set(ROLLNW_SLANG_LINK_LIB "${slang_prebuilt_SOURCE_DIR}/lib/libslang.dylib")
    file(GLOB ROLLNW_SLANG_RUNTIME
        "${slang_prebuilt_SOURCE_DIR}/lib/libslang*.dylib")
else()
    set(ROLLNW_SLANG_LINK_LIB "${slang_prebuilt_SOURCE_DIR}/lib/libslang.so")
    file(GLOB ROLLNW_SLANG_RUNTIME
        "${slang_prebuilt_SOURCE_DIR}/lib/libslang*.so*")
endif()

if(NOT EXISTS "${ROLLNW_SLANG_LINK_LIB}")
    message(FATAL_ERROR
        "nw-render: Slang link library not found at ${ROLLNW_SLANG_LINK_LIB}")
endif()

target_include_directories(nw-render SYSTEM PRIVATE ${SLANG_INCLUDE_DIR})
target_link_libraries(nw-render PUBLIC "${ROLLNW_SLANG_LINK_LIB}")
target_compile_definitions(nw-render PRIVATE NW_RENDER_HAS_SLANG=1)

# The core module is embedded in the library, so the lib/slang-standard-module-*
# tree in the archive is not a runtime dependency and is deliberately not
# installed (verified by compiling with that directory moved aside).
set(ROLLNW_SHADER_COMPILER_RUNTIME_LIBS ${ROLLNW_SLANG_RUNTIME} PARENT_SCOPE)

set(ROLLNW_OZZ_DIR "${CMAKE_SOURCE_DIR}/external/ozz-animation-0.16.0")
if(NOT EXISTS "${ROLLNW_OZZ_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Vendored ozz-animation not found at ${ROLLNW_OZZ_DIR}")
endif()

set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
set(ozz_build_data OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
if(MSVC)
    set(ozz_build_msvc_rt_dll ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${ROLLNW_OZZ_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ozz" EXCLUDE_FROM_ALL)

add_library(nw-render-ozz STATIC
    animation_backend.cpp
    animation_backend_ozz.cpp
    model_instance_animation.cpp)
target_include_directories(nw-render-ozz PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(nw-render-ozz PUBLIC nw-render ozz_base ozz_animation ozz_animation_offline)

add_library(nw-render-nwn STATIC
    nwn/model_loader.cpp
    nwn/render_asset_cache.cpp
    nwn/nwn_animation.cpp)
target_include_directories(nw-render-nwn PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(nw-render-nwn PUBLIC nw-render nw-render-ozz)

add_library(nw-render-gltf STATIC
    gltf/cgltf_impl.cpp
    gltf/import_gltf.cpp)
target_include_directories(nw-render-gltf PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_include_directories(nw-render-gltf SYSTEM PUBLIC ${ROLLNW_CGLTF_DIR})
target_link_libraries(nw-render-gltf PUBLIC nw-render)

add_library(nw-render-viewer STATIC
    viewer/area_render_scene.cpp
    viewer/area_lighting.cpp
    viewer/camera.cpp
    viewer/device.cpp
    viewer/preview_object.cpp
    viewer/preview_plt.cpp
    viewer/preview_model_animation.cpp
    viewer/preview_model_draws.cpp
    viewer/preview_load_report.cpp
    viewer/preview_nwn_creature.cpp
    viewer/preview_scene.cpp
    viewer/preview_render_resources.cpp
    viewer/scene_debug.cpp
    viewer/scene_lights.cpp
    viewer/scene_particles.cpp
    viewer/scene_shadow.cpp
    viewer/session.cpp
    viewer/tile_light.cpp)
target_include_directories(nw-render-viewer PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_include_directories(nw-render-viewer SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external/taskflow-4.1.0)
target_include_directories(nw-render-viewer SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/glm)
target_link_libraries(nw-render-viewer PUBLIC
    nw-render
    nw-render-gltf
    nw-render-nwn)
