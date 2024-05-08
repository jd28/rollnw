find_package(Threads)

add_executable(rollnw_test_all_scripts
    main.cpp
)

target_include_directories(rollnw_test_all_scripts PRIVATE
    ../../lib
    ../../lib/nw/profiles
)

target_link_libraries(rollnw_test_all_scripts PRIVATE
    nw
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_scripts PRIVATE dl)
endif()

install(TARGETS rollnw_test_all_scripts)
