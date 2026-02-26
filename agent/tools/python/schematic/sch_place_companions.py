"""
sch_place_companions - Place companion components adjacent to IC pins.

Companion circuits are small supporting parts (decoupling caps, pull-up/down resistors,
termination resistors, filter caps, LED indicators) that wire directly to specific IC pins.

The tool calculates optimal positions based on IC pin geometry and orientation:
- Gets IC pin position and escape direction via get_transformed_pin_position()
- Places companion symbol adjacent to pin (offset by N grid units in escape direction)
- Draws short wire stub from IC pin to companion pin
- Adds power symbols or text labels at companion terminals as specified
"""
import json, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
GRID_MM = 1.27           # 50 mil grid
BBOX_MARGIN = 0.5        # Margin around components for spacing
MAX_OFFSET_GRIDS = 15    # Max push-out distance
MIN_OFFSET_GRIDS = 3     # Minimum distance from IC (3.81mm)
COMP_HALF_LEN = 3.81     # Half-length of 2-pin passive (center to pin)
TERMINAL_EXTENSION = 5.08 # Terminal area for power symbols/labels
LABEL_CHAR_WIDTH = 1.0   # Approximate width per character in mm
LABEL_HEIGHT = 2.5       # Approximate label height in mm

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
# State tracking
# ---------------------------------------------------------------------------
results = []
placed_companions = []
placed_wires = []  # List of wire segments [(x1,y1,x2,y2), ...] to avoid crossings
existing_labels = []  # Bboxes for existing labels to avoid overlap
_debug = []

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

def _is_horizontal_pin_component(lib_id):
    """Check if component has horizontal pins at 0 rotation (LED, diode).
    R/C have vertical pins at 0, LED/D have horizontal pins."""
    lib_lower = lib_id.lower()
    return 'led' in lib_lower or ':d' in lib_lower or lib_lower.endswith(':d') or '_d_' in lib_lower or 'diode' in lib_lower

def _get_component_angle(escape_dir, lib_id):
    """Get correct rotation angle based on component type and escape direction.

    For vertical-pin components (R, C) at 0: pin 1 at top, pin 2 at bottom
    For horizontal-pin components (LED, D) at 0: pin 1 at left, pin 2 at right

    We want pin 2 facing toward IC (for wiring), pin 1 away (for terminal).
    """
    is_horiz = _is_horizontal_pin_component(lib_id)

    if is_horiz:
        angles = {'left': 0, 'right': 180, 'down': 270, 'up': 90}
    else:
        angles = {'left': 90, 'right': 270, 'down': 180, 'up': 0}

    return angles.get(escape_dir, 270)

def _bboxes_overlap(a, b):
    """Check if two bboxes overlap."""
    return (a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and
            a['min_y'] < b['max_y'] and a['max_y'] > b['min_y'])

def _segments_intersect(seg1, seg2):
    """Check if two line segments intersect (cross each other).
    Each segment is (x1, y1, x2, y2).
    Returns True only for actual crossings, not T-junctions or shared endpoints.
    """
    x1, y1, x2, y2 = seg1
    x3, y3, x4, y4 = seg2

    eps = 0.01
    def pts_equal(ax, ay, bx, by):
        return abs(ax - bx) < eps and abs(ay - by) < eps

    # Shared endpoints are OK
    if (pts_equal(x1, y1, x3, y3) or pts_equal(x1, y1, x4, y4) or
        pts_equal(x2, y2, x3, y3) or pts_equal(x2, y2, x4, y4)):
        return False

    # Horizontal segments on same line
    if abs(y1 - y2) < eps and abs(y3 - y4) < eps:
        if abs(y1 - y3) < eps:
            return max(min(x1, x2), min(x3, x4)) < min(max(x1, x2), max(x3, x4))
        return False

    # Vertical segments on same line
    if abs(x1 - x2) < eps and abs(x3 - x4) < eps:
        if abs(x1 - x3) < eps:
            return max(min(y1, y2), min(y3, y4)) < min(max(y1, y2), max(y3, y4))
        return False

    # One horizontal, one vertical - check for crossing
    if abs(y1 - y2) < eps:  # seg1 is horizontal
        h_y, h_x1, h_x2 = y1, min(x1, x2), max(x1, x2)
        v_x, v_y1, v_y2 = x3, min(y3, y4), max(y3, y4)
    elif abs(x1 - x2) < eps:  # seg1 is vertical
        v_x, v_y1, v_y2 = x1, min(y1, y2), max(y1, y2)
        h_y, h_x1, h_x2 = y3, min(x3, x4), max(x3, x4)
    else:
        return False

    return (h_x1 + eps < v_x < h_x2 - eps) and (v_y1 + eps < h_y < v_y2 - eps)

