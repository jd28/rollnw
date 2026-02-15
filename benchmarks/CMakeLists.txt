add_custom_target(copy-benchmark-files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/nw/smalls/scripts
    ${CMAKE_CURRENT_BINARY_DIR}/stdlib
)

add_executable(rollnw_benchmark
    main.cpp
    smalls.cpp
)

target_include_directories(rollnw_benchmark PRIVATE
    ../lib
)
target_include_directories(rollnw_benchmark SYSTEM PRIVATE ../external)

target_link_libraries(rollnw_benchmark PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    nw
)

install(TARGETS rollnw_benchmark)
