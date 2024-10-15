cmake_minimum_required(VERSION 3.21)
project(rollNW VERSION 0.39.0 LANGUAGES CXX C)
set (CMAKE_CXX_STANDARD 20)
include(cmake/Cache.cmake)
include(cmake/Sanitizers.cmake)

option(ROLLNW_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ROLLNW_BUILD_DOCS "Build documentation" OFF)
option(ROLLNW_BUILD_RUNTIME_SCRIPTING "Build Luau scripting language" ON)
option(ROLLNW_BUILD_TESTS "Build tests" OFF)
option(ROLLNW_BUILD_PYTHON "Build python bindings" OFF)

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# AppleClang changes visibility to hidden
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fvisibility=hidden)
endif()

if(MSVC)
    add_definitions(
        -D_CRT_SECURE_NO_WARNINGS
        -DUNICODE
        -D_UNICODE
        -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS # abseil
        -D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS # fmt
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
    add_definitions(-DROLLNW_OS_WINDOWS)
elseif(APPLE)
    add_definitions(-DROLLNW_OS_MACOS)
elseif(UNIX)
    add_definitions(-DROLLNW_OS_LINUX)
else()
    message(FATAL "Unsupported target platform")
endif()

set(ZLIB_BUILD_EXAMPLES OFF)
set(ZLIB_ENABLE_TESTS OFF)
add_subdirectory(external/zlib-1.3.1)

include_directories(SYSTEM external)
include_directories(SYSTEM external/immer)
include_directories(SYSTEM external/nowide/include)

set(ABSL_USE_SYSTEM_INCLUDES ON)
add_subdirectory(external/abseil-cpp-20240722.0)
add_subdirectory(external/fmt-10.2.1)
add_subdirectory(external/fzy)
add_subdirectory(external/immer)
add_subdirectory(external/inih)

add_subdirectory(external/libiconv)

include_directories(SYSTEM external/sqlite-3.45.2)
add_subdirectory(external/sqlite-3.45.2)

add_subdirectory(external/minizip)
include_directories(SYSTEM external/minizip/include)

add_subdirectory(external/nowide)
add_subdirectory(external/stb)

include_directories(SYSTEM external/zstd-1.5.6/lib)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_CONTRIB OFF)
add_subdirectory(external/zstd-1.5.6/build/cmake)

add_subdirectory(lib/nw)

if (ROLLNW_BUILD_TESTS)
add_subdirectory(external/googletest-1.14.0)
include(CTest)
enable_testing()
add_subdirectory(tests)
add_subdirectory(tests_ext)
endif()

if (ROLLNW_BUILD_BENCHMARKS)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(external/benchmark-1.8.3)
add_subdirectory(benchmarks)
endif()

if(ROLLNW_BUILD_DOCS)
add_subdirectory(docs)
endif()

if(ROLLNW_BUILD_PYTHON)
include_directories(profiles/)
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
add_subdirectory(external/pybind11)
add_subdirectory(rollnw-py)
endif()
