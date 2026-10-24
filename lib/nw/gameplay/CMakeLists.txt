add_library(nw-gameplay STATIC actions.cpp)
target_include_directories(nw-gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(nw-gameplay PUBLIC nw-nav)

include(../../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw-gameplay OWN_SOURCES)
include(../../../cmake/Sanitizers.cmake)
enable_sanitizers(nw-gameplay)
