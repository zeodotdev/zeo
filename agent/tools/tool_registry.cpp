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

#include "tool_registry.h"
#include "tool_handler.h"
#include "schematic/sch_tool_handler.h"
#include "pcb/pcb_tool_handler.h"


TOOL_REGISTRY& TOOL_REGISTRY::Instance()
{
    static TOOL_REGISTRY instance;
    return instance;
}


TOOL_REGISTRY::TOOL_REGISTRY()
{
    // Register schematic tool handler
    m_handlers.push_back( std::make_unique<SCH_TOOL_HANDLER>() );

    // Register PCB tool handler (stubs for now)
    m_handlers.push_back( std::make_unique<PCB_TOOL_HANDLER>() );
}


bool TOOL_REGISTRY::HasHandler( const std::string& aToolName ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return true;
    }
    return false;
}


std::string TOOL_REGISTRY::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    for( auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->Execute( aToolName, aInput );
    }
    return "Error: No handler found for tool '" + aToolName + "'";
}


std::string TOOL_REGISTRY::GetDescription( const std::string& aToolName,
                                            const nlohmann::json& aInput ) const
{
    for( const auto& handler : m_handlers )
    {
        if( handler->CanHandle( aToolName ) )
            return handler->GetDescription( aToolName, aInput );
    }
    return "Unknown tool: " + aToolName;
}
