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
    code << "        # Summarize traces (min/max/final/count)\n";
    code << "        trace_summaries = []\n";
    code << "        for trace in result.get('traces', []):\n";
    code << "            vals = trace.get('data_values', [])\n";
    code << "            summary = {'name': trace['name'], 'points': len(vals)}\n";
    code << "            if vals:\n";
    code << "                summary['min'] = min(vals)\n";
    code << "                summary['max'] = max(vals)\n";
    code << "                summary['final'] = vals[-1]\n";
    code << "            trace_summaries.append(summary)\n";
    code << "        summary_output = {\n";
    code << "            'status': 'success',\n";
    code << "            'command': '" << EscapePythonString( command ) << "',\n";
    code << "            'trace_count': len(result.get('traces', [])),\n";
    code << "            'traces': trace_summaries\n";
    code << "        }\n";
    code << "\n";
    code << "        # Generate plot if traces have array data (skip for .op)\n";
    code << "        traces = result.get('traces', [])\n";
    code << "        has_arrays = any(len(t.get('data_values', [])) > 1 for t in traces)\n";
    code << "        plot_b64 = None\n";
    code << "        display_b64 = None\n";
    code << "        if has_arrays:\n";
    code << "            try:\n";
    code << "                import matplotlib\n";
    code << "                matplotlib.use('Agg')\n";
    code << "                import matplotlib.pyplot as plt\n";
    code << "                import base64\n";
    code << "                from io import BytesIO\n";
    code << "\n";
    code << "                fig, ax = plt.subplots(figsize=(7, 4))\n";
    code << "                x_data = traces[0].get('time_values', [])\n";
    code << "                cmd_lower = '" << EscapePythonString( command ) << "'.lower().strip()\n";
    code << "                if cmd_lower.startswith('.ac'):\n";
    code << "                    ax.set_xlabel('Frequency (Hz)')\n";
    code << "                    ax.set_ylabel('Magnitude')\n";
    code << "                    ax.set_xscale('log')\n";
    code << "                elif cmd_lower.startswith('.dc'):\n";
    code << "                    ax.set_xlabel('Sweep Variable')\n";
    code << "                    ax.set_ylabel('Voltage / Current')\n";
    code << "                elif cmd_lower.startswith('.tran'):\n";
    code << "                    ax.set_xlabel('Time (s)')\n";
    code << "                    ax.set_ylabel('Voltage / Current')\n";
    code << "                else:\n";
    code << "                    ax.set_xlabel('X')\n";
    code << "                    ax.set_ylabel('Y')\n";
    code << "                for trace in traces:\n";
    code << "                    name = trace.get('name', '')\n";
    code << "                    y_data = trace.get('data_values', [])\n";
    code << "                    t_data = trace.get('time_values', x_data)\n";
    code << "                    if len(y_data) > 1 and len(t_data) == len(y_data):\n";
    code << "                        ax.plot(t_data, y_data, label=name, linewidth=1.5)\n";
    code << "                ax.legend(fontsize=8, loc='best')\n";
    code << "                ax.grid(True, alpha=0.3)\n";
    code << "                ax.set_title('" << EscapePythonString( command ) << "', fontsize=10)\n";
    code << "                fig.tight_layout()\n";
    code << "                # High-res for chat UI display\n";
    code << "                buf_display = BytesIO()\n";
    code << "                fig.savefig(buf_display, format='png', bbox_inches='tight', dpi=650)\n";
    code << "                buf_display.seek(0)\n";
    code << "                display_b64 = base64.b64encode(buf_display.read()).decode('ascii')\n";
    code << "                # Low-res for LLM API (fits Claude vision limits)\n";
    code << "                buf_api = BytesIO()\n";
    code << "                fig.savefig(buf_api, format='png', bbox_inches='tight', dpi=200)\n";
    code << "                buf_api.seek(0)\n";
    code << "                plot_b64 = base64.b64encode(buf_api.read()).decode('ascii')\n";
    code << "                plt.close(fig)\n";
    code << "            except ImportError as _ie:\n";
    code << "                summary_output['_plot_error'] = 'ImportError: ' + str(_ie)\n";
    code << "            except Exception as _pe:\n";
    code << "                summary_output['_plot_error'] = 'PlotError: ' + str(_pe)\n";
    code << "\n";
    code << "        if plot_b64:\n";
    code << "            output = {\n";
    code << "                'text': json.dumps(summary_output, indent=2),\n";
    code << "                'image': {\n";
    code << "                    'media_type': 'image/png',\n";
    code << "                    'base64': plot_b64\n";
    code << "                },\n";
    code << "                'display_image': {\n";
    code << "                    'media_type': 'image/png',\n";
    code << "                    'base64': display_b64\n";
    code << "                }\n";
    code << "            }\n";
    code << "        else:\n";
    code << "            output = summary_output\n";
    code << "\n";
    code << "        print(json.dumps(output))\n";
    code << "    else:\n";
    code << "        print(json.dumps({\n";
    code << "            'status': 'error',\n";
    code << "            'message': result.get('error_message', 'Simulation failed')\n";
    code << "        }))\n";
    code << "except Exception as e:\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

    return code.str();
}
