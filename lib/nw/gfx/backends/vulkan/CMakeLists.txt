add_library(nw-gfx STATIC
    vulkan_all.cpp
)

include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
set_project_warnings(nw-gfx OWN_SOURCES NO_INTERFACE)

# nw-gfx does not link nw, so it inherits no warnings from it. Its only
# translation unit exists to compile vendored implementations -- volk and
# VulkanMemoryAllocator -- whose warnings are not ours to fix; 224 of the 225
# it reports come from vk_mem_alloc.h alone. Warnings stay enabled on
# the target so that first-party sources added here are checked, and are
# silenced for this file only. NO_INTERFACE keeps the flags off consumers, which
# compile vendored imgui sources that would otherwise flood the build.
set_source_files_properties(vulkan_all.cpp PROPERTIES COMPILE_OPTIONS -w)

target_include_directories(nw-gfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/nw/gfx
    ${CMAKE_SOURCE_DIR}/external/SDL/src/video/khronos
)

# Mark RmlUi headers as SYSTEM to suppress warnings from external code
target_include_directories(nw-gfx SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external/RmlUi-6.2/Include
)

target_link_libraries(nw-gfx PUBLIC
    SDL3::SDL3
)

if(ROLLNW_ENABLE_TRACY)
    target_link_libraries(nw-gfx PUBLIC Tracy::TracyClient)
    target_compile_definitions(nw-gfx PUBLIC ROLLNW_ENABLE_TRACY)
endif()

target_compile_definitions(nw-gfx PUBLIC NW_GFX_BACKEND_VULKAN=1)

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
endif()
