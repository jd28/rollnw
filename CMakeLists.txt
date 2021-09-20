cmake_minimum_required(VERSION 3.9)
project(libnw VERSION 0.1.0 LANGUAGES CXX C)
set (CMAKE_CXX_STANDARD 17)

include(cmake/ProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)

add_library(libnw_project_options INTERFACE)
target_compile_features(libnw_project_options INTERFACE cxx_std_17)

add_library(libnw_project_warnings INTERFACE)

include(cmake/Cache.cmake)

include(cmake/CompilerWarnings.cmake)
set_project_warnings(libnw_project_warnings)

include(cmake/Sanitizers.cmake)
enable_sanitizers(libnw_project_options)

include_directories(external)
add_subdirectory(external/inih)
add_subdirectory(external/stb)
add_subdirectory(lib/nw)

if (ENABLE_TESTING)
add_subdirectory(tests)
endif()
