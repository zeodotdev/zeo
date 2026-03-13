"""
sch_place_companions - Place companion components adjacent to IC pins.

Companion circuits are small supporting parts (decoupling caps, pull-up/down resistors,
termination resistors, filter caps, LED indicators) that wire directly to specific IC pins.

NEW ALGORITHM (v2): Group-based unified layout
1. Group companions by escape direction (up, down, left, right)
2. Compute unified offset for each group (all companions at same distance from IC)
3. Each companion's perpendicular position matches its IC pin position
4. Result: clean parallel rows of components
"""
import json, sys
from kipy.geometry import Vector2, Box2
from kipy.common_types import Text
from kipy.proto.common import commands as base_commands_pb2
from kipy.proto.common import types as base_types_pb2

refresh_or_fail(sch)

# ---------------------------------------------------------------------------
# Get grid settings from API
# ---------------------------------------------------------------------------
try:
    _grid_settings = sch.page.get_grid_settings()
    GRID_MM = _grid_settings.get('size_mm', 1.27)
except Exception:
    GRID_MM = 1.27  # Default 50 mil grid

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
BBOX_MARGIN = 0.3        # Margin around components for spacing
MAX_OFFSET_GRIDS = 20    # Max push-out distance
MIN_OFFSET_GRIDS = 3     # Minimum distance from IC (3.81mm)
COMP_HALF_LEN = 3.81     # Half-length of 2-pin passive (center to pin)
TERMINAL_EXTENSION = 3.81 # Terminal area for power symbols/labels
COMP_WIDTH = 1.8         # Width of 2-pin passive perpendicular to pins
MIN_PERPENDICULAR_SPACING = 5.08  # Minimum spacing between companions (4 grids)

# ---------------------------------------------------------------------------
# Text measurement cache
# ---------------------------------------------------------------------------
_text_width_cache = {}

def measure_text_size_mm(text_string):
    """Measure actual text width and height in mm using KiCad API.

    Returns: (width_mm, height_mm) tuple
    """
    global _text_measure_calls
    if not hasattr(measure_text_size_mm, 'call_count'):
        measure_text_size_mm.call_count = 0
    measure_text_size_mm.call_count += 1

    if not text_string:
        return (0.0, 0.0)

    cache_key = text_string
    if cache_key in _text_width_cache:
        return _text_width_cache[cache_key]

    try:
        text = Text()
        text.value = text_string
        text.position = Vector2.from_xy_mm(0, 0)

        cmd = base_commands_pb2.GetTextExtents()
        cmd.text.CopyFrom(text.proto)
        reply = sch._kicad.send(cmd, base_types_pb2.Box2)
        bbox = Box2.from_proto(reply)

        # Box2 size is in nm, convert to mm
        width_mm = bbox.size.x / 1_000_000
        height_mm = bbox.size.y / 1_000_000
        result = (width_mm, height_mm)
        _text_width_cache[cache_key] = result
        tool_log(f'TextMeasure[{measure_text_size_mm.call_count}]: "{text_string}" = {width_mm:.2f}mm x {height_mm:.2f}mm')
        return result
    except Exception as e:
        tool_log(f'TextMeasure FAILED[{measure_text_size_mm.call_count}]: "{text_string}" error={type(e).__name__}: {str(e)}')
        # Fallback: estimate ~1mm per character width, 2mm height
        fallback = (len(text_string) * 1.0, 2.0)
        _text_width_cache[cache_key] = fallback
        return fallback

def measure_text_width_mm(text_string):
    """Measure actual text width in mm using KiCad API."""
    return measure_text_size_mm(text_string)[0]

def _calc_label_buffer(companion):
    """Calculate extra offset needed for terminal labels on a companion.

    Returns the height of the tallest label plus one grid unit for spacing.
    This ensures the component is pushed far enough from the IC to leave
    room for the label between the component pin and the IC.
    """
    if not companion:
        return 0.0

    max_label_height = 0.0

    # Check terminal_labels (e.g., "+3V3", "VDD_SPI")
    for label_text in companion.get('terminal_labels', {}).values():
        _, h = measure_text_size_mm(label_text)
        max_label_height = max(max_label_height, h)

    if max_label_height > 0:
        # Add grid spacing for visual separation
        return max_label_height + GRID_MM

    return 0.0

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
placed_wire_objs = []
_debug = []

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

