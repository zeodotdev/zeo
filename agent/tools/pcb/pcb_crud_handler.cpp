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
    "pcb_run_drc",
    "pcb_set_outline",
    "pcb_sync_schematic",
    "pcb_place",
    "pcb_add",
    "pcb_update",
    "pcb_delete",
    "pcb_batch_delete",
    "pcb_export"
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
        std::string elementType = aInput.value( "element_type", "element" );
        return "Adding " + elementType;
    }
    else if( aToolName == "pcb_update" )
    {
        std::string target = aInput.value( "target", "" );
        if( !target.empty() && target.length() > 8 )
            target = target.substr( 0, 8 ) + "...";
        return "Updating " + ( target.empty() ? "element" : target );
    }
    else if( aToolName == "pcb_delete" )
        return "Deleting element";
    else if( aToolName == "pcb_batch_delete" )
    {
        if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
        {
            size_t count = aInput["targets"].size();
            return "Deleting " + std::to_string( count ) + " element(s)";
        }
        return "Batch deleting elements";
    }
    else if( aToolName == "pcb_export" )
    {
        std::string format = aInput.value( "format", "gerber" );
        return "Exporting " + format;
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
    else if( aToolName == "pcb_run_drc" )
        code = GenerateRunDrcCode( aInput );
    else if( aToolName == "pcb_set_outline" )
        code = GenerateSetOutlineCode( aInput );
    else if( aToolName == "pcb_sync_schematic" )
        code = GenerateSyncSchematicCode( aInput );
    else if( aToolName == "pcb_place" )
        code = GeneratePlaceCode( aInput );
    else if( aToolName == "pcb_add" )
        code = GenerateAddCode( aInput );
    else if( aToolName == "pcb_update" )
        code = GenerateUpdateCode( aInput );
    else if( aToolName == "pcb_delete" )
        code = GenerateDeleteCode( aInput );
    else if( aToolName == "pcb_batch_delete" )
        code = GenerateBatchDeleteCode( aInput );
    else if( aToolName == "pcb_export" )
        code = GenerateExportCode( aInput );

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
    code << "# Get footprints\n";
    code << "footprints = board.get_items(types=['footprint'])\n";
    code << "for fp in footprints:\n";
    code << "    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'\n";
    code << "    pos = fp.position\n";
    code << "    summary['footprints'].append({\n";
    code << "        'ref': ref,\n";
    code << "        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',\n";
    code << "        'position': [pos.x / 1000000, pos.y / 1000000],\n";
    code << "        'layer': 'F.Cu' if not getattr(fp, 'flipped', False) else 'B.Cu'\n";
    code << "    })\n";
    code << "\n";
    code << "# Count tracks, vias, zones\n";
    code << "tracks = board.get_items(types=['track'])\n";
    code << "summary['tracks'] = len(tracks)\n";
    code << "\n";
    code << "vias = board.get_items(types=['via'])\n";
    code << "summary['vias'] = len(vias)\n";
    code << "\n";
    code << "zones = board.get_items(types=['zone'])\n";
    code << "summary['zones'] = len(zones)\n";
    code << "\n";
    code << "# Get nets\n";
    code << "try:\n";
    code << "    nets = board.get_nets()\n";
    code << "    summary['nets'] = [{'name': n.name, 'code': n.code} for n in nets[:50]]  # Limit to 50\n";
    code << "except:\n";
    code << "    pass\n";
    code << "\n";
    code << "# Get layers\n";
    code << "try:\n";
    code << "    layers = board.get_enabled_layers()\n";
    code << "    summary['layers'] = [l.name for l in layers.layers] if hasattr(layers, 'layers') else []\n";
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
    code << "\n";

    if( section == "footprints" )
    {
        code << "# Read footprints\n";
        code << "footprints = board.get_items(types=['footprint'])\n";
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
        code << "    angle = fp.orientation.degrees if hasattr(fp.orientation, 'degrees') else 0\n";
        code << "    result.append({\n";
        code << "        'id': fp.id.value,\n";
        code << "        'ref': ref,\n";
        code << "        'value': fp.value_field.text.value if hasattr(fp, 'value_field') else '',\n";
        code << "        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',\n";
        code << "        'position': [pos.x / 1000000, pos.y / 1000000],\n";
        code << "        'angle': angle,\n";
        code << "        'layer': 'F.Cu' if not getattr(fp, 'flipped', False) else 'B.Cu',\n";
        code << "        'locked': getattr(fp, 'locked', False)\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "tracks" )
    {
        code << "# Read tracks\n";
        code << "tracks = board.get_items(types=['track'])\n";
        code << "result = []\n";
        code << "for t in tracks[:100]:  # Limit to 100\n";
        code << "    result.append({\n";
        code << "        'id': t.id.value,\n";
        code << "        'start': [t.start.x / 1000000, t.start.y / 1000000],\n";
        code << "        'end': [t.end.x / 1000000, t.end.y / 1000000],\n";
        code << "        'width': t.width.value_nm / 1000000 if hasattr(t.width, 'value_nm') else 0,\n";
        code << "        'layer': t.layer.name if hasattr(t, 'layer') else '',\n";
        code << "        'net': t.net.name if hasattr(t, 'net') else ''\n";
        code << "    })\n";
        code << "print(json.dumps({'count': len(tracks), 'tracks': result}, indent=2))\n";
    }
    else if( section == "vias" )
    {
        code << "# Read vias\n";
        code << "vias = board.get_items(types=['via'])\n";
        code << "result = []\n";
        code << "for v in vias:\n";
        code << "    result.append({\n";
        code << "        'id': v.id.value,\n";
        code << "        'position': [v.position.x / 1000000, v.position.y / 1000000],\n";
        code << "        'net': v.net.name if hasattr(v, 'net') else ''\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "zones" )
    {
        code << "# Read zones\n";
        code << "zones = board.get_items(types=['zone'])\n";
        code << "result = []\n";
        code << "for z in zones:\n";
        code << "    result.append({\n";
        code << "        'id': z.id.value,\n";
        code << "        'net': z.net.name if hasattr(z, 'net') else '',\n";
        code << "        'layer': z.layer.name if hasattr(z, 'layer') else '',\n";
        code << "        'priority': getattr(z, 'priority', 0)\n";
        code << "    })\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "nets" )
    {
        code << "# Read nets\n";
        code << "nets = board.get_nets()\n";
        code << "result = [{'name': n.name, 'code': n.code} for n in nets]\n";
        code << "print(json.dumps(result, indent=2))\n";
    }
    else if( section == "layers" )
    {
        code << "# Read layers\n";
        code << "layers = board.get_enabled_layers()\n";
        code << "result = [{'name': l.name} for l in layers.layers] if hasattr(layers, 'layers') else []\n";
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


std::string PCB_CRUD_HANDLER::GenerateRunDrcCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    bool refillZones = aInput.value( "refill_zones", true );
    std::string format = aInput.value( "output_format", "summary" );

    code << "import json\n";
    code << "\n";
    code << "# Run DRC\n";
    code << "try:\n";
    code << "    result = board.run_drc(refill_zones=" << ( refillZones ? "True" : "False" ) << ")\n";
    code << "    \n";

    if( format == "summary" )
    {
        code << "    output = f'DRC Results:\\n'\n";
        code << "    output += f'  Errors: {result.error_count}\\n'\n";
        code << "    output += f'  Warnings: {result.warning_count}\\n'\n";
        code << "    output += f'  Exclusions: {result.exclusion_count}\\n'\n";
        code << "    print(output)\n";
    }
    else if( format == "detailed" )
    {
        code << "    violations = board.get_drc_violations()\n";
        code << "    output = {\n";
        code << "        'error_count': result.error_count,\n";
        code << "        'warning_count': result.warning_count,\n";
        code << "        'violations': []\n";
        code << "    }\n";
        code << "    for v in violations[:50]:  # Limit to 50\n";
        code << "        output['violations'].append({\n";
        code << "            'type': v.error_type,\n";
        code << "            'message': v.message,\n";
        code << "            'severity': v.severity.name if hasattr(v.severity, 'name') else str(v.severity),\n";
        code << "            'position': [v.position.x / 1000000, v.position.y / 1000000] if hasattr(v, 'position') else None\n";
        code << "        })\n";
        code << "    print(json.dumps(output, indent=2))\n";
    }
    else
    {
        code << "    print(json.dumps({'error_count': result.error_count, 'warning_count': result.warning_count}))\n";
    }

    code << "except Exception as e:\n";
    code << "    print(f'DRC Error: {e}')\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateSetOutlineCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string shape = aInput.value( "shape", "rectangle" );
    bool clearExisting = aInput.value( "clear_existing", true );

    code << "from kipy.board import BoardSegment\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "from kipy.proto.common.types.base_types_pb2 import GraphicShape\n";
    code << "\n";

    if( clearExisting )
    {
        code << "# Remove existing board outline\n";
        code << "edge_cuts = board.get_items(types=['line', 'arc', 'rectangle'])\n";
        code << "edge_items = [item for item in edge_cuts if hasattr(item, 'layer') and 'Edge' in str(item.layer)]\n";
        code << "if edge_items:\n";
        code << "    ids = [item.id for item in edge_items]\n";
        code << "    board.delete_items(ids)\n";
        code << "    print(f'Removed {len(ids)} existing outline segment(s)')\n";
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

        code << "# Create rectangular outline\n";
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
        code << "    seg = GraphicShape()\n";
        code << "    seg.segment.start.x_nm = corners[i][0]\n";
        code << "    seg.segment.start.y_nm = corners[i][1]\n";
        code << "    seg.segment.end.x_nm = corners[(i+1) % 4][0]\n";
        code << "    seg.segment.end.y_nm = corners[(i+1) % 4][1]\n";
        code << "    seg.layer = 'Edge.Cuts'\n";
        code << "    segments.append(BoardSegment(proto=seg))\n";
        code << "\n";
        code << "result = board.create_items(segments)\n";
        code << "print(f'Created rectangular outline: {" << width << "}mm x {" << height << "}mm')\n";
    }
    else if( shape == "polygon" )
    {
        if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            code << "# Create polygon outline\n";
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
            code << "    seg = GraphicShape()\n";
            code << "    seg.segment.start.x_nm = points[i][0]\n";
            code << "    seg.segment.start.y_nm = points[i][1]\n";
            code << "    seg.segment.end.x_nm = points[(i+1) % len(points)][0]\n";
            code << "    seg.segment.end.y_nm = points[(i+1) % len(points)][1]\n";
            code << "    seg.layer = 'Edge.Cuts'\n";
            code << "    segments.append(BoardSegment(proto=seg))\n";
            code << "\n";
            code << "result = board.create_items(segments)\n";
            code << "print(f'Created polygon outline with {len(points)} vertices')\n";
        }
        else
        {
            code << "print('Error: polygon shape requires points array')\n";
        }
    }
    else if( shape == "rounded_rectangle" )
    {
        double width = aInput.value( "width", 100.0 );
        double height = aInput.value( "height", 80.0 );
        double radius = aInput.value( "corner_radius", 5.0 );

        code << "# Create rounded rectangle outline\n";
        code << "# Note: Simplified - creates straight segments with corner arcs\n";
        code << "width_nm = " << MmToNm( width ) << "\n";
        code << "height_nm = " << MmToNm( height ) << "\n";
        code << "radius_nm = " << MmToNm( radius ) << "\n";
        code << "\n";
        code << "# For now, create a simple rectangle (arc support needs additional work)\n";
        code << "corners = [\n";
        code << "    (0, 0),\n";
        code << "    (width_nm, 0),\n";
        code << "    (width_nm, height_nm),\n";
        code << "    (0, height_nm)\n";
        code << "]\n";
        code << "segments = []\n";
        code << "for i in range(4):\n";
        code << "    seg = GraphicShape()\n";
        code << "    seg.segment.start.x_nm = corners[i][0]\n";
        code << "    seg.segment.start.y_nm = corners[i][1]\n";
        code << "    seg.segment.end.x_nm = corners[(i+1) % 4][0]\n";
        code << "    seg.segment.end.y_nm = corners[(i+1) % 4][1]\n";
        code << "    seg.layer = 'Edge.Cuts'\n";
        code << "    segments.append(BoardSegment(proto=seg))\n";
        code << "result = board.create_items(segments)\n";
        code << "print(f'Created outline: " << width << "mm x " << height << "mm (corner radius not yet supported)')\n";
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
        code << "print('Error: placements array is required')\n";
        return code.str();
    }

    code << "import json\n";
    code << "\n";
    code << "# Batch footprint placement\n";
    code << "placements = " << aInput["placements"].dump() << "\n";
    code << "\n";
    code << "# Get all footprints and build ref->footprint map\n";
    code << "all_fps = board.get_items(types=['footprint'])\n";
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
    code << "        fp.position.x = int(p['position'][0] * 1000000)\n";
    code << "        fp.position.y = int(p['position'][1] * 1000000)\n";
    code << "        updated = True\n";
    code << "    \n";
    code << "    # Update angle\n";
    code << "    if 'angle' in p:\n";
    code << "        fp.orientation.degrees = p['angle']\n";
    code << "        updated = True\n";
    code << "    \n";
    code << "    # Update layer (flip)\n";
    code << "    if 'layer' in p:\n";
    code << "        fp.flipped = (p['layer'] == 'B.Cu')\n";
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
    code << "result = {'placed': placed, 'not_found': not_found}\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateAddCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string elementType = aInput.value( "element_type", "" );

    code << "import json\n";
    code << "from kipy.board import BoardTrack, BoardVia, BoardZone, BoardSegment, BoardText\n";
    code << "from kipy.proto.board.board_pb2 import Track, Via, Zone\n";
    code << "from kipy.proto.common.types.base_types_pb2 import GraphicShape\n";
    code << "\n";

    if( elementType == "track" )
    {
        std::string layer = aInput.value( "layer", "F.Cu" );
        double width = aInput.value( "width", 0.25 );
        std::string net = aInput.value( "net", "" );

        code << "# Create track(s)\n";
        code << "tracks_to_create = []\n";

        if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
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
            code << "for i in range(len(points) - 1):\n";
            code << "    track = Track()\n";
            code << "    track.start.x_nm = points[i][0]\n";
            code << "    track.start.y_nm = points[i][1]\n";
            code << "    track.end.x_nm = points[i + 1][0]\n";
            code << "    track.end.y_nm = points[i + 1][1]\n";
            code << "    track.width.value_nm = " << MmToNm( width ) << "\n";
            code << "    track.layer.name = '" << EscapePythonString( layer ) << "'\n";
            if( !net.empty() )
            {
                code << "    track.net.name = '" << EscapePythonString( net ) << "'\n";
            }
            code << "    tracks_to_create.append(BoardTrack(proto=track))\n";
        }

        code << "\n";
        code << "if tracks_to_create:\n";
        code << "    result = board.create_items(tracks_to_create)\n";
        code << "    print(f'Created {len(result)} track segment(s)')\n";
        code << "else:\n";
        code << "    print('Error: No track segments to create')\n";
    }
    else if( elementType == "via" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
        }
        double size = aInput.value( "size", 0.8 );
        double drill = aInput.value( "drill", 0.4 );
        std::string net = aInput.value( "net", "" );

        code << "# Create via\n";
        code << "via = Via()\n";
        code << "via.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "via.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "via.padstack.size.x_nm = " << MmToNm( size ) << "\n";
        code << "via.padstack.size.y_nm = " << MmToNm( size ) << "\n";
        code << "via.padstack.drill.diameter_nm = " << MmToNm( drill ) << "\n";
        if( !net.empty() )
        {
            code << "via.net.name = '" << EscapePythonString( net ) << "'\n";
        }
        code << "\n";
        code << "result = board.create_items([BoardVia(proto=via)])\n";
        code << "print(f'Created via at (" << posX << ", " << posY << ")')\n";
    }
    else if( elementType == "zone" )
    {
        std::string layer = aInput.value( "layer", "F.Cu" );
        std::string net = aInput.value( "net", "" );
        int priority = aInput.value( "priority", 0 );

        code << "# Create zone\n";
        code << "zone = Zone()\n";
        code << "zone.layer.name = '" << EscapePythonString( layer ) << "'\n";
        if( !net.empty() )
        {
            code << "zone.net.name = '" << EscapePythonString( net ) << "'\n";
        }
        code << "zone.priority = " << priority << "\n";

        if( aInput.contains( "outline" ) && aInput["outline"].is_array() )
        {
            code << "# Set outline\n";
            code << "outline_points = [\n";
            for( const auto& pt : aInput["outline"] )
            {
                if( pt.is_array() && pt.size() >= 2 )
                {
                    double x = pt[0].get<double>();
                    double y = pt[1].get<double>();
                    code << "    (" << MmToNm( x ) << ", " << MmToNm( y ) << "),\n";
                }
            }
            code << "]\n";
            code << "for pt in outline_points:\n";
            code << "    node = zone.outline.nodes.add()\n";
            code << "    node.x_nm = pt[0]\n";
            code << "    node.y_nm = pt[1]\n";
        }

        code << "\n";
        code << "result = board.create_items([BoardZone(proto=zone)])\n";
        code << "print(f'Created zone on layer " << layer << "')\n";
    }
    else if( elementType == "line" || elementType == "rectangle" || elementType == "circle" ||
             elementType == "arc" )
    {
        std::string layer = aInput.value( "layer", "F.SilkS" );
        double width = aInput.value( "width", 0.15 );

        code << "# Create graphic: " << elementType << "\n";
        code << "shape = GraphicShape()\n";
        code << "shape.layer = '" << EscapePythonString( layer ) << "'\n";
        code << "shape.stroke.width.value_nm = " << MmToNm( width ) << "\n";

        if( elementType == "line" && aInput.contains( "points" ) && aInput["points"].size() >= 2 )
        {
            double x1 = aInput["points"][0][0].get<double>();
            double y1 = aInput["points"][0][1].get<double>();
            double x2 = aInput["points"][1][0].get<double>();
            double y2 = aInput["points"][1][1].get<double>();
            code << "shape.segment.start.x_nm = " << MmToNm( x1 ) << "\n";
            code << "shape.segment.start.y_nm = " << MmToNm( y1 ) << "\n";
            code << "shape.segment.end.x_nm = " << MmToNm( x2 ) << "\n";
            code << "shape.segment.end.y_nm = " << MmToNm( y2 ) << "\n";
        }
        else if( elementType == "rectangle" && aInput.contains( "top_left" ) &&
                 aInput.contains( "bottom_right" ) )
        {
            double x1 = aInput["top_left"][0].get<double>();
            double y1 = aInput["top_left"][1].get<double>();
            double x2 = aInput["bottom_right"][0].get<double>();
            double y2 = aInput["bottom_right"][1].get<double>();
            code << "shape.rectangle.top_left.x_nm = " << MmToNm( x1 ) << "\n";
            code << "shape.rectangle.top_left.y_nm = " << MmToNm( y1 ) << "\n";
            code << "shape.rectangle.bottom_right.x_nm = " << MmToNm( x2 ) << "\n";
            code << "shape.rectangle.bottom_right.y_nm = " << MmToNm( y2 ) << "\n";
        }
        else if( elementType == "circle" && aInput.contains( "center" ) )
        {
            double cx = aInput["center"][0].get<double>();
            double cy = aInput["center"][1].get<double>();
            double radius = aInput.value( "radius", 5.0 );
            code << "shape.circle.center.x_nm = " << MmToNm( cx ) << "\n";
            code << "shape.circle.center.y_nm = " << MmToNm( cy ) << "\n";
            code << "shape.circle.radius_nm = " << MmToNm( radius ) << "\n";
        }

        code << "\n";
        code << "result = board.create_items([BoardSegment(proto=shape)])\n";
        code << "print(f'Created " << elementType << " on " << layer << "')\n";
    }
    else if( elementType == "text" )
    {
        std::string layer = aInput.value( "layer", "F.SilkS" );
        std::string text = aInput.value( "text", "" );
        double textSize = aInput.value( "text_size", 1.0 );
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
        }

        code << "# Create text\n";
        code << "from kipy.proto.board.board_pb2 import BoardText as BoardTextProto\n";
        code << "txt = BoardTextProto()\n";
        code << "txt.text.text = '" << EscapePythonString( text ) << "'\n";
        code << "txt.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "txt.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "txt.text.attributes.size.x_nm = " << MmToNm( textSize ) << "\n";
        code << "txt.text.attributes.size.y_nm = " << MmToNm( textSize ) << "\n";
        code << "txt.layer.name = '" << EscapePythonString( layer ) << "'\n";
        code << "\n";
        code << "result = board.create_items([BoardText(proto=txt)])\n";
        code << "print(f'Created text: " << EscapePythonString( text ) << "')\n";
    }
    else
    {
        code << "print('Error: Unknown element_type: " << EscapePythonString( elementType )
             << "')\n";
    }

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateUpdateCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    if( target.empty() )
    {
        code << "print('Error: target UUID is required')\n";
        return code.str();
    }

    code << "import json\n";
    code << "from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "\n";
    code << "# Find and update element\n";
    code << "target_id = KIID(value='" << EscapePythonString( target ) << "')\n";
    code << "items = board.get_items_by_id([target_id])\n";
    code << "\n";
    code << "if not items:\n";
    code << "    print(f'Error: Element not found: {target_id.value}')\n";
    code << "else:\n";
    code << "    item = items[0]\n";
    code << "    updated = False\n";

    // Position update
    if( aInput.contains( "position" ) && aInput["position"].is_array() &&
        aInput["position"].size() >= 2 )
    {
        double posX = aInput["position"][0].get<double>();
        double posY = aInput["position"][1].get<double>();
        code << "    \n";
        code << "    if hasattr(item, 'position'):\n";
        code << "        item.position.x = " << MmToNm( posX ) << "\n";
        code << "        item.position.y = " << MmToNm( posY ) << "\n";
        code << "        updated = True\n";
    }

    // Net update
    if( aInput.contains( "net" ) )
    {
        std::string net = aInput.value( "net", "" );
        code << "    \n";
        code << "    if hasattr(item, 'net'):\n";
        code << "        item.net.name = '" << EscapePythonString( net ) << "'\n";
        code << "        updated = True\n";
    }

    // Text update
    if( aInput.contains( "text" ) )
    {
        std::string text = aInput.value( "text", "" );
        code << "    \n";
        code << "    if hasattr(item, 'text'):\n";
        code << "        item.text.text = '" << EscapePythonString( text ) << "'\n";
        code << "        updated = True\n";
    }

    // Layer update
    if( aInput.contains( "layer" ) )
    {
        std::string layer = aInput.value( "layer", "" );
        code << "    \n";
        code << "    if hasattr(item, 'layer'):\n";
        code << "        item.layer.name = '" << EscapePythonString( layer ) << "'\n";
        code << "        updated = True\n";
    }

    // Width update
    if( aInput.contains( "width" ) )
    {
        double width = aInput.value( "width", 0.25 );
        code << "    \n";
        code << "    if hasattr(item, 'width'):\n";
        code << "        item.width.value_nm = " << MmToNm( width ) << "\n";
        code << "        updated = True\n";
    }

    // Locked update
    if( aInput.contains( "locked" ) )
    {
        bool locked = aInput.value( "locked", false );
        code << "    \n";
        code << "    if hasattr(item, 'locked'):\n";
        code << "        item.locked = " << ( locked ? "True" : "False" ) << "\n";
        code << "        updated = True\n";
    }

    code << "    \n";
    code << "    if updated:\n";
    code << "        board.update_items([item])\n";
    code << "        print(f'Updated element')\n";
    code << "    else:\n";
    code << "        print('No changes specified')\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    if( target.empty() )
    {
        code << "print('Error: target UUID is required')\n";
        return code.str();
    }

    code << "from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "\n";
    code << "target_id = KIID(value='" << EscapePythonString( target ) << "')\n";
    code << "result = board.delete_items([target_id])\n";
    code << "print(f'Deleted element')\n";

    return code.str();
}


