import json
from kipy.geometry import Vector2
from kipy.proto.board.board_types_pb2 import BoardLayer

def mm_to_nm(mm):
    return round(mm * 1000000)

def get_pad_abs_position(fp, pad_num):
    """Get absolute position of a pad by number - pad.position is already absolute"""
    if not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):
        return None, None
    for pad in fp.definition.pads:
        if str(pad.number) == str(pad_num):
            # pad.position contains absolute board coordinates (already transformed)
            net = pad.net.name if hasattr(pad, 'net') else None
            return (pad.position.x, pad.position.y), net
    return None, None

# Layer name to BoardLayer enum mapping
layer_map = {
    'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,
    'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,
}

from_pad = TOOL_ARGS.get("from", {})
to_pad = TOOL_ARGS.get("to", {})

if not from_pad or not to_pad:
    print(json.dumps({'status': 'error', 'message': 'from and to are required'}))
else:
    from_ref = from_pad.get('ref', '')
    from_pad_num = from_pad.get('pad', '')
    to_ref = to_pad.get('ref', '')
    to_pad_num = to_pad.get('pad', '')
    width = TOOL_ARGS.get('width', 0.25)
    layer_name = TOOL_ARGS.get('layer', '') or 'F.Cu'
    waypoints_input = TOOL_ARGS.get('waypoints', [])

    width_nm = mm_to_nm(width)
    route_layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)

    # Find footprints by reference
    from_fp = board.footprints.get_by_reference(from_ref)
    to_fp = board.footprints.get_by_reference(to_ref)

    if not from_fp:
        print(json.dumps({'status': 'error', 'message': f'Footprint not found: {from_ref}'}))
    elif not to_fp:
        print(json.dumps({'status': 'error', 'message': f'Footprint not found: {to_ref}'}))
    else:
        # Get pad positions from footprint definitions
        from_pos, from_net = get_pad_abs_position(from_fp, from_pad_num)
        to_pos, _ = get_pad_abs_position(to_fp, to_pad_num)

        if not from_pos:
            print(json.dumps({'status': 'error', 'message': f'Pad {from_pad_num} not found on {from_ref}'}))
        elif not to_pos:
            print(json.dumps({'status': 'error', 'message': f'Pad {to_pad_num} not found on {to_ref}'}))
        else:
            all_tracks = []
            all_vias = []

            if waypoints_input:
                # Parse waypoints for multi-segment routing
                all_points = [from_pos]
                via_indices = []
                layer_changes = {}

                for i, wp in enumerate(waypoints_input):
                    wp_pos = (mm_to_nm(wp['position'][0]), mm_to_nm(wp['position'][1]))
                    all_points.append(wp_pos)
                    if wp.get('via', False):
                        via_indices.append(len(all_points) - 1)
                        if 'layer' in wp:
                            layer_changes[len(all_points) - 1] = wp['layer']

                all_points.append(to_pos)

                if via_indices:
                    current_layer = route_layer
                    seg_start = 0
                    for vi in via_indices:
                        seg_pts = [Vector2.from_xy(int(p[0]), int(p[1])) for p in all_points[seg_start:vi+1]]
                        tracks = board.route_track(points=seg_pts, width=width_nm, layer=current_layer, net=from_net)
                        all_tracks.extend(tracks)
                        via = board.routing.add_via(position=Vector2.from_xy(int(all_points[vi][0]), int(all_points[vi][1])),
                                            diameter=mm_to_nm(0.8), drill=mm_to_nm(0.4), net=from_net)
                        all_vias.append(via)
                        if vi in layer_changes:
                            current_layer = layer_map.get(layer_changes[vi], current_layer)
                        seg_start = vi
                    if seg_start < len(all_points) - 1:
                        seg_pts = [Vector2.from_xy(int(p[0]), int(p[1])) for p in all_points[seg_start:]]
                        tracks = board.route_track(points=seg_pts, width=width_nm, layer=current_layer, net=from_net)
                        all_tracks.extend(tracks)
                else:
                    pts = [Vector2.from_xy(int(p[0]), int(p[1])) for p in all_points]
                    all_tracks = board.route_track(points=pts, width=width_nm, layer=route_layer, net=from_net)
            else:
                # Simple two-point route
                points = [
                    Vector2.from_xy(int(from_pos[0]), int(from_pos[1])),
                    Vector2.from_xy(int(to_pos[0]), int(to_pos[1]))
                ]
                all_tracks = board.route_track(points=points, width=width_nm, layer=route_layer, net=from_net)
                all_vias = []

            track_info = []
            for t in all_tracks:
                track_info.append({
                    'id': str(t.id.value),
                    'layer': layer_name,
                    'from': [t.start.x / 1000000, t.start.y / 1000000],
                    'to': [t.end.x / 1000000, t.end.y / 1000000]
                })
            via_info = []
            for v in all_vias:
                via_info.append({
                    'id': str(v.id.value),
                    'position': [v.position.x / 1000000, v.position.y / 1000000]
                })

            print(json.dumps({
                'status': 'success',
                'tracks': track_info,
                'vias': via_info
            }, indent=2))
