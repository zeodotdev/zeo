# Copyright (C) 2026, Zeo <team@zeo.dev>
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
from kipy.proto.board.board_types_pb2 import BoardLayer

section = TOOL_ARGS.get("section", "footprints")
filter_pattern = TOOL_ARGS.get("filter", "")

if section == "footprints":
    # Read footprints + optionally include per-pad info (with is_connector_pad).
    include_pads = bool(TOOL_ARGS.get("include_pads", False))
    connector_pad_set = set()
    if include_pads:
        try:
            connector_pad_set = set(board.get_connector_pads())
        except Exception:
            pass

    footprints = board.get_footprints()
    result = []
    for fp in footprints:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'
        if filter_pattern:
            import fnmatch
            if not fnmatch.fnmatch(ref, filter_pattern):
                continue
        pos = fp.position
        angle = fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0
        entry = {
            'id': fp.id.value,
            'ref': ref,
            'value': fp.value_field.text.value if hasattr(fp, 'value_field') else '',
            'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',
            'position': [pos.x / 1000000, pos.y / 1000000],
            'angle': angle,
            'layer': 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu',
            'locked': getattr(fp, 'locked', False)
        }
        if include_pads:
            pads = []
            for pad in fp.definition.pads:
                puid = str(pad.id.value)
                pads.append({
                    'id': puid,
                    'number': pad.number,
                    'net': pad.net.name if hasattr(pad, 'net') else '',
                    'position': [pad.position.x / 1000000, pad.position.y / 1000000],
                    'is_connector_pad': puid in connector_pad_set,
                })
            entry['pads'] = pads
        result.append(entry)
    print(json.dumps(result, indent=2))

elif section == "connector_pads":
    # Multi-board metadata: list every pad currently marked as a connector
    # pad on this board (BOARD::m_connectorPads). Returns pad UUIDs only —
    # use section="footprints" with include_pads:true for full pad context.
    try:
        uuids = board.get_connector_pads()
        print(json.dumps({'count': len(uuids), 'pad_uuids': list(uuids)}, indent=2))
    except Exception as e:
        print(json.dumps({'error': str(e)}))

elif section == "tracks":
    # Read tracks
    tracks = board.get_tracks()
    result = []
    for t in list(tracks)[:100]:  # Limit to 100
        result.append({
            'id': t.id.value,
            'start': [t.start.x / 1000000, t.start.y / 1000000],
            'end': [t.end.x / 1000000, t.end.y / 1000000],
            'width': t.width / 1000000,
            'layer': BoardLayer.Name(t.layer).replace('BL_', '').replace('_', '.'),
            'net': t.net.name if hasattr(t, 'net') else ''
        })
    print(json.dumps({'count': len(tracks), 'tracks': result}, indent=2))

elif section == "vias":
    # Read vias
    vias = board.get_vias()
    result = []
    for v in vias:
        result.append({
            'id': v.id.value,
            'position': [v.position.x / 1000000, v.position.y / 1000000],
            'diameter': v.diameter / 1000000 if hasattr(v, 'diameter') else 0,
            'drill': v.drill_diameter / 1000000 if hasattr(v, 'drill_diameter') else 0,
            'net': v.net.name if hasattr(v, 'net') else ''
        })
    print(json.dumps(result, indent=2))

elif section == "zones":
    # Read zones
    zones = board.get_zones()
    result = []
    for z in zones:
        result.append({
            'id': z.id.value,
            'net': z.net.name if hasattr(z, 'net') else '',
            'layers': [BoardLayer.Name(l).replace('BL_', '').replace('_', '.') for l in z.layers] if hasattr(z, 'layers') else [],
            'priority': getattr(z, 'priority', 0)
        })
    print(json.dumps(result, indent=2))

elif section == "nets":
    # Read nets, with computed length-weighted average impedance (model-aware) per
    # routed net. Optional `filter` restricts by net name (fnmatch pattern).
    import fnmatch
    nets = board.get_nets()

    impedance_info = {}
    stackup_has_dk = None
    try:
        imp = board.nets.get_net_impedance()
        stackup_has_dk = imp['stackup_has_dk']
        for entry in imp['nets']:
            impedance_info[entry['net_name']] = entry
    except Exception:
        pass

    result = []
    for n in nets:
        if filter_pattern and not fnmatch.fnmatch(n.name, filter_pattern):
            continue
        net_entry = {'name': n.name}
        imp_entry = impedance_info.get(n.name)
        if imp_entry:
            net_entry['impedance'] = {
                'average_ohms': imp_entry['average_impedance_ohms'],
                'is_differential': imp_entry['is_differential'],
                'insertion_loss_db': round(imp_entry['insertion_loss_db'], 4),
                'routed_length_nm': imp_entry['routed_length_nm'],
            }
        result.append(net_entry)
    print(json.dumps({'stackup_has_dk': stackup_has_dk, 'nets': result}, indent=2))

