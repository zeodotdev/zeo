"""
pcb_place_companions - Place companion footprints near their associated IC.

Uses net connectivity to identify which companion pads connect to which IC pads,
then places companions at optimal positions minimizing ratsnest distance.
Supports bypass/decoupling caps, pull-up/down resistors, and other supplemental
components that should be placed close to their associated IC pins.
"""
import json, math
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.geometry import Vector2, Angle

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
GRID_MM = 0.5            # PCB placement grid
MIN_OFFSET_MM = 2.0      # Minimum distance from IC center
MAX_OFFSET_MM = 20.0     # Maximum search distance
SEARCH_STEP_MM = 0.5     # Grid step for expanding search
MAX_PERP_STEPS = 12      # Max perpendicular search steps
PERP_STEP_MM = 1.0       # Perpendicular step size

# ---------------------------------------------------------------------------
# Parse input
# ---------------------------------------------------------------------------
ic_ref = TOOL_ARGS.get('ic_ref', '')
companions_input = TOOL_ARGS.get('companions', [])

if not ic_ref:
    print(json.dumps({'status': 'error', 'message': 'ic_ref is required'}))
    raise SystemExit()

if not companions_input:
    print(json.dumps({'status': 'error', 'message': 'companions array is required'}))
    raise SystemExit()

# ---------------------------------------------------------------------------
# Gather board state
# ---------------------------------------------------------------------------
all_fps = board.get_footprints()
ref_to_fp = {}
for fp in all_fps:
    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
    if ref:
        ref_to_fp[ref] = fp

ic_fp = ref_to_fp.get(ic_ref)
if not ic_fp:
    print(json.dumps({'status': 'error', 'message': f'IC footprint not found: {ic_ref}'}))
    raise SystemExit()

ic_layer = 'B.Cu' if ic_fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'

# Build IC pad map: pad_number -> {x, y, net}
ic_pads = {}
if hasattr(ic_fp, 'definition') and hasattr(ic_fp.definition, 'pads'):
    for pad in ic_fp.definition.pads:
        pnum = str(pad.number) if hasattr(pad, 'number') else ''
        net = pad.net.name if hasattr(pad, 'net') else ''
        if pnum:
            ic_pads[pnum] = {
                'x': pad.position.x / 1e6,
                'y': pad.position.y / 1e6,
                'net': net
            }

# IC bounding box
ic_bb = _pcb_fp_bbox(ic_fp)
if not ic_bb:
    print(json.dumps({'status': 'error', 'message': f'Could not compute bounding box for {ic_ref}'}))
    raise SystemExit()

ic_bb_margined = _pcb_fp_bbox_margined(ic_bb)

# Board outline
outline_bb = _pcb_board_outline_bbox(board)

# Net-pad map for ratsnest scoring
net_pad_map = _pcb_build_net_pad_map(board)

# Collect existing bboxes for same-layer overlap (exclude companions being placed)
companion_refs = set(c.get('ref', '') for c in companions_input)
placed_bboxes = []
for fp in all_fps:
    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
    if not ref or ref in companion_refs:
        continue
    layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'
    if layer != ic_layer:
        continue
    bb = _pcb_fp_bbox(fp)
    if bb:
        mbb = _pcb_fp_bbox_margined(bb)
        placed_bboxes.append({'ref': ref, **mbb})


def _snap(v):
    return round(round(v / GRID_MM) * GRID_MM, 4)


def _find_anchor_pad(comp_fp, ic_pads_by_net):
    """Find the IC pad that shares a net with the companion.
    Returns (ic_pad_x, ic_pad_y, shared_net) or None.
    """
    if not hasattr(comp_fp, 'definition') or not hasattr(comp_fp.definition, 'pads'):
        return None
    best = None
    for pad in comp_fp.definition.pads:
        net = pad.net.name if hasattr(pad, 'net') else ''
        if net and net in ic_pads_by_net:
            # Prefer non-power nets for anchor (power nets like GND connect to many things)
            net_upper = net.upper()
            is_power = any(p in net_upper for p in ('GND', 'VCC', 'VDD', 'VSS', '3V3', '5V', '3.3V', '1.8V'))
            if best is None or (is_power and not best[3]):
                ic_pad = ic_pads_by_net[net]
                best = (ic_pad['x'], ic_pad['y'], net, is_power)
    return best[:3] if best else None


