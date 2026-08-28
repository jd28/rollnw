add_library(nw-nav STATIC
    NavGeometry.cpp
    NavTileBuild.cpp
    NavWorld.cpp
)

target_include_directories(nw-nav PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(nw-nav PUBLIC nw)
target_link_libraries(nw-nav PRIVATE RecastNavigation::Detour RecastNavigation::Recast)

include(../../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw-nav OWN_SOURCES)

include(../../../cmake/Sanitizers.cmake)
enable_sanitizers(nw-nav)
