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

#include "pcb_crud_handler.h"
#include <sstream>


static const char* PCB_CRUD_TOOLS[] = {
    "pcb_get_summary",
    "pcb_read_section",
    "pcb_validate",
    "pcb_run_drc",
    "pcb_set_outline",
    "pcb_sync_schematic",
    "pcb_place",
    "pcb_add",
    "pcb_update",
    "pcb_delete",
    "pcb_get_pads",
    "pcb_get_footprint",
    "pcb_route",
    "pcb_get_nets",
    "pcb_export",
    "pcb_autoroute"
};


bool PCB_CRUD_HANDLER::CanHandle( const std::string& aToolName ) const
{
    for( const char* name : PCB_CRUD_TOOLS )
    {
        if( aToolName == name )
            return true;
    }
    return false;
}


std::string PCB_CRUD_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string PCB_CRUD_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    if( aToolName == "pcb_get_summary" )
        return "Getting PCB summary";
    else if( aToolName == "pcb_read_section" )
    {
        std::string section = aInput.value( "section", "all" );
        return "Reading PCB " + section;
    }
    else if( aToolName == "pcb_validate" )
        return "Validating PCB file";
    else if( aToolName == "pcb_run_drc" )
        return "Running DRC check";
    else if( aToolName == "pcb_set_outline" )
    {
        std::string shape = aInput.value( "shape", "rectangle" );
        return "Setting board outline (" + shape + ")";
    }
    else if( aToolName == "pcb_sync_schematic" )
        return "Updating PCB from schematic";
    else if( aToolName == "pcb_place" )
    {
        if( aInput.contains( "placements" ) && aInput["placements"].is_array() )
        {
            size_t count = aInput["placements"].size();
            return "Placing " + std::to_string( count ) + " footprint(s)";
        }
        return "Placing footprints";
    }
    else if( aToolName == "pcb_add" )
    {
        if( aInput.contains( "elements" ) && aInput["elements"].is_array() )
        {
            size_t count = aInput["elements"].size();
            if( count == 1 )
            {
                std::string elemType = aInput["elements"][0].value( "element_type", "element" );
                return "Adding " + elemType;
            }
            return "Adding " + std::to_string( count ) + " elements";
        }
        return "Adding elements";
    }
    else if( aToolName == "pcb_update" )
    {
        if( aInput.contains( "updates" ) && aInput["updates"].is_array() )
        {
            size_t count = aInput["updates"].size();
            if( count == 1 )
            {
                std::string target = aInput["updates"][0].value( "target", "" );
                return "Updating " + ( target.empty() ? "element" : target );
            }
            return "Updating " + std::to_string( count ) + " elements";
        }
        return "Updating elements";
    }
    else if( aToolName == "pcb_delete" )
    {
        if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
        {
            size_t count = aInput["targets"].size();
            if( count == 1 )
                return "Deleting " + aInput["targets"][0].get<std::string>();
            return "Deleting " + std::to_string( count ) + " elements";
        }
        return "Deleting elements";
    }
    else if( aToolName == "pcb_get_pads" )
    {
        std::string ref = aInput.value( "ref", "" );
        return "Getting pads for " + ( ref.empty() ? "footprint" : ref );
    }
    else if( aToolName == "pcb_get_footprint" )
    {
        std::string ref = aInput.value( "ref", "" );
        return "Getting footprint " + ( ref.empty() ? "info" : ref );
    }
    else if( aToolName == "pcb_route" )
    {
        std::string fromRef, toRef;
        if( aInput.contains( "from" ) )
            fromRef = aInput["from"].value( "ref", "" );
        if( aInput.contains( "to" ) )
            toRef = aInput["to"].value( "ref", "" );
        if( !fromRef.empty() && !toRef.empty() )
            return "Routing " + fromRef + " to " + toRef;
        return "Routing pads";
    }
    else if( aToolName == "pcb_get_nets" )
        return "Getting net list";
    else if( aToolName == "pcb_export" )
    {
        std::string format = aInput.value( "format", "gerber" );
        return "Exporting " + format;
    }
    else if( aToolName == "pcb_autoroute" )
    {
        if( aInput.contains( "nets" ) && aInput["nets"].is_array() && !aInput["nets"].empty() )
        {
            size_t count = aInput["nets"].size();
            return "Autorouting " + std::to_string( count ) + " net(s)";
        }
        return "Autorouting all nets";
    }

    return "Executing " + aToolName;
}


bool PCB_CRUD_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    // All PCB CRUD tools require IPC
    return CanHandle( aToolName );
}


std::string PCB_CRUD_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "pcb_get_summary" )
        code = GenerateGetSummaryCode( aInput );
    else if( aToolName == "pcb_read_section" )
        code = GenerateReadSectionCode( aInput );
    else if( aToolName == "pcb_validate" )
        code = GenerateValidateCode( aInput );
    else if( aToolName == "pcb_run_drc" )
        code = GenerateRunDrcCode( aInput );
    else if( aToolName == "pcb_set_outline" )
        code = GenerateSetOutlineCode( aInput );
    else if( aToolName == "pcb_sync_schematic" )
        code = GenerateSyncSchematicCode( aInput );
    else if( aToolName == "pcb_place" )
        code = GeneratePlaceCode( aInput );
    else if( aToolName == "pcb_add" )
        code = GenerateAddBatchCode( aInput );
    else if( aToolName == "pcb_update" )
        code = GenerateUpdateBatchCode( aInput );
    else if( aToolName == "pcb_delete" )
        code = GenerateDeleteBatchCode( aInput );
    else if( aToolName == "pcb_get_pads" )
        code = GenerateGetPadsCode( aInput );
    else if( aToolName == "pcb_get_footprint" )
        code = GenerateGetFootprintCode( aInput );
    else if( aToolName == "pcb_route" )
        code = GenerateRouteCode( aInput );
    else if( aToolName == "pcb_get_nets" )
        code = GenerateGetNetsCode( aInput );
    else if( aToolName == "pcb_export" )
        code = GenerateExportCode( aInput );
    else if( aToolName == "pcb_autoroute" )
        code = GenerateAutorouteCode( aInput );

    return "run_shell pcb " + code;
}


std::string PCB_CRUD_HANDLER::EscapePythonString( const std::string& aStr ) const
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


std::string PCB_CRUD_HANDLER::MmToNm( double aMm ) const
{
    int64_t nm = static_cast<int64_t>( aMm * 1000000.0 );
    return std::to_string( nm );
}


