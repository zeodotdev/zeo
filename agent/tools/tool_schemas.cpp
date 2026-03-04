#include "tool_schemas.h"
#include "../agent_llm_client.h"
#include <nlohmann/json.hpp>
#include <wx/log.h>

namespace ToolSchemas
{

using json = nlohmann::json;


static void AddGeneralTools( std::vector<LLM_TOOL>& tools )
{
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

    // datasheet_query - Query extracted datasheet data for a component
    LLM_TOOL datasheetQuery;
    datasheetQuery.name = "datasheet_query";
    datasheetQuery.description =
        "Query extracted datasheet data for an electronic component. Returns detailed specifications "
        "including electrical ratings, pin definitions, packages, placement rules, design guidelines, "
        "decoupling requirements, and external parts. Data is automatically extracted from datasheets "
        "when components are placed in the schematic. Returns extraction_status: 'completed', "
        "'processing' (check back later), or 'not_found'.";
    datasheetQuery.input_schema = {
        { "type", "object" },
        { "properties", {
            { "part_number", {
                { "type", "string" },
                { "description", "Component part number (e.g., 'CP2102N', 'STM32F411CEU6')" }
            }},
            { "manufacturer", {
                { "type", "string" },
                { "description", "Component manufacturer (optional, helps disambiguate)" }
            }}
        }},
        { "required", json::array( { "part_number" } ) }
    };
    datasheetQuery.read_only = true;
    datasheetQuery.defer_loading = true;
    tools.push_back( datasheetQuery );

    // extract_datasheet - Synchronously extract datasheet data for a component
    LLM_TOOL extractDatasheet;
    extractDatasheet.name = "extract_datasheet";
    extractDatasheet.description =
        "Extract component data from a PDF datasheet URL. This calls the extraction "
        "service synchronously and waits for the result (may take 30-60 seconds). "
        "Use this when you need datasheet data immediately for a component that hasn't "
        "been extracted yet (datasheet_query returned 'not_found'). The extracted data "
        "is stored in the database and can be queried later with datasheet_query. "
        "Returns the full extracted component data including pins, electrical specs, "
        "and design guidelines.";
    extractDatasheet.input_schema = {
        { "type", "object" },
        { "properties", {
            { "part_number", {
                { "type", "string" },
                { "description", "Component part number (e.g., 'CP2102N', 'ESP32-C3-WROOM-02')" }
            }},
            { "manufacturer", {
                { "type", "string" },
                { "description", "Component manufacturer (optional, helps disambiguate)" }
            }},
            { "datasheet_url", {
                { "type", "string" },
                { "description", "URL to the component's PDF datasheet" }
            }}
        }},
        { "required", json::array( { "part_number", "datasheet_url" } ) }
    };
    extractDatasheet.defer_loading = true;
    tools.push_back( extractDatasheet );

    // generate_symbol - Generate a KiCad symbol from a datasheet
    LLM_TOOL generateSymbol;
    generateSymbol.name = "generate_symbol";
    generateSymbol.description =
        "Generate a KiCad schematic symbol (.kicad_sym) from a datasheet PDF. "
        "Self-contained: checks local libraries first (by datasheet URL), then checks "
        "the component database, and auto-extracts from the PDF if needed. "
        "Returns the lib_id that can be used with sch_add to place the symbol. "
        "Prefer passing datasheet_url for reliable deduplication. "
        "If the symbol already exists, pass force=true to regenerate it.";
    generateSymbol.input_schema = {
        { "type", "object" },
        { "properties", {
            { "part_number", {
                { "type", "string" },
                { "description", "Component part number (used as symbol name)" }
            }},
            { "datasheet_url", {
                { "type", "string" },
                { "description", "URL to the PDF datasheet. Used to check for existing symbols "
                                 "and to auto-extract pin data if not already in the database." }
            }},
            { "manufacturer", {
                { "type", "string" },
                { "description", "Component manufacturer (optional)" }
            }},
            { "library_name", {
                { "type", "string" },
                { "description", "Name for the output library file (without .kicad_sym extension). "
                                 "Defaults to 'project'. The file is created in the project directory." }
            }},
            { "force", {
                { "type", "boolean" },
                { "description", "If true, regenerate the symbol even if it already exists in the library. "
                                 "The old symbol will be replaced." }
            }}
        }},
        { "required", json::array( { "datasheet_url" } ) }
    };
    generateSymbol.defer_loading = true;
    tools.push_back( generateSymbol );

    // generate_footprint - Generate a KiCad footprint from a datasheet
    LLM_TOOL generateFootprint;
    generateFootprint.name = "generate_footprint";
    generateFootprint.description =
        "Generate a KiCad PCB footprint (.kicad_mod) from a datasheet PDF. "
        "Self-contained: first tries to match an existing KiCad standard library footprint "
        "from the package dimensions, then generates a custom footprint as fallback. "
        "Fetches package data from the component database (auto-extracts if needed). "
        "Returns the lib_id (Library:Footprint) that can be used to assign to a symbol. "
        "Prefer passing datasheet_url for reliable deduplication. "
        "If the footprint already exists, pass force=true to regenerate it.";
    generateFootprint.input_schema = {
        { "type", "object" },
        { "properties", {
            { "part_number", {
                { "type", "string" },
                { "description", "Component part number" }
            }},
            { "datasheet_url", {
                { "type", "string" },
                { "description", "URL to the PDF datasheet. Used to look up package data "
                                 "and to auto-extract if not already in the database." }
            }},
            { "manufacturer", {
                { "type", "string" },
                { "description", "Component manufacturer (optional)" }
            }},
            { "library_name", {
                { "type", "string" },
                { "description", "Name for the output .pretty library folder (without extension). "
                                 "Defaults to 'project'. Created in the project directory." }
            }},
            { "force", {
                { "type", "boolean" },
                { "description", "If true, regenerate the footprint even if it already exists. "
                                 "The old footprint file will be overwritten." }
            }}
        }},
        { "required", json::array( { "datasheet_url" } ) }
    };
    generateFootprint.defer_loading = true;
    tools.push_back( generateFootprint );

    // sch_import_symbol - Import a pre-built KiCad symbol+footprint from cse_get_kicad
    LLM_TOOL importSymbol;
    importSymbol.name = "sch_import_symbol";
    importSymbol.description =
        "Import a KiCad symbol and footprint (from component_search get_kicad) into the project "
        "library so the part can be immediately placed with sch_add. "
        "Writes the symbol into <library_name>.kicad_sym and the footprint into "
        "<library_name>.pretty/<name>.kicad_mod, registers both in sym-lib-table and fp-lib-table, "
        "and returns lib_id + footprint_lib_id. "
        "Pass kicad_symbol and kicad_footprint directly from component_search get_kicad output. "
        "Typical 3-step flow: (1) component_search { action: get_kicad, query: ... } "
        "→ kicad_symbol + kicad_footprint, "
        "(2) sch_import_symbol { kicad_symbol, kicad_footprint } → lib_id, "
        "(3) sch_add { elements: [{ lib_id }] }. "
        "If the symbol already exists, returns the existing lib_id (use force=true to replace). "
        "symbol_name and library_name are optional — name is extracted from the content, "
        "library defaults to 'project'.";
    importSymbol.input_schema = {
        { "type", "object" },
        { "properties", {
            { "kicad_symbol", {
                { "type", "string" },
                { "description", "Raw .kicad_sym S-expression from component_search get_kicad "
                                 "(the kicad_symbol field). Contains symbol pins, properties, and graphics." }
            }},
            { "kicad_footprint", {
                { "type", "string" },
                { "description", "Raw .kicad_mod S-expression from component_search get_kicad "
                                 "(the kicad_footprint field). Contains pad layout, silkscreen, courtyard." }
            }},
            { "symbol_name", {
                { "type", "string" },
                { "description", "Override for the symbol name (lib_id symbol part). "
                                 "If omitted, extracted automatically from kicad_symbol content." }
            }},
            { "library_name", {
                { "type", "string" },
                { "description", "Name for the library files (without extension). "
                                 "Defaults to 'project'. Symbol goes to project.kicad_sym, "
                                 "footprint goes to project.pretty/." }
            }},
            { "force", {
                { "type", "boolean" },
                { "description", "If true, overwrite an existing symbol/footprint with the same name. Default false." }
            }}
        }},
        { "required", json::array( { "kicad_symbol" } ) }
    };
    tools.push_back( importSymbol );

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
    createProject.defer_loading = true;
    tools.push_back( createProject );

    // screenshot - Export a visual render of a schematic or PCB
    LLM_TOOL screenshot;
    screenshot.name = "screenshot";
    screenshot.description =
        "Export a visual screenshot (PNG render) of a schematic or PCB file. "
        "Returns the image for visual inspection. Use this to verify layout, "
        "check component placement, review wiring, or confirm design changes. "
        "If file_path is omitted, screenshots the currently open sheet or PCB — "
        "this is the preferred default. Only pass file_path when you need to "
        "screenshot a specific sub-sheet that is NOT currently open.";
    screenshot.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to a .kicad_sch or .kicad_pcb file. "
                                 "If omitted, screenshots the currently open/visible sheet or PCB. "
                                 "Only needed to screenshot a specific sub-sheet that isn't currently visible." }
            }},
            { "view", {
                { "type", "string" },
                { "enum", json::array({ "top", "bottom" }) },
                { "description", "PCB view perspective. 'top' shows front layers, "
                                 "'bottom' shows back layers (mirrored). If omitted, shows all layers overlaid." }
            }},
            { "layers", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Layers to include: 'F.Cu', 'B.Cu', 'In1.Cu'-'In30.Cu', "
                                 "'F.SilkS', 'B.SilkS', 'F.Mask', 'B.Mask', 'Edge.Cuts', etc. "
                                 "If omitted, uses default layers for the view." }
            }},
            { "show_zones", {
                { "type", "boolean" },
                { "description", "Show filled copper zones. Default: true. When false, draws zone outlines only." }
            }},
            { "show_vias", {
                { "type", "boolean" },
                { "description", "Show filled vias. Default: true. When false, draws via outlines only." }
            }},
            { "show_pads", {
                { "type", "boolean" },
                { "description", "Show filled pads. Default: true. When false, draws pad outlines only." }
            }},
            { "show_tracks", {
                { "type", "boolean" },
                { "description", "Show filled tracks/traces. Default: true. When false, draws thin centerlines." }
            }},
            { "show_values", {
                { "type", "boolean" },
                { "description", "Show component value text. Default: true" }
            }},
            { "show_references", {
                { "type", "boolean" },
                { "description", "Show reference designators (R1, C1). Default: true" }
            }}
        }},
        { "required", json::array() }
    };
    screenshot.read_only = true;
    tools.push_back( screenshot );

}


