# CMake script to embed HTML file as a C++ char array
# Usage: cmake -DINPUT_FILE=input.html -DOUTPUT_FILE=output.h [-DVAR_NAME=MY_VAR] -P embed_html.cmake

if(NOT INPUT_FILE OR NOT OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE must be defined")
endif()

# Default variable name for backward compatibility
if(NOT VAR_NAME)
    set(VAR_NAME "AGENT_HTML_TEMPLATE_RAW")
endif()

# Read the HTML file as hex to avoid MSVC string literal size limits
file(READ "${INPUT_FILE}" HTML_CONTENT HEX)

# Get the length in bytes (hex string is 2 chars per byte)
string(LENGTH "${HTML_CONTENT}" HEX_LEN)
math(EXPR BYTE_LEN "${HEX_LEN} / 2")

# Convert hex pairs to C hex escape sequences
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" HEX_ESCAPED "${HTML_CONTENT}")

# Split into lines of ~80 hex escapes (~320 chars) for readability
set(CHUNK_SIZE 640)  # 160 hex escapes * 4 chars each
string(LENGTH "${HEX_ESCAPED}" ESC_LEN)
set(ARRAY_BODY "")
set(POS 0)
while(POS LESS ESC_LEN)
    math(EXPR REMAINING "${ESC_LEN} - ${POS}")
    if(REMAINING LESS CHUNK_SIZE)
        set(THIS_CHUNK ${REMAINING})
    else()
        set(THIS_CHUNK ${CHUNK_SIZE})
    endif()
    string(SUBSTRING "${HEX_ESCAPED}" ${POS} ${THIS_CHUNK} CHUNK)
    string(APPEND ARRAY_BODY "\"${CHUNK}\"\n")
    math(EXPR POS "${POS} + ${THIS_CHUNK}")
endwhile()

# Write the header file using adjacent string literal concatenation
file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} - do not edit directly
#pragma once

inline const char ${VAR_NAME}[] =
${ARRAY_BODY};
inline constexpr size_t ${VAR_NAME}_LEN = ${BYTE_LEN};
")
