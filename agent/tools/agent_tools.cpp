#include "agent_tools.h"
#include "../agent_llm_client.h"
#include "tool_registry.h"
#include <nlohmann/json.hpp>
#include <kiway.h>
#include <wx/string.h>
#include <wx/log.h>

namespace AgentTools
{

std::vector<LLM_TOOL> GetToolDefinitions()
{
    wxLogInfo( "AgentTools::GetToolDefinitions called" );
    using json = nlohmann::json;

    std::vector<LLM_TOOL> tools;

    // run_terminal - Execute bash/shell commands
    LLM_TOOL runTerminal;
    runTerminal.name = "run_terminal";
    runTerminal.description = "Execute bash/shell commands for file operations, git, and other terminal tasks.";
    runTerminal.input_schema = {
        { "type", "object" },
        { "properties", {
            { "command", {
                { "type", "string" },
                { "description", "Bash command to execute" }
            }}
        }},
        { "required", json::array( { "command" } ) }
    };
    tools.push_back( runTerminal );

    // open_editor - Open schematic or PCB editor with user approval
    LLM_TOOL openEditor;
    openEditor.name = "open_editor";
    openEditor.description = "Request to open the schematic or PCB editor. "
                             "This will prompt the user for approval before opening. "
                             "Use editor_type 'sch' for schematic editor or 'pcb' for PCB editor. "
                             "Optionally specify file_path to open a specific file.";
    openEditor.input_schema = {
        { "type", "object" },
        { "properties", {
            { "editor_type", {
                { "type", "string" },
                { "enum", json::array( { "sch", "pcb" } ) },
                { "description", "Editor to open: 'sch' for schematic, 'pcb' for PCB" }
            }},
            { "file_path", {
                { "type", "string" },
                { "description", "Optional: path to file to open (must be within project directory)" }
            }}
        }},
        { "required", json::array( { "editor_type" } ) }
    };
    tools.push_back( openEditor );

    // check_status - Get project and editor status
    LLM_TOOL checkStatus;
    checkStatus.name = "check_status";
    checkStatus.description = "Get the current project and editor status. Returns which editors are open, "
                              "current active sheet, project hierarchy, and unsaved changes. "
                              "ALWAYS call this before using IPC tools to verify the editor is open.";
    checkStatus.input_schema = {
        { "type", "object" },
        { "properties", {} },
        { "required", json::array() }
    };
    checkStatus.read_only = true;
    tools.push_back( checkStatus );

    // create_project - Create a new KiCad project
    LLM_TOOL createProject;
    createProject.name = "create_project";
    createProject.description = "Create a new KiCad project with the standard file structure. "
                                "Creates .kicad_pro, .kicad_sch, and .kicad_pcb files.";
    createProject.input_schema = {
        { "type", "object" },
        { "properties", {
            { "project_name", {
                { "type", "string" },
                { "description", "Name for the new project (used for filenames)" }
            }},
            { "directory", {
                { "type", "string" },
                { "description", "Directory where to create the project folder" }
            }}
        }},
        { "required", json::array( { "project_name", "directory" } ) }
    };
    tools.push_back( createProject );

    // sch_open_sheet - DISABLED: Navigation changes user's view
    // Agent should work on sheets via file paths without navigating user's view
    // LLM_TOOL schOpenSheet;
    // schOpenSheet.name = "sch_open_sheet";
    // schOpenSheet.description = "Navigate to a specific sheet in a hierarchical schematic. "
    //                            "Use sheet_path for navigation within the current hierarchy (e.g., '/root_uuid/child_uuid'), "
    //                            "or file_path to open a specific .kicad_sch file directly. "
    //                            "REQUIRES: Schematic editor must be open.";
    // schOpenSheet.input_schema = {
    //     { "type", "object" },
    //     { "properties", {
    //         { "sheet_path", {
    //             { "type", "string" },
    //             { "description", "Sheet path in hierarchy format (e.g., '/uuid1/uuid2' or use sheet names)" }
    //         }},
    //         { "file_path", {
    //             { "type", "string" },
    //             { "description", "Direct path to .kicad_sch file to open" }
    //         }}
    //     }},
    //     { "required", json::array() }
    // };
    // tools.push_back( schOpenSheet );

    // ===== Direct File Tools (sch_*, pcb_*) =====

    // sch_get_summary - Get high-level overview of schematic via IPC
    LLM_TOOL schGetSummary;
    schGetSummary.name = "sch_get_summary";
    schGetSummary.description = "Get a high-level overview of the schematic from the live editor. "
                                "Returns JSON with symbols, wires, junctions, labels, and counts. "
                                "REQUIRES: Schematic editor must be open with a document loaded.";
    schGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {} },
        { "required", json::array() }
    };
    schGetSummary.read_only = true;
    tools.push_back( schGetSummary );

    // sch_read_section - Read specific section of schematic
    LLM_TOOL schReadSection;
    schReadSection.name = "sch_read_section";
    schReadSection.description = "Read a specific section of the schematic from the live editor. "
                                 "Returns JSON data for the requested section. "
                                 "Sections: header, symbols, wires, junctions, labels, sheets, all. "
                                 "REQUIRES: Schematic editor must be open with a document loaded.";
    schReadSection.input_schema = {
        { "type", "object" },
        { "properties", {
            { "section", {
                { "type", "string" },
                { "enum", json::array( { "header", "symbols", "wires", "junctions", "labels", "sheets", "all" } ) },
                { "description", "Section to read" }
            }},
            { "filter", {
                { "type", "string" },
                { "description", "Optional filter by reference (e.g., 'R*' for all resistors) or UUID" }
            }}
        }},
        { "required", json::array( { "section" } ) }
    };
    schReadSection.read_only = true;
    tools.push_back( schReadSection );

    // sch_modify - DISABLED: Use sch_add/sch_update/sch_delete instead
    // This tool allowed raw S-expression manipulation which is error-prone.
    // The IPC-based CRUD tools provide a safer, structured interface.
    /*
    LLM_TOOL schModify;
    schModify.name = "sch_modify";
    schModify.description = "Modify a .kicad_sch file by adding, updating, or deleting elements. "
                            "Elements must be provided as raw S-expressions matching the KiCad file format. "
                            "The file is validated after modification to prevent corruption.";
    schModify.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_sch file" }
            }},
            { "operation", {
                { "type", "string" },
                { "enum", json::array( { "add", "update", "delete" } ) },
                { "description", "Operation to perform" }
            }},
            { "element_type", {
                { "type", "string" },
                { "enum", json::array( { "symbol", "wire", "junction", "label", "text" } ) },
                { "description", "Type of element to modify" }
            }},
            { "data", {
                { "type", "string" },
                { "description", "S-expression for the element (required for add/update)" }
            }},
            { "target", {
                { "type", "string" },
                { "description", "UUID or reference designator to update/delete (required for update/delete)" }
            }}
        }},
        { "required", json::array( { "file_path", "operation", "element_type" } ) }
    };
    tools.push_back( schModify );
    */

    // sch_run_erc - Run ERC on open schematic
    LLM_TOOL schRunErc;
    schRunErc.name = "sch_run_erc";
    schRunErc.description =
        "Run Electrical Rules Check (ERC) on the currently open schematic. "
        "Detects wiring errors, unconnected pins, duplicate references, and other issues. "
        "Returns error/warning counts and violation details. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schRunErc.input_schema = {
        { "type", "object" },
        { "properties", {
            { "output_format", {
                { "type", "string" },
                { "enum", json::array( { "summary", "detailed", "by_type" } ) },
                { "description", "Output format: 'summary' (counts + text), "
                                "'detailed' (full violation list), "
                                "'by_type' (grouped by error code). Default: summary" }
            }},
            { "include_warnings", {
                { "type", "boolean" },
                { "description", "Include warnings (default: true). Set false for errors only." }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( schRunErc );

    // sch_run_simulation - Run SPICE simulation on open schematic
    LLM_TOOL schRunSim;
    schRunSim.name = "sch_run_simulation";
    schRunSim.description =
        "Run a SPICE simulation on the currently open schematic. "
        "Returns trace summaries with signal names, point counts, and min/max/final values. "
        "When matplotlib is available, also generates a visual plot of all signal traces. "
        "Supported commands:\n"
        "  .op - Operating point analysis\n"
        "  .tran <step> <stop> [start] [maxstep] - Transient analysis (e.g. '.tran 1u 10m')\n"
        "  .ac <dec|oct|lin> <points> <fstart> <fstop> - AC analysis (e.g. '.ac dec 10 1 1meg')\n"
        "  .dc <source> <start> <stop> <incr> - DC sweep (e.g. '.dc V1 0 5 0.1')\n"
        "  .noise v(<out>[,<ref>]) <source> <scale> <pts> <fstart> <fstop> - Noise analysis\n"
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schRunSim.input_schema = {
        { "type", "object" },
        { "properties", {
            { "command", {
                { "type", "string" },
                { "description", "SPICE simulation command (e.g. '.tran 1u 10m', '.ac dec 10 1 1meg', '.op')" }
            }},
            { "save_to_schematic", {
                { "type", "boolean" },
                { "description", "Persist this command as the schematic's default simulation command (default: false)" }
            }}
        }},
        { "required", json::array( { "command" } ) }
    };
    tools.push_back( schRunSim );

    // sch_find_symbol - Query symbol library for pin positions
    LLM_TOOL schGetLibSymbol;
    schGetLibSymbol.name = "sch_find_symbol";
    schGetLibSymbol.description =
        "Query symbol library for symbol definitions including pin positions. "
        "Accepts a symbol name ('R') or full Library:Symbol identifier ('Device:R'). "
        "If just a name is given, searches all libraries. "
        "Supports wildcards ('Connector:Conn_01x*') and regex ('Device:R_[0-9]{4}'). "
        "Returns pin positions relative to symbol origin. "
        "Use this before wiring to get accurate pin locations. "
        "REQUIRES: Schematic editor must be open.";
    schGetLibSymbol.input_schema = {
        { "type", "object" },
        { "properties", {
            { "lib_id", {
                { "type", "string" },
                { "description", "Symbol name ('R') or Library:Symbol identifier ('Device:R'). "
                                "Supports wildcards ('Device:R*') and regex ('Device:R_[0-9]+')." }
            }},
            { "include_pins", {
                { "type", "boolean" },
                { "description", "Include full pin details with positions (default: true)" }
            }},
            { "max_suggestions", {
                { "type", "integer" },
                { "description", "Max suggestions if symbol not found (default: 10)" }
            }},
            { "pattern_type", {
                { "type", "string" },
                { "enum", json::array( { "exact", "wildcard", "regex" } ) },
                { "description", "Pattern type: 'exact', 'wildcard', or 'regex'. "
                                "Auto-detected if not specified." }
            }}
        }},
        { "required", json::array( { "lib_id" } ) }
    };
    schGetLibSymbol.read_only = true;
    tools.push_back( schGetLibSymbol );

    // sch_get_pins - Lightweight pin lookup for a single placed symbol
    LLM_TOOL schGetPins;
    schGetPins.name = "sch_get_pins";
    schGetPins.description =
        "Get pin positions for a single placed symbol. MUCH faster than sch_get_summary "
        "when you only need pin positions for wiring a specific component. "
        "Returns exact transformed positions (after rotation/mirror) in mm. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schGetPins.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ref", {
                { "type", "string" },
                { "description", "Reference designator of the placed symbol (e.g., 'R1', 'U3', 'C5')" }
            }}
        }},
        { "required", json::array( { "ref" } ) }
    };
    schGetPins.read_only = true;
    tools.push_back( schGetPins );

    // ===== IPC-based CRUD Tools (sch_add, sch_update, sch_delete, sch_batch_delete) =====
    // These work on the LIVE schematic via kipy API

    // sch_add - Add one or more elements to schematic
    LLM_TOOL schAdd;
    schAdd.name = "sch_add";
    schAdd.description =
        "Add elements to the schematic. Accepts an array of elements - use for single or batch operations. "
        "Returns pin positions for all symbols, enabling immediate wiring. "
        "REQUIRES: Schematic editor must be open with a document loaded.\n\n"
        "ROTATION (counter-clockwise degrees):\n"
        "- Passives (R, C, L): 0°=vertical (pins top/bottom), 90°=horizontal (pins left/right)\n"
        "- ICs: 0°=default (pin 1 top-left)\n"
        "- Diode: 0°=vertical (K cathode top, A anode bottom)\n"
        "- Power GND: 0°=standard (bars down), do NOT use 180\n"
        "- Power VCC/+V: 0°=standard (bar up), do NOT use 180\n\n"
        "WIRING RULES:\n"
        "- Use from_pin/to_pin for all wiring - backend auto-routes L-shapes\n"
        "- AVOID OVERLAPPING WIRES: Wires at same coordinates create shorts!\n\n"
        "ERC TIPS:\n"
        "- Add 'power:PWR_FLAG' on nets powered by connectors to fix 'Power pin not driven'\n"
        "- Use no_connect on unused pins to fix 'Unconnected pin' warnings\n\n"
        "ELEMENT TYPES:\n"
        "- symbol: {element_type, lib_id, position, angle?, mirror?, reference?, properties?}\n"
        "- power: {element_type, lib_id, position, angle?} - GND: 0=bars down(standard), VCC: 0=bar up(standard)\n"
        "- wire: {element_type, from_pin:{ref,pin}, to_pin:{ref,pin}, waypoints?} or {points:[[x,y],...]}\n"
        "- junction, label, no_connect, sheet\n\n"
        "EXAMPLE (horizontal resistor with GND):\n"
        "elements: [\n"
        "  {element_type:'symbol', lib_id:'Device:R', position:[50.8,50.8], angle:90},\n"
        "  {element_type:'power', lib_id:'power:GND', position:[50.8,63.5]}\n"
        "]";
    schAdd.input_schema = {
        { "type", "object" },
        { "properties", {
            { "elements", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "element_type", {
                            { "type", "string" },
                            { "enum", json::array( { "symbol", "power", "wire", "junction", "label", "no_connect", "sheet", "bus_entry" } ) },
                            { "description", "Type of element to add" }
                        }},
                        { "lib_id", {
                            { "type", "string" },
                            { "description", "Library ID: 'Device:R', 'power:GND', 'power:PWR_FLAG'" }
                        }},
                        { "position", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Position in mm [x, y]. Auto-snapped to 1.27mm grid. Use multiples of 2.54 for clean placement (e.g., 50.8, 76.2, 101.6)." }
                        }},
                        { "angle", {
                            { "type", "number" },
                            { "description", "CCW rotation degrees. Passives: 0=vertical, 90=horizontal. "
                                            "Power GND: 0=bars down(standard). Power VCC: 0=bar up(standard)." }
                        }},
                        { "mirror", {
                            { "type", "string" },
                            { "enum", json::array( { "none", "x", "y" } ) }
                        }},
                        { "reference", {
                            { "type", "string" },
                            { "description", "Reference designator (e.g. 'R1')" }
                        }},
                        { "properties", {
                            { "type", "object" },
                            { "description", "{Value, Footprint, ...}" }
                        }},
                        { "from_pin", {
                            { "type", "object" },
                            { "properties", {
                                { "ref", { { "type", "string" } } },
                                { "pin", { { "type", "string" } } }
                            }},
                            { "description", "Wire start: {ref:'R1', pin:'1'}" }
                        }},
                        { "to_pin", {
                            { "type", "object" },
                            { "properties", {
                                { "ref", { { "type", "string" } } },
                                { "pin", { { "type", "string" } } }
                            }},
                            { "description", "Wire end: {ref:'C1', pin:'2'}" }
                        }},
                        { "points", {
                            { "type", "array" },
                            { "items", { { "type", "array" } } },
                            { "description", "Wire coordinates: [[x1,y1], [x2,y2], ...]" }
                        }},
                        { "waypoints", {
                            { "type", "array" },
                            { "items", { { "type", "array" } } },
                            { "description", "L-route waypoints - YOU must calculate coordinates" }
                        }},
                        { "text", {
                            { "type", "string" },
                            { "description", "Label text" }
                        }},
                        { "label_type", {
                            { "type", "string" },
                            { "enum", json::array( { "local", "global", "hierarchical" } ) }
                        }},
                        { "sheet_name", { { "type", "string" } } },
                        { "sheet_file", { { "type", "string" } } },
                        { "size", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Sheet size [w, h] in mm" }
                        }}
                    }},
                    { "required", json::array( { "element_type" } ) }
                }},
                { "description", "Array of elements. Process in order: symbols before wires." }
            }}
        }},
        { "required", json::array( { "elements" } ) }
    };
    tools.push_back( schAdd );

    // sch_update - Update one or more elements
    LLM_TOOL schUpdate;
    schUpdate.name = "sch_update";
    schUpdate.description =
        "Update elements in the schematic. Accepts an array of updates - use for single or batch operations. "
        "Can modify position, rotation, mirror, and properties. Target by reference or UUID. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schUpdate.input_schema = {
        { "type", "object" },
        { "properties", {
            { "updates", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "target", {
                            { "type", "string" },
                            { "description", "Reference (e.g. 'R1') or UUID" }
                        }},
                        { "position", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "New position [x, y] in mm" }
                        }},
                        { "angle", {
                            { "type", "number" },
                            { "description", "CCW rotation: 0=default, 90=vertical, 180=flipped, 270=vertical-flipped" }
                        }},
                        { "mirror", {
                            { "type", "string" },
                            { "enum", json::array( { "none", "x", "y" } ) }
                        }},
                        { "properties", {
                            { "type", "object" },
                            { "description", "{Value, Footprint, ...}" }
                        }},
                        { "dnp", {
                            { "type", "boolean" },
                            { "description", "Do Not Populate flag" }
                        }}
                    }},
                    { "required", json::array( { "target" } ) }
                }},
                { "description", "Array of updates. Each must have 'target' plus properties to change." }
            }}
        }},
        { "required", json::array( { "updates" } ) }
    };
    tools.push_back( schUpdate );

    // sch_delete - Delete one or more elements
    LLM_TOOL schDelete;
    schDelete.name = "sch_delete";
    schDelete.description =
        "Delete elements from the schematic. Accepts an array of targets - use for single or batch operations. "
        "Target by reference designator or UUID. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schDelete.input_schema = {
        { "type", "object" },
        { "properties", {
            { "targets", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Array of references or UUIDs to delete (e.g. ['R1', 'R2', 'C1'])" }
            }}
        }},
        { "required", json::array( { "targets" } ) }
    };
    tools.push_back( schDelete );

    // sch_connect_to_power - Connect a pin to power in one call
    LLM_TOOL schConnectToPower;
    schConnectToPower.name = "sch_connect_to_power";
    schConnectToPower.description =
        "Connect a symbol pin to a power rail (GND, VCC, +3V3, etc.) in a single call. "
        "Places the power symbol and optionally draws a wire. "
        "This is a convenience tool that replaces 3 separate calls (place power, get pin position, draw wire). "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schConnectToPower.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ref", {
                { "type", "string" },
                { "description", "Reference designator of the symbol to connect (e.g., 'U1', 'R3')" }
            }},
            { "pin", {
                { "type", "string" },
                { "description", "Pin name or number to connect (e.g., 'VCC', 'GND', '1', '14')" }
            }},
            { "power", {
                { "type", "string" },
                { "description", "Power symbol name: 'GND', 'VCC', '+3V3', '+5V', '+3.3V', 'VBUS', 'VBAT', etc." }
            }},
            { "offset", {
                { "type", "array" },
                { "items", { { "type", "number" } } },
                { "description", "Offset from pin position for power symbol as [dx, dy] in mm. "
                                "Default [0, 0] places power symbol directly at pin (no wire needed). "
                                "Use [0, 5] to place GND 5mm below pin with connecting wire." }
            }},
            { "power_angle", {
                { "type", "number" },
                { "description", "CCW rotation for power symbol. GND: 180=arrow down, 0=arrow up. "
                                "VCC: 0=arrow up, 180=arrow down. Default: auto-detect." }
            }}
        }},
        { "required", json::array( { "ref", "pin", "power" } ) }
    };
    tools.push_back( schConnectToPower );

    // sch_annotate - Annotate schematic symbols
    LLM_TOOL schAnnotate;
    schAnnotate.name = "sch_annotate";
    schAnnotate.description =
        "Annotate symbols in the open schematic. Assigns reference designators (R1, C1, U1, etc.) "
        "to symbols that have '?' placeholders. Can re-annotate all symbols or only unannotated ones. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schAnnotate.input_schema = {
        { "type", "object" },
        { "properties", {
            { "scope", {
                { "type", "string" },
                { "enum", json::array( { "all", "unannotated_only", "current_sheet", "selection" } ) },
                { "description", "Annotation scope: 'all' annotates entire schematic, "
                                "'unannotated_only' only assigns to symbols with '?' (default), "
                                "'current_sheet' annotates current sheet only, "
                                "'selection' annotates selected symbols only" }
            }},
            { "sort_by", {
                { "type", "string" },
                { "enum", json::array( { "x_position", "y_position" } ) },
                { "description", "Sort order for annotation: 'x_position' (left to right), "
                                "'y_position' (top to bottom). Default: x_position" }
            }},
            { "reset_existing", {
                { "type", "boolean" },
                { "description", "If true, clear existing annotations before re-annotating. Default: false" }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( schAnnotate );

    // sch_get_nets - Get all nets and their connections
    LLM_TOOL schGetNets;
    schGetNets.name = "sch_get_nets";
    schGetNets.description =
        "Get a list of all nets in the schematic with their connected pins. "
        "Returns net names and the symbol pins connected to each net. "
        "Useful for verifying connections, debugging ERC issues, and understanding circuit topology. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schGetNets.input_schema = {
        { "type", "object" },
        { "properties", {
            { "filter", {
                { "type", "string" },
                { "description", "Optional filter to match net names (e.g., 'VCC', 'GND', 'DATA')" }
            }},
            { "include_unconnected", {
                { "type", "boolean" },
                { "description", "Include nets with only one pin connected (default: false)" }
            }}
        }},
        { "required", json::array() }
    };
    schGetNets.read_only = true;
    tools.push_back( schGetNets );

    // ===== PCB Tools =====

    // pcb_get_summary - Get high-level overview of PCB
    LLM_TOOL pcbGetSummary;
    pcbGetSummary.name = "pcb_get_summary";
    pcbGetSummary.description =
        "Get a high-level overview of the open PCB. Returns JSON with footprints, tracks, "
        "vias, zones, board outline, layer count, and net information. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {} },
        { "required", json::array() }
    };
    pcbGetSummary.read_only = true;
    tools.push_back( pcbGetSummary );

    // pcb_read_section - Read specific section of PCB
    LLM_TOOL pcbReadSection;
    pcbReadSection.name = "pcb_read_section";
    pcbReadSection.description =
        "Read a specific section of the open PCB in detail. "
        "Sections: footprints, tracks, vias, zones, drawings, nets, layers, stackup. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbReadSection.input_schema = {
        { "type", "object" },
        { "properties", {
            { "section", {
                { "type", "string" },
                { "enum", json::array( { "footprints", "tracks", "vias", "zones", "drawings", "nets", "layers", "stackup" } ) },
                { "description", "Section to read" }
            }},
            { "filter", {
                { "type", "string" },
                { "description", "Optional filter by reference (e.g., 'U*'), net name, or layer" }
            }}
        }},
        { "required", json::array( { "section" } ) }
    };
    pcbReadSection.read_only = true;
    tools.push_back( pcbReadSection );

    // pcb_run_drc - Run design rule check
    LLM_TOOL pcbRunDrc;
    pcbRunDrc.name = "pcb_run_drc";
    pcbRunDrc.description =
        "Run Design Rule Check (DRC) on the open PCB. "
        "Detects clearance violations, unconnected items, track/via issues, and other errors. "
        "Returns error/warning counts and violation details. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbRunDrc.input_schema = {
        { "type", "object" },
        { "properties", {
            { "refill_zones", {
                { "type", "boolean" },
                { "description", "Refill all zones before running DRC (default: true)" }
            }},
            { "output_format", {
                { "type", "string" },
                { "enum", json::array( { "summary", "detailed", "by_type" } ) },
                { "description", "Output format: 'summary', 'detailed', or 'by_type' (default: summary)" }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( pcbRunDrc );

    // pcb_set_outline - Set board outline/shape
    LLM_TOOL pcbSetOutline;
    pcbSetOutline.name = "pcb_set_outline";
    pcbSetOutline.description =
        "Set or replace the board outline (Edge.Cuts layer). "
        "Supports rectangle, polygon, or rounded rectangle shapes. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbSetOutline.input_schema = {
        { "type", "object" },
        { "properties", {
            { "shape", {
                { "type", "string" },
                { "enum", json::array( { "rectangle", "polygon", "rounded_rectangle" } ) },
                { "description", "Board shape type" }
            }},
            { "width", {
                { "type", "number" },
                { "description", "Board width in mm (for rectangle/rounded_rectangle)" }
            }},
            { "height", {
                { "type", "number" },
                { "description", "Board height in mm (for rectangle/rounded_rectangle)" }
            }},
            { "corner_radius", {
                { "type", "number" },
                { "description", "Corner radius in mm (for rounded_rectangle)" }
            }},
            { "origin", {
                { "type", "array" },
                { "items", { { "type", "number" } } },
                { "description", "Origin position as [x, y] in mm (default: [0, 0])" }
            }},
            { "points", {
                { "type", "array" },
                { "items", {
                    { "type", "array" },
                    { "items", { { "type", "number" } } }
                }},
                { "description", "For polygon: Array of [x, y] vertices in mm" }
            }},
            { "clear_existing", {
                { "type", "boolean" },
                { "description", "Remove existing board outline first (default: true)" }
            }}
        }},
        { "required", json::array( { "shape" } ) }
    };
    tools.push_back( pcbSetOutline );

    // pcb_sync_schematic - Import/update footprints from schematic
    LLM_TOOL pcbSyncSchematic;
    pcbSyncSchematic.name = "pcb_sync_schematic";
    pcbSyncSchematic.description =
        "Update PCB from schematic - imports new footprints, updates nets, removes deleted components. "
        "This is the standard 'Update PCB from Schematic' operation. "
        "REQUIRES: Both schematic and PCB editors must be open.";
    pcbSyncSchematic.input_schema = {
        { "type", "object" },
        { "properties", {
            { "delete_unused", {
                { "type", "boolean" },
                { "description", "Delete footprints not in schematic (default: false)" }
            }},
            { "replace_footprints", {
                { "type", "boolean" },
                { "description", "Replace footprints when library ID doesn't match (default: false)" }
            }},
            { "update_fields", {
                { "type", "boolean" },
                { "description", "Update field values from schematic (default: true)" }
            }},
            { "dry_run", {
                { "type", "boolean" },
                { "description", "Preview changes without applying (default: false)" }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( pcbSyncSchematic );

    // pcb_place - Batch footprint placement
    LLM_TOOL pcbPlace;
    pcbPlace.name = "pcb_place";
    pcbPlace.description =
        "Position multiple footprints in a single operation. Supports move, rotate, and flip. "
        "This is the most common PCB operation for layout. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbPlace.input_schema = {
        { "type", "object" },
        { "properties", {
            { "placements", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "ref", {
                            { "type", "string" },
                            { "description", "Reference designator (e.g., 'U1', 'R5')" }
                        }},
                        { "position", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Position as [x, y] in mm" }
                        }},
                        { "angle", {
                            { "type", "number" },
                            { "description", "Rotation angle in degrees (0, 90, 180, 270)" }
                        }},
                        { "layer", {
                            { "type", "string" },
                            { "enum", json::array( { "F.Cu", "B.Cu" } ) },
                            { "description", "Layer: 'F.Cu' (top) or 'B.Cu' (bottom/flipped)" }
                        }}
                    }},
                    { "required", json::array( { "ref" } ) }
                }},
                { "description", "Array of placement operations" }
            }}
        }},
        { "required", json::array( { "placements" } ) }
    };
    tools.push_back( pcbPlace );

    // pcb_add - Batch add elements to PCB (matches sch_add pattern)
    LLM_TOOL pcbAdd;
    pcbAdd.name = "pcb_add";
    pcbAdd.description =
        "Add elements to the PCB. Accepts an array of elements - use for single or batch operations. "
        "Returns IDs for all created elements. "
        "REQUIRES: PCB editor must be open with a document loaded.\n\n"
        "ELEMENT TYPES:\n"
        "- track: {element_type, layer, width?, net?, points:[[x,y],...]}\n"
        "- via: {element_type, position:[x,y], net?, size?, drill?}\n"
        "- zone: {element_type, layer, net, outline:[[x,y],...], priority?}\n"
        "- keepout: {element_type, layers:[], outline:[[x,y],...], no_tracks?, no_vias?, no_pour?}\n"
        "- line: {element_type, layer, width?, points:[[x,y],[x,y]]}\n"
        "- rectangle: {element_type, layer, width?, top_left:[x,y], bottom_right:[x,y], filled?}\n"
        "- circle: {element_type, layer, width?, center:[x,y], radius, filled?}\n"
        "- arc: {element_type, layer, width?, center:[x,y], radius, start_angle, end_angle}\n"
        "- text: {element_type, layer, position:[x,y], text, text_size?, thickness?}\n\n"
        "EXAMPLE (route with via layer transition):\n"
        "elements: [\n"
        "  {element_type:'track', layer:'F.Cu', width:0.25, net:'VCC', points:[[50,30],[60,30]]},\n"
        "  {element_type:'via', position:[60,30], net:'VCC'},\n"
        "  {element_type:'track', layer:'B.Cu', width:0.25, net:'VCC', points:[[60,30],[70,30]]}\n"
        "]";
    pcbAdd.input_schema = {
        { "type", "object" },
        { "properties", {
            { "elements", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "element_type", {
                            { "type", "string" },
                            { "enum", json::array( { "track", "via", "zone", "keepout", "line", "rectangle", "circle", "arc", "text" } ) },
                            { "description", "Type of element to add" }
                        }},
                        { "layer", {
                            { "type", "string" },
                            { "description", "Layer name (e.g., 'F.Cu', 'B.Cu', 'F.SilkS')" }
                        }},
                        { "width", {
                            { "type", "number" },
                            { "description", "Track/line width in mm (default: 0.25)" }
                        }},
                        { "net", {
                            { "type", "string" },
                            { "description", "Net name for track/via/zone" }
                        }},
                        { "points", {
                            { "type", "array" },
                            { "items", { { "type", "array" } } },
                            { "description", "Array of [x, y] points in mm" }
                        }},
                        { "position", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Position as [x, y] in mm" }
                        }},
                        { "size", {
                            { "type", "number" },
                            { "description", "Via size in mm (default: 0.8)" }
                        }},
                        { "drill", {
                            { "type", "number" },
                            { "description", "Via drill diameter in mm (default: 0.4)" }
                        }},
                        { "outline", {
                            { "type", "array" },
                            { "description", "Zone/keepout outline as [[x,y],...] vertices" }
                        }},
                        { "layers", {
                            { "type", "array" },
                            { "items", { { "type", "string" } } },
                            { "description", "For keepout: layers to apply to" }
                        }},
                        { "no_tracks", { { "type", "boolean" } } },
                        { "no_vias", { { "type", "boolean" } } },
                        { "no_pour", { { "type", "boolean" } } },
                        { "priority", { { "type", "integer" } } },
                        { "top_left", { { "type", "array" } } },
                        { "bottom_right", { { "type", "array" } } },
                        { "center", { { "type", "array" } } },
                        { "radius", { { "type", "number" } } },
                        { "start_angle", { { "type", "number" } } },
                        { "end_angle", { { "type", "number" } } },
                        { "text", { { "type", "string" } } },
                        { "text_size", { { "type", "number" } } },
                        { "thickness", { { "type", "number" } } },
                        { "filled", { { "type", "boolean" } } }
                    }},
                    { "required", json::array( { "element_type" } ) }
                }},
                { "description", "Array of elements to add. Processed in order." }
            }}
        }},
        { "required", json::array( { "elements" } ) }
    };
    tools.push_back( pcbAdd );

    // pcb_update - Batch update elements (matches sch_update pattern)
    LLM_TOOL pcbUpdate;
    pcbUpdate.name = "pcb_update";
    pcbUpdate.description =
        "Update elements in the PCB. Accepts an array of updates - use for single or batch operations. "
        "Target footprints by reference designator (e.g., 'U1') or any element by UUID. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbUpdate.input_schema = {
        { "type", "object" },
        { "properties", {
            { "updates", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "target", {
                            { "type", "string" },
                            { "description", "Reference designator (e.g., 'U1') or UUID" }
                        }},
                        { "position", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "New position [x, y] in mm" }
                        }},
                        { "angle", {
                            { "type", "number" },
                            { "description", "Rotation angle in degrees" }
                        }},
                        { "layer", {
                            { "type", "string" },
                            { "description", "'F.Cu' or 'B.Cu' (flips footprint)" }
                        }},
                        { "net", {
                            { "type", "string" },
                            { "description", "New net name (for tracks/vias/zones)" }
                        }},
                        { "width", {
                            { "type", "number" },
                            { "description", "New track/line width in mm" }
                        }},
                        { "locked", {
                            { "type", "boolean" },
                            { "description", "Lock/unlock element" }
                        }},
                        { "text", {
                            { "type", "string" },
                            { "description", "New text content" }
                        }},
                        { "outline", {
                            { "type", "array" },
                            { "description", "New zone/keepout outline [[x,y],...]" }
                        }}
                    }},
                    { "required", json::array( { "target" } ) }
                }},
                { "description", "Array of updates. Each must have 'target' plus properties to change." }
            }}
        }},
        { "required", json::array( { "updates" } ) }
    };
    tools.push_back( pcbUpdate );

    // pcb_delete - Batch delete elements (matches sch_delete pattern)
    LLM_TOOL pcbDelete;
    pcbDelete.name = "pcb_delete";
    pcbDelete.description =
        "Delete elements from the PCB. Accepts an array of targets - use for single or batch operations. "
        "Target footprints by reference designator (e.g., 'U1') or any element by UUID. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbDelete.input_schema = {
        { "type", "object" },
        { "properties", {
            { "targets", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Array of reference designators or UUIDs to delete (e.g., ['R1', 'C1', 'uuid-here'])" }
            }},
            { "query", {
                { "type", "object" },
                { "properties", {
                    { "layer", { { "type", "string" } } },
                    { "type", { { "type", "string" } } },
                    { "net", { { "type", "string" } } }
                }},
                { "description", "Alternative: query to select elements (e.g., {\"layer\": \"F.SilkS\", \"type\": \"text\"})" }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( pcbDelete );

    // pcb_get_pads - Get pad positions for a footprint (like sch_get_pins)
    LLM_TOOL pcbGetPads;
    pcbGetPads.name = "pcb_get_pads";
    pcbGetPads.description =
        "Get pad positions for a placed footprint. Returns exact pad coordinates for routing. "
        "This is the PCB equivalent of sch_get_pins - use it to get precise connection points. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbGetPads.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ref", {
                { "type", "string" },
                { "description", "Reference designator of the footprint (e.g., 'U1', 'R3', 'C5')" }
            }}
        }},
        { "required", json::array( { "ref" } ) }
    };
    pcbGetPads.read_only = true;
    tools.push_back( pcbGetPads );

    // pcb_get_footprint - Get detailed footprint info including pads
    LLM_TOOL pcbGetFootprint;
    pcbGetFootprint.name = "pcb_get_footprint";
    pcbGetFootprint.description =
        "Get detailed information about a placed footprint including position, orientation, "
        "all pads with their positions and net assignments, and courtyard bounds. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbGetFootprint.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ref", {
                { "type", "string" },
                { "description", "Reference designator of the footprint (e.g., 'U1', 'R3')" }
            }}
        }},
        { "required", json::array( { "ref" } ) }
    };
    pcbGetFootprint.read_only = true;
    tools.push_back( pcbGetFootprint );

    // pcb_route - High-level pad-to-pad routing
    LLM_TOOL pcbRoute;
    pcbRoute.name = "pcb_route";
    pcbRoute.description =
        "Draw a track between two pads with optional via layer transitions. "
        "This is a convenience tool that handles coordinate lookup and multi-segment routing. "
        "For simple connections, specify from/to pads. For complex routes, add waypoints. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbRoute.input_schema = {
        { "type", "object" },
        { "properties", {
            { "from", {
                { "type", "object" },
                { "properties", {
                    { "ref", { { "type", "string" }, { "description", "Footprint reference (e.g., 'U1')" } } },
                    { "pad", { { "type", "string" }, { "description", "Pad number/name (e.g., '1', 'VCC')" } } }
                }},
                { "description", "Starting pad: {ref, pad}" }
            }},
            { "to", {
                { "type", "object" },
                { "properties", {
                    { "ref", { { "type", "string" } } },
                    { "pad", { { "type", "string" } } }
                }},
                { "description", "Ending pad: {ref, pad}" }
            }},
            { "width", {
                { "type", "number" },
                { "description", "Track width in mm (default: net class width or 0.25)" }
            }},
            { "layer", {
                { "type", "string" },
                { "description", "Starting layer (default: auto-detect from pad)" }
            }},
            { "waypoints", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "position", { { "type", "array" }, { "description", "[x, y] in mm" } } },
                        { "via", { { "type", "boolean" }, { "description", "Place via at this waypoint" } } },
                        { "layer", { { "type", "string" }, { "description", "Switch to this layer after via" } } }
                    }}
                }},
                { "description", "Intermediate waypoints with optional layer transitions" }
            }}
        }},
        { "required", json::array( { "from", "to" } ) }
    };
    tools.push_back( pcbRoute );

    // pcb_get_nets - Get net information with connections
    LLM_TOOL pcbGetNets;
    pcbGetNets.name = "pcb_get_nets";
    pcbGetNets.description =
        "Get a list of all nets in the PCB with their connected pads and routing status. "
        "Useful for verifying connections, finding unrouted nets, and understanding connectivity. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbGetNets.input_schema = {
        { "type", "object" },
        { "properties", {
            { "filter", {
                { "type", "string" },
                { "description", "Optional filter to match net names (e.g., 'VCC', 'GND', 'DATA*')" }
            }},
            { "include_pads", {
                { "type", "boolean" },
                { "description", "Include list of connected pads for each net (default: true)" }
            }},
            { "unrouted_only", {
                { "type", "boolean" },
                { "description", "Only return nets with unconnected pads (default: false)" }
            }}
        }},
        { "required", json::array() }
    };
    pcbGetNets.read_only = true;
    tools.push_back( pcbGetNets );

    // pcb_export - Generate output files
    LLM_TOOL pcbExport;
    pcbExport.name = "pcb_export";
    pcbExport.description =
        "Export PCB to various output formats: Gerber, drill files, PDF, SVG, STEP, etc. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbExport.input_schema = {
        { "type", "object" },
        { "properties", {
            { "format", {
                { "type", "string" },
                { "enum", json::array( { "gerber", "drill", "pdf", "svg", "step", "dxf", "pos" } ) },
                { "description", "Export format" }
            }},
            { "output_dir", {
                { "type", "string" },
                { "description", "Output directory path" }
            }},
            { "layers", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Layers to export (for gerber/pdf/svg). Empty = all copper layers" }
            }},
            { "include_edge_cuts", {
                { "type", "boolean" },
                { "description", "Include board outline in export (default: true)" }
            }},
            { "drill_format", {
                { "type", "string" },
                { "enum", json::array( { "excellon", "gerber_x2" } ) },
                { "description", "Drill file format (default: excellon)" }
            }},
            { "separate_npth", {
                { "type", "boolean" },
                { "description", "Separate NPTH (non-plated) drill file (default: true)" }
            }}
        }},
        { "required", json::array( { "format", "output_dir" } ) }
    };
    tools.push_back( pcbExport );

    // screenshot - Export a visual render of a schematic or PCB
    LLM_TOOL screenshot;
    screenshot.name = "screenshot";
    screenshot.description =
        "Export a visual screenshot (PNG render) of a schematic or PCB file. "
        "Returns the image for visual inspection. Use this to verify layout, "
        "check component placement, review wiring, or confirm design changes. "
        "The file must be a .kicad_sch (schematic) or .kicad_pcb (PCB layout) file.";
    screenshot.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_sch or .kicad_pcb file to screenshot" }
            }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    screenshot.read_only = true;
    tools.push_back( screenshot );

    return tools;
}


