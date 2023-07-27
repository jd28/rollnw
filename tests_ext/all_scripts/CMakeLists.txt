find_package(Threads)
find_package(GTest CONFIG REQUIRED)

add_executable(rollnw_test_all_scripts
    main.cpp
)

target_include_directories(rollnw_test_all_scripts PRIVATE
    ../../lib
    ../../profiles
)

target_link_libraries(rollnw_test_all_scripts PRIVATE
    nw
    nw-profiles
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_scripts PRIVATE dl)
endif()