def _is_horizontal_pin_component(lib_id):
    """Check if component has horizontal pins at 0 rotation (LED, diode)."""
    lib_lower = lib_id.lower()
    return 'led' in lib_lower or ':d' in lib_lower or lib_lower.endswith(':d') or '_d_' in lib_lower or 'diode' in lib_lower

def _get_component_angle(escape_dir, lib_id):
    """Get correct rotation angle based on component type and escape direction."""
    is_horiz = _is_horizontal_pin_component(lib_id)
    if is_horiz:
        angles = {'left': 0, 'right': 180, 'down': 270, 'up': 90}
    else:
        angles = {'left': 90, 'right': 270, 'down': 180, 'up': 0}
    return angles.get(escape_dir, 270)

def _calc_companion_bbox(cx, cy, escape_dir, lib_id='', companion=None):
    """Calculate companion bbox given center and escape direction.

    Includes actual text measurements for terminal labels, power symbols,
    reference designator, and value text.
    """
    body_half = COMP_HALF_LEN + BBOX_MARGIN
    width_half = (3.0 if _is_horizontal_pin_component(lib_id) else 1.5) + BBOX_MARGIN

    # Measure actual text widths and heights for labels attached to this companion
    terminal_text_width = 0.0
    terminal_text_height = 0.0
    if companion:
        # Terminal labels (e.g., "+3V3", "VDD_SPI")
        for label_text in companion.get('terminal_labels', {}).values():
            w, h = measure_text_size_mm(label_text)
            terminal_text_width = max(terminal_text_width, w)
            terminal_text_height = max(terminal_text_height, h)

        # Terminal power symbols (e.g., "GND")
        for pwr_name in companion.get('terminal_power', {}).values():
            if ':' in pwr_name:
                pwr_name = pwr_name.split(':')[-1]
            w, h = measure_text_size_mm(pwr_name)
            terminal_text_width = max(terminal_text_width, w)
            terminal_text_height = max(terminal_text_height, h)

        # Value text (e.g., "0.1uF", "10uF")
        value_text = companion.get('properties', {}).get('Value', '')
        if value_text:
            w, h = measure_text_size_mm(value_text)
            terminal_text_width = max(terminal_text_width, w)
            terminal_text_height = max(terminal_text_height, h)

    # Terminal extension = component pin length + label text height (for vertical clearance) + margin
    # The label sits between component and IC, so we need room for label height
    # Note: terminal_text_width is used for perpendicular spacing, not escape direction extension
    term_extension = COMP_HALF_LEN + terminal_text_height + GRID_MM + BBOX_MARGIN

    # Reference designator adds width perpendicular to component
    ref_height = 2.5  # Typical reference text height
    width_half = max(width_half, ref_height + BBOX_MARGIN)

    # Debug: show calculated bbox dimensions
    if companion:
        lib_short = lib_id.split(':')[-1] if ':' in lib_id else lib_id
        tool_log(f'BBox {lib_short}: text_w={terminal_text_width:.2f}mm, term_ext={term_extension:.2f}mm, width_half={width_half:.2f}mm')

    # term_extension includes label space - labels are on the IC-FACING side (opposite of escape direction)
    # body_half is just the component body on the AWAY side (same as escape direction)
    if escape_dir == 'left':
        # Escape left = IC is to the right, so term_extension goes right (toward IC)
        return {'min_x': cx - body_half, 'max_x': cx + term_extension, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'right':
        # Escape right = IC is to the left, so term_extension goes left (toward IC)
        return {'min_x': cx - term_extension, 'max_x': cx + body_half, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'up':
        # Escape up = IC is below, so term_extension goes down (toward IC, positive Y)
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - body_half, 'max_y': cy + term_extension}
    else:  # down
        # Escape down = IC is above, so term_extension goes up (toward IC, negative Y)
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - term_extension, 'max_y': cy + body_half}