def _compute_wire_path(px, py, cx, cy, escape_dir):
    """Compute L-shaped wire path from IC pin (px,py) to companion (cx,cy).
    Returns list of segments [(x1,y1,x2,y2), ...].
    """
    px, py = snap_to_grid(px), snap_to_grid(py)
    cx, cy = snap_to_grid(cx), snap_to_grid(cy)

    if escape_dir in ('left', 'right'):
        if abs(py - cy) < 0.01:
            return [(px, py, cx, cy)]
        else:
            return [(px, py, px, cy), (px, cy, cx, cy)]
    else:
        if abs(px - cx) < 0.01:
            return [(px, py, cx, cy)]
        else:
            return [(px, py, cx, py), (cx, py, cx, cy)]

def _wire_path_crosses_existing(wire_path, existing_wires):
    """Check if any segment in wire_path crosses any existing wire."""
    for new_seg in wire_path:
        for existing_seg in existing_wires:
            if _segments_intersect(new_seg, existing_seg):
                return True
    return False

def _calc_companion_bbox(cx, cy, escape_dir, lib_id=''):
    """Calculate companion bbox given center and escape direction.
    Bbox is asymmetric: larger on pin1 side (away from IC) to include power symbols/labels.
    """
    body_half = COMP_HALF_LEN + BBOX_MARGIN
    term_half = COMP_HALF_LEN + TERMINAL_EXTENSION + BBOX_MARGIN

    if _is_horizontal_pin_component(lib_id):
        width_half = 3.0 + BBOX_MARGIN
    else:
        width_half = 1.5 + BBOX_MARGIN

    if escape_dir == 'left':
        return {'min_x': cx - term_half, 'max_x': cx + body_half, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'right':
        return {'min_x': cx - body_half, 'max_x': cx + term_half, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'up':
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - term_half, 'max_y': cy + body_half}
    else:  # down
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - body_half, 'max_y': cy + term_half}

def _calc_center_from_offset(px, py, offset_mm, escape_dir):
    """Calculate companion center given IC pin pos, offset, and escape direction."""
    if escape_dir == 'left':
        return snap_to_grid(px - offset_mm), snap_to_grid(py)
    elif escape_dir == 'right':
        return snap_to_grid(px + offset_mm), snap_to_grid(py)
    elif escape_dir == 'up':
        return snap_to_grid(px), snap_to_grid(py - offset_mm)
    else:  # down
        return snap_to_grid(px), snap_to_grid(py + offset_mm)

def _estimate_label_bbox(pos_x, pos_y, text, angle=0):
    """Estimate bounding box for a label based on text length and position."""
    text_len = len(text) if text else 3
    width = text_len * LABEL_CHAR_WIDTH + 1.0
    height = LABEL_HEIGHT
    margin = 1.0
    if angle in (0, 180):
        return {
            'min_x': pos_x - margin,
            'max_x': pos_x + width + margin,
            'min_y': pos_y - height/2 - margin,
            'max_y': pos_y + height/2 + margin
        }
    else:
        return {
            'min_x': pos_x - height/2 - margin,
            'max_x': pos_x + height/2 + margin,
            'min_y': pos_y - margin,
            'max_y': pos_y + width + margin
        }

