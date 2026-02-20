import json
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.geometry import Vector2, Angle

placements = TOOL_ARGS.get("placements", [])
if not placements:
    print(json.dumps({'status': 'error', 'message': 'placements array is required'}))
else:
    # Get all footprints and build ref->footprint map
    all_fps = board.get_footprints()
    ref_to_fp = {}
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if ref:
            ref_to_fp[ref] = fp

    placed = []
    not_found = []

    for p in placements:
        ref = p.get('ref')
        if ref not in ref_to_fp:
            not_found.append(ref)
            continue

        fp = ref_to_fp[ref]
        updated = False

        # Update position
        if 'position' in p and len(p['position']) >= 2:
            fp.position = Vector2.from_xy(round(p['position'][0] * 1000000), round(p['position'][1] * 1000000))
            updated = True

        # Update angle
        if 'angle' in p:
            fp.orientation = Angle.from_degrees(p['angle'])
            updated = True

        # Update layer (flip)
        if 'layer' in p:
            fp.layer = BoardLayer.BL_B_Cu if p['layer'] == 'B.Cu' else BoardLayer.BL_F_Cu
            updated = True

        if updated:
            placed.append(ref)

    # Apply updates
    if placed:
        fps_to_update = [ref_to_fp[ref] for ref in placed]
        board.update_items(fps_to_update)

    # Build result with pad positions for immediate routing
    placed_info = []
    for ref in placed:
        fp = ref_to_fp[ref]
        fp_layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'

        fp_info = {
            'ref': ref,
            'position': [fp.position.x / 1000000, fp.position.y / 1000000],
            'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,
            'layer': fp_layer,
            'pads': []
        }
        # Get pads - pad.position is already in absolute board coordinates
        if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):
            for pad in fp.definition.pads:
                fp_info['pads'].append({
                    'number': str(pad.number) if hasattr(pad, 'number') else '',
                    'position': [pad.position.x / 1000000, pad.position.y / 1000000],
                    'net': pad.net.name if hasattr(pad, 'net') else ''
                })
        placed_info.append(fp_info)

    result = {'status': 'success', 'placed': placed_info, 'not_found': not_found}
    print(json.dumps(result, indent=2))