def _calc_center_from_pin(px, py, offset_mm, escape_dir):
    """Calculate companion center given IC pin position, offset, and escape direction."""
    if escape_dir == 'left':
        return snap_to_grid(px - offset_mm), snap_to_grid(py)
    elif escape_dir == 'right':
        return snap_to_grid(px + offset_mm), snap_to_grid(py)
    elif escape_dir == 'up':
        return snap_to_grid(px), snap_to_grid(py - offset_mm)
    else:  # down
        return snap_to_grid(px), snap_to_grid(py + offset_mm)

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
# Pre-collect obstacles and build spatial index
# ---------------------------------------------------------------------------
_ic_uuid = get_uuid_str(ic_sym)
_all_obstacle_bboxes = collect_all_obstacle_bboxes(sch, exclude_ids={_ic_uuid})
_spatial = SpatialIndex(cell_size=10.0)
for _ob in _all_obstacle_bboxes:
    _spatial.insert(_ob)
_spatial.insert(ic_bbox_expanded)
_debug.append(f'Pre-collected {len(_all_obstacle_bboxes)} obstacles')

# Pre-fetch ALL IC pin positions
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
_debug.append(f'Pre-fetched {len(_ic_pin_map)} IC pin positions')

# Cache all existing symbols for reference counter
_all_symbols_cache = sch.symbols.get_all()

# Orientation transform for pin escape direction
_rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
_rot_steps = round(getattr(ic_sym, 'angle', 0) / 90) % 4

def transform_orientation(orient):
    o = orient
    for _ in range(_rot_steps):
        o = _rot90.get(o, o)
    if getattr(ic_sym, 'mirror_x', False):
        if o == 2: o = 3
        elif o == 3: o = 2
    if getattr(ic_sym, 'mirror_y', False):
        if o == 0: o = 1
        elif o == 1: o = 0
    return o

def get_escape_dir(orient):
    """Convert orientation enum to escape direction string."""
    if orient == 0: return 'left'
    elif orient == 1: return 'right'
    elif orient == 2: return 'down'
    elif orient == 3: return 'up'
    else: return 'right'

def _get_ic_pin_data(ic_pin):
    """Get IC pin position/orientation from pre-fetched map."""
    pin_data = _ic_pin_map.get(ic_pin)
    if pin_data:
        return pin_data
    for _pnum, _pdata in _ic_pin_map.items():
        if _pdata.get('pin_name', '') == ic_pin:
            return _pdata
    pin_result = sch.symbols.get_transformed_pin_position(ic_sym, ic_pin)
    if pin_result:
        return {'position': pin_result['position'], 'orientation': pin_result.get('orientation', 1)}
    return None

# ===========================================================================
# PHASE 1: Analyze all companions and group by escape direction
# ===========================================================================
_debug.append('--- PHASE 1: Grouping companions by escape direction ---')

# Data structure for each companion's computed layout
companion_layouts = []  # List of {index, companion, px, py, escape_dir, cx, cy, is_power_only, ...}

# Group companions by escape direction
groups = {'up': [], 'down': [], 'left': [], 'right': []}

for i, companion in enumerate(companions_input):
    lib_id = companion.get('lib_id', '')
    ic_pin = companion.get('ic_pin', '')

    if not lib_id:
        results.append({'index': i, 'error': 'lib_id is required'})
        continue
    if not ic_pin:
        results.append({'index': i, 'error': 'ic_pin is required'})
        continue

    pin_data = _get_ic_pin_data(ic_pin)
    if not pin_data:
        results.append({'index': i, 'error': f'Pin not found: {ic_pin}'})
        continue

    px = pin_data['position'].x / 1_000_000
    py = pin_data['position'].y / 1_000_000
    orient = transform_orientation(pin_data['orientation'])
    escape_dir = get_escape_dir(orient)

    is_power_only = lib_id.startswith('power:')

    layout = {
        'index': i,
        'companion': companion,
        'lib_id': lib_id,
        'ic_pin': ic_pin,
        'px': px,
        'py': py,
        'escape_dir': escape_dir,
        'is_power_only': is_power_only,
        'cx': None,  # To be computed
        'cy': None,
        'offset': None,
    }
    companion_layouts.append(layout)

    if not is_power_only:
        groups[escape_dir].append(layout)

_debug.append(f'Groups: up={len(groups["up"])}, down={len(groups["down"])}, left={len(groups["left"])}, right={len(groups["right"])}')

