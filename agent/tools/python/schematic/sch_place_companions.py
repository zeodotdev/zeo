"""
sch_place_companions - Place companion components adjacent to IC pins.

Companion circuits are small supporting parts (decoupling caps, pull-up/down resistors,
termination resistors, filter caps, LED indicators) that wire directly to specific IC pins.

The tool calculates optimal positions based on IC pin geometry and orientation:
- Gets IC pin position and escape direction via get_transformed_pin_position()
- Places companion symbol adjacent to pin (offset by N grid units in escape direction)
- Draws short wire stub from IC pin to companion pin
- Adds power symbols or text labels at companion terminals as specified
- Supports chaining components (e.g., VCC -> cap -> GND) via recursive chain processing
"""
import json, sys
from kipy.geometry import Vector2

refresh_or_fail(sch)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
GRID_MM = 1.27           # 50 mil grid
BBOX_MARGIN = 0.3        # Margin around components for spacing
MAX_OFFSET_GRIDS = 15    # Max push-out distance
MIN_OFFSET_GRIDS = 3     # Minimum distance from IC (3.81mm)
COMP_HALF_LEN = 3.81     # Half-length of 2-pin passive (center to pin)
TERMINAL_EXTENSION = 3.81 # Terminal area for power symbols/labels
LABEL_CHAR_WIDTH = 1.0   # Approximate width per character in mm
LABEL_HEIGHT = 2.5       # Approximate label height in mm
_LABEL_SHRINK = 0.4      # Shrink label bboxes to allow stacking at 2.54mm pitch

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
placed_wire_objs = []  # Wire objects returned by add_wire (for junction placement)
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
    """Estimate bounding box for a label based on text length and position.
    Used for newly-placed labels where calling the API would add IPC cost.
    """
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
    """Find a non-overlapping position by increasing offset distance.
    Uses spatial index for O(1) average-case overlap checks against all
    existing schematic items (symbols, labels, sheets, wires, IC bbox,
    and previously placed companions).

    If no clear position is found along the escape direction, tries
    perpendicular stagger offsets (e.g., when multiple companions share
    the same IC pin).
    """
    _debug.append(f'_find_clear_position: px={px:.2f}, py={py:.2f}, escape={escape_dir}')

    # Calculate perpendicular stagger step from companion bbox dimensions
    sample_bbox = _calc_companion_bbox(0, 0, escape_dir, lib_id)
    if escape_dir in ('left', 'right'):
        perp_size = sample_bbox['max_y'] - sample_bbox['min_y']
    else:
        perp_size = sample_bbox['max_x'] - sample_bbox['min_x']
    perp_step = snap_to_grid(perp_size + GRID_MM)

    # Try perpendicular stagger offsets: 0, +step, -step, +2*step, -2*step, ...
    max_perp_steps = 4
    perp_offsets = [0]
    for s in range(1, max_perp_steps + 1):
        perp_offsets.append(s * perp_step)
        perp_offsets.append(-s * perp_step)

    for perp_off in perp_offsets:
        # Apply perpendicular offset to the pin position
        if escape_dir in ('left', 'right'):
            search_px, search_py = px, snap_to_grid(py + perp_off)
        else:
            search_px, search_py = snap_to_grid(px + perp_off), py

        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
            try_offset = try_grids * GRID_MM

            try_cx, try_cy = _calc_center_from_offset(search_px, search_py, try_offset, escape_dir)
            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)

            # O(1) average overlap check via spatial index
            if _spatial.any_overlap(try_bbox):
                continue

            # Check if wire path would cross existing wires
            try_wire_path = _compute_wire_path(search_px, search_py, try_cx, try_cy, escape_dir)
            if _wire_path_crosses_existing(try_wire_path, placed_wires):
                continue

            _debug.append(f'  FOUND: offset={try_grids} perp={perp_off:.2f} -> ({try_cx:.2f}, {try_cy:.2f})')
            return try_cx, try_cy, try_offset

    # Fallback
    _debug.append(f'  FALLBACK: no clear position found')
    cx, cy = _calc_center_from_offset(px, py, MIN_OFFSET_GRIDS * GRID_MM, escape_dir)
    return cx, cy, MIN_OFFSET_GRIDS * GRID_MM

