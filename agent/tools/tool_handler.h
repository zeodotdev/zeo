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

    /**
     * Check if this tool requires IPC (run_shell) execution rather than direct execution.
     * Tools that need KiCad's live document state (e.g., ERC) return true.
     * @param aToolName The name of the tool to check.
     * @return true if the tool requires IPC execution.
     */
    virtual bool RequiresIPC( const std::string& aToolName ) const { return false; }

    /**
     * Get the IPC command string for tools that require IPC execution.
     * Only called if RequiresIPC() returns true.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return The command string to execute via run_shell (e.g., "run_shell sch <code>").
     */
    virtual std::string GetIPCCommand( const std::string& aToolName,
                                        const nlohmann::json& aInput ) const { return ""; }

    /**
     * Set whether the schematic editor is currently open.
     * When the editor is open, file-based write operations should be blocked
     * to prevent data conflicts between IPC and direct file access.
     * @param aOpen true if the schematic editor is open.
     */
    virtual void SetSchematicEditorOpen( bool aOpen ) { m_schematicEditorOpen = aOpen; }

    /**
     * Check if the schematic editor is currently open.
     * @return true if the schematic editor is open.
     */
    virtual bool IsSchematicEditorOpen() const { return m_schematicEditorOpen; }

    /**
     * Set whether the PCB editor is currently open.
     * When the editor is open, file-based write operations should be blocked
     * to prevent data conflicts between IPC and direct file access.
     * @param aOpen true if the PCB editor is open.
     */
    virtual void SetPcbEditorOpen( bool aOpen ) { m_pcbEditorOpen = aOpen; }

    /**
     * Check if the PCB editor is currently open.
     * @return true if the PCB editor is open.
     */
    virtual bool IsPcbEditorOpen() const { return m_pcbEditorOpen; }

protected:
    bool m_schematicEditorOpen = false;
    bool m_pcbEditorOpen = false;
};

#endif // TOOL_HANDLER_H
