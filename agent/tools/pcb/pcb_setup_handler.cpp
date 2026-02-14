#include "pcb_setup_handler.h"
#include <sstream>


bool PCB_SETUP_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "pcb_setup";
}


std::string PCB_SETUP_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // This tool requires IPC execution - should not be called directly
    return "Error: pcb_setup requires IPC execution. Use GetIPCCommand() instead.";
}


std::string PCB_SETUP_HANDLER::GetDescription( const std::string& aToolName,
                                                const nlohmann::json& aInput ) const
{
    std::string action = aInput.value( "action", "get" );

    if( action == "get" )
    {
        return "Reading PCB board settings";
    }
    else
    {
        // Build description of what's being updated
        std::vector<std::string> sections;

        if( aInput.contains( "stackup" ) )
            sections.push_back( "stackup" );
        if( aInput.contains( "design_rules" ) )
            sections.push_back( "design rules" );
        if( aInput.contains( "text_and_graphics" ) )
            sections.push_back( "text/graphics defaults" );
        if( aInput.contains( "grid" ) )
            sections.push_back( "grid" );
        if( aInput.contains( "drc_severities" ) )
            sections.push_back( "DRC severities" );
        if( aInput.contains( "net_classes" ) )
            sections.push_back( "net classes" );
        if( aInput.contains( "title_block" ) )
            sections.push_back( "title block" );
        if( aInput.contains( "origins" ) )
            sections.push_back( "origins" );

        if( sections.empty() )
            return "Updating PCB settings";

        std::string desc = "Updating ";
        for( size_t i = 0; i < sections.size(); ++i )
        {
            if( i > 0 )
                desc += ( i == sections.size() - 1 ) ? " and " : ", ";
            desc += sections[i];
        }
        return desc;
    }
}


bool PCB_SETUP_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "pcb_setup";
}


std::string PCB_SETUP_HANDLER::GetIPCCommand( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string action = aInput.value( "action", "get" );
    std::string code;

    if( action == "get" )
        code = GenerateGetCode();
    else
        code = GenerateSetCode( aInput );

    return "run_shell pcb " + code;
}


std::string PCB_SETUP_HANDLER::EscapePythonString( const std::string& aStr ) const
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