std::string PCB_CRUD_HANDLER::GenerateGetSummaryCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Get PCB summary\n";
    code << "summary = {\n";
    code << "    'footprints': [],\n";
    code << "    'tracks': 0,\n";
    code << "    'vias': 0,\n";
    code << "    'zones': 0,\n";
    code << "    'nets': [],\n";
    code << "    'layers': [],\n";
    code << "    'board_outline': None\n";
    code << "}\n";
    code << "\n";
    code << "# Get footprints using correct API\n";
    code << "footprints = board.get_footprints()\n";
    code << "for fp in footprints:\n";
    code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'\n";
    code << "    pos = fp.position\n";
    code << "    summary['footprints'].append({\n";
    code << "        'ref': ref,\n";
    code << "        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',\n";
    code << "        'position': [pos.x / 1000000, pos.y / 1000000],\n";
    code << "        'layer': 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'\n";
    code << "    })\n";
    code << "\n";
    code << "# Count tracks, vias, zones using correct API\n";
    code << "tracks = board.get_tracks()\n";
    code << "summary['tracks'] = len(tracks)\n";
    code << "\n";
    code << "vias = board.get_vias()\n";
    code << "summary['vias'] = len(vias)\n";
    code << "\n";
    code << "zones = board.get_zones()\n";
    code << "summary['zones'] = len(zones)\n";
    code << "\n";
    code << "# Get nets\n";
    code << "try:\n";
    code << "    nets = board.get_nets()\n";
    code << "    summary['nets'] = [{'name': n.name} for n in nets[:50]]  # Limit to 50\n";
    code << "except:\n";
    code << "    pass\n";
    code << "\n";
    code << "# Get layers\n";
    code << "try:\n";
    code << "    layers = board.get_enabled_layers()\n";
    code << "    summary['layers'] = [BoardLayer.Name(l) for l in layers] if layers else []\n";
    code << "except:\n";
    code << "    pass\n";
    code << "\n";
    code << "print(json.dumps(summary, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateReadSectionCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string section = aInput.value( "section", "footprints" );
    std::string filter = aInput.value( "filter", "" );

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";

    if( section == "footprints" )
    {
        code << "# Read footprints\n";
        code << "footprints = board.get_footprints()\n";
        code << "result = []\n";
        code << "for fp in footprints:\n";
        code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'\n";
        if( !filter.empty() )
        {
            code << "    import fnmatch\n";
            code << "    if not fnmatch.fnmatch(ref, '" << EscapePythonString( filter ) << "'):\n";
            code << "        continue\n";
        }
        code << "    pos = fp.position\n";
        code << "    angle = fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0\n";
        code << "    result.append({\n";
        code << "        'id': fp.id.value,\n";
        code << "        'ref': ref,\n";
        code << "        'value': fp.value_field.text.value if hasattr(fp, 'value_field') else '',\n";
        code << "        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',\n";
        code << "        'position': [pos.x / 1000000, pos.y / 1000000],\n";
        code << "        'angle': angle,\n";
        code << "        'layer': 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu',\n";
        code << "        'locked': getattr(fp, 'locked', False)\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "tracks" )
    {
        code << "# Read tracks\n";
        code << "tracks = board.get_tracks()\n";
        code << "result = []\n";
        code << "for t in list(tracks)[:100]:  # Limit to 100\n";
        code << "    result.append({\n";
        code << "        'id': t.id.value,\n";
        code << "        'start': [t.start.x / 1000000, t.start.y / 1000000],\n";
        code << "        'end': [t.end.x / 1000000, t.end.y / 1000000],\n";
        code << "        'width': t.width / 1000000,\n";
        code << "        'layer': BoardLayer.Name(t.layer),\n";
        code << "        'net': t.net.name if hasattr(t, 'net') else ''\n";
        code << "    })\n";
        code << "print(json.dumps({'count': len(tracks), 'tracks': result}, indent=2))\n";
    }
    else if( section == "vias" )
    {
        code << "# Read vias\n";
        code << "vias = board.get_vias()\n";
        code << "result = []\n";
        code << "for v in vias:\n";
        code << "    result.append({\n";
        code << "        'id': v.id.value,\n";
        code << "        'position': [v.position.x / 1000000, v.position.y / 1000000],\n";
        code << "        'diameter': v.diameter / 1000000 if hasattr(v, 'diameter') else 0,\n";
        code << "        'drill': v.drill_diameter / 1000000 if hasattr(v, 'drill_diameter') else 0,\n";
        code << "        'net': v.net.name if hasattr(v, 'net') else ''\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "zones" )
    {
        code << "# Read zones\n";
        code << "zones = board.get_zones()\n";
        code << "result = []\n";
        code << "for z in zones:\n";
        code << "    result.append({\n";
        code << "        'id': z.id.value,\n";
        code << "        'net': z.net.name if hasattr(z, 'net') else '',\n";
        code << "        'layers': [BoardLayer.Name(l) for l in z.layers] if hasattr(z, 'layers') else [],\n";
        code << "        'priority': getattr(z, 'priority', 0)\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "nets" )
    {
        code << "# Read nets\n";
        code << "nets = board.get_nets()\n";
        code << "result = [{'name': n.name} for n in nets]\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "layers" )
    {
        code << "# Read layers\n";
        code << "layers = board.get_enabled_layers()\n";
        code << "result = [{'name': BoardLayer.Name(l)} for l in layers] if layers else []\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "stackup" )
    {
        code << "# Read stackup\n";
        code << "try:\n";
        code << "    stackup = board.get_stackup()\n";
        code << "    result = {'copper_layers': stackup.copper_layer_count if hasattr(stackup, 'copper_layer_count') else 2}\n";
        code << "    print(json.dumps(result, indent=2))\n";
        code << "except Exception as e:\n";
        code << "    print(json.dumps({'error': str(e)}))\n";
    }
    else
    {
        code << "print(json.dumps({'error': 'Unknown section: " << EscapePythonString( section )
             << "'}))\n";
    }

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateValidateCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Validate PCB file - check for common issues\n";
    code << "issues = []\n";
    code << "warnings = []\n";
    code << "stats = {}\n";
    code << "\n";
    code << "# Check board outline (Edge.Cuts layer)\n";
    code << "try:\n";
    code << "    shapes = board.get_shapes()\n";
    code << "    edge_cuts = [s for s in shapes if hasattr(s, 'layer') and s.layer == BoardLayer.BL_Edge_Cuts]\n";
    code << "    stats['edge_cuts_segments'] = len(edge_cuts)\n";
    code << "    if not edge_cuts:\n";
    code << "        issues.append({'type': 'missing_outline', 'message': 'No board outline found (Edge.Cuts layer is empty)'})\n";
    code << "except Exception as e:\n";
    code << "    warnings.append(f'Could not check board outline: {e}')\n";
    code << "\n";
    code << "# Check footprints\n";
    code << "try:\n";
    code << "    footprints = board.get_footprints()\n";
    code << "    stats['footprints'] = len(footprints)\n";
    code << "    origin_count = 0\n";
    code << "    refs_seen = {}\n";
    code << "    for fp in footprints:\n";
    code << "        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'\n";
    code << "        # Check for duplicate references\n";
    code << "        if ref in refs_seen:\n";
    code << "            issues.append({'type': 'duplicate_ref', 'message': f'Duplicate reference: {ref}', 'ref': ref})\n";
    code << "        refs_seen[ref] = True\n";
    code << "        # Check for footprints at origin (likely unplaced)\n";
    code << "        if fp.position.x == 0 and fp.position.y == 0:\n";
    code << "            origin_count += 1\n";
    code << "    if origin_count > 0:\n";
    code << "        warnings.append(f'{origin_count} footprint(s) at origin - may be unplaced')\n";
    code << "except Exception as e:\n";
    code << "    warnings.append(f'Could not check footprints: {e}')\n";
    code << "\n";
    code << "# Check nets and connectivity\n";
    code << "try:\n";
    code << "    nets = board.get_nets()\n";
    code << "    stats['nets'] = len(nets)\n";
    code << "    pads = board.get_pads()\n";
    code << "    stats['pads'] = len(pads)\n";
    code << "    # Count pads per net to find potentially unrouted nets\n";
    code << "    net_pad_counts = {}\n";
    code << "    for pad in pads:\n";
    code << "        net_name = pad.net.name if hasattr(pad, 'net') else ''\n";
    code << "        if net_name:\n";
    code << "            net_pad_counts[net_name] = net_pad_counts.get(net_name, 0) + 1\n";
    code << "    # Find nets with multiple pads (need routing)\n";
    code << "    multi_pad_nets = [n for n, c in net_pad_counts.items() if c > 1]\n";
    code << "    stats['nets_requiring_routing'] = len(multi_pad_nets)\n";
    code << "except Exception as e:\n";
    code << "    warnings.append(f'Could not check nets: {e}')\n";
    code << "\n";
    code << "# Check tracks and vias\n";
    code << "try:\n";
    code << "    tracks = board.get_tracks()\n";
    code << "    vias = board.get_vias()\n";
    code << "    stats['tracks'] = len(tracks)\n";
    code << "    stats['vias'] = len(vias)\n";
    code << "except Exception as e:\n";
    code << "    warnings.append(f'Could not check tracks/vias: {e}')\n";
    code << "\n";
    code << "# Check zones\n";
    code << "try:\n";
    code << "    zones = board.get_zones()\n";
    code << "    stats['zones'] = len(zones)\n";
    code << "except Exception as e:\n";
    code << "    warnings.append(f'Could not check zones: {e}')\n";
    code << "\n";
    code << "# Build result\n";
    code << "is_valid = len(issues) == 0\n";
    code << "result = {\n";
    code << "    'valid': is_valid,\n";
    code << "    'issues': issues,\n";
    code << "    'warnings': warnings,\n";
    code << "    'stats': stats\n";
    code << "}\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateRunDrcCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    // NOTE: pcb_run_drc is currently disabled due to a crash bug in KiCad's IPC API.
    // The internal DRC_TOOL::RunTests() expects a non-null progress reporter but the
    // API handler passes nullptr, causing a segfault at drc_tool.cpp:205.
    // This needs to be fixed in KiCad's api_handler_pcb.cpp before we can enable this.

    code << "import json\n";
    code << "\n";
    code << "# DRC via IPC is temporarily disabled due to a KiCad API bug\n";
    code << "# The internal API crashes when called without a progress reporter\n";
    code << "result = {\n";
    code << "    'status': 'error',\n";
    code << "    'message': 'pcb_run_drc is temporarily disabled due to a crash bug in KiCad IPC API. '\n";
    code << "               'Run DRC manually from the PCB editor: Inspect > Design Rules Checker.',\n";
    code << "    'workaround': 'Use the PCB editor UI to run DRC: Inspect menu > Design Rules Checker'\n";
    code << "}\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateSetOutlineCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string shape = aInput.value( "shape", "rectangle" );
    bool clearExisting = aInput.value( "clear_existing", true );

    code << "import json\n";
    code << "from kipy.board_types import BoardSegment\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";

    if( clearExisting )
    {
        code << "# Remove existing board outline (shapes on Edge.Cuts)\n";
        code << "shapes = board.get_shapes()\n";
        code << "edge_items = [s for s in shapes if hasattr(s, 'layer') and s.layer == BoardLayer.BL_Edge_Cuts]\n";
        code << "if edge_items:\n";
        code << "    board.remove_items(edge_items)\n";
        code << "    print(f'Removed {len(edge_items)} existing outline segment(s)')\n";
        code << "\n";
    }

    if( shape == "rectangle" )
    {
        double width = aInput.value( "width", 100.0 );
        double height = aInput.value( "height", 80.0 );
        double originX = 0, originY = 0;
        if( aInput.contains( "origin" ) && aInput["origin"].is_array() && aInput["origin"].size() >= 2 )
        {
            originX = aInput["origin"][0].get<double>();
            originY = aInput["origin"][1].get<double>();
        }

        code << "# Create rectangular outline on Edge.Cuts layer\n";
        code << "width_nm = " << MmToNm( width ) << "\n";
        code << "height_nm = " << MmToNm( height ) << "\n";
        code << "origin_x = " << MmToNm( originX ) << "\n";
        code << "origin_y = " << MmToNm( originY ) << "\n";
        code << "\n";
        code << "corners = [\n";
        code << "    (origin_x, origin_y),\n";
        code << "    (origin_x + width_nm, origin_y),\n";
        code << "    (origin_x + width_nm, origin_y + height_nm),\n";
        code << "    (origin_x, origin_y + height_nm)\n";
        code << "]\n";
        code << "\n";
        code << "segments = []\n";
        code << "for i in range(4):\n";
        code << "    seg = BoardSegment()\n";
        code << "    seg.layer = BoardLayer.BL_Edge_Cuts\n";
        code << "    seg.start = Vector2.from_xy(corners[i][0], corners[i][1])\n";
        code << "    seg.end = Vector2.from_xy(corners[(i+1) % 4][0], corners[(i+1) % 4][1])\n";
        code << "    segments.append(seg)\n";
        code << "\n";
        code << "board.create_items(segments)\n";
        code << "print(json.dumps({'status': 'success', 'message': f'Created rectangular outline: " << width << "mm x " << height << "mm'}))\n";
    }
    else if( shape == "polygon" )
    {
        if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            code << "# Create polygon outline on Edge.Cuts layer\n";
            code << "points = [\n";
            for( const auto& pt : aInput["points"] )
            {
                if( pt.is_array() && pt.size() >= 2 )
                {
                    double x = pt[0].get<double>();
                    double y = pt[1].get<double>();
                    code << "    (" << MmToNm( x ) << ", " << MmToNm( y ) << "),\n";
                }
            }
            code << "]\n";
            code << "\n";
            code << "segments = []\n";
            code << "for i in range(len(points)):\n";
            code << "    seg = BoardSegment()\n";
            code << "    seg.layer = BoardLayer.BL_Edge_Cuts\n";
            code << "    seg.start = Vector2.from_xy(points[i][0], points[i][1])\n";
            code << "    seg.end = Vector2.from_xy(points[(i+1) % len(points)][0], points[(i+1) % len(points)][1])\n";
            code << "    segments.append(seg)\n";
            code << "\n";
            code << "board.create_items(segments)\n";
            code << "print(json.dumps({'status': 'success', 'message': f'Created polygon outline with {len(points)} vertices'}))\n";
        }
        else
        {
            code << "print(json.dumps({'status': 'error', 'message': 'polygon shape requires points array'}))\n";
        }
    }
    else if( shape == "rounded_rectangle" )
    {
        double width = aInput.value( "width", 100.0 );
        double height = aInput.value( "height", 80.0 );
        double radius = aInput.value( "corner_radius", 5.0 );

        code << "# Create rounded rectangle outline on Edge.Cuts layer\n";
        code << "# Note: Currently creates straight segments only (arc corners require additional work)\n";
        code << "width_nm = " << MmToNm( width ) << "\n";
        code << "height_nm = " << MmToNm( height ) << "\n";
        code << "radius_nm = " << MmToNm( radius ) << "\n";
        code << "\n";
        code << "corners = [\n";
        code << "    (0, 0),\n";
        code << "    (width_nm, 0),\n";
        code << "    (width_nm, height_nm),\n";
        code << "    (0, height_nm)\n";
        code << "]\n";
        code << "segments = []\n";
        code << "for i in range(4):\n";
        code << "    seg = BoardSegment()\n";
        code << "    seg.layer = BoardLayer.BL_Edge_Cuts\n";
        code << "    seg.start = Vector2.from_xy(corners[i][0], corners[i][1])\n";
        code << "    seg.end = Vector2.from_xy(corners[(i+1) % 4][0], corners[(i+1) % 4][1])\n";
        code << "    segments.append(seg)\n";
        code << "board.create_items(segments)\n";
        code << "print(json.dumps({'status': 'success', 'message': f'Created outline: " << width << "mm x " << height << "mm (corner radius not yet supported)'}))\n";
    }

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateSyncSchematicCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    bool deleteUnused = aInput.value( "delete_unused", false );
    bool replaceFootprints = aInput.value( "replace_footprints", false );
    bool updateFields = aInput.value( "update_fields", true );
    bool dryRun = aInput.value( "dry_run", false );

    code << "import json\n";
    code << "\n";
    code << "# Update PCB from Schematic\n";
    code << "try:\n";
    code << "    result = board.update_from_schematic(\n";
    code << "        delete_unused_footprints=" << ( deleteUnused ? "True" : "False" ) << ",\n";
    code << "        replace_footprints=" << ( replaceFootprints ? "True" : "False" ) << ",\n";
    code << "        update_fields=" << ( updateFields ? "True" : "False" ) << ",\n";
    code << "        dry_run=" << ( dryRun ? "True" : "False" ) << "\n";
    code << "    )\n";
    code << "    \n";
    code << "    output = {\n";
    code << "        'footprints_added': result.footprints_added,\n";
    code << "        'footprints_replaced': result.footprints_replaced,\n";
    code << "        'footprints_deleted': result.footprints_deleted,\n";
    code << "        'footprints_updated': result.footprints_updated,\n";
    code << "        'nets_changed': result.nets_changed,\n";
    code << "        'warnings': result.warnings,\n";
    code << "        'errors': result.errors,\n";
    code << "        'changes_applied': result.changes_applied\n";
    code << "    }\n";
    code << "    print(json.dumps(output, indent=2))\n";
    code << "except Exception as e:\n";
    code << "    print(f'Error: {e}')\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GeneratePlaceCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "placements" ) || !aInput["placements"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'placements array is required'}))\n";
        return code.str();
    }

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "from kipy.geometry import Vector2, Angle\n";
    code << "\n";
    code << "# Batch footprint placement\n";
    code << "placements = " << aInput["placements"].dump() << "\n";
    code << "\n";
    code << "# Get all footprints and build ref->footprint map\n";
    code << "all_fps = board.get_footprints()\n";
    code << "ref_to_fp = {}\n";
    code << "for fp in all_fps:\n";
    code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None\n";
    code << "    if ref:\n";
    code << "        ref_to_fp[ref] = fp\n";
    code << "\n";
    code << "placed = []\n";
    code << "not_found = []\n";
    code << "\n";
    code << "for p in placements:\n";
    code << "    ref = p.get('ref')\n";
    code << "    if ref not in ref_to_fp:\n";
    code << "        not_found.append(ref)\n";
    code << "        continue\n";
    code << "    \n";
    code << "    fp = ref_to_fp[ref]\n";
    code << "    updated = False\n";
    code << "    \n";
    code << "    # Update position\n";
    code << "    if 'position' in p and len(p['position']) >= 2:\n";
    code << "        fp.position = Vector2.from_xy(int(p['position'][0] * 1000000), int(p['position'][1] * 1000000))\n";
    code << "        updated = True\n";
    code << "    \n";
    code << "    # Update angle\n";
    code << "    if 'angle' in p:\n";
    code << "        fp.orientation = Angle.from_degrees(p['angle'])\n";
    code << "        updated = True\n";
    code << "    \n";
    code << "    # Update layer (flip)\n";
    code << "    if 'layer' in p:\n";
    code << "        fp.layer = BoardLayer.BL_B_Cu if p['layer'] == 'B.Cu' else BoardLayer.BL_F_Cu\n";
    code << "        updated = True\n";
    code << "    \n";
    code << "    if updated:\n";
    code << "        placed.append(ref)\n";
    code << "\n";
    code << "# Apply updates\n";
    code << "if placed:\n";
    code << "    fps_to_update = [ref_to_fp[ref] for ref in placed]\n";
    code << "    board.update_items(fps_to_update)\n";
    code << "\n";
    code << "# Build result with pad positions for immediate routing\n";
    code << "placed_info = []\n";
    code << "for ref in placed:\n";
    code << "    fp = ref_to_fp[ref]\n";
    code << "    fp_layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'\n";
    code << "    \n";
    code << "    fp_info = {\n";
    code << "        'ref': ref,\n";
    code << "        'position': [fp.position.x / 1000000, fp.position.y / 1000000],\n";
    code << "        'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,\n";
    code << "        'layer': fp_layer,\n";
    code << "        'pads': []\n";
    code << "    }\n";
    code << "    # Get pads - pad.position is already in absolute board coordinates\n";
    code << "    if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):\n";
    code << "        for pad in fp.definition.pads:\n";
    code << "            fp_info['pads'].append({\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [pad.position.x / 1000000, pad.position.y / 1000000],\n";
    code << "                'net': pad.net.name if hasattr(pad, 'net') else ''\n";
    code << "            })\n";
    code << "    placed_info.append(fp_info)\n";
    code << "\n";
    code << "result = {'status': 'success', 'placed': placed_info, 'not_found': not_found}\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateAddBatchCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "elements" ) || !aInput["elements"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'elements array is required'}))\n";
        return code.str();
    }

    code << "import json\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.board_types import Track, Via, Zone, BoardSegment, BoardCircle, BoardRectangle, BoardText\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "elements = " << aInput["elements"].dump() << "\n";
    code << "created = []\n";
    code << "errors = []\n";
    code << "\n";
    code << "def mm_to_nm(mm):\n";
    code << "    return int(mm * 1000000)\n";
    code << "\n";
    code << "# Layer name to BoardLayer enum mapping\n";
    code << "layer_map = {\n";
    code << "    'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,\n";
    code << "    'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,\n";
    code << "    'F.SilkS': BoardLayer.BL_F_SilkS, 'B.SilkS': BoardLayer.BL_B_SilkS,\n";
    code << "    'F.Mask': BoardLayer.BL_F_Mask, 'B.Mask': BoardLayer.BL_B_Mask,\n";
    code << "    'Edge.Cuts': BoardLayer.BL_Edge_Cuts,\n";
    code << "    'F.Fab': BoardLayer.BL_F_Fab, 'B.Fab': BoardLayer.BL_B_Fab,\n";
    code << "    'F.CrtYd': BoardLayer.BL_F_CrtYd, 'B.CrtYd': BoardLayer.BL_B_CrtYd,\n";
    code << "}\n";
    code << "\n";
    code << "for idx, elem in enumerate(elements):\n";
    code << "    elem_type = elem.get('element_type', '')\n";
    code << "    try:\n";
    code << "        if elem_type == 'track':\n";
    code << "            layer_name = elem.get('layer', 'F.Cu')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)\n";
    code << "            width = mm_to_nm(elem.get('width', 0.25))\n";
    code << "            net = elem.get('net', '')\n";
    code << "            points = elem.get('points', [])\n";
    code << "            if len(points) < 2:\n";
    code << "                errors.append({'index': idx, 'error': 'track requires at least 2 points'})\n";
    code << "                continue\n";
    code << "            # Use board.route_track for multi-point tracks\n";
    code << "            point_vectors = [Vector2.from_xy(mm_to_nm(p[0]), mm_to_nm(p[1])) for p in points]\n";
    code << "            tracks = board.route_track(points=point_vectors, width=width, layer=layer, net=net if net else None)\n";
    code << "            created.append({'element_type': 'track', 'segments': len(tracks), 'ids': [str(t.id.value) for t in tracks]})\n";
    code << "        \n";
    code << "        elif elem_type == 'via':\n";
    code << "            pos = elem.get('position', [0, 0])\n";
    code << "            size = mm_to_nm(elem.get('size', 0.8))\n";
    code << "            drill = mm_to_nm(elem.get('drill', 0.4))\n";
    code << "            net = elem.get('net', '')\n";
    code << "            position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))\n";
    code << "            via = board.add_via(position=position, diameter=size, drill=drill, net=net if net else None)\n";
    code << "            created.append({'element_type': 'via', 'position': pos, 'id': str(via.id.value)})\n";
    code << "        \n";
    code << "        elif elem_type == 'zone':\n";
    code << "            layer_name = elem.get('layer', 'F.Cu')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)\n";
    code << "            net = elem.get('net', '')\n";
    code << "            priority = elem.get('priority', 0)\n";
    code << "            outline_pts = elem.get('outline', [])\n";
    code << "            outline = [Vector2.from_xy(mm_to_nm(p[0]), mm_to_nm(p[1])) for p in outline_pts]\n";
    code << "            zone = board.add_zone(outline=outline, layers=[layer], net=net if net else None, priority=priority)\n";
    code << "            created.append({'element_type': 'zone', 'layer': layer_name, 'id': str(zone.id.value)})\n";
    code << "        \n";
    code << "        elif elem_type == 'line':\n";
    code << "            layer_name = elem.get('layer', 'F.SilkS')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)\n";
    code << "            points = elem.get('points', [[0,0], [10,10]])\n";
    code << "            seg = BoardSegment()\n";
    code << "            seg.layer = layer\n";
    code << "            seg.start = Vector2.from_xy(mm_to_nm(points[0][0]), mm_to_nm(points[0][1]))\n";
    code << "            seg.end = Vector2.from_xy(mm_to_nm(points[1][0]), mm_to_nm(points[1][1]))\n";
    code << "            result = board.create_items([seg])\n";
    code << "            created.append({'element_type': 'line', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})\n";
    code << "        \n";
    code << "        elif elem_type == 'rectangle':\n";
    code << "            layer_name = elem.get('layer', 'F.SilkS')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)\n";
    code << "            tl = elem.get('top_left', [0, 0])\n";
    code << "            br = elem.get('bottom_right', [10, 10])\n";
    code << "            rect = BoardRectangle()\n";
    code << "            rect.layer = layer\n";
    code << "            rect.top_left = Vector2.from_xy(mm_to_nm(tl[0]), mm_to_nm(tl[1]))\n";
    code << "            rect.bottom_right = Vector2.from_xy(mm_to_nm(br[0]), mm_to_nm(br[1]))\n";
    code << "            result = board.create_items([rect])\n";
    code << "            created.append({'element_type': 'rectangle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})\n";
    code << "        \n";
    code << "        elif elem_type == 'circle':\n";
    code << "            layer_name = elem.get('layer', 'F.SilkS')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)\n";
    code << "            center = elem.get('center', [0, 0])\n";
    code << "            radius = elem.get('radius', 5.0)\n";
    code << "            circ = BoardCircle()\n";
    code << "            circ.layer = layer\n";
    code << "            circ.center = Vector2.from_xy(mm_to_nm(center[0]), mm_to_nm(center[1]))\n";
    code << "            circ.radius_point = Vector2.from_xy(mm_to_nm(center[0] + radius), mm_to_nm(center[1]))\n";
    code << "            result = board.create_items([circ])\n";
    code << "            created.append({'element_type': 'circle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})\n";
    code << "        \n";
    code << "        elif elem_type == 'text':\n";
    code << "            layer_name = elem.get('layer', 'F.SilkS')\n";
    code << "            layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)\n";
    code << "            text_content = elem.get('text', '')\n";
    code << "            pos = elem.get('position', [0, 0])\n";
    code << "            txt = BoardText()\n";
    code << "            txt.layer = layer\n";
    code << "            txt.position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))\n";
    code << "            txt.value = text_content\n";
    code << "            result = board.create_items([txt])\n";
    code << "            created.append({'element_type': 'text', 'text': text_content, 'id': str(result[0].id.value) if result else ''})\n";
    code << "        \n";
    code << "        else:\n";
    code << "            errors.append({'index': idx, 'error': f'Unknown element_type: {elem_type}'})\n";
    code << "    \n";
    code << "    except Exception as e:\n";
    code << "        errors.append({'index': idx, 'element_type': elem_type, 'error': str(e)})\n";
    code << "\n";
    code << "status = 'success' if not errors else ('partial' if created else 'error')\n";
    code << "print(json.dumps({'status': status, 'created': created, 'errors': errors}, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateUpdateBatchCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "updates" ) || !aInput["updates"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'updates array is required'}))\n";
        return code.str();
    }

    code << "import json\n";
    code << "from kipy.geometry import Vector2, Angle\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "from kipy.board_types import Net\n";
    code << "\n";
    code << "updates = " << aInput["updates"].dump() << "\n";
    code << "\n";
    code << "def mm_to_nm(mm):\n";
    code << "    return int(mm * 1000000)\n";
    code << "\n";
    code << "# Layer name to BoardLayer enum mapping\n";
    code << "layer_map = {\n";
    code << "    'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,\n";
    code << "    'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,\n";
    code << "}\n";
    code << "\n";
    code << "# Build ref->footprint map for reference lookups\n";
    code << "all_fps = board.get_footprints()\n";
    code << "ref_to_fp = {}\n";
    code << "for fp in all_fps:\n";
    code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None\n";
    code << "    if ref:\n";
    code << "        ref_to_fp[ref] = fp\n";
    code << "\n";
    code << "updated = []\n";
    code << "not_found = []\n";
    code << "errors = []\n";
    code << "\n";
    code << "for upd in updates:\n";
    code << "    target = upd.get('target', '')\n";
    code << "    if not target:\n";
    code << "        errors.append({'error': 'missing target'})\n";
    code << "        continue\n";
    code << "    \n";
    code << "    item = None\n";
    code << "    is_footprint = False\n";
    code << "    \n";
    code << "    # Try as reference first (footprints)\n";
    code << "    if target in ref_to_fp:\n";
    code << "        item = ref_to_fp[target]\n";
    code << "        is_footprint = True\n";
    code << "    # For UUIDs, we'd need to search all item types\n";
    code << "    # This is currently only supported for footprints\n";
    code << "    \n";
    code << "    if not item:\n";
    code << "        not_found.append(target)\n";
    code << "        continue\n";
    code << "    \n";
    code << "    try:\n";
    code << "        changed = False\n";
    code << "        \n";
    code << "        # Position update\n";
    code << "        if 'position' in upd and len(upd['position']) >= 2:\n";
    code << "            item.position = Vector2.from_xy(mm_to_nm(upd['position'][0]), mm_to_nm(upd['position'][1]))\n";
    code << "            changed = True\n";
    code << "        \n";
    code << "        # Angle/rotation update (for footprints)\n";
    code << "        if 'angle' in upd and is_footprint:\n";
    code << "            item.orientation = Angle.from_degrees(upd['angle'])\n";
    code << "            changed = True\n";
    code << "        \n";
    code << "        # Layer update (flip for footprints)\n";
    code << "        if 'layer' in upd:\n";
    code << "            layer_name = upd['layer']\n";
    code << "            item.layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)\n";
    code << "            changed = True\n";
    code << "        \n";
    code << "        # Locked update\n";
    code << "        if 'locked' in upd:\n";
    code << "            item.locked = upd['locked']\n";
    code << "            changed = True\n";
    code << "        \n";
    code << "        if changed:\n";
    code << "            board.update_items([item])\n";
    code << "            updated.append(target)\n";
    code << "    \n";
    code << "    except Exception as e:\n";
    code << "        errors.append({'target': target, 'error': str(e)})\n";
    code << "\n";
    code << "status = 'success' if not errors and not not_found else 'partial'\n";
    code << "print(json.dumps({'status': status, 'updated': updated, 'not_found': not_found, 'errors': errors}, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateDeleteBatchCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";

    // Handle targets array (refs or UUIDs)
    if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
    {
        code << "targets = " << aInput["targets"].dump() << "\n";
        code << "\n";
        code << "# Build ref->footprint map for reference lookups\n";
        code << "all_fps = board.get_footprints()\n";
        code << "ref_to_fp = {}\n";
        code << "for fp in all_fps:\n";
        code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None\n";
        code << "    if ref:\n";
        code << "        ref_to_fp[ref] = fp\n";
        code << "\n";
        code << "# Build id->item map for UUID lookups (shapes, text, tracks, vias, zones)\n";
        code << "id_to_item = {}\n";
        code << "for fp in all_fps:\n";
        code << "    id_to_item[str(fp.id.value)] = fp\n";
        code << "for item in board.get_shapes():\n";
        code << "    id_to_item[str(item.id.value)] = item\n";
        code << "for item in board.get_text():\n";
        code << "    id_to_item[str(item.id.value)] = item\n";
        code << "for item in board.get_tracks():\n";
        code << "    id_to_item[str(item.id.value)] = item\n";
        code << "for item in board.get_vias():\n";
        code << "    id_to_item[str(item.id.value)] = item\n";
        code << "for item in board.get_zones():\n";
        code << "    id_to_item[str(item.id.value)] = item\n";
        code << "\n";
        code << "items_to_delete = []\n";
        code << "not_found = []\n";
        code << "\n";
        code << "for target in targets:\n";
        code << "    # Try as footprint reference first\n";
        code << "    if target in ref_to_fp:\n";
        code << "        items_to_delete.append(ref_to_fp[target])\n";
        code << "    # Then try as UUID\n";
        code << "    elif target in id_to_item:\n";
        code << "        items_to_delete.append(id_to_item[target])\n";
        code << "    else:\n";
        code << "        not_found.append(target)\n";
        code << "\n";
        code << "deleted_count = 0\n";
        code << "if items_to_delete:\n";
        code << "    try:\n";
        code << "        board.remove_items(items_to_delete)\n";
        code << "        deleted_count = len(items_to_delete)\n";
        code << "    except Exception as e:\n";
        code << "        not_found.append(str(e))\n";
        code << "\n";
        code << "print(json.dumps({'status': 'success', 'deleted': deleted_count, 'not_found': not_found}, indent=2))\n";
    }
    // Handle query-based deletion
    else if( aInput.contains( "query" ) && aInput["query"].is_object() )
    {
        auto query = aInput["query"];
        std::string layer = query.value( "layer", "" );
        std::string type = query.value( "type", "" );
        std::string net = query.value( "net", "" );

        // Map type name to getter method
        code << "# Delete by query\n";
        if( type == "track" )
        {
            code << "items = board.get_tracks()\n";
        }
        else if( type == "via" )
        {
            code << "items = board.get_vias()\n";
        }
        else if( type == "zone" )
        {
            code << "items = board.get_zones()\n";
        }
        else if( type == "shape" || type == "line" || type == "rectangle" || type == "circle" )
        {
            code << "items = board.get_shapes()\n";
        }
        else if( type == "text" )
        {
            code << "items = board.get_text()\n";
        }
        else if( type == "footprint" )
        {
            code << "items = board.get_footprints()\n";
        }
        else
        {
            code << "items = []\n";
        }

        code << "items_to_delete = []\n";
        code << "for item in items:\n";
        if( !layer.empty() )
        {
            code << "    if hasattr(item, 'layer') and BoardLayer.Name(item.layer) != '"
                 << EscapePythonString( layer ) << "':\n";
            code << "        continue\n";
        }
        if( !net.empty() )
        {
            code << "    if hasattr(item, 'net') and item.net.name != '"
                 << EscapePythonString( net ) << "':\n";
            code << "        continue\n";
        }
        code << "    items_to_delete.append(item)\n";
        code << "\n";
        code << "deleted_count = 0\n";
        code << "if items_to_delete:\n";
        code << "    board.remove_items(items_to_delete)\n";
        code << "    deleted_count = len(items_to_delete)\n";
        code << "\n";
        code << "print(json.dumps({'status': 'success', 'deleted': deleted_count}, indent=2))\n";
    }
    else
    {
        code << "print(json.dumps({'status': 'error', 'message': 'targets array or query object required'}))\n";
    }

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateGetPadsCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string ref = aInput.value( "ref", "" );
    if( ref.empty() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'ref is required'}))\n";
        return code.str();
    }

    code << "import json\n";
    code << "\n";
    code << "# Find footprint by reference\n";
    code << "target_fp = board.footprints.get_by_reference('" << EscapePythonString( ref ) << "')\n";
    code << "\n";
    code << "if not target_fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( ref ) << "'}))\n";
    code << "else:\n";
    code << "    # Get pads from footprint definition\n";
    code << "    # NOTE: pad.position is already in ABSOLUTE board coordinates\n";
    code << "    # KiCad API returns transformed positions, no manual transformation needed\n";
    code << "    pads = []\n";
    code << "    \n";
    code << "    if hasattr(target_fp, 'definition') and hasattr(target_fp.definition, 'pads'):\n";
    code << "        for pad in target_fp.definition.pads:\n";
    code << "            pad_info = {\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [pad.position.x / 1000000, pad.position.y / 1000000],\n";
    code << "                'net': pad.net.name if hasattr(pad, 'net') else ''\n";
    code << "            }\n";
    code << "            pads.append(pad_info)\n";
    code << "    \n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'ref': '" << EscapePythonString( ref ) << "',\n";
    code << "        'pads': pads\n";
    code << "    }\n";
    code << "    print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateGetFootprintCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string ref = aInput.value( "ref", "" );
    if( ref.empty() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'ref is required'}))\n";
        return code.str();
    }

    code << "import json\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Find footprint by reference\n";
    code << "fp = board.footprints.get_by_reference('" << EscapePythonString( ref ) << "')\n";
    code << "\n";
    code << "if not fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( ref ) << "'}))\n";
    code << "else:\n";
    code << "    # Get pads from footprint definition - pad.position is already absolute\n";
    code << "    pads = []\n";
    code << "    if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):\n";
    code << "        for pad in fp.definition.pads:\n";
    code << "            # pad.position contains absolute board coordinates (already transformed)\n";
    code << "            pad_info = {\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [pad.position.x / 1000000, pad.position.y / 1000000],\n";
    code << "                'net': pad.net.name if hasattr(pad, 'net') else ''\n";
    code << "            }\n";
    code << "            pads.append(pad_info)\n";
    code << "    \n";
    code << "    fp_layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'\n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'ref': '" << EscapePythonString( ref ) << "',\n";
    code << "        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',\n";
    code << "        'position': [fp.position.x / 1000000, fp.position.y / 1000000],\n";
    code << "        'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,\n";
    code << "        'layer': fp_layer,\n";
    code << "        'locked': getattr(fp, 'locked', False),\n";
    code << "        'pads': pads\n";
    code << "    }\n";
    code << "    print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateRouteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "from" ) || !aInput.contains( "to" ) )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'from and to are required'}))\n";
        return code.str();
    }

    auto fromPad = aInput["from"];
    auto toPad = aInput["to"];
    std::string fromRef = fromPad.value( "ref", "" );
    std::string fromPadNum = fromPad.value( "pad", "" );
    std::string toRef = toPad.value( "ref", "" );
    std::string toPadNum = toPad.value( "pad", "" );
    double width = aInput.value( "width", 0.25 );
    std::string layer = aInput.value( "layer", "" );

    code << "import json\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "def mm_to_nm(mm):\n";
    code << "    return int(mm * 1000000)\n";
    code << "\n";
    code << "def get_pad_abs_position(fp, pad_num):\n";
    code << "    \"\"\"Get absolute position of a pad by number - pad.position is already absolute\"\"\"\n";
    code << "    if not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):\n";
    code << "        return None, None\n";
    code << "    for pad in fp.definition.pads:\n";
    code << "        if str(pad.number) == str(pad_num):\n";
    code << "            # pad.position contains absolute board coordinates (already transformed)\n";
    code << "            net = pad.net.name if hasattr(pad, 'net') else None\n";
    code << "            return (pad.position.x, pad.position.y), net\n";
    code << "    return None, None\n";
    code << "\n";
    code << "# Layer name to BoardLayer enum mapping\n";
    code << "layer_map = {\n";
    code << "    'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,\n";
    code << "    'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,\n";
    code << "}\n";
    code << "\n";
    code << "# Find footprints by reference\n";
    code << "from_fp = board.footprints.get_by_reference('" << EscapePythonString( fromRef ) << "')\n";
    code << "to_fp = board.footprints.get_by_reference('" << EscapePythonString( toRef ) << "')\n";
    code << "\n";
    code << "from_pad_num = '" << EscapePythonString( fromPadNum ) << "'\n";
    code << "to_pad_num = '" << EscapePythonString( toPadNum ) << "'\n";
    code << "width_nm = mm_to_nm(" << width << ")\n";
    code << "layer_name = '" << ( layer.empty() ? "F.Cu" : EscapePythonString( layer ) ) << "'\n";
    code << "route_layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)\n";
    code << "\n";
    code << "if not from_fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( fromRef ) << "'}))\n";
    code << "elif not to_fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( toRef ) << "'}))\n";
    code << "else:\n";
    code << "    # Get pad positions from footprint definitions\n";
    code << "    from_pos, from_net = get_pad_abs_position(from_fp, from_pad_num)\n";
    code << "    to_pos, _ = get_pad_abs_position(to_fp, to_pad_num)\n";
    code << "    \n";
    code << "    if not from_pos:\n";
    code << "        print(json.dumps({'status': 'error', 'message': f'Pad {from_pad_num} not found on " << EscapePythonString( fromRef ) << "'}))\n";
    code << "    elif not to_pos:\n";
    code << "        print(json.dumps({'status': 'error', 'message': f'Pad {to_pad_num} not found on " << EscapePythonString( toRef ) << "'}))\n";
    code << "    else:\n";
    code << "        # Create track using board.route_track\n";
    code << "        points = [\n";
    code << "            Vector2.from_xy(int(from_pos[0]), int(from_pos[1])),\n";
    code << "            Vector2.from_xy(int(to_pos[0]), int(to_pos[1]))\n";
    code << "        ]\n";
    code << "        tracks = board.route_track(points=points, width=width_nm, layer=route_layer, net=from_net)\n";
    code << "        \n";
    code << "        track_info = []\n";
    code << "        for t in tracks:\n";
    code << "            track_info.append({\n";
    code << "                'id': str(t.id.value),\n";
    code << "                'layer': layer_name,\n";
    code << "                'from': [t.start.x / 1000000, t.start.y / 1000000],\n";
    code << "                'to': [t.end.x / 1000000, t.end.y / 1000000]\n";
    code << "            })\n";
    code << "        \n";
    code << "        print(json.dumps({\n";
    code << "            'status': 'success',\n";
    code << "            'tracks': track_info,\n";
    code << "            'vias': []\n";
    code << "        }, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateGetNetsCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string filter = aInput.value( "filter", "" );
    bool includePads = aInput.value( "include_pads", true );
    bool unroutedOnly = aInput.value( "unrouted_only", false );

    code << "import json\n";
    code << "import fnmatch\n";
    code << "\n";
    code << "# Get all nets\n";
    code << "nets = board.get_nets()\n";
    code << "result_nets = []\n";
    code << "\n";
    if( !filter.empty() )
    {
        code << "filter_pattern = '" << EscapePythonString( filter ) << "'\n";
    }
    code << "\n";

    // Build pad map if needed
    if( includePads )
    {
        code << "# Build net->pads map\n";
        code << "net_pads = {}  # net_name -> [{ref, pad}, ...]\n";
        code << "all_fps = board.get_footprints()\n";
        code << "\n";
        code << "for fp in all_fps:\n";
        code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'\n";
        code << "    if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):\n";
        code << "        for pad in fp.definition.pads:\n";
        code << "            net_name = pad.net.name if hasattr(pad, 'net') else ''\n";
        code << "            if net_name:\n";
        code << "                if net_name not in net_pads:\n";
        code << "                    net_pads[net_name] = []\n";
        code << "                net_pads[net_name].append({'ref': ref, 'pad': str(pad.number)})\n";
        code << "\n";
    }

    // Use connectivity API to get actual routing status
    code << "# Get actual routing status from KiCad connectivity engine\n";
    code << "unrouted_info = {}  # net_name -> {routed, unrouted, is_complete}\n";
    code << "try:\n";
    code << "    unrouted_nets = board.connectivity.get_unrouted_nets()\n";
    code << "    for info in unrouted_nets:\n";
    code << "        unrouted_info[info.net_name] = {\n";
    code << "            'routed_connections': info.routed_connections,\n";
    code << "            'unrouted_connections': info.unrouted_connections,\n";
    code << "            'is_complete': info.is_complete\n";
    code << "        }\n";
    code << "except Exception as e:\n";
    code << "    # Fallback if connectivity API unavailable\n";
    code << "    pass\n";
    code << "\n";

    code << "for net in nets:\n";
    if( !filter.empty() )
    {
        code << "    if not fnmatch.fnmatch(net.name, filter_pattern):\n";
        code << "        continue\n";
    }

    if( unroutedOnly )
    {
        // Only include nets that are actually unrouted according to connectivity engine
        code << "    # Skip nets that are fully routed (or have < 2 pads)\n";
        code << "    conn_info = unrouted_info.get(net.name)\n";
        code << "    if conn_info and conn_info['is_complete']:\n";
        code << "        continue\n";
        if( includePads )
        {
            code << "    pads = net_pads.get(net.name, [])\n";
            code << "    if len(pads) < 2:\n";
            code << "        continue\n";
        }
    }

    code << "    net_info = {'name': net.name}\n";

    if( includePads )
    {
        code << "    net_info['pads'] = net_pads.get(net.name, [])\n";
    }

    // Add routing status from connectivity engine
    code << "    conn_info = unrouted_info.get(net.name)\n";
    code << "    if conn_info:\n";
    code << "        net_info['routed_connections'] = conn_info['routed_connections']\n";
    code << "        net_info['unrouted_connections'] = conn_info['unrouted_connections']\n";
    code << "        net_info['is_complete'] = conn_info['is_complete']\n";
    code << "    else:\n";
    code << "        # Net not in unrouted list means it's complete (or single pad)\n";
    code << "        net_info['is_complete'] = True\n";
    code << "        net_info['unrouted_connections'] = 0\n";
    code << "    result_nets.append(net_info)\n";
    code << "\n";
    code << "print(json.dumps({'status': 'success', 'nets': result_nets}, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateRefToFootprintMap() const
{
    std::ostringstream code;

    code << "# Build ref->footprint map\n";
    code << "all_fps = board.get_footprints()\n";
    code << "ref_to_fp = {}\n";
    code << "for fp in all_fps:\n";
    code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None\n";
    code << "    if ref:\n";
    code << "        ref_to_fp[ref] = fp\n";
    code << "\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateExportCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string format = aInput.value( "format", "gerber" );
    std::string outputDir = aInput.value( "output_dir", "" );

    if( outputDir.empty() )
    {
        code << "print('Error: output_dir is required')\n";
        return code.str();
    }

    code << "import os\n";
    code << "import json\n";
    code << "\n";
    code << "output_dir = '" << EscapePythonString( outputDir ) << "'\n";
    code << "os.makedirs(output_dir, exist_ok=True)\n";
    code << "\n";

    if( format == "gerber" )
    {
        code << "# Export Gerber files\n";
        code << "try:\n";
        code << "    # Note: Export API may vary - this is a placeholder\n";
        code << "    # Real implementation would use board.export_gerber() or similar\n";
        code << "    print(f'Gerber export to {output_dir} - API pending implementation')\n";
        code << "    print('Use kicad-cli for production Gerber export')\n";
        code << "except Exception as e:\n";
        code << "    print(f'Export error: {e}')\n";
    }
    else if( format == "drill" )
    {
        code << "# Export drill files\n";
        code << "print(f'Drill export to {output_dir} - API pending implementation')\n";
        code << "print('Use kicad-cli for production drill export')\n";
    }
    else if( format == "pdf" || format == "svg" )
    {
        code << "# Export " << format << "\n";
        code << "print(f'" << format << " export to {output_dir} - API pending implementation')\n";
    }
    else if( format == "step" )
    {
        code << "# Export STEP 3D model\n";
        code << "print(f'STEP export to {output_dir} - API pending implementation')\n";
        code << "print('Use kicad-cli for STEP export')\n";
    }
    else
    {
        code << "print(f'Unknown export format: " << format << "')\n";
    }

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateAutorouteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    int maxPasses = aInput.value( "max_passes", 100 );
    bool viasAllowed = aInput.value( "vias_allowed", true );

    code << "import json\n";
    code << "import heapq\n";
    code << "import traceback\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.board import board_types_pb2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Autoroute configuration\n";
    code << "VIAS_ALLOWED = " << ( viasAllowed ? "True" : "False" ) << "\n";
    code << "GRID_SIZE = 150000  # 0.15mm grid in nm (balance of precision and speed)\n";
    code << "TRACK_WIDTH = 250000  # 0.25mm in nm\n";
    code << "VIA_DIAMETER = 800000  # 0.8mm in nm\n";
    code << "VIA_DRILL = 400000  # 0.4mm in nm\n";
    code << "CLEARANCE = 200000  # 0.2mm clearance in nm\n";
    code << "\n";

    // Check for specific nets to route
    if( aInput.contains( "nets" ) && aInput["nets"].is_array() && !aInput["nets"].empty() )
    {
        code << "nets_to_route = set(" << aInput["nets"].dump() << ")\n";
    }
    else
    {
        code << "nets_to_route = None  # Route all unrouted nets\n";
    }

    code << R"PYTHON(

def mm_to_nm(mm):
    return int(mm * 1000000)

def nm_to_grid(nm):
    """Convert nm to grid coordinates"""
    return nm // GRID_SIZE

def grid_to_nm(grid):
    """Convert grid coordinates to nm (center of grid cell)"""
    return grid * GRID_SIZE + GRID_SIZE // 2

class ObstacleMap:
    """Tracks occupied grid cells per layer"""
    def __init__(self):
        # layer -> set of (gx, gy) tuples
        self.occupied = {0: set(), 1: set()}  # 0=F.Cu, 1=B.Cu

    def mark_occupied(self, layer, gx, gy, radius=1):
        """Mark grid cells as occupied"""
        for dx in range(-radius, radius + 1):
            for dy in range(-radius, radius + 1):
                self.occupied[layer].add((gx + dx, gy + dy))

    def mark_line(self, layer, gx1, gy1, gx2, gy2, width=1):
        """Mark grid cells along a line as occupied"""
        # Bresenham's line algorithm
        dx = abs(gx2 - gx1)
        dy = abs(gy2 - gy1)
        sx = 1 if gx1 < gx2 else -1
        sy = 1 if gy1 < gy2 else -1
        err = dx - dy

        gx, gy = gx1, gy1
        while True:
            self.mark_occupied(layer, gx, gy, width)
            if gx == gx2 and gy == gy2:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                gx += sx
            if e2 < dx:
                err += dx
                gy += sy

    def is_free(self, layer, gx, gy):
        """Check if a grid cell is free"""
        return (gx, gy) not in self.occupied[layer]

    def is_path_free(self, layer, gx1, gy1, gx2, gy2):
        """Check if a straight path is free"""
        dx = abs(gx2 - gx1)
        dy = abs(gy2 - gy1)
        sx = 1 if gx1 < gx2 else -1
        sy = 1 if gy1 < gy2 else -1
        err = dx - dy

        gx, gy = gx1, gy1
        while True:
            if not self.is_free(layer, gx, gy):
                return False
            if gx == gx2 and gy == gy2:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                gx += sx
            if e2 < dx:
                err += dx
                gy += sy
        return True


def astar_route(obstacles, start, end, start_layer=0, current_net=None, pad_net_map=None):
    """
    A* pathfinding with layer changes (vias).

    State: (gx, gy, layer)
    Returns: list of (gx, gy, layer) or None if no path

    Args:
        obstacles: ObstacleMap for tracks/vias
        start: (x, y) in nm
        end: (x, y) in nm
        start_layer: starting layer (0=F.Cu, 1=B.Cu)
        current_net: name of net being routed (to allow routing to own pads)
        pad_net_map: dict mapping (gx, gy) -> net_name for pad collision checking
    """
    sgx, sgy = nm_to_grid(start[0]), nm_to_grid(start[1])
    egx, egy = nm_to_grid(end[0]), nm_to_grid(end[1])

    def heuristic(gx, gy, layer):
        # Octile distance for 8-connected grid (diagonal cost = sqrt(2))
        # This is optimal for A* with diagonal moves
        dx = abs(gx - egx)
        dy = abs(gy - egy)
        return max(dx, dy) + 0.414 * min(dx, dy)  # sqrt(2) - 1 ≈ 0.414

    def is_cell_free(layer, gx, gy):
        """Check if cell is free from obstacles AND other nets' pads"""
        # Check track/via obstacles
        if not obstacles.is_free(layer, gx, gy):
            return False
        # Check pad obstacles - allow routing through our own net's pads
        if pad_net_map and (gx, gy) in pad_net_map:
            pad_owner = pad_net_map[(gx, gy)]
            if pad_owner != current_net:
                return False  # Can't route through another net's pad
        return True

    # Priority queue: (f_score, g_score, gx, gy, layer, path)
    start_state = (sgx, sgy, start_layer)
    initial_h = heuristic(sgx, sgy, start_layer)
    heap = [(initial_h, 0, sgx, sgy, start_layer, [start_state])]
    visited = set()

    # Directions: 8-connected grid (orthogonal + diagonal for 45° angles)
    # (dx, dy, cost) - diagonal moves cost sqrt(2) ~= 1.414
    directions = [
        (1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),  # Orthogonal
        (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)  # Diagonal 45°
    ]

    max_iterations = 100000  # Balanced for 0.15mm grid
    iterations = 0

    while heap and iterations < max_iterations:
        iterations += 1
        f, g, gx, gy, layer, path = heapq.heappop(heap)

        state = (gx, gy, layer)
        if state in visited:
            continue
        visited.add(state)

        # Check if reached destination
        if gx == egx and gy == egy:
            return path

        # Try moving in each direction (orthogonal and diagonal)
        for dx, dy, move_cost in directions:
            nx, ny = gx + dx, gy + dy
            if (nx, ny, layer) not in visited and is_cell_free(layer, nx, ny):
                new_g = g + move_cost
                new_h = heuristic(nx, ny, layer)
                new_path = path + [(nx, ny, layer)]
                heapq.heappush(heap, (new_g + new_h, new_g, nx, ny, layer, new_path))

        # Try layer change (via) if allowed
        if VIAS_ALLOWED:
            other_layer = 1 - layer
            if (gx, gy, other_layer) not in visited and is_cell_free(other_layer, gx, gy):
                new_g = g + 5  # Via cost penalty
                new_h = heuristic(gx, gy, other_layer)
                new_path = path + [(gx, gy, other_layer)]
                heapq.heappush(heap, (new_g + new_h, new_g, gx, gy, other_layer, new_path))

    return None  # No path found


def simplify_path(path):
    """Remove redundant points from path (keep corners and layer changes)"""
    if len(path) <= 2:
        return path

    simplified = [path[0]]

    for i in range(1, len(path) - 1):
        prev = path[i - 1]
        curr = path[i]
        next_pt = path[i + 1]

        # Keep if layer changes
        if prev[2] != curr[2] or curr[2] != next_pt[2]:
            simplified.append(curr)
            continue

        # Keep if direction changes
        dx1, dy1 = curr[0] - prev[0], curr[1] - prev[1]
        dx2, dy2 = next_pt[0] - curr[0], next_pt[1] - curr[1]
        if (dx1, dy1) != (dx2, dy2):
            simplified.append(curr)

    simplified.append(path[-1])
    return simplified


# Initialize obstacle map
obstacles = ObstacleMap()

# Collect all pads by iterating through footprints
# NOTE: pad.position is already in ABSOLUTE board coordinates (KiCad API returns transformed positions)
# No manual transformation is needed - the pad positions are already correct
all_pad_positions = []  # List of (x, y, net_name, layer) tuples - layer: 0=F.Cu, 1=B.Cu, -1=both
debug_info = []
sample_pads = []  # Collect sample pads for debugging
for fp in board.get_footprints():
    fp_ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else 'unknown'
    # Get footprint layer to determine pad layer for SMD pads
    fp_layer = fp.layer if hasattr(fp, 'layer') else BoardLayer.BL_F_Cu
    fp_layer_idx = 0 if fp_layer == BoardLayer.BL_F_Cu else 1

    for pad in fp.definition.pads:
        try:
            pos = pad.position
            # pad.position is already in absolute board coordinates (nm)
            pad_x = pos.x if pos else None
            pad_y = pos.y if pos else None

            # Skip pads with invalid positions
            if pad_x is None or pad_y is None:
                debug_info.append(f'{fp_ref}: pad has None position')
                continue

            net_name = pad.net.name if hasattr(pad, 'net') and pad.net else ''

            # Determine pad layer based on pad type
            # SMD pads are on the footprint's layer only
            # PTH (plated through-hole) and NPTH pads are on both layers
            pad_type = pad.pad_type if hasattr(pad, 'pad_type') else None
            # PadType enum: PT_UNKNOWN=0, PT_PTH=1, PT_SMD=2, PT_EDGE_CONNECTOR=3, PT_NPTH=4
            if pad_type == board_types_pb2.PT_SMD:  # SMD pads on footprint's layer only
                pad_layer = fp_layer_idx
            else:
                pad_layer = -1  # PTH/NPTH on both layers

            # Collect samples for debugging
            if len(sample_pads) < 5 and net_name:
                sample_pads.append({
                    'ref': fp_ref,
                    'pad': str(pad.number),
                    'net': net_name,
                    'position': (pad_x, pad_y),
                    'fp_layer': 'F.Cu' if fp_layer_idx == 0 else 'B.Cu',
                    'pad_type': pad_type,
                    'pad_layer': pad_layer
                })

            if net_name:
                all_pad_positions.append((pad_x, pad_y, net_name, pad_layer))
        except Exception as e:
            debug_info.append(f'{fp_ref}: exception {e}')
            continue

# Compute board bounds from pads
if all_pad_positions:
    min_x = min(p[0] for p in all_pad_positions)
    max_x = max(p[0] for p in all_pad_positions)
    min_y = min(p[1] for p in all_pad_positions)
    max_y = max(p[1] for p in all_pad_positions)
    min_gx = nm_to_grid(min_x) - 20
    min_gy = nm_to_grid(min_y) - 20
    max_gx = nm_to_grid(max_x) + 20
    max_gy = nm_to_grid(max_y) + 20
else:
    min_gx, min_gy, max_gx, max_gy = 0, 0, 1000, 1000

# Mark existing tracks as obstacles
for track in board.get_tracks():
    try:
        layer = 0 if track.layer == BoardLayer.BL_F_Cu else 1
        gx1, gy1 = nm_to_grid(track.start.x), nm_to_grid(track.start.y)
        gx2, gy2 = nm_to_grid(track.end.x), nm_to_grid(track.end.y)
        obstacles.mark_line(layer, gx1, gy1, gx2, gy2, width=3)
    except Exception:
        continue

# Mark existing vias as obstacles on both layers
for via in board.get_vias():
    try:
        gx, gy = nm_to_grid(via.position.x), nm_to_grid(via.position.y)
        obstacles.mark_occupied(0, gx, gy, radius=4)
        obstacles.mark_occupied(1, gx, gy, radius=4)
    except Exception:
        continue

# Build pad_net_map for obstacle checking
# Map grid cells to their net names so we can avoid routing through other nets' pads
pad_net_map = {}  # (gx, gy) -> net_name
pad_grid_cells = {}  # net_name -> set of (gx, gy) cells occupied by that net's pads
for x, y, net_name, pad_layer in all_pad_positions:
    gx, gy = nm_to_grid(x), nm_to_grid(y)
    # Mark pad area (with clearance) - pads are typically larger than a grid cell
    # With 0.15mm grid: typical pad ~0.5mm + 0.2mm clearance = 0.7mm = ~5 cells diameter
    pad_radius = 3  # ~0.9mm diameter around pad center
    for dx in range(-pad_radius, pad_radius + 1):
        for dy in range(-pad_radius, pad_radius + 1):
            cell = (gx + dx, gy + dy)
            pad_net_map[cell] = net_name
            if net_name not in pad_grid_cells:
                pad_grid_cells[net_name] = set()
            pad_grid_cells[net_name].add(cell)

# Build net->pads mapping with layer info
# Each pad is stored as (x, y, layer) where layer is 0=F.Cu, 1=B.Cu, -1=both
net_pads = {}
for x, y, net_name, pad_layer in all_pad_positions:
    if not net_name:
        continue
    if nets_to_route is not None and net_name not in nets_to_route:
        continue

    pos = (x, y, pad_layer)  # Include layer info
    if net_name not in net_pads:
        net_pads[net_name] = []
    net_pads[net_name].append(pos)

# Filter to nets with 2+ pads and sort by estimated routing difficulty
nets_needing_routing = [(k, v) for k, v in net_pads.items() if len(v) >= 2]

# Sort by total manhattan distance (route easier/shorter nets first)
def net_complexity(item):
    name, pads = item
    total_dist = 0
    for i in range(len(pads) - 1):
        total_dist += abs(pads[i][0] - pads[i+1][0]) + abs(pads[i][1] - pads[i+1][1])
    return total_dist

nets_needing_routing.sort(key=net_complexity)

# Route each net
routed_count = 0
failed_count = 0
tracks_added = 0
vias_added = 0
errors = []

layer_map = {0: BoardLayer.BL_F_Cu, 1: BoardLayer.BL_B_Cu}

for net_name, pads in nets_needing_routing:
    try:
        # Connect pads using robust minimum spanning tree approach
        # pads are now (x, y, layer) tuples where layer is 0=F.Cu, 1=B.Cu, -1=both
        connected = {pads[0]}
        unconnected = set(pads[1:])
        failed_pairs = set()  # Track (from, to) pairs that failed
        net_tracks = 0
        net_vias = 0
        max_attempts = len(pads) * 3  # Limit total attempts to avoid infinite loops
        attempts = 0

        while unconnected and attempts < max_attempts:
            attempts += 1

            # Find closest unconnected pad to any connected pad (that we haven't failed on)
            best_dist = float('inf')
            best_from = None
            best_to = None

            for c_pad in connected:
                for u_pad in unconnected:
                    # Skip pairs we've already failed on
                    if (c_pad, u_pad) in failed_pairs:
                        continue
                    dist = abs(c_pad[0] - u_pad[0]) + abs(c_pad[1] - u_pad[1])
                    if dist < best_dist:
                        best_dist = dist
                        best_from = c_pad
                        best_to = u_pad

            if not best_from or not best_to:
                # No more valid pairs to try - all remaining are blocked
                break

            # Validate coordinates before routing
            # pads are now (x, y, layer) tuples
            try:
                from_x, from_y, from_pad_layer = best_from
                to_x, to_y, to_pad_layer = best_to
                # Force integer conversion to catch any type issues
                from_x, from_y = int(from_x), int(from_y)
                to_x, to_y = int(to_x), int(to_y)
            except (TypeError, ValueError) as e:
                errors.append(f'{net_name}: Coordinate type error from={best_from} to={best_to}: {e}')
                failed_pairs.add((best_from, best_to))
                continue

            # Determine start layer for routing based on source pad
            # -1 means pad is on both layers (PTH), so prefer F.Cu
            start_layer = from_pad_layer if from_pad_layer >= 0 else 0

            # Also determine destination layer preference
            dest_layer = to_pad_layer if to_pad_layer >= 0 else start_layer

            # Find path using A* with pad collision checking
            path = None
            try:
                # Try routing on the preferred layers first
                path = astar_route(obstacles, (from_x, from_y), (to_x, to_y),
                                   start_layer=start_layer, current_net=net_name, pad_net_map=pad_net_map)
                if not path and start_layer != dest_layer:
                    # Try starting from destination's preferred layer
                    path = astar_route(obstacles, (from_x, from_y), (to_x, to_y),
                                       start_layer=dest_layer, current_net=net_name, pad_net_map=pad_net_map)
                if not path:
                    # Try the other layer as last resort
                    alt_layer = 1 - start_layer
                    path = astar_route(obstacles, (from_x, from_y), (to_x, to_y),
                                       start_layer=alt_layer, current_net=net_name, pad_net_map=pad_net_map)
            except Exception as e:
                errors.append(f'{net_name}: A* error from=({from_x},{from_y}) to=({to_x},{to_y}): {e}')
                failed_pairs.add((best_from, best_to))
                continue

            if path:
                path = simplify_path(path)

                # Convert path to tracks and vias
                # Build list of track waypoints, inserting vias at layer changes
                # Use exact pad positions for first/last endpoints to ensure KiCad connectivity

                # Collect all waypoints with their layers
                waypoints = []  # List of (x_nm, y_nm, layer)

                # CRITICAL: First waypoint MUST be on pad's layer for connectivity
                # The A* path starts at start_layer, but we override for exact pad layer
                first_path_layer = path[0][2] if path else start_layer
                actual_start_layer = from_pad_layer if from_pad_layer >= 0 else first_path_layer

                # Similarly, last waypoint must be on destination pad's layer
                last_path_layer = path[-1][2] if path else start_layer
                actual_end_layer = to_pad_layer if to_pad_layer >= 0 else last_path_layer

                for i, (gx, gy, layer) in enumerate(path):
                    if i == 0:
                        # First point: use exact source pad position and pad's actual layer
                        waypoints.append((from_x, from_y, actual_start_layer))
                    elif i == len(path) - 1:
                        # Last point: use exact destination pad position and pad's actual layer
                        waypoints.append((to_x, to_y, actual_end_layer))
                    else:
                        # Intermediate point: use grid center
                        waypoints.append((grid_to_nm(gx), grid_to_nm(gy), layer))

                # Create tracks and vias
                # IMPORTANT: Track segments must be created on the correct layers for connectivity
                # When layer changes occur, we need track on BOTH layers meeting at the via
                current_layer = waypoints[0][2]
                for i in range(1, len(waypoints)):
                    prev_x, prev_y, prev_layer = waypoints[i-1]
                    curr_x, curr_y, curr_layer = waypoints[i]

                    # Check for layer change - need via and tracks on both layers
                    if curr_layer != prev_layer:
                        # For layer change: create track on current layer TO the via point,
                        # then via, then track FROM via on new layer

                        # If this is not the first segment, we already have a track ending here
                        # For the first segment (from pad), we may need a short track on pad's layer

                        # Create track from previous position to via position on CURRENT layer
                        # (this ensures connectivity to pad on its layer)
                        if prev_x != curr_x or prev_y != curr_y:
                            # Track on old layer from prev to curr (where via will be)
                            points = [
                                Vector2.from_xy(prev_x, prev_y),
                                Vector2.from_xy(curr_x, curr_y)
                            ]
                            tracks = board.route_track(
                                points=points,
                                width=TRACK_WIDTH,
                                layer=layer_map[current_layer],
                                net=net_name
                            )
                            net_tracks += len(tracks)
                            obstacles.mark_line(current_layer,
                                              nm_to_grid(prev_x), nm_to_grid(prev_y),
                                              nm_to_grid(curr_x), nm_to_grid(curr_y), width=3)

                        # Place via at the current position (where we change layers)
                        via = board.add_via(
                            position=Vector2.from_xy(curr_x, curr_y),
                            diameter=VIA_DIAMETER,
                            drill=VIA_DRILL,
                            net=net_name,
                            via_type=board_types_pb2.ViaType.VT_THROUGH
                        )
                        net_vias += 1
                        obstacles.mark_occupied(0, nm_to_grid(curr_x), nm_to_grid(curr_y), radius=4)
                        obstacles.mark_occupied(1, nm_to_grid(curr_x), nm_to_grid(curr_y), radius=4)
                        current_layer = curr_layer
                        # Via is now at curr position, next segment will start from here on new layer
                    else:
                        # Same layer - just create track segment if positions differ
                        if prev_x != curr_x or prev_y != curr_y:
                            points = [
                                Vector2.from_xy(prev_x, prev_y),
                                Vector2.from_xy(curr_x, curr_y)
                            ]
                            tracks = board.route_track(
                                points=points,
                                width=TRACK_WIDTH,
                                layer=layer_map[current_layer],
                                net=net_name
                            )
                            net_tracks += len(tracks)
                            obstacles.mark_line(current_layer,
                                              nm_to_grid(prev_x), nm_to_grid(prev_y),
                                              nm_to_grid(curr_x), nm_to_grid(curr_y), width=3)

                connected.add(best_to)
                unconnected.remove(best_to)
            else:
                # No path found from this source - mark pair as failed and try other sources
                failed_pairs.add((best_from, best_to))
                # Don't remove from unconnected yet - we might reach it from another pad

        # Report any pads that couldn't be connected
        if unconnected:
            errors.append(f'{net_name}: Could not connect {len(unconnected)} pads after {attempts} attempts')

        if net_tracks > 0:
            tracks_added += net_tracks
            vias_added += net_vias
            routed_count += 1
        else:
            failed_count += 1

    except Exception as e:
        failed_count += 1
        tb = traceback.format_exc().split('\n')
        # Get the last few lines of traceback for context
        tb_short = ' | '.join([l.strip() for l in tb[-4:-1] if l.strip()])
        errors.append(f'{net_name}: {e} @ {tb_short}')

result = {
    'status': 'success' if failed_count == 0 else 'partial',
    'nets_routed': routed_count,
    'nets_failed': failed_count,
    'tracks_added': tracks_added,
    'vias_added': vias_added,
    'total_pads_found': len(all_pad_positions),
    'nets_with_pads': len(net_pads),
    'nets_to_route': len(nets_needing_routing),
    'message': f'Routed {routed_count} nets with {tracks_added} tracks and {vias_added} vias',
    'errors': errors[:10] if errors else [],
    'debug': debug_info[:5] if debug_info else [],
    'sample_pads': sample_pads  # Debug: show pad positions vs footprint positions
}
print(json.dumps(result, indent=2))
)PYTHON";

    return code.str();
}
