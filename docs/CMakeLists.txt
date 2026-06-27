find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(ROLLNW_DOCS_MANIFEST ${CMAKE_SOURCE_DIR}/docs.toml)
set(ROLLNW_DOCS_OUTPUT ${CMAKE_BINARY_DIR}/docs/site)
set(ROLLNW_DOCS_INDEX ${ROLLNW_DOCS_OUTPUT}/index.html)
set(ROLLNW_DOCS_SCRIPT ${CMAKE_SOURCE_DIR}/tools/docs/build_site.py)
set(ROLLNW_DOCS_REQUIREMENTS ${CMAKE_SOURCE_DIR}/tools/docs/requirements.txt)

file(GLOB_RECURSE ROLLNW_DOCS_MARKDOWN
     CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/README.md"
     "${CMAKE_SOURCE_DIR}/lib/nw/docs/*.md"
     "${CMAKE_SOURCE_DIR}/lib/nw/gfx/README.md"
     "${CMAKE_SOURCE_DIR}/lib/nw/render/README.md"
     "${CMAKE_SOURCE_DIR}/lib/nw/render/docs/*.md"
     "${CMAKE_SOURCE_DIR}/lib/nw/smalls/docs/*.md"
     "${CMAKE_SOURCE_DIR}/tools/mudl/README.md")

add_custom_command(OUTPUT ${ROLLNW_DOCS_INDEX}
                   COMMAND
                     ${Python3_EXECUTABLE} ${ROLLNW_DOCS_SCRIPT}
                       --manifest ${ROLLNW_DOCS_MANIFEST}
                       --repo-root ${CMAKE_SOURCE_DIR}
                       --output ${ROLLNW_DOCS_OUTPUT}
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                   DEPENDS
                     ${ROLLNW_DOCS_SCRIPT}
                     ${ROLLNW_DOCS_REQUIREMENTS}
                     ${ROLLNW_DOCS_MANIFEST}
                     ${ROLLNW_DOCS_MARKDOWN}
                   COMMENT "Generating inline Markdown documentation site")

add_custom_target(docs ALL DEPENDS ${ROLLNW_DOCS_INDEX})

include(GNUInstallDirs)
install(DIRECTORY ${ROLLNW_DOCS_OUTPUT}
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
