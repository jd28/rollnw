find_package(Threads)

add_executable(rollnw_test_smalls
    main.cpp
)

target_include_directories(rollnw_test_smalls PRIVATE
    ../../lib
)

target_link_libraries(rollnw_test_smalls PRIVATE
    nw
    Threads::Threads)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_smalls PRIVATE dl)
endif()

install(TARGETS rollnw_test_smalls)