std::string PCB_CRUD_HANDLER::GenerateBatchDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "\n";

    if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
    {
        code << "# Delete by UUIDs\n";
        code << "targets = [";
        for( size_t i = 0; i < aInput["targets"].size(); ++i )
        {
            code << "'" << EscapePythonString( aInput["targets"][i].get<std::string>() ) << "'";
            if( i < aInput["targets"].size() - 1 )
                code << ", ";
        }
        code << "]\n";
        code << "\n";
        code << "ids_to_delete = [KIID(value=t) for t in targets]\n";
        code << "result = board.delete_items(ids_to_delete)\n";
        code << "print(f'Deleted {len(ids_to_delete)} element(s)')\n";
    }
    else if( aInput.contains( "query" ) && aInput["query"].is_object() )
    {
        code << "# Delete by query\n";
        auto query = aInput["query"];
        std::string layer = query.value( "layer", "" );
        std::string type = query.value( "type", "" );
        std::string net = query.value( "net", "" );

        std::string types = "[]";
        if( !type.empty() )
        {
            types = "['" + type + "']";
        }

        code << "# Get items matching query\n";
        code << "items = board.get_items(types=" << types << ")\n";
        code << "ids_to_delete = []\n";
        code << "for item in items:\n";
        if( !layer.empty() )
        {
            code << "    if hasattr(item, 'layer') and item.layer.name != '"
                 << EscapePythonString( layer ) << "':\n";
            code << "        continue\n";
        }
        if( !net.empty() )
        {
            code << "    if hasattr(item, 'net') and item.net.name != '"
                 << EscapePythonString( net ) << "':\n";
            code << "        continue\n";
        }
        code << "    ids_to_delete.append(item.id)\n";
        code << "\n";
        code << "if ids_to_delete:\n";
        code << "    result = board.delete_items(ids_to_delete)\n";
        code << "    print(f'Deleted {len(ids_to_delete)} element(s) matching query')\n";
        code << "else:\n";
        code << "    print('No elements matched query')\n";
    }
    else
    {
        code << "print('Error: Either targets array or query object required')\n";
    }

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
