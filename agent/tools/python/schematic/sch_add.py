import json, sys, re, math
from kipy.geometry import Vector2
from kipy.proto.common.types.enums_pb2 import HA_LEFT, HA_RIGHT, VA_TOP, VA_BOTTOM

refresh_or_fail(sch)

# Build map of used references for auto-numbering
used_refs = {}
for _s in sch.symbols.get_all():
    _r = getattr(_s, 'reference', '')
    _m = re.match(r'^([A-Za-z#]+)(\d+)$', _r)
    if _m:
        used_refs.setdefault(_m.group(1), set()).add(int(_m.group(2)))

def next_ref(prefix):
    nums = used_refs.get(prefix, set())
    n = 1
    while n in nums:
        n += 1
    used_refs.setdefault(prefix, set()).add(n)
    return f'{prefix}{n}'

results = []
_placed_syms = {}
_placed_wires = []

# Collect bounding boxes of all existing symbols, sheets, and labels for overlap detection
# (_BBOX_MARGIN and collect_placed_bboxes from bbox.py preamble are not exactly the same
#  shape as what's needed here, so we build our own list with custom sheet/label handling)
_LABEL_SHRINK = 0.4  # Shrink label bboxes to allow stacking at 2.54mm pitch
placed_bboxes = []
try:
    _all_existing = sch.symbols.get_all()
    for _esym in _all_existing:
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

def _bboxes_overlap_add(a, b):
    return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']

def _find_crossing_wire(bbox):
    """Return AABB dict of the first wire segment crossing bbox, or None."""
    for _w in sch.crud.get_wires():
        _sx, _sy = round(_w.start.x/1e6, 2), round(_w.start.y/1e6, 2)
        _ex, _ey = round(_w.end.x/1e6, 2), round(_w.end.y/1e6, 2)
        _wbb = {'min_x': min(_sx,_ex), 'max_x': max(_sx,_ex), 'min_y': min(_sy,_ey), 'max_y': max(_sy,_ey)}
        if _wbb['min_x'] == _wbb['max_x']:
            _wbb['min_x'] -= 0.01
            _wbb['max_x'] += 0.01
        if _wbb['min_y'] == _wbb['max_y']:
            _wbb['min_y'] -= 0.01
            _wbb['max_y'] += 0.01
        if _wbb['max_x'] > bbox['min_x'] and _wbb['min_x'] < bbox['max_x'] and _wbb['max_y'] > bbox['min_y'] and _wbb['min_y'] < bbox['max_y']:
            return _wbb
    return None

def _overlap_info(new_bb):
    """Find first overlap and return descriptive string with overlap amounts."""
    for _pb in placed_bboxes:
        if _bboxes_overlap_add(new_bb, _pb):
            ox = min(new_bb['max_x'], _pb['max_x']) - max(new_bb['min_x'], _pb['min_x'])
            oy = min(new_bb['max_y'], _pb['max_y']) - max(new_bb['min_y'], _pb['min_y'])
            ref = _pb.get('ref', '?')
            return f"Overlaps '{ref}' by {ox:.1f}mm horizontal, {oy:.1f}mm vertical"
    _wcross = _find_crossing_wire(new_bb)
    if _wcross:
        return 'Overlaps a wire segment'
    return 'Overlaps existing element(s)'

# Get sheet dimensions for bounds checking
_sheet_w, _sheet_h = 297.0, 210.0
try:
    _page = sch.page.get_settings()
    _sheet_w = _page.width_mm
    _sheet_h = _page.height_mm
except:
    pass

class _OOB(Exception): pass
def _check_bounds(x, y, idx):
    if not (0 <= x <= _sheet_w and 0 <= y <= _sheet_h):
        results.append({'index': idx, 'error': f'Position ({x}, {y}) is outside sheet ({_sheet_w}x{_sheet_h}mm)'})
        raise _OOB()

_SLIDE_GRID = 1.27
_SLIDE_MAX_ITER = 5
_SLIDE_MAX_MM = 30.0

def _snap_grid(v, grid=_SLIDE_GRID):
    return round(round(v / grid) * grid, 4)

