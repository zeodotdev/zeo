import json, math
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.geometry import Vector2, Angle

# ---------------------------------------------------------------------------
# Slide-off constants (PCB-adapted from sch_add.py)
# ---------------------------------------------------------------------------
_PCB_SLIDE_GRID = 0.5       # mm (finer than schematic's 1.27mm)
_PCB_SLIDE_MAX_ITER = 5
_PCB_SLIDE_MAX_MM = 20.0    # mm (smaller than schematic's 30mm)

def _snap_pcb_grid(v, grid=_PCB_SLIDE_GRID):
    return round(round(v / grid) * grid, 4)

def _fp_layer_str(fp):
    return 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'

def _overlap_info(new_bb, placed_bboxes):
    """Find first overlap and return descriptive string."""
    for pb in placed_bboxes:
        if bboxes_overlap(new_bb, pb):
            ox = min(new_bb['max_x'], pb['max_x']) - max(new_bb['min_x'], pb['min_x'])
            oy = min(new_bb['max_y'], pb['max_y']) - max(new_bb['min_y'], pb['min_y'])
            ref = pb.get('ref', '?')
            return f"Overlaps '{ref}' by {ox:.1f}mm x {oy:.1f}mm"
    return 'Overlaps existing element(s)'

def _pcb_slide_off(raw_bb, margined_bb, placed_bboxes, outline_bb):
    """Slide footprint to clear position via repulsion. Returns (ok, dx, dy)."""
    total_dx, total_dy = 0.0, 0.0
    r_bb = dict(raw_bb)
    m_bb = dict(margined_bb)

    for _iter in range(_PCB_SLIDE_MAX_ITER):
        # Find first overlapping obstacle
        obstacle = None
        for pb in placed_bboxes:
            if bboxes_overlap(m_bb, pb):
                obstacle = pb
                break

        if obstacle is None:
            return (True, total_dx, total_dy)

        # Compute repulsion direction (center-to-center)
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

        # Minimum safe distance using half-widths
        hw_c = (m_bb['max_x'] - m_bb['min_x']) / 2
        hh_c = (m_bb['max_y'] - m_bb['min_y']) / 2
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
        dx = _snap_pcb_grid(new_cx - comp_cx)
        dy = _snap_pcb_grid(new_cy - comp_cy)

        # Ensure at least one grid step
        if abs(dx) < 1e-6 and abs(dy) < 1e-6:
            if abs(dir_x) >= abs(dir_y):
                dx = _PCB_SLIDE_GRID if dir_x >= 0 else -_PCB_SLIDE_GRID
            else:
                dy = _PCB_SLIDE_GRID if dir_y >= 0 else -_PCB_SLIDE_GRID

        total_dx += dx
        total_dy += dy
        if abs(total_dx) > _PCB_SLIDE_MAX_MM or abs(total_dy) > _PCB_SLIDE_MAX_MM:
            return (False, total_dx, total_dy)

        # Shift bboxes for next iteration
        r_bb = {'min_x': r_bb['min_x'] + dx, 'max_x': r_bb['max_x'] + dx,
                'min_y': r_bb['min_y'] + dy, 'max_y': r_bb['max_y'] + dy}
        m_bb = {'min_x': m_bb['min_x'] + dx, 'max_x': m_bb['max_x'] + dx,
                'min_y': m_bb['min_y'] + dy, 'max_y': m_bb['max_y'] + dy}

        # Check outline containment after slide
        if outline_bb and not _pcb_bbox_inside_outline(r_bb, outline_bb):
            return (False, total_dx, total_dy)

    return (False, total_dx, total_dy)


