import json
from kipy.geometry import Vector2
from kipy.proto.common.types.enums_pb2 import HA_LEFT, HA_RIGHT, VA_TOP, VA_BOTTOM
from kipy.schematic_types import LocalLabel, GlobalLabel, HierarchicalLabel

refresh_or_fail(sch)

ref = TOOL_ARGS.get("ref", "")
label_type = TOOL_ARGS.get("label_type", "local")
pin_labels = TOOL_ARGS.get("labels", {})
h_align_override = TOOL_ARGS.get("h_align", None)
v_align_override = TOOL_ARGS.get("v_align", None)

def build_label(text, position, h_align, v_align, rotation=0):
    """Build a label proto in memory (no IPC call)."""
    if label_type == 'global':
        lbl = GlobalLabel.create(position, text)
    elif label_type == 'hierarchical':
        lbl = HierarchicalLabel.create(position, text)
    else:
        lbl = LocalLabel.create(position, text)
    lbl._proto.text.attributes.horizontal_alignment = h_align
    lbl._proto.text.attributes.vertical_alignment = v_align
    if rotation != 0:
        lbl._proto.text.attributes.angle.value_degrees = rotation
    return lbl

# Resolve ref as symbol or sheet
results = []
labels_to_create = []  # Collect all label protos for batch creation
_is_sheet = False
sym = None
sheet = None
try:
    sym = sch.symbols.get_by_ref(ref)
    if not sym:
        raise ValueError('not found')
except:
    sym = None

if not sym:
    for _s in sch.crud.get_sheets():
        if _s.name == ref:
            sheet = _s
            _is_sheet = True
            break

if not sym and not sheet:
    results = [{'error': f'No symbol or sheet found matching: {ref}'}]

elif _is_sheet:
    _pin_by_name = {p.name: p for p in sheet.pins}

    for pin_id, label_text in pin_labels.items():
        try:
            _pin = _pin_by_name.get(pin_id)
            if not _pin:
                results.append({'pin': pin_id, 'label': label_text, 'error': f'Sheet pin not found: {pin_id}'})
                continue

            px = _pin.position.x / 1_000_000
            py = _pin.position.y / 1_000_000
            _side = _pin.side

            # Sheet pin side -> label alignment (already world-space)
            rotation = 0
            if _side == 1:
                h_align, v_align = HA_RIGHT, VA_BOTTOM
                direction = 'left'
            elif _side == 2:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'
            elif _side == 3:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'up'
                rotation = 90
            elif _side == 4:
                h_align, v_align = HA_LEFT, VA_TOP
                direction = 'down'
                rotation = 270
            else:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'

            if h_align_override is not None:
                h_align = HA_LEFT if h_align_override == 'left' else HA_RIGHT
            if v_align_override is not None:
                v_align = VA_TOP if v_align_override == 'top' else VA_BOTTOM

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            lbl = build_label(label_text, label_pos, h_align, v_align, rotation)
            labels_to_create.append(lbl)
            results.append({'pin': pin_id, 'label': label_text, 'position': [round(lbl_x, 2), round(lbl_y, 2)], 'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

else:
    # Orientation transform: apply symbol rotation and mirroring
    _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
    _rot_steps = round(getattr(sym, 'angle', 0) / 90) % 4

    def transform_orientation(orient):
        o = orient
        for _ in range(_rot_steps):
            o = _rot90.get(o, o)
        if getattr(sym, 'mirror_x', False):
            if o == 2: o = 3
            elif o == 3: o = 2
        if getattr(sym, 'mirror_y', False):
            if o == 0: o = 1
            elif o == 1: o = 0
        return o

    for pin_id, label_text in pin_labels.items():
        try:
            pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)
            if not pin_result:
                pin_pos = sch.symbols.get_pin_position(sym, pin_id)
                if not pin_pos:
                    results.append({'pin': pin_id, 'label': label_text, 'error': f'Pin not found: {pin_id}'})
                    continue
                px = pin_pos.x / 1_000_000
                py = pin_pos.y / 1_000_000
                orient = None
            else:
                px = pin_result['position'].x / 1_000_000
                py = pin_result['position'].y / 1_000_000
                orient = pin_result.get('orientation', None)

            out_dx, out_dy = 0, 0
            rotation = 0
            if orient is not None:
                orient = transform_orientation(orient)
                if orient == 0:    # escape left
                    h_align, v_align = HA_RIGHT, VA_BOTTOM
                    direction = 'left'
                    out_dx, out_dy = -1, 0
                elif orient == 1:  # escape right
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
                elif orient == 2:  # escape down (vertical pin)
                    h_align, v_align = HA_LEFT, VA_TOP
                    direction = 'down'
                    out_dx, out_dy = 0, 1
                    rotation = 270
                elif orient == 3:  # escape up (vertical pin)
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'up'
                    out_dx, out_dy = 0, -1
                    rotation = 90
                else:
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
            else:
                sym_cx = sym.position.x / 1_000_000
                sym_cy = sym.position.y / 1_000_000
                if px > sym_cx:
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
                else:
                    h_align, v_align = HA_RIGHT, VA_BOTTOM
                    direction = 'left'
                    out_dx, out_dy = -1, 0

            if h_align_override is not None:
                h_align = HA_LEFT if h_align_override == 'left' else HA_RIGHT
            if v_align_override is not None:
                v_align = VA_TOP if v_align_override == 'top' else VA_BOTTOM

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            lbl = build_label(label_text, label_pos, h_align, v_align, rotation)
            labels_to_create.append(lbl)
            results.append({'pin': pin_id, 'label': label_text, 'position': [round(lbl_x, 2), round(lbl_y, 2)], 'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

# Batch-create all labels in a single IPC call
created_items = []
if labels_to_create:
    created_items = sch.crud.create_items(labels_to_create) or []

# Attach UUIDs from created items to results
if created_items:
    success_results = [r for r in results if 'error' not in r]
    for i, item in enumerate(created_items):
        if i < len(success_results):
            uid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', ''))
            if uid:
                success_results[i]['uuid'] = uid

# Auto-sync sheet pins when placing hierarchical labels
if label_type == 'hierarchical':
    try:
        sch.sheets.sync_pins()
    except:
        pass

print(json.dumps({'status': 'success', 'ref': ref, 'labels_placed': len([r for r in results if 'error' not in r]), 'labels_failed': len([r for r in results if 'error' in r]), 'results': results}, indent=2))
