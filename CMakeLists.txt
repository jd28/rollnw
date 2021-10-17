cmake_minimum_required(VERSION 3.21)
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

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

    add_definitions(
        -D_CRT_SECURE_NO_WARNINGS
        -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS # abseil
        -DSTBI_WINDOWS_UTF8 # stbi_image
    )
endif()

include_directories(external)
add_subdirectory(external/inih)
add_subdirectory(external/stb)
add_subdirectory(lib/nw)

if (ENABLE_TESTING)
include(CTest)
enable_testing()
add_subdirectory(tests)
endif()
