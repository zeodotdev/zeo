
#
# KiCad Agent CMake config
#

# Add KiCad Agent specific definitions
add_definitions( -DAGENT_APP )

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/utility
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${INC_AFTER}
    )


# Sources for the standalone executable
set( AGENT_SRCS
    agent.cpp
    agent_frame.cpp
    agent_llm_client.cpp
    agent_thread.cpp
    agent_events.cpp
    agent_state.cpp
    agent_chat_history.cpp
)

# Resources for the macOS bundle
if( APPLE )
    set( AGENT_RESOURCES agent.icns )
    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/agent.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )
endif()

# Create the standalone executable
# Uses single_top.cpp as the main entry point which loads the KIFACE
add_executable( agent WIN32 MACOSX_BUNDLE
    agent.cpp
    agent_frame.cpp
    agent_llm_client.cpp
    agent_thread.cpp
    agent_events.cpp
    agent_state.cpp
    agent_chat_history.cpp
    ${CMAKE_SOURCE_DIR}/common/single_top.cpp
    ${AGENT_RESOURCES}
    )

if( APPLE )
    set_target_properties( agent PROPERTIES
        MACOSX_BUNDLE_ICON_FILE agent.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.kicad-pcb.kicad-agent
        MACOSX_BUNDLE_NAME Agent
        )
    
    # RPATH for finding Python and other Frameworks relative to the app bundle
    # RPATH for finding Python and other Frameworks relative to the app bundle
    # Since Agent.app is nested in KiCad.app/Contents/Applications, we need up 4 levels
    # RPATH for finding Python and other Frameworks relative to the app bundle
    # Since Agent.app is nested in KiCad.app/Contents/Applications, we need up 4 levels
    # Python lib id is @rpath/Versions/3.9/Python, so we need .../Python.framework in rpath
    set_target_properties( agent PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../../../../Frameworks;@executable_path/../../../../Frameworks/Python.framework" )
    set_target_properties( agent PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )
endif()

# Define the KIFACE module for the agent
add_library( agent_kiface MODULE
    ${AGENT_SRCS}
    )

# Set compile definitions for the standalone app
# TOP_FRAME identifies the main window type
set_source_files_properties( ${CMAKE_SOURCE_DIR}/common/single_top.cpp PROPERTIES
    COMPILE_DEFINITIONS "TOP_FRAME=FRAME_AGENT;BUILD_KIWAY_DLL"
    )

set_source_files_properties( agent.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_KIWAY_DLL;COMPILING_DLL"
    )

# Set properties for the KIFACE module
set_target_properties( agent_kiface PROPERTIES
    OUTPUT_NAME agent
    PREFIX ${KIFACE_PREFIX}
    SUFFIX ${KIFACE_SUFFIX}
    )

if( APPLE )
    # KIFACE needs RPATH too if loaded
    # KIFACE needs RPATH too if loaded
    # KIFACE needs RPATH too if loaded
    set_target_properties( agent_kiface PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../../../../Frameworks;@executable_path/../../../../Frameworks/Python.framework" )
    set_target_properties( agent_kiface PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )
    
    # Puts binaries into the *.app bundle while linking
    set_target_properties( agent_kiface PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OSX_BUNDLE_BUILD_KIFACE_DIR}
        )
endif()

# Link libraries for the KIFACE module
target_link_libraries( agent_kiface
    PRIVATE
    common
    gal
    kicommon
    ${wxWidgets_LIBRARIES}
)

# Link libraries for the executable
target_link_libraries( agent
    PRIVATE
    common
    kicommon
    # ${wxWidgets_LIBRARIES} # common/single_top.cpp doesn't need much, but let's be safe if needed
)

add_dependencies( agent agent_kiface )

# Installation rules
install( TARGETS agent
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

install( TARGETS agent_kiface
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

# Install model_context directory
if( APPLE )
    # Install to KiCad.app's SharedSupport directory (not agent.app since it's a symlink to non-existent path)
    # The agent KIFACE runs from within KiCad, so it can find files relative to KiCad.app
    install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/model_context
        DESTINATION ${KICAD_BIN}/KiCad.app/Contents/SharedSupport/agent
        COMPONENT binary
        FILES_MATCHING PATTERN "*.txt"
        )
else()
    # On other platforms, install to share directory
    install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/model_context
        DESTINATION ${KICAD_DATA}/kicad-agent
        COMPONENT binary
        FILES_MATCHING PATTERN "*.txt"
        )
endif()
