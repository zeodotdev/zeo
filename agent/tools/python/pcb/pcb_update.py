import json
from kipy.geometry import Vector2, Angle
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.board_types import Net

updates = TOOL_ARGS.get("updates", [])
if not updates:
    print(json.dumps({'status': 'error', 'message': 'updates array is required'}))
else:
    def mm_to_nm(mm):
        return round(mm * 1000000)

    # Layer name to BoardLayer enum mapping
    layer_map = {
        'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,
        'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,
    }

    # Build ref->footprint map for reference lookups
    all_fps = board.get_footprints()
    ref_to_fp = {}
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if ref:
            ref_to_fp[ref] = fp

    # Build id->item map for UUID lookups
    id_to_item = {}
    for fp in all_fps:
        id_to_item[str(fp.id.value)] = ('footprint', fp)
    for item in board.get_tracks():
        id_to_item[str(item.id.value)] = ('track', item)
    for item in board.get_vias():
        id_to_item[str(item.id.value)] = ('via', item)
    for item in board.get_shapes():
        id_to_item[str(item.id.value)] = ('shape', item)
    for item in board.get_text():
        id_to_item[str(item.id.value)] = ('text', item)
    for item in board.get_zones():
        id_to_item[str(item.id.value)] = ('zone', item)

    updated = []
    not_found = []
    errors = []

    for upd in updates:
        target = upd.get('target', '')
        if not target:
            errors.append({'error': 'missing target'})
            continue

        item = None
        is_footprint = False

        # Try as footprint reference first
        if target in ref_to_fp:
            item = ref_to_fp[target]
            is_footprint = True
        # Then try as UUID
        elif target in id_to_item:
            item_type, item = id_to_item[target]
            is_footprint = (item_type == 'footprint')

        if not item:
            not_found.append(target)
            continue

        try:
            changed = False

            # Position update
            if 'position' in upd and len(upd['position']) >= 2:
                item.position = Vector2.from_xy(mm_to_nm(upd['position'][0]), mm_to_nm(upd['position'][1]))
                changed = True

            # Angle/rotation update (for footprints)
            if 'angle' in upd and is_footprint:
                item.orientation = Angle.from_degrees(upd['angle'])
                changed = True

            # Layer update
            if 'layer' in upd:
                layer_name = upd['layer']
                item.layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                changed = True

            # Locked update
            if 'locked' in upd:
                item.locked = upd['locked']
                changed = True

            # Width update (tracks, vias)
            if 'width' in upd and hasattr(item, 'width'):
                item.width = mm_to_nm(upd['width'])
                changed = True

            # Net update (tracks, vias, zones)
            if 'net' in upd and hasattr(item, 'net'):
                net_obj = Net()
                net_obj.name = upd['net']
                item.net = net_obj
                changed = True

            # Text/value update (text items)
            if 'text' in upd and hasattr(item, 'value'):
                item.value = upd['text']
                changed = True

            if changed:
                board.update_items([item])
                updated.append(target)

        except Exception as e:
            errors.append({'target': target, 'error': str(e)})

    status = 'success' if not errors and not not_found else 'partial'
    print(json.dumps({'status': status, 'updated': updated, 'not_found': not_found, 'errors': errors}, indent=2))
