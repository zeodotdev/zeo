
#
# VCS (Version Control) CMake config
#

# Generate C++ header from VCS HTML template at build time
set( VCS_TEMPLATE_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/vcs_template.html" )
set( VCS_TEMPLATE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vcs_html_template_embedded.h" )

add_custom_command(
    OUTPUT "${VCS_TEMPLATE_OUTPUT}"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE="${VCS_TEMPLATE_INPUT}"
        -DOUTPUT_FILE="${VCS_TEMPLATE_OUTPUT}"
        -DVAR_NAME=VCS_HTML_TEMPLATE_RAW
        -P "${CMAKE_SOURCE_DIR}/agent/view/embed_html.cmake"
    DEPENDS "${VCS_TEMPLATE_INPUT}" "${CMAKE_SOURCE_DIR}/agent/view/embed_html.cmake"
    COMMENT "Embedding VCS HTML template"
)

add_custom_target( vcs_html_template DEPENDS "${VCS_TEMPLATE_OUTPUT}" )

include_directories( BEFORE ${INC_BEFORE} )
include_directories(
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/common/dialogs
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${CMAKE_SOURCE_DIR}/libs/sexpr/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated vcs_html_template_embedded.h
    ${INC_AFTER}
)

set( VCS_SRCS
    vcs.cpp
    vcs_frame.cpp
    vcs_ipc_handler.cpp
    git_vcs_bridge.cpp
    vcs_html_template.cpp
    kicad_file_renderer.cpp
)

# Define the KIFACE module
add_library( vcs_kiface MODULE
    ${VCS_SRCS}
)

set_source_files_properties( vcs.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_KIWAY_DLL;COMPILING_DLL"
)

set_target_properties( vcs_kiface PROPERTIES
    OUTPUT_NAME vcs
    PREFIX ${KIFACE_PREFIX}
    SUFFIX ${KIFACE_SUFFIX}
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)

if( APPLE )
    set_target_properties( vcs_kiface PROPERTIES INSTALL_RPATH
        "@executable_path/../Frameworks;@executable_path/../../../../Frameworks" )
    set_target_properties( vcs_kiface PROPERTIES BUILD_WITH_INSTALL_RPATH 1 )

    set_target_properties( vcs_kiface PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OSX_BUNDLE_BUILD_KIFACE_DIR}
    )
endif()

# Link libraries
target_link_libraries( vcs_kiface
    PRIVATE
    common
    gal
    kicommon
    ${wxWidgets_LIBRARIES}
    ${LIBGIT2_LIBRARIES}
)

if( APPLE )
    target_link_libraries( vcs_kiface PRIVATE "-framework CoreServices" )
endif()

add_dependencies( vcs_kiface vcs_html_template )

# Installation
install( TARGETS vcs_kiface
    DESTINATION ${KICAD_BIN}
    COMPONENT binary
)
