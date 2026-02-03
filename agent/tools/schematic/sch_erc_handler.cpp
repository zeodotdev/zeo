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

#include "sch_erc_handler.h"


bool SCH_ERC_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_run_erc";
}


std::string SCH_ERC_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // This tool requires IPC execution - should not be called directly
    return "Error: sch_run_erc requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_ERC_HANDLER::GetDescription( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string format = aInput.value( "output_format", "summary" );

    if( format == "detailed" )
        return "Running detailed ERC analysis";
    else if( format == "by_type" )
        return "Running ERC (grouped by type)";
    else
        return "Running ERC check";
}


bool SCH_ERC_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_run_erc";
}


std::string SCH_ERC_HANDLER::GetIPCCommand( const std::string& aToolName,
                                             const nlohmann::json& aInput ) const
{
    std::string format = aInput.value( "output_format", "summary" );
    bool includeWarnings = aInput.value( "include_warnings", true );

    std::string code;

    if( format == "summary" )
    {
        code = "# Run ERC and print summary\n"
               "result = sch.erc.analyze()\n"
               "print(result['summary'])\n";
    }
    else if( format == "detailed" )
    {
        code = "import json\n"
               "result = sch.erc.analyze()\n";
        if( !includeWarnings )
            code += "result['violations'] = [v for v in result['violations'] if v['severity'] == 'error']\n";
        code += "output = {'error_count': result['error_count'], "
                "'warning_count': result['warning_count'], "
                "'violations': result['violations']}\n"
                "print(json.dumps(output, indent=2))\n";
    }
    else if( format == "by_type" )
    {
        code = "import json\n"
               "result = sch.erc.analyze()\n"
               "output = {'error_count': result['error_count'], "
               "'warning_count': result['warning_count'], "
               "'by_type': {code: len(items) for code, items in result['by_type'].items()}}\n"
               "print(json.dumps(output, indent=2))\n";
    }

    return "run_shell sch " + code;
}
