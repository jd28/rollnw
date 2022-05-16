cmake_minimum_required(VERSION 3.21)
project(libnw VERSION 0.1.0 LANGUAGES CXX C)
set (CMAKE_CXX_STANDARD 17)

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/PreventInSourceBuilds.cmake)

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

# Code Coverage Configuration
add_library(coverage_config INTERFACE)

option(CODE_COVERAGE "Enable coverage reporting" OFF)
if(CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Add required flags (GCC & LLVM/Clang)
  target_compile_options(coverage_config INTERFACE
    -O0        # no optimization
    -g         # generate debug info
    --coverage # sets all required flags
  )
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.13)
    target_link_options(coverage_config INTERFACE --coverage)
  else()
    target_link_libraries(coverage_config INTERFACE --coverage)
  endif()
endif(CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

if(WIN32)
    add_definitions(-DLIBNW_OS_WINDOWS)
elseif(APPLE)
    add_definitions(-DLIBNW_OS_MACOS)
elseif(UNIX)
    add_definitions(-DLIBNW_OS_LINUX)
else()
    message(FATAL "Unsupported target platform")
endif()

include_directories(external)
add_subdirectory(external/flecs)
add_subdirectory(external/inih)
add_subdirectory(external/stb)
add_subdirectory(lib/nw)

if (ENABLE_TESTING)
include(CTest)
enable_testing()
add_subdirectory(tests)
endif()

if (ENABLE_BENCHMARKS)
add_subdirectory(benchmarks)
endif()