def _slide_off(item, raw_bb, margined_bb):
    """Slide item to clear position via repulsion. Returns (ok, dx, dy)."""
    total_dx, total_dy = 0.0, 0.0
    r_bb = dict(raw_bb)
    m_bb = dict(margined_bb)
    for _iter in range(_SLIDE_MAX_ITER):
        obstacle = None
        use_margin = True
        for _pb in placed_bboxes:
            if _bboxes_overlap_add(m_bb, _pb):
                obstacle = _pb
                break
        if obstacle is None:
            _wcross = _find_crossing_wire(r_bb)
            if _wcross:
                obstacle = _wcross
                use_margin = False
        if obstacle is None:
            return (True, total_dx, total_dy)
        comp_cx = (r_bb['min_x'] + r_bb['max_x']) / 2
        comp_cy = (r_bb['min_y'] + r_bb['max_y']) / 2
        obs_cx = (obstacle['min_x'] + obstacle['max_x']) / 2
        obs_cy = (obstacle['min_y'] + obstacle['max_y']) / 2
        dir_x = comp_cx - obs_cx
        dir_y = comp_cy - obs_cy
        mag = math.sqrt(dir_x * dir_x + dir_y * dir_y)
        if mag < 1e-6:
            dir_x, dir_y, mag = 1.0, 0.0, 1.0
        dir_x /= mag
        dir_y /= mag
        if use_margin:
            hw_c = (m_bb['max_x'] - m_bb['min_x']) / 2
            hh_c = (m_bb['max_y'] - m_bb['min_y']) / 2
        else:
            hw_c = (r_bb['max_x'] - r_bb['min_x']) / 2
            hh_c = (r_bb['max_y'] - r_bb['min_y']) / 2
        hw_o = (obstacle['max_x'] - obstacle['min_x']) / 2
        hh_o = (obstacle['max_y'] - obstacle['min_y']) / 2
        candidates = []
        if abs(dir_x) > 1e-9:
            candidates.append((hw_c + hw_o) / abs(dir_x))
        if abs(dir_y) > 1e-9:
            candidates.append((hh_c + hh_o) / abs(dir_y))
        if not candidates:
            return (False, total_dx, total_dy)
        t = min(candidates)
        new_cx = obs_cx + dir_x * t
        new_cy = obs_cy + dir_y * t
        dx = _snap_grid(new_cx - comp_cx)
        dy = _snap_grid(new_cy - comp_cy)
        if abs(dx) < 1e-6 and abs(dy) < 1e-6:
            if abs(dir_x) >= abs(dir_y):
                dx = _SLIDE_GRID if dir_x >= 0 else -_SLIDE_GRID
            else:
                dy = _SLIDE_GRID if dir_y >= 0 else -_SLIDE_GRID
        total_dx += dx
        total_dy += dy
        if abs(total_dx) > _SLIDE_MAX_MM or abs(total_dy) > _SLIDE_MAX_MM:
            return (False, total_dx, total_dy)
        r_bb = {'min_x': r_bb['min_x']+dx, 'max_x': r_bb['max_x']+dx, 'min_y': r_bb['min_y']+dy, 'max_y': r_bb['max_y']+dy}
        m_bb = {'min_x': m_bb['min_x']+dx, 'max_x': m_bb['max_x']+dx, 'min_y': m_bb['min_y']+dy, 'max_y': m_bb['max_y']+dy}
        if r_bb['min_x'] < 0 or r_bb['max_x'] > _sheet_w or r_bb['min_y'] < 0 or r_bb['max_y'] > _sheet_h:
            return (False, total_dx, total_dy)
    return (False, total_dx, total_dy)

elements = TOOL_ARGS.get("elements", [])
has_hierarchical_label = False

