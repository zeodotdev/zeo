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

#include "pcb_tool_handler.h"

// Static list of tool names this handler will support (stub for now)
static const char* PCB_TOOL_NAMES[] = {
    "pcb_get_summary",
    "pcb_read_section",
    "pcb_modify",
    "pcb_validate",
    "pcb_write"
};


bool PCB_TOOL_HANDLER::CanHandle( const std::string& aToolName ) const
{
    for( const char* name : PCB_TOOL_NAMES )
    {
        if( aToolName == name )
            return true;
    }
    return false;
}


std::string PCB_TOOL_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // All PCB tools return "not implemented" for now
    return "Error: PCB tools are not yet implemented. Tool: " + aToolName +
           "\n\nPCB file operations will be available in a future update. "
           "For now, use the run_shell tool with KiPy board API for PCB operations.";
}


std::string PCB_TOOL_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    if( aToolName == "pcb_get_summary" )
        return "Getting PCB summary (not implemented)";
    else if( aToolName == "pcb_read_section" )
        return "Reading PCB section (not implemented)";
    else if( aToolName == "pcb_modify" )
        return "Modifying PCB (not implemented)";
    else if( aToolName == "pcb_validate" )
        return "Validating PCB (not implemented)";
    else if( aToolName == "pcb_write" )
        return "Writing PCB (not implemented)";

    return "Executing " + aToolName;
}