std::string BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput )
{
    wxLogInfo( "AgentTools::BuildToolPayload called for tool: %s", aToolName.c_str() );
    // Build the command string for the terminal based on tool name
    if( aToolName == "run_terminal" )
    {
        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return "run_terminal " + command;
    }

    return "Error: Unknown tool '" + aToolName + "'";
}


std::string ExecuteToolSync( const std::string& aToolName, const nlohmann::json& aInput,
                              std::function<std::string( int, const std::string& )> aSendRequestFn )
{
    wxLogInfo( "AgentTools::ExecuteToolSync called for tool: %s", aToolName.c_str() );

    // Check if this is a registered tool (sch_*, pcb_*)
    if( TOOL_REGISTRY::Instance().HasHandler( aToolName ) )
    {
        // Check if this tool requires IPC execution (e.g., ERC needs live KiCad state)
        if( TOOL_REGISTRY::Instance().RequiresIPC( aToolName ) )
        {
            std::string command = TOOL_REGISTRY::Instance().GetIPCCommand( aToolName, aInput );
            return aSendRequestFn( FRAME_TERMINAL, command );
        }

        // Provide the IPC send function so handlers can communicate with editor frames
        TOOL_REGISTRY::Instance().SetSendRequestFn( aSendRequestFn );

        // Execute directly without going through terminal frame
        return TOOL_REGISTRY::Instance().Execute( aToolName, aInput );
    }

    if( aToolName == "run_terminal" )
    {
        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return aSendRequestFn( FRAME_TERMINAL, "run_terminal " + command );
    }

    return "Error: Unknown tool '" + aToolName + "'";
}


