
# ---------------------------------------------------------------------------
# Bounding-box overlap utilities (prepended where needed)
# ---------------------------------------------------------------------------

_BBOX_MARGIN = 1.0  # mm padding per side

def collect_placed_bboxes(sch):
    """Return list of padded bounding boxes for all placed symbols and labels."""
    bboxes = []
    try:
        for sym in sch.symbols.get_all():
            try:
                bb = sch.transform.get_bounding_box(sym, units='mm', include_text=False)
            except Exception:
                continue
            if bb:
                bboxes.append({
                    'id': get_uuid_str(sym),
                    'ref': getattr(sym, 'reference', '?'),
                    'min_x': bb['min_x'] - _BBOX_MARGIN,
                    'max_x': bb['max_x'] + _BBOX_MARGIN,
                    'min_y': bb['min_y'] - _BBOX_MARGIN,
                    'max_y': bb['max_y'] + _BBOX_MARGIN,
                })
    except Exception:
        pass
    try:
        for lbl in sch.labels.get_all():
            try:
                bb = sch.transform.get_bounding_box(lbl, units='mm')
            except Exception:
                continue
            if bb:
                bboxes.append({
                    'id': get_uuid_str(lbl),
                    'ref': getattr(lbl, 'text', '?'),
                    'min_x': bb['min_x'] - _BBOX_MARGIN,
                    'max_x': bb['max_x'] + _BBOX_MARGIN,
                    'min_y': bb['min_y'] - _BBOX_MARGIN,
                    'max_y': bb['max_y'] + _BBOX_MARGIN,
                })
    except Exception:
        pass
    return bboxes


def bboxes_overlap(a, b, eps=0.001):
    """Check if two bounding-box dicts overlap (with epsilon tolerance)."""
    return (a['min_x'] < b['max_x'] - eps and
            a['max_x'] > b['min_x'] + eps and
            a['min_y'] < b['max_y'] - eps and
            a['max_y'] > b['min_y'] + eps)


# ---------------------------------------------------------------------------
# PCB bounding-box and ratsnest utilities
# ---------------------------------------------------------------------------

_PCB_BBOX_MARGIN = 0.5  # mm padding per side (tighter than schematic)

def _pcb_fp_bbox(fp):
    """Compute bounding box of a footprint from its pad positions and sizes.

    Pad positions from the API are already in absolute board coordinates.
    Returns dict {min_x, max_x, min_y, max_y} in mm, or None.
    """
    if not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):
        return None
    pads = list(fp.definition.pads)
    if not pads:
        # Fallback for padless footprints (mounting holes, fiducials)
        if hasattr(fp, 'position'):
            cx, cy = fp.position.x / 1e6, fp.position.y / 1e6
            return {'min_x': cx - 1.0, 'max_x': cx + 1.0,
                    'min_y': cy - 1.0, 'max_y': cy + 1.0}
        return None

    min_x = float('inf')
    max_x = float('-inf')
    min_y = float('inf')
    max_y = float('-inf')

    for pad in pads:
        px = pad.position.x / 1e6
        py = pad.position.y / 1e6
        hw, hh = 0.5, 0.5  # fallback half-sizes in mm
        try:
            if hasattr(pad, 'pad_stack') and hasattr(pad.pad_stack, 'copper_layers'):
                cl = list(pad.pad_stack.copper_layers)
                if cl:
                    hw = cl[0].size.x / 1e6 / 2.0
                    hh = cl[0].size.y / 1e6 / 2.0
        except Exception:
            pass
        min_x = min(min_x, px - hw)
        max_x = max(max_x, px + hw)
        min_y = min(min_y, py - hh)
        max_y = max(max_y, py + hh)

    if min_x == float('inf'):
        return None
    return {'min_x': min_x, 'max_x': max_x, 'min_y': min_y, 'max_y': max_y}


def _pcb_fp_bbox_margined(bbox):
    """Add PCB margin to a raw bbox dict."""
    if bbox is None:
        return None
    return {
        'min_x': bbox['min_x'] - _PCB_BBOX_MARGIN,
        'max_x': bbox['max_x'] + _PCB_BBOX_MARGIN,
        'min_y': bbox['min_y'] - _PCB_BBOX_MARGIN,
        'max_y': bbox['max_y'] + _PCB_BBOX_MARGIN,
    }


def _pcb_board_outline_bbox(board):
    """Compute bounding box of the board outline (Edge.Cuts segments).

    Returns dict {min_x, max_x, min_y, max_y} in mm, or None if no outline.
    """
    from kipy.proto.board.board_types_pb2 import BoardLayer as _BL
    shapes = board.get_shapes()
    edge_pts = []
    for s in shapes:
        if not hasattr(s, 'layer') or s.layer != _BL.BL_Edge_Cuts:
            continue
        if hasattr(s, 'start') and hasattr(s, 'end'):
            edge_pts.append((s.start.x / 1e6, s.start.y / 1e6))
            edge_pts.append((s.end.x / 1e6, s.end.y / 1e6))
    if not edge_pts:
        return None
    xs = [p[0] for p in edge_pts]
    ys = [p[1] for p in edge_pts]
    return {'min_x': min(xs), 'max_x': max(xs), 'min_y': min(ys), 'max_y': max(ys)}


def _pcb_bbox_inside_outline(bbox, outline):
    """Check if bbox is fully inside the board outline bbox."""
    if outline is None:
        return True  # no outline = no constraint
    return (bbox['min_x'] >= outline['min_x'] and
            bbox['max_x'] <= outline['max_x'] and
            bbox['min_y'] >= outline['min_y'] and
            bbox['max_y'] <= outline['max_y'])


def _pcb_build_net_pad_map(board):
    """Build net_name -> [(x_mm, y_mm, ref)] map from all footprint pads."""
    net_pads = {}
    for fp in board.get_footprints():
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if not ref or not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):
            continue
        for pad in fp.definition.pads:
            net_name = pad.net.name if hasattr(pad, 'net') else ''
            if net_name:
                net_pads.setdefault(net_name, []).append(
                    (pad.position.x / 1e6, pad.position.y / 1e6, ref))
    return net_pads


def _pcb_ratsnest_for_fp(fp, net_pad_map):
    """Compute total ratsnest distance for a footprint.

    For each pad, find the nearest same-net pad on a *different* footprint.
    Returns sum of those distances in mm.
    """
    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
    if not ref or not hasattr(fp, 'definition') or not hasattr(fp.definition, 'pads'):
        return 0.0
    total = 0.0
    for pad in fp.definition.pads:
        net_name = pad.net.name if hasattr(pad, 'net') else ''
        if not net_name or net_name not in net_pad_map:
            continue
        px, py = pad.position.x / 1e6, pad.position.y / 1e6
        best = float('inf')
        for ox, oy, oref in net_pad_map[net_name]:
            if oref == ref:
                continue
            d = math.sqrt((px - ox) ** 2 + (py - oy) ** 2)
            if d < best:
                best = d
        if best < float('inf'):
            total += best
    return round(total, 2)