def _find_clear_position(px, py, escape_dir, lib_id=''):
    """Find a non-overlapping position by increasing offset distance (vertical staggering)."""
    _debug.append(f'_find_clear_position: px={px:.2f}, py={py:.2f}, escape={escape_dir}, placed={len(placed_companions)}, labels={len(existing_labels)}')

    for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
        try_offset = try_grids * GRID_MM

        try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir)
        try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)

        # Check against IC bbox
        if _bboxes_overlap(try_bbox, ic_bbox_expanded):
            continue

        # Check against placed companions
        overlaps = False
        for placed in placed_companions:
            placed_bbox = _calc_companion_bbox(placed['cx'], placed['cy'], placed['escape_dir'], placed.get('lib_id', ''))
            if _bboxes_overlap(try_bbox, placed_bbox):
                overlaps = True
                break

        # Check against existing labels
        if not overlaps:
            for lbl_bbox in existing_labels:
                if _bboxes_overlap(try_bbox, lbl_bbox):
                    overlaps = True
                    break

        # Check if wire path would cross existing wires
        if not overlaps:
            try_wire_path = _compute_wire_path(px, py, try_cx, try_cy, escape_dir)
            if _wire_path_crosses_existing(try_wire_path, placed_wires):
                overlaps = True

        if not overlaps:
            _debug.append(f'  FOUND: offset={try_grids} -> ({try_cx:.2f}, {try_cy:.2f})')
            return try_cx, try_cy, try_offset

    # Fallback
    _debug.append(f'  FALLBACK: no clear position found')
    cx, cy = _calc_center_from_offset(px, py, MIN_OFFSET_GRIDS * GRID_MM, escape_dir)
    return cx, cy, MIN_OFFSET_GRIDS * GRID_MM

# Reference counters for auto-generating refdes
ref_counters = {}
def get_next_ref(prefix):
    if prefix not in ref_counters:
        highest = 0
        for sym in sch.symbols.get_all():
            ref = getattr(sym, 'reference', '')
            if ref.startswith(prefix):
                try:
                    num = int(ref[len(prefix):])
                    highest = max(highest, num)
                except:
                    pass
        ref_counters[prefix] = highest
    ref_counters[prefix] += 1
    return f'{prefix}{ref_counters[prefix]}'

# ---------------------------------------------------------------------------
# Get IC symbol and bounding box
# ---------------------------------------------------------------------------
ic_sym = sch.symbols.get_by_ref(ic_ref)
if not ic_sym:
    print(json.dumps({'status': 'error', 'message': f'IC not found: {ic_ref}'}))
    raise SystemExit()

ic_bbox = sch.transform.get_bounding_box(ic_sym, units='mm', include_text=False)
if not ic_bbox:
    print(json.dumps({'status': 'error', 'message': 'Could not get IC bounding box'}))
    raise SystemExit()

ic_bbox_expanded = {
    'min_x': ic_bbox['min_x'] - BBOX_MARGIN,
    'max_x': ic_bbox['max_x'] + BBOX_MARGIN,
    'min_y': ic_bbox['min_y'] - BBOX_MARGIN,
    'max_y': ic_bbox['max_y'] + BBOX_MARGIN
}
_debug.append(f'IC bbox: ({ic_bbox_expanded["min_x"]:.2f},{ic_bbox_expanded["min_y"]:.2f})-({ic_bbox_expanded["max_x"]:.2f},{ic_bbox_expanded["max_y"]:.2f})')

# Collect existing labels to avoid overlap
try:
    if hasattr(sch, 'labels'):
        for label in sch.labels.get_all():
            try:
                lbl_pos = getattr(label, 'position', None)
                lbl_text = getattr(label, 'text', getattr(label, 'name', ''))
                lbl_angle = getattr(label, 'angle', 0)
                if lbl_pos:
                    lbl_x = lbl_pos.x / 1_000_000 if hasattr(lbl_pos, 'x') else 0
                    lbl_y = lbl_pos.y / 1_000_000 if hasattr(lbl_pos, 'y') else 0
                    lbl_bbox = _estimate_label_bbox(lbl_x, lbl_y, lbl_text, lbl_angle)
                    existing_labels.append(lbl_bbox)
            except:
                pass
except:
    pass
_debug.append(f'Found {len(existing_labels)} existing labels')

# Orientation transform for pin escape direction
_rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
_rot_steps = round(getattr(ic_sym, 'angle', 0) / 90) % 4

