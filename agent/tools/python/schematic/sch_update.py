import json, re, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

updates = TOOL_ARGS.get("updates", [])
file_path = TOOL_ARGS.get("file_path", "")
results = []

# Overlap detection: only collect bboxes if any update has position
_has_position_update = any(
    isinstance(u.get("position"), list) and len(u.get("position", [])) >= 2
    for u in updates
)

if _has_position_update:
    # Use collect_placed_bboxes from bbox.py preamble
    placed_bboxes = collect_placed_bboxes(sch)

try:
    for i, update in enumerate(updates):
        target = update.get("target", "")
        if not target:
            results.append({'index': i, 'error': 'target is required'})
            continue

        try:
            is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))
            item = None
            if is_uuid:
                items = sch.crud.get_by_id([target])
                if items:
                    item = items[0]
            else:
                item = sch.symbols.get_by_ref(target)
            if not item:
                raise ValueError(f'Element not found: {target}')
            updated = False

            # --- Position update ---
            pos = update.get("position")
            if isinstance(pos, list) and len(pos) >= 2:
                pos_x = snap_to_grid(pos[0])
                pos_y = snap_to_grid(pos[1])

                # Capture old symbol position and pin positions before move (for wire dragging)
                _old_sym_x = round(item.position.x / 1e6, 2)
                _old_sym_y = round(item.position.y / 1e6, 2)
                _old_pins = []
                for _pin in item.pins:
                    try:
                        _tp = sch.symbols.get_transformed_pin_position(item, _pin.number)
                        if _tp:
                            _old_pins.append((round(_tp['position'].x / 1e6, 2), round(_tp['position'].y / 1e6, 2)))
                    except: pass

                # Move the symbol
                new_pos = Vector2.from_xy_mm(pos_x, pos_y)
                item = sch.symbols.move(item, new_pos)

                # Overlap detection: check new position against all existing (excluding self)
                _overlap = False
                _item_id = str(item.id.value)
                _obstacle_ref = '?'
                try:
                    _bb = sch.transform.get_bounding_box(item, units='mm', include_text=False)
                    if _bb:
                        _new_bbox = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                        for _pb in placed_bboxes:
                            if _pb.get('id') == _item_id:
                                continue
                            if bboxes_overlap(_new_bbox, _pb):
                                _overlap = True
                                _obstacle_ref = _pb.get('ref', '?')
                                break
                except:
                    pass
                if _overlap:
                    item = sch.symbols.move(item, Vector2.from_xy_mm(_old_sym_x, _old_sym_y))
                    raise ValueError(f'Move rejected: overlaps {_obstacle_ref}')

                # Update placed_bboxes with new position
                if _bb:
                    for _idx_pb, _pb in enumerate(placed_bboxes):
                        if _pb.get('id') == _item_id:
                            placed_bboxes[_idx_pb] = {'id': _item_id, 'min_x': _new_bbox['min_x'], 'max_x': _new_bbox['max_x'], 'min_y': _new_bbox['min_y'], 'max_y': _new_bbox['max_y']}
                            break

                # Compute move delta from known target position (avoids stale position reads)
                _dx = round(pos_x - _old_sym_x, 4)
                _dy = round(pos_y - _old_sym_y, 4)
                # Build pos_map by applying delta to old pin positions
                _pos_map = {}
                if abs(_dx) > 0.001 or abs(_dy) > 0.001:
                    for _op in _old_pins:
                        _np = (round(_op[0] + _dx, 2), round(_op[1] + _dy, 2))
                        _pos_map[_op] = _np
                if _pos_map:
                    _rnd = lambda v: round(v, 2)
                    def _mpos(p):
                        for k, v in _pos_map.items():
                            if abs(p[0]-k[0]) < 0.05 and abs(p[1]-k[1]) < 0.05:
                                return v
                        return None
                    # Pre-scan for overshoot: if a pin moves past the far end of a wire,
                    # add the far end to pos_map so the connected wire gets an L-bend instead
                    for _w in sch.crud.get_wires():
                        _ws2 = (_rnd(_w.start.x / 1e6), _rnd(_w.start.y / 1e6))
                        _we2 = (_rnd(_w.end.x / 1e6), _rnd(_w.end.y / 1e6))
                        _ns2 = _mpos(_ws2)
                        _ne2 = _mpos(_we2)
                        if _ns2 and not _ne2:
                            if abs(_ws2[0]-_we2[0]) < 0.01 and (_ws2[1]-_we2[1])*(_ns2[1]-_we2[1]) < 0:
                                _pos_map[_we2] = (_we2[0], _ns2[1])
                            elif abs(_ws2[1]-_we2[1]) < 0.01 and (_ws2[0]-_we2[0])*(_ns2[0]-_we2[0]) < 0:
                                _pos_map[_we2] = (_ns2[0], _we2[1])
                        elif _ne2 and not _ns2:
                            if abs(_ws2[0]-_we2[0]) < 0.01 and (_we2[1]-_ws2[1])*(_ne2[1]-_ws2[1]) < 0:
                                _pos_map[_ws2] = (_ws2[0], _ne2[1])
                            elif abs(_ws2[1]-_we2[1]) < 0.01 and (_we2[0]-_ws2[0])*(_ne2[0]-_ws2[0]) < 0:
                                _pos_map[_ws2] = (_ne2[0], _ws2[1])
                    # Update wires with orthogonal bend routing (like KiCad drag)
                    _all_w = sch.crud.get_wires()
                    _rm_w = []
                    _add_segs = []
                    for _w in _all_w:
                        _ws = (_rnd(_w.start.x / 1e6), _rnd(_w.start.y / 1e6))
                        _we = (_rnd(_w.end.x / 1e6), _rnd(_w.end.y / 1e6))
                        _ns = _mpos(_ws)
                        _ne = _mpos(_we)
                        if _ns or _ne:
                            _rm_w.append(_w)
                            _s = _ns or _ws
                            _e = _ne or _we
                            if abs(_s[0] - _e[0]) < 0.01 or abs(_s[1] - _e[1]) < 0.01:
                                _add_segs.append((_s, _e))
                            else:
                                _horiz = abs(_ws[1] - _we[1]) < abs(_ws[0] - _we[0])
                                if (_ns and _horiz) or (_ne and not _horiz):
                                    _c = (_e[0], _s[1])
                                else:
                                    _c = (_s[0], _e[1])
                                _add_segs.append((_s, _c))
                                _add_segs.append((_c, _e))
                        # Also check for wires passing THROUGH old pin positions (pin between start/end)
                        elif not _ns and not _ne:
                            _is_h = abs(_ws[1] - _we[1]) < 0.01
                            _is_v = abs(_ws[0] - _we[0]) < 0.01
                            if _is_h or _is_v:
                                for _op, _np in _pos_map.items():
                                    if _is_h and abs(_op[1] - _ws[1]) < 0.01:
                                        _lo = min(_ws[0], _we[0])
                                        _hi = max(_ws[0], _we[0])
                                        if _lo + 0.01 < _op[0] < _hi - 0.01:
                                            _rm_w.append(_w)
                                            _add_segs.append((_ws, _np))
                                            _add_segs.append((_np, _we))
                                            break
                                    elif _is_v and abs(_op[0] - _ws[0]) < 0.01:
                                        _lo = min(_ws[1], _we[1])
                                        _hi = max(_ws[1], _we[1])
                                        if _lo + 0.01 < _op[1] < _hi - 0.01:
                                            _rm_w.append(_w)
                                            _add_segs.append((_ws, _np))
                                            _add_segs.append((_np, _we))
                                            break
                    # Filter out zero-length segments (from overshoot collapsing)
                    _add_segs = [seg for seg in _add_segs if abs(seg[0][0]-seg[1][0]) > 0.01 or abs(seg[0][1]-seg[1][1]) > 0.01]
                    if _rm_w:
                        sch.crud.remove_items(_rm_w)
                        for (_s, _e) in _add_segs:
                            sch.wiring.add_wire(Vector2.from_xy_mm(_s[0], _s[1]), Vector2.from_xy_mm(_e[0], _e[1]))
                    # Update junctions
                    _all_j = sch.crud.get_junctions()
                    _rm_j = []
                    _add_j = []
                    for _j in _all_j:
                        _jp = (_rnd(_j.position.x / 1e6), _rnd(_j.position.y / 1e6))
                        _jm = _mpos(_jp)
                        if _jm:
                            _rm_j.append(_j)
                            _add_j.append(_jm)
                    if _rm_j:
                        sch.crud.remove_items(_rm_j)
                        for _np in _add_j:
                            sch.wiring.add_junction(Vector2.from_xy_mm(_np[0], _np[1]))
                    # Update no-connects
                    _all_nc = sch.crud.get_no_connects()
                    _rm_nc = []
                    _add_nc = []
                    for _nc in _all_nc:
                        _ncp = (_rnd(_nc.position.x / 1e6), _rnd(_nc.position.y / 1e6))
                        _ncm = _mpos(_ncp)
                        if _ncm:
                            _rm_nc.append(_nc)
                            _add_nc.append(_ncm)
                    if _rm_nc:
                        sch.crud.remove_items(_rm_nc)
                        for _np in _add_nc:
                            sch.wiring.add_no_connect(Vector2.from_xy_mm(_np[0], _np[1]))

                updated = True

            # --- Angle update ---
            if "angle" in update:
                desired_angle = update["angle"]
                current_angle = getattr(item, 'angle', 0) or 0
                delta = (desired_angle - current_angle) % 360
                if delta != 0:
                    item = sch.symbols.rotate(item, delta)
                updated = True

            # --- Properties update ---
            props = update.get("properties")
            if isinstance(props, dict):
                if 'Value' in props:
                    sch.symbols.set_value(item, props['Value'])
                    updated = True
                if 'Footprint' in props:
                    sch.symbols.set_footprint(item, props['Footprint'])
                    updated = True

            # --- Fields update ---
            fields = update.get("fields")
            if isinstance(fields, dict):
                sym_pos = item.position
                for field_name, field_spec in fields.items():
                    if not isinstance(field_spec, dict):
                        continue
                    has_offset = isinstance(field_spec.get("offset"), list) and len(field_spec.get("offset", [])) >= 2
                    has_angle = isinstance(field_spec.get("angle"), (int, float))
                    if not has_offset and not has_angle:
                        continue
                    for _f in item._proto.fields:
                        if _f.name == field_name:
                            if has_offset:
                                dx = snap_to_grid(field_spec["offset"][0])
                                dy = snap_to_grid(field_spec["offset"][1])
                                _f.position.x_nm = sym_pos.x + round(dx * 1_000_000)
                                _f.position.y_nm = sym_pos.y + round(dy * 1_000_000)
                            if has_angle:
                                _f.attributes.angle.value_degrees = field_spec["angle"]
                            break
                _upd = sch.crud.update_items(item)
                if _upd:
                    item = _upd[0]
                updated = True

            # Build state info
            state = {}
            if hasattr(item, 'position'):
                pos = item.position
                state['position'] = [pos.x / 1_000_000, pos.y / 1_000_000]
            if hasattr(item, 'angle'):
                state['angle'] = item.angle
            if hasattr(item, 'reference'):
                state['reference'] = item.reference
            results.append({'index': i, 'target': target, 'updated': updated, 'state': state})
        except Exception as e:
            results.append({'index': i, 'target': target, 'error': str(e)})

    _fail = sum(1 for r in results if 'error' in r)
    result = {
        'status': 'success' if _fail == 0 else 'partial',
        'source': 'ipc',
        'total': len(updates),
        'succeeded': len(results) - _fail,
        'failed': _fail,
        'results': results
    }

except Exception as batch_error:
    result = {'status': 'error', 'message': str(batch_error), 'results': results}

print(json.dumps(result, indent=2))