# ===========================================================================
# PHASE 2: Compute unified layout for each group
# ===========================================================================
_debug.append('--- PHASE 2: Computing unified layouts ---')

def _get_companion_perp_width(companion, escape_dir):
    """Calculate required perpendicular width for a companion including labels."""
    # Base component width
    lib_id = companion.get('lib_id', '')
    base_width = 3.0 if _is_horizontal_pin_component(lib_id) else 1.5

    # Add label widths (labels extend perpendicular to escape direction)
    max_label_width = 0.0

    # For up/down escape, labels are horizontal and add to X width
    # For left/right escape, labels are vertical (rotated) so less width impact
    if escape_dir in ('up', 'down'):
        # Terminal labels
        for label_text in companion.get('terminal_labels', {}).values():
            w, _ = measure_text_size_mm(label_text)
            max_label_width = max(max_label_width, w)

        # Value text
        value_text = companion.get('properties', {}).get('Value', '')
        if value_text:
            w, _ = measure_text_size_mm(value_text)
            max_label_width = max(max_label_width, w)

        # Reference designator estimate (C##)
        ref_w, _ = measure_text_size_mm("C99")  # Typical ref length
        max_label_width = max(max_label_width, ref_w)

    # Total width = max of component body or label width
    total_half_width = max(base_width, max_label_width / 2 + 1.0) + BBOX_MARGIN
    return total_half_width * 2  # Full width

def compute_group_layout(group, escape_dir):
    """Compute positions for all companions in a group.

    Algorithm:
    1. Sort companions by IC pin perpendicular position
    2. Cluster by proximity (gap > threshold starts new cluster)
    3. Within each cluster, place side-by-side with proper spacing based on actual label widths
    4. Center each cluster above its IC pins
    5. Find offset that clears obstacles for each cluster
    """
    if not group:
        return

    # Sort by perpendicular position (X for up/down, Y for left/right)
    if escape_dir in ('up', 'down'):
        group.sort(key=lambda c: c['px'])
        get_perp = lambda c: c['px']
    else:
        group.sort(key=lambda c: c['py'])
        get_perp = lambda c: c['py']

    # Cluster companions by IC pin proximity
    # Gap > cluster_gap_threshold starts a new cluster
    cluster_gap_threshold = MIN_PERPENDICULAR_SPACING * 2  # ~10mm gap = new cluster
    clusters = []
    current_cluster = []

    for comp in group:
        if not current_cluster:
            current_cluster.append(comp)
        else:
            prev_perp = get_perp(current_cluster[-1])
            curr_perp = get_perp(comp)
            if curr_perp - prev_perp > cluster_gap_threshold:
                # Gap too big, start new cluster
                clusters.append(current_cluster)
                current_cluster = [comp]
            else:
                current_cluster.append(comp)

    if current_cluster:
        clusters.append(current_cluster)

    _debug.append(f'  {escape_dir}: {len(group)} companions in {len(clusters)} clusters')

    # Process each cluster
    for cluster in clusters:
        # Calculate required spacing for each companion based on actual label widths
        companion_widths = []
        for comp in cluster:
            w = _get_companion_perp_width(comp['companion'], escape_dir)
            companion_widths.append(w)
            tool_log(f'  Companion perp width: {w:.2f}mm')

        # Calculate positions with proper spacing between each pair
        # Spacing between i and i+1 = (width[i] + width[i+1]) / 2
        positions = []
        current_pos = 0.0
        for i, comp in enumerate(cluster):
            if i == 0:
                positions.append(0.0)
            else:
                # Spacing = half of previous width + half of current width
                spacing = (companion_widths[i-1] + companion_widths[i]) / 2
                spacing = max(spacing, MIN_PERPENDICULAR_SPACING)  # Minimum spacing
                current_pos += spacing
                positions.append(current_pos)

        # Center the cluster around the midpoint of IC pins
        pin_perps = [get_perp(c) for c in cluster]
        cluster_center = (min(pin_perps) + max(pin_perps)) / 2
        total_span = positions[-1] if positions else 0
        offset = cluster_center - total_span / 2

        for i, comp in enumerate(cluster):
            comp['final_perp'] = snap_to_grid(offset + positions[i])

        # Calculate label buffer for this cluster (to be added after obstacle avoidance)
        max_label_buffer = max(_calc_label_buffer(c['companion']) for c in cluster)
        label_buffer_grids = int(max_label_buffer / GRID_MM) + 1 if max_label_buffer > 0 else 0

        # Find minimum offset that clears obstacles for this cluster
        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 1):
            offset_mm = try_grids * GRID_MM
            all_clear = True

            for comp in cluster:
                if escape_dir in ('up', 'down'):
                    cx, cy = _calc_center_from_pin(comp['final_perp'], comp['py'], offset_mm, escape_dir)
                else:
                    cx, cy = _calc_center_from_pin(comp['px'], comp['final_perp'], offset_mm, escape_dir)

                bbox = _calc_companion_bbox(cx, cy, escape_dir, comp['lib_id'], comp['companion'])
                if _spatial.any_overlap(bbox):
                    all_clear = False
                    break

            if all_clear:
                # Add label buffer ON TOP of obstacle-clear offset
                final_offset_grids = try_grids + label_buffer_grids
                final_offset_mm = final_offset_grids * GRID_MM

                # Assign positions with the final offset (includes label buffer)
                for comp in cluster:
                    if escape_dir in ('up', 'down'):
                        cx, cy = _calc_center_from_pin(comp['final_perp'], comp['py'], final_offset_mm, escape_dir)
                    else:
                        cx, cy = _calc_center_from_pin(comp['px'], comp['final_perp'], final_offset_mm, escape_dir)

                    comp['cx'] = cx
                    comp['cy'] = cy
                    comp['offset'] = final_offset_mm

                    # Register as obstacle for subsequent clusters
                    comp_bbox = _calc_companion_bbox(cx, cy, escape_dir, comp['lib_id'], comp['companion'])
                    _spatial.insert(comp_bbox)

                break
        else:
            # Fallback at max offset
            offset_mm = MAX_OFFSET_GRIDS * GRID_MM
            for comp in cluster:
                if escape_dir in ('up', 'down'):
                    cx, cy = _calc_center_from_pin(comp['final_perp'], comp['py'], offset_mm, escape_dir)
                else:
                    cx, cy = _calc_center_from_pin(comp['px'], comp['final_perp'], offset_mm, escape_dir)

                comp['cx'] = cx
                comp['cy'] = cy
                comp['offset'] = offset_mm