def transform_orientation(orient):
    o = orient
    for _ in range(_rot_steps):
        o = _rot90.get(o, o)
    if getattr(ic_sym, 'mirror_x', False):
        if o == 0: o = 1
        elif o == 1: o = 0
    if getattr(ic_sym, 'mirror_y', False):
        if o == 2: o = 3
        elif o == 3: o = 2
    return o

def get_escape_dir(orient):
    """Convert orientation enum to escape direction string."""
    if orient == 0: return 'left'   # pin points right, escape left
    elif orient == 1: return 'right' # pin points left, escape right
    elif orient == 2: return 'down'  # pin points up, escape down
    elif orient == 3: return 'up'    # pin points down, escape up
    else: return 'right'

def draw_wire_stub(px, py, cpx, cpy, escape_dir, wire_idx):
    """Draw L-shaped wire stub with staggered corner to avoid crossings."""
    x0, y0 = snap_to_grid(px), snap_to_grid(py)
    x1, y1 = snap_to_grid(cpx), snap_to_grid(cpy)
    wire_len = ((x1 - x0)**2 + (y1 - y0)**2)**0.5

    if wire_len < 0.1:
        return

    corner_offset = GRID_MM * (1 + wire_idx)

    if escape_dir in ('left', 'right'):
        if escape_dir == 'right':
            corner_x = snap_to_grid(x0 + corner_offset)
        else:
            corner_x = snap_to_grid(x0 - corner_offset)

        if escape_dir == 'right' and corner_x > x1:
            corner_x = x1
        elif escape_dir == 'left' and corner_x < x1:
            corner_x = x1

        if abs(y0 - y1) < 0.01 and abs(corner_x - x1) < 0.01:
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
            placed_wires.append((x0, y0, x1, y1))
        else:
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(corner_x, y0))
            sch.wiring.add_wire(Vector2.from_xy_mm(corner_x, y0), Vector2.from_xy_mm(corner_x, y1))
            if abs(corner_x - x1) > 0.01:
                sch.wiring.add_wire(Vector2.from_xy_mm(corner_x, y1), Vector2.from_xy_mm(x1, y1))
            placed_wires.append((x0, y0, corner_x, y0))
            placed_wires.append((corner_x, y0, corner_x, y1))
            if abs(corner_x - x1) > 0.01:
                placed_wires.append((corner_x, y1, x1, y1))
    else:
        if escape_dir == 'down':
            corner_y = snap_to_grid(y0 + corner_offset)
        else:
            corner_y = snap_to_grid(y0 - corner_offset)

        if escape_dir == 'down' and corner_y > y1:
            corner_y = y1
        elif escape_dir == 'up' and corner_y < y1:
            corner_y = y1

        if abs(x0 - x1) < 0.01 and abs(corner_y - y1) < 0.01:
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
            placed_wires.append((x0, y0, x1, y1))
        else:
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x0, corner_y))
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, corner_y), Vector2.from_xy_mm(x1, corner_y))
            if abs(corner_y - y1) > 0.01:
                sch.wiring.add_wire(Vector2.from_xy_mm(x1, corner_y), Vector2.from_xy_mm(x1, y1))
            placed_wires.append((x0, y0, x0, corner_y))
            placed_wires.append((x0, corner_y, x1, corner_y))
            if abs(corner_y - y1) > 0.01:
                placed_wires.append((x1, corner_y, x1, y1))

def place_power_symbol(pin_pos_x, pin_pos_y, power_name, escape_dir):
    """Place a power symbol at the given pin position."""
    pwr_name_upper = power_name.upper()
    is_gnd = 'GND' in pwr_name_upper or 'VSS' in pwr_name_upper
    pwr_offset = GRID_MM

    if escape_dir == 'left':
        pwr_x = snap_to_grid(pin_pos_x - pwr_offset)
        pwr_y = snap_to_grid(pin_pos_y)
        pwr_angle = 270 if is_gnd else 90
    elif escape_dir == 'right':
        pwr_x = snap_to_grid(pin_pos_x + pwr_offset)
        pwr_y = snap_to_grid(pin_pos_y)
        pwr_angle = 90 if is_gnd else 270
    elif escape_dir == 'down':
        pwr_x = snap_to_grid(pin_pos_x)
        pwr_y = snap_to_grid(pin_pos_y + pwr_offset)
        pwr_angle = 0 if is_gnd else 180
    else:  # up
        pwr_x = snap_to_grid(pin_pos_x)
        pwr_y = snap_to_grid(pin_pos_y - pwr_offset)
        pwr_angle = 180 if is_gnd else 0

    pwr_pos = Vector2.from_xy_mm(pwr_x, pwr_y)
    pwr_sym = sch.labels.add_power(power_name, pwr_pos, angle=pwr_angle)

    # Wire from pin to power symbol
    sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(pin_pos_x), snap_to_grid(pin_pos_y)), pwr_pos)

    return pwr_sym, pwr_x, pwr_y