# Reference counters for auto-generating refdes (uses cached symbols)
ref_counters = {}
def get_next_ref(prefix):
    if prefix not in ref_counters:
        highest = 0
        for sym in _all_symbols_cache:
            ref = getattr(sym, 'reference', '')
            if ref.startswith(prefix):
                try:
                    num = int(ref[len(prefix):])
                    highest = max(highest, num)
                except Exception:
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

# ---------------------------------------------------------------------------
# Pre-collect all obstacles for overlap detection (uses bbox.py utilities)
# ---------------------------------------------------------------------------
_ic_uuid = get_uuid_str(ic_sym)

# Collect all existing symbols, labels, sheets, wires with accurate API bboxes
_all_obstacle_bboxes = collect_all_obstacle_bboxes(
    sch,
    label_shrink=_LABEL_SHRINK,
    exclude_ids={_ic_uuid}
)

# Build spatial index for fast O(1) overlap lookups
_spatial = SpatialIndex(cell_size=10.0)
for _ob in _all_obstacle_bboxes:
    _spatial.insert(_ob)

# Insert expanded IC bbox into spatial index
_spatial.insert(ic_bbox_expanded)

_debug.append(f'Pre-collected {len(_all_obstacle_bboxes)} obstacles (symbols, labels, sheets, wires)')

# Pre-fetch ALL IC pin positions in one batch IPC call
_ic_pin_map = {}
try:
    _all_ic_pins = sch.symbols.get_all_transformed_pin_positions(ic_sym)
    for _p in _all_ic_pins:
        _ic_pin_map[_p['pin_number']] = {
            'position': _p['position'],
            'orientation': _p['orientation'],
            'pin_name': _p.get('pin_name', ''),
        }
except Exception:
    pass
_debug.append(f'Pre-fetched {len(_ic_pin_map)} IC pin positions in 1 IPC call')

# Cache all existing symbols for get_next_ref (avoid redundant get_all)
_all_symbols_cache = sch.symbols.get_all()

# Orientation transform for pin escape direction
_rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
_rot_steps = round(getattr(ic_sym, 'angle', 0) / 90) % 4

def transform_orientation(orient):
    o = orient
    for _ in range(_rot_steps):
        o = _rot90.get(o, o)
    # mirror_x = vertical flip (up<->down), mirror_y = horizontal flip (left<->right)
    if getattr(ic_sym, 'mirror_x', False):
        if o == 2: o = 3
        elif o == 3: o = 2
    if getattr(ic_sym, 'mirror_y', False):
        if o == 0: o = 1
        elif o == 1: o = 0
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
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
            if _w: placed_wire_objs.append(_w)
            placed_wires.append((x0, y0, x1, y1))
        else:
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(corner_x, y0))
            if _w: placed_wire_objs.append(_w)
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(corner_x, y0), Vector2.from_xy_mm(corner_x, y1))
            if _w: placed_wire_objs.append(_w)
            if abs(corner_x - x1) > 0.01:
                _w = sch.wiring.add_wire(Vector2.from_xy_mm(corner_x, y1), Vector2.from_xy_mm(x1, y1))
                if _w: placed_wire_objs.append(_w)
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
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
            if _w: placed_wire_objs.append(_w)
            placed_wires.append((x0, y0, x1, y1))
        else:
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x0, corner_y))
            if _w: placed_wire_objs.append(_w)
            _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, corner_y), Vector2.from_xy_mm(x1, corner_y))
            if _w: placed_wire_objs.append(_w)
            if abs(corner_y - y1) > 0.01:
                _w = sch.wiring.add_wire(Vector2.from_xy_mm(x1, corner_y), Vector2.from_xy_mm(x1, y1))
                if _w: placed_wire_objs.append(_w)
            placed_wires.append((x0, y0, x0, corner_y))
            placed_wires.append((x0, corner_y, x1, corner_y))
            if abs(corner_y - y1) > 0.01:
                placed_wires.append((x1, corner_y, x1, y1))

def place_power_symbol(pin_pos_x, pin_pos_y, power_name, escape_dir):
    """Place a power symbol at the given pin position and register in spatial index."""
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
    _w = sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(pin_pos_x), snap_to_grid(pin_pos_y)), pwr_pos)
    if _w: placed_wire_objs.append(_w)

    # Register power symbol bbox in spatial index for future collision avoidance
    _spatial.insert({
        'kind': 'power_placed',
        'min_x': pwr_x - 2.0, 'max_x': pwr_x + 2.0,
        'min_y': pwr_y - 3.0, 'max_y': pwr_y + 3.0,
    })

    return pwr_sym, pwr_x, pwr_y

