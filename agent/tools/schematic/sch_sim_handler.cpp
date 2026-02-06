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

#include "sch_sim_handler.h"
#include <sstream>


bool SCH_SIM_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_run_simulation";
}


std::string SCH_SIM_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // This tool requires IPC execution - should not be called directly
    return "Error: sch_run_simulation requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_SIM_HANDLER::GetDescription( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string command = aInput.value( "command", "" );
    bool save = aInput.value( "save_to_schematic", false );

    // Extract the sim type from command (e.g. ".tran 1u 10m" -> ".tran")
    std::string simType;
    if( !command.empty() )
    {
        size_t spacePos = command.find( ' ' );
        simType = ( spacePos != std::string::npos ) ? command.substr( 0, spacePos ) : command;
    }

    std::string desc = "Running simulation";

    if( !simType.empty() )
        desc += ": " + simType;

    if( save )
        desc += " (saving to schematic)";

    return desc;
}


bool SCH_SIM_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_run_simulation";
}


std::string SCH_SIM_HANDLER::GetIPCCommand( const std::string& aToolName,
                                             const nlohmann::json& aInput ) const
{
    std::string code = GenerateRunSimCode( aInput );
    return "run_shell sch " + code;
}


std::string SCH_SIM_HANDLER::EscapePythonString( const std::string& aStr ) const
{
    std::string result;
    result.reserve( aStr.size() + 10 );

    for( char c : aStr )
    {
        switch( c )
        {
        case '\\': result += "\\\\"; break;
        case '\'': result += "\\'"; break;
        case '\"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }

    return result;
}


std::string SCH_SIM_HANDLER::GenerateRunSimCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string command = aInput.value( "command", "" );
    bool saveToSchematic = aInput.value( "save_to_schematic", false );

    if( command.empty() )
    {
        code << "print('Error: command parameter is required')\n";
        return code.str();
    }

    code << "import json\n";
    code << "\n";

    // Optionally persist the command to schematic settings
    if( saveToSchematic )
    {
        code << "# Save simulation command to schematic settings\n";
        code << "try:\n";
        code << "    sch.simulation.set_settings(spice_command='"
             << EscapePythonString( command ) << "')\n";
        code << "except Exception as e:\n";
        code << "    print(f'Warning: Could not save settings: {e}')\n";
        code << "\n";
    }

    code << "# Run SPICE simulation\n";
    code << "try:\n";
    code << "    result = sch.simulation.run(command_override='"
         << EscapePythonString( command ) << "')\n";
    code << "    if result['success']:\n";
    code << "        # Summarize traces (min/max/final/count) to keep output manageable\n";
    code << "        trace_summaries = []\n";
    code << "        for trace in result.get('traces', []):\n";
    code << "            vals = trace.get('data_values', [])\n";
    code << "            summary = {'name': trace['name'], 'points': len(vals)}\n";
    code << "            if vals:\n";
    code << "                summary['min'] = min(vals)\n";
    code << "                summary['max'] = max(vals)\n";
    code << "                summary['final'] = vals[-1]\n";
    code << "            trace_summaries.append(summary)\n";
    code << "        output = {\n";
    code << "            'status': 'success',\n";
    code << "            'command': '" << EscapePythonString( command ) << "',\n";
    code << "            'trace_count': len(result.get('traces', [])),\n";
    code << "            'traces': trace_summaries\n";
    code << "        }\n";
    code << "        print(json.dumps(output, indent=2))\n";
    code << "    else:\n";
    code << "        print(json.dumps({\n";
    code << "            'status': 'error',\n";
    code << "            'message': result.get('error_message', 'Simulation failed')\n";
    code << "        }))\n";
    code << "except Exception as e:\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

    return code.str();
}