try:
    for i, elem in enumerate(elements):
        element_type = elem.get("element_type", "")
        try:
            if element_type == "symbol":
                lib_id = elem.get("lib_id", "")
                pos = elem.get("position", [0, 0])
                pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
                pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0
                angle = elem.get("angle", 0.0)
                mirror = elem.get("mirror", "none")
                mirror_x = (mirror == "x")
                mirror_y = (mirror == "y")
                unit = elem.get("unit", 1)

                _check_bounds(pos_x, pos_y, i)
                sym = sch.symbols.add(
                    lib_id=lib_id,
                    position=Vector2.from_xy_mm(pos_x, pos_y),
                    unit=unit,
                    angle=angle,
                    mirror_x=mirror_x,
                    mirror_y=mirror_y
                )

                # Handle properties
                props = elem.get("properties", {})
                if 'Value' in props:
                    sch.symbols.set_value(sym, props['Value'])
                if 'Footprint' in props:
                    sch.symbols.set_footprint(sym, props['Footprint'])

                # Overlap check + slide-off
                _shifted = False
                _shift_dx = 0
                _shift_dy = 0
                _rejected = False
                _bb = None
                _new_bbox = None
                try:
                    _bb = sch.transform.get_bounding_box(sym, units='mm', include_text=False)
                    if _bb:
                        _raw_bb = dict(_bb)
                        _new_bbox = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                        _has_conflict = any(_bboxes_overlap_add(_new_bbox, _pb) for _pb in placed_bboxes) or (_find_crossing_wire(_raw_bb) is not None)
                        if _has_conflict:
                            _sok, _sdx, _sdy = _slide_off(sym, _raw_bb, _new_bbox)
                            if _sok and (abs(_sdx) > 1e-6 or abs(_sdy) > 1e-6):
                                sch.transform.move(sym, delta_x_mm=_sdx, delta_y_mm=_sdy)
                                _bb = sch.transform.get_bounding_box(sym, units='mm', include_text=False)
                                if _bb:
                                    _new_bbox = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                                _shifted = True
                                _shift_dx = _sdx
                                _shift_dy = _sdy
                            elif not _sok:
                                _rejected = True
                except:
                    pass
                if _rejected:
                    sch.crud.remove_items([sym])
                    results.append({'index': i, 'error': f'Placement rejected: {_overlap_info(_new_bbox)}. Could not auto-slide to clear position.'})
                else:
                    _prefix = re.match(r'^([A-Za-z#]+)', getattr(sym, 'reference', 'X')).group(1)
                    _new_ref = next_ref(_prefix)
                    for _f in sym._proto.fields:
                        if _f.name == 'Reference':
                            _f.text = _new_ref
                            break
                    sch.crud.update_items(sym)
                    if _bb:
                        placed_bboxes.append({'ref': _new_ref, **_new_bbox})
                    _placed_syms[i] = sym
                    _res = {'index': i, 'element_type': 'symbol', 'reference': _new_ref, 'lib_id': lib_id}
                    try:
                        _sym_info = sch.library.get_symbol_info(lib_id)
                        if hasattr(_sym_info, 'datasheet') and _sym_info.datasheet:
                            _res['datasheet_url'] = _sym_info.datasheet
                    except:
                        pass
                    if _bb:
                        _res['bbox_mm'] = {'min_x': round(_new_bbox['min_x'], 2), 'max_x': round(_new_bbox['max_x'], 2), 'min_y': round(_new_bbox['min_y'], 2), 'max_y': round(_new_bbox['max_y'], 2)}
                    if _shifted:
                        _res['shifted'] = True
                        _res['shift_mm'] = [round(_shift_dx, 2), round(_shift_dy, 2)]
                    results.append(_res)

            elif element_type == "power":
                lib_id = elem.get("lib_id", "")
                power_name = lib_id
                colon_pos = lib_id.find(':')
                if colon_pos != -1:
                    power_name = lib_id[colon_pos + 1:]
                pos = elem.get("position", [0, 0])
                pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
                pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0
                angle = elem.get("angle", 0.0)

                _check_bounds(pos_x, pos_y, i)
                pwr = sch.labels.add_power(power_name, Vector2.from_xy_mm(pos_x, pos_y), angle=angle)

                # Overlap check + slide-off for power symbol
                _shifted = False
                _shift_dx = 0
                _shift_dy = 0
                _rejected = False
                _bb = None
                _new_bbox = None
                try:
                    _bb = sch.transform.get_bounding_box(pwr, units='mm', include_text=False)
                    if _bb:
                        _raw_bb = dict(_bb)
                        _new_bbox = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                        _has_conflict = any(_bboxes_overlap_add(_new_bbox, _pb) for _pb in placed_bboxes) or (_find_crossing_wire(_raw_bb) is not None)
                        if _has_conflict:
                            _sok, _sdx, _sdy = _slide_off(pwr, _raw_bb, _new_bbox)
                            if _sok and (abs(_sdx) > 1e-6 or abs(_sdy) > 1e-6):
                                sch.transform.move(pwr, delta_x_mm=_sdx, delta_y_mm=_sdy)
                                _bb = sch.transform.get_bounding_box(pwr, units='mm', include_text=False)
                                if _bb:
                                    _new_bbox = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                                _shifted = True
                                _shift_dx = _sdx
                                _shift_dy = _sdy
                            elif not _sok:
                                _rejected = True
                except:
                    pass
                if _rejected:
                    sch.crud.remove_items([pwr])
                    results.append({'index': i, 'error': f'Placement rejected: {_overlap_info(_new_bbox)}. Could not auto-slide to clear position.'})
                else:
                    _pwr_ref = next_ref('#PWR')
                    for _f in pwr._proto.fields:
                        if _f.name == 'Reference':
                            _f.text = _pwr_ref
                            break
                    sch.crud.update_items(pwr)
                    if _bb:
                        placed_bboxes.append({'ref': _pwr_ref, **_new_bbox})
                    _res = {'index': i, 'element_type': 'power', 'reference': _pwr_ref}
                    if _bb:
                        _res['bbox_mm'] = {'min_x': round(_new_bbox['min_x'], 2), 'max_x': round(_new_bbox['max_x'], 2), 'min_y': round(_new_bbox['min_y'], 2), 'max_y': round(_new_bbox['max_y'], 2)}
                    if _shifted:
                        _res['shifted'] = True
                        _res['shift_mm'] = [round(_shift_dx, 2), round(_shift_dy, 2)]
                    results.append(_res)

            elif element_type == "label":
                text = elem.get("text", "")
                label_type = elem.get("label_type", "local")
                pos = elem.get("position", [0, 0])
                pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
                pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0

                _check_bounds(pos_x, pos_y, i)
                pos_vec = Vector2.from_xy_mm(pos_x, pos_y)

                if label_type == "global":
                    lbl = sch.labels.add_global(text, pos_vec)
                elif label_type == "hierarchical":
                    lbl = sch.labels.add_hierarchical(text, pos_vec)
                    has_hierarchical_label = True
                else:
                    lbl = sch.labels.add_local(text, pos_vec)

                # Map angle to label alignment
                angle = int(elem.get("angle", 0.0)) % 360
                if angle != 0:
                    if angle == 180 or angle == 270:
                        lbl._proto.text.attributes.horizontal_alignment = HA_RIGHT
                    if angle == 90 or angle == 270:
                        lbl._proto.text.attributes.vertical_alignment = VA_TOP
                    sch.crud.update_items([lbl])

                # Overlap check for label (no slide-off — moving labels disconnects them)
                _rejected = False
                _bb = None
                _new_bbox = None
                try:
                    _bb = sch.transform.get_bounding_box(lbl, units='mm')
                    if _bb:
                        _new_bbox = {'min_x': _bb['min_x'] + _LABEL_SHRINK, 'max_x': _bb['max_x'] - _LABEL_SHRINK, 'min_y': _bb['min_y'] + _LABEL_SHRINK, 'max_y': _bb['max_y'] - _LABEL_SHRINK}
                        _has_conflict = any(_bboxes_overlap_add(_new_bbox, _pb) for _pb in placed_bboxes) or (_find_crossing_wire(_bb) is not None)
                        if _has_conflict:
                            _rejected = True
                except:
                    pass
                if _rejected:
                    sch.crud.remove_items([lbl])
                    results.append({'index': i, 'error': f'Placement rejected: {_overlap_info(_new_bbox)}. Moving labels would disconnect them \u2014 place the label at a clear position.'})
                else:
                    if _bb:
                        placed_bboxes.append({'ref': text, **_new_bbox})
                    _res = {'index': i, 'element_type': 'label'}
                    if _bb:
                        _res['bbox_mm'] = {'min_x': round(_new_bbox['min_x'], 2), 'max_x': round(_new_bbox['max_x'], 2), 'min_y': round(_new_bbox['min_y'], 2), 'max_y': round(_new_bbox['max_y'], 2)}
                    results.append(_res)

            elif element_type == "wire":
                raw_points = elem.get("points", [])
                if len(raw_points) >= 2:
                    _wpts = []
                    for pt in raw_points:
                        if isinstance(pt, (list, tuple)) and len(pt) >= 2:
                            _wpts.append((snap_to_grid(pt[0]), snap_to_grid(pt[1])))

                    # Bounds check all wire points
                    for _wp in _wpts:
                        _check_bounds(_wp[0], _wp[1], i)

                    _wc = 0
                    for _si in range(len(_wpts) - 1):
                        _w = sch.wiring.add_wire(Vector2.from_xy_mm(*_wpts[_si]), Vector2.from_xy_mm(*_wpts[_si + 1]))
                        _placed_wires.append(_w)
                        _wc += 1
                    results.append({'index': i, 'element_type': 'wire', 'segments': _wc})
                else:
                    results.append({'index': i, 'error': 'Wire requires points array with at least 2 coordinate pairs'})

            elif element_type == "no_connect":
                pos = elem.get("position", [0, 0])
                pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
                pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0

                _check_bounds(pos_x, pos_y, i)
                nc = sch.wiring.add_no_connect(Vector2.from_xy_mm(pos_x, pos_y))
                results.append({'index': i, 'element_type': 'no_connect'})

            elif element_type == "bus_entry":
                pos = elem.get("position", [0, 0])
                pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
                pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0
                direction = elem.get("direction", "right_down")

                _check_bounds(pos_x, pos_y, i)
                be = sch.buses.add_bus_entry(Vector2.from_xy_mm(pos_x, pos_y), direction=direction)
                results.append({'index': i, 'element_type': 'bus_entry'})

            else:
                results.append({'index': i, 'error': f'Unknown element_type: {element_type}'})

        except _OOB:
            pass
        except Exception as e:
            results.append({'index': i, 'error': str(e)})

    # --- Collect pin positions for placed symbols (batch API for efficiency) ---
    for _idx, _sym in _placed_syms.items():
        try:
            _pins = []
            if hasattr(_sym, 'pins'):
                # Use batch API for efficiency
                _pin_map = {}
                try:
                    _all_pins = sch.symbols.get_all_transformed_pin_positions(_sym)
                    for _ap in _all_pins:
                        _pin_map[_ap['pin_number']] = [round(_ap['position'].x / 1_000_000, 4), round(_ap['position'].y / 1_000_000, 4)]
                except:
                    pass

                for _p in _sym.pins:
                    _pin_info = {'number': _p.number, 'name': getattr(_p, 'name', '')}
                    if _p.number in _pin_map:
                        _pin_info['position'] = _pin_map[_p.number]
                    _pins.append(_pin_info)
            for _r in results:
                if _r.get('index') == _idx and 'error' not in _r:
                    _r['pins'] = _pins
                    break
        except:
            pass

    # --- Auto-place junctions for any wires placed in this batch ---
    _junction_count = 0
    if _placed_wires:
        try:
            _junc_positions = sch.wiring.get_needed_junctions(_placed_wires)
            for _jp in _junc_positions:
                sch.wiring.add_junction(_jp)
                _junction_count += 1
        except:
            pass

    _fail = sum(1 for r in results if 'error' in r)
    result = {
        'status': 'success' if _fail == 0 else 'partial',
        'total': len(elements),
        'succeeded': len(results) - _fail,
        'failed': _fail,
        'results': results
    }
    if _junction_count > 0:
        result['junctions_placed'] = _junction_count

except Exception as batch_error:
    result = {'status': 'error', 'message': str(batch_error), 'results': results}

# Sync sheet pins on parent sheet to match hierarchical labels
if has_hierarchical_label:
    try:
        sch.sheets.sync_pins()
    except:
        pass

print(json.dumps(result, indent=2))