std::string PCB_SETUP_HANDLER::GenerateGetCode() const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.board import board_pb2, board_commands_pb2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "result = {'status': 'success'}\n";
    code << "\n";

    // Stackup
    code << "# Stackup\n";
    code << "try:\n";
    code << "    stackup = board.layers.get_stackup()\n";
    code << "    result['stackup'] = {\n";
    code << "        'layers': [{\n";
    code << "            'layer': BoardLayer.Name(l.layer) if l.layer else 'dielectric',\n";
    code << "            'thickness_nm': l.thickness,\n";
    code << "            'type': board_pb2.BoardStackupLayerType.Name(l.type),\n";
    code << "            'material': l.material_name,\n";
    code << "            'enabled': l.enabled\n";
    code << "        } for l in stackup.layers]\n";
    code << "    }\n";
    code << "    result['stackup']['copper_layer_count'] = board.layers.get_copper_layer_count()\n";
    code << "except Exception as e:\n";
    code << "    result['stackup'] = {'error': str(e)}\n";
    code << "\n";

    // Design rules
    code << "# Design rules\n";
    code << "try:\n";
    code << "    rules = board.design_rules.get()\n";
    code << "    result['design_rules'] = {\n";
    code << "        'min_clearance_nm': rules.min_clearance,\n";
    code << "        'min_track_width_nm': rules.min_track_width,\n";
    code << "        'min_connection_nm': rules.min_connection,\n";
    code << "        'min_via_diameter_nm': rules.min_via_diameter,\n";
    code << "        'min_via_drill_nm': rules.min_via_drill,\n";
    code << "        'min_via_annular_width_nm': rules.min_via_annular_width,\n";
    code << "        'min_microvia_diameter_nm': rules.min_microvia_diameter,\n";
    code << "        'min_microvia_drill_nm': rules.min_microvia_drill,\n";
    code << "        'min_through_hole_nm': rules.min_through_hole,\n";
    code << "        'min_hole_to_hole_nm': rules.min_hole_to_hole,\n";
    code << "        'hole_to_copper_clearance_nm': rules.hole_to_copper_clearance,\n";
    code << "        'copper_edge_clearance_nm': rules.copper_edge_clearance,\n";
    code << "        'solder_mask_expansion_nm': rules.solder_mask_expansion,\n";
    code << "        'solder_mask_min_width_nm': rules.solder_mask_min_width,\n";
    code << "        'solder_mask_to_copper_clearance_nm': rules.solder_mask_to_copper_clearance,\n";
    code << "        'solder_paste_margin_nm': rules.solder_paste_margin,\n";
    code << "        'solder_paste_margin_ratio': rules.solder_paste_margin_ratio,\n";
    code << "        'min_silk_clearance_nm': rules.min_silk_clearance,\n";
    code << "        'min_silk_text_height_nm': rules.min_silk_text_height,\n";
    code << "        'min_silk_text_thickness_nm': rules.min_silk_text_thickness,\n";
    code << "        'min_resolved_spokes': rules.min_resolved_spokes\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['design_rules'] = {'error': str(e)}\n";
    code << "\n";

    // Text and graphics defaults
    code << "# Text and graphics defaults\n";
    code << "try:\n";
    code << "    defaults = board.layers.get_graphics_defaults()\n";
    code << "    layer_class_names = {\n";
    code << "        board_pb2.BoardLayerClass.BLC_SILKSCREEN: 'silkscreen',\n";
    code << "        board_pb2.BoardLayerClass.BLC_COPPER: 'copper',\n";
    code << "        board_pb2.BoardLayerClass.BLC_EDGES: 'edges',\n";
    code << "        board_pb2.BoardLayerClass.BLC_COURTYARD: 'courtyard',\n";
    code << "        board_pb2.BoardLayerClass.BLC_FABRICATION: 'fabrication',\n";
    code << "        board_pb2.BoardLayerClass.BLC_OTHER: 'other'\n";
    code << "    }\n";
    code << "    result['text_and_graphics'] = {}\n";
    code << "    for layer_class, layer_defaults in defaults.items():\n";
    code << "        class_name = layer_class_names.get(layer_class, str(layer_class))\n";
    code << "        result['text_and_graphics'][class_name] = {\n";
    code << "            'line_thickness_nm': layer_defaults.line_thickness,\n";
    code << "            'text_size_x_nm': layer_defaults.text.size.x,\n";
    code << "            'text_size_y_nm': layer_defaults.text.size.y,\n";
    code << "            'text_thickness_nm': layer_defaults.text.thickness\n";
    code << "        }\n";
    code << "except Exception as e:\n";
    code << "    result['text_and_graphics'] = {'error': str(e)}\n";
    code << "\n";

    // Grid settings
    code << "# Grid settings\n";
    code << "try:\n";
    code << "    grid = board.grid.get_settings()\n";
    code << "    style_names = {\n";
    code << "        board_commands_pb2.GridStyle.GS_DOTS: 'dots',\n";
    code << "        board_commands_pb2.GridStyle.GS_LINES: 'lines',\n";
    code << "        board_commands_pb2.GridStyle.GS_SMALL_CROSS: 'small_cross'\n";
    code << "    }\n";
    code << "    result['grid'] = {\n";
    code << "        'size_x_nm': grid.grid_size_x_nm,\n";
    code << "        'size_y_nm': grid.grid_size_y_nm,\n";
    code << "        'visible': grid.show_grid,\n";
    code << "        'style': style_names.get(grid.style, 'dots')\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['grid'] = {'error': str(e)}\n";
    code << "\n";

    // DRC severities
    code << "# DRC severities\n";
    code << "try:\n";
    code << "    drc_settings = board.drc.get_settings()\n";
    code << "    severity_names = {\n";
    code << "        board_commands_pb2.DrcSeverity.DS_ERROR: 'error',\n";
    code << "        board_commands_pb2.DrcSeverity.DS_WARNING: 'warning',\n";
    code << "        board_commands_pb2.DrcSeverity.DS_IGNORE: 'ignore'\n";
    code << "    }\n";
    code << "    result['drc_severities'] = {\n";
    code << "        cs.check_name: severity_names.get(cs.severity, 'error')\n";
    code << "        for cs in drc_settings.check_severities\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['drc_severities'] = {'error': str(e)}\n";
    code << "\n";

    // Net classes (via project)
    code << "# Net classes (via project)\n";
    code << "try:\n";
    code << "    project = kicad.get_project()\n";
    code << "    net_classes = project.get_net_classes()\n";
    code << "    result['net_classes'] = [{\n";
    code << "        'name': nc.name,\n";
    code << "        'priority': nc.priority,\n";
    code << "        'clearance_nm': nc.clearance,\n";
    code << "        'track_width_nm': nc.track_width,\n";
    code << "        'via_diameter_nm': nc.via_diameter,\n";
    code << "        'via_drill_nm': nc.via_drill,\n";
    code << "        'diff_pair_width_nm': nc.diff_pair_track_width,\n";
    code << "        'diff_pair_gap_nm': nc.diff_pair_gap\n";
    code << "    } for nc in net_classes]\n";
    code << "except Exception as e:\n";
    code << "    result['net_classes'] = {'error': str(e)}\n";
    code << "\n";

    // Title block
    code << "# Title block\n";
    code << "try:\n";
    code << "    tb = board.page.get_title_block()\n";
    code << "    result['title_block'] = {\n";
    code << "        'title': tb.title,\n";
    code << "        'date': tb.date,\n";
    code << "        'revision': tb.revision,\n";
    code << "        'company': tb.company,\n";
    code << "        'comment1': tb.comment1,\n";
    code << "        'comment2': tb.comment2,\n";
    code << "        'comment3': tb.comment3,\n";
    code << "        'comment4': tb.comment4\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['title_block'] = {'error': str(e)}\n";
    code << "\n";

    // Origins
    code << "# Origins\n";
    code << "try:\n";
    code << "    grid_origin = board.page.get_origin(board_commands_pb2.BoardOriginType.BOT_GRID_ORIGIN)\n";
    code << "    drill_origin = board.page.get_origin(board_commands_pb2.BoardOriginType.BOT_DRILL_PLACE_ORIGIN)\n";
    code << "    result['origins'] = {\n";
    code << "        'grid_mm': [grid_origin.x / 1e6, grid_origin.y / 1e6],\n";
    code << "        'drill_mm': [drill_origin.x / 1e6, drill_origin.y / 1e6]\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['origins'] = {'error': str(e)}\n";
    code << "\n";

    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string PCB_SETUP_HANDLER::GenerateSetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.proto.board import board_pb2, board_commands_pb2\n";
    code << "from kipy.proto.common.types import MapMergeMode\n";
    code << "from kipy.project_types import NetClass\n";
    code << "\n";
    code << "result = {'status': 'success', 'updated': []}\n";
    code << "\n";

    // Design rules
    if( aInput.contains( "design_rules" ) )
    {
        const auto& rules = aInput["design_rules"];

        code << "# Update design rules\n";
        code << "try:\n";
        code << "    rules = board.design_rules.get()\n";

        if( rules.contains( "min_clearance_nm" ) )
            code << "    rules.min_clearance = " << rules["min_clearance_nm"].get<int>() << "\n";
        if( rules.contains( "min_track_width_nm" ) )
            code << "    rules.min_track_width = " << rules["min_track_width_nm"].get<int>() << "\n";
        if( rules.contains( "min_connection_nm" ) )
            code << "    rules.min_connection = " << rules["min_connection_nm"].get<int>() << "\n";
        if( rules.contains( "min_via_diameter_nm" ) )
            code << "    rules.min_via_diameter = " << rules["min_via_diameter_nm"].get<int>() << "\n";
        if( rules.contains( "min_via_drill_nm" ) )
            code << "    rules.min_via_drill = " << rules["min_via_drill_nm"].get<int>() << "\n";
        if( rules.contains( "min_via_annular_width_nm" ) )
            code << "    rules.min_via_annular_width = " << rules["min_via_annular_width_nm"].get<int>() << "\n";
        if( rules.contains( "min_microvia_diameter_nm" ) )
            code << "    rules.min_microvia_diameter = " << rules["min_microvia_diameter_nm"].get<int>() << "\n";
        if( rules.contains( "min_microvia_drill_nm" ) )
            code << "    rules.min_microvia_drill = " << rules["min_microvia_drill_nm"].get<int>() << "\n";
        if( rules.contains( "min_through_hole_nm" ) )
            code << "    rules.min_through_hole = " << rules["min_through_hole_nm"].get<int>() << "\n";
        if( rules.contains( "min_hole_to_hole_nm" ) )
            code << "    rules.min_hole_to_hole = " << rules["min_hole_to_hole_nm"].get<int>() << "\n";
        if( rules.contains( "hole_to_copper_clearance_nm" ) )
            code << "    rules.hole_to_copper_clearance = " << rules["hole_to_copper_clearance_nm"].get<int>() << "\n";
        if( rules.contains( "copper_edge_clearance_nm" ) )
            code << "    rules.copper_edge_clearance = " << rules["copper_edge_clearance_nm"].get<int>() << "\n";
        if( rules.contains( "solder_mask_expansion_nm" ) )
            code << "    rules.solder_mask_expansion = " << rules["solder_mask_expansion_nm"].get<int>() << "\n";
        if( rules.contains( "solder_mask_min_width_nm" ) )
            code << "    rules.solder_mask_min_width = " << rules["solder_mask_min_width_nm"].get<int>() << "\n";
        if( rules.contains( "solder_mask_to_copper_clearance_nm" ) )
            code << "    rules.solder_mask_to_copper_clearance = " << rules["solder_mask_to_copper_clearance_nm"].get<int>() << "\n";
        if( rules.contains( "solder_paste_margin_nm" ) )
            code << "    rules.solder_paste_margin = " << rules["solder_paste_margin_nm"].get<int>() << "\n";
        if( rules.contains( "solder_paste_margin_ratio" ) )
            code << "    rules.solder_paste_margin_ratio = " << rules["solder_paste_margin_ratio"].get<double>() << "\n";
        if( rules.contains( "min_silk_clearance_nm" ) )
            code << "    rules.min_silk_clearance = " << rules["min_silk_clearance_nm"].get<int>() << "\n";
        if( rules.contains( "min_silk_text_height_nm" ) )
            code << "    rules.min_silk_text_height = " << rules["min_silk_text_height_nm"].get<int>() << "\n";
        if( rules.contains( "min_silk_text_thickness_nm" ) )
            code << "    rules.min_silk_text_thickness = " << rules["min_silk_text_thickness_nm"].get<int>() << "\n";
        if( rules.contains( "min_resolved_spokes" ) )
            code << "    rules.min_resolved_spokes = " << rules["min_resolved_spokes"].get<int>() << "\n";

        code << "    board.design_rules.set(rules)\n";
        code << "    result['updated'].append('design_rules')\n";
        code << "except Exception as e:\n";
        code << "    result['design_rules_error'] = str(e)\n";
        code << "\n";
    }

    // Grid settings
    if( aInput.contains( "grid" ) )
    {
        const auto& grid = aInput["grid"];

        code << "# Update grid settings\n";
        code << "try:\n";
        code << "    kwargs = {}\n";

        if( grid.contains( "size_x_nm" ) )
            code << "    kwargs['grid_size_x_nm'] = " << grid["size_x_nm"].get<int>() << "\n";
        if( grid.contains( "size_y_nm" ) )
            code << "    kwargs['grid_size_y_nm'] = " << grid["size_y_nm"].get<int>() << "\n";
        if( grid.contains( "visible" ) )
            code << "    kwargs['show_grid'] = " << ( grid["visible"].get<bool>() ? "True" : "False" ) << "\n";
        if( grid.contains( "style" ) )
        {
            std::string style = grid["style"].get<std::string>();
            code << "    style_map = {'dots': board_commands_pb2.GridStyle.GS_DOTS, "
                 << "'lines': board_commands_pb2.GridStyle.GS_LINES, "
                 << "'small_cross': board_commands_pb2.GridStyle.GS_SMALL_CROSS}\n";
            code << "    kwargs['style'] = style_map.get('" << EscapePythonString( style )
                 << "', board_commands_pb2.GridStyle.GS_DOTS)\n";
        }

        code << "    if kwargs:\n";
        code << "        board.grid.set_settings(**kwargs)\n";
        code << "        result['updated'].append('grid')\n";
        code << "except Exception as e:\n";
        code << "    result['grid_error'] = str(e)\n";
        code << "\n";
    }

    // DRC severities
    if( aInput.contains( "drc_severities" ) )
    {
        const auto& drc = aInput["drc_severities"];

        code << "# Update DRC severities\n";
        code << "try:\n";
        code << "    settings = board.drc.get_settings()\n";
        code << "    severity_map = {\n";
        code << "        'error': board_commands_pb2.DrcSeverity.DS_ERROR,\n";
        code << "        'warning': board_commands_pb2.DrcSeverity.DS_WARNING,\n";
        code << "        'ignore': board_commands_pb2.DrcSeverity.DS_IGNORE\n";
        code << "    }\n";

        for( auto it = drc.begin(); it != drc.end(); ++it )
        {
            std::string checkName = it.key();
            std::string severity = it.value().get<std::string>();
            code << "    settings.set_check_severity('" << EscapePythonString( checkName )
                 << "', severity_map['" << EscapePythonString( severity ) << "'])\n";
        }

        code << "    board.drc.set_settings(settings)\n";
        code << "    result['updated'].append('drc_severities')\n";
        code << "except Exception as e:\n";
        code << "    result['drc_severities_error'] = str(e)\n";
        code << "\n";
    }

    // Net classes
    if( aInput.contains( "net_classes" ) )
    {
        const auto& netClasses = aInput["net_classes"];

        code << "# Update net classes\n";
        code << "try:\n";
        code << "    project = kicad.get_project()\n";
        code << "    new_classes = []\n";

        for( const auto& nc : netClasses )
        {
            code << "    nc = NetClass()\n";
            if( nc.contains( "name" ) )
                code << "    nc.name = '" << EscapePythonString( nc["name"].get<std::string>() ) << "'\n";
            if( nc.contains( "priority" ) )
                code << "    nc.priority = " << nc["priority"].get<int>() << "\n";
            if( nc.contains( "clearance_nm" ) )
                code << "    nc.clearance = " << nc["clearance_nm"].get<int>() << "\n";
            if( nc.contains( "track_width_nm" ) )
                code << "    nc.track_width = " << nc["track_width_nm"].get<int>() << "\n";
            if( nc.contains( "via_diameter_nm" ) )
                code << "    nc.via_diameter = " << nc["via_diameter_nm"].get<int>() << "\n";
            if( nc.contains( "via_drill_nm" ) )
                code << "    nc.via_drill = " << nc["via_drill_nm"].get<int>() << "\n";
            if( nc.contains( "diff_pair_width_nm" ) )
                code << "    nc.diff_pair_track_width = " << nc["diff_pair_width_nm"].get<int>() << "\n";
            if( nc.contains( "diff_pair_gap_nm" ) )
                code << "    nc.diff_pair_gap = " << nc["diff_pair_gap_nm"].get<int>() << "\n";
            code << "    new_classes.append(nc)\n";
        }

        code << "    project.set_net_classes(new_classes, merge_mode=MapMergeMode.MMM_MERGE)\n";
        code << "    result['updated'].append('net_classes')\n";
        code << "except Exception as e:\n";
        code << "    result['net_classes_error'] = str(e)\n";
        code << "\n";
    }

    // Title block
    if( aInput.contains( "title_block" ) )
    {
        const auto& tb = aInput["title_block"];

        code << "# Update title block\n";
        code << "try:\n";
        code << "    kwargs = {}\n";

        if( tb.contains( "title" ) )
            code << "    kwargs['title'] = '" << EscapePythonString( tb["title"].get<std::string>() ) << "'\n";
        if( tb.contains( "date" ) )
            code << "    kwargs['date'] = '" << EscapePythonString( tb["date"].get<std::string>() ) << "'\n";
        if( tb.contains( "revision" ) )
            code << "    kwargs['revision'] = '" << EscapePythonString( tb["revision"].get<std::string>() ) << "'\n";
        if( tb.contains( "company" ) )
            code << "    kwargs['company'] = '" << EscapePythonString( tb["company"].get<std::string>() ) << "'\n";

        // Handle comments
        code << "    comments = {}\n";
        for( int i = 1; i <= 9; ++i )
        {
            std::string key = "comment" + std::to_string( i );
            if( tb.contains( key ) )
            {
                code << "    comments[" << i << "] = '"
                     << EscapePythonString( tb[key].get<std::string>() ) << "'\n";
            }
        }
        code << "    if comments:\n";
        code << "        kwargs['comments'] = comments\n";

        code << "    if kwargs:\n";
        code << "        board.page.set_title_block(**kwargs)\n";
        code << "        result['updated'].append('title_block')\n";
        code << "except Exception as e:\n";
        code << "    result['title_block_error'] = str(e)\n";
        code << "\n";
    }

    // Origins
    if( aInput.contains( "origins" ) )
    {
        const auto& origins = aInput["origins"];

        code << "# Update origins\n";
        code << "try:\n";

        if( origins.contains( "grid_mm" ) || origins.contains( "grid" ) )
        {
            // Support both grid_mm and grid keys
            std::string key = origins.contains( "grid_mm" ) ? "grid_mm" : "grid";
            const auto& gridOrigin = origins[key];
            if( gridOrigin.is_array() && gridOrigin.size() >= 2 )
            {
                double x = gridOrigin[0].get<double>();
                double y = gridOrigin[1].get<double>();
                code << "    board.page.set_origin(\n";
                code << "        board_commands_pb2.BoardOriginType.BOT_GRID_ORIGIN,\n";
                code << "        Vector2(int(" << x << " * 1e6), int(" << y << " * 1e6))\n";
                code << "    )\n";
            }
        }

        if( origins.contains( "drill_mm" ) || origins.contains( "drill" ) )
        {
            std::string key = origins.contains( "drill_mm" ) ? "drill_mm" : "drill";
            const auto& drillOrigin = origins[key];
            if( drillOrigin.is_array() && drillOrigin.size() >= 2 )
            {
                double x = drillOrigin[0].get<double>();
                double y = drillOrigin[1].get<double>();
                code << "    board.page.set_origin(\n";
                code << "        board_commands_pb2.BoardOriginType.BOT_DRILL_PLACE_ORIGIN,\n";
                code << "        Vector2(int(" << x << " * 1e6), int(" << y << " * 1e6))\n";
                code << "    )\n";
            }
        }

        code << "    result['updated'].append('origins')\n";
        code << "except Exception as e:\n";
        code << "    result['origins_error'] = str(e)\n";
        code << "\n";
    }

    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}
