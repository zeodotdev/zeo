import json, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

target = TOOL_ARGS.get("target", "")
new_size = TOOL_ARGS.get("size", None)
new_pos = TOOL_ARGS.get("position", None)

# Use hierarchy to find the target sheet's parent, then navigate there
# so get_sheets() can see the sheet symbol regardless of current location.
def _find_parent_path(node, target_name, parent_path=None):
    """Recursively search hierarchy for target sheet, return parent's path."""
    name = getattr(node, 'name', '') or ''
    if name == target_name and parent_path is not None:
        return parent_path
    node_path = getattr(node, 'path', None)
    if hasattr(node, 'children'):
        for child in node.children:
            found = _find_parent_path(child, target_name, node_path)
            if found is not None:
                return found
    return None

parent_nav_path = None
if hasattr(sch.sheets, 'get_hierarchy') and hasattr(sch.sheets, 'navigate_to'):
    try:
        hierarchy = sch.sheets.get_hierarchy()
        parent_nav_path = _find_parent_path(hierarchy, target)
        if parent_nav_path is not None:
            tool_log(f'[sch_update_sheet] Navigating to parent sheet of "{target}"')
            sch.sheets.navigate_to(parent_nav_path)
    except Exception as e:
        tool_log(f'[sch_update_sheet] Hierarchy navigation failed: {e}')

# Find the sheet by name on the current (now parent) sheet
sheet = None
for _s in sch.crud.get_sheets():
    if _s.name == target:
        sheet = _s
        break

if not sheet:
    print(json.dumps({'status': 'error', 'message': f'Sheet not found: {target}. Make sure the sheet exists in the hierarchy.'}))
    sys.exit(0)

changed = False

# Apply position change (move fields and pins by the same delta)
if isinstance(new_pos, list) and len(new_pos) >= 2:
    px = snap_to_grid(new_pos[0])
    py = snap_to_grid(new_pos[1])
    dx_nm = round(px * 1_000_000) - sheet._proto.position.x_nm
    dy_nm = round(py * 1_000_000) - sheet._proto.position.y_nm
    sheet._proto.position.x_nm += dx_nm
    sheet._proto.position.y_nm += dy_nm
    for _f in sheet._proto.fields:
        _f.position.x_nm += dx_nm
        _f.position.y_nm += dy_nm
    for _p in sheet._proto.pins:
        _p.position.x_nm += dx_nm
        _p.position.y_nm += dy_nm
    changed = True

# Apply size change (reposition fields relative to new sheet edges)
if isinstance(new_size, list) and len(new_size) >= 2:
    sw = new_size[0]
    sh = new_size[1]
    sheet._proto.size.x_nm = round(sw * 1_000_000)
    sheet._proto.size.y_nm = round(sh * 1_000_000)
    # Redistribute pins evenly along their edges
    _sheet_x = sheet._proto.position.x_nm / 1_000_000
    _sheet_y = sheet._proto.position.y_nm / 1_000_000
    _pin_margin = 2.54
    _raw_pins = list(sheet._proto.pins)
    if _raw_pins:
        _sides = {}
        for _rp in _raw_pins:
            _sides.setdefault(_rp.side, []).append(_rp)
        for _side, _side_pins in _sides.items():
            n = len(_side_pins)
            if _side in (1, 'left') or _side in (2, 'right'):
                _x_nm = round((_sheet_x if _side in (1, 'left') else _sheet_x + sw) * 1_000_000)
                _y_start = _sheet_y + _pin_margin
                _y_end = _sheet_y + sh - _pin_margin
                _step = (_y_end - _y_start) / max(n - 1, 1) if n > 1 else 0
                for _idx, _rp in enumerate(_side_pins):
                    _rp.position.x_nm = _x_nm
                    _rp.position.y_nm = round(snap_to_grid(_y_start + _idx * _step) * 1_000_000)
            elif _side in (3, 'top') or _side in (4, 'bottom'):
                _y_nm = round((_sheet_y if _side in (3, 'top') else _sheet_y + sh) * 1_000_000)
                _x_start = _sheet_x + _pin_margin
                _x_end = _sheet_x + sw - _pin_margin
                _step = (_x_end - _x_start) / max(n - 1, 1) if n > 1 else 0
                for _idx, _rp in enumerate(_side_pins):
                    _rp.position.x_nm = round(snap_to_grid(_x_start + _idx * _step) * 1_000_000)
                    _rp.position.y_nm = _y_nm
    # Autoplace fields to match KiCad native behavior:
    # Sheet name: above sheet at pos + (0, -margin)
    # Sheet file: below sheet at pos + (0, size.y + margin)
    _top_nm = sheet._proto.position.y_nm
    _bot_nm = sheet._proto.position.y_nm + sheet._proto.size.y_nm
    _left_nm = sheet._proto.position.x_nm
    _margin = round(1.0 * 1_000_000)  # ~1mm approximation of borderMargin + text offset
    for _f in sheet._proto.fields:
        _f.position.x_nm = _left_nm
        if _f.name == 'Sheetname' or _f.id_int == 7:
            _f.position.y_nm = _top_nm - _margin
        elif _f.name == 'Sheetfile' or _f.id_int == 8:
            _f.position.y_nm = _bot_nm + _margin
    changed = True

if not changed:
    print(json.dumps({'status': 'error', 'message': 'No size or position provided'}))
    sys.exit(0)

# Send update to editor
result_items = sch.crud.update_items([sheet])

# Build response
sheet_x = sheet._proto.position.x_nm / 1_000_000
sheet_y = sheet._proto.position.y_nm / 1_000_000
sheet_w = sheet._proto.size.x_nm / 1_000_000
sheet_h = sheet._proto.size.y_nm / 1_000_000

result = {
    'status': 'success',
    'target': target,
    'position': [round(sheet_x, 2), round(sheet_y, 2)],
    'size': [round(sheet_w, 2), round(sheet_h, 2)],
}

print(json.dumps(result, indent=2))
