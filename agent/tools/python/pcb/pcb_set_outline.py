import json
from kipy.board_types import BoardSegment
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.geometry import Vector2

shape = TOOL_ARGS.get("shape", "rectangle")
clear_existing = TOOL_ARGS.get("clear_existing", True)

def mm_to_nm(mm):
    return round(mm * 1000000)

if clear_existing:
    # Remove existing board outline (shapes on Edge.Cuts)
    shapes = board.get_shapes()
    edge_items = [s for s in shapes if hasattr(s, 'layer') and s.layer == BoardLayer.BL_Edge_Cuts]
    if edge_items:
        board.remove_items(edge_items)
        print(f'Removed {len(edge_items)} existing outline segment(s)')

if shape == "rectangle":
    width = TOOL_ARGS.get("width", 100.0)
    height = TOOL_ARGS.get("height", 80.0)
    origin = TOOL_ARGS.get("origin", [0, 0])
    origin_x = mm_to_nm(origin[0] if len(origin) >= 1 else 0)
    origin_y = mm_to_nm(origin[1] if len(origin) >= 2 else 0)
    width_nm = mm_to_nm(width)
    height_nm = mm_to_nm(height)

    corners = [
        (origin_x, origin_y),
        (origin_x + width_nm, origin_y),
        (origin_x + width_nm, origin_y + height_nm),
        (origin_x, origin_y + height_nm)
    ]

    segments = []
    for i in range(4):
        seg = BoardSegment()
        seg.layer = BoardLayer.BL_Edge_Cuts
        seg.start = Vector2.from_xy(corners[i][0], corners[i][1])
        seg.end = Vector2.from_xy(corners[(i+1) % 4][0], corners[(i+1) % 4][1])
        segments.append(seg)

    board.create_items(segments)
    print(json.dumps({'status': 'success', 'message': f'Created rectangular outline: {width}mm x {height}mm'}))

elif shape == "polygon":
    points_input = TOOL_ARGS.get("points", [])
    if not points_input:
        print(json.dumps({'status': 'error', 'message': 'polygon shape requires points array'}))
    else:
        points = [(mm_to_nm(pt[0]), mm_to_nm(pt[1])) for pt in points_input if len(pt) >= 2]

        segments = []
        for i in range(len(points)):
            seg = BoardSegment()
            seg.layer = BoardLayer.BL_Edge_Cuts
            seg.start = Vector2.from_xy(points[i][0], points[i][1])
            seg.end = Vector2.from_xy(points[(i+1) % len(points)][0], points[(i+1) % len(points)][1])
            segments.append(seg)

        board.create_items(segments)
        print(json.dumps({'status': 'success', 'message': f'Created polygon outline with {len(points)} vertices'}))

elif shape == "rounded_rectangle":
    import math
    width = TOOL_ARGS.get("width", 100.0)
    height = TOOL_ARGS.get("height", 80.0)
    radius = TOOL_ARGS.get("corner_radius", 5.0)
    origin = TOOL_ARGS.get("origin", [0, 0])
    ox = mm_to_nm(origin[0] if len(origin) >= 1 else 0)
    oy = mm_to_nm(origin[1] if len(origin) >= 2 else 0)
    w = mm_to_nm(width)
    h = mm_to_nm(height)
    r = mm_to_nm(radius)
    # Clamp radius to half the smallest dimension to prevent inverted geometry
    max_r = min(w, h) // 2
    if r > max_r:
        r = max_r
    n = 8  # points per corner arc
    points = []
    # Four corners: TR, BR, BL, TL (clockwise traversal)
    corners = [(ox+w-r, oy+r, 270), (ox+w-r, oy+h-r, 0), (ox+r, oy+h-r, 90), (ox+r, oy+r, 180)]
    for cx, cy, start_deg in corners:
        for i in range(n):
            a = math.radians(start_deg + 90.0 * i / n)
            points.append((int(cx + r * math.cos(a)), int(cy + r * math.sin(a))))

    segments = []
    for i in range(len(points)):
        seg = BoardSegment()
        seg.layer = BoardLayer.BL_Edge_Cuts
        seg.start = Vector2.from_xy(points[i][0], points[i][1])
        seg.end = Vector2.from_xy(points[(i+1) % len(points)][0], points[(i+1) % len(points)][1])
        segments.append(seg)
    board.create_items(segments)
    print(json.dumps({'status': 'success', 'message': f'Created rounded rectangle: {width}mm x {height}mm, corner radius {radius}mm'}))