# ---------------------------------------------------------------------------
# Main placement logic
# ---------------------------------------------------------------------------
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

    # Refs being placed in this batch
    batch_refs = set(p.get('ref', '') for p in placements)

    # Collect bboxes for all footprints NOT in the placement batch (same-layer grouped)
    placed_bboxes_by_layer = {'F.Cu': [], 'B.Cu': []}
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if ref and ref not in batch_refs:
            bb = _pcb_fp_bbox(fp)
            if bb:
                mbb = _pcb_fp_bbox_margined(bb)
                layer = _fp_layer_str(fp)
                placed_bboxes_by_layer[layer].append({'ref': ref, **mbb})

    # Get board outline
    outline_bb = _pcb_board_outline_bbox(board)

    # Build net-pad map for ratsnest calculation
    net_pad_map = _pcb_build_net_pad_map(board)

    placed_info = []
    not_found = []

    for p in placements:
        ref = p.get('ref')
        if ref not in ref_to_fp:
            not_found.append(ref)
            continue

        fp = ref_to_fp[ref]
        updated = False

        # Apply position
        if 'position' in p and len(p['position']) >= 2:
            fp.position = Vector2.from_xy(
                round(p['position'][0] * 1e6),
                round(p['position'][1] * 1e6))
            updated = True

        # Apply angle
        if 'angle' in p:
            fp.orientation = Angle.from_degrees(p['angle'])
            updated = True

        # Apply layer (flip)
        if 'layer' in p:
            fp.layer = BoardLayer.BL_B_Cu if p['layer'] == 'B.Cu' else BoardLayer.BL_F_Cu
            updated = True

        if not updated:
            continue

        # Commit to get updated absolute pad positions
        board.update_items([fp])

        # Re-fetch to get updated pad positions
        fp = board.footprints.get_by_reference(ref)
        if not fp:
            not_found.append(ref)
            continue
        ref_to_fp[ref] = fp

        fp_layer = _fp_layer_str(fp)
        placed_bboxes = placed_bboxes_by_layer[fp_layer]

        # Compute bbox
        raw_bb = _pcb_fp_bbox(fp)
        shifted = False
        shift_dx, shift_dy = 0.0, 0.0
        violations = []

        if raw_bb:
            margined_bb = _pcb_fp_bbox_margined(raw_bb)

            # Check same-layer overlap
            has_conflict = any(bboxes_overlap(margined_bb, pb) for pb in placed_bboxes)
            if has_conflict:
                ok, sdx, sdy = _pcb_slide_off(raw_bb, margined_bb, placed_bboxes, outline_bb)
                if ok and (abs(sdx) > 1e-6 or abs(sdy) > 1e-6):
                    # Apply slide
                    new_x = fp.position.x + round(sdx * 1e6)
                    new_y = fp.position.y + round(sdy * 1e6)
                    fp.position = Vector2.from_xy(new_x, new_y)
                    board.update_items([fp])
                    fp = board.footprints.get_by_reference(ref)
                    if fp:
                        ref_to_fp[ref] = fp
                        raw_bb = _pcb_fp_bbox(fp)
                        margined_bb = _pcb_fp_bbox_margined(raw_bb) if raw_bb else None
                    shifted = True
                    shift_dx, shift_dy = sdx, sdy
                elif not ok:
                    violations.append(
                        f'{_overlap_info(margined_bb, placed_bboxes)}; '
                        f'could not auto-slide (tried {sdx:.1f}, {sdy:.1f}mm)')

            # Check outline containment
            if raw_bb and outline_bb and not _pcb_bbox_inside_outline(raw_bb, outline_bb):
                violations.append('Footprint extends outside board outline')

            # Add to same-layer placed_bboxes for subsequent batch items
            if margined_bb:
                placed_bboxes.append({'ref': ref, **margined_bb})

        # Rebuild net-pad map entry for this footprint (positions changed)
        if fp:
            for net_name in list(net_pad_map.keys()):
                net_pad_map[net_name] = [
                    (x, y, r) for x, y, r in net_pad_map[net_name] if r != ref]
            if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):
                for pad in fp.definition.pads:
                    net_name = pad.net.name if hasattr(pad, 'net') else ''
                    if net_name:
                        net_pad_map.setdefault(net_name, []).append(
                            (pad.position.x / 1e6, pad.position.y / 1e6, ref))

        # Build result entry
        fp_info = {
            'ref': ref,
            'position': [fp.position.x / 1e6, fp.position.y / 1e6],
            'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,
            'layer': fp_layer,
            'pads': []
        }
        if raw_bb:
            fp_info['bbox_mm'] = {k: round(v, 2) for k, v in raw_bb.items()}
        if shifted:
            fp_info['shifted'] = True
            fp_info['shift_mm'] = [round(shift_dx, 2), round(shift_dy, 2)]
        if violations:
            fp_info['violations'] = violations

        # Ratsnest distance
        fp_info['ratsnest_mm'] = _pcb_ratsnest_for_fp(fp, net_pad_map)

        # Pad positions
        if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):
            for pad in fp.definition.pads:
                fp_info['pads'].append({
                    'number': str(pad.number) if hasattr(pad, 'number') else '',
                    'position': [pad.position.x / 1e6, pad.position.y / 1e6],
                    'net': pad.net.name if hasattr(pad, 'net') else ''
                })

        placed_info.append(fp_info)

    has_violations = any(fi.get('violations') for fi in placed_info)
    result = {
        'status': 'partial' if has_violations else 'success',
        'placed': placed_info,
        'not_found': not_found
    }
    if outline_bb:
        result['board_outline_mm'] = {k: round(v, 2) for k, v in outline_bb.items()}
    print(json.dumps(result, indent=2))
