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
    "pcb_get_pads",
    "pcb_get_footprint",
    "pcb_route",
    "pcb_get_nets",
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
    code << "import math\n";
    code << "placed_info = []\n";
    code << "for ref in placed:\n";
    code << "    fp = ref_to_fp[ref]\n";
    code << "    fp_layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'\n";
    code << "    fp_x = fp.position.x\n";
    code << "    fp_y = fp.position.y\n";
    code << "    fp_angle_rad = math.radians(fp.orientation.degrees) if hasattr(fp, 'orientation') else 0\n";
    code << "    cos_a = math.cos(fp_angle_rad)\n";
    code << "    sin_a = math.sin(fp_angle_rad)\n";
    code << "    \n";
    code << "    fp_info = {\n";
    code << "        'ref': ref,\n";
    code << "        'position': [fp_x / 1000000, fp_y / 1000000],\n";
    code << "        'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,\n";
    code << "        'layer': fp_layer,\n";
    code << "        'pads': []\n";
    code << "    }\n";
    code << "    # Get pads from footprint definition and calculate absolute positions\n";
    code << "    if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):\n";
    code << "        for pad in fp.definition.pads:\n";
    code << "            rel_x = pad.position.x\n";
    code << "            rel_y = pad.position.y\n";
    code << "            abs_x = fp_x + rel_x * cos_a - rel_y * sin_a\n";
    code << "            abs_y = fp_y + rel_x * sin_a + rel_y * cos_a\n";
    code << "            fp_info['pads'].append({\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [abs_x / 1000000, abs_y / 1000000],\n";
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
    code << "import math\n";
    code << "\n";
    code << "# Find footprint by reference\n";
    code << "target_fp = board.footprints.get_by_reference('" << EscapePythonString( ref ) << "')\n";
    code << "\n";
    code << "if not target_fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( ref ) << "'}))\n";
    code << "else:\n";
    code << "    # Get pads from footprint definition and calculate absolute positions\n";
    code << "    pads = []\n";
    code << "    fp_x = target_fp.position.x\n";
    code << "    fp_y = target_fp.position.y\n";
    code << "    fp_angle_rad = math.radians(target_fp.orientation.degrees) if hasattr(target_fp, 'orientation') else 0\n";
    code << "    cos_a = math.cos(fp_angle_rad)\n";
    code << "    sin_a = math.sin(fp_angle_rad)\n";
    code << "    \n";
    code << "    if hasattr(target_fp, 'definition') and hasattr(target_fp.definition, 'pads'):\n";
    code << "        for pad in target_fp.definition.pads:\n";
    code << "            # Pad position is relative to footprint origin, rotate and translate\n";
    code << "            rel_x = pad.position.x\n";
    code << "            rel_y = pad.position.y\n";
    code << "            abs_x = fp_x + rel_x * cos_a - rel_y * sin_a\n";
    code << "            abs_y = fp_y + rel_x * sin_a + rel_y * cos_a\n";
    code << "            pad_info = {\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [abs_x / 1000000, abs_y / 1000000],\n";
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
    code << "import math\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Find footprint by reference\n";
    code << "fp = board.footprints.get_by_reference('" << EscapePythonString( ref ) << "')\n";
    code << "\n";
    code << "if not fp:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Footprint not found: " << EscapePythonString( ref ) << "'}))\n";
    code << "else:\n";
    code << "    # Get pads from footprint definition and calculate absolute positions\n";
    code << "    pads = []\n";
    code << "    fp_x = fp.position.x\n";
    code << "    fp_y = fp.position.y\n";
    code << "    fp_angle_rad = math.radians(fp.orientation.degrees) if hasattr(fp, 'orientation') else 0\n";
    code << "    cos_a = math.cos(fp_angle_rad)\n";
    code << "    sin_a = math.sin(fp_angle_rad)\n";
    code << "    \n";
    code << "    if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):\n";
    code << "        for pad in fp.definition.pads:\n";
    code << "            # Pad position is relative to footprint origin, rotate and translate\n";
    code << "            rel_x = pad.position.x\n";
    code << "            rel_y = pad.position.y\n";
    code << "            abs_x = fp_x + rel_x * cos_a - rel_y * sin_a\n";
    code << "            abs_y = fp_y + rel_x * sin_a + rel_y * cos_a\n";
    code << "            pad_info = {\n";
    code << "                'number': str(pad.number) if hasattr(pad, 'number') else '',\n";
    code << "                'position': [abs_x / 1000000, abs_y / 1000000],\n";
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
    code << "import math\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "def mm_to_nm(mm):\n";
    code << "    return int(mm * 1000000)\n";
    code << "\n";
    code << "def get_pad_abs_position(fp, pad_num):\n";
    code << "    \"\"\"Get absolute position of a pad by number, accounting for footprint rotation\"\"\"\n";
    code << "    if not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):\n";
    code << "        return None, None\n";
    code << "    fp_x = fp.position.x\n";
    code << "    fp_y = fp.position.y\n";
    code << "    fp_angle_rad = math.radians(fp.orientation.degrees) if hasattr(fp, 'orientation') else 0\n";
    code << "    cos_a = math.cos(fp_angle_rad)\n";
    code << "    sin_a = math.sin(fp_angle_rad)\n";
    code << "    for pad in fp.definition.pads:\n";
    code << "        if str(pad.number) == str(pad_num):\n";
    code << "            rel_x = pad.position.x\n";
    code << "            rel_y = pad.position.y\n";
    code << "            abs_x = fp_x + rel_x * cos_a - rel_y * sin_a\n";
    code << "            abs_y = fp_y + rel_x * sin_a + rel_y * cos_a\n";
    code << "            net = pad.net.name if hasattr(pad, 'net') else None\n";
    code << "            return (abs_x, abs_y), net\n";
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
    code << "# Build net->pads map if needed\n";
    if( includePads )
    {
        code << "net_pads = {}  # net_name -> [{ref, pad}, ...]\n";
        code << "all_fps = board.get_footprints()\n";
        code << "\n";
        code << "# Iterate through footprints and their definition pads to build net->pads map\n";
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
    code << "for net in nets:\n";
    if( !filter.empty() )
    {
        code << "    if not fnmatch.fnmatch(net.name, filter_pattern):\n";
        code << "        continue\n";
    }
    code << "    net_info = {'name': net.name}\n";
    if( includePads )
    {
        code << "    net_info['pads'] = net_pads.get(net.name, [])\n";
        if( unroutedOnly )
        {
            code << "    # Skip nets with 0 or 1 pad (nothing to route)\n";
            code << "    if len(net_info['pads']) < 2:\n";
            code << "        continue\n";
        }
    }
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
