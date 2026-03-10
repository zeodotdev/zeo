#include "tool_schemas.h"
#include "../agent_llm_client.h"
#include "handlers/python_tool_handler.h"
#include <nlohmann/json.hpp>
#include <wx/log.h>
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

    // file_write - Create or overwrite a text file
    LLM_TOOL fileWrite;
    fileWrite.name = "file_write";
    fileWrite.description = "Create a new file or overwrite an existing file with the given content. "
                            "Use this for writing text files like .txt, .md, .py, .json, .csv, etc. "
                            "Cannot write KiCad files (.kicad_sch, .kicad_pcb, etc.) or binary files. "
                            "The file_path must be an absolute path within the project directory. "
                            "Parent directories will be created automatically if they don't exist.";
    fileWrite.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the file to create or overwrite" }
            }},
            { "content", {
                { "type", "string" },
                { "description", "The full content to write to the file" }
            }}
        }},
        { "required", json::array( { "file_path", "content" } ) }
    };
    tools.push_back( fileWrite );

    // file_read - Read contents of a text file
    LLM_TOOL fileRead;
    fileRead.name = "file_read";
    fileRead.description = "Read the contents of a file. Returns the file content with line count information. "
                           "Use offset and limit parameters for large files.";
    fileRead.read_only = true;
    fileRead.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the file to read" }
            }},
            { "offset", {
                { "type", "integer" },
                { "description", "Line number to start reading from (0-based). Default: 0" }
            }},
            { "limit", {
                { "type", "integer" },
                { "description", "Maximum number of lines to read. Default: 2000" }
            }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    tools.push_back( fileRead );

    // Remaining "general" tools (check_status, screenshot, datasheet_query,
    // generate_symbol, generate_footprint, sch_import_symbol, create_project,
    // extract_datasheet) are now defined in tool_manifest.json and loaded
    // by AddToolsFromManifest().
}


/**
 * Load tool definitions from tool_manifest.json — single source of truth
 * shared with the MCP server and ToolScriptLoader.
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
        pythonDir = PYTHON_TOOL_HANDLER::FindPythonDir();
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
        std::string groupStr = strVal( entry, "group", "GENERAL" );

        if( name.empty() )
            continue;

        // Skip tools with no app and no script — these are MCP-only
        std::string app = strVal( entry, "app" );
        std::string script = strVal( entry, "script" );

        if( app.empty() && script.empty() )
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
