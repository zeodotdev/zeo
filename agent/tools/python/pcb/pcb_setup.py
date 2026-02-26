import json
from kipy.proto.board import board_pb2, board_commands_pb2
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.proto.common.types import MapMergeMode
from kipy.project_types import NetClass
from kipy.geometry import Vector2


def _get():
    result = {'status': 'success'}

    # Physical Stackup
    try:
        stackup = board.layers.get_stackup()
        layers_data = []
        for idx, l in enumerate(stackup.layers):
            layer_info = {
                'index': idx,
                'layer': BoardLayer.Name(l.layer) if l.layer else None,
                'user_name': l.user_name if l.user_name else None,
                'thickness_nm': l.thickness,
                'type': board_pb2.BoardStackupLayerType.Name(l.type),
                'material': l.material_name if l.material_name else None,
                'enabled': l.enabled
            }
            # Add color if set (access proto directly to avoid wrapper issues)
            c = l._proto.color
            if c and (c.r or c.g or c.b or c.a):
                layer_info['color'] = {'r': c.r, 'g': c.g, 'b': c.b, 'a': c.a}
            # Add dielectric properties if this is a dielectric layer
            if l.type == board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC:
                dielectric_props = []
                for sub in l.dielectric.layers:
                    dielectric_props.append({
                        'epsilon_r': sub.epsilon_r,
                        'loss_tangent': sub.loss_tangent,
                        'material': sub.material_name if sub.material_name else None,
                        'thickness_nm': sub.thickness
                    })
                layer_info['dielectric'] = dielectric_props
            layers_data.append(layer_info)
        result['physical_stackup'] = {
            'impedance_controlled': stackup.impedance_controlled,
            'finish_type': stackup.finish_type if stackup.finish_type else None,
            'has_edge_plating': stackup.has_edge_plating,
            'edge_connector': stackup.edge_connector,
            'copper_layer_count': board.layers.get_copper_layer_count(),
            'board_thickness_nm': stackup.calculate_board_thickness(),
            'layers': layers_data
        }
    except Exception as e:
        result['physical_stackup'] = {'error': str(e)}

    # Board Finish
    try:
        stackup = board.layers.get_stackup()
        result['board_finish'] = {
            'copper_finish': stackup.finish_type if stackup.finish_type else 'None',
            'has_plated_edge': stackup.has_edge_plating,
            'edge_connector': stackup.edge_connector
        }
    except Exception as e:
        result['board_finish'] = {'error': str(e)}

    # Board editor layers
    try:
        layers_info = board.layers.get_layers_info()
        result['board_editor_layers'] = {
            'copper_layer_count': board.layers.get_copper_layer_count(),
            'layers': [{
                'layer': info['layer_name'],
                'name': info['name'],
                'user_name': info['user_name'],
                'type': info['type'],
                'enabled': info['enabled'],
                'visible': info['visible']
            } for info in layers_info]
        }
    except Exception as e:
        result['board_editor_layers'] = {'error': str(e)}

    # Design rules
    try:
        rules = board.design_rules.get()
        result['design_rules'] = {
            'min_clearance_nm': rules.min_clearance,
            'min_track_width_nm': rules.min_track_width,
            'min_connection_nm': rules.min_connection,
            'min_via_diameter_nm': rules.min_via_diameter,
            'min_via_drill_nm': rules.min_via_drill,
            'min_via_annular_width_nm': rules.min_via_annular_width,
            'min_microvia_diameter_nm': rules.min_microvia_diameter,
            'min_microvia_drill_nm': rules.min_microvia_drill,
            'min_through_hole_nm': rules.min_through_hole,
            'min_hole_to_hole_nm': rules.min_hole_to_hole,
            'hole_to_copper_clearance_nm': rules.hole_to_copper_clearance,
            'copper_edge_clearance_nm': rules.copper_edge_clearance,
            'solder_mask_expansion_nm': rules.solder_mask_expansion,
            'solder_mask_min_width_nm': rules.solder_mask_min_width,
            'solder_mask_to_copper_clearance_nm': rules.solder_mask_to_copper_clearance,
            'solder_paste_margin_nm': rules.solder_paste_margin,
            'solder_paste_margin_ratio': rules.solder_paste_margin_ratio,
            'min_silk_clearance_nm': rules.min_silk_clearance,
            'min_silk_text_height_nm': rules.min_silk_text_height,
            'min_silk_text_thickness_nm': rules.min_silk_text_thickness,
            'min_resolved_spokes': rules.min_resolved_spokes,
            'max_error_nm': rules._proto.max_error,
            'allow_external_fillets': rules._proto.allow_external_fillets,
            'include_stackup_in_length': rules._proto.include_stackup_in_length
        }
    except Exception as e:
        result['design_rules'] = {'error': str(e)}

    # Solder Mask/Paste
    try:
        rules = board.design_rules.get()
        result['solder_mask_paste'] = {
            'solder_mask_expansion_nm': rules.solder_mask_expansion,
            'solder_mask_min_width_nm': rules.solder_mask_min_width,
            'solder_mask_to_copper_clearance_nm': rules.solder_mask_to_copper_clearance,
            'allow_bridged_apertures': rules._proto.allow_soldermask_bridges_in_fps,
            'tent_vias_front': rules._proto.tent_vias_front,
            'tent_vias_back': rules._proto.tent_vias_back,
            'solder_paste_clearance_nm': rules.solder_paste_margin,
            'solder_paste_ratio': rules.solder_paste_margin_ratio
        }
    except Exception as e:
        result['solder_mask_paste'] = {'error': str(e)}

    # Zone hatch offsets
    try:
        offsets = board.layers.get_zone_hatch_offsets()
        result['zone_hatch_offsets'] = [{
            'layer': off['layer_name'],
            'offset_x_nm': off['offset_x'],
            'offset_y_nm': off['offset_y']
        } for off in offsets]
    except Exception as e:
        result['zone_hatch_offsets'] = {'error': str(e)}

    # Text and graphics defaults
    try:
        defaults = board.layers.get_graphics_defaults()
        layer_class_names = {
            board_pb2.BoardLayerClass.BLC_SILKSCREEN: 'silkscreen',
            board_pb2.BoardLayerClass.BLC_COPPER: 'copper',
            board_pb2.BoardLayerClass.BLC_EDGES: 'edges',
            board_pb2.BoardLayerClass.BLC_COURTYARD: 'courtyard',
            board_pb2.BoardLayerClass.BLC_FABRICATION: 'fabrication',
            board_pb2.BoardLayerClass.BLC_OTHER: 'other'
        }
        result['text_and_graphics'] = {}
        for layer_class, layer_defaults in defaults.items():
            class_name = layer_class_names.get(layer_class, str(layer_class))
            result['text_and_graphics'][class_name] = {
                'line_thickness_nm': layer_defaults.line_thickness,
                'text_width_nm': layer_defaults.text.size.x,
                'text_height_nm': layer_defaults.text.size.y,
                'text_thickness_nm': layer_defaults.text.stroke_width,
                'italic': layer_defaults.text.italic,
                'keep_upright': layer_defaults.text.keep_upright
            }
    except Exception as e:
        result['text_and_graphics'] = {'error': str(e)}

    # Dimension defaults
    try:
        dim_defaults = board.dimension_defaults.get()
        result['dimension_defaults'] = dim_defaults.to_dict()
    except Exception as e:
        result['dimension_defaults'] = {'error': str(e)}

    # Zone defaults
    try:
        zone_defaults = board.zone_defaults.get()
        result['zone_defaults'] = zone_defaults.to_dict()
    except Exception as e:
        result['zone_defaults'] = {'error': str(e)}

    # Pre-defined sizes (tracks, vias, diff pairs)
    try:
        sizes = board.client.send(board_commands_pb2.GetPreDefinedSizes(board=board._doc), board_commands_pb2.PreDefinedSizesResponse)
        result['predefined_sizes'] = {
            'tracks': [{'width_nm': w} for w in sizes.sizes.track_widths_nm],
            'vias': [{'diameter_nm': v.diameter_nm, 'drill_nm': v.drill_nm} for v in sizes.sizes.via_sizes],
            'diff_pairs': [{'width_nm': d.width_nm, 'gap_nm': d.gap_nm, 'via_gap_nm': d.via_gap_nm} for d in sizes.sizes.diff_pairs]
        }
    except Exception as e:
        result['predefined_sizes'] = {'error': str(e)}

    # Teardrop settings
    try:
        td = board.teardrops.get()
        result['teardrops'] = td.to_dict()
    except Exception as e:
        result['teardrops'] = {'error': str(e)}

    # Length-tuning pattern settings
    try:
        ltp = board.tuning.get_pattern_settings()
        result['length_tuning_patterns'] = ltp.to_dict()
    except Exception as e:
        result['length_tuning_patterns'] = {'error': str(e)}

    # Tuning profiles
    try:
        profiles = board.tuning.get_profiles()
        result['tuning_profiles'] = [p.to_dict() for p in profiles]
    except Exception as e:
        result['tuning_profiles'] = {'error': str(e)}

    # Component classes
    try:
        cc = board.component_classes.get()
        result['component_classes'] = cc.to_dict()
    except Exception as e:
        result['component_classes'] = {'error': str(e)}

    # Custom DRC rules
    try:
        result['custom_rules'] = board.custom_rules.get()
    except Exception as e:
        result['custom_rules'] = {'error': str(e)}

    # Grid settings
    try:
        grid = board.grid.get_settings()
        style_names = {
            board_commands_pb2.GridStyle.GS_DOTS: 'dots',
            board_commands_pb2.GridStyle.GS_LINES: 'lines',
            board_commands_pb2.GridStyle.GS_SMALL_CROSS: 'small_cross'
        }
        result['grid'] = {
            'size_x_nm': grid.grid_size_x_nm,
            'size_y_nm': grid.grid_size_y_nm,
            'visible': grid.show_grid,
            'style': style_names.get(grid.style, 'dots')
        }
    except Exception as e:
        result['grid'] = {'error': str(e)}

    # DRC severities
    try:
        drc_settings = board.drc.get_settings()
        severity_names = {
            board_commands_pb2.DrcSeverity.DRS_ERROR: 'error',
            board_commands_pb2.DrcSeverity.DRS_WARNING: 'warning',
            board_commands_pb2.DrcSeverity.DRS_IGNORE: 'ignore'
        }
        result['drc_severities'] = {
            cs.check_name: severity_names.get(cs.severity, 'error')
            for cs in drc_settings.check_severities
        }
    except Exception as e:
        result['drc_severities'] = {'error': str(e)}

    # Net classes (via project)
    try:
        project = board.get_project()
        net_classes = project.get_net_classes()
        def nc_to_dict(nc):
            d = {
                'name': nc.name,
                'priority': nc.priority,
                'clearance_nm': nc.clearance,
                'track_width_nm': nc.track_width,
                'via_diameter_nm': nc.via_diameter,
                'via_drill_nm': nc.via_drill,
                'microvia_diameter_nm': nc.microvia_diameter,
                'microvia_drill_nm': nc.microvia_drill,
                'diff_pair_width_nm': nc.diff_pair_track_width,
                'diff_pair_gap_nm': nc.diff_pair_gap,
                'diff_pair_via_gap_nm': nc.diff_pair_via_gap,
                'tuning_profile': nc.tuning_profile
            }
            if nc._proto.HasField('board') and nc._proto.board.HasField('color'):
                bc = nc._proto.board.color
                d['pcb_color'] = {'r': bc.r, 'g': bc.g, 'b': bc.b, 'a': bc.a}
            return d
        result['net_classes'] = [nc_to_dict(nc) for nc in net_classes]
    except Exception as e:
        result['net_classes'] = {'error': str(e)}

    # Text variables (via project)
    try:
        project = board.get_project()
        text_vars = project.get_text_variables()
        result['text_variables'] = dict(text_vars.variables)
    except Exception as e:
        result['text_variables'] = {'error': str(e)}

    # Title block
    try:
        tb = board.page.get_title_block()
        result['title_block'] = {
            'title': tb.title,
            'date': tb.date,
            'revision': tb.revision,
            'company': tb.company,
            'comments': tb.comments
        }
    except Exception as e:
        result['title_block'] = {'error': str(e)}

    # Origins
    try:
        from kipy.proto.board import board_commands_pb2 as bc
        grid_origin = board.page.get_origin(bc.BoardOriginType.BOT_GRID)
        drill_origin = board.page.get_origin(bc.BoardOriginType.BOT_DRILL)
        result['origins'] = {
            'grid_mm': [grid_origin.x / 1e6, grid_origin.y / 1e6],
            'drill_mm': [drill_origin.x / 1e6, drill_origin.y / 1e6]
        }
    except Exception as e:
        result['origins'] = {'error': str(e)}

    # Net class assignments (pattern -> netclass mappings)
    try:
        project = board.get_project()
        result['net_class_assignments'] = project.get_net_class_assignments()
    except Exception as e:
        result['net_class_assignments'] = {'error': str(e)}

    # Components summary (ref, value, datasheet) for net class generation
    try:
        comp_list = []
        for fp in board.get_footprints():
            ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else ''
            value = fp.value_field.text.value if hasattr(fp, 'value_field') else ''
            comp = {'ref': ref, 'value': value}
            # Extract datasheet from fields
            if hasattr(fp, 'definition') and hasattr(fp.definition, 'fields'):
                for field in fp.definition.fields:
                    if hasattr(field, 'name') and field.name == 'Datasheet':
                        ds = field.text.value if hasattr(field.text, 'value') else ''
                        if ds:
                            comp['datasheet'] = ds
                        break
            comp_list.append(comp)
        result['components'] = comp_list
    except Exception as e:
        result['components'] = {'error': str(e)}

    print(json.dumps(result, indent=2))