def _get_ic_pin_data(ic_pin):
    """Get IC pin position/orientation from pre-fetched map, falling back to IPC call."""
    pin_data = _ic_pin_map.get(ic_pin)
    if pin_data:
        return pin_data
    # Fallback: try pin by name if number lookup failed
    for _pnum, _pdata in _ic_pin_map.items():
        if _pdata.get('pin_name', '') == ic_pin:
            return _pdata
    # Last resort: IPC call
    pin_result = sch.symbols.get_transformed_pin_position(ic_sym, ic_pin)
    if pin_result:
        return {
            'position': pin_result['position'],
            'orientation': pin_result.get('orientation', 1),
        }
    return None

def _place_single_companion(companion, index, parent_pin_x=None, parent_pin_y=None,
                             parent_escape_dir=None, is_chain=False):
    """Place a single companion component. Used for both top-level and chain items.

    For top-level companions, reads ic_pin from companion dict and looks up IC pin position.
    For chain items, uses parent_pin_x/y and parent_escape_dir directly.

    Returns (comp_sym, away_pin_x, away_pin_y, escape_dir) or None on failure.
    """
    lib_id = companion.get('lib_id', '')
    ic_pin = companion.get('ic_pin', '')
    offset_grids = companion.get('offset_grids', 3)
    reverse = companion.get('reverse', False)

    if not lib_id:
        results.append({'index': index, 'error': 'lib_id is required'})
        return None

    if not is_chain and not ic_pin:
        results.append({'index': index, 'error': 'ic_pin is required for top-level companions'})
        return None

    # Determine pin position and escape direction
    if is_chain:
        px, py = parent_pin_x, parent_pin_y
        escape_dir = parent_escape_dir
    else:
        pin_data = _get_ic_pin_data(ic_pin)
        if not pin_data:
            raise ValueError(f'Pin not found on IC: {ic_pin}')
        px = pin_data['position'].x / 1_000_000
        py = pin_data['position'].y / 1_000_000
        orient = transform_orientation(pin_data['orientation'])
        escape_dir = get_escape_dir(orient)

    # Check for power-only companion
    is_power_only = lib_id.startswith('power:')
    if is_power_only:
        power_name = lib_id[6:]  # Remove "power:" prefix

    offset_mm = snap_to_grid(offset_grids * 1.27)

    # Power-only: place power symbol directly at pin
    if is_power_only:
        pwr_sym, pwr_x, pwr_y = place_power_symbol(px, py, power_name, escape_dir)
        _debug.append(f'Power-only: {power_name} at ({pwr_x:.2f},{pwr_y:.2f})')

        placed_companions.append({
            'index': index,
            'lib_id': lib_id,
            'ic_pin': ic_pin,
            'ref': pwr_sym.reference if hasattr(pwr_sym, 'reference') else power_name,
            'cx': pwr_x,
            'cy': pwr_y,
            'position': [round(pwr_x, 2), round(pwr_y, 2)],
            'escape_dir': escape_dir,
            'power_only': True
        })
        # Power-only has no "away" pin for chaining, return position for potential chain
        return (pwr_sym, pwr_x, pwr_y, escape_dir)

    # Find clear position
    cx, cy, _offset = _find_clear_position(px, py, escape_dir, lib_id)
    _debug.append(f'Comp{index}: offset={_offset:.2f}mm at ({cx:.2f},{cy:.2f})')

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

    # Get ALL companion pin positions in 1 IPC call
    comp_pin_map = {}
    try:
        comp_all_pins = sch.symbols.get_all_transformed_pin_positions(comp_sym)
        for cp in comp_all_pins:
            comp_pin_map[cp['pin_number']] = {
                'x': cp['position'].x / 1_000_000,
                'y': cp['position'].y / 1_000_000,
            }
    except Exception:
        pass

    # Get wire pin position (pin 2 toward IC by default, pin 1 if reversed)
    wire_pin = '1' if reverse else '2'
    if wire_pin in comp_pin_map:
        cpwx = comp_pin_map[wire_pin]['x']
        cpwy = comp_pin_map[wire_pin]['y']
    else:
        cpwx, cpwy = cx, cy

    # Draw wire stub from parent pin to companion
    wire_idx = len(placed_companions)
    draw_wire_stub(px, py, cpwx, cpwy, escape_dir, wire_idx)

    # Register companion bbox in spatial index for future collision avoidance
    comp_bbox = _calc_companion_bbox(cx, cy, escape_dir, lib_id)
    _spatial.insert(comp_bbox)

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
            if actual_pin in comp_pin_map:
                pwr_px = comp_pin_map[actual_pin]['x']
                pwr_py = comp_pin_map[actual_pin]['y']
            else:
                pwr_pin = sch.symbols.get_transformed_pin_position(comp_sym, actual_pin)
                if pwr_pin:
                    pwr_px = pwr_pin['position'].x / 1_000_000
                    pwr_py = pwr_pin['position'].y / 1_000_000
                else:
                    continue
            place_power_symbol(pwr_px, pwr_py, pwr_name, escape_dir)
        except Exception as pwr_e:
            results.append({'index': index, 'warning': f'Power symbol at pin {pin_num}: {str(pwr_e)}'})

    # Handle terminal_labels
    terminal_labels = companion.get('terminal_labels', {})
    for pin_num, label_text in terminal_labels.items():
        actual_pin = pin_num
        if reverse:
            if pin_num == '1': actual_pin = '2'
            elif pin_num == '2': actual_pin = '1'

        try:
            if actual_pin in comp_pin_map:
                lbl_px = comp_pin_map[actual_pin]['x']
                lbl_py = comp_pin_map[actual_pin]['y']
            else:
                lbl_pin = sch.symbols.get_transformed_pin_position(comp_sym, actual_pin)
                if lbl_pin:
                    lbl_px = lbl_pin['position'].x / 1_000_000
                    lbl_py = lbl_pin['position'].y / 1_000_000
                else:
                    continue
            lbl_pos = Vector2.from_xy_mm(snap_to_grid(lbl_px), snap_to_grid(lbl_py))
            sch.labels.add_local(label_text, lbl_pos)
            # Register placed label in spatial index
            lbl_est_bbox = _estimate_label_bbox(snap_to_grid(lbl_px), snap_to_grid(lbl_py), label_text)
            _spatial.insert(lbl_est_bbox)
        except Exception as lbl_e:
            results.append({'index': index, 'warning': f'Label at pin {pin_num}: {str(lbl_e)}'})

    # Record placement (include UUID for post-annotation ref lookup)
    comp_uuid = str(comp_sym.id.value) if hasattr(comp_sym, 'id') and hasattr(comp_sym.id, 'value') else str(getattr(comp_sym, 'id', ''))
    placed_companions.append({
        'index': index,
        'lib_id': lib_id,
        'ic_pin': ic_pin,
        'ref': comp_sym.reference,
        'uuid': comp_uuid,
        'cx': cx,
        'cy': cy,
        'position': [round(cx, 2), round(cy, 2)],
        'escape_dir': escape_dir
    })

    # Determine "away" pin position for chaining
    away_pin = '1' if not reverse else '2'
    if away_pin in comp_pin_map:
        away_x = comp_pin_map[away_pin]['x']
        away_y = comp_pin_map[away_pin]['y']
    else:
        away_x, away_y = cx, cy

    return (comp_sym, away_x, away_y, escape_dir)