# Compute layout for each group
for escape_dir in ['up', 'down', 'left', 'right']:
    compute_group_layout(groups[escape_dir], escape_dir)

# Handle power-only companions (place directly at pin)
for layout in companion_layouts:
    if layout['is_power_only']:
        layout['cx'] = layout['px']
        layout['cy'] = layout['py']

# ===========================================================================
# PHASE 3: Execute placement
# ===========================================================================
_debug.append('--- PHASE 3: Placing companions ---')

def place_power_symbol(pin_pos_x, pin_pos_y, power_name, escape_dir):
    """Place a power symbol at the given pin position."""
    pwr_name_upper = power_name.upper()
    is_gnd = 'GND' in pwr_name_upper or 'VSS' in pwr_name_upper
    pwr_offset = GRID_MM

    if escape_dir == 'left':
        pwr_x, pwr_y = snap_to_grid(pin_pos_x - pwr_offset), snap_to_grid(pin_pos_y)
        pwr_angle = 270 if is_gnd else 90
    elif escape_dir == 'right':
        pwr_x, pwr_y = snap_to_grid(pin_pos_x + pwr_offset), snap_to_grid(pin_pos_y)
        pwr_angle = 90 if is_gnd else 270
    elif escape_dir == 'down':
        pwr_x, pwr_y = snap_to_grid(pin_pos_x), snap_to_grid(pin_pos_y + pwr_offset)
        pwr_angle = 0 if is_gnd else 180
    else:  # up
        pwr_x, pwr_y = snap_to_grid(pin_pos_x), snap_to_grid(pin_pos_y - pwr_offset)
        pwr_angle = 180 if is_gnd else 0

    pwr_pos = Vector2.from_xy_mm(pwr_x, pwr_y)
    pwr_sym = sch.labels.add_power(power_name, pwr_pos, angle=pwr_angle)

    _w = sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(pin_pos_x), snap_to_grid(pin_pos_y)), pwr_pos)
    if _w: placed_wire_objs.append(_w)

    return pwr_sym, pwr_x, pwr_y

