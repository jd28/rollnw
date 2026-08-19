add_executable(fuzz_smalls_parse
    smalls_parse_fuzzer.cpp
)

add_executable(fuzz_smalls_resolve
    smalls_resolve_fuzzer.cpp
)

add_executable(fuzz_smalls_vm
    smalls_vm_fuzzer.cpp
)

add_executable(fuzz_rml_smalls_binding
    rml_smalls_binding_fuzzer.cpp
    ${PROJECT_SOURCE_DIR}/tools/ui/rml_smalls_expression_binding.cpp
    ${PROJECT_SOURCE_DIR}/tools/ui/smalls_rmlui.cpp
)
target_include_directories(fuzz_rml_smalls_binding PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/ui
)

add_executable(fuzz_mdl
    mdl_fuzzer.cpp
)

add_executable(fuzz_text_mdl
    text_mdl_fuzzer.cpp
)

add_executable(fuzz_format_parsers
    format_parsers_fuzzer.cpp
)

set(fuzzer_targets
    fuzz_format_parsers
    fuzz_mdl
    fuzz_rml_smalls_binding
    fuzz_text_mdl
    fuzz_smalls_parse
    fuzz_smalls_resolve
    fuzz_smalls_vm
)

set(ROLLNW_FUZZER_CLANG_RESOURCE_DIR "" CACHE PATH
    "Optional Clang resource directory used only when building fuzz targets")

foreach(tgt IN LISTS fuzzer_targets)
    target_include_directories(${tgt} PRIVATE
        ${PROJECT_SOURCE_DIR}/lib
    )
    target_link_libraries(${tgt} PRIVATE
        nw
    )

    # libFuzzer (Clang-only) + ASan/UBSan
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        if(ROLLNW_FUZZER_CLANG_RESOURCE_DIR)
            target_compile_options(${tgt} PRIVATE
                -resource-dir=${ROLLNW_FUZZER_CLANG_RESOURCE_DIR}
            )
            target_link_options(${tgt} PRIVATE
                -resource-dir=${ROLLNW_FUZZER_CLANG_RESOURCE_DIR}
            )
        endif()
        target_compile_options(${tgt} PRIVATE
            -fsanitize=fuzzer
            -fno-omit-frame-pointer
        )
        target_link_options(${tgt} PRIVATE
            -fsanitize=fuzzer
        )
    else()
        message(FATAL_ERROR "Fuzzers require Clang (libFuzzer)")
    endif()
endforeach()
