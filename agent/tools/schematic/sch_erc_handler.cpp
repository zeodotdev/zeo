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
#include <sstream>


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

    std::ostringstream code;

    // ERC code to human-readable name mapping (matches erc_settings.h ERCE_T enum)
    code << "import json\n\n";
    code << "# ERC code descriptions (from KiCad ERCE_T enum)\n";
    code << "ERC_DESCRIPTIONS = {\n";
    code << "    0: 'Unspecified',\n";
    code << "    1: 'Duplicate sheet name',\n";
    code << "    2: 'Pin or wire endpoint off grid',\n";
    code << "    3: 'Pin not connected',\n";
    code << "    4: 'Pin not driven',\n";
    code << "    5: 'Power pin not driven',\n";
    code << "    6: 'Hierarchical label mismatch',\n";
    code << "    7: 'No-connect connected to pins',\n";
    code << "    8: 'No-connect not connected',\n";
    code << "    9: 'Label not connected',\n";
    code << "    10: 'Similar labels (case mismatch)',\n";
    code << "    11: 'Similar power symbols',\n";
    code << "    12: 'Similar label and power',\n";
    code << "    13: 'Single global label',\n";
    code << "    14: 'Same local/global label',\n";
    code << "    15: 'Different unit footprints',\n";
    code << "    16: 'Missing power input pin',\n";
    code << "    17: 'Missing input pin',\n";
    code << "    18: 'Missing bidirectional pin',\n";
    code << "    19: 'Missing unit',\n";
    code << "    20: 'Different unit net',\n";
    code << "    21: 'Bus alias conflict',\n";
    code << "    22: 'Driver conflict',\n";
    code << "    23: 'Bus entry conflict',\n";
    code << "    24: 'Bus to bus conflict',\n";
    code << "    25: 'Bus to net conflict',\n";
    code << "    26: 'Ground pin not on ground net',\n";
    code << "    27: 'Label on single pin',\n";
    code << "    28: 'Unresolved variable',\n";
    code << "    29: 'Undefined netclass',\n";
    code << "    30: 'Simulation model error',\n";
    code << "    31: 'Dangling wire',\n";
    code << "    32: 'Library symbol issues',\n";
    code << "    33: 'Library symbol mismatch',\n";
    code << "    34: 'Footprint link issues',\n";
    code << "    35: 'Footprint filter mismatch',\n";
    code << "    36: 'Unannotated symbol',\n";
    code << "    37: 'Extra units',\n";
    code << "    38: 'Different unit values',\n";
    code << "    39: 'Duplicate reference',\n";
    code << "    40: 'Bus entry needed',\n";
    code << "    41: 'Four-way junction',\n";
    code << "    42: 'Label on multiple wires',\n";
    code << "    43: 'Unconnected wire endpoint',\n";
    code << "    44: 'Stacked pin syntax',\n";
    code << "}\n\n";
    code << "def get_erc_description(code):\n";
    code << "    \"\"\"Get human-readable description for ERC code.\"\"\"\n";
    code << "    try:\n";
    code << "        code_int = int(code) if isinstance(code, str) else code\n";
    code << "        return ERC_DESCRIPTIONS.get(code_int, f'Unknown error code {code}')\n";
    code << "    except:\n";
    code << "        return str(code)\n\n";

    code << "# Run ERC analysis\n";
    code << "result = sch.erc.analyze()\n\n";

    if( format == "summary" )
    {
        code << "# Format summary with readable error descriptions\n";
        code << "lines = []\n";
        code << "lines.append(f\"ERC Results: {result['error_count']} errors, {result['warning_count']} warnings\")\n";
        code << "lines.append('')\n";
        code << "if result.get('by_type'):\n";
        code << "    for code, items in sorted(result['by_type'].items(), key=lambda x: -len(x[1])):\n";
        code << "        desc = get_erc_description(code)\n";
        code << "        errors = sum(1 for v in items if v.get('severity') == 'error')\n";
        code << "        warnings = len(items) - errors\n";
        code << "        if errors > 0 and warnings > 0:\n";
        code << "            lines.append(f\"  {desc}: {errors} errors, {warnings} warnings\")\n";
        code << "        elif errors > 0:\n";
        code << "            lines.append(f\"  {desc}: {errors} errors\")\n";
        code << "        else:\n";
        code << "            lines.append(f\"  {desc}: {warnings} warnings\")\n";
        code << "print('\\n'.join(lines))\n";
    }
    else if( format == "detailed" )
    {
        if( !includeWarnings )
            code << "result['violations'] = [v for v in result['violations'] if v.get('severity') == 'error']\n";

        code << "# Add readable descriptions to violations\n";
        code << "for v in result.get('violations', []):\n";
        code << "    if 'code' in v:\n";
        code << "        v['description'] = get_erc_description(v['code'])\n";
        code << "output = {\n";
        code << "    'error_count': result['error_count'],\n";
        code << "    'warning_count': result['warning_count'],\n";
        code << "    'violations': result.get('violations', [])\n";
        code << "}\n";
        code << "print(json.dumps(output, indent=2))\n";
    }
    else if( format == "by_type" )
    {
        code << "# Format by_type with readable descriptions\n";
        code << "by_type_readable = {}\n";
        code << "for code, items in result.get('by_type', {}).items():\n";
        code << "    desc = get_erc_description(code)\n";
        code << "    errors = sum(1 for v in items if v.get('severity') == 'error')\n";
        code << "    warnings = len(items) - errors\n";
        code << "    by_type_readable[desc] = {'total': len(items), 'errors': errors, 'warnings': warnings}\n";
        code << "output = {\n";
        code << "    'error_count': result['error_count'],\n";
        code << "    'warning_count': result['warning_count'],\n";
        code << "    'by_type': by_type_readable\n";
        code << "}\n";
        code << "print(json.dumps(output, indent=2))\n";
    }

    return "run_shell sch " + code.str();
}
