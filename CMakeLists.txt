cmake_minimum_required(VERSION 3.13)
project(libnw VERSION 0.1.0 LANGUAGES CXX C)
set (CMAKE_CXX_STANDARD 17)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_definitions(
        -D_CRT_SECURE_NO_WARNINGS
        -DUNICODE
        -D_UNICODE
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