def _set():
    result = {'status': 'success', 'updated': []}

    # Board editor layers
    board_editor_layers = TOOL_ARGS.get("board_editor_layers")
    if board_editor_layers:
        try:
            # Handle copper layer count change
            if 'copper_layer_count' in board_editor_layers:
                enabled = board.layers.get_enabled_layers()
                board.layers.set_enabled_layers(board_editor_layers['copper_layer_count'], enabled)

            # Handle individual layer updates
            layers_list = board_editor_layers.get('layers', [])
            if layers_list:
                # Layer name to BoardLayer enum mapping
                layer_map = {
                    'BL_F_Cu': BoardLayer.BL_F_Cu,
                    'BL_B_Cu': BoardLayer.BL_B_Cu,
                    'BL_In1_Cu': BoardLayer.BL_In1_Cu,
                    'BL_In2_Cu': BoardLayer.BL_In2_Cu,
                    'BL_In3_Cu': BoardLayer.BL_In3_Cu,
                    'BL_In4_Cu': BoardLayer.BL_In4_Cu,
                    'BL_In5_Cu': BoardLayer.BL_In5_Cu,
                    'BL_In6_Cu': BoardLayer.BL_In6_Cu,
                    'BL_F_SilkS': BoardLayer.BL_F_SilkS,
                    'BL_B_SilkS': BoardLayer.BL_B_SilkS,
                    'BL_F_Mask': BoardLayer.BL_F_Mask,
                    'BL_B_Mask': BoardLayer.BL_B_Mask,
                    'BL_F_Paste': BoardLayer.BL_F_Paste,
                    'BL_B_Paste': BoardLayer.BL_B_Paste,
                    'BL_F_Adhes': BoardLayer.BL_F_Adhes,
                    'BL_B_Adhes': BoardLayer.BL_B_Adhes,
                    'BL_F_CrtYd': BoardLayer.BL_F_CrtYd,
                    'BL_B_CrtYd': BoardLayer.BL_B_CrtYd,
                    'BL_F_Fab': BoardLayer.BL_F_Fab,
                    'BL_B_Fab': BoardLayer.BL_B_Fab,
                    'BL_Edge_Cuts': BoardLayer.BL_Edge_Cuts,
                    'BL_Margin': BoardLayer.BL_Margin,
                    'BL_Dwgs_User': BoardLayer.BL_Dwgs_User,
                    'BL_Cmts_User': BoardLayer.BL_Cmts_User,
                    'BL_Eco1_User': BoardLayer.BL_Eco1_User,
                    'BL_Eco2_User': BoardLayer.BL_Eco2_User,
                    'BL_User_1': BoardLayer.BL_User_1,
                    'BL_User_2': BoardLayer.BL_User_2,
                    'BL_User_3': BoardLayer.BL_User_3,
                    'BL_User_4': BoardLayer.BL_User_4,
                }
                for layer in layers_list:
                    layer_name = layer.get('layer')
                    if not layer_name:
                        continue
                    if 'user_name' in layer and layer_name in layer_map:
                        board.layers.set_layer_name(layer_map[layer_name], layer['user_name'])
                    if 'type' in layer and layer_name in layer_map:
                        try:
                            board.layers.set_layer_type(layer_map[layer_name], layer['type'])
                        except ValueError:
                            pass  # Layer type only applies to copper layers

            result['updated'].append('board_editor_layers')
        except Exception as e:
            result['board_editor_layers_error'] = str(e)

    # Physical stackup
    physical_stackup = TOOL_ARGS.get("physical_stackup")
    if physical_stackup:
        try:
            stackup = board.layers.get_stackup()
            if 'impedance_controlled' in physical_stackup:
                stackup.impedance_controlled = physical_stackup['impedance_controlled']
            if 'finish_type' in physical_stackup:
                stackup.finish_type = physical_stackup['finish_type']
            if 'has_edge_plating' in physical_stackup:
                stackup.has_edge_plating = physical_stackup['has_edge_plating']

            # Update individual layer properties
            stackup_layers = physical_stackup.get('layers', [])
            if stackup_layers:
                layer_type_map = {
                    'BSLT_COPPER': board_pb2.BoardStackupLayerType.BSLT_COPPER,
                    'BSLT_DIELECTRIC': board_pb2.BoardStackupLayerType.BSLT_DIELECTRIC,
                    'BSLT_SILKSCREEN': board_pb2.BoardStackupLayerType.BSLT_SILKSCREEN,
                    'BSLT_SOLDERMASK': board_pb2.BoardStackupLayerType.BSLT_SOLDERMASK,
                    'BSLT_SOLDERPASTE': board_pb2.BoardStackupLayerType.BSLT_SOLDERPASTE,
                }
                for sl in stackup_layers:
                    sl_index = sl.get('index')
                    sl_layer = sl.get('layer')

                    # Iterate over stackup.layers (wrapper) to match indices from _get
                    for idx, layer in enumerate(stackup.layers):
                        # Match by index if provided, otherwise by layer name
                        match = False
                        if sl_index is not None:
                            match = (idx == sl_index)
                        elif sl_layer:
                            match = (BoardLayer.Name(layer.layer) == sl_layer)

                        if match:
                            # Access proto through the wrapper for modifications
                            proto_layer = layer._proto
                            if 'thickness_nm' in sl:
                                proto_layer.thickness.value_nm = sl['thickness_nm']
                            if 'material' in sl:
                                proto_layer.material_name = sl['material']
                            if 'color' in sl and isinstance(sl['color'], dict):
                                proto_layer.color.r = sl['color'].get('r', 0)
                                proto_layer.color.g = sl['color'].get('g', 0)
                                proto_layer.color.b = sl['color'].get('b', 0)
                                proto_layer.color.a = sl['color'].get('a', 255)
                            if 'dielectric' in sl and isinstance(sl['dielectric'], list):
                                for i, sub_props in enumerate(sl['dielectric']):
                                    if i < len(proto_layer.dielectric.layer):
                                        if 'epsilon_r' in sub_props:
                                            proto_layer.dielectric.layer[i].epsilon_r = sub_props['epsilon_r']
                                        if 'loss_tangent' in sub_props:
                                            proto_layer.dielectric.layer[i].loss_tangent = sub_props['loss_tangent']
                                        if 'material' in sub_props:
                                            proto_layer.dielectric.layer[i].material_name = sub_props['material']
                                        if 'thickness_nm' in sub_props:
                                            proto_layer.dielectric.layer[i].thickness.value_nm = sub_props['thickness_nm']
                            break

            # Apply the updated stackup
            board.layers.update_stackup(stackup)
            result['updated'].append('physical_stackup')
        except Exception as e:
            result['physical_stackup_error'] = str(e)

    # Board Finish
    board_finish = TOOL_ARGS.get("board_finish")
    if board_finish:
        try:
            stackup = board.layers.get_stackup()
            if 'copper_finish' in board_finish:
                stackup.finish_type = board_finish['copper_finish']
            if 'has_plated_edge' in board_finish:
                stackup.has_edge_plating = board_finish['has_plated_edge']
            if 'edge_connector' in board_finish:
                stackup.edge_connector = board_finish['edge_connector']
            board.layers.update_stackup(stackup)
            result['updated'].append('board_finish')
        except Exception as e:
            result['board_finish_error'] = str(e)

    # Solder Mask/Paste
    solder_mask_paste = TOOL_ARGS.get("solder_mask_paste")
    if solder_mask_paste:
        try:
            rules = board.design_rules.get()
            if 'solder_mask_expansion_nm' in solder_mask_paste:
                rules.solder_mask_expansion = solder_mask_paste['solder_mask_expansion_nm']
            if 'solder_mask_min_width_nm' in solder_mask_paste:
                rules.solder_mask_min_width = solder_mask_paste['solder_mask_min_width_nm']
            if 'solder_mask_to_copper_clearance_nm' in solder_mask_paste:
                rules.solder_mask_to_copper_clearance = solder_mask_paste['solder_mask_to_copper_clearance_nm']
            if 'allow_bridged_apertures' in solder_mask_paste:
                rules._proto.allow_soldermask_bridges_in_fps = solder_mask_paste['allow_bridged_apertures']
            if 'tent_vias_front' in solder_mask_paste:
                rules._proto.tent_vias_front = solder_mask_paste['tent_vias_front']
            if 'tent_vias_back' in solder_mask_paste:
                rules._proto.tent_vias_back = solder_mask_paste['tent_vias_back']
            if 'solder_paste_clearance_nm' in solder_mask_paste:
                rules.solder_paste_margin = solder_mask_paste['solder_paste_clearance_nm']
            if 'solder_paste_ratio' in solder_mask_paste:
                rules.solder_paste_margin_ratio = solder_mask_paste['solder_paste_ratio']
            board.design_rules.set(rules)
            result['updated'].append('solder_mask_paste')
        except Exception as e:
            result['solder_mask_paste_error'] = str(e)

    # Zone hatch offsets
    zone_hatch_offsets = TOOL_ARGS.get("zone_hatch_offsets")
    if zone_hatch_offsets:
        try:
            layer_offsets = []
            if isinstance(zone_hatch_offsets, list):
                for offset in zone_hatch_offsets:
                    if 'layer' not in offset:
                        continue
                    layer_offsets.append({
                        'layer': offset['layer'],
                        'offset_x': offset.get('offset_x_nm', 0),
                        'offset_y': offset.get('offset_y_nm', 0)
                    })
            if layer_offsets:
                board.layers.set_zone_hatch_offsets(layer_offsets)
            result['updated'].append('zone_hatch_offsets')
        except Exception as e:
            result['zone_hatch_offsets_error'] = str(e)

    # Design rules
    design_rules = TOOL_ARGS.get("design_rules")
    if design_rules:
        try:
            rules = board.design_rules.get()
            if 'min_clearance_nm' in design_rules:
                rules.min_clearance = design_rules['min_clearance_nm']
            if 'min_track_width_nm' in design_rules:
                rules.min_track_width = design_rules['min_track_width_nm']
            if 'min_connection_nm' in design_rules:
                rules.min_connection = design_rules['min_connection_nm']
            if 'min_via_diameter_nm' in design_rules:
                rules.min_via_diameter = design_rules['min_via_diameter_nm']
            if 'min_via_drill_nm' in design_rules:
                rules.min_via_drill = design_rules['min_via_drill_nm']
            if 'min_via_annular_width_nm' in design_rules:
                rules.min_via_annular_width = design_rules['min_via_annular_width_nm']
            if 'min_microvia_diameter_nm' in design_rules:
                rules.min_microvia_diameter = design_rules['min_microvia_diameter_nm']
            if 'min_microvia_drill_nm' in design_rules:
                rules.min_microvia_drill = design_rules['min_microvia_drill_nm']
            if 'min_through_hole_nm' in design_rules:
                rules.min_through_hole = design_rules['min_through_hole_nm']
            if 'min_hole_to_hole_nm' in design_rules:
                rules.min_hole_to_hole = design_rules['min_hole_to_hole_nm']
            if 'hole_to_copper_clearance_nm' in design_rules:
                rules.hole_to_copper_clearance = design_rules['hole_to_copper_clearance_nm']
            if 'copper_edge_clearance_nm' in design_rules:
                rules.copper_edge_clearance = design_rules['copper_edge_clearance_nm']
            if 'solder_mask_expansion_nm' in design_rules:
                rules.solder_mask_expansion = design_rules['solder_mask_expansion_nm']
            if 'solder_mask_min_width_nm' in design_rules:
                rules.solder_mask_min_width = design_rules['solder_mask_min_width_nm']
            if 'solder_mask_to_copper_clearance_nm' in design_rules:
                rules.solder_mask_to_copper_clearance = design_rules['solder_mask_to_copper_clearance_nm']
            if 'solder_paste_margin_nm' in design_rules:
                rules.solder_paste_margin = design_rules['solder_paste_margin_nm']
            if 'solder_paste_margin_ratio' in design_rules:
                rules.solder_paste_margin_ratio = design_rules['solder_paste_margin_ratio']
            if 'min_silk_clearance_nm' in design_rules:
                rules.min_silk_clearance = design_rules['min_silk_clearance_nm']
            if 'min_silk_text_height_nm' in design_rules:
                rules.min_silk_text_height = design_rules['min_silk_text_height_nm']
            if 'min_silk_text_thickness_nm' in design_rules:
                rules.min_silk_text_thickness = design_rules['min_silk_text_thickness_nm']
            if 'min_resolved_spokes' in design_rules:
                rules.min_resolved_spokes = design_rules['min_resolved_spokes']
            if 'max_error_nm' in design_rules:
                rules._proto.max_error = design_rules['max_error_nm']
            if 'allow_external_fillets' in design_rules:
                rules._proto.allow_external_fillets = design_rules['allow_external_fillets']
            if 'include_stackup_in_length' in design_rules:
                rules._proto.include_stackup_in_length = design_rules['include_stackup_in_length']
            board.design_rules.set(rules)
            result['updated'].append('design_rules')
        except Exception as e:
            result['design_rules_error'] = str(e)

    # Text and graphics defaults
    text_and_graphics = TOOL_ARGS.get("text_and_graphics")
    if text_and_graphics:
        try:
            from kipy.board.types import BoardLayerGraphicsDefaults
            from kipy.common_types import TextAttributes
            layer_name_map = {
                'silkscreen': board_pb2.BoardLayerClass.BLC_SILKSCREEN,
                'copper': board_pb2.BoardLayerClass.BLC_COPPER,
                'edges': board_pb2.BoardLayerClass.BLC_EDGES,
                'courtyard': board_pb2.BoardLayerClass.BLC_COURTYARD,
                'fabrication': board_pb2.BoardLayerClass.BLC_FABRICATION,
                'other': board_pb2.BoardLayerClass.BLC_OTHER
            }
            current_defaults = board.layers.get_graphics_defaults()
            updated_defaults = {}
            for layer_class_name, layer_settings in text_and_graphics.items():
                layer_class = layer_name_map.get(layer_class_name)
                if layer_class is not None and layer_class in current_defaults:
                    defaults_obj = current_defaults[layer_class]
                    if 'line_thickness_nm' in layer_settings:
                        defaults_obj.line_thickness = layer_settings['line_thickness_nm']
                    if 'text_width_nm' in layer_settings:
                        defaults_obj._proto.text.size.x_nm = layer_settings['text_width_nm']
                    if 'text_height_nm' in layer_settings:
                        defaults_obj._proto.text.size.y_nm = layer_settings['text_height_nm']
                    if 'text_thickness_nm' in layer_settings:
                        defaults_obj._proto.text.stroke_width.value_nm = layer_settings['text_thickness_nm']
                    if 'italic' in layer_settings:
                        defaults_obj._proto.text.italic = layer_settings['italic']
                    if 'keep_upright' in layer_settings:
                        defaults_obj._proto.text.keep_upright = layer_settings['keep_upright']
                    updated_defaults[layer_class] = defaults_obj
            if updated_defaults:
                board.layers.set_graphics_defaults(updated_defaults)
            result['updated'].append('text_and_graphics')
        except Exception as e:
            result['text_and_graphics_error'] = str(e)

    # Dimension defaults
    dimension_defaults = TOOL_ARGS.get("dimension_defaults")
    if dimension_defaults:
        try:
            from kipy.board.dimension_defaults import DimensionUnitsMode, DimensionUnitsFormat, DimensionPrecision, DimensionTextPosition
            kwargs = {}
            if 'units_mode' in dimension_defaults:
                kwargs['units_mode'] = DimensionUnitsMode.from_string(dimension_defaults['units_mode'])
            if 'units_format' in dimension_defaults:
                kwargs['units_format'] = DimensionUnitsFormat.from_string(dimension_defaults['units_format'])
            if 'precision' in dimension_defaults:
                prec = dimension_defaults['precision']
                kwargs['precision'] = DimensionPrecision.from_string(prec)
            if 'suppress_zeroes' in dimension_defaults:
                kwargs['suppress_zeroes'] = dimension_defaults['suppress_zeroes']
            if 'text_position' in dimension_defaults:
                kwargs['text_position'] = DimensionTextPosition.from_string(dimension_defaults['text_position'])
            if 'keep_text_aligned' in dimension_defaults:
                kwargs['keep_text_aligned'] = dimension_defaults['keep_text_aligned']
            if 'arrow_length_nm' in dimension_defaults:
                kwargs['arrow_length_nm'] = dimension_defaults['arrow_length_nm']
            if 'extension_offset_nm' in dimension_defaults:
                kwargs['extension_offset_nm'] = dimension_defaults['extension_offset_nm']
            board.dimension_defaults.set(**kwargs)
            result['updated'].append('dimension_defaults')
        except Exception as e:
            result['dimension_defaults_error'] = str(e)

    # Zone defaults
    zone_defaults = TOOL_ARGS.get("zone_defaults")
    if zone_defaults:
        try:
            from kipy.board.zone_defaults import CornerSmoothingMode, ZonePadConnection, ZoneIslandRemoval
            kwargs = {}
            if 'name' in zone_defaults:
                kwargs['name'] = zone_defaults['name']
            if 'locked' in zone_defaults:
                kwargs['locked'] = zone_defaults['locked']
            if 'priority' in zone_defaults:
                kwargs['priority'] = zone_defaults['priority']
            if 'corner_smoothing' in zone_defaults:
                kwargs['corner_smoothing'] = CornerSmoothingMode.from_string(zone_defaults['corner_smoothing'])
            if 'corner_radius_nm' in zone_defaults:
                kwargs['corner_radius_nm'] = zone_defaults['corner_radius_nm']
            if 'clearance_nm' in zone_defaults:
                kwargs['clearance_nm'] = zone_defaults['clearance_nm']
            if 'min_thickness_nm' in zone_defaults:
                kwargs['min_thickness_nm'] = zone_defaults['min_thickness_nm']
            if 'pad_connection' in zone_defaults:
                kwargs['pad_connection'] = ZonePadConnection.from_string(zone_defaults['pad_connection'])
            if 'thermal_gap_nm' in zone_defaults:
                kwargs['thermal_gap_nm'] = zone_defaults['thermal_gap_nm']
            if 'thermal_spoke_width_nm' in zone_defaults:
                kwargs['thermal_spoke_width_nm'] = zone_defaults['thermal_spoke_width_nm']
            if 'island_removal' in zone_defaults:
                kwargs['island_removal'] = ZoneIslandRemoval.from_string(zone_defaults['island_removal'])
            if 'min_island_area_nm2' in zone_defaults:
                kwargs['min_island_area_nm2'] = zone_defaults['min_island_area_nm2']
            board.zone_defaults.set(**kwargs)
            result['updated'].append('zone_defaults')
        except Exception as e:
            result['zone_defaults_error'] = str(e)

    # Pre-defined sizes (read-modify-write to preserve unspecified categories)
    predefined_sizes = TOOL_ARGS.get("predefined_sizes")
    if predefined_sizes:
        try:
            existing = board.client.send(board_commands_pb2.GetPreDefinedSizes(board=board._doc), board_commands_pb2.PreDefinedSizesResponse)
            cmd = board_commands_pb2.SetPreDefinedSizes()
            cmd.board.CopyFrom(board._doc)

            # Handle tracks
            if 'tracks' in predefined_sizes:
                for track in predefined_sizes['tracks']:
                    if 'width_nm' in track:
                        cmd.sizes.track_widths_nm.append(track['width_nm'])
            else:
                for w in existing.sizes.track_widths_nm:
                    cmd.sizes.track_widths_nm.append(w)

            # Handle vias
            if 'vias' in predefined_sizes:
                for via in predefined_sizes['vias']:
                    v = cmd.sizes.via_sizes.add()
                    if 'diameter_nm' in via:
                        v.diameter_nm = via['diameter_nm']
                    if 'drill_nm' in via:
                        v.drill_nm = via['drill_nm']
            else:
                for v in existing.sizes.via_sizes:
                    via = cmd.sizes.via_sizes.add()
                    via.diameter_nm = v.diameter_nm
                    via.drill_nm = v.drill_nm

            # Handle diff pairs
            if 'diff_pairs' in predefined_sizes:
                for dp_input in predefined_sizes['diff_pairs']:
                    dp = cmd.sizes.diff_pairs.add()
                    if 'width_nm' in dp_input:
                        dp.width_nm = dp_input['width_nm']
                    if 'gap_nm' in dp_input:
                        dp.gap_nm = dp_input['gap_nm']
                    if 'via_gap_nm' in dp_input:
                        dp.via_gap_nm = dp_input['via_gap_nm']
            else:
                for d in existing.sizes.diff_pairs:
                    dp = cmd.sizes.diff_pairs.add()
                    dp.width_nm = d.width_nm
                    dp.gap_nm = d.gap_nm
                    dp.via_gap_nm = d.via_gap_nm

            board.client.send(cmd, board_commands_pb2.PreDefinedSizesResponse)
            result['updated'].append('predefined_sizes')
        except Exception as e:
            result['predefined_sizes_error'] = str(e)

    # Teardrop settings
    teardrops = TOOL_ARGS.get("teardrops")
    if teardrops:
        try:
            from kipy.board.teardrops import TeardropParameters
            td_kwargs = {}
            if 'target_vias' in teardrops:
                td_kwargs['target_vias'] = teardrops['target_vias']
            if 'target_pth_pads' in teardrops:
                td_kwargs['target_pth_pads'] = teardrops['target_pth_pads']
            if 'target_smd_pads' in teardrops:
                td_kwargs['target_smd_pads'] = teardrops['target_smd_pads']
            if 'target_track_to_track' in teardrops:
                td_kwargs['target_track_to_track'] = teardrops['target_track_to_track']
            if 'round_shapes_only' in teardrops:
                td_kwargs['round_shapes_only'] = teardrops['round_shapes_only']

            def build_td_params(params):
                kwargs = {}
                for key in ('best_length_ratio', 'max_length_nm', 'best_width_ratio', 'max_width_nm',
                            'curved_edges', 'allow_two_segments', 'prefer_zone_connection', 'track_width_limit_ratio'):
                    if key in params:
                        kwargs[key] = params[key]
                return TeardropParameters(**kwargs)

            if 'round_shapes' in teardrops and isinstance(teardrops['round_shapes'], dict):
                td_kwargs['round_shapes'] = build_td_params(teardrops['round_shapes'])
            if 'rect_shapes' in teardrops and isinstance(teardrops['rect_shapes'], dict):
                td_kwargs['rect_shapes'] = build_td_params(teardrops['rect_shapes'])
            if 'track_to_track' in teardrops and isinstance(teardrops['track_to_track'], dict):
                td_kwargs['track_to_track'] = build_td_params(teardrops['track_to_track'])

            board.teardrops.set(**td_kwargs)
            result['updated'].append('teardrops')
        except Exception as e:
            result['teardrops_error'] = str(e)

    # Length-tuning pattern settings
    length_tuning_patterns = TOOL_ARGS.get("length_tuning_patterns")
    if length_tuning_patterns:
        try:
            cmd = board_commands_pb2.SetLengthTuningPatternSettings()
            cmd.board.CopyFrom(board._doc)

            def apply_meander_params(field_obj, params):
                if 'min_amplitude_nm' in params:
                    field_obj.min_amplitude_nm = params['min_amplitude_nm']
                if 'max_amplitude_nm' in params:
                    field_obj.max_amplitude_nm = params['max_amplitude_nm']
                if 'spacing_nm' in params:
                    field_obj.spacing_nm = params['spacing_nm']
                if 'corner_style' in params:
                    corner_style_map = {
                        'round': board_commands_pb2.MeanderCornerStyle.MCS_ROUND,
                        'chamfer': board_commands_pb2.MeanderCornerStyle.MCS_CHAMFER
                    }
                    field_obj.corner_style = corner_style_map.get(params['corner_style'], board_commands_pb2.MeanderCornerStyle.MCS_ROUND)
                if 'corner_radius_percent' in params:
                    field_obj.corner_radius_percent = params['corner_radius_percent']
                if 'single_sided' in params:
                    field_obj.single_sided = params['single_sided']

            if 'single_track' in length_tuning_patterns and isinstance(length_tuning_patterns['single_track'], dict):
                apply_meander_params(cmd.single_track, length_tuning_patterns['single_track'])
            if 'diff_pair' in length_tuning_patterns and isinstance(length_tuning_patterns['diff_pair'], dict):
                apply_meander_params(cmd.diff_pair, length_tuning_patterns['diff_pair'])
            if 'diff_pair_skew' in length_tuning_patterns and isinstance(length_tuning_patterns['diff_pair_skew'], dict):
                apply_meander_params(cmd.diff_pair_skew, length_tuning_patterns['diff_pair_skew'])

            board.client.send(cmd, board_commands_pb2.LengthTuningPatternSettingsResponse)
            result['updated'].append('length_tuning_patterns')
        except Exception as e:
            result['length_tuning_patterns_error'] = str(e)

    # Tuning profiles
    tuning_profiles = TOOL_ARGS.get("tuning_profiles")
    if tuning_profiles:
        try:
            cmd = board_commands_pb2.SetTuningProfiles()
            cmd.board.CopyFrom(board._doc)
            if isinstance(tuning_profiles, list):
                for profile_input in tuning_profiles:
                    profile = cmd.profiles.profiles.add()
                    if 'name' in profile_input:
                        profile.name = profile_input['name']
                    if 'type' in profile_input:
                        type_map = {
                            'single': board_commands_pb2.TuningProfileType.TPT_SINGLE,
                            'differential': board_commands_pb2.TuningProfileType.TPT_DIFFERENTIAL
                        }
                        profile.type = type_map.get(profile_input['type'], board_commands_pb2.TuningProfileType.TPT_SINGLE)
                    if 'target_impedance_ohms' in profile_input:
                        profile.target_impedance_ohms = profile_input['target_impedance_ohms']
                    if 'enable_time_domain_tuning' in profile_input:
                        profile.enable_time_domain_tuning = profile_input['enable_time_domain_tuning']
                    if 'via_propagation_delay_ps' in profile_input:
                        profile.via_propagation_delay_ps = profile_input['via_propagation_delay_ps']
                    # Track entries
                    for entry_input in profile_input.get('track_entries', []):
                        track_entry = profile.track_entries.add()
                        if 'signal_layer' in entry_input:
                            track_entry.signal_layer.layer_id = entry_input['signal_layer']
                        if 'top_reference_layer' in entry_input:
                            track_entry.top_reference_layer.layer_id = entry_input['top_reference_layer']
                        if 'bottom_reference_layer' in entry_input:
                            track_entry.bottom_reference_layer.layer_id = entry_input['bottom_reference_layer']
                        if 'width_nm' in entry_input:
                            track_entry.width_nm = entry_input['width_nm']
                        if 'diff_pair_gap_nm' in entry_input:
                            track_entry.diff_pair_gap_nm = entry_input['diff_pair_gap_nm']
                        if 'delay_ps_per_mm' in entry_input:
                            track_entry.delay_ps_per_mm = entry_input['delay_ps_per_mm']
                        if 'enable_time_domain' in entry_input:
                            track_entry.enable_time_domain = entry_input['enable_time_domain']
                    # Via overrides
                    for override_input in profile_input.get('via_overrides', []):
                        via_override = profile.via_overrides.add()
                        if 'signal_layer_from' in override_input:
                            via_override.signal_layer_from.layer_id = override_input['signal_layer_from']
                        if 'signal_layer_to' in override_input:
                            via_override.signal_layer_to.layer_id = override_input['signal_layer_to']
                        if 'via_layer_from' in override_input:
                            via_override.via_layer_from.layer_id = override_input['via_layer_from']
                        if 'via_layer_to' in override_input:
                            via_override.via_layer_to.layer_id = override_input['via_layer_to']
                        if 'delay_ps' in override_input:
                            via_override.delay_ps = override_input['delay_ps']
            board.client.send(cmd, board_commands_pb2.TuningProfilesResponse)
            result['updated'].append('tuning_profiles')
        except Exception as e:
            result['tuning_profiles_error'] = str(e)

    # Component classes
    component_classes = TOOL_ARGS.get("component_classes")
    if component_classes:
        try:
            cmd = board_commands_pb2.SetComponentClassSettings()
            cmd.board.CopyFrom(board._doc)
            if 'enable_sheet_component_classes' in component_classes:
                cmd.settings.enable_sheet_component_classes = component_classes['enable_sheet_component_classes']
            for assignment_input in component_classes.get('assignments', []):
                assignment = cmd.settings.assignments.add()
                if 'component_class' in assignment_input:
                    assignment.component_class = assignment_input['component_class']
                if 'operator' in assignment_input:
                    operator_map = {
                        'all': board_commands_pb2.ComponentClassConditionsOperator.CCCO_ALL,
                        'any': board_commands_pb2.ComponentClassConditionsOperator.CCCO_ANY
                    }
                    assignment.operator = operator_map.get(assignment_input['operator'], board_commands_pb2.ComponentClassConditionsOperator.CCCO_ALL)
                for cond_input in assignment_input.get('conditions', []):
                    condition = assignment.conditions.add()
                    if 'type' in cond_input:
                        type_map = {
                            'reference': board_commands_pb2.ComponentClassConditionType.CCCT_REFERENCE,
                            'footprint': board_commands_pb2.ComponentClassConditionType.CCCT_FOOTPRINT,
                            'side': board_commands_pb2.ComponentClassConditionType.CCCT_SIDE,
                            'rotation': board_commands_pb2.ComponentClassConditionType.CCCT_ROTATION,
                            'footprint_field': board_commands_pb2.ComponentClassConditionType.CCCT_FOOTPRINT_FIELD,
                            'custom': board_commands_pb2.ComponentClassConditionType.CCCT_CUSTOM,
                            'sheet_name': board_commands_pb2.ComponentClassConditionType.CCCT_SHEET_NAME
                        }
                        condition.type = type_map.get(cond_input['type'], board_commands_pb2.ComponentClassConditionType.CCCT_REFERENCE)
                    if 'primary_data' in cond_input:
                        condition.primary_data = cond_input['primary_data']
                    if 'secondary_data' in cond_input:
                        condition.secondary_data = cond_input['secondary_data']
            board.client.send(cmd, board_commands_pb2.ComponentClassSettingsResponse)
            result['updated'].append('component_classes')
        except Exception as e:
            result['component_classes_error'] = str(e)

    # Custom DRC rules
    custom_rules = TOOL_ARGS.get("custom_rules")
    if custom_rules is not None:
        try:
            rules_text = ''
            if isinstance(custom_rules, str):
                rules_text = custom_rules
            elif isinstance(custom_rules, dict) and 'rules_text' in custom_rules:
                if isinstance(custom_rules['rules_text'], str):
                    rules_text = custom_rules['rules_text']
            board.custom_rules.set(rules_text)
            result['updated'].append('custom_rules')
        except Exception as e:
            result['custom_rules_error'] = str(e)

    # Grid settings
    grid = TOOL_ARGS.get("grid")
    if grid:
        try:
            kwargs = {}
            if 'size_x_nm' in grid:
                kwargs['grid_size_x_nm'] = grid['size_x_nm']
            if 'size_y_nm' in grid:
                kwargs['grid_size_y_nm'] = grid['size_y_nm']
            if 'visible' in grid:
                kwargs['show_grid'] = grid['visible']
            if 'style' in grid:
                style_map = {
                    'dots': board_commands_pb2.GridStyle.GS_DOTS,
                    'lines': board_commands_pb2.GridStyle.GS_LINES,
                    'small_cross': board_commands_pb2.GridStyle.GS_SMALL_CROSS
                }
                kwargs['style'] = style_map.get(grid['style'], board_commands_pb2.GridStyle.GS_DOTS)
            if kwargs:
                board.grid.set_settings(**kwargs)
                result['updated'].append('grid')
        except Exception as e:
            result['grid_error'] = str(e)

    # DRC severities
    drc_severities = TOOL_ARGS.get("drc_severities")
    if drc_severities:
        try:
            settings = board.drc.get_settings()
            severity_map = {
                'error': board_commands_pb2.DrcSeverity.DRS_ERROR,
                'warning': board_commands_pb2.DrcSeverity.DRS_WARNING,
                'ignore': board_commands_pb2.DrcSeverity.DRS_IGNORE
            }
            for check_name, severity in drc_severities.items():
                settings.set_check_severity(check_name, severity_map[severity])
            board.drc.set_settings(settings)
            result['updated'].append('drc_severities')
        except Exception as e:
            result['drc_severities_error'] = str(e)

    # Net classes
    net_classes = TOOL_ARGS.get("net_classes")
    if net_classes:
        try:
            from kipy.common_types import Color
            project = board.get_project()
            new_classes = []
            for nc_input in net_classes:
                nc = NetClass()
                if 'name' in nc_input:
                    nc.name = nc_input['name']
                if 'priority' in nc_input:
                    nc.priority = nc_input['priority']
                if 'clearance_nm' in nc_input:
                    nc.clearance = nc_input['clearance_nm']
                if 'track_width_nm' in nc_input:
                    nc.track_width = nc_input['track_width_nm']
                if 'via_diameter_nm' in nc_input:
                    nc.via_diameter = nc_input['via_diameter_nm']
                if 'via_drill_nm' in nc_input:
                    nc.via_drill = nc_input['via_drill_nm']
                if 'microvia_diameter_nm' in nc_input:
                    nc.microvia_diameter = nc_input['microvia_diameter_nm']
                if 'microvia_drill_nm' in nc_input:
                    nc.microvia_drill = nc_input['microvia_drill_nm']
                if 'diff_pair_width_nm' in nc_input:
                    nc.diff_pair_track_width = nc_input['diff_pair_width_nm']
                if 'diff_pair_gap_nm' in nc_input:
                    nc.diff_pair_gap = nc_input['diff_pair_gap_nm']
                if 'diff_pair_via_gap_nm' in nc_input:
                    nc.diff_pair_via_gap = nc_input['diff_pair_via_gap_nm']
                if 'tuning_profile' in nc_input:
                    nc.tuning_profile = nc_input['tuning_profile']
                if 'pcb_color' in nc_input and isinstance(nc_input['pcb_color'], dict):
                    _color = Color()
                    _color.red = nc_input['pcb_color'].get('r', 0.0)
                    _color.green = nc_input['pcb_color'].get('g', 0.0)
                    _color.blue = nc_input['pcb_color'].get('b', 0.0)
                    _color.alpha = nc_input['pcb_color'].get('a', 1.0)
                    nc.board_color = _color
                new_classes.append(nc)
            project.set_net_classes(new_classes, merge_mode=MapMergeMode.MMM_REPLACE)
            result['updated'].append('net_classes')
        except Exception as e:
            result['net_classes_error'] = str(e)

    # Net class assignments (pattern -> netclass mappings)
    net_class_assignments = TOOL_ARGS.get("net_class_assignments")
    if net_class_assignments:
        try:
            project = board.get_project()
            project.set_net_class_assignments(net_class_assignments)
            result['updated'].append('net_class_assignments')
        except Exception as e:
            result['net_class_assignments_error'] = str(e)

    # Text variables
    text_variables = TOOL_ARGS.get("text_variables")
    if text_variables:
        try:
            from kipy.project_types import TextVariables
            project = board.get_project()
            tv = TextVariables()
            tv.variables = dict(text_variables)
            project.set_text_variables(tv, merge_mode=MapMergeMode.MMM_MERGE)
            result['updated'].append('text_variables')
        except Exception as e:
            result['text_variables_error'] = str(e)

    # Title block
    title_block = TOOL_ARGS.get("title_block")
    if title_block:
        try:
            kwargs = {}
            if 'title' in title_block:
                kwargs['title'] = title_block['title']
            if 'date' in title_block:
                kwargs['date'] = title_block['date']
            if 'revision' in title_block:
                kwargs['revision'] = title_block['revision']
            if 'company' in title_block:
                kwargs['company'] = title_block['company']

            comments = {}
            if 'comments' in title_block and isinstance(title_block['comments'], dict):
                for key, value in title_block['comments'].items():
                    if isinstance(value, str):
                        comments[int(key)] = value
            else:
                for i in range(1, 10):
                    key = f'comment{i}'
                    if key in title_block:
                        comments[i] = title_block[key]
            if comments:
                kwargs['comments'] = comments

            if kwargs:
                board.page.set_title_block(**kwargs)
                result['updated'].append('title_block')
        except Exception as e:
            result['title_block_error'] = str(e)

    # Origins
    origins = TOOL_ARGS.get("origins")
    if origins:
        try:
            grid_key = 'grid_mm' if 'grid_mm' in origins else ('grid' if 'grid' in origins else None)
            if grid_key:
                grid_origin = origins[grid_key]
                if isinstance(grid_origin, list) and len(grid_origin) >= 2:
                    board.page.set_origin(
                        board_commands_pb2.BoardOriginType.BOT_GRID,
                        Vector2.from_xy(int(grid_origin[0] * 1e6), int(grid_origin[1] * 1e6))
                    )

            drill_key = 'drill_mm' if 'drill_mm' in origins else ('drill' if 'drill' in origins else None)
            if drill_key:
                drill_origin = origins[drill_key]
                if isinstance(drill_origin, list) and len(drill_origin) >= 2:
                    board.page.set_origin(
                        board_commands_pb2.BoardOriginType.BOT_DRILL,
                        Vector2.from_xy(int(drill_origin[0] * 1e6), int(drill_origin[1] * 1e6))
                    )

            result['updated'].append('origins')
        except Exception as e:
            result['origins_error'] = str(e)

    print(json.dumps(result, indent=2))


action = TOOL_ARGS.get("action", "get")
if action == "get":
    _get()
else:
    _set()
