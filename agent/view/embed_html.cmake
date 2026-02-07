# CMake script to embed HTML file as a C++ raw string literal
# Usage: cmake -DINPUT_FILE=input.html -DOUTPUT_FILE=output.h [-DVAR_NAME=MY_VAR] -P embed_html.cmake

if(NOT INPUT_FILE OR NOT OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE must be defined")
endif()

# Default variable name for backward compatibility
if(NOT VAR_NAME)
    set(VAR_NAME "AGENT_HTML_TEMPLATE_RAW")
endif()

# Read the HTML file
file(READ "${INPUT_FILE}" HTML_CONTENT)

# Escape characters that would break the raw string literal
# The )"; sequence would terminate R"( so we need to use a delimiter
# Using R"HTMLTEMPLATE( ... )HTMLTEMPLATE" as the delimiter

# Write the header file
file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} - do not edit directly
#pragma once

inline const char* ${VAR_NAME} = R\"HTMLTEMPLATE(${HTML_CONTENT})HTMLTEMPLATE\";
")
