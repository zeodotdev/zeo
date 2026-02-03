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

#ifndef AGENT_TOOLS_H
#define AGENT_TOOLS_H

#include <vector>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include <wx/string.h>

struct LLM_TOOL;

namespace AgentTools
{
    /**
     * Get the list of available tools for the KiCad Agent.
     * @return Vector of tool definitions.
     */
    std::vector<LLM_TOOL> GetToolDefinitions();

    /**
     * Build the payload string for sending a tool request to the terminal frame.
     * @param aToolName The name of the tool to execute.
     * @param aInput The tool input parameters as JSON.
     * @return The payload string, or an error string starting with "Error:" if invalid.
     */
    std::string BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput );

    /**
     * Execute a tool synchronously (legacy interface).
     * @param aToolName The name of the tool to execute.
     * @param aInput The tool input parameters as JSON.
     * @param aSendRequestFn Function to send the request to the target frame.
     * @return The result string from tool execution.
     */
    std::string ExecuteToolSync( const std::string& aToolName, const nlohmann::json& aInput,
                                  std::function<std::string( int, const std::string& )> aSendRequestFn );

    /**
     * Generate a human-readable description for a tool call.
     * Extracts meaningful descriptions from tool names and inputs.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return A human-readable description string.
     */
    wxString GetToolDescription( const std::string& aToolName, const nlohmann::json& aInput );
}

#endif // AGENT_TOOLS_H
