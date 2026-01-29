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
#include <nlohmann/json.hpp>
#include <kiway.h>
#include <wx/string.h>

namespace AgentTools
{

std::vector<LLM_TOOL> GetToolDefinitions()
{
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
                             "Use editor_type 'sch' for schematic editor or 'pcb' for PCB editor.";
    openEditor.input_schema = {
        { "type", "object" },
        { "properties", {
            { "editor_type", {
                { "type", "string" },
                { "enum", json::array( { "sch", "pcb" } ) },
                { "description", "Editor to open: 'sch' for schematic, 'pcb' for PCB" }
            }}
        }},
        { "required", json::array( { "editor_type" } ) }
    };
    tools.push_back( openEditor );

    return tools;
}


std::string BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput )
{
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

    return "Error: Unknown tool '" + aToolName + "'";
}


wxString GetToolDescription( const std::string& aToolName, const nlohmann::json& aInput )
{
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
    else
    {
        return wxString::Format( "Executing %s", aToolName );
    }
}

} // namespace AgentTools