static void AddSchematicTools( std::vector<LLM_TOOL>& tools )
{
    // sch_get_summary - Get high-level overview of schematic via IPC
    LLM_TOOL schGetSummary;
    schGetSummary.name = "sch_get_summary";
    schGetSummary.description = "Get a high-level overview of the schematic from the live editor. "
                                "Returns a lightweight summary: symbols (ref, value, lib_id, pos, pin_count), "
                                "labels (text, type, pos), sheets (name, file, pin_count), and element counts. "
                                "Includes an audit section that flags orphaned items (power symbols with no wire "
                                "connections, labels not touching any wire or pin, junctions with fewer than 2 wires). "
                                "For detailed data use sch_get_pins (pin positions), sch_inspect (wires, "
                                "junctions, full symbol fields), or sch_find_symbol (library lookup). "
                                "REQUIRES: Schematic editor must be open with a document loaded.";
    schGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {} },
        { "required", json::array() }
    };
    schGetSummary.read_only = true;
    schGetSummary.group = ToolGroup::SCHEMATIC;
    tools.push_back( schGetSummary );

    // sch_inspect - Inspect specific section of schematic
    LLM_TOOL schReadSection;
    schReadSection.name = "sch_inspect";
    schReadSection.description = "Inspect a specific section of the schematic from the live editor. "
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
    schReadSection.group = ToolGroup::SCHEMATIC;
    tools.push_back( schReadSection );

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
    schRunErc.read_only = true;
    schRunErc.group = ToolGroup::SCHEMATIC;
    tools.push_back( schRunErc );

    // sch_run_simulation - Run SPICE simulation on open schematic
    LLM_TOOL schRunSim;
    schRunSim.name = "sch_run_simulation";
    schRunSim.description =
        "Run a SPICE simulation on the currently open schematic. "
        "Returns trace summaries with signal names, point counts, and min/max/final values. "
        "When matplotlib is available, also generates a visual plot of signal traces. "
        "By default, plots user-facing signals (net labels like /in, /out) and filters out "
        "internal nodes and branch currents. Use plot_signals to select specific traces. "
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
            }},
            { "plot_signals", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Signal names to plot (e.g. [\"/in\", \"/out\", \"/outb\"]). "
                    "If omitted, auto-selects user-facing signals (net labels, named nodes) "
                    "and filters out internal subcircuit nodes, branch currents, and constant rails." }
            }}
        }},
        { "required", json::array( { "command" } ) }
    };
    schRunSim.group = ToolGroup::SCHEMATIC;
    schRunSim.defer_loading = true;
    tools.push_back( schRunSim );

    // sch_find_symbol - Query symbol library for pin positions
    LLM_TOOL schGetLibSymbol;
    schGetLibSymbol.name = "sch_find_symbol";
    schGetLibSymbol.description =
        "Query symbol library for symbol definitions including pin positions and body size. "
        "Accepts a symbol name ('R') or full Library:Symbol identifier ('Device:R'). "
        "If just a name is given, searches all libraries. "
        "Supports wildcards ('Connector:Conn_01x*') and regex ('Device:R_[0-9]{4}'). "
        "Returns pin positions relative to symbol origin, body_size (width/height in mm), datasheet URL, and footprint_filters. "
        "Use this before placing/wiring to get accurate pin locations and symbol dimensions. "
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
    schGetLibSymbol.group = ToolGroup::SCHEMATIC;
    tools.push_back( schGetLibSymbol );

    // sch_get_pins - Lightweight pin lookup for a single placed symbol
    LLM_TOOL schGetPins;
    schGetPins.name = "sch_get_pins";
    schGetPins.description =
        "Get pin positions for a single placed symbol by reference designator. "
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
    schGetPins.group = ToolGroup::SCHEMATIC;
    tools.push_back( schGetPins );

    // sch_symbols - Query symbols with comprehensive data including footprint and connectivity
    LLM_TOOL schSymbols;
    schSymbols.name = "sch_symbols";
    schSymbols.description =
        "Query schematic symbols with comprehensive data including footprint and pin connectivity. "
        "Returns for each symbol: ref, value, footprint, position, and pins with connection status. "
        "Pin connectivity shows which pins are connected to wires/labels and their net names. "
        "Use sch_update with properties.Footprint to change footprint assignments. "
        "REQUIRES: Schematic editor must be open.";
    schSymbols.input_schema = {
        { "type", "object" },
        { "properties", {
            { "filter", {
                { "type", "string" },
                { "description", "Filter by reference pattern (glob): 'R*', 'U?', 'C1'. Omit for all." }
            }},
            { "library", {
                { "type", "string" },
                { "description", "Filter by library name (e.g., 'Device')" }
            }},
            { "refs", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "description", "Specific refs to query: ['R1', 'C2', 'U3']" }
            }},
            { "include_library_info", {
                { "type", "boolean" },
                { "description", "Include footprint_filters, description. Default: false" }
            }},
            { "include_connectivity", {
                { "type", "boolean" },
                { "description", "Include pin connection status. Default: true" }
            }}
        }},
        { "required", json::array() }
    };
    schSymbols.read_only = true;
    schSymbols.group = ToolGroup::SCHEMATIC;
    tools.push_back( schSymbols );

    // sch_get_nets - Query schematic net connectivity
    LLM_TOOL schGetNets;
    schGetNets.name = "sch_get_nets";
    schGetNets.description =
        "Query schematic net connectivity. Returns which symbol pins are connected to which nets. "
        "Use filter to query specific nets, or include_unconnected to find floating pins.";
    schGetNets.input_schema = {
        { "type", "object" },
        { "properties", {
            { "filter", {
                { "type", "string" },
                { "description", "Glob pattern to match net names (e.g. 'VCC', 'SPI_*'). Omit for all nets." }
            }},
            { "include_unconnected", {
                { "type", "boolean" },
                { "description", "Include pins not connected to any net. Default false." }
            }}
        }},
        { "required", json::array() }
    };
    schGetNets.read_only = true;
    schGetNets.group = ToolGroup::SCHEMATIC;
    tools.push_back( schGetNets );

    // ===== IPC-based CRUD Tools (sch_add, sch_update, sch_delete, sch_batch_delete) =====
    // These work on the LIVE schematic via kipy API

    // sch_add - Add one or more elements to schematic
    LLM_TOOL schAdd;
    schAdd.name = "sch_add";
    schAdd.description =
        "Add elements to the schematic. Accepts an array of elements. "
        "Returns pin positions for all placed symbols. "
        "Rejects placements that overlap existing components. "
        "Use sch_connect_net for auto-routed wiring. Use wire element type for manual wires "
        "(rejected if any segment crosses a component bounding box).\n\n"
        "Element types: symbol, power, wire, label, no_connect, bus_entry.";
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
                            { "enum", json::array( { "symbol", "power", "wire", "label", "no_connect", "bus_entry" } ) },
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
                            { "description", "CCW rotation degrees (0, 90, 180, 270). "
                                            "Passives (R, C, L): "
                                            "0°=vertical (pin1 top, pin2 bottom), "
                                            "90°=horizontal (pin1 left, pin2 right), "
                                            "180°=vertical (pin1 bottom, pin2 top), "
                                            "270°=horizontal (pin1 right, pin2 left). "
                                            "Power symbols: see system prompt for wire-exit direction table." }
                        }},
                        { "mirror", {
                            { "type", "string" },
                            { "enum", json::array( { "none", "x", "y" } ) }
                        }},
                        { "unit", {
                            { "type", "integer" },
                            { "description", "Unit number for multi-unit symbols (e.g., 1-3 for LM358). Default: 1" }
                        }},
                        { "properties", {
                            { "type", "object" },
                            { "description", "{Value, Footprint, ...}" }
                        }},
                        { "points", {
                            { "type", "array" },
                            { "items", { { "type", "array" } } },
                            { "description", "Wire coordinates: [[x1,y1], [x2,y2], ...]. Segments are drawn between consecutive points." }
                        }},
                        { "text", {
                            { "type", "string" },
                            { "description", "Label text" }
                        }},
                        { "label_type", {
                            { "type", "string" },
                            { "enum", json::array( { "local", "global", "hierarchical" } ) },
                            { "description", "Label type. 'local' for intra-sheet net labels (default). 'hierarchical' for inter-sheet signals (creates sheet pins). 'global' only when a signal genuinely needs to be visible everywhere." }
                        }},
                        { "direction", {
                            { "type", "string" },
                            { "enum", json::array( { "right_down", "right_up", "left_down", "left_up" } ) },
                            { "description", "Bus entry direction. Default: right_down" }
                        }}
                    }},
                    { "required", json::array( { "element_type" } ) }
                }},
                { "description", "Array of elements to add to the schematic." }
            }}
        }},
        { "required", json::array( { "elements" } ) }
    };
    schAdd.group = ToolGroup::SCHEMATIC;
    tools.push_back( schAdd );

    // sch_update - Update one or more elements
    LLM_TOOL schUpdate;
    schUpdate.name = "sch_update";
    schUpdate.description =
        "Update elements in the schematic. Accepts an array of updates - use for single or batch operations. "
        "Can modify position, rotation, mirror, properties, and text field positions. Target by reference or UUID. "
        "Moves are rejected if the new position overlaps an existing component. "
        "Use 'fields' to reposition Reference/Value text relative to symbol center (avoids overlap). "
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
                        }},
                        { "fields", {
                            { "type", "object" },
                            { "description", "Reposition/rotate text fields relative to symbol center. "
                              "Keys: field names (Reference, Value). "
                              "Values: {offset?: [dx, dy], angle?: degrees} (both optional, provide either or both). "
                              "Examples: {\"Value\": {\"angle\": 90}}, {\"Reference\": {\"offset\": [0, -3], \"angle\": 0}}" },
                            { "additionalProperties", {
                                { "type", "object" },
                                { "properties", {
                                    { "offset", {
                                        { "type", "array" },
                                        { "items", { { "type", "number" } } },
                                        { "description", "[dx, dy] offset from symbol center in mm" }
                                    }},
                                    { "angle", {
                                        { "type", "number" },
                                        { "description", "Text rotation in degrees (0=horizontal, 90=vertical)" }
                                    }}
                                }}
                            }}
                        }}
                    }},
                    { "required", json::array( { "target" } ) }
                }},
                { "description", "Array of updates. Each must have 'target' plus properties to change." }
            }}
        }},
        { "required", json::array( { "updates" } ) }
    };
    schUpdate.group = ToolGroup::SCHEMATIC;
    tools.push_back( schUpdate );

    // sch_delete - Delete one or more elements
    LLM_TOOL schDelete;
    schDelete.name = "sch_delete";
    schDelete.description =
        "Delete elements from the schematic. Accepts an array of targets - use for single or batch operations. "
        "Each target can be a string (reference designator like 'R1' or UUID) or a query object to match items by properties:\n"
        "  {type: 'wire', start: [x,y], end: [x,y]} - match wire by endpoints\n"
        "  {type: 'wire', position: [x,y]} - match wires touching this point\n"
        "  {type: 'label', text: 'NET_NAME'} - match local label by text\n"
        "  {type: 'label', text: 'NET_NAME', position: [x,y]} - match label by text and position\n"
        "  {type: 'global_label', text: 'SPI_CLK'} - match global label\n"
        "  {type: 'hierarchical_label', text: 'DATA'} - match hierarchical label\n"
        "  {type: 'junction', position: [x,y]} - match junction at position\n"
        "  {type: 'no_connect', position: [x,y]} - match no-connect at position\n"
        "  {type: 'bus_entry', position: [x,y]} - match bus entry at position\n"
        "Query: type is required. text/position/start/end are optional filters. All specified must match. Position tolerance: 0.01mm. "
        "By default, recursively removes orphaned wires, junctions, and power symbols (#PWR) connected to deleted symbol pins. "
        "Set chain_delete: true to expand wire/junction/label targets to delete the ENTIRE connected net "
        "(all wires, junctions, labels on the same net). Does not delete symbols. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schDelete.input_schema = {
        { "type", "object" },
        { "properties", {
            { "targets", {
                { "type", "array" },
                { "description", "Array of targets to delete. Each item is either a string (reference/UUID) "
                  "or a query object with: type (required), text (optional), position [x,y] (optional), "
                  "start [x,y] (optional, wires), end [x,y] (optional, wires)" }
            }},
            { "cleanup_wires", {
                { "type", "boolean" },
                { "description", "Recursively remove orphaned wires, junctions, and power symbols (#PWR) connected to deleted symbol pins. Default: true." }
            }},
            { "chain_delete", {
                { "type", "boolean" },
                { "description", "When true, expand each wire/junction/label target to delete ALL wires, junctions, and labels on the same net. "
                  "Does not delete symbols or pins. Default: false." }
            }}
        }},
        { "required", json::array( { "targets" } ) }
    };
    schDelete.group = ToolGroup::SCHEMATIC;
    tools.push_back( schDelete );

    // sch_label_pins - Batch label pins on a symbol or sheet
    LLM_TOOL schLabelPins;
    schLabelPins.name = "sch_label_pins";
    schLabelPins.description =
        "Batch-label pins on a symbol or hierarchical sheet. Places labels directly at pin tips "
        "with auto-justified text based on pin orientation (text reads away from the component). "
        "If overlap is detected, tries flipping the label to the other side of the pin. "
        "Use this instead of placing labels one by one with sch_add. "
        "For sheets, pass the sheet name as ref and use sheet pin names as label keys. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schLabelPins.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ref", {
                { "type", "string" },
                { "description", "Reference designator of the symbol (e.g., 'U1') "
                                 "or name of a hierarchical sheet (e.g., 'Power Supply')" }
            }},
            { "labels", {
                { "type", "object" },
                { "description", "Map of pin number/name to label text. "
                                 "For symbols: {\"2\": \"EN\", \"18\": \"SPI_CS\"}. "
                                 "For sheets: keys are sheet pin names." },
                { "additionalProperties", { { "type", "string" } } }
            }},
            { "label_type", {
                { "type", "string" },
                { "enum", json::array( { "local", "global", "hierarchical" } ) },
                { "description", "Label type. 'local' for intra-sheet (default). "
                                 "'hierarchical' for inter-sheet signals. "
                                 "'global' only when genuinely needed everywhere." }
            }},
            { "h_align", {
                { "type", "string" },
                { "enum", json::array( { "left", "right" } ) },
                { "description", "Horizontal alignment of the label connection point. "
                                 "'left' places the pin on the left side, 'right' on the right. "
                                 "Overrides auto-justification when set." }
            }},
            { "v_align", {
                { "type", "string" },
                { "enum", json::array( { "top", "bottom" } ) },
                { "description", "Vertical alignment of the label connection point. "
                                 "'top' places the pin on the top, 'bottom' on the bottom. "
                                 "Overrides auto-justification when set." }
            }},
        }},
        { "required", json::array( { "ref", "labels" } ) }
    };
    schLabelPins.group = ToolGroup::SCHEMATIC;
    tools.push_back( schLabelPins );

    // sch_place_companions - Place companion components adjacent to IC pins
    LLM_TOOL schPlaceCompanions;
    schPlaceCompanions.name = "sch_place_companions";
    schPlaceCompanions.description =
        "Place companion components adjacent to an IC's pins. Companion circuits are small "
        "supporting parts (decoupling caps, pull-up/down resistors, termination resistors, "
        "filter caps, LED indicators) that wire directly to specific IC pins.\n\n"
        "The tool calculates optimal positions based on IC geometry and pin locations. "
        "Components are placed adjacent to pins with short wire stubs. "
        "Other connections use labels instead of long wires.\n\n"
        "Use this for IC support circuitry AFTER placing the IC with sch_add. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schPlaceCompanions.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ic_ref", {
                { "type", "string" },
                { "description", "Reference designator of anchor IC (e.g., 'U1')" }
            }},
            { "companions", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "lib_id", {
                            { "type", "string" },
                            { "description", "Library ID. Use 'Device:C', 'Device:R' for passives. Use 'power:GND', 'power:VCC' to place a power symbol directly at the pin (no component)." }
                        }},
                        { "ic_pin", {
                            { "type", "string" },
                            { "description", "IC pin number or name to place adjacent to" }
                        }},
                        { "offset_grids", {
                            { "type", "integer" },
                            { "description", "Distance in grid units (1.27mm each). Default: 3" }
                        }},
                        { "properties", {
                            { "type", "object" },
                            { "description", "Symbol properties like {\"Value\": \"100nF\", \"Footprint\": \"...\"}" },
                            { "additionalProperties", { { "type", "string" } } }
                        }},
                        { "terminal_labels", {
                            { "type", "object" },
                            { "description", "Net labels at companion terminals. Map pin number to label: {\"1\": \"OUT\"} for pin 1 (away from IC) or {\"2\": \"IN\"} for pin 2 (toward IC)" },
                            { "additionalProperties", { { "type", "string" } } }
                        }},
                        { "terminal_power", {
                            { "type", "object" },
                            { "description", "Power symbols at pin 1 (the terminal away from IC). Pin 2 connects to IC pin. Use {\"1\": \"GND\"} or {\"1\": \"VCC\"}" },
                            { "additionalProperties", { { "type", "string" } } }
                        }},
                        { "reverse", {
                            { "type", "boolean" },
                            { "description", "Swap pin orientation. When true, pin 1 faces IC (default: pin 2 faces IC). Use for polarized components like LEDs where you need opposite polarity." }
                        }},
                        { "chain", {
                            { "type", "array" },
                            { "description", "Chain of components extending from this companion's 'away' terminal. "
                                            "Each chain item has: lib_id, properties, terminal_power, terminal_labels, reverse, offset_grids. "
                                            "No ic_pin needed - chain items connect to parent's away terminal. "
                                            "The 'away' terminal is pin 1 normally, or pin 2 if parent has reverse:true. "
                                            "Chain items can have their own nested 'chain' for multi-component series (R→C→LED). "
                                            "Multiple items = branches (staggered perpendicular): [{LED1}, {LED2}] places two LEDs side-by-side. "
                                            "Example: {\"lib_id\": \"Device:R\", \"ic_pin\": \"PA0\", \"chain\": [{\"lib_id\": \"Device:LED\", \"reverse\": true, \"terminal_power\": {\"1\": \"GND\"}}]}" }
                        }}
                    }},
                    { "required", json::array( { "lib_id", "ic_pin" } ) }
                }},
                { "description", "Array of companion components to place" }
            }}
        }},
        { "required", json::array( { "ic_ref", "companions" } ) }
    };
    schPlaceCompanions.group = ToolGroup::SCHEMATIC;
    tools.push_back( schPlaceCompanions );

    // sch_add_sheet - Add a hierarchical sheet
    LLM_TOOL schAddSheet;
    schAddSheet.name = "sch_add_sheet";
    schAddSheet.description =
        "Add a hierarchical sheet to the schematic. Creates a new sub-sheet with its own .kicad_sch file. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schAddSheet.input_schema = {
        { "type", "object" },
        { "properties", {
            { "sheet_path", {
                { "type", "string" },
                { "description", "Full path of the new sheet to create "
                                "(e.g., '/Power Supply/' for a root-level sheet, "
                                "'/Parent Sheet/Child Sheet/' for a nested sheet). "
                                "The last segment becomes the sheet name. "
                                "Use sch_switch_sheet with no arguments to list existing sheets." }
            }},
            { "sheet_file", {
                { "type", "string" },
                { "description", "Filename for the sheet (e.g., 'power_supply.kicad_sch'). Defaults to sheet_name + '.kicad_sch'." }
            }},
            { "position", {
                { "type", "array" },
                { "items", { { "type", "number" } } },
                { "description", "Position [x, y] in mm. Auto-snapped to 1.27mm grid." }
            }},
            { "size", {
                { "type", "array" },
                { "items", { { "type", "number" } } },
                { "description", "Sheet size [width, height] in mm. Default: [50, 50]." }
            }}
        }},
        { "required", json::array( { "sheet_path" } ) }
    };
    schAddSheet.group = ToolGroup::SCHEMATIC;
    tools.push_back( schAddSheet );

    // sch_switch_sheet - Navigate between sheets in a hierarchical schematic
    LLM_TOOL schSwitchSheet;
    schSwitchSheet.name = "sch_switch_sheet";
    schSwitchSheet.description =
        "Navigate to a specific sheet in a hierarchical schematic. "
        "Use the human-readable sheet name or path (e.g., 'Power Supply' or '/Power Supply/'). "
        "Use '/' to navigate back to the root sheet. "
        "Call with no arguments to list available sheets. "
        "If multiple sheets share the same name, you must use the UUID instead to disambiguate. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schSwitchSheet.input_schema = {
        { "type", "object" },
        { "properties", {
            { "sheet_path", {
                { "type", "string" },
                { "description", "Sheet to navigate to. Use sheet name (e.g., 'Power Supply'), "
                                "path (e.g., '/Power Supply/'), or '/' for root sheet." }
            }}
        }},
        { "required", json::array() }
    };
    schSwitchSheet.read_only = true;
    schSwitchSheet.group = ToolGroup::SCHEMATIC;
    tools.push_back( schSwitchSheet );

    // sch_connect_net - Connect multiple pins on the same net in one call
    LLM_TOOL schConnectNet;
    schConnectNet.name = "sch_connect_net";
    schConnectNet.description =
        "Connect component pins with auto-routed wires. "
        "Resolves pin positions automatically and places junctions at T-connections. "
        "Use mode 'chain' (default) for series component paths (pins wired sequentially in order). "
        "Use mode 'star' for shared nodes where multiple pins tap the same net (trunk-and-branch).";
    schConnectNet.input_schema = {
        { "type", "object" },
        { "properties", {
            { "pins", {
                { "type", "array" },
                { "items", {
                    { "type", "string" },
                    { "description", "Pin specifier as 'REF:PIN' (e.g., 'R1:1', 'U1:VCC', '#PWR01:1') or label name without colon (e.g., 'VCC', 'OUT')" }
                }},
                { "minItems", 2 },
                { "description", "Array of pin specifiers or label names. "
                                "Pins: 'REF:PIN' (e.g., 'R1:2', 'U1:3', '#PWR01:1'). "
                                "Labels: name without colon (e.g., 'VCC', 'OUT'). "
                                "Example: ['U1:8', 'VCC']" }
            }},
            { "mode", {
                { "type", "string" },
                { "enum", json::array( { "chain", "star" } ) },
                { "description", "Routing topology. 'chain' (default): wire pins sequentially in order. "
                                "'star': trunk-and-branch for shared nodes like power fan-outs." }
            }}
        }},
        { "required", json::array( { "pins" } ) }
    };
    schConnectNet.group = ToolGroup::SCHEMATIC;
    tools.push_back( schConnectNet );

    // sch_annotate - Annotate schematic symbols
    LLM_TOOL schAnnotate;
    schAnnotate.name = "sch_annotate";
    schAnnotate.description =
        "Annotate symbols in the open schematic. Assigns reference designators (R1, C1, U1, etc.) "
        "to symbols that have '?' placeholders. Can re-annotate all symbols or only unannotated ones. "
        "Annotation is global across all sheets, so references on a sub-sheet may not start at 1 "
        "(e.g., R5, C3) — this is expected. "
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
    schAnnotate.group = ToolGroup::SCHEMATIC;
    schAnnotate.defer_loading = true;
    tools.push_back( schAnnotate );

    // sch_setup - Read/modify schematic document settings
    LLM_TOOL schSetup;
    schSetup.name = "sch_setup";
    schSetup.description =
        "Read or modify schematic document settings (Schematic Setup dialog). "
        "action='get' returns all settings. action='set' updates only provided fields. "
        "Use 'get' first to discover available settings and current values. "
        "REQUIRES: Schematic editor must be open with a document loaded.";
    schSetup.input_schema = {
        { "type", "object" },
        { "properties", {
            { "action", {
                { "type", "string" },
                { "enum", json::array( { "get", "set" } ) },
                { "description", "Action: 'get' retrieves settings, 'set' updates settings" }
            }},
            { "page", {
                { "type", "object" },
                { "description", "Page size settings (for set action)" },
                { "properties", {
                    { "size", { { "type", "string" }, { "enum", json::array( { "A5", "A4", "A3", "A2", "A1", "A0", "A", "B", "C", "D", "E", "USLetter", "USLegal", "USLedger", "USER" } ) } } },
                    { "portrait", { { "type", "boolean" } } },
                    { "width_mm", { { "type", "number" }, { "description", "Custom width (for USER size)" } } },
                    { "height_mm", { { "type", "number" }, { "description", "Custom height (for USER size)" } } }
                }}
            }},
            { "title_block", {
                { "type", "object" },
                { "description", "Title block information (for set action)" },
                { "properties", {
                    { "title", { { "type", "string" } } },
                    { "date", { { "type", "string" } } },
                    { "revision", { { "type", "string" } } },
                    { "company", { { "type", "string" } } },
                    { "comments", { { "type", "object" }, { "description", "Comments 1-9 as {\"comment1\": \"...\", ...}" } } }
                }}
            }},
            { "grid", {
                { "type", "object" },
                { "description", "Grid settings (for set action)" },
                { "properties", {
                    { "size_mm", { { "type", "number" } } },
                    { "size_mils", { { "type", "number" } } },
                    { "visible", { { "type", "boolean" } } },
                    { "snap", { { "type", "boolean" } } }
                }}
            }},
            { "formatting", {
                { "type", "object" },
                { "description", "Formatting settings from Schematic Setup (for set action)" },
                { "properties", {
                    { "text", {
                        { "type", "object" },
                        { "properties", {
                            { "default_text_size_mils", { { "type", "integer" }, { "description", "Default text size (mils)" } } },
                            { "overbar_offset_ratio", { { "type", "number" }, { "description", "Overbar offset (percentage)" } } },
                            { "label_offset_ratio", { { "type", "number" }, { "description", "Label offset (percentage)" } } },
                            { "global_label_margin_ratio", { { "type", "number" }, { "description", "Global label margin (percentage)" } } }
                        }}
                    }},
                    { "symbols", {
                        { "type", "object" },
                        { "properties", {
                            { "default_line_width_mils", { { "type", "integer" }, { "description", "Default line width (mils)" } } },
                            { "pin_symbol_size_mils", { { "type", "integer" }, { "description", "Pin symbol size (mils)" } } }
                        }}
                    }},
                    { "connections", {
                        { "type", "object" },
                        { "properties", {
                            { "junction_size_choice", { { "type", "integer" }, { "description", "Junction dot size: 0=none, 1=smallest, 2=small, 3=default, 4=large, 5=largest" } } },
                            { "hop_over_size_choice", { { "type", "integer" }, { "description", "Hop-over size: 0=none, 1=smallest, etc." } } },
                            { "connection_grid_mils", { { "type", "integer" }, { "description", "Connection grid size (mils)" } } }
                        }}
                    }},
                    { "intersheet_refs", {
                        { "type", "object" },
                        { "properties", {
                            { "show", { { "type", "boolean" }, { "description", "Show inter-sheet references" } } },
                            { "list_own_page", { { "type", "boolean" }, { "description", "Show own page reference" } } },
                            { "format_short", { { "type", "boolean" }, { "description", "Use abbreviated format (1..3) vs standard (1,2,3)" } } },
                            { "prefix", { { "type", "string" } } },
                            { "suffix", { { "type", "string" } } }
                        }}
                    }},
                    { "dashed_lines", {
                        { "type", "object" },
                        { "properties", {
                            { "dash_ratio", { { "type", "number" }, { "description", "Dash length as ratio of line width" } } },
                            { "gap_ratio", { { "type", "number" }, { "description", "Gap length as ratio of line width" } } }
                        }}
                    }},
                    { "opo", {
                        { "type", "object" },
                        { "description", "Operating-point overlay settings" },
                        { "properties", {
                            { "voltage_precision", { { "type", "integer" }, { "description", "Voltage significant digits" } } },
                            { "voltage_range", { { "type", "string" }, { "description", "Voltage range: 'Auto', 'V', 'mV', etc." } } },
                            { "current_precision", { { "type", "integer" }, { "description", "Current significant digits" } } },
                            { "current_range", { { "type", "string" }, { "description", "Current range: 'Auto', 'A', 'mA', etc." } } }
                        }}
                    }}
                }}
            }},
            { "erc", {
                { "type", "object" },
                { "description", "ERC (Electrical Rules Check) violation severity settings. Maps rule codes to severity levels." },
                { "properties", {
                    { "rule_severities", {
                        { "type", "object" },
                        { "description",
                            "Map of ERC rule codes to severity ('error', 'warning', or 'ignore'). "
                            "Use sch_setup action='get' to see all available rule codes and their current severities." },
                        { "additionalProperties", {
                            { "type", "string" },
                            { "enum", json::array( { "error", "warning", "ignore" } ) }
                        }}
                    }}
                }}
            }},
            { "field_name_templates", {
                { "type", "array" },
                { "description", "Field name templates - custom fields for symbols (for set action). Replaces all existing templates." },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "name", {
                            { "type", "string" },
                            { "description", "Field name" }
                        }},
                        { "visible", {
                            { "type", "boolean" },
                            { "description", "Default visibility when field is added to symbol" }
                        }},
                        { "url", {
                            { "type", "boolean" },
                            { "description", "Field contains URL (shows browse button)" }
                        }}
                    }},
                    { "required", json::array( { "name" } ) }
                }}
            }},
            { "annotation", {
                { "type", "object" },
                { "description", "Annotation settings from Schematic Setup (for set action)" },
                { "properties", {
                    { "units", {
                        { "type", "object" },
                        { "description", "Symbol unit notation settings" },
                        { "properties", {
                            { "symbol_unit_notation", {
                                { "type", "string" },
                                { "enum", json::array( { "A", ".A", "-A", "_A", ".1", "-1", "_1" } ) },
                                { "description", "Symbol unit notation style: A (no separator), .A, -A, _A (letters), .1, -1, _1 (numbers)" }
                            }}
                        }}
                    }},
                    { "order", {
                        { "type", "object" },
                        { "description", "Annotation sort order" },
                        { "properties", {
                            { "sort_order", {
                                { "type", "string" },
                                { "enum", json::array( { "x", "y" } ) },
                                { "description", "Sort symbols by position: 'x' (left-to-right) or 'y' (top-to-bottom)" }
                            }}
                        }}
                    }},
                    { "numbering", {
                        { "type", "object" },
                        { "description", "Annotation numbering settings" },
                        { "properties", {
                            { "method", {
                                { "type", "string" },
                                { "enum", json::array( { "first_free", "sheet_x_100", "sheet_x_1000" } ) },
                                { "description", "Numbering method: first_free (sequential), sheet_x_100, or sheet_x_1000" }
                            }},
                            { "start_number", {
                                { "type", "integer" },
                                { "description", "Starting number for annotation" }
                            }},
                            { "allow_reference_reuse", {
                                { "type", "boolean" },
                                { "description", "Allow reusing reference designators from deleted symbols" }
                            }}
                        }}
                    }}
                }}
            }},
            { "pin_conflict_map", {
                { "type", "object" },
                { "description",
                    "Pin conflict map (ERC pin-to-pin error matrix). Defines what happens when pins of "
                    "different electrical types are connected. The matrix is symmetric - setting (A,B) also affects (B,A)." },
                { "properties", {
                    { "reset_to_defaults", {
                        { "type", "boolean" },
                        { "description", "Reset the entire pin conflict map to default values before applying any entries" }
                    }},
                    { "entries", {
                        { "type", "array" },
                        { "description",
                            "List of pin conflict entries to set. Each entry specifies a pair of pin types and the conflict level. "
                            "Pin types: input, output, bidirectional, tri_state, passive, free, unspecified, power_in, power_out, "
                            "open_collector, open_emitter, no_connect. "
                            "Error types: ok (green, no error), warning (yellow triangle), error (red circle)." },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "first_pin_type", {
                                    { "type", "string" },
                                    { "enum", json::array( { "input", "output", "bidirectional", "tri_state", "passive", "free", "unspecified", "power_in", "power_out", "open_collector", "open_emitter", "no_connect" } ) },
                                    { "description", "First pin type in the connection" }
                                }},
                                { "second_pin_type", {
                                    { "type", "string" },
                                    { "enum", json::array( { "input", "output", "bidirectional", "tri_state", "passive", "free", "unspecified", "power_in", "power_out", "open_collector", "open_emitter", "no_connect" } ) },
                                    { "description", "Second pin type in the connection" }
                                }},
                                { "error_type", {
                                    { "type", "string" },
                                    { "enum", json::array( { "ok", "warning", "error" } ) },
                                    { "description", "Conflict level: ok (no error), warning (yellow), error (red)" }
                                }}
                            }},
                            { "required", json::array( { "first_pin_type", "second_pin_type", "error_type" } ) }
                        }}
                    }}
                }}
            }},
            { "net_classes", {
                { "type", "object" },
                { "description",
                    "Net classes define visual properties for groups of nets (wire thickness, bus thickness, color, line style). "
                    "Use 'create' to add new net classes, 'update' to modify existing ones (including 'Default'), 'delete' to remove." },
                { "properties", {
                    { "create", {
                        { "type", "array" },
                        { "description", "Net classes to create" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "name", { { "type", "string" }, { "description", "Unique net class name" } } },
                                { "wire_width_mils", { { "type", "integer" }, { "description", "Wire thickness in mils (default: 6)" } } },
                                { "bus_width_mils", { { "type", "integer" }, { "description", "Bus thickness in mils (default: 12)" } } },
                                { "color", { { "type", "string" }, { "description", "Color in hex format (#RRGGBB) or empty for default" } } },
                                { "line_style", {
                                    { "type", "string" },
                                    { "enum", json::array( { "solid", "dash", "dot", "dash_dot", "dash_dot_dot" } ) },
                                    { "description", "Line style for wires/buses" }
                                }},
                                { "description", { { "type", "string" }, { "description", "Optional description" } } },
                                { "priority", { { "type", "integer" }, { "description", "Priority for multi-netclass resolution (lower = higher priority)" } } }
                            }},
                            { "required", json::array( { "name" } ) }
                        }}
                    }},
                    { "update", {
                        { "type", "array" },
                        { "description", "Net classes to update (including 'Default')" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "name", { { "type", "string" }, { "description", "Net class name (must exist)" } } },
                                { "wire_width_mils", { { "type", "integer" } } },
                                { "bus_width_mils", { { "type", "integer" } } },
                                { "color", { { "type", "string" } } },
                                { "line_style", {
                                    { "type", "string" },
                                    { "enum", json::array( { "solid", "dash", "dot", "dash_dot", "dash_dot_dot" } ) }
                                }},
                                { "description", { { "type", "string" } } },
                                { "priority", { { "type", "integer" } } }
                            }},
                            { "required", json::array( { "name" } ) }
                        }}
                    }},
                    { "delete", {
                        { "type", "array" },
                        { "description", "Net class names to delete (cannot delete 'Default')" },
                        { "items", { { "type", "string" } } }
                    }}
                }}
            }},
            { "net_class_assignments", {
                { "type", "object" },
                { "description",
                    "Net class assignments map net name patterns to net classes using wildcard matching "
                    "(e.g., 'VCC*' matches 'VCC', 'VCC_3V3', etc.)." },
                { "properties", {
                    { "replace_all", {
                        { "type", "array" },
                        { "description", "Replace ALL assignments with this list" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "pattern", { { "type", "string" }, { "description", "Wildcard pattern (e.g., 'VCC*', '*_CLK')" } } },
                                { "netclass", { { "type", "string" }, { "description", "Net class name to assign" } } }
                            }},
                            { "required", json::array( { "pattern", "netclass" } ) }
                        }}
                    }},
                    { "add", {
                        { "type", "array" },
                        { "description", "Assignments to add" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "pattern", { { "type", "string" } } },
                                { "netclass", { { "type", "string" } } }
                            }},
                            { "required", json::array( { "pattern", "netclass" } ) }
                        }}
                    }},
                    { "remove", {
                        { "type", "array" },
                        { "description", "Patterns to remove (exact match)" },
                        { "items", { { "type", "string" } } }
                    }}
                }}
            }},
            { "bus_aliases", {
                { "type", "object" },
                { "description",
                    "Bus aliases define named groups of signals that can be used together as a bus. "
                    "For example, 'DATA_BUS' could contain signals D0, D1, D2, ..., D7." },
                { "properties", {
                    { "replace_all", {
                        { "type", "array" },
                        { "description", "Replace ALL bus aliases with this list" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "name", { { "type", "string" }, { "description", "Bus alias name" } } },
                                { "members", {
                                    { "type", "array" },
                                    { "description", "List of member signal names" },
                                    { "items", { { "type", "string" } } }
                                }}
                            }},
                            { "required", json::array( { "name", "members" } ) }
                        }}
                    }},
                    { "create", {
                        { "type", "array" },
                        { "description", "Bus aliases to create" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "name", { { "type", "string" }, { "description", "Bus alias name (must be unique)" } } },
                                { "members", {
                                    { "type", "array" },
                                    { "description", "List of member signal names" },
                                    { "items", { { "type", "string" } } }
                                }}
                            }},
                            { "required", json::array( { "name", "members" } ) }
                        }}
                    }},
                    { "update", {
                        { "type", "array" },
                        { "description", "Bus aliases to update (replaces members)" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "name", { { "type", "string" }, { "description", "Bus alias name (must exist)" } } },
                                { "members", {
                                    { "type", "array" },
                                    { "description", "New list of member signal names" },
                                    { "items", { { "type", "string" } } }
                                }}
                            }},
                            { "required", json::array( { "name", "members" } ) }
                        }}
                    }},
                    { "delete", {
                        { "type", "array" },
                        { "description", "Bus alias names to delete" },
                        { "items", { { "type", "string" } } }
                    }}
                }}
            }},
            { "text_variables", {
                { "type", "object" },
                { "description",
                    "Text variables for substitution in schematic text fields. Variables are referenced "
                    "as ${VARIABLE_NAME} in text. Project-level setting shared with PCB." },
                { "properties", {
                    { "replace_all", {
                        { "type", "object" },
                        { "description", "Replace ALL text variables with this map (clears existing)" },
                        { "additionalProperties", { { "type", "string" } } }
                    }},
                    { "set", {
                        { "type", "object" },
                        { "description", "Set/merge text variables (adds new, updates existing)" },
                        { "additionalProperties", { { "type", "string" } } }
                    }},
                    { "delete", {
                        { "type", "array" },
                        { "description", "Variable names to delete" },
                        { "items", { { "type", "string" } } }
                    }}
                }}
            }}
        }},
        { "required", json::array( { "action" } ) }
    };
    schSetup.group = ToolGroup::SCHEMATIC;
    schSetup.defer_loading = true;
    tools.push_back( schSetup );
}