wxString GetToolDescription( const std::string& aToolName, const nlohmann::json& aInput )
{
    wxLogInfo( "AgentTools::GetToolDescription called for tool: %s", aToolName.c_str() );

    // Check if this is a direct file tool (sch_*, pcb_*)
    if( TOOL_REGISTRY::Instance().HasHandler( aToolName ) )
    {
        return wxString::FromUTF8( TOOL_REGISTRY::Instance().GetDescription( aToolName, aInput ) );
    }

    // Generate human-readable description based on tool name and input
    if( aToolName == "run_terminal" )
    {
        std::string cmd = aInput.value( "command", "" );
        if( cmd.length() > 50 )
            cmd = cmd.substr( 0, 47 ) + "...";
        return wxString::Format( "Running: %s", cmd );
    }
    else if( aToolName == "open_editor" )
    {
        std::string editorType = aInput.value( "editor_type", "" );
        wxString label = ( editorType == "sch" ) ? "Schematic" : "PCB";
        return wxString::Format( "Open %s Editor", label );
    }
    else if( aToolName == "check_status" )
    {
        return "Check Project Status";
    }
    else if( aToolName == "save" )
    {
        return "Save";
    }
    else if( aToolName == "create_project" )
    {
        return "Create Project";
    }
    else
    {
        return wxString::Format( "Executing %s", aToolName );
    }
}

} // namespace AgentTools
