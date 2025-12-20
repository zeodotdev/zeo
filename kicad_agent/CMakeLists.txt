
#
# KiCad Agent CMake config
#

# Add KiCad Agent specific definitions
add_definitions( -DAGENT_APP )

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/utility
    ${INC_AFTER}
    )


# Sources for the standalone executable
set( KICAD_AGENT_SRCS
    kicad_agent.cpp
    kicad_agent_frame.cpp
)

# Resources for the macOS bundle
if( APPLE )
    # Set icons and other bundle properties here
    # For now, we reuse existing resources or define placeholders
    set( KICAD_AGENT_RESOURCES kicad_agent.icns )
    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/kicad_agent.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )
endif()

# Create the standalone executable
add_executable( kicad_agent WIN32 MACOSX_BUNDLE
    kicad_agent.cpp
    kicad_agent_frame.cpp
    ${CMAKE_SOURCE_DIR}/common/single_top.cpp
    kicad_agent.icns
    ${KICAD_AGENT_RESOURCES}
    )

# Define the KIFACE module for the agent
add_library( kicad_agent_kiface MODULE
    ${KICAD_AGENT_SRCS}
    )

if( APPLE )
    set_target_properties( kicad_agent PROPERTIES
        MACOSX_BUNDLE_ICON_FILE kicad_agent.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.kicad-pcb.kicad-agent
        MACOSX_BUNDLE_NAME kicad_agent
        )

    # RPATH for finding Python and other Frameworks relative to the app bundle
    set_target_properties( kicad_agent PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../Frameworks/Python.framework/Versions/Current" )
    
    # KIFACE needs RPATH too if loaded
    set_target_properties( kicad_agent_kiface PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../Frameworks/Python.framework/Versions/Current" )
    set_target_properties( kicad_agent_kiface PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )

    set_target_properties( kicad_agent PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )
endif()

# Define the KIFACE module for the agent


# Set compile definitions for the standalone app
# TOP_FRAME identifies the main window type
set_source_files_properties( ${CMAKE_SOURCE_DIR}/common/single_top.cpp PROPERTIES
    COMPILE_DEFINITIONS "TOP_FRAME=FRAME_AGENT;BUILD_KIWAY_DLL"
    )

# Set properties for the KIFACE module
set_target_properties( kicad_agent_kiface PROPERTIES
    OUTPUT_NAME kicad_agent
    PREFIX ${KIFACE_PREFIX}
    SUFFIX ${KIFACE_SUFFIX}
    )

# Link libraries for the KIFACE module
target_link_libraries( kicad_agent_kiface
    common
    gal
    kicommon
    ${wxWidgets_LIBRARIES}
)

# Link libraries for the executable
target_link_libraries( kicad_agent
    common
    kicommon
    # ${wxWidgets_LIBRARIES} # common/single_top.cpp doesn't need much, but let's be safe if needed
)

add_dependencies( kicad_agent kicad_agent_kiface )

# Installation rules
install( TARGETS kicad_agent
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

install( TARGETS kicad_agent_kiface
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

if( APPLE )
    # Helper to fixup the bundle (if needed for standalone linking)
    # installation logic often handled in root CMake for bundles
endif()