def draw_orthogonal_wire(px, py, cpx, cpy, escape_dir):
    """Draw L-shaped orthogonal wire from IC pin to companion pin.

    Uses escape direction to determine whether to go horizontal-first or vertical-first.
    """
    x0, y0 = snap_to_grid(px), snap_to_grid(py)
    x1, y1 = snap_to_grid(cpx), snap_to_grid(cpy)

    if abs(x1 - x0) < 0.01 and abs(y1 - y0) < 0.01:
        return  # No wire needed

    # If already aligned, draw single straight wire
    if abs(x1 - x0) < 0.01 or abs(y1 - y0) < 0.01:
        _w = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
        if _w: placed_wire_objs.append(_w)
        return

    # L-shaped routing: choose corner based on escape direction
    # For up/down escapes: go vertical first (escape), then horizontal
    # For left/right escapes: go horizontal first (escape), then vertical
    if escape_dir in ('up', 'down'):
        # Vertical first, then horizontal
        corner_x, corner_y = x0, y1
    else:
        # Horizontal first, then vertical
        corner_x, corner_y = x1, y0

    # Draw two segments
    _w1 = sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(corner_x, corner_y))
    if _w1: placed_wire_objs.append(_w1)
    _w2 = sch.wiring.add_wire(Vector2.from_xy_mm(corner_x, corner_y), Vector2.from_xy_mm(x1, y1))
    if _w2: placed_wire_objs.append(_w2)

