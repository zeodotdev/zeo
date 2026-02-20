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

_LABEL_SHRINK = 0.4  # Shrink label bboxes to allow stacking at 2.54mm pitch

# Collect bounding boxes of existing elements for overlap detection
_BBOX_MARGIN = 1.0
placed_bboxes = []
try:
    for _esym in sch.symbols.get_all():
        try:
            _ebb = sch.transform.get_bounding_box(_esym, units='mm', include_text=False)
        except:
            continue
        if _ebb:
            placed_bboxes.append({'ref': getattr(_esym, 'reference', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})
except:
    pass
try:
    for _esht in sch.crud.get_sheets():
        try:
            _ebb = sch.transform.get_bounding_box(_esht, units='mm')
        except:
            continue
        if _ebb:
            placed_bboxes.append({'ref': getattr(_esht, 'name', 'sheet'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})
except:
    pass
try:
    for _elbl in sch.labels.get_all():
        try:
            _ebb = sch.transform.get_bounding_box(_elbl, units='mm')
        except:
            continue
        if _ebb:
            placed_bboxes.append({'ref': getattr(_elbl, 'text', '?'), 'min_x': _ebb['min_x'] + _LABEL_SHRINK, 'max_x': _ebb['max_x'] - _LABEL_SHRINK, 'min_y': _ebb['min_y'] + _LABEL_SHRINK, 'max_y': _ebb['max_y'] - _LABEL_SHRINK})
except:
    pass

def _bboxes_overlap(a, b):
    return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']

def _point_in_bbox(px, py, bb):
    """Check if a point is inside a bounding box (for pin-on-pin exclusion)."""
    return bb['min_x'] <= px <= bb['max_x'] and bb['min_y'] <= py <= bb['max_y']

def create_label(text, position, h_align, v_align):
    if label_type == 'global':
        lbl = GlobalLabel.create(position, text)
    elif label_type == 'hierarchical':
        lbl = HierarchicalLabel.create(position, text)
    else:
        lbl = LocalLabel.create(position, text)
    lbl._proto.text.attributes.horizontal_alignment = h_align
    lbl._proto.text.attributes.vertical_alignment = v_align
    created = sch.crud.create_items(lbl)
    return created[0] if created else lbl

# Resolve ref as symbol or sheet
results = []
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
            # SPS_LEFT=1: label extends left; SPS_RIGHT=2: label extends right
            # SPS_TOP=3: horizontal above; SPS_BOTTOM=4: horizontal below
            if _side == 1:
                h_align, v_align = HA_RIGHT, VA_BOTTOM
                direction = 'left'
            elif _side == 2:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'
            elif _side == 3:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'up'
            elif _side == 4:
                h_align, v_align = HA_LEFT, VA_TOP
                direction = 'down'
            else:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'

            # Apply alignment overrides
            if h_align_override is not None:
                h_align = HA_LEFT if h_align_override == 'left' else HA_RIGHT
            if v_align_override is not None:
                v_align = VA_TOP if v_align_override == 'top' else VA_BOTTOM

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            _lbl = create_label(label_text, label_pos, h_align, v_align)

            # Overlap detection -- reject if label overlaps any existing element
            _rejected = False
            try:
                _bb = sch.transform.get_bounding_box(_lbl, units='mm')
                if _bb:
                    _new_bbox = {'min_x': _bb['min_x'] + _LABEL_SHRINK, 'max_x': _bb['max_x'] - _LABEL_SHRINK, 'min_y': _bb['min_y'] + _LABEL_SHRINK, 'max_y': _bb['max_y'] - _LABEL_SHRINK}
                    _conflict = None
                    for _pb in placed_bboxes:
                        if _bboxes_overlap(_new_bbox, _pb) and not _point_in_bbox(px, py, _pb):
                            _conflict = _pb
                            break
                    if _conflict:
                        sch.crud.remove_items([_lbl])
                        _cr = _conflict.get('ref', '?')
                        _ox = min(_new_bbox['max_x'], _conflict['max_x']) - max(_new_bbox['min_x'], _conflict['min_x'])
                        _oy = min(_new_bbox['max_y'], _conflict['max_y']) - max(_new_bbox['min_y'], _conflict['min_y'])
                        results.append({'pin': pin_id, 'label': label_text, 'error': f"Placement rejected: overlaps '{_cr}' by {_ox:.1f}mm horizontal, {_oy:.1f}mm vertical"})
                        _rejected = True
                    else:
                        placed_bboxes.append({'ref': label_text, **_new_bbox})
            except:
                pass
            if not _rejected:
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
        # Apply mirroring: mirror_x flips left<->right, mirror_y flips up<->down
        if getattr(sym, 'mirror_x', False):
            if o == 0: o = 1
            elif o == 1: o = 0
        if getattr(sym, 'mirror_y', False):
            if o == 2: o = 3
            elif o == 3: o = 2
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

            # Transform orientation and compute escape direction
            out_dx, out_dy = 0, 0
            if orient is not None:
                orient = transform_orientation(orient)
                # orient 0=PIN_RIGHT(body), 1=PIN_LEFT(body), 2=PIN_UP(body), 3=PIN_DOWN(body)
                if orient == 0:    # escape left
                    h_align, v_align = HA_RIGHT, VA_BOTTOM
                    direction = 'left'
                    out_dx, out_dy = -1, 0
                elif orient == 1:  # escape right
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
                elif orient == 2:  # escape down
                    h_align, v_align = HA_LEFT, VA_TOP
                    direction = 'down'
                    out_dx, out_dy = 0, 1
                elif orient == 3:  # escape up
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'up'
                    out_dx, out_dy = 0, -1
                else:
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
            else:
                # Fallback: guess from symbol center
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

            # Apply alignment overrides
            if h_align_override is not None:
                h_align = HA_LEFT if h_align_override == 'left' else HA_RIGHT
            if v_align_override is not None:
                v_align = VA_TOP if v_align_override == 'top' else VA_BOTTOM

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            _lbl = create_label(label_text, label_pos, h_align, v_align)

            # Overlap detection -- reject if label overlaps any existing element
            _rejected = False
            try:
                _bb = sch.transform.get_bounding_box(_lbl, units='mm')
                if _bb:
                    _new_bbox = {'min_x': _bb['min_x'] + _LABEL_SHRINK, 'max_x': _bb['max_x'] - _LABEL_SHRINK, 'min_y': _bb['min_y'] + _LABEL_SHRINK, 'max_y': _bb['max_y'] - _LABEL_SHRINK}
                    _conflict = None
                    for _pb in placed_bboxes:
                        if _bboxes_overlap(_new_bbox, _pb) and not _point_in_bbox(px, py, _pb):
                            _conflict = _pb
                            break
                    if _conflict:
                        sch.crud.remove_items([_lbl])
                        _cr = _conflict.get('ref', '?')
                        _ox = min(_new_bbox['max_x'], _conflict['max_x']) - max(_new_bbox['min_x'], _conflict['min_x'])
                        _oy = min(_new_bbox['max_y'], _conflict['max_y']) - max(_new_bbox['min_y'], _conflict['min_y'])
                        results.append({'pin': pin_id, 'label': label_text, 'error': f"Placement rejected: overlaps '{_cr}' by {_ox:.1f}mm horizontal, {_oy:.1f}mm vertical"})
                        _rejected = True
                    else:
                        placed_bboxes.append({'ref': label_text, **_new_bbox})
            except:
                pass
            if not _rejected:
                results.append({'pin': pin_id, 'label': label_text, 'position': [round(lbl_x, 2), round(lbl_y, 2)], 'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

# Auto-sync sheet pins when placing hierarchical labels
if label_type == 'hierarchical':
    try:
        sch.sheets.sync_pins()
    except:
        pass

print(json.dumps({'status': 'success', 'ref': ref, 'labels_placed': len([r for r in results if 'error' not in r]), 'labels_failed': len([r for r in results if 'error' in r]), 'results': results}, indent=2))
