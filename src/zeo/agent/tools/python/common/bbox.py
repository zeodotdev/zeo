
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
                bb = sch.transform.get_bounding_box(sym, units='mm', include_text=True)
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


def collect_all_obstacle_bboxes(sch, label_shrink=0.0, exclude_ids=None):
    """Return list of padded bounding boxes for all placed symbols, labels, sheets, and wires.

    Args:
        sch: Schematic API handle
        label_shrink: mm to shrink label bboxes inward (allows label stacking)
        exclude_ids: Set of UUID strings to exclude (e.g., the IC being decorated)

    Returns:
        List of bbox dicts: {id, ref, kind, min_x, max_x, min_y, max_y}
    """
    _exclude = exclude_ids or set()
    bboxes = []

    # Symbols (includes power symbols) - include_text=True to include Reference and Value labels
    try:
        for sym in sch.symbols.get_all():
            uid = get_uuid_str(sym)
            if uid in _exclude:
                continue
            try:
                bb = sch.transform.get_bounding_box(sym, units='mm', include_text=True)
            except Exception:
                continue
            if bb:
                bboxes.append({
                    'id': uid,
                    'ref': getattr(sym, 'reference', '?'),
                    'kind': 'symbol',
                    'min_x': bb['min_x'] - _BBOX_MARGIN,
                    'max_x': bb['max_x'] + _BBOX_MARGIN,
                    'min_y': bb['min_y'] - _BBOX_MARGIN,
                    'max_y': bb['max_y'] + _BBOX_MARGIN,
                })
    except Exception:
        pass

    # Labels (local, global, hierarchical)
    try:
        for lbl in sch.labels.get_all():
            uid = get_uuid_str(lbl)
            if uid in _exclude:
                continue
            try:
                bb = sch.transform.get_bounding_box(lbl, units='mm')
            except Exception:
                continue
            if bb:
                bboxes.append({
                    'id': uid,
                    'ref': getattr(lbl, 'text', '?'),
                    'kind': 'label',
                    'min_x': bb['min_x'] + label_shrink,
                    'max_x': bb['max_x'] - label_shrink,
                    'min_y': bb['min_y'] + label_shrink,
                    'max_y': bb['max_y'] - label_shrink,
                })
    except Exception:
        pass

    # Sheets
    try:
        for sht in sch.crud.get_sheets():
            uid = get_uuid_str(sht)
            if uid in _exclude:
                continue
            try:
                bb = sch.transform.get_bounding_box(sht, units='mm')
            except Exception:
                continue
            if bb:
                bboxes.append({
                    'id': uid,
                    'ref': getattr(sht, 'name', 'sheet'),
                    'kind': 'sheet',
                    'min_x': bb['min_x'] - _BBOX_MARGIN,
                    'max_x': bb['max_x'] + _BBOX_MARGIN,
                    'min_y': bb['min_y'] - _BBOX_MARGIN,
                    'max_y': bb['max_y'] + _BBOX_MARGIN,
                })
    except Exception:
        pass

    # Wires
    try:
        bboxes.extend(wire_segments_to_bboxes(sch.crud.get_wires()))
    except Exception:
        pass

    return bboxes


def wire_segments_to_bboxes(wires):
    """Convert wire objects into thin AABB dicts.

    Zero-width segments (vertical/horizontal) are expanded by 0.01mm so
    overlap detection still works.
    """
    result = []
    for w in wires:
        sx = round(w.start.x / 1e6, 2)
        sy = round(w.start.y / 1e6, 2)
        ex = round(w.end.x / 1e6, 2)
        ey = round(w.end.y / 1e6, 2)
        wbb = {
            'kind': 'wire',
            'min_x': min(sx, ex), 'max_x': max(sx, ex),
            'min_y': min(sy, ey), 'max_y': max(sy, ey),
        }
        if wbb['min_x'] == wbb['max_x']:
            wbb['min_x'] -= 0.01
            wbb['max_x'] += 0.01
        if wbb['min_y'] == wbb['max_y']:
            wbb['min_y'] -= 0.01
            wbb['max_y'] += 0.01
        result.append(wbb)
    return result


class SpatialIndex:
    """Grid-based spatial index for fast bounding-box overlap queries.

    Divides 2D space into cells of ``cell_size`` mm.  Each bbox is registered
    in every cell it touches.  Overlap queries check only the cells touched
    by the query bbox, giving O(1) average-case performance.
    """

    def __init__(self, cell_size=10.0):
        self._cell_size = cell_size
        self._grid = {}   # (col, row) -> list of bbox dicts
        self._all = []     # flat list for iteration

    def _cells_for_bbox(self, bbox):
        cs = self._cell_size
        c0 = int(bbox['min_x'] // cs)
        c1 = int(bbox['max_x'] // cs)
        r0 = int(bbox['min_y'] // cs)
        r1 = int(bbox['max_y'] // cs)
        cells = []
        for c in range(c0, c1 + 1):
            for r in range(r0, r1 + 1):
                cells.append((c, r))
        return cells

    def insert(self, bbox):
        """Add a bbox to the index."""
        self._all.append(bbox)
        for cell in self._cells_for_bbox(bbox):
            self._grid.setdefault(cell, []).append(bbox)

    def any_overlap(self, bbox, eps=0.001):
        """Return True if any indexed bbox overlaps with the given bbox."""
        seen = set()
        for cell in self._cells_for_bbox(bbox):
            for b in self._grid.get(cell, []):
                bid = id(b)
                if bid not in seen:
                    seen.add(bid)
                    if bboxes_overlap(bbox, b, eps):
                        return True
        return False

    def query_overlaps(self, bbox, eps=0.001):
        """Return list of indexed bboxes that overlap with the given bbox."""
        seen = set()
        results = []
        for cell in self._cells_for_bbox(bbox):
            for b in self._grid.get(cell, []):
                bid = id(b)
                if bid not in seen:
                    seen.add(bid)
                    if bboxes_overlap(bbox, b, eps):
                        results.append(b)
        return results


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
