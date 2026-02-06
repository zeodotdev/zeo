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

#ifndef SCH_UTIL_HANDLER_H
#define SCH_UTIL_HANDLER_H

#include "../tool_handler.h"
#include <nlohmann/json.hpp>
#include <string>

/**
 * Tool handler for schematic utility operations via kipy IPC.
 * Handles: sch_annotate, sch_save, sch_get_nets
 *
 * These tools work on the LIVE schematic through the kipy Python API.
 * Requires KiCad's schematic editor to be open with a document loaded.
 */
class SCH_UTIL_HANDLER : public TOOL_HANDLER
{
public:
    SCH_UTIL_HANDLER() = default;
    ~SCH_UTIL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    /**
     * Generate Python code for sch_annotate operation.
     */
    std::string GenerateAnnotateCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_save operation.
     */
    std::string GenerateSaveCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_get_nets operation.
     */
    std::string GenerateGetNetsCode( const nlohmann::json& aInput ) const;
};

#endif // SCH_UTIL_HANDLER_H
