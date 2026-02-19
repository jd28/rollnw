cmake_minimum_required(VERSION 3.21)
project(rollNW VERSION 0.45.0 LANGUAGES CXX C)
set (CMAKE_CXX_STANDARD 20)
include(cmake/Cache.cmake)
include(cmake/Sanitizers.cmake)
include(GNUInstallDirs)

# Apply sanitizer flags globally (including external deps) when enabled.
enable_global_sanitizers()

option(ROLLNW_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ROLLNW_BUILD_DOCS "Build documentation" OFF)
option(ROLLNW_BUILD_RUNTIME_SCRIPTING "Build Luau scripting language" ON)
option(ROLLNW_BUILD_TESTS "Build tests" OFF)
option(ROLLNW_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(ROLLNW_BUILD_PYTHON "Build python bindings" OFF)
option(ROLLNW_BUILD_WITH_MIMALLOC "Build with mimalloc" OFF)
option(ROLLNW_BUILD_TOOLS "Build CLI tools" OFF)
option(ROLLNW_ENABLE_TRACY "Enable Tracy profiling" OFF)
option(ROLLNW_TRACY_ON_DEMAND "Enable Tracy on-demand profiling" ON)

if(ROLLNW_BUILD_PYTHON AND ROLLNW_BUILD_WITH_MIMALLOC)
message(FATAL_ERROR "Python bindings cannot be built with mimalloc")
endif()

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
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
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
    target_link_libraries(coverage_config INTERFACE --coverage)
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

# Ignore all this..
set(BUILD_TESTING OFF)
set(FMT_INSTALL OFF)
set(SKIP_INSTALL_ALL OFF)

set(ZLIB_BUILD_EXAMPLES OFF)
set(ZLIB_ENABLE_TESTS OFF)
add_subdirectory(external/zlib-1.3.1 EXCLUDE_FROM_ALL SYSTEM)

include_directories(SYSTEM external)
include_directories(SYSTEM external/immer)
include_directories(SYSTEM external/nowide/include)
include_directories(SYSTEM external/rang-3.2)
include_directories(SYSTEM external/xsimd-13.0.0/include)
include_directories(SYSTEM external/xxhash-0.8.3)

if(ROLLNW_BUILD_WITH_MIMALLOC AND NOT APPLE)
set(MI_BUILD_SHARED OFF)
set(MI_BUILD_OBJECT OFF)
set(MI_BUILD_TESTS OFF)
set(MI_OPT_ARCH OFF)
include_directories(SYSTEM external/mimalloc-3.0.1/include)
add_subdirectory(external/mimalloc-3.0.1 EXCLUDE_FROM_ALL SYSTEM)
endif()

set(ABSL_USE_SYSTEM_INCLUDES ON)
add_subdirectory(external/abseil-cpp-20260107.0 EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/fmt-10.2.1 EXCLUDE_FROM_ALL SYSTEM)

# Clang (especially newer releases) can be overly strict about fmt's consteval
# format string path in this vendored version. Disable fmt's consteval use.
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  if(TARGET fmt)
    target_compile_definitions(fmt PRIVATE FMT_CONSTEVAL=)
  endif()
endif()

add_subdirectory(external/fzy EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/immer EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/inih EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/libiconv EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/OffsetAllocator EXCLUDE_FROM_ALL SYSTEM)

include_directories(SYSTEM external/sqlite-3.45.2)
add_subdirectory(external/sqlite-3.45.2 EXCLUDE_FROM_ALL SYSTEM)

set(ZLIB_LINK_LIBRARY_NAME zlibstatic)
add_library(ZLIB::ZLIB ALIAS zlibstatic)
add_subdirectory(external/libzip-1.11.3 EXCLUDE_FROM_ALL SYSTEM)
include_directories(SYSTEM external/libzip-1.11.3/lib)

add_subdirectory(external/nowide EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(external/stb EXCLUDE_FROM_ALL SYSTEM)

if(ROLLNW_ENABLE_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  if(ROLLNW_TRACY_ON_DEMAND)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  else()
    set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(external/tracy-0.13.1 EXCLUDE_FROM_ALL SYSTEM)
endif()

include_directories(SYSTEM external/zstd-1.5.6/lib)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_CONTRIB OFF)
add_subdirectory(external/zstd-1.5.6/build/cmake EXCLUDE_FROM_ALL SYSTEM)

add_subdirectory(lib/nw)

if (ROLLNW_BUILD_TESTS)
set(INSTALL_GTEST OFF)
add_subdirectory(external/googletest-1.14.0 EXCLUDE_FROM_ALL SYSTEM)
include(CTest)
enable_testing()
add_subdirectory(tests)
add_subdirectory(tests_ext)
endif()

if (ROLLNW_BUILD_BENCHMARKS)
set(BENCHMARK_ENABLE_INSTALL OFF)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(external/benchmark-1.8.3)
# GCC 15 emits -Wmaybe-uninitialized false positives in benchmark's <regex> usage;
# suppress it on the vendored target rather than dropping -Werror project-wide.
target_compile_options(benchmark PRIVATE -Wno-maybe-uninitialized)
target_compile_options(benchmark_main PRIVATE -Wno-maybe-uninitialized)
add_subdirectory(benchmarks)
endif()

if (ROLLNW_BUILD_FUZZERS)
add_subdirectory(fuzz)
endif()

if(ROLLNW_BUILD_DOCS)
add_subdirectory(docs)
endif()

if(ROLLNW_BUILD_TOOLS AND PROJECT_IS_TOP_LEVEL)
add_subdirectory(tools/smalls)
endif()
