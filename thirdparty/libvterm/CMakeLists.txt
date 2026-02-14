set(VTERM_SRCS
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

add_library( vterm STATIC ${VTERM_SRCS} )

target_include_directories( vterm
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Suppress warnings from third-party C code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options( vterm PRIVATE -w )
endif()
