import json
from kipy.geometry import Vector2

refresh_or_fail(sch)

target = TOOL_ARGS.get("target", "")
new_size = TOOL_ARGS.get("size", None)
new_pos = TOOL_ARGS.get("position", None)

# Find the sheet by name
sheet = None
for _s in sch.crud.get_sheets():
    if _s.name == target:
        sheet = _s
        break

if not sheet:
    print(json.dumps({'status': 'error', 'message': f'Sheet not found: {target}'}))
    import sys; sys.exit(0)

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
    import sys; sys.exit(0)

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