def _try_position(cx, cy, comp_fp, placed_bboxes, outline_bb):
    """Try placing companion at (cx, cy). Returns True if position is clear."""
    # Temporarily move to compute bbox
    comp_fp.position = Vector2.from_xy(round(cx * 1e6), round(cy * 1e6))
    board.update_items([comp_fp])

    ref = comp_fp.reference_field.text.value
    updated_fp = board.footprints.get_by_reference(ref)
    if not updated_fp:
        return False, None, None

    raw_bb = _pcb_fp_bbox(updated_fp)
    if not raw_bb:
        return False, None, None

    margined_bb = _pcb_fp_bbox_margined(raw_bb)

    # Check IC overlap
    if bboxes_overlap(margined_bb, ic_bb_margined):
        return False, updated_fp, raw_bb

    # Check other component overlaps
    for pb in placed_bboxes:
        if bboxes_overlap(margined_bb, pb):
            return False, updated_fp, raw_bb

    # Check outline containment
    if outline_bb and not _pcb_bbox_inside_outline(raw_bb, outline_bb):
        return False, updated_fp, raw_bb

    return True, updated_fp, raw_bb


def _score_position(comp_fp, net_pad_map):
    """Score a position by total ratsnest distance (lower = better)."""
    return _pcb_ratsnest_for_fp(comp_fp, net_pad_map)


# Build IC net -> pad lookup
ic_pads_by_net = {}
for pnum, pinfo in ic_pads.items():
    if pinfo['net']:
        ic_pads_by_net[pinfo['net']] = pinfo

# ---------------------------------------------------------------------------
# Process each companion
# ---------------------------------------------------------------------------
results = []

