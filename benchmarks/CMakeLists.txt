find_package(benchmark CONFIG REQUIRED)

add_executable(libnw_benchmark
    main.cpp
)

target_include_directories(libnw_benchmark PRIVATE ../lib)

target_link_libraries(libnw_benchmark PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    nw
)
