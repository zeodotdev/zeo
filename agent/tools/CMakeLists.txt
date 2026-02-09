#
# KiCad Agent Tools CMake config
#

# Tool infrastructure and handlers
set( TOOLS_SRCS
    # Shared infrastructure
    tool_registry.cpp
    kicad_file/sexpr_util.cpp
    kicad_file/file_writer.cpp
    kicad_file/uuid_util.cpp

    # Schematic tools
    schematic/sch_parser.cpp
    schematic/sch_tool_handler.cpp
    schematic/sch_erc_handler.cpp
    schematic/sch_lib_symbol_handler.cpp
    schematic/sch_crud_handler.cpp
    schematic/sch_util_handler.cpp

    # PCB tools
    pcb/pcb_parser.cpp
    pcb/pcb_validator.cpp
    pcb/pcb_tool_handler.cpp
    pcb/pcb_crud_handler.cpp
    PARENT_SCOPE
)
