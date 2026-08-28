find_package(Threads)

add_executable(rollnw_test_all_walkmesh
    main.cpp
)

target_include_directories(rollnw_test_all_walkmesh PRIVATE
    ../../lib
)

target_link_libraries(rollnw_test_all_walkmesh PRIVATE
    nw
    nw-nav
    nw-render-nwn
    nw-render-ozz
    RecastNavigation::Detour
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_walkmesh PRIVATE dl)
endif()

install(TARGETS rollnw_test_all_walkmesh)
