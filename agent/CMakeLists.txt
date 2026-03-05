
#
# KiCad Agent CMake config
#

# Add KiCad Agent specific definitions
add_definitions( -DAGENT_APP )

# Generate C++ header from unified HTML template at build time
set( UNIFIED_TEMPLATE_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/view/unified_template.html" )
set( UNIFIED_TEMPLATE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/unified_template_embedded.h" )

add_custom_command(
    OUTPUT "${UNIFIED_TEMPLATE_OUTPUT}"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE="${UNIFIED_TEMPLATE_INPUT}"
        -DOUTPUT_FILE="${UNIFIED_TEMPLATE_OUTPUT}"
        -DVAR_NAME=UNIFIED_HTML_TEMPLATE_RAW
        -P "${CMAKE_CURRENT_SOURCE_DIR}/view/embed_html.cmake"
    DEPENDS "${UNIFIED_TEMPLATE_INPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/view/embed_html.cmake"
    COMMENT "Embedding unified HTML template"
)

add_custom_target( unified_html_template DEPENDS "${UNIFIED_TEMPLATE_OUTPUT}" )

# ---------------------------------------------------------------------------
# Python tool scripts — installed into the app bundle at build time
# ---------------------------------------------------------------------------
set( AGENT_PYTHON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/python" )

# Copy python/ directory into the app bundle so scripts are available at runtime.
# On macOS: Zeo.app/Contents/SharedSupport/agent/python/
if( APPLE )
    set( AGENT_PYTHON_INSTALL_DIR "${OSX_BUNDLE_BUILD_DIR}/${OSX_BUNDLE_SUP_DIR}/agent/python" )

    add_custom_target( install_python_scripts ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${AGENT_PYTHON_DIR}"
            "${AGENT_PYTHON_INSTALL_DIR}"
        COMMENT "Installing Python tool scripts into app bundle"
    )
endif()

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/utility
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${CMAKE_SOURCE_DIR}/libs/sexpr/include
    ${CMAKE_SOURCE_DIR}/eeschema
    ${CMAKE_SOURCE_DIR}/terminal
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/view
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/util
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated unified_template_embedded.h
    ${INC_AFTER}
    )


# Sources for the standalone executable
set( AGENT_SRCS
    # Entry point and main frame
    agent.cpp
    agent_frame.cpp

    # Core modules (kept at root level)
    agent_llm_client.cpp
    agent_events.cpp
    agent_state.cpp
    agent_chat_history.cpp

    # Monitor logging (shared with terminal)
    ${CMAKE_SOURCE_DIR}/terminal/agent_monitor_log.cpp

    # Core logic modules
    core/chat_controller.cpp

    # Tool schemas (JSON definitions sent to LLM)
    tools/tool_schemas.cpp

    # Bridge (JS↔C++ message routing)
    bridge/webview_bridge.cpp

    # View (templates, markdown, file attachments)
    view/agent_markdown.cpp
    view/unified_html_template.cpp
    view/file_attach.cpp

    # Authentication (AGENT_AUTH lives in common/zeo/)

    # Cloud sync (chat + log uploads to Supabase Storage)
    cloud/agent_cloud_sync.cpp

    # Tool infrastructure
    tools/tool_registry.cpp

    # Tool handlers
    tools/handlers/python_tool_handler.cpp
    tools/handlers/pcb_autoroute_handler.cpp
    tools/handlers/screenshot_handler.cpp
    tools/handlers/check_status_handler.cpp
    tools/handlers/create_project_handler.cpp
    tools/handlers/open_editor_handler.cpp
    tools/handlers/datasheet_handler.cpp
    tools/handlers/symbol_generator.cpp
    tools/handlers/symbol_importer.cpp
    tools/handlers/footprint_generator.cpp
    tools/handlers/component_search_handler.cpp
    tools/handlers/netclass_generator.cpp
    tools/handlers/file_io_handler.cpp

)

# Platform-specific sources
if( APPLE )
    list( APPEND AGENT_SRCS platform/macos_key_monitor.mm platform/macos_webview_bg.mm )
endif()


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
    ${AGENT_SRCS}
    ${CMAKE_SOURCE_DIR}/common/single_top.cpp
    ${AGENT_RESOURCES}
    )

if( APPLE )
    set_target_properties( agent     PROPERTIES
        MACOSX_BUNDLE_ICON_FILE agent.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.zeo.agent
        MACOSX_BUNDLE_NAME Agent
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_BINARY_DIR}/agent/Info.plist
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
    sexpr
    ${wxWidgets_LIBRARIES}
)

# Link libraries for the executable
target_link_libraries( agent
    PRIVATE
    common
    kicommon
    # ${wxWidgets_LIBRARIES} # common/single_top.cpp doesn't need much, but let's be safe if needed
)

# Ensure HTML template and Python scripts are generated before compiling
add_dependencies( agent_kiface unified_html_template install_python_scripts )
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
