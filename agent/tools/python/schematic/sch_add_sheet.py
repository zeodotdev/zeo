import json, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

human_path = TOOL_ARGS["sheet_path"]
sheet_file = TOOL_ARGS.get("sheet_file", "")
raw_pos = TOOL_ARGS.get("position", [0, 0])
pos_x = snap_to_grid(raw_pos[0]) if len(raw_pos) >= 2 else 0
pos_y = snap_to_grid(raw_pos[1]) if len(raw_pos) >= 2 else 0
raw_size = TOOL_ARGS.get("size", [50, 50])
size_w = raw_size[0] if len(raw_size) >= 2 else 50
size_h = raw_size[1] if len(raw_size) >= 2 else 50

# Parse human_path into parent path and sheet name
# e.g., "/Parent/Child/" -> parent="/Parent/", name="Child"
# e.g., "/Power Supply/" -> parent="/", name="Power Supply"
parts = [p for p in human_path.split('/') if p]
if not parts:
    print(json.dumps({'status': 'error', 'message': 'human_path must contain a sheet name (e.g., "/Power Supply/")'}))
    sys.exit(0)

sheet_name = parts[-1]
if len(parts) == 1:
    parent_sheet_path = '/'
else:
    parent_sheet_path = '/' + '/'.join(parts[:-1]) + '/'

try:
    pos = Vector2.from_xy_mm(pos_x, pos_y)
    size = Vector2.from_xy_mm(size_w, size_h)
    sheet = sch.sheets.create(
        name=sheet_name,
        filename=sheet_file if sheet_file else sheet_name + ".kicad_sch",
        position=pos,
        size=size,
        parent_sheet_path=parent_sheet_path
    )
    result = {'status': 'success', 'name': sheet_name, 'sheet_path': human_path}

except Exception as e:
    result = {'status': 'error', 'message': str(e)}

print(json.dumps(result, indent=2))