# ---------------------------------------------------------------------------
# Process each companion
# ---------------------------------------------------------------------------
try:
    for i, companion in enumerate(companions_input):
        lib_id = companion.get('lib_id', '')
        ic_pin = companion.get('ic_pin', '')
        offset_grids = companion.get('offset_grids', 3)
        reverse = companion.get('reverse', False)

        if not lib_id or not ic_pin:
            results.append({'index': i, 'error': 'lib_id and ic_pin are required'})
            continue

        # Check for power-only companion
        is_power_only = lib_id.startswith('power:')
        if is_power_only:
            power_name = lib_id[6:]  # Remove "power:" prefix

        offset_mm = snap_to_grid(offset_grids * 1.27)

        try:
            # Get IC pin position and orientation
            pin_result = sch.symbols.get_transformed_pin_position(ic_sym, ic_pin)
            if not pin_result:
                raise ValueError(f'Pin not found on IC: {ic_pin}')

            px = pin_result['position'].x / 1_000_000
            py = pin_result['position'].y / 1_000_000
            orient = transform_orientation(pin_result.get('orientation', 1))
            escape_dir = get_escape_dir(orient)

            # Power-only: place power symbol directly at IC pin
            if is_power_only:
                pwr_offset = GRID_MM * 2
                pwr_name_upper = power_name.upper()
                is_gnd = 'GND' in pwr_name_upper or 'VSS' in pwr_name_upper

                if escape_dir == 'left':
                    pwr_x = snap_to_grid(px - pwr_offset)
                    pwr_y = snap_to_grid(py)
                    pwr_angle = 270 if is_gnd else 90
                elif escape_dir == 'right':
                    pwr_x = snap_to_grid(px + pwr_offset)
                    pwr_y = snap_to_grid(py)
                    pwr_angle = 90 if is_gnd else 270
                elif escape_dir == 'down':
                    pwr_x = snap_to_grid(px)
                    pwr_y = snap_to_grid(py + pwr_offset)
                    pwr_angle = 0 if is_gnd else 180
                else:  # up
                    pwr_x = snap_to_grid(px)
                    pwr_y = snap_to_grid(py - pwr_offset)
                    pwr_angle = 180 if is_gnd else 0

                pwr_pos = Vector2.from_xy_mm(pwr_x, pwr_y)
                pwr_sym = sch.labels.add_power(power_name, pwr_pos, angle=pwr_angle)

                sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(px), snap_to_grid(py)), pwr_pos)
                _debug.append(f'Power-only: {power_name} at ({pwr_x:.2f},{pwr_y:.2f})')

                placed_companions.append({
                    'index': i,
                    'lib_id': lib_id,
                    'ic_pin': ic_pin,
                    'ref': pwr_sym.reference if hasattr(pwr_sym, 'reference') else power_name,
                    'cx': pwr_x,
                    'cy': pwr_y,
                    'position': [round(pwr_x, 2), round(pwr_y, 2)],
                    'escape_dir': escape_dir,
                    'power_only': True
                })
                continue

            # Find clear position (vertical staggering only, no lateral shift)
            cx, cy, _offset = _find_clear_position(px, py, escape_dir, lib_id)
            _debug.append(f'Comp{i}: offset={_offset:.2f}mm at ({cx:.2f},{cy:.2f})')

            # Calculate rotation angle
            comp_angle = _get_component_angle(escape_dir, lib_id)
            if reverse:
                comp_angle = (comp_angle + 180) % 360

            # Create companion symbol
            comp_pos = Vector2.from_xy_mm(cx, cy)
            comp_sym = sch.symbols.add(lib_id=lib_id, position=comp_pos, angle=comp_angle)
            if not comp_sym:
                raise ValueError(f'Failed to create companion symbol: {lib_id}')

            # Set properties
            properties = companion.get('properties', {})
            for prop_name, prop_value in properties.items():
                if prop_name == 'Value':
                    sch.symbols.set_value(comp_sym, prop_value)
                elif prop_name == 'Footprint':
                    sch.symbols.set_footprint(comp_sym, prop_value)
                else:
                    comp_sym.set_field(prop_name, prop_value)
                    sch.crud.update_items(comp_sym)

            # Get wire pin position (pin 2 toward IC by default, pin 1 if reversed)
            wire_pin = '1' if reverse else '2'
            comp_wire_pin = sch.symbols.get_transformed_pin_position(comp_sym, wire_pin)
            if comp_wire_pin:
                cpwx = comp_wire_pin['position'].x / 1_000_000
                cpwy = comp_wire_pin['position'].y / 1_000_000
            else:
                cpwx, cpwy = cx, cy

            # Draw wire stub from IC pin to companion
            wire_idx = len(placed_companions)
            draw_wire_stub(px, py, cpwx, cpwy, escape_dir, wire_idx)

            # Handle terminal_power
            terminal_power = companion.get('terminal_power', {})
            for pin_num, pwr_name in terminal_power.items():
                actual_pin = pin_num
                if reverse:
                    if pin_num == '1': actual_pin = '2'
                    elif pin_num == '2': actual_pin = '1'

                # Extract power name (remove library prefix if present)
                if ':' in pwr_name:
                    pwr_name = pwr_name.split(':')[-1]

                try:
                    pwr_pin = sch.symbols.get_transformed_pin_position(comp_sym, actual_pin)
                    if pwr_pin:
                        pwr_px = pwr_pin['position'].x / 1_000_000
                        pwr_py = pwr_pin['position'].y / 1_000_000
                        place_power_symbol(pwr_px, pwr_py, pwr_name, escape_dir)
                except Exception as pwr_e:
                    results.append({'index': i, 'warning': f'Power symbol at pin {pin_num}: {str(pwr_e)}'})

            # Handle terminal_labels
            terminal_labels = companion.get('terminal_labels', {})
            for pin_num, label_text in terminal_labels.items():
                actual_pin = pin_num
                if reverse:
                    if pin_num == '1': actual_pin = '2'
                    elif pin_num == '2': actual_pin = '1'

                try:
                    lbl_pin = sch.symbols.get_transformed_pin_position(comp_sym, actual_pin)
                    if lbl_pin:
                        lbl_px = lbl_pin['position'].x / 1_000_000
                        lbl_py = lbl_pin['position'].y / 1_000_000
                        lbl_pos = Vector2.from_xy_mm(snap_to_grid(lbl_px), snap_to_grid(lbl_py))
                        sch.labels.add_local(label_text, lbl_pos)
                except Exception as lbl_e:
                    results.append({'index': i, 'warning': f'Label at pin {pin_num}: {str(lbl_e)}'})

            # Record placement
            placed_companions.append({
                'index': i,
                'lib_id': lib_id,
                'ic_pin': ic_pin,
                'ref': comp_sym.reference,
                'cx': cx,
                'cy': cy,
                'position': [round(cx, 2), round(cy, 2)],
                'escape_dir': escape_dir
            })

            # Process chain items
            chain = companion.get('chain', [])
            if chain:
                # TODO: Implement chain processing (recursive companion placement)
                pass

        except Exception as e:
            results.append({'index': i, 'lib_id': lib_id, 'ic_pin': ic_pin, 'error': str(e)})

except Exception as e:
    results = [{'error': str(e)}]

# Output result
output = {
    'status': 'success' if placed_companions else 'error',
    'ic_ref': ic_ref,
    'companions_placed': len(placed_companions),
    'companions_failed': len([r for r in results if 'error' in r]),
    'placed': placed_companions,
    'errors': [r for r in results if 'error' in r],
    'warnings': [r for r in results if 'warning' in r],
    '_debug': _debug
}
print(json.dumps(output, indent=2))
