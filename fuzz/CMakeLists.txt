add_executable(fuzz_smalls_parse
    smalls_parse_fuzzer.cpp
)

add_executable(fuzz_smalls_resolve
    smalls_resolve_fuzzer.cpp
)

add_executable(fuzz_smalls_vm
    smalls_vm_fuzzer.cpp
)

set(fuzzer_targets
    fuzz_smalls_parse
    fuzz_smalls_resolve
    fuzz_smalls_vm
)

foreach(tgt IN LISTS fuzzer_targets)
    target_include_directories(${tgt} PRIVATE
        ${PROJECT_SOURCE_DIR}/lib
    )
    target_link_libraries(${tgt} PRIVATE
        nw
    )

    # libFuzzer (Clang-only) + ASan
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        target_compile_options(${tgt} PRIVATE
            -fsanitize=fuzzer,address
            -fno-omit-frame-pointer
        )
        target_link_options(${tgt} PRIVATE
            -fsanitize=fuzzer,address
        )
    else()
        message(FATAL_ERROR "Fuzzers require Clang (libFuzzer)")
    endif()
endforeach()
