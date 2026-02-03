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

#ifndef SCH_ERC_HANDLER_H
#define SCH_ERC_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic ERC operations (sch_run_erc).
 * Requires KiCad's schematic editor to be open with a document loaded.
 * Executes via kipy IPC (run_shell) rather than direct file access.
 */
class SCH_ERC_HANDLER : public TOOL_HANDLER
{
public:
    SCH_ERC_HANDLER() = default;
    ~SCH_ERC_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;
};

#endif // SCH_ERC_HANDLER_H
