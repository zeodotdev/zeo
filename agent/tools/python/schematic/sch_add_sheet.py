import json, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

def get_id(obj):
    if obj is None:
        return ''
    if hasattr(obj, 'id'):
        id_obj = obj.id
        if hasattr(id_obj, 'value'):
            return str(id_obj.value)
        return str(id_obj)
    if hasattr(obj, 'uuid'):
        return str(obj.uuid)
    if isinstance(obj, str):
        return obj
    return str(obj)

sheet_name = TOOL_ARGS.get("sheet_name", "Subsheet")
sheet_file = TOOL_ARGS.get("sheet_file", "")
raw_pos = TOOL_ARGS.get("position", [0, 0])
pos_x = snap_to_grid(raw_pos[0]) if len(raw_pos) >= 2 else 0
pos_y = snap_to_grid(raw_pos[1]) if len(raw_pos) >= 2 else 0
raw_size = TOOL_ARGS.get("size", [50, 50])
size_w = raw_size[0] if len(raw_size) >= 2 else 50
size_h = raw_size[1] if len(raw_size) >= 2 else 50

try:
    pos = Vector2.from_xy_mm(pos_x, pos_y)
    size = Vector2.from_xy_mm(size_w, size_h)
    sheet = sch.sheets.create(
        name=sheet_name,
        filename=sheet_file if sheet_file else sheet_name + ".kicad_sch",
        position=pos,
        size=size
    )
    result = {'status': 'success', 'source': 'ipc', 'id': get_id(sheet), 'name': sheet_name}

except Exception as e:
    result = {'status': 'error', 'message': str(e)}

print(json.dumps(result, indent=2))