for i, comp_input in enumerate(companions_input):
    comp_ref = comp_input.get('ref', '')
    angle = comp_input.get('angle', None)

    if not comp_ref:
        results.append({'index': i, 'error': 'ref is required'})
        continue

    comp_fp = ref_to_fp.get(comp_ref)
    if not comp_fp:
        results.append({'index': i, 'ref': comp_ref, 'error': f'Footprint not found: {comp_ref}'})
        continue

    # Apply angle if specified
    if angle is not None:
        comp_fp.orientation = Angle.from_degrees(angle)
        board.update_items([comp_fp])
        comp_fp = board.footprints.get_by_reference(comp_ref)
        ref_to_fp[comp_ref] = comp_fp

    # Find anchor: IC pad that shares a net with this companion
    anchor = _find_anchor_pad(comp_fp, ic_pads_by_net)

    if anchor:
        anchor_x, anchor_y, anchor_net = anchor
    else:
        # Fallback: place near IC center
        anchor_x = (ic_bb['min_x'] + ic_bb['max_x']) / 2
        anchor_y = (ic_bb['min_y'] + ic_bb['max_y']) / 2
        anchor_net = None

    # Expanding search: try positions in concentric rings around anchor pad
    best_pos = None
    best_score = float('inf')
    best_fp = None
    best_bb = None

    offset = MIN_OFFSET_MM
    while offset <= MAX_OFFSET_MM:
        for perp_idx in range(MAX_PERP_STEPS):
            if perp_idx == 0:
                perp = 0
            else:
                mult = (perp_idx + 1) // 2
                sign = 1 if perp_idx % 2 == 1 else -1
                perp = mult * PERP_STEP_MM * sign

            # Try 4 cardinal directions from anchor
            candidates = [
                (_snap(anchor_x + offset), _snap(anchor_y + perp)),
                (_snap(anchor_x - offset), _snap(anchor_y + perp)),
                (_snap(anchor_x + perp),   _snap(anchor_y + offset)),
                (_snap(anchor_x + perp),   _snap(anchor_y - offset)),
            ]

            for cx, cy in candidates:
                clear, updated_fp, raw_bb = _try_position(
                    cx, cy, comp_fp, placed_bboxes, outline_bb)
                if not clear:
                    continue

                score = _score_position(updated_fp, net_pad_map)
                if score < best_score:
                    best_score = score
                    best_pos = (cx, cy)
                    best_fp = updated_fp
                    best_bb = raw_bb

            # Early exit: if we found something at this offset, no need to go further
            if best_pos is not None:
                break

        if best_pos is not None:
            break
        offset += SEARCH_STEP_MM

    if best_pos is None:
        # Fallback: place adjacent to IC with minimum offset
        best_pos = (_snap(anchor_x + MIN_OFFSET_MM), _snap(anchor_y))
        comp_fp.position = Vector2.from_xy(
            round(best_pos[0] * 1e6), round(best_pos[1] * 1e6))
        board.update_items([comp_fp])
        best_fp = board.footprints.get_by_reference(comp_ref)
        best_bb = _pcb_fp_bbox(best_fp) if best_fp else None
        best_score = _score_position(best_fp, net_pad_map) if best_fp else 0
        violation = 'No clear position found; placed at fallback'
    else:
        # Move to best position (may already be there from _try_position)
        comp_fp = ref_to_fp.get(comp_ref) or comp_fp
        cur_x = comp_fp.position.x / 1e6
        cur_y = comp_fp.position.y / 1e6
        if abs(cur_x - best_pos[0]) > 0.001 or abs(cur_y - best_pos[1]) > 0.001:
            comp_fp.position = Vector2.from_xy(
                round(best_pos[0] * 1e6), round(best_pos[1] * 1e6))
            board.update_items([comp_fp])
            best_fp = board.footprints.get_by_reference(comp_ref)
            best_bb = _pcb_fp_bbox(best_fp) if best_fp else None
        violation = None

    # Update ref_to_fp and net_pad_map
    if best_fp:
        ref_to_fp[comp_ref] = best_fp
        # Update net_pad_map
        for net_name in list(net_pad_map.keys()):
            net_pad_map[net_name] = [
                (x, y, r) for x, y, r in net_pad_map[net_name] if r != comp_ref]
        if hasattr(best_fp, 'definition') and hasattr(best_fp.definition, 'pads'):
            for pad in best_fp.definition.pads:
                net_name = pad.net.name if hasattr(pad, 'net') else ''
                if net_name:
                    net_pad_map.setdefault(net_name, []).append(
                        (pad.position.x / 1e6, pad.position.y / 1e6, comp_ref))

    # Add to placed_bboxes so subsequent companions avoid this one
    if best_bb:
        mbb = _pcb_fp_bbox_margined(best_bb)
        if mbb:
            placed_bboxes.append({'ref': comp_ref, **mbb})

    # Build result
    comp_result = {
        'index': i,
        'ref': comp_ref,
        'position': [round(best_pos[0], 2), round(best_pos[1], 2)],
        'ratsnest_mm': best_score,
        'pads': []
    }
    if anchor_net:
        comp_result['anchor_net'] = anchor_net
        comp_result['anchor_pad'] = [round(anchor_x, 2), round(anchor_y, 2)]
    if best_bb:
        comp_result['bbox_mm'] = {k: round(v, 2) for k, v in best_bb.items()}
    if violation:
        comp_result['violation'] = violation

    # Pad positions
    fp_for_pads = best_fp or comp_fp
    if hasattr(fp_for_pads, 'definition') and hasattr(fp_for_pads.definition, 'pads'):
        for pad in fp_for_pads.definition.pads:
            comp_result['pads'].append({
                'number': str(pad.number) if hasattr(pad, 'number') else '',
                'position': [pad.position.x / 1e6, pad.position.y / 1e6],
                'net': pad.net.name if hasattr(pad, 'net') else ''
            })

    results.append(comp_result)

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
failed = sum(1 for r in results if 'error' in r)
output = {
    'status': 'success' if failed == 0 else ('partial' if failed < len(results) else 'error'),
    'ic_ref': ic_ref,
    'ic_position': [round(ic_fp.position.x / 1e6, 2), round(ic_fp.position.y / 1e6, 2)],
    'companions_placed': len(results) - failed,
    'companions_failed': failed,
    'results': results
}
if outline_bb:
    output['board_outline_mm'] = {k: round(v, 2) for k, v in outline_bb.items()}
print(json.dumps(output, indent=2))