def _process_chain(chain_items, parent_away_x, parent_away_y, escape_dir, depth, parent_index):
    """Recursively place chained components from a parent's away terminal.

    Chain items connect to the parent's away terminal. Multiple items at the
    same level are staggered perpendicular to the escape direction (branches).
    """

    for idx, chain_item in enumerate(chain_items):
        # Calculate lateral offset for branches (multiple items = side-by-side)
        lateral_offset = 0
        if len(chain_items) > 1:
            # Compute perpendicular spacing from companion bbox dimensions
            # so branches don't overlap regardless of component size.
            # Use the full escape-direction extent (includes terminal extensions
            # for power symbols/labels) to prevent label overlap too.
            chain_lib = chain_item.get('lib_id', '')
            sample_bbox = _calc_companion_bbox(0, 0, escape_dir, chain_lib)
            if escape_dir in ('left', 'right'):
                perp_size = sample_bbox['max_y'] - sample_bbox['min_y']
            else:
                perp_size = sample_bbox['max_x'] - sample_bbox['min_x']
            # Space branches by full perpendicular bbox size + 2 grid gaps minimum
            branch_spacing = snap_to_grid(max(perp_size + GRID_MM * 2, GRID_MM * 6))
            lateral_offset = (idx - (len(chain_items) - 1) / 2.0) * branch_spacing

        # Apply lateral offset perpendicular to escape direction
        if lateral_offset != 0:
            if escape_dir in ('left', 'right'):
                chain_px = parent_away_x
                chain_py = snap_to_grid(parent_away_y + lateral_offset)
            else:
                chain_px = snap_to_grid(parent_away_x + lateral_offset)
                chain_py = parent_away_y
        else:
            chain_px = parent_away_x
            chain_py = parent_away_y

        chain_index = f'{parent_index}.chain[{idx}]'

        try:
            result = _place_single_companion(
                chain_item, chain_index,
                parent_pin_x=chain_px, parent_pin_y=chain_py,
                parent_escape_dir=escape_dir, is_chain=True
            )

            if result and chain_item.get('chain'):
                _, away_x, away_y, esc_dir = result
                _process_chain(chain_item['chain'], away_x, away_y, esc_dir, depth + 1, chain_index)

        except Exception as e:
            results.append({
                'index': chain_index,
                'lib_id': chain_item.get('lib_id', ''),
                'error': f'Chain placement failed: {str(e)}'
            })


