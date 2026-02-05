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

#ifndef PCB_CRUD_HANDLER_H
#define PCB_CRUD_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for PCB IPC-based operations via kipy.
 * Handles: pcb_get_summary, pcb_read_section, pcb_run_drc, pcb_set_outline,
 *          pcb_sync_schematic, pcb_place, pcb_add, pcb_update, pcb_delete,
 *          pcb_batch_delete, pcb_export
 */
class PCB_CRUD_HANDLER : public TOOL_HANDLER
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
    std::string GenerateGetSummaryCode( const nlohmann::json& aInput ) const;
    std::string GenerateReadSectionCode( const nlohmann::json& aInput ) const;
    std::string GenerateRunDrcCode( const nlohmann::json& aInput ) const;
    std::string GenerateSetOutlineCode( const nlohmann::json& aInput ) const;
    std::string GenerateSyncSchematicCode( const nlohmann::json& aInput ) const;
    std::string GeneratePlaceCode( const nlohmann::json& aInput ) const;
    std::string GenerateAddCode( const nlohmann::json& aInput ) const;
    std::string GenerateUpdateCode( const nlohmann::json& aInput ) const;
    std::string GenerateDeleteCode( const nlohmann::json& aInput ) const;
    std::string GenerateBatchDeleteCode( const nlohmann::json& aInput ) const;
    std::string GenerateExportCode( const nlohmann::json& aInput ) const;

    std::string EscapePythonString( const std::string& aStr ) const;
    std::string MmToNm( double aMm ) const;
};

#endif // PCB_CRUD_HANDLER_H
