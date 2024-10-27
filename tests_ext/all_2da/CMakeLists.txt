find_package(Threads)

add_executable(rollnw_test_all_2da
    main.cpp
)

target_include_directories(rollnw_test_all_2da PRIVATE
    ../../lib
    ../../lib/nw/profiles
)

target_link_libraries(rollnw_test_all_2da PRIVATE
    nw
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_2da PRIVATE dl)
endif()

install(TARGETS rollnw_test_all_2da)