static void AddPcbTools( std::vector<LLM_TOOL>& tools )
{
    // pcb_get_summary - Get high-level overview of PCB
    LLM_TOOL pcbGetSummary;
    pcbGetSummary.name = "pcb_get_summary";
    pcbGetSummary.description =
        "Get a high-level overview of the open PCB. Returns footprints (ref, lib_id, position, layer), "
        "track/via/zone counts, net names, and enabled layers. "
        "For detailed data use pcb_inspect, pcb_get_pads, or pcb_get_footprint. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {} },
        { "required", json::array() }
    };
    pcbGetSummary.read_only = true;
    pcbGetSummary.group = ToolGroup::PCB;
    tools.push_back( pcbGetSummary );

    // pcb_inspect - Inspect specific section of PCB
    LLM_TOOL pcbReadSection;
    pcbReadSection.name = "pcb_inspect";
    pcbReadSection.description =
        "Inspect a specific section of the open PCB in detail. "
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
    pcbReadSection.group = ToolGroup::PCB;
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
    pcbRunDrc.read_only = true;
    pcbRunDrc.group = ToolGroup::PCB;
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
    pcbSetOutline.group = ToolGroup::PCB;
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
    pcbSyncSchematic.group = ToolGroup::PCB;
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
    pcbPlace.group = ToolGroup::PCB;
    tools.push_back( pcbPlace );

    // pcb_place_companions - Place companion footprints near an IC
    LLM_TOOL pcbPlaceCompanions;
    pcbPlaceCompanions.name = "pcb_place_companions";
    pcbPlaceCompanions.description =
        "Place companion footprints (bypass caps, pull-up/down resistors, filter caps) near their "
        "associated IC on the PCB. Uses net connectivity to identify which companion pads connect "
        "to which IC pads, then finds optimal positions minimizing ratsnest distance.\n\n"
        "Use AFTER importing the netlist (Update PCB from Schematic) so footprints and nets exist. "
        "Place the IC first with pcb_place, then use this tool for its support components.\n\n"
        "Best practices:\n"
        "- Bypass/decoupling caps should be as close as possible to IC power pins\n"
        "- Pull-up resistors near the IC pin they connect to\n"
        "- Place larger/critical components first, then companions\n\n"
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbPlaceCompanions.input_schema = {
        { "type", "object" },
        { "properties", {
            { "ic_ref", {
                { "type", "string" },
                { "description", "Reference designator of the IC to place companions near (e.g., 'U1')" }
            }},
            { "companions", {
                { "type", "array" },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "ref", {
                            { "type", "string" },
                            { "description", "Reference designator of the companion footprint (e.g., 'C1', 'R3')" }
                        }},
                        { "angle", {
                            { "type", "number" },
                            { "description", "Optional rotation angle in degrees (0, 90, 180, 270)" }
                        }}
                    }},
                    { "required", json::array( { "ref" } ) }
                }},
                { "description", "Array of companion footprints to place near the IC" }
            }}
        }},
        { "required", json::array( { "ic_ref", "companions" } ) }
    };
    pcbPlaceCompanions.group = ToolGroup::PCB;
    tools.push_back( pcbPlaceCompanions );

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
                        { "via_type", {
                            { "type", "string" },
                            { "enum", json::array({ "through", "blind", "buried", "blind_buried", "micro" }) },
                            { "description", "Via type: 'through' (default), 'blind', 'buried', 'micro'" }
                        }},
                        { "start_layer", {
                            { "type", "string" },
                            { "description", "Start layer for blind/buried vias (e.g., 'F.Cu', 'In1.Cu')" }
                        }},
                        { "end_layer", {
                            { "type", "string" },
                            { "description", "End layer for blind/buried vias (e.g., 'In2.Cu', 'B.Cu')" }
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
                        { "filled", { { "type", "boolean" } } },
                        { "clearance", {
                            { "type", "number" },
                            { "description", "Zone clearance in mm" }
                        }},
                        { "min_thickness", {
                            { "type", "number" },
                            { "description", "Zone minimum fill thickness in mm" }
                        }},
                        { "locked", {
                            { "type", "boolean" },
                            { "description", "Lock element to prevent accidental modification" }
                        }}
                    }},
                    { "required", json::array( { "element_type" } ) }
                }},
                { "description", "Array of elements to add. Processed in order." }
            }}
        }},
        { "required", json::array( { "elements" } ) }
    };
    pcbAdd.group = ToolGroup::PCB;
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
                        }},
                        { "diameter", {
                            { "type", "number" },
                            { "description", "Via pad diameter in mm" }
                        }},
                        { "drill_diameter", {
                            { "type", "number" },
                            { "description", "Via drill hole diameter in mm" }
                        }},
                        { "via_type", {
                            { "type", "string" },
                            { "description", "Via type: through, blind, buried, micro" }
                        }},
                        { "start_layer", {
                            { "type", "string" },
                            { "description", "Start layer for blind/buried vias" }
                        }},
                        { "end_layer", {
                            { "type", "string" },
                            { "description", "End layer for blind/buried vias" }
                        }},
                        { "start", {
                            { "type", "array" },
                            { "description", "Track start point [x, y] in mm" }
                        }},
                        { "end", {
                            { "type", "array" },
                            { "description", "Track end point [x, y] in mm" }
                        }},
                        { "priority", {
                            { "type", "integer" },
                            { "description", "Zone fill priority" }
                        }},
                        { "clearance", {
                            { "type", "number" },
                            { "description", "Zone clearance in mm" }
                        }},
                        { "min_thickness", {
                            { "type", "number" },
                            { "description", "Zone minimum fill thickness in mm" }
                        }},
                        { "top_left", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Rectangle top-left corner [x, y] in mm" }
                        }},
                        { "bottom_right", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Rectangle bottom-right corner [x, y] in mm" }
                        }},
                        { "center", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Circle center [x, y] in mm" }
                        }},
                        { "radius", {
                            { "type", "number" },
                            { "description", "Circle radius in mm" }
                        }},
                        { "mid", {
                            { "type", "array" },
                            { "items", { { "type", "number" } } },
                            { "description", "Arc midpoint [x, y] in mm" }
                        }},
                        { "no_copper", {
                            { "type", "boolean" },
                            { "description", "Keepout: prohibit copper pour" }
                        }},
                        { "no_vias", {
                            { "type", "boolean" },
                            { "description", "Keepout: prohibit vias" }
                        }},
                        { "no_tracks", {
                            { "type", "boolean" },
                            { "description", "Keepout: prohibit tracks" }
                        }}
                    }},
                    { "required", json::array( { "target" } ) }
                }},
                { "description", "Array of updates. Each must have 'target' plus properties to change." }
            }}
        }},
        { "required", json::array( { "updates" } ) }
    };
    pcbUpdate.group = ToolGroup::PCB;
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
    pcbDelete.group = ToolGroup::PCB;
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
    pcbGetPads.group = ToolGroup::PCB;
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
    pcbGetFootprint.group = ToolGroup::PCB;
    tools.push_back( pcbGetFootprint );

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
    pcbGetNets.group = ToolGroup::PCB;
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
    pcbExport.group = ToolGroup::PCB;
    tools.push_back( pcbExport );

    // pcb_autoroute - Run Freerouting autorouter
    LLM_TOOL pcbAutoroute;
    pcbAutoroute.name = "pcb_autoroute";
    pcbAutoroute.description =
        "Run the Freerouting autorouter on unrouted connections. "
        "Exports board to Specctra DSN format, runs headless Freerouting, then imports "
        "the routed session (SES) file back into the PCB. "
        "Returns statistics: {routed, total, failed, tracks_added, vias_added}. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbAutoroute.input_schema = {
        { "type", "object" },
        { "properties", {
            { "max_passes", {
                { "type", "integer" },
                { "minimum", 1 },
                { "maximum", 999 },
                { "description", "Maximum number of routing passes (default: 50). "
                                "More passes may find better routes but take longer." }
            }},
            { "timeout", {
                { "type", "integer" },
                { "minimum", 30 },
                { "maximum", 1800 },
                { "description", "Timeout in seconds (default: 300). "
                                "Complex boards may need longer timeouts." }
            }}
        }},
        { "required", json::array() }
    };
    pcbAutoroute.group = ToolGroup::PCB;
    tools.push_back( pcbAutoroute );

    // generate_net_classes - AI-powered net class generation
    LLM_TOOL generateNetClasses;
    generateNetClasses.name = "generate_net_classes";
    generateNetClasses.description =
        "Generate net class definitions using AI analysis of the PCB design. "
        "Analyzes net names, components, and datasheet-extracted electrical specs to create "
        "appropriate net classes (power, signal, differential pairs, etc.) with calculated "
        "trace widths, clearances, and diff pair dimensions. "
        "By default, applies the generated classes to the board. Set apply=false to preview. "
        "REQUIRES: PCB editor must be open with a netlist loaded.";
    generateNetClasses.input_schema = {
        { "type", "object" },
        { "properties", {
            { "apply", {
                { "type", "boolean" },
                { "description", "If true (default), apply generated net classes to the board. "
                                 "If false, return the generated classes without applying." }
            }}
        }},
        { "required", json::array() }
    };
    tools.push_back( generateNetClasses );

    // pcb_setup - Read/write board settings
    LLM_TOOL pcbSetup;
    pcbSetup.name = "pcb_setup";
    pcbSetup.description =
        "Read or modify PCB board settings (Board Setup dialog). "
        "action='get' returns all settings. action='set' updates only provided fields. "
        "Use 'get' first to discover available settings and current values. "
        "All dimensions are in nanometers (nm) unless otherwise noted. "
        "Net classes are project-level (shared with schematic). Set REPLACES all classes (except Default). "
        "custom_rules set REPLACES the entire file — get current rules first if appending. "
        "REQUIRES: PCB editor must be open with a document loaded.";
    pcbSetup.input_schema = {
        { "type", "object" },
        { "properties", {
            { "action", {
                { "type", "string" },
                { "enum", json::array( { "get", "set" } ) },
                { "description", "Action: 'get' to read all settings, 'set' to modify provided fields" }
            }},
            { "board_editor_layers", {
                { "type", "object" },
                { "description", "Board editor layers: manage copper layer count, layer names, and types" },
                { "properties", {
                    { "copper_layer_count", {
                        { "type", "integer" },
                        { "description", "Number of copper layers (must be even, 2-32). WARNING: reducing layers deletes content on removed layers" }
                    }},
                    { "layers", {
                        { "type", "array" },
                        { "description", "Layer-specific settings to update" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "layer", {
                                    { "type", "string" },
                                    { "description", "Layer name (e.g., 'BL_F_Cu', 'BL_In1_Cu', 'BL_B_SilkS')" }
                                }},
                                { "user_name", {
                                    { "type", "string" },
                                    { "description", "Custom layer name (empty to reset to default)" }
                                }},
                                { "type", {
                                    { "type", "string" },
                                    { "enum", json::array( { "signal", "power", "mixed", "jumper" } ) },
                                    { "description", "Layer type (for copper layers only)" }
                                }}
                            }},
                            { "required", json::array( { "layer" } ) }
                        }}
                    }}
                }}
            }},
            { "physical_stackup", {
                { "type", "object" },
                { "description", "Physical board stackup with dielectric properties, impedance control, and layer materials. "
                               "Use to configure PCB fabrication parameters." },
                { "properties", {
                    { "impedance_controlled", {
                        { "type", "boolean" },
                        { "description", "Enable impedance control for the board" }
                    }},
                    { "finish_type", {
                        { "type", "string" },
                        { "description", "Board finish type (e.g., 'HASL', 'ENIG', 'OSP', 'Immersion gold')" }
                    }},
                    { "has_edge_plating", {
                        { "type", "boolean" },
                        { "description", "Board has edge plating" }
                    }},
                    { "layers", {
                        { "type", "array" },
                        { "description", "Stackup layer properties to update" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "layer", {
                                    { "type", "string" },
                                    { "description", "Layer name (e.g., 'BL_F_Cu', 'BL_F_Mask'). Omit for dielectric layers." }
                                }},
                                { "type", {
                                    { "type", "string" },
                                    { "enum", json::array( { "BSLT_COPPER", "BSLT_DIELECTRIC", "BSLT_SILKSCREEN", "BSLT_SOLDERMASK", "BSLT_SOLDERPASTE" } ) },
                                    { "description", "Layer type in stackup" }
                                }},
                                { "thickness_nm", {
                                    { "type", "integer" },
                                    { "description", "Layer thickness in nanometers (e.g., 35000 for 35µm copper)" }
                                }},
                                { "material", {
                                    { "type", "string" },
                                    { "description", "Material name (e.g., 'FR4', 'Copper', 'Not specified')" }
                                }},
                                { "color", {
                                    { "type", "object" },
                                    { "description", "Layer color (for silkscreen/soldermask)" },
                                    { "properties", {
                                        { "r", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 255 } } },
                                        { "g", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 255 } } },
                                        { "b", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 255 } } },
                                        { "a", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 255 } } }
                                    }}
                                }},
                                { "dielectric", {
                                    { "type", "array" },
                                    { "description", "Dielectric sub-layer properties (for BSLT_DIELECTRIC layers)" },
                                    { "items", {
                                        { "type", "object" },
                                        { "properties", {
                                            { "epsilon_r", {
                                                { "type", "number" },
                                                { "description", "Relative permittivity (dielectric constant). Typical: FR4=4.5, Rogers 4350B=3.66" }
                                            }},
                                            { "loss_tangent", {
                                                { "type", "number" },
                                                { "description", "Loss tangent (dissipation factor). Typical: FR4=0.02, Rogers 4350B=0.004" }
                                            }},
                                            { "material", {
                                                { "type", "string" },
                                                { "description", "Dielectric material name (e.g., 'FR4', 'Prepreg', 'Core')" }
                                            }},
                                            { "thickness_nm", {
                                                { "type", "integer" },
                                                { "description", "Sub-layer thickness in nanometers" }
                                            }}
                                        }}
                                    }}
                                }}
                            }}
                        }}
                    }}
                }}
            }},
            { "board_finish", {
                { "type", "object" },
                { "description", "Board finish options: copper finish, edge plating, and edge connectors" },
                { "properties", {
                    { "copper_finish", {
                        { "type", "string" },
                        { "description", "Copper finish type (e.g., 'None', 'HASL', 'ENIG', 'OSP', 'Hard gold', 'Immersion tin')" }
                    }},
                    { "has_plated_edge", {
                        { "type", "boolean" },
                        { "description", "Whether board edges are plated" }
                    }},
                    { "edge_connector", {
                        { "type", "string" },
                        { "enum", json::array( { "none", "in_use", "bevelled" } ) },
                        { "description", "Edge card connector constraints: 'none', 'in_use' (present), or 'bevelled' (present and bevelled)" }
                    }}
                }}
            }},
            { "solder_mask_paste", {
                { "type", "object" },
                { "description", "Solder mask and paste settings. Controls mask expansion, via tenting, and paste margins." },
                { "properties", {
                    { "solder_mask_expansion_nm", {
                        { "type", "integer" },
                        { "description", "Solder mask expansion (clearance around pads) in nanometers. Typical: 51000 (0.051mm)" }
                    }},
                    { "solder_mask_min_width_nm", {
                        { "type", "integer" },
                        { "description", "Minimum solder mask web width in nanometers" }
                    }},
                    { "solder_mask_to_copper_clearance_nm", {
                        { "type", "integer" },
                        { "description", "Solder mask to copper clearance in nanometers" }
                    }},
                    { "allow_bridged_apertures", {
                        { "type", "boolean" },
                        { "description", "Allow bridged solder mask apertures between pads within footprints" }
                    }},
                    { "tent_vias_front", {
                        { "type", "boolean" },
                        { "description", "Tent (cover with solder mask) vias on front side" }
                    }},
                    { "tent_vias_back", {
                        { "type", "boolean" },
                        { "description", "Tent (cover with solder mask) vias on back side" }
                    }},
                    { "solder_paste_clearance_nm", {
                        { "type", "integer" },
                        { "description", "Solder paste clearance/margin in nanometers (positive = larger, negative = smaller than pad)" }
                    }},
                    { "solder_paste_ratio", {
                        { "type", "number" },
                        { "description", "Solder paste ratio (percentage of pad size, e.g., -0.1 = 10% smaller)" }
                    }}
                }}
            }},
            { "zone_hatch_offsets", {
                { "type", "array" },
                { "description", "Zone hatched fill offsets per copper layer. Offsets the hatch pattern for multi-layer boards." },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "layer", {
                            { "type", "string" },
                            { "description", "Layer name (e.g., 'BL_F_Cu', 'BL_In1_Cu', 'BL_B_Cu')" }
                        }},
                        { "offset_x_nm", {
                            { "type", "integer" },
                            { "description", "X offset in nanometers" }
                        }},
                        { "offset_y_nm", {
                            { "type", "integer" },
                            { "description", "Y offset in nanometers" }
                        }}
                    }},
                    { "required", json::array( { "layer" } ) }
                }}
            }},
            { "text_and_graphics", {
                { "type", "object" },
                { "description", "Default text and graphics properties per layer class. Controls line thickness and text settings for new objects." },
                { "properties", {
                    { "silkscreen", {
                        { "type", "object" },
                        { "description", "Default properties for silkscreen layers (F.SilkS, B.SilkS)" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }},
                    { "copper", {
                        { "type", "object" },
                        { "description", "Default properties for copper layers" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }},
                    { "edges", {
                        { "type", "object" },
                        { "description", "Default properties for board edge layer (Edge.Cuts)" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }},
                    { "courtyard", {
                        { "type", "object" },
                        { "description", "Default properties for courtyard layers (F.CrtYd, B.CrtYd)" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }},
                    { "fabrication", {
                        { "type", "object" },
                        { "description", "Default properties for fabrication layers (F.Fab, B.Fab)" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }},
                    { "other", {
                        { "type", "object" },
                        { "description", "Default properties for other layers (User layers, comments, etc.)" },
                        { "properties", {
                            { "line_thickness_nm", { { "type", "integer" }, { "description", "Default line thickness in nanometers" } } },
                            { "text_width_nm", { { "type", "integer" }, { "description", "Default text width in nanometers" } } },
                            { "text_height_nm", { { "type", "integer" }, { "description", "Default text height in nanometers" } } },
                            { "text_thickness_nm", { { "type", "integer" }, { "description", "Default text stroke thickness in nanometers" } } },
                            { "italic", { { "type", "boolean" }, { "description", "Default text italic style" } } },
                            { "keep_upright", { { "type", "boolean" }, { "description", "Keep text upright (don't mirror when rotated)" } } }
                        }}
                    }}
                }}
            }},
            { "dimension_defaults", {
                { "type", "object" },
                { "description", "Default settings for new dimension objects (units, precision, text position, arrows)" },
                { "properties", {
                    { "units_mode", {
                        { "type", "string" },
                        { "enum", json::array( { "automatic", "inches", "mils", "millimeters" } ) },
                        { "description", "Units mode: 'automatic' (use editor units), 'inches', 'mils', or 'millimeters'" }
                    }},
                    { "units_format", {
                        { "type", "string" },
                        { "enum", json::array( { "no_suffix", "bare_suffix", "paren_suffix" } ) },
                        { "description", "Units display: 'no_suffix' (1234.0), 'bare_suffix' (1234.0 mm), 'paren_suffix' (1234.0 (mm))" }
                    }},
                    { "precision", {
                        { "description", "Number of decimal places (0-5) or variable format ('V.VV', 'V.VVV', 'V.VVVV', 'V.VVVVV')" }
                    }},
                    { "suppress_zeroes", {
                        { "type", "boolean" },
                        { "description", "Suppress trailing zeroes in dimension text" }
                    }},
                    { "text_position", {
                        { "type", "string" },
                        { "enum", json::array( { "outside", "inline", "manual" } ) },
                        { "description", "Text position: 'outside' (above line), 'inline' (on line), 'manual' (user-placed)" }
                    }},
                    { "keep_text_aligned", {
                        { "type", "boolean" },
                        { "description", "Keep text aligned with dimension line" }
                    }},
                    { "arrow_length_nm", {
                        { "type", "integer" },
                        { "description", "Arrow length in nanometers" }
                    }},
                    { "extension_offset_nm", {
                        { "type", "integer" },
                        { "description", "Extension line offset from measured point in nanometers" }
                    }}
                }}
            }},
            { "zone_defaults", {
                { "type", "object" },
                { "description", "Default settings for new zones (clearance, pad connection, thermal relief, islands)" },
                { "properties", {
                    { "name", {
                        { "type", "string" },
                        { "description", "Default zone name (usually empty)" }
                    }},
                    { "locked", {
                        { "type", "boolean" },
                        { "description", "Default locked state for new zones" }
                    }},
                    { "priority", {
                        { "type", "integer" },
                        { "minimum", 0 },
                        { "description", "Default zone priority (0 = lowest)" }
                    }},
                    { "corner_smoothing", {
                        { "type", "string" },
                        { "enum", json::array( { "none", "chamfer", "fillet" } ) },
                        { "description", "Corner smoothing type" }
                    }},
                    { "corner_radius_nm", {
                        { "type", "integer" },
                        { "description", "Corner radius for chamfer/fillet in nanometers" }
                    }},
                    { "clearance_nm", {
                        { "type", "integer" },
                        { "description", "Zone clearance in nanometers" }
                    }},
                    { "min_thickness_nm", {
                        { "type", "integer" },
                        { "description", "Minimum fill width in nanometers" }
                    }},
                    { "pad_connection", {
                        { "type", "string" },
                        { "enum", json::array( { "inherited", "none", "thermal", "solid", "tht_thermal" } ) },
                        { "description", "Pad connection: 'thermal' (spokes), 'solid' (full), 'none', 'tht_thermal' (thermal for THT, solid for SMD)" }
                    }},
                    { "thermal_gap_nm", {
                        { "type", "integer" },
                        { "description", "Thermal relief gap in nanometers" }
                    }},
                    { "thermal_spoke_width_nm", {
                        { "type", "integer" },
                        { "description", "Thermal relief spoke width in nanometers" }
                    }},
                    { "island_removal", {
                        { "type", "string" },
                        { "enum", json::array( { "always", "never", "area" } ) },
                        { "description", "Remove isolated islands: 'always', 'never', or 'area' (below min area)" }
                    }},
                    { "min_island_area_nm2", {
                        { "type", "integer" },
                        { "description", "Minimum island area in nm² (for island_removal='area')" }
                    }}
                }}
            }},
            { "predefined_sizes", {
                { "type", "object" },
                { "description", "Pre-defined track widths, via sizes, and differential pair dimensions for quick selection" },
                { "properties", {
                    { "tracks", {
                        { "type", "array" },
                        { "description", "List of predefined track widths" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "width_nm", { { "type", "integer" }, { "description", "Track width in nanometers" } } }
                            }},
                            { "required", json::array( { "width_nm" } ) }
                        }}
                    }},
                    { "vias", {
                        { "type", "array" },
                        { "description", "List of predefined via sizes (diameter and drill)" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "diameter_nm", { { "type", "integer" }, { "description", "Via pad diameter in nanometers" } } },
                                { "drill_nm", { { "type", "integer" }, { "description", "Via drill diameter in nanometers" } } }
                            }},
                            { "required", json::array( { "diameter_nm", "drill_nm" } ) }
                        }}
                    }},
                    { "diff_pairs", {
                        { "type", "array" },
                        { "description", "List of predefined differential pair dimensions" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "width_nm", { { "type", "integer" }, { "description", "Track width in nanometers" } } },
                                { "gap_nm", { { "type", "integer" }, { "description", "Gap between tracks in nanometers" } } },
                                { "via_gap_nm", { { "type", "integer" }, { "description", "Gap between vias in nanometers" } } }
                            }},
                            { "required", json::array( { "width_nm", "gap_nm" } ) }
                        }}
                    }}
                }}
            }},
            { "teardrops", {
                { "type", "object" },
                { "description", "Teardrop settings for pad/via connections. Teardrops strengthen track-to-pad junctions." },
                { "properties", {
                    { "target_vias", { { "type", "boolean" }, { "description", "Create teardrops for vias" } } },
                    { "target_pth_pads", { { "type", "boolean" }, { "description", "Create teardrops for PTH (through-hole) pads" } } },
                    { "target_smd_pads", { { "type", "boolean" }, { "description", "Create teardrops for SMD pads" } } },
                    { "target_track_to_track", { { "type", "boolean" }, { "description", "Create teardrops at track-to-track width transitions" } } },
                    { "round_shapes_only", { { "type", "boolean" }, { "description", "Only apply to round-shaped pads/vias" } } },
                    { "round_shapes", {
                        { "type", "object" },
                        { "description", "Teardrop parameters for round pads/vias" },
                        { "properties", {
                            { "best_length_ratio", { { "type", "number" }, { "description", "Best length as ratio of pad size (0.5 = 50%)" } } },
                            { "max_length_nm", { { "type", "integer" }, { "description", "Maximum length in nanometers" } } },
                            { "best_width_ratio", { { "type", "number" }, { "description", "Best width as ratio of pad size (1.0 = 100%)" } } },
                            { "max_width_nm", { { "type", "integer" }, { "description", "Maximum width in nanometers" } } },
                            { "curved_edges", { { "type", "boolean" }, { "description", "Use curved edges for teardrop shape" } } },
                            { "allow_two_segments", { { "type", "boolean" }, { "description", "Allow teardrop to span two track segments" } } },
                            { "prefer_zone_connection", { { "type", "boolean" }, { "description", "Prefer zone connection over teardrop for pads in zones" } } },
                            { "track_width_limit_ratio", { { "type", "number" }, { "description", "Max track width to pad size ratio for teardrop (0.9 = 90%)" } } }
                        }}
                    }},
                    { "rect_shapes", {
                        { "type", "object" },
                        { "description", "Teardrop parameters for rectangular pads" },
                        { "properties", {
                            { "best_length_ratio", { { "type", "number" }, { "description", "Best length as ratio of pad width (0.5 = 50%)" } } },
                            { "max_length_nm", { { "type", "integer" }, { "description", "Maximum length in nanometers" } } },
                            { "best_width_ratio", { { "type", "number" }, { "description", "Best width as ratio of pad width (1.0 = 100%)" } } },
                            { "max_width_nm", { { "type", "integer" }, { "description", "Maximum width in nanometers" } } },
                            { "curved_edges", { { "type", "boolean" }, { "description", "Use curved edges for teardrop shape" } } },
                            { "allow_two_segments", { { "type", "boolean" }, { "description", "Allow teardrop to span two track segments" } } },
                            { "prefer_zone_connection", { { "type", "boolean" }, { "description", "Prefer zone connection over teardrop for pads in zones" } } },
                            { "track_width_limit_ratio", { { "type", "number" }, { "description", "Max track width to pad width ratio for teardrop (0.9 = 90%)" } } }
                        }}
                    }},
                    { "track_to_track", {
                        { "type", "object" },
                        { "description", "Teardrop parameters for track-to-track width transitions" },
                        { "properties", {
                            { "best_length_ratio", { { "type", "number" }, { "description", "Best length as ratio of wider track (0.5 = 50%)" } } },
                            { "max_length_nm", { { "type", "integer" }, { "description", "Maximum length in nanometers" } } },
                            { "best_width_ratio", { { "type", "number" }, { "description", "Best width as ratio of wider track (1.0 = 100%)" } } },
                            { "max_width_nm", { { "type", "integer" }, { "description", "Maximum width in nanometers" } } },
                            { "curved_edges", { { "type", "boolean" }, { "description", "Use curved edges for teardrop shape" } } },
                            { "allow_two_segments", { { "type", "boolean" }, { "description", "Allow teardrop to span two track segments" } } },
                            { "track_width_limit_ratio", { { "type", "number" }, { "description", "Max track width ratio for teardrop (0.9 = 90%)" } } }
                        }}
                    }}
                }}
            }},
            { "length_tuning_patterns", {
                { "type", "object" },
                { "description", "Length-tuning meander pattern settings for single tracks, diff pairs, and skew tuning" },
                { "properties", {
                    { "single_track", {
                        { "type", "object" },
                        { "description", "Settings for single track length tuning" },
                        { "properties", {
                            { "min_amplitude_nm", { { "type", "integer" }, { "description", "Minimum amplitude (A) in nanometers" } } },
                            { "max_amplitude_nm", { { "type", "integer" }, { "description", "Maximum amplitude (A) in nanometers" } } },
                            { "spacing_nm", { { "type", "integer" }, { "description", "Spacing (s) between meanders in nanometers" } } },
                            { "corner_style", {
                                { "type", "string" },
                                { "enum", json::array( { "round", "chamfer" } ) },
                                { "description", "Corner style: 'round' (fillet) or 'chamfer' (45 degree)" }
                            }},
                            { "corner_radius_percent", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 100 }, { "description", "Corner radius as percentage (0-100)" } } },
                            { "single_sided", { { "type", "boolean" }, { "description", "Place meanders on one side only" } } }
                        }}
                    }},
                    { "diff_pair", {
                        { "type", "object" },
                        { "description", "Settings for differential pair length tuning" },
                        { "properties", {
                            { "min_amplitude_nm", { { "type", "integer" }, { "description", "Minimum amplitude (A) in nanometers" } } },
                            { "max_amplitude_nm", { { "type", "integer" }, { "description", "Maximum amplitude (A) in nanometers" } } },
                            { "spacing_nm", { { "type", "integer" }, { "description", "Spacing (s) between meanders in nanometers" } } },
                            { "corner_style", {
                                { "type", "string" },
                                { "enum", json::array( { "round", "chamfer" } ) },
                                { "description", "Corner style: 'round' (fillet) or 'chamfer' (45 degree)" }
                            }},
                            { "corner_radius_percent", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 100 }, { "description", "Corner radius as percentage (0-100)" } } },
                            { "single_sided", { { "type", "boolean" }, { "description", "Place meanders on one side only" } } }
                        }}
                    }},
                    { "diff_pair_skew", {
                        { "type", "object" },
                        { "description", "Settings for differential pair skew tuning" },
                        { "properties", {
                            { "min_amplitude_nm", { { "type", "integer" }, { "description", "Minimum amplitude (A) in nanometers" } } },
                            { "max_amplitude_nm", { { "type", "integer" }, { "description", "Maximum amplitude (A) in nanometers" } } },
                            { "spacing_nm", { { "type", "integer" }, { "description", "Spacing (s) between meanders in nanometers" } } },
                            { "corner_style", {
                                { "type", "string" },
                                { "enum", json::array( { "round", "chamfer" } ) },
                                { "description", "Corner style: 'round' (fillet) or 'chamfer' (45 degree)" }
                            }},
                            { "corner_radius_percent", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 100 }, { "description", "Corner radius as percentage (0-100)" } } },
                            { "single_sided", { { "type", "boolean" }, { "description", "Place meanders on one side only" } } }
                        }}
                    }}
                }}
            }},
            { "tuning_profiles", {
                { "type", "array" },
                { "description", "Impedance/delay tuning profiles for controlled impedance routing and time domain analysis. Set replaces all profiles." },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "name", { { "type", "string" }, { "description", "Profile name (unique identifier)" } } },
                        { "type", {
                            { "type", "string" },
                            { "enum", json::array( { "single", "differential" } ) },
                            { "description", "Profile type: 'single' (single-ended) or 'differential' (differential pair)" }
                        }},
                        { "target_impedance_ohms", { { "type", "number" }, { "description", "Target impedance in ohms" } } },
                        { "enable_time_domain_tuning", { { "type", "boolean" }, { "description", "Enable time domain tuning (uses propagation delays)" } } },
                        { "via_propagation_delay_ps", { { "type", "integer" }, { "description", "Default via propagation delay in picoseconds" } } },
                        { "track_entries", {
                            { "type", "array" },
                            { "description", "Per-layer track propagation settings" },
                            { "items", {
                                { "type", "object" },
                                { "properties", {
                                    { "signal_layer", { { "type", "integer" }, { "description", "Signal layer ID" } } },
                                    { "top_reference_layer", { { "type", "integer" }, { "description", "Top reference plane layer ID" } } },
                                    { "bottom_reference_layer", { { "type", "integer" }, { "description", "Bottom reference plane layer ID" } } },
                                    { "width_nm", { { "type", "integer" }, { "description", "Track width in nanometers" } } },
                                    { "diff_pair_gap_nm", { { "type", "integer" }, { "description", "Differential pair gap in nanometers (0 for single-ended)" } } },
                                    { "delay_ps_per_mm", { { "type", "integer" }, { "description", "Propagation delay in picoseconds per millimeter" } } },
                                    { "enable_time_domain", { { "type", "boolean" }, { "description", "Enable time domain tuning for this entry" } } }
                                }}
                            }}
                        }},
                        { "via_overrides", {
                            { "type", "array" },
                            { "description", "Via propagation delay overrides for specific layer transitions" },
                            { "items", {
                                { "type", "object" },
                                { "properties", {
                                    { "signal_layer_from", { { "type", "integer" }, { "description", "Signal start layer ID" } } },
                                    { "signal_layer_to", { { "type", "integer" }, { "description", "Signal end layer ID" } } },
                                    { "via_layer_from", { { "type", "integer" }, { "description", "Via start layer ID" } } },
                                    { "via_layer_to", { { "type", "integer" }, { "description", "Via end layer ID" } } },
                                    { "delay_ps", { { "type", "integer" }, { "description", "Via propagation delay in picoseconds" } } }
                                }}
                            }}
                        }}
                    }}
                }}
            }},
            { "component_classes", {
                { "type", "object" },
                { "description", "Component class assignment settings. Set replaces all assignments." },
                { "properties", {
                    { "enable_sheet_component_classes", { { "type", "boolean" }, { "description", "Assign component class per hierarchical sheet" } } },
                    { "assignments", {
                        { "type", "array" },
                        { "description", "Custom component class assignment rules" },
                        { "items", {
                            { "type", "object" },
                            { "properties", {
                                { "component_class", { { "type", "string" }, { "description", "Name of the component class to assign" } } },
                                { "operator", {
                                    { "type", "string" },
                                    { "enum", json::array( { "all", "any" } ) },
                                    { "description", "How conditions are combined: 'all' (AND) or 'any' (OR)" }
                                }},
                                { "conditions", {
                                    { "type", "array" },
                                    { "description", "Conditions that must match for assignment" },
                                    { "items", {
                                        { "type", "object" },
                                        { "properties", {
                                            { "type", {
                                                { "type", "string" },
                                                { "enum", json::array( { "reference", "footprint", "side", "rotation", "footprint_field", "custom", "sheet_name" } ) },
                                                { "description", "Condition type: reference (designator pattern), footprint (name pattern), side (Top/Bottom), rotation (angle), footprint_field (field match), custom (expression), sheet_name (hierarchical sheet)" }
                                            }},
                                            { "primary_data", { { "type", "string" }, { "description", "Primary match data (pattern, field name, side, etc.)" } } },
                                            { "secondary_data", { { "type", "string" }, { "description", "Secondary data (e.g., field value for footprint_field type)" } } }
                                        }},
                                        { "required", json::array( { "type", "primary_data" } ) }
                                    }}
                                }}
                            }},
                            { "required", json::array( { "component_class" } ) }
                        }}
                    }}
                }}
            }},
            { "design_rules", {
                { "type", "object" },
                { "description", "Board design rules/constraints (all values in nanometers)" },
                { "properties", {
                    { "min_clearance_nm", { { "type", "integer" }, { "description", "Min copper-to-copper clearance" } } },
                    { "min_track_width_nm", { { "type", "integer" }, { "description", "Min track width" } } },
                    { "min_connection_nm", { { "type", "integer" }, { "description", "Min zone connection width" } } },
                    { "min_via_diameter_nm", { { "type", "integer" }, { "description", "Min via pad diameter" } } },
                    { "min_via_drill_nm", { { "type", "integer" }, { "description", "Min via drill diameter" } } },
                    { "min_via_annular_width_nm", { { "type", "integer" }, { "description", "Min via annular ring" } } },
                    { "copper_edge_clearance_nm", { { "type", "integer" }, { "description", "Min copper-to-edge clearance" } } },
                    { "min_hole_to_hole_nm", { { "type", "integer" }, { "description", "Min hole-to-hole spacing" } } },
                    { "hole_to_copper_clearance_nm", { { "type", "integer" }, { "description", "Min hole-to-copper clearance" } } },
                    { "solder_mask_expansion_nm", { { "type", "integer" }, { "description", "Solder mask expansion" } } },
                    { "solder_mask_min_width_nm", { { "type", "integer" }, { "description", "Min solder mask width" } } },
                    { "solder_paste_margin_nm", { { "type", "integer" }, { "description", "Solder paste margin" } } },
                    { "solder_paste_margin_ratio", { { "type", "number" }, { "description", "Solder paste margin ratio (0-1)" } } },
                    { "min_silk_clearance_nm", { { "type", "integer" }, { "description", "Min silkscreen clearance" } } },
                    { "min_silk_text_height_nm", { { "type", "integer" }, { "description", "Min silkscreen text height" } } },
                    { "min_silk_text_thickness_nm", { { "type", "integer" }, { "description", "Min silkscreen text thickness" } } },
                    { "min_resolved_spokes", { { "type", "integer" }, { "description", "Min thermal relief spokes" } } },
                    { "max_error_nm", { { "type", "integer" }, { "description", "Arc/circle approximation: max allowed deviation (nm)" } } },
                    { "allow_external_fillets", { { "type", "boolean" }, { "description", "Zone fill: allow fillets/chamfers outside zone outline" } } },
                    { "include_stackup_in_length", { { "type", "boolean" }, { "description", "Length tuning: include stackup height in track length" } } }
                }}
            }},
            { "grid", {
                { "type", "object" },
                { "description", "Grid settings" },
                { "properties", {
                    { "size_x_nm", { { "type", "integer" }, { "description", "Grid X spacing in nanometers" } } },
                    { "size_y_nm", { { "type", "integer" }, { "description", "Grid Y spacing in nanometers" } } },
                    { "visible", { { "type", "boolean" }, { "description", "Show/hide grid" } } },
                    { "style", {
                        { "type", "string" },
                        { "enum", json::array( { "dots", "lines", "small_cross" } ) },
                        { "description", "Grid display style" }
                    }}
                }}
            }},
            { "drc_severities", {
                { "type", "object" },
                { "description", "Map of DRC check names to severity: 'error', 'warning', or 'ignore'. "
                                "Use pcb_setup action='get' to see all available check names and current severities." },
                { "additionalProperties", { { "type", "string" }, { "enum", json::array( { "error", "warning", "ignore" } ) } } }
            }},
            { "custom_rules", {
                { "type", "object" },
                { "description", "Custom DRC rules (contents of .kicad_dru file). Get returns the rules text. "
                                "SET COMPLETELY REPLACES the file - to add rules to existing, first GET current rules, "
                                "append your new rule(s), then SET the combined text. To clear all rules, SET empty or minimal '(version 1)'." },
                { "properties", {
                    { "rules_text", {
                        { "type", "string" },
                        { "description", "Full text content of the custom DRC rules file. Must start with '(version 1)'. "
                                        "See KiCad DRC rules syntax for format." }
                    }}
                }}
            }},
            { "net_classes", {
                { "type", "array" },
                { "description", "Net class definitions (project-level, shared with schematic). SET REPLACES all net classes - classes not in the input are REMOVED (except Default which is always kept). Include all desired classes in each set operation." },
                { "items", {
                    { "type", "object" },
                    { "properties", {
                        { "name", { { "type", "string" }, { "description", "Net class name ('Default' for the default class)" } } },
                        { "priority", { { "type", "integer" }, { "description", "Priority (higher = more specific)" } } },
                        { "clearance_nm", { { "type", "integer" }, { "description", "Clearance in nm" } } },
                        { "track_width_nm", { { "type", "integer" }, { "description", "Track width in nm" } } },
                        { "via_diameter_nm", { { "type", "integer" }, { "description", "Via pad diameter in nm" } } },
                        { "via_drill_nm", { { "type", "integer" }, { "description", "Via drill diameter in nm" } } },
                        { "microvia_diameter_nm", { { "type", "integer" }, { "description", "Microvia pad diameter in nm" } } },
                        { "microvia_drill_nm", { { "type", "integer" }, { "description", "Microvia drill diameter in nm" } } },
                        { "diff_pair_width_nm", { { "type", "integer" }, { "description", "Differential pair track width in nm" } } },
                        { "diff_pair_gap_nm", { { "type", "integer" }, { "description", "Differential pair gap in nm" } } },
                        { "diff_pair_via_gap_nm", { { "type", "integer" }, { "description", "Differential pair via gap in nm" } } },
                        { "tuning_profile", { { "type", "string" }, { "description", "Name of tuning profile to use for this net class" } } },
                        { "pcb_color", {
                            { "type", "object" },
                            { "description", "PCB color for nets in this class (transparent = use layer default)" },
                            { "properties", {
                                { "r", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "Red (0-1)" } } },
                                { "g", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "Green (0-1)" } } },
                                { "b", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "Blue (0-1)" } } },
                                { "a", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "Alpha (0-1, 0=transparent)" } } }
                            }}
                        }}
                    }},
                    { "required", json::array( { "name" } ) }
                }}
            }},
            { "text_variables", {
                { "type", "object" },
                { "description", "User-defined text variables (project-level). Map of variable names to text substitution values. "
                                "Use in text items as ${VARIABLE_NAME}. Set uses merge mode - existing variables updated, new ones added." },
                { "additionalProperties", { { "type", "string" } } }
            }},
            { "title_block", {
                { "type", "object" },
                { "description", "Title block information" },
                { "properties", {
                    { "title", { { "type", "string" } } },
                    { "date", { { "type", "string" } } },
                    { "revision", { { "type", "string" } } },
                    { "company", { { "type", "string" } } },
                    { "comment1", { { "type", "string" } } },
                    { "comment2", { { "type", "string" } } },
                    { "comment3", { { "type", "string" } } },
                    { "comment4", { { "type", "string" } } }
                }}
            }},
            { "origins", {
                { "type", "object" },
                { "description", "Board origins (in mm)" },
                { "properties", {
                    { "grid_mm", {
                        { "type", "array" },
                        { "items", { { "type", "number" } } },
                        { "description", "Grid origin as [x, y] in mm" }
                    }},
                    { "drill_mm", {
                        { "type", "array" },
                        { "items", { { "type", "number" } } },
                        { "description", "Drill/place origin as [x, y] in mm" }
                    }}
                }}
            }}
        }},
        { "required", json::array( { "action" } ) }
    };
    pcbSetup.group = ToolGroup::PCB;
    pcbSetup.defer_loading = true;
    tools.push_back( pcbSetup );
}


// Part search tool schemas (jlc_*, mouser_*, digikey_*, cse_*) are fetched
// dynamically from pcbparts.dev/mcp by COMPONENT_SEARCH_HANDLER at startup.


std::vector<LLM_TOOL> GetToolDefinitions()
{
    wxLogInfo( "ToolSchemas::GetToolDefinitions called" );

    std::vector<LLM_TOOL> tools;
    AddGeneralTools( tools );
    AddSchematicTools( tools );
    AddPcbTools( tools );
    return tools;
}

} // namespace ToolSchemas
