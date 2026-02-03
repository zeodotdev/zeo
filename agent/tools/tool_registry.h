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

#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

class TOOL_HANDLER;

/**
 * Registry for direct file tools (sch_*, pcb_*).
 * This factory creates and manages tool handlers for direct KiCad file manipulation.
 */
class TOOL_REGISTRY
{
public:
    /**
     * Get the singleton instance.
     */
    static TOOL_REGISTRY& Instance();

    /**
     * Check if the given tool name is handled by a registered direct tool handler.
     * @param aToolName The name of the tool to check.
     * @return true if a handler exists for this tool.
     */
    bool HasHandler( const std::string& aToolName ) const;

    /**
     * Execute a direct tool.
     * @param aToolName The name of the tool to execute.
     * @param aInput The JSON input parameters for the tool.
     * @return The result string from tool execution, or error string starting with "Error:".
     */
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput );

    /**
     * Generate a human-readable description for a tool call.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return A human-readable description string.
     */
    std::string GetDescription( const std::string& aToolName, const nlohmann::json& aInput ) const;

    /**
     * Set the project path for path validation in all handlers.
     * @param aPath The absolute path to the project directory.
     */
    void SetProjectPath( const std::string& aPath );

    /**
     * Check if a tool requires IPC (run_shell) execution.
     * @param aToolName The name of the tool to check.
     * @return true if the tool requires IPC execution.
     */
    bool RequiresIPC( const std::string& aToolName ) const;

    /**
     * Get the IPC command string for a tool.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return The command string to execute via run_shell.
     */
    std::string GetIPCCommand( const std::string& aToolName, const nlohmann::json& aInput ) const;

private:
    TOOL_REGISTRY();
    ~TOOL_REGISTRY() = default;

    // Delete copy and move constructors
    TOOL_REGISTRY( const TOOL_REGISTRY& ) = delete;
    TOOL_REGISTRY& operator=( const TOOL_REGISTRY& ) = delete;

    std::vector<std::unique_ptr<TOOL_HANDLER>> m_handlers;
};

#endif // TOOL_REGISTRY_H
