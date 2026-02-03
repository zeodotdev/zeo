/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "agent_tools.h"
#include "agent_llm_client.h"
#include "../tools/tool_registry.h"
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

    // Tool 1: run_shell - Execute Python code in KiCad IPC shell
    LLM_TOOL runShell;
    runShell.name = "run_shell";
    runShell.description = "Execute Python code in the KiCad IPC shell. Use mode 'sch' for schematic operations "
                           "(sch object pre-imported) or 'pcb' for board operations (board object pre-imported). "
                           "kipy, Vector2 are also pre-imported.";
    runShell.input_schema = {
        { "type", "object" },
        { "properties", {
            { "mode", {
                { "type", "string" },
                { "enum", json::array( { "sch", "pcb" } ) },
                { "description", "sch for schematic operations, pcb for board operations" }
            }},
            { "code", {
                { "type", "string" },
                { "description", "Python code to execute. Variables available: kipy, sch/board (depending on mode), Vector2" }
            }}
        }},
        { "required", json::array( { "mode", "code" } ) }
    };
    tools.push_back( runShell );

    // Tool 2: run_terminal - Execute bash/shell commands
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

    // Tool 3: open_editor - Open schematic or PCB editor with user approval
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

    // ===== Direct File Tools (sch_*, pcb_*) =====

    // Tool 4: sch_get_summary - Get high-level overview of schematic
    LLM_TOOL schGetSummary;
    schGetSummary.name = "sch_get_summary";
    schGetSummary.description = "Get a high-level overview of a .kicad_sch schematic file. "
                                "Returns JSON with symbols, wires, junctions, labels, and counts. "
                                "Use this to understand the schematic before making modifications.";
    schGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_sch file" }
            }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    tools.push_back( schGetSummary );

    // Tool 5: sch_read_section - Read specific section of schematic
    LLM_TOOL schReadSection;
    schReadSection.name = "sch_read_section";
    schReadSection.description = "Read a specific section of a .kicad_sch file. "
                                 "Returns raw S-expression text for the requested section. "
                                 "Sections: header, symbols, wires, junctions, labels, lib_symbols, text, sheets, all";
    schReadSection.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_sch file" }
            }},
            { "section", {
                { "type", "string" },
                { "enum", json::array( { "header", "symbols", "wires", "junctions", "labels", "lib_symbols", "text", "sheets", "all" } ) },
                { "description", "Section to read" }
            }},
            { "filter", {
                { "type", "string" },
                { "description", "Optional filter by reference (e.g., 'R*' for all resistors) or UUID" }
            }}
        }},
        { "required", json::array( { "file_path", "section" } ) }
    };
    tools.push_back( schReadSection );

    // Tool 6: sch_modify - Add, update, or delete elements
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

    // Tool 7: sch_validate - Validate schematic file
    LLM_TOOL schValidate;
    schValidate.name = "sch_validate";
    schValidate.description = "Validate a .kicad_sch file without modifying it. "
                              "Checks syntax (S-expression parsing), structure (required fields, UUID uniqueness), "
                              "and returns warnings about potential issues.";
    schValidate.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_sch file" }
            }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    tools.push_back( schValidate );

    // Tool 8: sch_write - Write complete schematic content
    LLM_TOOL schWrite;
    schWrite.name = "sch_write";
    schWrite.description = "Write complete schematic content to a .kicad_sch file. "
                           "Use for creating new schematics or major rewrites. "
                           "Content must be valid KiCad S-expression format. "
                           "Creates a .bak backup if file exists (unless backup=false).";
    schWrite.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to write the .kicad_sch file" }
            }},
            { "content", {
                { "type", "string" },
                { "description", "Complete schematic content as S-expression" }
            }},
            { "backup", {
                { "type", "boolean" },
                { "description", "Create .bak backup if file exists (default: true)" }
            }}
        }},
        { "required", json::array( { "file_path", "content" } ) }
    };
    tools.push_back( schWrite );

    // Tool: sch_run_erc - Run ERC on open schematic
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

    // PCB tools (stubs - not yet implemented)
    // Tool 9: pcb_get_summary
    LLM_TOOL pcbGetSummary;
    pcbGetSummary.name = "pcb_get_summary";
    pcbGetSummary.description = "[NOT YET IMPLEMENTED] Get overview of a .kicad_pcb file. "
                                "Use run_shell with mode 'pcb' for PCB operations until implemented.";
    pcbGetSummary.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", {
                { "type", "string" },
                { "description", "Absolute path to the .kicad_pcb file" }
            }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    tools.push_back( pcbGetSummary );

    // Tool 10: pcb_read_section
    LLM_TOOL pcbReadSection;
    pcbReadSection.name = "pcb_read_section";
    pcbReadSection.description = "[NOT YET IMPLEMENTED] Read section of a .kicad_pcb file. "
                                  "Use run_shell with mode 'pcb' for PCB operations until implemented.";
    pcbReadSection.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", { { "type", "string" } }},
            { "section", { { "type", "string" } }}
        }},
        { "required", json::array( { "file_path", "section" } ) }
    };
    tools.push_back( pcbReadSection );

    // Tool 11: pcb_modify
    LLM_TOOL pcbModify;
    pcbModify.name = "pcb_modify";
    pcbModify.description = "[NOT YET IMPLEMENTED] Modify a .kicad_pcb file. "
                            "Use run_shell with mode 'pcb' for PCB operations until implemented.";
    pcbModify.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", { { "type", "string" } }},
            { "operation", { { "type", "string" } }},
            { "element_type", { { "type", "string" } }}
        }},
        { "required", json::array( { "file_path", "operation", "element_type" } ) }
    };
    tools.push_back( pcbModify );

    // Tool 12: pcb_validate
    LLM_TOOL pcbValidate;
    pcbValidate.name = "pcb_validate";
    pcbValidate.description = "[NOT YET IMPLEMENTED] Validate a .kicad_pcb file. "
                              "Use run_shell with mode 'pcb' for PCB operations until implemented.";
    pcbValidate.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", { { "type", "string" } }}
        }},
        { "required", json::array( { "file_path" } ) }
    };
    tools.push_back( pcbValidate );

    // Tool 13: pcb_write
    LLM_TOOL pcbWrite;
    pcbWrite.name = "pcb_write";
    pcbWrite.description = "[NOT YET IMPLEMENTED] Write complete PCB content to a .kicad_pcb file. "
                           "Use run_shell with mode 'pcb' for PCB operations until implemented.";
    pcbWrite.input_schema = {
        { "type", "object" },
        { "properties", {
            { "file_path", { { "type", "string" } }},
            { "content", { { "type", "string" } }}
        }},
        { "required", json::array( { "file_path", "content" } ) }
    };
    tools.push_back( pcbWrite );

    return tools;
}


