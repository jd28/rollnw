add_custom_target(vscode-smalls-vsix
    # Ensure the LSP server is built first
    DEPENDS smalls-lsp

    # Copy the LSP binary (Local dev fallback)
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:smalls-lsp> ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:smalls-lsp>

    # Copy the stdlib
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/stdlib
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/core
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1 ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/nwn1

    # Build the extension
    COMMAND npm install
    COMMAND npm run compile
    COMMAND npx @vscode/vsce package
    
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building VS Code extension for Smalls..."
)
