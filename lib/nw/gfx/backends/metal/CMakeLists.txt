add_library(nw-gfx STATIC
    metal_core.mm
    metal_context.mm
    metal_native.mm
)

include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
set_project_warnings(nw-gfx OWN_SOURCES NO_INTERFACE)

target_compile_options(nw-gfx PRIVATE -fobjc-arc)

target_include_directories(nw-gfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/nw/gfx
)

target_include_directories(nw-gfx SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_definitions(nw-gfx PUBLIC NW_GFX_BACKEND_METAL=1)

target_link_libraries(nw-gfx PUBLIC
    SDL3::SDL3
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
)

if(ROLLNW_ENABLE_TRACY)
    target_link_libraries(nw-gfx PUBLIC Tracy::TracyClient)
    target_compile_definitions(nw-gfx PUBLIC ROLLNW_ENABLE_TRACY)
endif()
