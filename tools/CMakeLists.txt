
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