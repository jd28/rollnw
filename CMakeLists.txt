cmake_minimum_required(VERSION 3.4)
project(libnw)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

option(LIBNW_BUILD_TESTS "Build Tests" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Wextra -pedantic -fno-omit-frame-pointer")

include_directories(external)

add_subdirectory(lib/nw)

if (LIBNW_BUILD_TESTS)
add_subdirectory(tests)
endif()