elif section == "net_impedance":
    # Per-track impedance breakdown for a single net. `filter` MUST be an exact net
    # name (bounds output size). Reports each routed segment's model, Z0/Z_diff, and loss.
    net_name = filter_pattern
    if not net_name:
        print(json.dumps({'error': "section='net_impedance' requires 'filter' set to a net name"}))
    else:
        try:
            res = board.nets.get_track_impedance(net_name)
            print(json.dumps({
                'net': net_name,
                'stackup_has_dk': res['stackup_has_dk'],
                'tracks': res['tracks'],
            }, indent=2))
        except Exception as e:
            print(json.dumps({'error': str(e)}))

elif section == "layers":
    # Read layers
    layers = board.get_enabled_layers()
    result = [{'name': BoardLayer.Name(l).replace('BL_', '').replace('_', '.')} for l in layers] if layers else []
    print(json.dumps(result, indent=2))

elif section == "stackup":
    # Read stackup. BoardStackup has no scalar copper_layer_count field —
    # derive it from the layer list and include the per-layer detail the
    # LLM actually needs (names, thickness, dielectric vs copper, finish).
    try:
        from kipy.proto.board.board_pb2 import BoardStackupLayerType

        stackup = board.get_stackup()
        layers_out = []
        copper_count = 0

        for layer in stackup.layers:
            lname = BoardLayer.Name(layer.layer).replace('BL_', '').replace('_', '.') \
                if layer.layer else ''
            ltype_name = BoardStackupLayerType.Name(layer.type).replace('BSLT_', '').lower() \
                if hasattr(layer, 'type') else ''
            if ltype_name == 'copper':
                copper_count += 1
            layers_out.append({
                'layer': lname,
                'type': ltype_name,
                'thickness_nm': getattr(layer, 'thickness', 0),
                'material': getattr(layer, 'material_name', ''),
                'user_name': getattr(layer, 'user_name', ''),
            })

        # stackup.calculate_board_thickness() under-reports for dielectrics
        # whose `dielectric.layer` sub-list is empty (data lives directly on
        # layer.thickness instead). Sum the per-layer thickness we already
        # collected so the total matches the on-disk board thickness.
        total_thickness = sum( l['thickness_nm'] for l in layers_out )

        result = {
            'copper_layers': copper_count,
            'total_thickness_nm': total_thickness,
            'impedance_controlled': stackup.impedance_controlled,
            'finish_type': stackup.finish_type,
            'has_edge_plating': stackup.has_edge_plating,
            'edge_connector': stackup.edge_connector,
            'layers': layers_out,
        }
        print(json.dumps(result, indent=2))
    except Exception as e:
        print(json.dumps({'error': str(e)}))

elif section == "drawings":
    # Read drawings (shapes + text)
    shapes = board.get_shapes()
    texts = board.get_text()
    result = {'shapes': [], 'text': []}
    for s in shapes:
        shape_info = {'id': s.id.value, 'layer': BoardLayer.Name(s.layer).replace('BL_', '').replace('_', '.')}
        if hasattr(s, 'start') and hasattr(s, 'end'):
            shape_info['type'] = 'segment'
            shape_info['start'] = [s.start.x / 1000000, s.start.y / 1000000]
            shape_info['end'] = [s.end.x / 1000000, s.end.y / 1000000]
        elif hasattr(s, 'center') and hasattr(s, 'radius_point'):
            shape_info['type'] = 'circle'
            shape_info['center'] = [s.center.x / 1000000, s.center.y / 1000000]
        elif hasattr(s, 'top_left') and hasattr(s, 'bottom_right'):
            shape_info['type'] = 'rectangle'
            shape_info['top_left'] = [s.top_left.x / 1000000, s.top_left.y / 1000000]
            shape_info['bottom_right'] = [s.bottom_right.x / 1000000, s.bottom_right.y / 1000000]
        result['shapes'].append(shape_info)

    for t in texts:
        result['text'].append({
            'id': t.id.value,
            'text': t.value if hasattr(t, 'value') else '',
            'position': [t.position.x / 1000000, t.position.y / 1000000],
            'layer': BoardLayer.Name(t.layer).replace('BL_', '').replace('_', '.')
        })
    print(json.dumps(result, indent=2))

else:
    print(json.dumps({'error': f'Unknown section: {section}'}))