def place_companion(layout):
    """Place a single companion at its computed position."""
    companion = layout['companion']
    lib_id = layout['lib_id']
    escape_dir = layout['escape_dir']
    cx, cy = layout['cx'], layout['cy']
    px, py = layout['px'], layout['py']
    index = layout['index']

    # Handle power-only
    if layout['is_power_only']:
        power_name = lib_id[6:]  # Remove "power:" prefix
        pwr_sym, pwr_x, pwr_y = place_power_symbol(px, py, power_name, escape_dir)

        placed_companions.append({
            'index': index,
            'lib_id': lib_id,
            'ic_pin': layout['ic_pin'],
            'ref': pwr_sym.reference if hasattr(pwr_sym, 'reference') else power_name,
            'cx': pwr_x,
            'cy': pwr_y,
            'position': [round(pwr_x, 2), round(pwr_y, 2)],
            'escape_dir': escape_dir,
            'power_only': True
        })
        return None

    # Calculate rotation
    reverse = companion.get('reverse', False)
    comp_angle = _get_component_angle(escape_dir, lib_id)
    if reverse:
        comp_angle = (comp_angle + 180) % 360

    # Create symbol
    comp_pos = Vector2.from_xy_mm(cx, cy)
    comp_sym = sch.symbols.add(lib_id=lib_id, position=comp_pos, angle=comp_angle)
    if not comp_sym:
        raise ValueError(f'Failed to create symbol: {lib_id}')

    # Set properties
    properties = companion.get('properties', {})
    for prop_name, prop_value in properties.items():
        if prop_name == 'Value':
            sch.symbols.set_value(comp_sym, prop_value)
        elif prop_name == 'Footprint':
            sch.symbols.set_footprint(comp_sym, prop_value)

    # Get pin positions
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

    # Determine companion's IC-facing pin position
    wire_pin = '1' if reverse else '2'
    if wire_pin in comp_pin_map:
        cpwx, cpwy = comp_pin_map[wire_pin]['x'], comp_pin_map[wire_pin]['y']
    else:
        cpwx, cpwy = cx, cy

    # Check if we have terminal_labels - if so, place label as part of wire chain
    terminal_labels = companion.get('terminal_labels', {})
    label_positions = {}  # pin -> (lbl_x, lbl_y) for labels placed in chain

    for pin_num, label_text in terminal_labels.items():
        actual_pin = pin_num
        if reverse:
            if pin_num == '1': actual_pin = '2'
            elif pin_num == '2': actual_pin = '1'

        if actual_pin != wire_pin:
            continue  # Only handle labels on the IC-facing pin here

        # Calculate label position: between IC and cap
        _, label_height = measure_text_size_mm(label_text)
        label_buffer = max(label_height + GRID_MM, GRID_MM * 2)

        # Label goes at: cap_pin + label_buffer toward IC
        if escape_dir == 'up':
            lbl_x, lbl_y = cpwx, snap_to_grid(cpwy + label_buffer)
        elif escape_dir == 'down':
            lbl_x, lbl_y = cpwx, snap_to_grid(cpwy - label_buffer)
        elif escape_dir == 'left':
            lbl_x, lbl_y = snap_to_grid(cpwx + label_buffer), cpwy
        else:  # right
            lbl_x, lbl_y = snap_to_grid(cpwx - label_buffer), cpwy

        label_positions[actual_pin] = (lbl_x, lbl_y)

        # Draw wire chain: IC → label → cap
        draw_orthogonal_wire(px, py, lbl_x, lbl_y, escape_dir)
        _w = sch.wiring.add_wire(
            Vector2.from_xy_mm(lbl_x, lbl_y),
            Vector2.from_xy_mm(snap_to_grid(cpwx), snap_to_grid(cpwy))
        )
        if _w: placed_wire_objs.append(_w)

        # Place label at the intermediate position
        sch.labels.add_local(label_text, Vector2.from_xy_mm(lbl_x, lbl_y))

    # If no label on IC-facing pin, draw direct wire from IC to cap
    if wire_pin not in label_positions:
        draw_orthogonal_wire(px, py, cpwx, cpwy, escape_dir)

    # Register bbox
    comp_bbox = _calc_companion_bbox(cx, cy, escape_dir, lib_id, companion)
    _spatial.insert(comp_bbox)

    # Handle terminal_power
    terminal_power = companion.get('terminal_power', {})
    for pin_num, pwr_name in terminal_power.items():
        actual_pin = pin_num
        if reverse:
            if pin_num == '1': actual_pin = '2'
            elif pin_num == '2': actual_pin = '1'

        if ':' in pwr_name:
            pwr_name = pwr_name.split(':')[-1]

        try:
            if actual_pin in comp_pin_map:
                pwr_px, pwr_py = comp_pin_map[actual_pin]['x'], comp_pin_map[actual_pin]['y']
            else:
                continue
            place_power_symbol(pwr_px, pwr_py, pwr_name, escape_dir)
        except Exception:
            pass

    # Handle terminal_labels on non-IC-facing pins (IC-facing labels handled above in wire chain)
    # These are placed directly at the pin position
    for pin_num, label_text in terminal_labels.items():
        actual_pin = pin_num
        if reverse:
            if pin_num == '1': actual_pin = '2'
            elif pin_num == '2': actual_pin = '1'

        # Skip if already placed in wire chain
        if actual_pin in label_positions:
            continue

        try:
            if actual_pin in comp_pin_map:
                lbl_px, lbl_py = comp_pin_map[actual_pin]['x'], comp_pin_map[actual_pin]['y']
            else:
                continue

            # Place label directly at pin (no buffer needed for away-from-IC pins)
            lbl_pos = Vector2.from_xy_mm(snap_to_grid(lbl_px), snap_to_grid(lbl_py))
            sch.labels.add_local(label_text, lbl_pos)
        except Exception:
            pass

    # Record placement
    comp_uuid = str(comp_sym.id.value) if hasattr(comp_sym, 'id') and hasattr(comp_sym.id, 'value') else ''
    placed_companions.append({
        'index': index,
        'lib_id': lib_id,
        'ic_pin': layout['ic_pin'],
        'ref': comp_sym.reference,
        'uuid': comp_uuid,
        'cx': cx,
        'cy': cy,
        'position': [round(cx, 2), round(cy, 2)],
        'escape_dir': escape_dir
    })

    # Return away pin for chaining
    away_pin = '1' if not reverse else '2'
    if away_pin in comp_pin_map:
        return (comp_sym, comp_pin_map[away_pin]['x'], comp_pin_map[away_pin]['y'], escape_dir)
    return (comp_sym, cx, cy, escape_dir)

