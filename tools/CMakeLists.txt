
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/pcbnew
    ${BOOST_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    )

add_executable( test-nm-biu-to-ascii-mm-round-tripping
    EXCLUDE_FROM_ALL
    test-nm-biu-to-ascii-mm-round-tripping.cpp
    )

add_executable( utf8_tests
    EXCLUDE_FROM_ALL
    utf8_tests.cpp
    )
target_link_libraries( utf8_tests
    common
    ${wxWidgets_LIBRARIES}
    )

add_subdirectory( newstroke )

# Install zeo CLI tool (Python script — goes in SharedSupport, not MacOS, to avoid codesign issues)
if( APPLE )
    set( ZEO_CLI_BUILD_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_SUP_DIR}/bin" )

    add_custom_target( zeo_cli ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ZEO_CLI_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/zeo
            ${ZEO_CLI_BUILD_DIR}/zeo
        COMMAND chmod +x ${ZEO_CLI_BUILD_DIR}/zeo
        COMMENT "Copying zeo CLI tool to app bundle"
        )

    install( PROGRAMS zeo
        DESTINATION ${OSX_BUNDLE_INSTALL_DIR}/${OSX_BUNDLE_SUP_DIR}/bin
        COMPONENT binary
        )
else()
    install( PROGRAMS zeo
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )
endif()

# Native Windows MCP launcher. Embeds python311.dll (same one Zeo.exe links)
# and runs kipy.mcp.server from the bundled bin/Lib/site-packages. Replaces
# the bash zeo script for MCP integration on Windows, where bash isn't viable.
if( WIN32 )
    add_executable( zeo-mcp WIN32 zeo-mcp/zeo-mcp.cpp )

    target_include_directories( zeo-mcp PRIVATE ${PYTHON_INCLUDE_DIRS} )
    target_link_libraries( zeo-mcp PRIVATE ${PYTHON_LIBRARIES} )

    if( MSVC )
        # Override the WIN32 subsystem with CONSOLE for stdio MCP transport.
        # Mirrors the kicad-cli pattern in kicad/CMakeLists.txt.
        set_target_properties( zeo-mcp PROPERTIES COMPILE_DEFINITIONS_DEBUG          "_CONSOLE" )
        set_target_properties( zeo-mcp PROPERTIES COMPILE_DEFINITIONS_RELWITHDEBINFO "_CONSOLE" )
        set_target_properties( zeo-mcp PROPERTIES LINK_FLAGS_DEBUG          "/SUBSYSTEM:CONSOLE" )
        set_target_properties( zeo-mcp PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE" )
        set_target_properties( zeo-mcp PROPERTIES LINK_FLAGS_RELEASE        "/SUBSYSTEM:CONSOLE" )
        set_target_properties( zeo-mcp PROPERTIES LINK_FLAGS_MINSIZEREL     "/SUBSYSTEM:CONSOLE" )
    endif()

    install( TARGETS zeo-mcp
        DESTINATION ${KICAD_BIN}
        COMPONENT binary
        )
endif()