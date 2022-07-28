find_package(benchmark CONFIG REQUIRED)

add_executable(rollnw_benchmark
    main.cpp
)

target_include_directories(rollnw_benchmark PRIVATE
    ../lib
    ../profiles
)
target_include_directories(rollnw_benchmark SYSTEM PRIVATE ../external)

target_link_libraries(rollnw_benchmark PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    nw
    nw-profiles
)