# Place all companions
for layout in companion_layouts:
    if layout['cx'] is None:
        continue  # Skip if position couldn't be computed

    try:
        result = place_companion(layout)

        # Handle chains
        chain = layout['companion'].get('chain', [])
        if chain and result:
            _, away_x, away_y, esc_dir = result

            # Determine if chain items are parallel (all have terminal_power on pin 1)
            # or series (no terminal_power, they chain together)
            all_have_gnd_on_pin1 = all(
                chain_item.get('terminal_power', {}).get('1') for chain_item in chain
            )

            if all_have_gnd_on_pin1:
                # PARALLEL placement: all chain items connect from the same point
                # Place them perpendicular to escape direction with spacing
                branch_x, branch_y = away_x, away_y

                for chain_idx, chain_item in enumerate(chain):
                    chain_lib = chain_item.get('lib_id', '')
                    if not chain_lib:
                        continue

                    # Offset perpendicular to escape direction
                    perp_offset = chain_idx * MIN_PERPENDICULAR_SPACING
                    chain_offset = COMP_HALF_LEN + GRID_MM * 2

                    if esc_dir == 'left':
                        chain_cx = snap_to_grid(branch_x - chain_offset)
                        chain_cy = snap_to_grid(branch_y + perp_offset)
                    elif esc_dir == 'right':
                        chain_cx = snap_to_grid(branch_x + chain_offset)
                        chain_cy = snap_to_grid(branch_y + perp_offset)
                    elif esc_dir == 'up':
                        chain_cx = snap_to_grid(branch_x + perp_offset)
                        chain_cy = snap_to_grid(branch_y - chain_offset)
                    else:  # down
                        chain_cx = snap_to_grid(branch_x + perp_offset)
                        chain_cy = snap_to_grid(branch_y + chain_offset)

                    chain_layout = {
                        'index': f'{layout["index"]}.chain[{chain_idx}]',
                        'companion': chain_item,
                        'lib_id': chain_lib,
                        'ic_pin': '',
                        'px': branch_x,  # All connect from same branch point
                        'py': branch_y,
                        'escape_dir': esc_dir,
                        'is_power_only': chain_lib.startswith('power:'),
                        'cx': chain_cx,
                        'cy': chain_cy,
                    }

                    place_companion(chain_layout)

            else:
                # SERIES placement: chain items connect end-to-end
                for chain_idx, chain_item in enumerate(chain):
                    chain_lib = chain_item.get('lib_id', '')
                    if not chain_lib:
                        continue

                    # Compute chain position (continue in escape direction)
                    chain_offset = (chain_idx + 1) * (COMP_HALF_LEN * 2 + GRID_MM * 2)
                    if esc_dir == 'left':
                        chain_cx = snap_to_grid(away_x - chain_offset)
                        chain_cy = away_y
                    elif esc_dir == 'right':
                        chain_cx = snap_to_grid(away_x + chain_offset)
                        chain_cy = away_y
                    elif esc_dir == 'up':
                        chain_cx = away_x
                        chain_cy = snap_to_grid(away_y - chain_offset)
                    else:
                        chain_cx = away_x
                        chain_cy = snap_to_grid(away_y + chain_offset)

                    chain_layout = {
                        'index': f'{layout["index"]}.chain[{chain_idx}]',
                        'companion': chain_item,
                        'lib_id': chain_lib,
                        'ic_pin': '',
                        'px': away_x,
                        'py': away_y,
                        'escape_dir': esc_dir,
                        'is_power_only': chain_lib.startswith('power:'),
                        'cx': chain_cx,
                        'cy': chain_cy,
                    }

                    chain_result = place_companion(chain_layout)
                    if chain_result:
                        _, away_x, away_y, esc_dir = chain_result

    except Exception as e:
        results.append({
            'index': layout['index'],
            'lib_id': layout['lib_id'],
            'ic_pin': layout['ic_pin'],
            'error': str(e)
        })

# ---------------------------------------------------------------------------
# Auto-place junctions
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
# Auto-annotate
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
        uuid_to_ref = {}
        for s in sch.symbols.get_all():
            s_uuid = str(s.id.value) if hasattr(s, 'id') and hasattr(s.id, 'value') else ''
            uuid_to_ref[s_uuid] = getattr(s, 'reference', '?')
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
