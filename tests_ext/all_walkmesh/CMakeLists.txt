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
    RecastNavigation::Detour
    Threads::Threads
    GTest::gtest)

if(ROLLNW_BUILD_RENDERER)
    target_compile_definitions(rollnw_test_all_walkmesh PRIVATE
        ROLLNW_ALL_WALKMESH_RENDERER=1)
    target_link_libraries(rollnw_test_all_walkmesh PRIVATE
        nw-render-nwn
        nw-render-ozz)
endif()

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test_all_walkmesh PRIVATE dl)
endif()

install(TARGETS rollnw_test_all_walkmesh)
