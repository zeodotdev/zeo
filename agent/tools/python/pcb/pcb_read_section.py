import json
from kipy.proto.board.board_types_pb2 import BoardLayer

section = TOOL_ARGS.get("section", "footprints")
filter_pattern = TOOL_ARGS.get("filter", "")

if section == "footprints":
    # Read footprints
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
        result.append({
            'id': fp.id.value,
            'ref': ref,
            'value': fp.value_field.text.value if hasattr(fp, 'value_field') else '',
            'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',
            'position': [pos.x / 1000000, pos.y / 1000000],
            'angle': angle,
            'layer': 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu',
            'locked': getattr(fp, 'locked', False)
        })
    print(json.dumps(result, indent=2))

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
    # Read nets
    nets = board.get_nets()
    result = [{'name': n.name} for n in nets]
    print(json.dumps(result, indent=2))

elif section == "layers":
    # Read layers
    layers = board.get_enabled_layers()
    result = [{'name': BoardLayer.Name(l).replace('BL_', '').replace('_', '.')} for l in layers] if layers else []
    print(json.dumps(result, indent=2))

elif section == "stackup":
    # Read stackup
    try:
        stackup = board.get_stackup()
        result = {'copper_layers': stackup.copper_layer_count if hasattr(stackup, 'copper_layer_count') else 2}
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
