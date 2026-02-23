
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