std::string BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput )
{
    wxLogInfo( "AgentTools::BuildToolPayload called for tool: %s", aToolName.c_str() );
    // Build the command string for the terminal based on tool name
    if( aToolName == "run_shell" )
    {
        std::string code = aInput.value( "code", "" );
        std::string mode = aInput.value( "mode", "sch" );

        if( code.empty() )
            return "Error: run_shell requires 'code' parameter";

        // All modes go through terminal with same format
        return "run_shell " + mode + " " + code;
    }
    else if( aToolName == "run_terminal" )
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

    // Check if this is a direct file tool (sch_*, pcb_*)
    if( TOOL_REGISTRY::Instance().HasHandler( aToolName ) )
    {
        // Execute directly without going through terminal frame
        return TOOL_REGISTRY::Instance().Execute( aToolName, aInput );
    }

    if( aToolName == "run_shell" )
    {
        std::string mode = aInput.value( "mode", "" );
        std::string code = aInput.value( "code", "" );

        if( mode.empty() || code.empty() )
            return "Error: run_shell requires 'mode' and 'code' parameters";

        // Build the command string for the terminal frame
        std::string command = "run_shell " + mode + " " + code;
        return aSendRequestFn( FRAME_TERMINAL, command );
    }
    else if( aToolName == "run_terminal" )
    {
        std::string command = aInput.value( "command", "" );

        if( command.empty() )
            return "Error: run_terminal requires 'command' parameter";

        return aSendRequestFn( FRAME_TERMINAL, "run_terminal " + command );
    }
    else if( aToolName == "sch_run_erc" )
    {
        std::string format = aInput.value( "output_format", "summary" );
        bool includeWarnings = aInput.value( "include_warnings", true );

        std::string code;

        if( format == "summary" )
        {
            code = "# Run ERC and print summary\n"
                   "result = sch.erc.analyze()\n"
                   "print(result['summary'])\n";
        }
        else if( format == "detailed" )
        {
            code = "import json\n"
                   "result = sch.erc.analyze()\n";
            if( !includeWarnings )
                code += "result['violations'] = [v for v in result['violations'] if v['severity'] == 'error']\n";
            code += "output = {'error_count': result['error_count'], "
                    "'warning_count': result['warning_count'], "
                    "'violations': result['violations']}\n"
                    "print(json.dumps(output, indent=2))\n";
        }
        else if( format == "by_type" )
        {
            code = "import json\n"
                   "result = sch.erc.analyze()\n"
                   "output = {'error_count': result['error_count'], "
                   "'warning_count': result['warning_count'], "
                   "'by_type': {code: len(items) for code, items in result['by_type'].items()}}\n"
                   "print(json.dumps(output, indent=2))\n";
        }

        std::string command = "run_shell sch " + code;
        return aSendRequestFn( FRAME_TERMINAL, command );
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
    if( aToolName == "run_shell" || aToolName == "run_python" )
    {
        std::string mode = aInput.value( "mode", "python" );
        std::string code = aInput.value( "code", "" );

        // Try to extract a description from the code
        // Look for a comment at the start like "# Description: ..." or just "# ..."
        std::string desc;
        size_t firstNewline = code.find( '\n' );
        std::string firstLine = ( firstNewline != std::string::npos ) ? code.substr( 0, firstNewline ) : code;

        if( firstLine.length() > 2 && firstLine[0] == '#' )
        {
            // Extract the comment text
            desc = firstLine.substr( 1 );
            // Trim leading whitespace
            size_t start = desc.find_first_not_of( " \t" );
            if( start != std::string::npos )
                desc = desc.substr( start );
            // Remove "Description:" prefix if present
            if( desc.find( "Description:" ) == 0 )
                desc = desc.substr( 12 );
            // Trim again
            start = desc.find_first_not_of( " \t" );
            if( start != std::string::npos )
                desc = desc.substr( start );
        }

        // If we found a description, use it
        if( !desc.empty() && desc.length() < 100 )
        {
            return wxString::FromUTF8( desc );
        }

        // Otherwise, try to infer from the code content
        if( code.find( "add_symbol" ) != std::string::npos )
            return "Adding symbol to schematic";
        else if( code.find( "delete" ) != std::string::npos || code.find( "remove" ) != std::string::npos )
            return "Removing component";
        else if( code.find( "move" ) != std::string::npos || code.find( "position" ) != std::string::npos )
            return "Moving component";
        else if( code.find( "connect" ) != std::string::npos || code.find( "wire" ) != std::string::npos )
            return "Adding connections";
        else if( code.find( "get_symbols" ) != std::string::npos || code.find( "find" ) != std::string::npos )
            return "Searching schematic";
        else if( code.find( "property" ) != std::string::npos || code.find( "value" ) != std::string::npos )
            return "Modifying component properties";

        // Fallback to mode-based description
        if( mode == "kipy_schematic" )
            return "Modifying schematic";
        else if( mode == "kipy_pcb" )
            return "Modifying PCB layout";
        else
            return "Executing Python script";
    }
    else if( aToolName == "run_terminal" )
    {
        std::string cmd = aInput.value( "command", "" );
        if( cmd.length() > 50 )
            cmd = cmd.substr( 0, 47 ) + "...";
        return wxString::Format( "Running: %s", cmd );
    }
    else if( aToolName == "sch_run_erc" )
    {
        std::string format = aInput.value( "output_format", "summary" );
        if( format == "detailed" )
            return "Running detailed ERC analysis";
        else if( format == "by_type" )
            return "Running ERC (grouped by type)";
        else
            return "Running ERC check";
    }
    else
    {
        return wxString::Format( "Executing %s", aToolName );
    }
}

} // namespace AgentTools
