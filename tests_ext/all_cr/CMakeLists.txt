find_package(Threads)

add_executable(rollnw_test_all_cr
    main.cpp
)

target_include_directories(rollnw_test_all_cr PRIVATE
    ../../lib
)

target_link_libraries(rollnw_test_all_cr PRIVATE
    nw
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_cr PRIVATE dl)
endif()

install(TARGETS rollnw_test_all_cr)
