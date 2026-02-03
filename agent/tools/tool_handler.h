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

#ifndef TOOL_HANDLER_H
#define TOOL_HANDLER_H

#include <string>
#include <nlohmann/json.hpp>

/**
 * Base interface for tool handlers.
 * Each tool handler is responsible for executing a specific tool or category of tools.
 */
class TOOL_HANDLER
{
public:
    virtual ~TOOL_HANDLER() = default;

    /**
     * Check if this handler can process the given tool name.
     * @param aToolName The name of the tool to check.
     * @return true if this handler can process the tool.
     */
    virtual bool CanHandle( const std::string& aToolName ) const = 0;

    /**
     * Execute the tool with the given input parameters.
     * @param aToolName The name of the tool to execute.
     * @param aInput The JSON input parameters for the tool.
     * @return The result string from tool execution.
     */
    virtual std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) = 0;

    /**
     * Generate a human-readable description for a tool call.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return A human-readable description string.
     */
    virtual std::string GetDescription( const std::string& aToolName,
                                        const nlohmann::json& aInput ) const = 0;

    /**
     * Set the project path for path validation.
     * Handlers should validate that file operations stay within this directory.
     * @param aPath The absolute path to the project directory.
     */
    virtual void SetProjectPath( const std::string& aPath ) { /* Default no-op */ }
};

#endif // TOOL_HANDLER_H
