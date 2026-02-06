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

#ifndef SCH_CRUD_HANDLER_H
#define SCH_CRUD_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for schematic CRUD and navigation operations via kipy IPC.
 * Handles: sch_add, sch_update, sch_delete, sch_batch_delete, sch_open_sheet
 *
 * These tools work on the LIVE schematic through the kipy Python API,
 * allowing real-time creation, modification, deletion, and navigation.
 */
class SCH_CRUD_HANDLER : public TOOL_HANDLER
{
public:
    bool CanHandle( const std::string& aToolName ) const override;

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;

    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool RequiresIPC( const std::string& aToolName ) const override;

    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    /**
     * Generate Python code for sch_add operation.
     */
    std::string GenerateAddCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_update operation.
     */
    std::string GenerateUpdateCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_delete operation.
     */
    std::string GenerateDeleteCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_batch_delete operation.
     */
    std::string GenerateBatchDeleteCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_open_sheet operation.
     */
    std::string GenerateOpenSheetCode( const nlohmann::json& aInput ) const;

    /**
     * Generate common Python code header for file fallback operations.
     * Includes imports, file loading, and save function.
     */
    std::string GenerateFileFallbackHeader() const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;

    /**
     * Convert mm to nanometers (KiCad internal units).
     */
    std::string MmToNm( double aMm ) const;
};

#endif // SCH_CRUD_HANDLER_H
