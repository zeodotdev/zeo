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

        if( aInput.contains( "physical_stackup" ) )
            sections.push_back( "physical stackup" );
        if( aInput.contains( "board_finish" ) )
            sections.push_back( "board finish" );
        if( aInput.contains( "solder_mask_paste" ) )
            sections.push_back( "solder mask/paste" );
        if( aInput.contains( "zone_hatch_offsets" ) )
            sections.push_back( "zone hatch offsets" );
        if( aInput.contains( "board_editor_layers" ) )
            sections.push_back( "board editor layers" );
        if( aInput.contains( "design_rules" ) )
            sections.push_back( "design rules" );
        if( aInput.contains( "text_and_graphics" ) )
            sections.push_back( "text/graphics defaults" );
        if( aInput.contains( "dimension_defaults" ) )
            sections.push_back( "dimension defaults" );
        if( aInput.contains( "zone_defaults" ) )
            sections.push_back( "zone defaults" );
        if( aInput.contains( "predefined_sizes" ) )
            sections.push_back( "predefined sizes" );
        if( aInput.contains( "teardrops" ) )
            sections.push_back( "teardrops" );
        if( aInput.contains( "length_tuning_patterns" ) )
            sections.push_back( "length tuning patterns" );
        if( aInput.contains( "tuning_profiles" ) )
            sections.push_back( "tuning profiles" );
        if( aInput.contains( "component_classes" ) )
            sections.push_back( "component classes" );
        if( aInput.contains( "custom_rules" ) )
            sections.push_back( "custom DRC rules" );
        if( aInput.contains( "grid" ) )
            sections.push_back( "grid" );
        if( aInput.contains( "drc_severities" ) )
            sections.push_back( "DRC severities" );
        if( aInput.contains( "net_classes" ) )
            sections.push_back( "net classes" );
        if( aInput.contains( "text_variables" ) )
            sections.push_back( "text variables" );
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

    // Physical Stackup (full stackup with dielectric properties)
    code << "# Physical Stackup\n";
    code << "try:\n";
    code << "    stackup = board.layers.get_stackup()\n";
    code << "    layers_data = []\n";
    code << "    for l in stackup.layers:\n";
    code << "        layer_info = {\n";
    code << "            'layer': BoardLayer.Name(l.layer) if l.layer else None,\n";
    code << "            'user_name': l.user_name if l.user_name else None,\n";
    code << "            'thickness_nm': l.thickness,\n";
    code << "            'type': board_pb2.BoardStackupLayerType.Name(l.type),\n";
    code << "            'material': l.material_name if l.material_name else None,\n";
    code << "            'enabled': l.enabled\n";
    code << "        }\n";
    code << "        # Add color if set (access proto directly to avoid wrapper issues)\n";
    code << "        c = l._proto.color\n";
    code << "        if c and (c.r or c.g or c.b or c.a):\n";
    code << "            layer_info['color'] = {'r': c.r, 'g': c.g, 'b': c.b, 'a': c.a}\n";
    code << "        # Add dielectric properties if this is a dielectric layer\n";
    code << "        if l.type == board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC:\n";
    code << "            dielectric_props = []\n";
    code << "            for sub in l.dielectric.layers:\n";
    code << "                dielectric_props.append({\n";
    code << "                    'epsilon_r': sub.epsilon_r,\n";
    code << "                    'loss_tangent': sub.loss_tangent,\n";
    code << "                    'material': sub.material_name if sub.material_name else None,\n";
    code << "                    'thickness_nm': sub.thickness\n";
    code << "                })\n";
    code << "            layer_info['dielectric'] = dielectric_props\n";
    code << "        layers_data.append(layer_info)\n";
    code << "    result['physical_stackup'] = {\n";
    code << "        'impedance_controlled': stackup.impedance_controlled,\n";
    code << "        'finish_type': stackup.finish_type if stackup.finish_type else None,\n";
    code << "        'has_edge_plating': stackup.has_edge_plating,\n";
    code << "        'edge_connector': stackup.edge_connector,\n";
    code << "        'copper_layer_count': board.layers.get_copper_layer_count(),\n";
    code << "        'board_thickness_nm': stackup.calculate_board_thickness(),\n";
    code << "        'layers': layers_data\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['physical_stackup'] = {'error': str(e)}\n";
    code << "\n";

    // Board Finish (dedicated section for finish options)
    code << "# Board Finish\n";
    code << "try:\n";
    code << "    stackup = board.layers.get_stackup()\n";
    code << "    result['board_finish'] = {\n";
    code << "        'copper_finish': stackup.finish_type if stackup.finish_type else 'None',\n";
    code << "        'has_plated_edge': stackup.has_edge_plating,\n";
    code << "        'edge_connector': stackup.edge_connector\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['board_finish'] = {'error': str(e)}\n";
    code << "\n";

    // Board editor layers (names, types, enabled status)
    code << "# Board editor layers\n";
    code << "try:\n";
    code << "    layers_info = board.layers.get_layers_info()\n";
    code << "    result['board_editor_layers'] = {\n";
    code << "        'copper_layer_count': board.layers.get_copper_layer_count(),\n";
    code << "        'layers': [{\n";
    code << "            'layer': info['layer_name'],\n";
    code << "            'name': info['name'],\n";
    code << "            'user_name': info['user_name'],\n";
    code << "            'type': info['type'],\n";
    code << "            'enabled': info['enabled'],\n";
    code << "            'visible': info['visible']\n";
    code << "        } for info in layers_info]\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['board_editor_layers'] = {'error': str(e)}\n";
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
    code << "        'min_resolved_spokes': rules.min_resolved_spokes,\n";
    code << "        'max_error_nm': rules._proto.max_error,\n";
    code << "        'allow_external_fillets': rules._proto.allow_external_fillets,\n";
    code << "        'include_stackup_in_length': rules._proto.include_stackup_in_length\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['design_rules'] = {'error': str(e)}\n";
    code << "\n";

    // Solder Mask/Paste (dedicated section for solder mask and paste settings)
    code << "# Solder Mask/Paste\n";
    code << "try:\n";
    code << "    rules = board.design_rules.get()\n";
    code << "    result['solder_mask_paste'] = {\n";
    code << "        'solder_mask_expansion_nm': rules.solder_mask_expansion,\n";
    code << "        'solder_mask_min_width_nm': rules.solder_mask_min_width,\n";
    code << "        'solder_mask_to_copper_clearance_nm': rules.solder_mask_to_copper_clearance,\n";
    code << "        'allow_bridged_apertures': rules._proto.allow_soldermask_bridges_in_fps,\n";
    code << "        'tent_vias_front': rules._proto.tent_vias_front,\n";
    code << "        'tent_vias_back': rules._proto.tent_vias_back,\n";
    code << "        'solder_paste_clearance_nm': rules.solder_paste_margin,\n";
    code << "        'solder_paste_ratio': rules.solder_paste_margin_ratio\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['solder_mask_paste'] = {'error': str(e)}\n";
    code << "\n";

    // Zone hatch offsets
    code << "# Zone hatch offsets\n";
    code << "try:\n";
    code << "    offsets = board.layers.get_zone_hatch_offsets()\n";
    code << "    result['zone_hatch_offsets'] = [{\n";
    code << "        'layer': off['layer_name'],\n";
    code << "        'offset_x_nm': off['offset_x'],\n";
    code << "        'offset_y_nm': off['offset_y']\n";
    code << "    } for off in offsets]\n";
    code << "except Exception as e:\n";
    code << "    result['zone_hatch_offsets'] = {'error': str(e)}\n";
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
    code << "            'text_width_nm': layer_defaults.text.size.x,\n";
    code << "            'text_height_nm': layer_defaults.text.size.y,\n";
    code << "            'text_thickness_nm': layer_defaults.text.stroke_width,\n";
    code << "            'italic': layer_defaults.text.italic,\n";
    code << "            'keep_upright': layer_defaults.text.keep_upright\n";
    code << "        }\n";
    code << "except Exception as e:\n";
    code << "    result['text_and_graphics'] = {'error': str(e)}\n";
    code << "\n";

    // Dimension defaults (via kipy wrapper)
    code << "# Dimension defaults\n";
    code << "try:\n";
    code << "    dim_defaults = board.dimension_defaults.get()\n";
    code << "    result['dimension_defaults'] = dim_defaults.to_dict()\n";
    code << "except Exception as e:\n";
    code << "    result['dimension_defaults'] = {'error': str(e)}\n";
    code << "\n";

    // Zone defaults (via kipy wrapper)
    code << "# Zone defaults\n";
    code << "try:\n";
    code << "    zone_defaults = board.zone_defaults.get()\n";
    code << "    result['zone_defaults'] = zone_defaults.to_dict()\n";
    code << "except Exception as e:\n";
    code << "    result['zone_defaults'] = {'error': str(e)}\n";
    code << "\n";

    // Pre-defined sizes
    code << "# Pre-defined sizes (tracks, vias, diff pairs)\n";
    code << "try:\n";
    code << "    sizes = board.client.send(board_commands_pb2.GetPreDefinedSizes(board=board._doc), board_commands_pb2.PreDefinedSizesResponse)\n";
    code << "    result['predefined_sizes'] = {\n";
    code << "        'tracks': [{'width_nm': w} for w in sizes.sizes.track_widths_nm],\n";
    code << "        'vias': [{'diameter_nm': v.diameter_nm, 'drill_nm': v.drill_nm} for v in sizes.sizes.via_sizes],\n";
    code << "        'diff_pairs': [{'width_nm': d.width_nm, 'gap_nm': d.gap_nm, 'via_gap_nm': d.via_gap_nm} for d in sizes.sizes.diff_pairs]\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['predefined_sizes'] = {'error': str(e)}\n";
    code << "\n";

    // Teardrop settings (via kipy wrapper)
    code << "# Teardrop settings\n";
    code << "try:\n";
    code << "    td = board.teardrops.get()\n";
    code << "    result['teardrops'] = td.to_dict()\n";
    code << "except Exception as e:\n";
    code << "    result['teardrops'] = {'error': str(e)}\n";
    code << "\n";

    // Length-tuning pattern settings (via kipy wrapper)
    code << "# Length-tuning pattern settings\n";
    code << "try:\n";
    code << "    ltp = board.tuning.get_pattern_settings()\n";
    code << "    result['length_tuning_patterns'] = ltp.to_dict()\n";
    code << "except Exception as e:\n";
    code << "    result['length_tuning_patterns'] = {'error': str(e)}\n";
    code << "\n";

    // Tuning profiles (via kipy wrapper)
    code << "# Tuning profiles\n";
    code << "try:\n";
    code << "    profiles = board.tuning.get_profiles()\n";
    code << "    result['tuning_profiles'] = [p.to_dict() for p in profiles]\n";
    code << "except Exception as e:\n";
    code << "    result['tuning_profiles'] = {'error': str(e)}\n";
    code << "\n";

    // Component classes (via kipy wrapper)
    code << "# Component classes\n";
    code << "try:\n";
    code << "    cc = board.component_classes.get()\n";
    code << "    result['component_classes'] = cc.to_dict()\n";
    code << "except Exception as e:\n";
    code << "    result['component_classes'] = {'error': str(e)}\n";
    code << "\n";

    // Custom DRC rules (via kipy wrapper)
    code << "# Custom DRC rules\n";
    code << "try:\n";
    code << "    result['custom_rules'] = board.custom_rules.get()\n";
    code << "except Exception as e:\n";
    code << "    result['custom_rules'] = {'error': str(e)}\n";
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
    code << "        board_commands_pb2.DrcSeverity.DRS_ERROR: 'error',\n";
    code << "        board_commands_pb2.DrcSeverity.DRS_WARNING: 'warning',\n";
    code << "        board_commands_pb2.DrcSeverity.DRS_IGNORE: 'ignore'\n";
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
    code << "    project = board.get_project()\n";
    code << "    net_classes = project.get_net_classes()\n";
    code << "    def nc_to_dict(nc):\n";
    code << "        d = {\n";
    code << "            'name': nc.name,\n";
    code << "            'priority': nc.priority,\n";
    code << "            'clearance_nm': nc.clearance,\n";
    code << "            'track_width_nm': nc.track_width,\n";
    code << "            'via_diameter_nm': nc.via_diameter,\n";
    code << "            'via_drill_nm': nc.via_drill,\n";
    code << "            'microvia_diameter_nm': nc.microvia_diameter,\n";
    code << "            'microvia_drill_nm': nc.microvia_drill,\n";
    code << "            'diff_pair_width_nm': nc.diff_pair_track_width,\n";
    code << "            'diff_pair_gap_nm': nc.diff_pair_gap,\n";
    code << "            'diff_pair_via_gap_nm': nc.diff_pair_via_gap,\n";
    code << "            'tuning_profile': nc.tuning_profile\n";
    code << "        }\n";
    code << "        if nc._proto.HasField('board') and nc._proto.board.HasField('color'):\n";
    code << "            bc = nc._proto.board.color\n";
    code << "            d['pcb_color'] = {'r': bc.r, 'g': bc.g, 'b': bc.b, 'a': bc.a}\n";
    code << "        return d\n";
    code << "    result['net_classes'] = [nc_to_dict(nc) for nc in net_classes]\n";
    code << "except Exception as e:\n";
    code << "    result['net_classes'] = {'error': str(e)}\n";
    code << "\n";

    // Text variables (via project)
    code << "# Text variables (via project)\n";
    code << "try:\n";
    code << "    project = board.get_project()\n";
    code << "    text_vars = project.get_text_variables()\n";
    code << "    result['text_variables'] = dict(text_vars.variables)\n";
    code << "except Exception as e:\n";
    code << "    result['text_variables'] = {'error': str(e)}\n";
    code << "\n";

    // Title block - use kipy wrapper for cleaner error handling
    code << "# Title block\n";
    code << "try:\n";
    code << "    tb = board.page.get_title_block()\n";
    code << "    result['title_block'] = {\n";
    code << "        'title': tb.title,\n";
    code << "        'date': tb.date,\n";
    code << "        'revision': tb.revision,\n";
    code << "        'company': tb.company,\n";
    code << "        'comments': tb.comments\n";
    code << "    }\n";
    code << "except Exception as e:\n";
    code << "    result['title_block'] = {'error': str(e)}\n";
    code << "\n";

    // Origins - use kipy wrapper for cleaner error handling
    code << "# Origins\n";
    code << "try:\n";
    code << "    from kipy.proto.board import board_commands_pb2 as bc\n";
    code << "    grid_origin = board.page.get_origin(bc.BoardOriginType.BOT_GRID)\n";
    code << "    drill_origin = board.page.get_origin(bc.BoardOriginType.BOT_DRILL)\n";
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
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "from kipy.proto.common.types import MapMergeMode\n";
    code << "from kipy.project_types import NetClass\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << "result = {'status': 'success', 'updated': []}\n";
    code << "\n";

    // Board editor layers
    if( aInput.contains( "board_editor_layers" ) )
    {
        const auto& layerSettings = aInput["board_editor_layers"];

        code << "# Update board editor layers\n";
        code << "try:\n";

        // Handle copper layer count change
        if( layerSettings.contains( "copper_layer_count" ) )
        {
            int copperCount = layerSettings["copper_layer_count"].get<int>();
            code << "    # Set copper layer count\n";
            code << "    enabled = board.layers.get_enabled_layers()\n";
            code << "    board.layers.set_enabled_layers(" << copperCount << ", enabled)\n";
        }

        // Handle individual layer updates
        if( layerSettings.contains( "layers" ) && layerSettings["layers"].is_array() )
        {
            code << "    # Layer name to BoardLayer enum mapping\n";
            code << "    layer_map = {\n";
            code << "        'BL_F_Cu': BoardLayer.BL_F_Cu,\n";
            code << "        'BL_B_Cu': BoardLayer.BL_B_Cu,\n";
            code << "        'BL_In1_Cu': BoardLayer.BL_In1_Cu,\n";
            code << "        'BL_In2_Cu': BoardLayer.BL_In2_Cu,\n";
            code << "        'BL_In3_Cu': BoardLayer.BL_In3_Cu,\n";
            code << "        'BL_In4_Cu': BoardLayer.BL_In4_Cu,\n";
            code << "        'BL_In5_Cu': BoardLayer.BL_In5_Cu,\n";
            code << "        'BL_In6_Cu': BoardLayer.BL_In6_Cu,\n";
            code << "        'BL_F_SilkS': BoardLayer.BL_F_SilkS,\n";
            code << "        'BL_B_SilkS': BoardLayer.BL_B_SilkS,\n";
            code << "        'BL_F_Mask': BoardLayer.BL_F_Mask,\n";
            code << "        'BL_B_Mask': BoardLayer.BL_B_Mask,\n";
            code << "        'BL_F_Paste': BoardLayer.BL_F_Paste,\n";
            code << "        'BL_B_Paste': BoardLayer.BL_B_Paste,\n";
            code << "        'BL_F_Adhes': BoardLayer.BL_F_Adhes,\n";
            code << "        'BL_B_Adhes': BoardLayer.BL_B_Adhes,\n";
            code << "        'BL_F_CrtYd': BoardLayer.BL_F_CrtYd,\n";
            code << "        'BL_B_CrtYd': BoardLayer.BL_B_CrtYd,\n";
            code << "        'BL_F_Fab': BoardLayer.BL_F_Fab,\n";
            code << "        'BL_B_Fab': BoardLayer.BL_B_Fab,\n";
            code << "        'BL_Edge_Cuts': BoardLayer.BL_Edge_Cuts,\n";
            code << "        'BL_Margin': BoardLayer.BL_Margin,\n";
            code << "        'BL_Dwgs_User': BoardLayer.BL_Dwgs_User,\n";
            code << "        'BL_Cmts_User': BoardLayer.BL_Cmts_User,\n";
            code << "        'BL_Eco1_User': BoardLayer.BL_Eco1_User,\n";
            code << "        'BL_Eco2_User': BoardLayer.BL_Eco2_User,\n";
            code << "        'BL_User_1': BoardLayer.BL_User_1,\n";
            code << "        'BL_User_2': BoardLayer.BL_User_2,\n";
            code << "        'BL_User_3': BoardLayer.BL_User_3,\n";
            code << "        'BL_User_4': BoardLayer.BL_User_4,\n";
            code << "    }\n";

            for( const auto& layer : layerSettings["layers"] )
            {
                if( !layer.contains( "layer" ) )
                    continue;

                std::string layerName = layer["layer"].get<std::string>();

                // Set layer custom name
                if( layer.contains( "user_name" ) )
                {
                    std::string userName = layer["user_name"].get<std::string>();
                    code << "    if '" << layerName << "' in layer_map:\n";
                    code << "        board.layers.set_layer_name(layer_map['" << layerName << "'], '"
                         << EscapePythonString( userName ) << "')\n";
                }

                // Set layer type (for copper layers)
                if( layer.contains( "type" ) )
                {
                    std::string layerType = layer["type"].get<std::string>();
                    code << "    if '" << layerName << "' in layer_map:\n";
                    code << "        try:\n";
                    code << "            board.layers.set_layer_type(layer_map['" << layerName << "'], '"
                         << EscapePythonString( layerType ) << "')\n";
                    code << "        except ValueError:\n";
                    code << "            pass  # Layer type only applies to copper layers\n";
                }
            }
        }

        code << "    result['updated'].append('board_editor_layers')\n";
        code << "except Exception as e:\n";
        code << "    result['board_editor_layers_error'] = str(e)\n";
        code << "\n";
    }

    // Physical stackup
    if( aInput.contains( "physical_stackup" ) )
    {
        const auto& stackupSettings = aInput["physical_stackup"];

        code << "# Update physical stackup\n";
        code << "try:\n";
        code << "    stackup = board.layers.get_stackup()\n";

        // Update global stackup properties
        if( stackupSettings.contains( "impedance_controlled" ) )
        {
            bool impedanceControlled = stackupSettings["impedance_controlled"].get<bool>();
            code << "    stackup.impedance_controlled = " << ( impedanceControlled ? "True" : "False" ) << "\n";
        }

        if( stackupSettings.contains( "finish_type" ) )
        {
            std::string finishType = stackupSettings["finish_type"].get<std::string>();
            code << "    stackup.finish_type = '" << EscapePythonString( finishType ) << "'\n";
        }

        if( stackupSettings.contains( "has_edge_plating" ) )
        {
            bool hasEdgePlating = stackupSettings["has_edge_plating"].get<bool>();
            code << "    stackup.has_edge_plating = " << ( hasEdgePlating ? "True" : "False" ) << "\n";
        }

        // Update individual layer properties
        if( stackupSettings.contains( "layers" ) && stackupSettings["layers"].is_array() )
        {
            code << "    # Update layer properties in stackup\n";
            code << "    layer_type_map = {\n";
            code << "        'BSLT_COPPER': board_pb2.BoardStackupLayerType.BSLT_COPPER,\n";
            code << "        'BSLT_DIELECTRIC': board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC,\n";
            code << "        'BSLT_SILKSCREEN': board_pb2.BoardStackupLayerType.BSLT_SILKSCREEN,\n";
            code << "        'BSLT_SOLDERMASK': board_pb2.BoardStackupLayerType.BSLT_SOLDERMASK,\n";
            code << "        'BSLT_SOLDERPASTE': board_pb2.BoardStackupLayerType.BSLT_SOLDERPASTE,\n";
            code << "    }\n";
            code << "    for proto_layer in stackup.proto.layers:\n";

            for( const auto& layer : stackupSettings["layers"] )
            {
                if( !layer.contains( "layer" ) && !layer.contains( "type" ) )
                    continue;

                // Match by layer name or by type for dielectric
                if( layer.contains( "layer" ) )
                {
                    std::string layerName = layer["layer"].get<std::string>();
                    code << "        if BoardLayer.Name(proto_layer.layer) == '" << EscapePythonString( layerName ) << "':\n";
                }
                else if( layer.contains( "type" ) && layer["type"].get<std::string>() == "BSLT_DIELECTRIC" )
                {
                    // For dielectric layers, match by index if provided
                    if( layer.contains( "index" ) )
                    {
                        int index = layer["index"].get<int>();
                        code << "        # Match dielectric layer by index\n";
                        code << "        dielectric_idx = 0\n";
                        code << "        for i, pl in enumerate(stackup.proto.layers):\n";
                        code << "            if pl.type == board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC:\n";
                        code << "                if dielectric_idx == " << index << ":\n";
                        code << "                    proto_layer = pl\n";
                        code << "                    break\n";
                        code << "                dielectric_idx += 1\n";
                    }
                    continue;  // Dielectric handled below
                }

                // Set thickness
                if( layer.contains( "thickness_nm" ) )
                {
                    int thickness = layer["thickness_nm"].get<int>();
                    code << "            proto_layer.thickness.value_nm = " << thickness << "\n";
                }

                // Set material
                if( layer.contains( "material" ) )
                {
                    std::string material = layer["material"].get<std::string>();
                    code << "            proto_layer.material_name = '" << EscapePythonString( material ) << "'\n";
                }

                // Set color
                if( layer.contains( "color" ) )
                {
                    const auto& color = layer["color"];
                    if( color.is_object() )
                    {
                        int r = color.value( "r", 0 );
                        int g = color.value( "g", 0 );
                        int b = color.value( "b", 0 );
                        int a = color.value( "a", 255 );
                        code << "            proto_layer.color.red = " << r << "\n";
                        code << "            proto_layer.color.green = " << g << "\n";
                        code << "            proto_layer.color.blue = " << b << "\n";
                        code << "            proto_layer.color.alpha = " << a << "\n";
                    }
                }

                // Set dielectric properties if this is a dielectric layer
                if( layer.contains( "dielectric" ) && layer["dielectric"].is_array() )
                {
                    const auto& dielectricProps = layer["dielectric"];
                    code << "            # Update dielectric sub-layer properties\n";
                    code << "            for i, sub_props in enumerate([";

                    bool first = true;
                    for( const auto& sub : dielectricProps )
                    {
                        if( !first )
                            code << ", ";
                        first = false;

                        code << "{";
                        bool subFirst = true;
                        if( sub.contains( "epsilon_r" ) )
                        {
                            code << "'epsilon_r': " << sub["epsilon_r"].get<double>();
                            subFirst = false;
                        }
                        if( sub.contains( "loss_tangent" ) )
                        {
                            if( !subFirst )
                                code << ", ";
                            code << "'loss_tangent': " << sub["loss_tangent"].get<double>();
                            subFirst = false;
                        }
                        if( sub.contains( "material" ) )
                        {
                            if( !subFirst )
                                code << ", ";
                            code << "'material': '" << EscapePythonString( sub["material"].get<std::string>() ) << "'";
                            subFirst = false;
                        }
                        if( sub.contains( "thickness_nm" ) )
                        {
                            if( !subFirst )
                                code << ", ";
                            code << "'thickness_nm': " << sub["thickness_nm"].get<int>();
                        }
                        code << "}";
                    }

                    code << "]):\n";
                    code << "                if i < len(proto_layer.dielectric.layer):\n";
                    code << "                    if 'epsilon_r' in sub_props:\n";
                    code << "                        proto_layer.dielectric.layer[i].epsilon_r = sub_props['epsilon_r']\n";
                    code << "                    if 'loss_tangent' in sub_props:\n";
                    code << "                        proto_layer.dielectric.layer[i].loss_tangent = sub_props['loss_tangent']\n";
                    code << "                    if 'material' in sub_props:\n";
                    code << "                        proto_layer.dielectric.layer[i].material_name = sub_props['material']\n";
                    code << "                    if 'thickness_nm' in sub_props:\n";
                    code << "                        proto_layer.dielectric.layer[i].thickness.value_nm = sub_props['thickness_nm']\n";
                }
            }
        }

        code << "    # Apply the updated stackup\n";
        code << "    board.layers.update_stackup(stackup)\n";
        code << "    result['updated'].append('physical_stackup')\n";
        code << "except Exception as e:\n";
        code << "    result['physical_stackup_error'] = str(e)\n";
        code << "\n";
    }

    // Board Finish
    if( aInput.contains( "board_finish" ) )
    {
        const auto& finishSettings = aInput["board_finish"];

        code << "# Update board finish\n";
        code << "try:\n";
        code << "    stackup = board.layers.get_stackup()\n";

        if( finishSettings.contains( "copper_finish" ) )
        {
            std::string finish = finishSettings["copper_finish"].get<std::string>();
            code << "    stackup.finish_type = '" << EscapePythonString( finish ) << "'\n";
        }

        if( finishSettings.contains( "has_plated_edge" ) )
        {
            bool hasPlatedEdge = finishSettings["has_plated_edge"].get<bool>();
            code << "    stackup.has_edge_plating = " << ( hasPlatedEdge ? "True" : "False" ) << "\n";
        }

        if( finishSettings.contains( "edge_connector" ) )
        {
            std::string edgeConnector = finishSettings["edge_connector"].get<std::string>();
            code << "    stackup.edge_connector = '" << EscapePythonString( edgeConnector ) << "'\n";
        }

        code << "    board.layers.update_stackup(stackup)\n";
        code << "    result['updated'].append('board_finish')\n";
        code << "except Exception as e:\n";
        code << "    result['board_finish_error'] = str(e)\n";
        code << "\n";
    }

    // Solder Mask/Paste
    if( aInput.contains( "solder_mask_paste" ) )
    {
        const auto& maskPaste = aInput["solder_mask_paste"];

        code << "# Update solder mask/paste settings\n";
        code << "try:\n";
        code << "    rules = board.design_rules.get()\n";

        // Solder mask settings
        if( maskPaste.contains( "solder_mask_expansion_nm" ) )
            code << "    rules.solder_mask_expansion = " << maskPaste["solder_mask_expansion_nm"].get<int>() << "\n";
        if( maskPaste.contains( "solder_mask_min_width_nm" ) )
            code << "    rules.solder_mask_min_width = " << maskPaste["solder_mask_min_width_nm"].get<int>() << "\n";
        if( maskPaste.contains( "solder_mask_to_copper_clearance_nm" ) )
            code << "    rules.solder_mask_to_copper_clearance = " << maskPaste["solder_mask_to_copper_clearance_nm"].get<int>() << "\n";
        if( maskPaste.contains( "allow_bridged_apertures" ) )
            code << "    rules._proto.allow_soldermask_bridges_in_fps = " << ( maskPaste["allow_bridged_apertures"].get<bool>() ? "True" : "False" ) << "\n";
        if( maskPaste.contains( "tent_vias_front" ) )
            code << "    rules._proto.tent_vias_front = " << ( maskPaste["tent_vias_front"].get<bool>() ? "True" : "False" ) << "\n";
        if( maskPaste.contains( "tent_vias_back" ) )
            code << "    rules._proto.tent_vias_back = " << ( maskPaste["tent_vias_back"].get<bool>() ? "True" : "False" ) << "\n";

        // Solder paste settings
        if( maskPaste.contains( "solder_paste_clearance_nm" ) )
            code << "    rules.solder_paste_margin = " << maskPaste["solder_paste_clearance_nm"].get<int>() << "\n";
        if( maskPaste.contains( "solder_paste_ratio" ) )
            code << "    rules.solder_paste_margin_ratio = " << maskPaste["solder_paste_ratio"].get<double>() << "\n";

        code << "    board.design_rules.set(rules)\n";
        code << "    result['updated'].append('solder_mask_paste')\n";
        code << "except Exception as e:\n";
        code << "    result['solder_mask_paste_error'] = str(e)\n";
        code << "\n";
    }

    // Zone hatch offsets
    if( aInput.contains( "zone_hatch_offsets" ) )
    {
        const auto& offsets = aInput["zone_hatch_offsets"];

        code << "# Update zone hatch offsets\n";
        code << "try:\n";
        code << "    layer_offsets = []\n";

        if( offsets.is_array() )
        {
            for( const auto& offset : offsets )
            {
                if( !offset.contains( "layer" ) )
                    continue;

                std::string layer = offset["layer"].get<std::string>();
                int offsetX = offset.value( "offset_x_nm", 0 );
                int offsetY = offset.value( "offset_y_nm", 0 );

                code << "    layer_offsets.append({'layer': '" << EscapePythonString( layer )
                     << "', 'offset_x': " << offsetX << ", 'offset_y': " << offsetY << "})\n";
            }
        }

        code << "    if layer_offsets:\n";
        code << "        board.layers.set_zone_hatch_offsets(layer_offsets)\n";
        code << "    result['updated'].append('zone_hatch_offsets')\n";
        code << "except Exception as e:\n";
        code << "    result['zone_hatch_offsets_error'] = str(e)\n";
        code << "\n";
    }

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
        if( rules.contains( "max_error_nm" ) )
            code << "    rules._proto.max_error = " << rules["max_error_nm"].get<int>() << "\n";
        if( rules.contains( "allow_external_fillets" ) )
            code << "    rules._proto.allow_external_fillets = " << ( rules["allow_external_fillets"].get<bool>() ? "True" : "False" ) << "\n";
        if( rules.contains( "include_stackup_in_length" ) )
            code << "    rules._proto.include_stackup_in_length = " << ( rules["include_stackup_in_length"].get<bool>() ? "True" : "False" ) << "\n";

        code << "    board.design_rules.set(rules)\n";
        code << "    result['updated'].append('design_rules')\n";
        code << "except Exception as e:\n";
        code << "    result['design_rules_error'] = str(e)\n";
        code << "\n";
    }

    // Text and graphics defaults
    if( aInput.contains( "text_and_graphics" ) )
    {
        const auto& textGraphics = aInput["text_and_graphics"];

        code << "# Update text and graphics defaults\n";
        code << "try:\n";
        code << "    from kipy.board.types import BoardLayerGraphicsDefaults\n";
        code << "    from kipy.common_types import TextAttributes\n";
        code << "    layer_name_map = {\n";
        code << "        'silkscreen': board_pb2.BoardLayerClass.BLC_SILKSCREEN,\n";
        code << "        'copper': board_pb2.BoardLayerClass.BLC_COPPER,\n";
        code << "        'edges': board_pb2.BoardLayerClass.BLC_EDGES,\n";
        code << "        'courtyard': board_pb2.BoardLayerClass.BLC_COURTYARD,\n";
        code << "        'fabrication': board_pb2.BoardLayerClass.BLC_FABRICATION,\n";
        code << "        'other': board_pb2.BoardLayerClass.BLC_OTHER\n";
        code << "    }\n";
        code << "    current_defaults = board.layers.get_graphics_defaults()\n";
        code << "    updated_defaults = {}\n";

        for( auto it = textGraphics.begin(); it != textGraphics.end(); ++it )
        {
            std::string layerClass = it.key();
            const auto& layerSettings = it.value();

            code << "    layer_class = layer_name_map.get('" << EscapePythonString( layerClass ) << "')\n";
            code << "    if layer_class is not None and layer_class in current_defaults:\n";
            code << "        defaults_obj = current_defaults[layer_class]\n";

            if( layerSettings.contains( "line_thickness_nm" ) )
            {
                code << "        defaults_obj.line_thickness = " << layerSettings["line_thickness_nm"].get<int>() << "\n";
            }
            // Text settings must use direct proto access because the wrapper
            // creates new objects on property access (changes aren't persisted)
            if( layerSettings.contains( "text_width_nm" ) )
            {
                code << "        defaults_obj._proto.text.size.x_nm = " << layerSettings["text_width_nm"].get<int>() << "\n";
            }
            if( layerSettings.contains( "text_height_nm" ) )
            {
                code << "        defaults_obj._proto.text.size.y_nm = " << layerSettings["text_height_nm"].get<int>() << "\n";
            }
            if( layerSettings.contains( "text_thickness_nm" ) )
            {
                code << "        defaults_obj._proto.text.stroke_width.value_nm = " << layerSettings["text_thickness_nm"].get<int>() << "\n";
            }
            if( layerSettings.contains( "italic" ) )
            {
                code << "        defaults_obj._proto.text.italic = " << ( layerSettings["italic"].get<bool>() ? "True" : "False" ) << "\n";
            }
            if( layerSettings.contains( "keep_upright" ) )
            {
                code << "        defaults_obj._proto.text.keep_upright = " << ( layerSettings["keep_upright"].get<bool>() ? "True" : "False" ) << "\n";
            }

            code << "        updated_defaults[layer_class] = defaults_obj\n";
        }

        code << "    if updated_defaults:\n";
        code << "        board.layers.set_graphics_defaults(updated_defaults)\n";
        code << "    result['updated'].append('text_and_graphics')\n";
        code << "except Exception as e:\n";
        code << "    result['text_and_graphics_error'] = str(e)\n";
        code << "\n";
    }

    // Dimension defaults (via kipy wrapper)
    if( aInput.contains( "dimension_defaults" ) )
    {
        const auto& dims = aInput["dimension_defaults"];

        code << "# Update dimension defaults\n";
        code << "try:\n";
        code << "    from kipy.board.dimension_defaults import DimensionUnitsMode, DimensionUnitsFormat, DimensionPrecision, DimensionTextPosition\n";
        code << "    board.dimension_defaults.set(\n";

        bool firstParam = true;

        if( dims.contains( "units_mode" ) )
        {
            std::string mode = dims["units_mode"].get<std::string>();
            code << "        units_mode=DimensionUnitsMode.from_string('" << EscapePythonString( mode ) << "')";
            firstParam = false;
        }

        if( dims.contains( "units_format" ) )
        {
            std::string fmt = dims["units_format"].get<std::string>();
            if( !firstParam ) code << ",\n";
            code << "        units_format=DimensionUnitsFormat.from_string('" << EscapePythonString( fmt ) << "')";
            firstParam = false;
        }

        if( dims.contains( "precision" ) )
        {
            auto prec = dims["precision"];
            if( !firstParam ) code << ",\n";
            if( prec.is_number_integer() )
                code << "        precision=DimensionPrecision.from_string(" << prec.get<int>() << ")";
            else if( prec.is_string() )
                code << "        precision=DimensionPrecision.from_string('" << EscapePythonString( prec.get<std::string>() ) << "')";
            firstParam = false;
        }

        if( dims.contains( "suppress_zeroes" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        suppress_zeroes=" << ( dims["suppress_zeroes"].get<bool>() ? "True" : "False" );
            firstParam = false;
        }

        if( dims.contains( "text_position" ) )
        {
            std::string pos = dims["text_position"].get<std::string>();
            if( !firstParam ) code << ",\n";
            code << "        text_position=DimensionTextPosition.from_string('" << EscapePythonString( pos ) << "')";
            firstParam = false;
        }

        if( dims.contains( "keep_text_aligned" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        keep_text_aligned=" << ( dims["keep_text_aligned"].get<bool>() ? "True" : "False" );
            firstParam = false;
        }

        if( dims.contains( "arrow_length_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        arrow_length_nm=" << dims["arrow_length_nm"].get<int>();
            firstParam = false;
        }

        if( dims.contains( "extension_offset_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        extension_offset_nm=" << dims["extension_offset_nm"].get<int>();
            firstParam = false;
        }

        code << "\n    )\n";
        code << "    result['updated'].append('dimension_defaults')\n";
        code << "except Exception as e:\n";
        code << "    result['dimension_defaults_error'] = str(e)\n";
        code << "\n";
    }

    // Zone defaults (via kipy wrapper)
    if( aInput.contains( "zone_defaults" ) )
    {
        const auto& zone = aInput["zone_defaults"];

        code << "# Update zone defaults\n";
        code << "try:\n";
        code << "    from kipy.board.zone_defaults import CornerSmoothingMode, ZonePadConnection, ZoneIslandRemoval\n";
        code << "    board.zone_defaults.set(\n";

        bool firstParam = true;

        if( zone.contains( "name" ) )
        {
            code << "        name='" << EscapePythonString( zone["name"].get<std::string>() ) << "'";
            firstParam = false;
        }

        if( zone.contains( "locked" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        locked=" << ( zone["locked"].get<bool>() ? "True" : "False" );
            firstParam = false;
        }

        if( zone.contains( "priority" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        priority=" << zone["priority"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "corner_smoothing" ) )
        {
            std::string smooth = zone["corner_smoothing"].get<std::string>();
            if( !firstParam ) code << ",\n";
            code << "        corner_smoothing=CornerSmoothingMode.from_string('" << EscapePythonString( smooth ) << "')";
            firstParam = false;
        }

        if( zone.contains( "corner_radius_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        corner_radius_nm=" << zone["corner_radius_nm"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "clearance_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        clearance_nm=" << zone["clearance_nm"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "min_thickness_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        min_thickness_nm=" << zone["min_thickness_nm"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "pad_connection" ) )
        {
            std::string conn = zone["pad_connection"].get<std::string>();
            if( !firstParam ) code << ",\n";
            code << "        pad_connection=ZonePadConnection.from_string('" << EscapePythonString( conn ) << "')";
            firstParam = false;
        }

        if( zone.contains( "thermal_gap_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        thermal_gap_nm=" << zone["thermal_gap_nm"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "thermal_spoke_width_nm" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        thermal_spoke_width_nm=" << zone["thermal_spoke_width_nm"].get<int>();
            firstParam = false;
        }

        if( zone.contains( "island_removal" ) )
        {
            std::string removal = zone["island_removal"].get<std::string>();
            if( !firstParam ) code << ",\n";
            code << "        island_removal=ZoneIslandRemoval.from_string('" << EscapePythonString( removal ) << "')";
            firstParam = false;
        }

        if( zone.contains( "min_island_area_nm2" ) )
        {
            if( !firstParam ) code << ",\n";
            code << "        min_island_area_nm2=" << zone["min_island_area_nm2"].get<int64_t>();
            firstParam = false;
        }

        code << "\n    )\n";
        code << "    result['updated'].append('zone_defaults')\n";
        code << "except Exception as e:\n";
        code << "    result['zone_defaults_error'] = str(e)\n";
        code << "\n";
    }

    // Pre-defined sizes - use read-modify-write to preserve unspecified categories
    if( aInput.contains( "predefined_sizes" ) )
    {
        const auto& sizes = aInput["predefined_sizes"];

        code << "# Update pre-defined sizes (read-modify-write to preserve unspecified categories)\n";
        code << "try:\n";
        // First read existing sizes
        code << "    existing = board.client.send(board_commands_pb2.GetPreDefinedSizes(board=board._doc), board_commands_pb2.PreDefinedSizesResponse)\n";
        code << "    cmd = board_commands_pb2.SetPreDefinedSizes()\n";
        code << "    cmd.board.CopyFrom(board._doc)\n";

        // Handle tracks - use new values if provided, otherwise preserve existing
        if( sizes.contains( "tracks" ) && sizes["tracks"].is_array() )
        {
            code << "    # Track widths (new values)\n";
            for( const auto& track : sizes["tracks"] )
            {
                if( track.contains( "width_nm" ) )
                {
                    code << "    cmd.sizes.track_widths_nm.append(" << track["width_nm"].get<int>() << ")\n";
                }
            }
        }
        else
        {
            // Preserve existing tracks
            code << "    # Track widths (preserve existing)\n";
            code << "    for w in existing.sizes.track_widths_nm:\n";
            code << "        cmd.sizes.track_widths_nm.append(w)\n";
        }

        // Handle vias - use new values if provided, otherwise preserve existing
        if( sizes.contains( "vias" ) && sizes["vias"].is_array() )
        {
            code << "    # Via sizes (new values)\n";
            for( const auto& via : sizes["vias"] )
            {
                code << "    via = cmd.sizes.via_sizes.add()\n";
                if( via.contains( "diameter_nm" ) )
                    code << "    via.diameter_nm = " << via["diameter_nm"].get<int>() << "\n";
                if( via.contains( "drill_nm" ) )
                    code << "    via.drill_nm = " << via["drill_nm"].get<int>() << "\n";
            }
        }
        else
        {
            // Preserve existing vias
            code << "    # Via sizes (preserve existing)\n";
            code << "    for v in existing.sizes.via_sizes:\n";
            code << "        via = cmd.sizes.via_sizes.add()\n";
            code << "        via.diameter_nm = v.diameter_nm\n";
            code << "        via.drill_nm = v.drill_nm\n";
        }

        // Handle diff pairs - use new values if provided, otherwise preserve existing
        if( sizes.contains( "diff_pairs" ) && sizes["diff_pairs"].is_array() )
        {
            code << "    # Differential pair sizes (new values)\n";
            for( const auto& dp : sizes["diff_pairs"] )
            {
                code << "    dp = cmd.sizes.diff_pairs.add()\n";
                if( dp.contains( "width_nm" ) )
                    code << "    dp.width_nm = " << dp["width_nm"].get<int>() << "\n";
                if( dp.contains( "gap_nm" ) )
                    code << "    dp.gap_nm = " << dp["gap_nm"].get<int>() << "\n";
                if( dp.contains( "via_gap_nm" ) )
                    code << "    dp.via_gap_nm = " << dp["via_gap_nm"].get<int>() << "\n";
            }
        }
        else
        {
            // Preserve existing diff pairs
            code << "    # Differential pair sizes (preserve existing)\n";
            code << "    for d in existing.sizes.diff_pairs:\n";
            code << "        dp = cmd.sizes.diff_pairs.add()\n";
            code << "        dp.width_nm = d.width_nm\n";
            code << "        dp.gap_nm = d.gap_nm\n";
            code << "        dp.via_gap_nm = d.via_gap_nm\n";
        }

        code << "    board.client.send(cmd, board_commands_pb2.PreDefinedSizesResponse)\n";
        code << "    result['updated'].append('predefined_sizes')\n";
        code << "except Exception as e:\n";
        code << "    result['predefined_sizes_error'] = str(e)\n";
        code << "\n";
    }

    // Teardrop settings (via kipy wrapper)
    if( aInput.contains( "teardrops" ) )
    {
        const auto& td = aInput["teardrops"];

        code << "# Update teardrop settings\n";
        code << "try:\n";
        code << "    from kipy.board.teardrops import TeardropParameters\n";
        code << "    td_kwargs = {}\n";

        // Global flags
        if( td.contains( "target_vias" ) )
            code << "    td_kwargs['target_vias'] = " << ( td["target_vias"].get<bool>() ? "True" : "False" ) << "\n";
        if( td.contains( "target_pth_pads" ) )
            code << "    td_kwargs['target_pth_pads'] = " << ( td["target_pth_pads"].get<bool>() ? "True" : "False" ) << "\n";
        if( td.contains( "target_smd_pads" ) )
            code << "    td_kwargs['target_smd_pads'] = " << ( td["target_smd_pads"].get<bool>() ? "True" : "False" ) << "\n";
        if( td.contains( "target_track_to_track" ) )
            code << "    td_kwargs['target_track_to_track'] = " << ( td["target_track_to_track"].get<bool>() ? "True" : "False" ) << "\n";
        if( td.contains( "round_shapes_only" ) )
            code << "    td_kwargs['round_shapes_only'] = " << ( td["round_shapes_only"].get<bool>() ? "True" : "False" ) << "\n";

        // Helper lambda for generating TeardropParameters
        auto generateTeardropParams = [&]( const std::string& fieldName, const nlohmann::json& params )
        {
            code << "    td_kwargs['" << fieldName << "'] = TeardropParameters(\n";
            bool first = true;
            if( params.contains( "best_length_ratio" ) )
            {
                if( !first ) code << ",\n";
                code << "        best_length_ratio=" << params["best_length_ratio"].get<double>();
                first = false;
            }
            if( params.contains( "max_length_nm" ) )
            {
                if( !first ) code << ",\n";
                code << "        max_length_nm=" << params["max_length_nm"].get<int>();
                first = false;
            }
            if( params.contains( "best_width_ratio" ) )
            {
                if( !first ) code << ",\n";
                code << "        best_width_ratio=" << params["best_width_ratio"].get<double>();
                first = false;
            }
            if( params.contains( "max_width_nm" ) )
            {
                if( !first ) code << ",\n";
                code << "        max_width_nm=" << params["max_width_nm"].get<int>();
                first = false;
            }
            if( params.contains( "curved_edges" ) )
            {
                if( !first ) code << ",\n";
                code << "        curved_edges=" << ( params["curved_edges"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( params.contains( "allow_two_segments" ) )
            {
                if( !first ) code << ",\n";
                code << "        allow_two_segments=" << ( params["allow_two_segments"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( params.contains( "prefer_zone_connection" ) )
            {
                if( !first ) code << ",\n";
                code << "        prefer_zone_connection=" << ( params["prefer_zone_connection"].get<bool>() ? "True" : "False" );
                first = false;
            }
            if( params.contains( "track_width_limit_ratio" ) )
            {
                if( !first ) code << ",\n";
                code << "        track_width_limit_ratio=" << params["track_width_limit_ratio"].get<double>();
                first = false;
            }
            code << "\n    )\n";
        };

        // Per-type parameters
        if( td.contains( "round_shapes" ) && td["round_shapes"].is_object() )
            generateTeardropParams( "round_shapes", td["round_shapes"] );
        if( td.contains( "rect_shapes" ) && td["rect_shapes"].is_object() )
            generateTeardropParams( "rect_shapes", td["rect_shapes"] );
        if( td.contains( "track_to_track" ) && td["track_to_track"].is_object() )
            generateTeardropParams( "track_to_track", td["track_to_track"] );

        code << "    board.teardrops.set(**td_kwargs)\n";
        code << "    result['updated'].append('teardrops')\n";
        code << "except Exception as e:\n";
        code << "    result['teardrops_error'] = str(e)\n";
        code << "\n";
    }

    // Length-tuning pattern settings
    if( aInput.contains( "length_tuning_patterns" ) )
    {
        const auto& ltp = aInput["length_tuning_patterns"];

        code << "# Update length-tuning pattern settings\n";
        code << "try:\n";
        code << "    cmd = board_commands_pb2.SetLengthTuningPatternSettings()\n";
        code << "    cmd.board.CopyFrom(board._doc)\n";

        // Helper lambda for generating meander params code
        auto generateMeanderParams = [&]( const std::string& fieldName, const nlohmann::json& params )
        {
            code << "    # " << fieldName << " parameters\n";
            if( params.contains( "min_amplitude_nm" ) )
                code << "    cmd." << fieldName << ".min_amplitude_nm = " << params["min_amplitude_nm"].get<int>() << "\n";
            if( params.contains( "max_amplitude_nm" ) )
                code << "    cmd." << fieldName << ".max_amplitude_nm = " << params["max_amplitude_nm"].get<int>() << "\n";
            if( params.contains( "spacing_nm" ) )
                code << "    cmd." << fieldName << ".spacing_nm = " << params["spacing_nm"].get<int>() << "\n";
            if( params.contains( "corner_style" ) )
            {
                std::string style = params["corner_style"].get<std::string>();
                code << "    corner_style_map = {'round': board_commands_pb2.MeanderCornerStyle.MCS_ROUND, "
                     << "'chamfer': board_commands_pb2.MeanderCornerStyle.MCS_CHAMFER}\n";
                code << "    cmd." << fieldName << ".corner_style = corner_style_map.get('" << EscapePythonString( style )
                     << "', board_commands_pb2.MeanderCornerStyle.MCS_ROUND)\n";
            }
            if( params.contains( "corner_radius_percent" ) )
                code << "    cmd." << fieldName << ".corner_radius_percent = " << params["corner_radius_percent"].get<int>() << "\n";
            if( params.contains( "single_sided" ) )
                code << "    cmd." << fieldName << ".single_sided = " << ( params["single_sided"].get<bool>() ? "True" : "False" ) << "\n";
        };

        // Per-type parameters
        if( ltp.contains( "single_track" ) && ltp["single_track"].is_object() )
            generateMeanderParams( "single_track", ltp["single_track"] );
        if( ltp.contains( "diff_pair" ) && ltp["diff_pair"].is_object() )
            generateMeanderParams( "diff_pair", ltp["diff_pair"] );
        if( ltp.contains( "diff_pair_skew" ) && ltp["diff_pair_skew"].is_object() )
            generateMeanderParams( "diff_pair_skew", ltp["diff_pair_skew"] );

        code << "    board.client.send(cmd, board_commands_pb2.LengthTuningPatternSettingsResponse)\n";
        code << "    result['updated'].append('length_tuning_patterns')\n";
        code << "except Exception as e:\n";
        code << "    result['length_tuning_patterns_error'] = str(e)\n";
        code << "\n";
    }

    // Tuning profiles
    if( aInput.contains( "tuning_profiles" ) )
    {
        const auto& profiles = aInput["tuning_profiles"];

        code << "# Update tuning profiles\n";
        code << "try:\n";
        code << "    cmd = board_commands_pb2.SetTuningProfiles()\n";
        code << "    cmd.board.CopyFrom(board._doc)\n";

        if( profiles.is_array() )
        {
            for( const auto& profile : profiles )
            {
                code << "    profile = cmd.profiles.profiles.add()\n";

                if( profile.contains( "name" ) )
                    code << "    profile.name = '" << EscapePythonString( profile["name"].get<std::string>() ) << "'\n";

                if( profile.contains( "type" ) )
                {
                    std::string type = profile["type"].get<std::string>();
                    code << "    type_map = {'single': board_commands_pb2.TuningProfileType.TPT_SINGLE, "
                         << "'differential': board_commands_pb2.TuningProfileType.TPT_DIFFERENTIAL}\n";
                    code << "    profile.type = type_map.get('" << EscapePythonString( type )
                         << "', board_commands_pb2.TuningProfileType.TPT_SINGLE)\n";
                }

                if( profile.contains( "target_impedance_ohms" ) )
                    code << "    profile.target_impedance_ohms = " << profile["target_impedance_ohms"].get<double>() << "\n";

                if( profile.contains( "enable_time_domain_tuning" ) )
                    code << "    profile.enable_time_domain_tuning = " << ( profile["enable_time_domain_tuning"].get<bool>() ? "True" : "False" ) << "\n";

                if( profile.contains( "via_propagation_delay_ps" ) )
                    code << "    profile.via_propagation_delay_ps = " << profile["via_propagation_delay_ps"].get<int>() << "\n";

                // Track entries
                if( profile.contains( "track_entries" ) && profile["track_entries"].is_array() )
                {
                    for( const auto& entry : profile["track_entries"] )
                    {
                        code << "    track_entry = profile.track_entries.add()\n";

                        if( entry.contains( "signal_layer" ) )
                            code << "    track_entry.signal_layer.layer_id = " << entry["signal_layer"].get<int>() << "\n";
                        if( entry.contains( "top_reference_layer" ) )
                            code << "    track_entry.top_reference_layer.layer_id = " << entry["top_reference_layer"].get<int>() << "\n";
                        if( entry.contains( "bottom_reference_layer" ) )
                            code << "    track_entry.bottom_reference_layer.layer_id = " << entry["bottom_reference_layer"].get<int>() << "\n";
                        if( entry.contains( "width_nm" ) )
                            code << "    track_entry.width_nm = " << entry["width_nm"].get<int64_t>() << "\n";
                        if( entry.contains( "diff_pair_gap_nm" ) )
                            code << "    track_entry.diff_pair_gap_nm = " << entry["diff_pair_gap_nm"].get<int64_t>() << "\n";
                        if( entry.contains( "delay_ps_per_mm" ) )
                            code << "    track_entry.delay_ps_per_mm = " << entry["delay_ps_per_mm"].get<int>() << "\n";
                        if( entry.contains( "enable_time_domain" ) )
                            code << "    track_entry.enable_time_domain = " << ( entry["enable_time_domain"].get<bool>() ? "True" : "False" ) << "\n";
                    }
                }

                // Via overrides
                if( profile.contains( "via_overrides" ) && profile["via_overrides"].is_array() )
                {
                    for( const auto& override : profile["via_overrides"] )
                    {
                        code << "    via_override = profile.via_overrides.add()\n";

                        if( override.contains( "signal_layer_from" ) )
                            code << "    via_override.signal_layer_from.layer_id = " << override["signal_layer_from"].get<int>() << "\n";
                        if( override.contains( "signal_layer_to" ) )
                            code << "    via_override.signal_layer_to.layer_id = " << override["signal_layer_to"].get<int>() << "\n";
                        if( override.contains( "via_layer_from" ) )
                            code << "    via_override.via_layer_from.layer_id = " << override["via_layer_from"].get<int>() << "\n";
                        if( override.contains( "via_layer_to" ) )
                            code << "    via_override.via_layer_to.layer_id = " << override["via_layer_to"].get<int>() << "\n";
                        if( override.contains( "delay_ps" ) )
                            code << "    via_override.delay_ps = " << override["delay_ps"].get<int>() << "\n";
                    }
                }
            }
        }

        code << "    board.client.send(cmd, board_commands_pb2.TuningProfilesResponse)\n";
        code << "    result['updated'].append('tuning_profiles')\n";
        code << "except Exception as e:\n";
        code << "    result['tuning_profiles_error'] = str(e)\n";
        code << "\n";
    }

    // Component classes
    if( aInput.contains( "component_classes" ) )
    {
        const auto& cc = aInput["component_classes"];

        code << "# Update component class settings\n";
        code << "try:\n";
        code << "    cmd = board_commands_pb2.SetComponentClassSettings()\n";
        code << "    cmd.board.CopyFrom(board._doc)\n";

        if( cc.contains( "enable_sheet_component_classes" ) )
            code << "    cmd.settings.enable_sheet_component_classes = "
                 << ( cc["enable_sheet_component_classes"].get<bool>() ? "True" : "False" ) << "\n";

        if( cc.contains( "assignments" ) && cc["assignments"].is_array() )
        {
            for( const auto& assignment : cc["assignments"] )
            {
                code << "    assignment = cmd.settings.assignments.add()\n";

                if( assignment.contains( "component_class" ) )
                    code << "    assignment.component_class = '"
                         << EscapePythonString( assignment["component_class"].get<std::string>() ) << "'\n";

                if( assignment.contains( "operator" ) )
                {
                    std::string op = assignment["operator"].get<std::string>();
                    code << "    operator_map = {'all': board_commands_pb2.ComponentClassConditionsOperator.CCCO_ALL, "
                         << "'any': board_commands_pb2.ComponentClassConditionsOperator.CCCO_ANY}\n";
                    code << "    assignment.operator = operator_map.get('" << EscapePythonString( op )
                         << "', board_commands_pb2.ComponentClassConditionsOperator.CCCO_ALL)\n";
                }

                if( assignment.contains( "conditions" ) && assignment["conditions"].is_array() )
                {
                    for( const auto& cond : assignment["conditions"] )
                    {
                        code << "    condition = assignment.conditions.add()\n";

                        if( cond.contains( "type" ) )
                        {
                            std::string type = cond["type"].get<std::string>();
                            code << "    type_map = {\n";
                            code << "        'reference': board_commands_pb2.ComponentClassConditionType.CCCT_REFERENCE,\n";
                            code << "        'footprint': board_commands_pb2.ComponentClassConditionType.CCCT_FOOTPRINT,\n";
                            code << "        'side': board_commands_pb2.ComponentClassConditionType.CCCT_SIDE,\n";
                            code << "        'rotation': board_commands_pb2.ComponentClassConditionType.CCCT_ROTATION,\n";
                            code << "        'footprint_field': board_commands_pb2.ComponentClassConditionType.CCCT_FOOTPRINT_FIELD,\n";
                            code << "        'custom': board_commands_pb2.ComponentClassConditionType.CCCT_CUSTOM,\n";
                            code << "        'sheet_name': board_commands_pb2.ComponentClassConditionType.CCCT_SHEET_NAME\n";
                            code << "    }\n";
                            code << "    condition.type = type_map.get('" << EscapePythonString( type )
                                 << "', board_commands_pb2.ComponentClassConditionType.CCCT_REFERENCE)\n";
                        }

                        if( cond.contains( "primary_data" ) )
                            code << "    condition.primary_data = '"
                                 << EscapePythonString( cond["primary_data"].get<std::string>() ) << "'\n";
                        if( cond.contains( "secondary_data" ) )
                            code << "    condition.secondary_data = '"
                                 << EscapePythonString( cond["secondary_data"].get<std::string>() ) << "'\n";
                    }
                }
            }
        }

        code << "    board.client.send(cmd, board_commands_pb2.ComponentClassSettingsResponse)\n";
        code << "    result['updated'].append('component_classes')\n";
        code << "except Exception as e:\n";
        code << "    result['component_classes_error'] = str(e)\n";
        code << "\n";
    }

    // Custom DRC rules (via kipy wrapper)
    if( aInput.contains( "custom_rules" ) )
    {
        const auto& customRules = aInput["custom_rules"];
        std::string rulesText;

        // Handle various input formats
        if( customRules.is_string() )
        {
            rulesText = customRules.get<std::string>();
        }
        else if( customRules.is_object() && customRules.contains( "rules_text" ) )
        {
            const auto& rulesField = customRules["rules_text"];
            if( rulesField.is_string() )
                rulesText = rulesField.get<std::string>();
            // If rules_text is not a string, leave rulesText empty
        }
        // If customRules is neither string nor object with rules_text, leave rulesText empty

        code << "# Update custom DRC rules\n";
        code << "try:\n";
        code << "    board.custom_rules.set('''" << rulesText << "''')\n";
        code << "    result['updated'].append('custom_rules')\n";
        code << "except Exception as e:\n";
        code << "    result['custom_rules_error'] = str(e)\n";
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
        code << "        'error': board_commands_pb2.DrcSeverity.DRS_ERROR,\n";
        code << "        'warning': board_commands_pb2.DrcSeverity.DRS_WARNING,\n";
        code << "        'ignore': board_commands_pb2.DrcSeverity.DRS_IGNORE\n";
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
        code << "    from kipy.common_types import Color\n";
        code << "    project = board.get_project()\n";
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
            if( nc.contains( "microvia_diameter_nm" ) )
                code << "    nc.microvia_diameter = " << nc["microvia_diameter_nm"].get<int>() << "\n";
            if( nc.contains( "microvia_drill_nm" ) )
                code << "    nc.microvia_drill = " << nc["microvia_drill_nm"].get<int>() << "\n";
            if( nc.contains( "diff_pair_width_nm" ) )
                code << "    nc.diff_pair_track_width = " << nc["diff_pair_width_nm"].get<int>() << "\n";
            if( nc.contains( "diff_pair_gap_nm" ) )
                code << "    nc.diff_pair_gap = " << nc["diff_pair_gap_nm"].get<int>() << "\n";
            if( nc.contains( "diff_pair_via_gap_nm" ) )
                code << "    nc.diff_pair_via_gap = " << nc["diff_pair_via_gap_nm"].get<int>() << "\n";
            if( nc.contains( "tuning_profile" ) )
                code << "    nc.tuning_profile = '" << EscapePythonString( nc["tuning_profile"].get<std::string>() ) << "'\n";
            if( nc.contains( "pcb_color" ) && nc["pcb_color"].is_object() )
            {
                const auto& color = nc["pcb_color"];
                double r = color.contains( "r" ) ? color["r"].get<double>() : 0.0;
                double g = color.contains( "g" ) ? color["g"].get<double>() : 0.0;
                double b = color.contains( "b" ) ? color["b"].get<double>() : 0.0;
                double a = color.contains( "a" ) ? color["a"].get<double>() : 1.0;
                code << "    _color = Color()\n";
                code << "    _color.red = " << r << "\n";
                code << "    _color.green = " << g << "\n";
                code << "    _color.blue = " << b << "\n";
                code << "    _color.alpha = " << a << "\n";
                code << "    nc.board_color = _color\n";
            }
            code << "    new_classes.append(nc)\n";
        }

        code << "    project.set_net_classes(new_classes, merge_mode=MapMergeMode.MMM_REPLACE)\n";
        code << "    result['updated'].append('net_classes')\n";
        code << "except Exception as e:\n";
        code << "    result['net_classes_error'] = str(e)\n";
        code << "\n";
    }

    // Text variables
    if( aInput.contains( "text_variables" ) )
    {
        const auto& textVars = aInput["text_variables"];

        code << "# Update text variables\n";
        code << "try:\n";
        code << "    from kipy.project_types import TextVariables\n";
        code << "    project = board.get_project()\n";
        code << "    tv = TextVariables()\n";
        code << "    tv.variables = {\n";

        bool first = true;
        for( auto it = textVars.begin(); it != textVars.end(); ++it )
        {
            if( !first )
                code << ",\n";
            first = false;

            code << "        '" << EscapePythonString( it.key() ) << "': '"
                 << EscapePythonString( it.value().get<std::string>() ) << "'";
        }

        code << "\n    }\n";
        code << "    project.set_text_variables(tv, merge_mode=MapMergeMode.MMM_MERGE)\n";
        code << "    result['updated'].append('text_variables')\n";
        code << "except Exception as e:\n";
        code << "    result['text_variables_error'] = str(e)\n";
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

        // Handle comments - support both formats:
        // 1. "comments": {"1": "...", "3": "..."} (nested object)
        // 2. "comment1": "...", "comment3": "..." (individual keys)
        code << "    comments = {}\n";
        if( tb.contains( "comments" ) && tb["comments"].is_object() )
        {
            // Handle nested comments object format
            for( auto& [key, value] : tb["comments"].items() )
            {
                if( value.is_string() )
                {
                    // Convert string key to int for Python dict
                    code << "    comments[" << key << "] = '"
                         << EscapePythonString( value.get<std::string>() ) << "'\n";
                }
            }
        }
        else
        {
            // Handle individual comment1, comment2, etc. keys
            for( int i = 1; i <= 9; ++i )
            {
                std::string key = "comment" + std::to_string( i );
                if( tb.contains( key ) )
                {
                    code << "    comments[" << i << "] = '"
                         << EscapePythonString( tb[key].get<std::string>() ) << "'\n";
                }
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
                code << "        board_commands_pb2.BoardOriginType.BOT_GRID,\n";
                code << "        Vector2.from_xy(int(" << x << " * 1e6), int(" << y << " * 1e6))\n";
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
                code << "        board_commands_pb2.BoardOriginType.BOT_DRILL,\n";
                code << "        Vector2.from_xy(int(" << x << " * 1e6), int(" << y << " * 1e6))\n";
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
