include(FetchContent)

set(ROLLNW_CGLTF_DIR "${CMAKE_SOURCE_DIR}/external/cgltf-1.15")
if(NOT EXISTS "${ROLLNW_CGLTF_DIR}/cgltf.h")
    message(FATAL_ERROR "Vendored cgltf not found at ${ROLLNW_CGLTF_DIR}")
endif()

add_library(nw-render STATIC
    renderer.cpp
    shader_provider.cpp
    render_service.cpp)
target_include_directories(nw-render PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(nw-render PUBLIC nw nw-gfx)

set(ROLLNW_DXC_RUNTIME_LIBS "" PARENT_SCOPE)

if(WIN32)
    FetchContent_Declare(dxc_prebuilt
        URL      https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip
        URL_HASH SHA256=a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(dxc_prebuilt)
    if(NOT dxc_prebuilt_POPULATED)
        FetchContent_Populate(dxc_prebuilt)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DXC_ARCH x64)
    else()
        set(DXC_ARCH x86)
    endif()

    find_path(DXC_INCLUDE_DIR
        NAMES dxc/dxcapi.h dxcapi.h
        PATHS
            ${dxc_prebuilt_SOURCE_DIR}/include
            ${dxc_prebuilt_SOURCE_DIR}/inc
        NO_DEFAULT_PATH)
    if(NOT DXC_INCLUDE_DIR)
        message(FATAL_ERROR "nw-render: DXC headers not found in prebuilt package")
    endif()

    target_include_directories(nw-render SYSTEM PRIVATE ${DXC_INCLUDE_DIR})
    target_link_libraries(nw-render PUBLIC ${dxc_prebuilt_SOURCE_DIR}/lib/${DXC_ARCH}/dxcompiler.lib)
    target_compile_definitions(nw-render PRIVATE NW_RENDER_HAS_DXC=1)
    set(ROLLNW_DXC_RUNTIME_LIBS
        ${dxc_prebuilt_SOURCE_DIR}/bin/${DXC_ARCH}/dxcompiler.dll
        ${dxc_prebuilt_SOURCE_DIR}/bin/${DXC_ARCH}/dxil.dll
        PARENT_SCOPE)
elseif(APPLE)
    find_library(DXC_LIB dxcompiler
        PATHS /opt/homebrew/lib /usr/local/lib
        NO_DEFAULT_PATH)
    find_path(DXC_INCLUDE dxc/dxcapi.h
        PATHS /opt/homebrew/include /usr/local/include
        NO_DEFAULT_PATH)
    if(DXC_LIB AND DXC_INCLUDE)
        target_include_directories(nw-render SYSTEM PRIVATE ${DXC_INCLUDE})
        target_link_libraries(nw-render PUBLIC ${DXC_LIB})
        target_compile_definitions(nw-render PRIVATE NW_RENDER_HAS_DXC=1)
    else()
        message(WARNING
            "nw-render: DXC not found — shaders will not compile at runtime.\n"
            "Install with: brew install directx-shader-compiler")
    endif()
elseif(UNIX)
    FetchContent_Declare(dxc_prebuilt
        URL      https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505/linux_dxc_2025_05_24.x86_64.tar.gz
        URL_HASH SHA256=b99655f65215287825fcdd49102b17e2a1608eff79ffaf9457514c2676892aa5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(dxc_prebuilt)
    if(NOT dxc_prebuilt_POPULATED)
        FetchContent_Populate(dxc_prebuilt)
    endif()

    find_path(DXC_INCLUDE_DIR
        NAMES dxc/dxcapi.h dxcapi.h
        PATHS
            ${dxc_prebuilt_SOURCE_DIR}/include
            ${dxc_prebuilt_SOURCE_DIR}/inc
        NO_DEFAULT_PATH)
    if(NOT DXC_INCLUDE_DIR)
        message(FATAL_ERROR "nw-render: DXC headers not found in prebuilt package")
    endif()

    target_include_directories(nw-render SYSTEM PRIVATE ${DXC_INCLUDE_DIR})
    target_link_libraries(nw-render PUBLIC ${dxc_prebuilt_SOURCE_DIR}/lib/libdxcompiler.so)
    target_compile_definitions(nw-render PRIVATE NW_RENDER_HAS_DXC=1)
    set(ROLLNW_DXC_RUNTIME_LIBS
        ${dxc_prebuilt_SOURCE_DIR}/lib/libdxcompiler.so
        ${dxc_prebuilt_SOURCE_DIR}/lib/libdxil.so
        PARENT_SCOPE)
endif()

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
    animation_backend_ozz.cpp)
target_include_directories(nw-render-ozz PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(nw-render-ozz PUBLIC nw-render ozz_base ozz_animation ozz_animation_offline)

add_library(nw-render-nwn STATIC
    nwn/model_loader.cpp
    nwn/model_renderer.cpp
    nwn/model_gpu_backend.cpp
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
