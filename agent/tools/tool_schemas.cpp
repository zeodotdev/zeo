#include "tool_schemas.h"
#include "../agent_llm_client.h"
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <fstream>
#include <sstream>

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


/**
 * Load Python-based tool definitions from tool_manifest.json.
 * Replaces the hand-coded AddSchematicTools/AddPcbTools with a single
 * source of truth shared with the MCP server and ToolScriptLoader.
 *
 * Skips tools with empty "app" (check_status, launch_editor, screenshot)
 * since those are defined in AddGeneralTools with C++-specific handling.
 */
static void AddToolsFromManifest( std::vector<LLM_TOOL>& tools )
{
    // FindPythonDir: same logic as PYTHON_TOOL_HANDLER and TOOL_SCRIPT_LOADER
    std::string pythonDir;

    const char* envDir = std::getenv( "AGENT_PYTHON_DIR" );

    if( envDir && envDir[0] )
    {
        pythonDir = envDir;
    }
    else
    {
        wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
        wxFileName dir( exePath.GetPath(), "" );
        dir.RemoveLastDir();
        dir.AppendDir( "SharedSupport" );
        dir.AppendDir( "agent" );
        dir.AppendDir( "python" );
        pythonDir = dir.GetPath().ToStdString();
    }

    std::string manifestPath = pythonDir + "/tool_manifest.json";
    std::ifstream file( manifestPath );

    if( !file.is_open() )
    {
        wxLogWarning( "ToolSchemas: Could not open %s — no Python tools will be registered",
                      manifestPath );
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    json manifest;

    try
    {
        manifest = json::parse( content );
    }
    catch( const json::exception& e )
    {
        wxLogError( "ToolSchemas: Failed to parse tool_manifest.json: %s", e.what() );
        return;
    }

    if( !manifest.is_array() )
    {
        wxLogError( "ToolSchemas: tool_manifest.json is not a JSON array" );
        return;
    }

    int count = 0;

    for( const auto& entry : manifest )
    {
        // nlohmann::json::value() throws type_error if key exists with null value,
        // so use a helper lambda to safely extract strings that may be null.
        auto strVal = []( const json& obj, const char* key, const char* def = "" ) -> std::string
        {
            if( !obj.contains( key ) || obj[key].is_null() )
                return def;
            return obj[key].get<std::string>();
        };

        std::string name = strVal( entry, "name" );
        std::string app = strVal( entry, "app" );
        std::string groupStr = strVal( entry, "group", "GENERAL" );

        // Skip tools without an app — they're handled by AddGeneralTools
        if( app.empty() || name.empty() )
            continue;

        LLM_TOOL tool;
        tool.name = name;
        tool.description = strVal( entry, "description" );
        tool.read_only = entry.value( "read_only", false );
        tool.defer_loading = entry.value( "deferred", false );

        // Parse input_schema
        if( entry.contains( "input_schema" ) )
            tool.input_schema = entry["input_schema"];
        else
            tool.input_schema = { { "type", "object" }, { "properties", {} }, { "required", json::array() } };

        // Map group string to enum
        if( groupStr == "SCHEMATIC" )
            tool.group = ToolGroup::SCHEMATIC;
        else if( groupStr == "PCB" )
            tool.group = ToolGroup::PCB;
        else
            tool.group = ToolGroup::GENERAL;

        tools.push_back( std::move( tool ) );
        count++;
    }

    wxLogInfo( "ToolSchemas: Loaded %d Python tools from manifest", count );
}


// Part search tool schemas (jlc_*, mouser_*, digikey_*, cse_*) are fetched
// dynamically from pcbparts.dev/mcp by COMPONENT_SEARCH_HANDLER at startup.


std::vector<LLM_TOOL> GetToolDefinitions()
{
    wxLogInfo( "ToolSchemas::GetToolDefinitions called" );

    std::vector<LLM_TOOL> tools;
    AddGeneralTools( tools );
    AddToolsFromManifest( tools );
    return tools;
}

} // namespace ToolSchemas