# ---------------------------------------------------------------------------
# Process each companion
# ---------------------------------------------------------------------------
try:
    for i, companion in enumerate(companions_input):
        try:
            result = _place_single_companion(companion, i)

            # Process chain items if present
            chain = companion.get('chain', [])
            if chain and result:
                _, away_x, away_y, esc_dir = result
                _process_chain(chain, away_x, away_y, esc_dir, depth=0, parent_index=i)

        except Exception as e:
            results.append({
                'index': i,
                'lib_id': companion.get('lib_id', ''),
                'ic_pin': companion.get('ic_pin', ''),
                'error': str(e)
            })

except Exception as e:
    results.append({'error': f'Unexpected error during companion placement: {str(e)}'})

# ---------------------------------------------------------------------------
# Auto-place junctions where wires branch or meet
# ---------------------------------------------------------------------------
_junction_count = 0
if placed_wire_objs:
    try:
        _junc_positions = sch.wiring.get_needed_junctions(placed_wire_objs)
        for _jp in _junc_positions:
            sch.wiring.add_junction(_jp)
            _junction_count += 1
        if _junction_count:
            _debug.append(f'Auto-placed {_junction_count} junctions')
    except Exception as junc_e:
        _debug.append(f'Junction placement failed: {str(junc_e)}')

# ---------------------------------------------------------------------------
# Auto-annotate newly placed companions
# ---------------------------------------------------------------------------
if placed_companions:
    try:
        response = sch.erc.annotate(
            scope='all',
            order='x_y',
            algorithm='incremental',
            start_number=1,
            reset_existing=False,
            recursive=True
        )
        # Build UUID->ref map from all symbols after annotation
        uuid_to_ref = {}
        for s in sch.symbols.get_all():
            s_uuid = str(s.id.value) if hasattr(s, 'id') and hasattr(s.id, 'value') else str(getattr(s, 'id', ''))
            uuid_to_ref[s_uuid] = getattr(s, 'reference', '?')
        # Update placed companion refs using UUID lookup
        for comp in placed_companions:
            if comp.get('power_only'):
                continue
            comp_uuid = comp.get('uuid', '')
            if comp_uuid and comp_uuid in uuid_to_ref:
                comp['ref'] = uuid_to_ref[comp_uuid]
        _debug.append(f'Auto-annotated {getattr(response, "symbols_annotated", "?")} symbols')
    except Exception as ann_e:
        _debug.append(f'Auto-annotate failed: {str(ann_e)}')

# Output result
output = {
    'status': 'success' if placed_companions else 'error',
    'ic_ref': ic_ref,
    'companions_placed': len(placed_companions),
    'companions_failed': len([r for r in results if 'error' in r]),
    'junctions_placed': _junction_count,
    'placed': placed_companions,
    'errors': [r for r in results if 'error' in r],
    'warnings': [r for r in results if 'warning' in r],
    '_debug': _debug
}
print(json.dumps(output, indent=2))
