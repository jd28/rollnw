include(GNUInstallDirs)
find_package(Threads)

add_executable(smalls-lsp main.cpp)

target_include_directories(smalls-lsp PRIVATE ../../lib)

target_link_libraries(smalls-lsp PRIVATE nw Threads::Threads)

if(CMAKE_HOST_UNIX)
    target_link_libraries(smalls-lsp PRIVATE dl)
endif()

install(TARGETS smalls-lsp
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
