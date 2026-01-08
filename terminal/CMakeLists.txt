
#
# KiCad Terminal CMake config
#

# Add Terminal specific definitions
add_definitions( -DTERMINAL_APP )

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/utility
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${CMAKE_SOURCE_DIR}/scripting
    ${INC_AFTER}
    ${PYTHON_INCLUDE_DIRS}
    )

# Sources for the standalone executable
set( TERMINAL_SRCS
    terminal.cpp
    terminal_frame.cpp
    terminal_panel.cpp
    terminal_panel_agent.cpp
    python_exec_thread.cpp
)

# Resources for the macOS bundle
if( APPLE )
    # Use agent.icns for now as placeholder, or copy py_script if we can convert it. 
    # For now, let's reuse agent.icns or just expect a terminal.icns to be provided? 
    # The user asked for "same icon as scripting console". 
    # Usually that's a bitmap, not an OS icon. 
    # I will try to use agent.icns as a fallback if I can't make one, 
    # but I'll set it to terminal.icns and I'll copy agent.icns to terminal.icns as a starter.
    set( TERMINAL_RESOURCES terminal.icns )
    set_source_files_properties( "${CMAKE_CURRENT_SOURCE_DIR}/terminal.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )
endif()

# Create the standalone executable
# Uses single_top.cpp as the main entry point which loads the KIFACE
add_executable( terminal WIN32 MACOSX_BUNDLE
    ${CMAKE_SOURCE_DIR}/common/single_top.cpp
    ${TERMINAL_RESOURCES}
    )

if( APPLE )
    set_target_properties( terminal PROPERTIES
        MACOSX_BUNDLE_ICON_FILE terminal.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.kicad-pcb.kicad-terminal
        MACOSX_BUNDLE_NAME Terminal
        )
    
    set_target_properties( terminal PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../../../../Frameworks;@executable_path/../../../../Frameworks/Python.framework" )
    set_target_properties( terminal PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )
endif()

# Define the KIFACE module for the terminal
add_library( terminal_kiface MODULE
    ${TERMINAL_SRCS}
    )

set_source_files_properties( ${CMAKE_SOURCE_DIR}/common/single_top.cpp PROPERTIES
    COMPILE_DEFINITIONS "TOP_FRAME=FRAME_TERMINAL;BUILD_KIWAY_DLL"
    )

set_source_files_properties( terminal.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_KIWAY_DLL;COMPILING_DLL"
    )

set_target_properties( terminal_kiface PROPERTIES
    OUTPUT_NAME terminal
    PREFIX ${KIFACE_PREFIX}
    SUFFIX ${KIFACE_SUFFIX}
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    )

if( APPLE )
    set_target_properties( terminal_kiface PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../../../../Frameworks;@executable_path/../../../../Frameworks/Python.framework" )
    set_target_properties( terminal_kiface PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )
    
    set_target_properties( terminal_kiface PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OSX_BUNDLE_BUILD_KIFACE_DIR}
        )
else()
    set_target_properties( terminal_kiface PROPERTIES
        OUTPUT_NAME terminal
        PREFIX ${KIFACE_PREFIX}
        SUFFIX ${KIFACE_SUFFIX}
        )
endif()

# Installation rules
install( TARGETS terminal
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

install( TARGETS terminal_kiface
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
    )

target_link_libraries( terminal
    PRIVATE
    common
    kicommon
    ${wxWidgets_LIBRARIES}
    )

target_link_libraries( terminal_kiface
    PRIVATE
    common
    gal
    kicommon
    ${wxWidgets_LIBRARIES}
    scripting
    ${PYTHON_LIBRARIES}
    )

target_compile_definitions( terminal PRIVATE KICAD_SCRIPTING )
target_compile_definitions( terminal_kiface PRIVATE KICAD_SCRIPTING )
