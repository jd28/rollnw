# glslang
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER      OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL    ON  CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/glslang glslang EXCLUDE_FROM_ALL SYSTEM)
rollnw_silence_target_warnings(
    glslang
    SPIRV
    glslang-default-resource-limits
    MachineIndependent
    GenericCodeGen
    OSDependent
)

# SPIRV-Reflect
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON  CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/SPIRV-Reflect SPIRV-Reflect EXCLUDE_FROM_ALL SYSTEM)
rollnw_silence_target_warnings(spirv-reflect-static)

add_library(nw-gfx STATIC
    vulkan_all.cpp
)

target_include_directories(nw-gfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/nw/gfx
    ${CMAKE_SOURCE_DIR}/external/glslang
    ${CMAKE_SOURCE_DIR}/external/SPIRV-Reflect
    ${CMAKE_SOURCE_DIR}/external/SDL/src/video/khronos
)

# Mark RmlUi headers as SYSTEM to suppress warnings from external code
target_include_directories(nw-gfx SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external/RmlUi-6.2/Include
)

target_link_libraries(nw-gfx PUBLIC
    SDL3::SDL3
    glslang
    SPIRV
    glslang-default-resource-limits
    spirv-reflect-static
)

if(ROLLNW_ENABLE_TRACY)
    target_link_libraries(nw-gfx PUBLIC Tracy::TracyClient)
    target_compile_definitions(nw-gfx PUBLIC ROLLNW_ENABLE_TRACY)
endif()

if(WIN32)
    target_compile_definitions(nw-gfx PUBLIC VK_USE_PLATFORM_WIN32_KHR=1 NOMINMAX)
elseif(UNIX AND NOT APPLE)
    find_package(Wayland QUIET)
    if(NOT Wayland_FOUND AND NOT WAYLAND_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(WAYLAND QUIET wayland-client)
        endif()
    endif()
    if(Wayland_FOUND OR WAYLAND_FOUND)
        target_compile_definitions(nw-gfx PUBLIC VK_USE_PLATFORM_WAYLAND_KHR=1)
    else()
        target_compile_definitions(nw-gfx PUBLIC VK_USE_PLATFORM_XLIB_KHR=1)
    endif()
elseif(APPLE)
    target_compile_definitions(nw-gfx PUBLIC VK_USE_PLATFORM_MACOS_MVK=1 VK_USE_PLATFORM_METAL_EXT=1)
endif()
