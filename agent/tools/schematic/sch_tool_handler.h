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

#ifndef SCH_TOOL_HANDLER_H
#define SCH_TOOL_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic file operations (sch_* tools).
 * Implements direct reading, modification, and writing of .kicad_sch files.
 */
class SCH_TOOL_HANDLER : public TOOL_HANDLER
{
public:
    SCH_TOOL_HANDLER() = default;
    ~SCH_TOOL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    /**
     * sch_get_summary supports IPC for live state queries.
     * Returns true for sch_get_summary to try IPC first.
     */
    bool RequiresIPC( const std::string& aToolName ) const override;

    /**
     * Generate IPC command for sch_get_summary.
     * Queries live schematic state via kipy API.
     */
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

    /**
     * Set the project path for path validation.
     * File write operations will be restricted to this directory.
     */
    void SetProjectPath( const std::string& aPath ) override { m_projectPath = aPath; }

private:
    std::string m_projectPath;  ///< Project directory for path validation
    /**
     * Execute sch_get_summary tool.
     * Returns a JSON summary of the schematic file.
     */
    std::string ExecuteGetSummary( const nlohmann::json& aInput );

    /**
     * Execute sch_read_section tool.
     * Returns raw S-expression text for requested section.
     */
    std::string ExecuteReadSection( const nlohmann::json& aInput );

    /**
     * Execute sch_modify tool.
     * Adds, updates, or deletes elements in the schematic.
     */
    std::string ExecuteModify( const nlohmann::json& aInput );

    /**
     * Execute sch_validate tool.
     * Validates a schematic file and returns results as JSON.
     */
    std::string ExecuteValidate( const nlohmann::json& aInput );

    /**
     * Execute sch_export_spice_netlist tool.
     * Generates a SPICE netlist from the schematic using kicad-cli.
     */
    std::string ExecuteExportSpiceNetlist( const nlohmann::json& aInput );
};

#endif // SCH_TOOL_HANDLER_H
