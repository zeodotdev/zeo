#include "sch_companion_handler.h"
#include <sstream>
#include <cmath>


bool SCH_COMPANION_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_place_companions";
}


std::string SCH_COMPANION_HANDLER::Execute( const std::string& aToolName,
                                             const nlohmann::json& aInput )
{
    return "Error: sch_place_companions requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_COMPANION_HANDLER::GetDescription( const std::string& aToolName,
                                                    const nlohmann::json& aInput ) const
{
    std::string icRef = aInput.value( "ic_ref", "?" );
    int count = 0;

    if( aInput.contains( "companions" ) && aInput["companions"].is_array() )
        count = static_cast<int>( aInput["companions"].size() );

    if( count > 0 )
        return "Placing " + std::to_string( count ) + " companions for " + icRef;

    return "Placing companions for " + icRef;
}


bool SCH_COMPANION_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_place_companions";
}


std::string SCH_COMPANION_HANDLER::GetIPCCommand( const std::string& aToolName,
                                                   const nlohmann::json& aInput ) const
{
    return "run_shell sch " + GeneratePlaceCompanionsCode( aInput );
}


std::string SCH_COMPANION_HANDLER::EscapePythonString( const std::string& aStr ) const
{
    std::string result;
    result.reserve( aStr.size() + 10 );

    for( char c : aStr )
    {
        switch( c )
        {
        case '\\': result += "\\\\"; break;
        case '\'': result += "\\'"; break;
        case '\"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }

    return result;
}


double SCH_COMPANION_HANDLER::SnapToGrid( double aMm, double aGrid )
{
    double gridUnits = std::round( aMm / aGrid );
    return std::round( gridUnits * aGrid * 1e4 ) / 1e4;
}


std::string SCH_COMPANION_HANDLER::GeneratePlaceCompanionsCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string icRef = aInput.value( "ic_ref", "" );

    if( icRef.empty() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'ic_ref is required'}))\n";
        return code.str();
    }

    if( !aInput.contains( "companions" ) || !aInput["companions"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'companions array is required'}))\n";
        return code.str();
    }

    auto companions = aInput["companions"];

    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";

    // Refresh preamble
    code << "# Refresh document to handle close/reopen cycles\n";
    code << "if hasattr(sch, 'refresh_document'):\n";
    code << "    if not sch.refresh_document():\n";
    code << "        raise RuntimeError('Schematic editor not open or document not available')\n";
    code << "\n";

    code << "ic_ref = '" << EscapePythonString( icRef ) << "'\n";
    code << "results = []\n";
    code << "placed_companions = []\n";
    code << "placed_wires = []  # List of wire segments [(x1,y1,x2,y2), ...] to avoid crossings\n";
    code << "_debug = []\n";
    code << "existing_labels = []  # Bboxes for existing labels to avoid overlap\n";
    code << "\n";

    // Constants
    code << "GRID_MM = 1.27  # 50 mil grid\n";
    code << "BBOX_MARGIN = 0.5  # Margin around components for spacing (tighter)\n";
    code << "MAX_OFFSET_GRIDS = 15  # More room to push out if needed\n";
    code << "MIN_OFFSET_GRIDS = 3  # Closer to IC (3.81mm)\n";
    code << "COMP_HALF_LEN = 3.81  # Half-length of 2-pin passive (center to pin)\n";
    code << "TERMINAL_EXTENSION = 5.08  # Smaller terminal area for power symbols/labels\n";
    code << "LABEL_CHAR_WIDTH = 1.0  # Approximate width per character in mm\n";
    code << "LABEL_HEIGHT = 2.5  # Approximate label height in mm\n";
    code << "\n";

    // Snap to grid function
    code << "def snap_to_grid(val):\n";
    code << "    return round(round(val / GRID_MM) * GRID_MM, 4)\n";
    code << "\n";

    // Build bboxes for IC (we need IC bbox for pull-back limit)
    code << "# Get IC symbol and its bounding box\n";
    code << "ic_sym = sch.symbols.get_by_ref(ic_ref)\n";
    code << "if not ic_sym:\n";
    code << "    print(json.dumps({'status': 'error', 'message': f'IC not found: {ic_ref}'}))\n";
    code << "    raise SystemExit()\n";
    code << "\n";
    code << "ic_bbox = sch.transform.get_bounding_box(ic_sym, units='mm', include_text=False)\n";
    code << "if not ic_bbox:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'Could not get IC bounding box'}))\n";
    code << "    raise SystemExit()\n";
    code << "\n";
    code << "# Expand IC bbox for margin\n";
    code << "ic_bbox_expanded = {\n";
    code << "    'min_x': ic_bbox['min_x'] - BBOX_MARGIN,\n";
    code << "    'max_x': ic_bbox['max_x'] + BBOX_MARGIN,\n";
    code << "    'min_y': ic_bbox['min_y'] - BBOX_MARGIN,\n";
    code << "    'max_y': ic_bbox['max_y'] + BBOX_MARGIN\n";
    code << "}\n";
    code << "_debug.append(f'IC bbox: ({ic_bbox_expanded[\"min_x\"]:.2f},{ic_bbox_expanded[\"min_y\"]:.2f})-({ic_bbox_expanded[\"max_x\"]:.2f},{ic_bbox_expanded[\"max_y\"]:.2f})')\n";
    code << "\n";

    // Collect existing labels to avoid overlap
    code << "# Collect existing labels (hierarchical, local, global) to avoid overlap\n";
    code << "def _estimate_label_bbox(pos_x, pos_y, text, angle=0):\n";
    code << "    \"\"\"Estimate bounding box for a label based on text length and position.\"\"\"\n";
    code << "    text_len = len(text) if text else 3\n";
    code << "    width = text_len * LABEL_CHAR_WIDTH + 1.0  # Extra padding\n";
    code << "    height = LABEL_HEIGHT\n";
    code << "    # Labels extend from their position based on justification (assume left-justified)\n";
    code << "    # Add margin around the label\n";
    code << "    margin = 1.0\n";
    code << "    if angle in (0, 180):\n";
    code << "        # Horizontal label\n";
    code << "        return {\n";
    code << "            'min_x': pos_x - margin,\n";
    code << "            'max_x': pos_x + width + margin,\n";
    code << "            'min_y': pos_y - height/2 - margin,\n";
    code << "            'max_y': pos_y + height/2 + margin\n";
    code << "        }\n";
    code << "    else:\n";
    code << "        # Vertical label\n";
    code << "        return {\n";
    code << "            'min_x': pos_x - height/2 - margin,\n";
    code << "            'max_x': pos_x + height/2 + margin,\n";
    code << "            'min_y': pos_y - margin,\n";
    code << "            'max_y': pos_y + width + margin\n";
    code << "        }\n";
    code << "\n";
    code << "# Query existing labels on the schematic\n";
    code << "try:\n";
    code << "    if hasattr(sch, 'labels'):\n";
    code << "        # Get all label types\n";
    code << "        for label in sch.labels.get_all():\n";
    code << "            try:\n";
    code << "                lbl_pos = getattr(label, 'position', None)\n";
    code << "                lbl_text = getattr(label, 'text', getattr(label, 'name', ''))\n";
    code << "                lbl_angle = getattr(label, 'angle', 0)\n";
    code << "                if lbl_pos:\n";
    code << "                    lbl_x = lbl_pos.x / 1_000_000 if hasattr(lbl_pos, 'x') else 0\n";
    code << "                    lbl_y = lbl_pos.y / 1_000_000 if hasattr(lbl_pos, 'y') else 0\n";
    code << "                    lbl_bbox = _estimate_label_bbox(lbl_x, lbl_y, lbl_text, lbl_angle)\n";
    code << "                    existing_labels.append(lbl_bbox)\n";
    code << "                    _debug.append(f'Label \"{lbl_text}\" bbox: ({lbl_bbox[\"min_x\"]:.1f},{lbl_bbox[\"min_y\"]:.1f})-({lbl_bbox[\"max_x\"]:.1f},{lbl_bbox[\"max_y\"]:.1f})')\n";
    code << "            except:\n";
    code << "                pass\n";
    code << "except:\n";
    code << "    pass  # No labels or labels API not available\n";
    code << "_debug.append(f'Found {len(existing_labels)} existing labels')\n";
    code << "\n";

    // Helper to detect horizontal-pin components (LED, diode) vs vertical-pin (R, C)
    code << "def _is_horizontal_pin_component(lib_id):\n";
    code << "    \"\"\"Check if component has horizontal pins at 0° rotation (LED, diode).\n";
    code << "    R/C have vertical pins at 0°, LED/D have horizontal pins.\"\"\"\n";
    code << "    lib_lower = lib_id.lower()\n";
    code << "    return 'led' in lib_lower or ':d' in lib_lower or lib_lower.endswith(':d') or '_d_' in lib_lower or 'diode' in lib_lower\n";
    code << "\n";
    code << "def _get_component_angle(escape_dir, lib_id):\n";
    code << "    \"\"\"Get correct rotation angle based on component type and escape direction.\n";
    code << "    \n";
    code << "    For vertical-pin components (R, C) at 0°: pin 1 at top, pin 2 at bottom\n";
    code << "    For horizontal-pin components (LED, D) at 0°: pin 1 at left, pin 2 at right\n";
    code << "    \n";
    code << "    We always want pin 2 facing toward IC (for wiring), pin 1 away (for terminal).\n";
    code << "    \"\"\"\n";
    code << "    is_horiz = _is_horizontal_pin_component(lib_id)\n";
    code << "    \n";
    code << "    if is_horiz:\n";
    code << "        # Horizontal-pin components (LED, diode)\n";
    code << "        angles = {'left': 0, 'right': 180, 'down': 270, 'up': 90}\n";
    code << "    else:\n";
    code << "        # Vertical-pin components (R, C)\n";
    code << "        angles = {'left': 90, 'right': 270, 'down': 180, 'up': 0}\n";
    code << "    \n";
    code << "    return angles.get(escape_dir, 270)\n";
    code << "\n";

    // Helper functions for collision detection
    code << "def _bboxes_overlap(a, b):\n";
    code << "    \"\"\"Check if two bboxes overlap.\"\"\"\n";
    code << "    return (a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and\n";
    code << "            a['min_y'] < b['max_y'] and a['max_y'] > b['min_y'])\n";
    code << "\n";

    // Wire segment intersection detection
    code << "def _segments_intersect(seg1, seg2):\n";
    code << "    \"\"\"Check if two line segments intersect (cross each other).\n";
    code << "    Each segment is (x1, y1, x2, y2).\n";
    code << "    Returns True only for actual crossings, not T-junctions or shared endpoints.\n";
    code << "    \"\"\"\n";
    code << "    x1, y1, x2, y2 = seg1\n";
    code << "    x3, y3, x4, y4 = seg2\n";
    code << "    \n";
    code << "    # Check if segments share an endpoint (this is OK, not a crossing)\n";
    code << "    eps = 0.01\n";
    code << "    def pts_equal(ax, ay, bx, by):\n";
    code << "        return abs(ax - bx) < eps and abs(ay - by) < eps\n";
    code << "    if (pts_equal(x1, y1, x3, y3) or pts_equal(x1, y1, x4, y4) or\n";
    code << "        pts_equal(x2, y2, x3, y3) or pts_equal(x2, y2, x4, y4)):\n";
    code << "        return False\n";
    code << "    \n";
    code << "    # Check for parallel segments on same line (collinear)\n";
    code << "    # Horizontal segments\n";
    code << "    if abs(y1 - y2) < eps and abs(y3 - y4) < eps:\n";
    code << "        if abs(y1 - y3) < eps:  # Same Y level\n";
    code << "            # Check X overlap\n";
    code << "            return max(min(x1, x2), min(x3, x4)) < min(max(x1, x2), max(x3, x4))\n";
    code << "        return False\n";
    code << "    # Vertical segments\n";
    code << "    if abs(x1 - x2) < eps and abs(x3 - x4) < eps:\n";
    code << "        if abs(x1 - x3) < eps:  # Same X level\n";
    code << "            # Check Y overlap\n";
    code << "            return max(min(y1, y2), min(y3, y4)) < min(max(y1, y2), max(y3, y4))\n";
    code << "        return False\n";
    code << "    \n";
    code << "    # One horizontal, one vertical - check for crossing\n";
    code << "    if abs(y1 - y2) < eps:  # seg1 is horizontal\n";
    code << "        h_y, h_x1, h_x2 = y1, min(x1, x2), max(x1, x2)\n";
    code << "        v_x, v_y1, v_y2 = x3, min(y3, y4), max(y3, y4)\n";
    code << "    elif abs(x1 - x2) < eps:  # seg1 is vertical\n";
    code << "        v_x, v_y1, v_y2 = x1, min(y1, y2), max(y1, y2)\n";
    code << "        h_y, h_x1, h_x2 = y3, min(x3, x4), max(x3, x4)\n";
    code << "    else:\n";
    code << "        return False  # Neither is axis-aligned, shouldn't happen in schematics\n";
    code << "    \n";
    code << "    # Check if vertical segment crosses horizontal segment (strictly inside)\n";
    code << "    return (h_x1 + eps < v_x < h_x2 - eps) and (v_y1 + eps < h_y < v_y2 - eps)\n";
    code << "\n";

    code << "def _compute_wire_path(px, py, cx, cy, escape_dir):\n";
    code << "    \"\"\"Compute L-shaped wire path from IC pin (px,py) to companion (cx,cy).\n";
    code << "    Returns list of segments [(x1,y1,x2,y2), ...].\n";
    code << "    Uses vertical-first for horizontal escapes to minimize crossings.\n";
    code << "    \"\"\"\n";
    code << "    px, py = snap_to_grid(px), snap_to_grid(py)\n";
    code << "    cx, cy = snap_to_grid(cx), snap_to_grid(cy)\n";
    code << "    \n";
    code << "    if escape_dir in ('left', 'right'):\n";
    code << "        # Horizontal escape: vertical first, then horizontal\n";
    code << "        if abs(py - cy) < 0.01:\n";
    code << "            return [(px, py, cx, cy)]  # Straight horizontal\n";
    code << "        else:\n";
    code << "            return [(px, py, px, cy), (px, cy, cx, cy)]  # Vertical then horizontal\n";
    code << "    else:\n";
    code << "        # Vertical escape: horizontal first, then vertical\n";
    code << "        if abs(px - cx) < 0.01:\n";
    code << "            return [(px, py, cx, cy)]  # Straight vertical\n";
    code << "        else:\n";
    code << "            return [(px, py, cx, py), (cx, py, cx, cy)]  # Horizontal then vertical\n";
    code << "\n";

    code << "def _wire_path_crosses_existing(wire_path, placed_wires):\n";
    code << "    \"\"\"Check if any segment in wire_path crosses any existing wire.\"\"\"\n";
    code << "    for new_seg in wire_path:\n";
    code << "        for existing_seg in placed_wires:\n";
    code << "            if _segments_intersect(new_seg, existing_seg):\n";
    code << "                return True\n";
    code << "    return False\n";
    code << "\n";
    code << "def _calc_companion_bbox(cx, cy, escape_dir, lib_id=''):\n";
    code << "    \"\"\"Calculate companion bbox given center and escape direction.\n";
    code << "    Bbox is asymmetric: larger on pin1 side (away from IC) to include power symbols/labels.\n";
    code << "    LEDs and diodes get wider perpendicular spacing due to their triangular body shape.\n";
    code << "    \"\"\"\n";
    code << "    body_half = COMP_HALF_LEN + BBOX_MARGIN  # Body extent toward IC (pin 2 side)\n";
    code << "    term_half = COMP_HALF_LEN + TERMINAL_EXTENSION + BBOX_MARGIN  # Terminal extent away from IC (pin 1 side)\n";
    code << "    # Horizontal-pin components (LED, diode) have wider body perpendicular to axis\n";
    code << "    if _is_horizontal_pin_component(lib_id):\n";
    code << "        width_half = 3.0 + BBOX_MARGIN  # Wider for LED/diode triangular body\n";
    code << "    else:\n";
    code << "        width_half = 1.5 + BBOX_MARGIN  # Standard width for R/C\n";
    code << "    \n";
    code << "    if escape_dir == 'left':  # Component extends left, pin1 at far left\n";
    code << "        return {'min_x': cx - term_half, 'max_x': cx + body_half, 'min_y': cy - width_half, 'max_y': cy + width_half}\n";
    code << "    elif escape_dir == 'right':  # Component extends right, pin1 at far right\n";
    code << "        return {'min_x': cx - body_half, 'max_x': cx + term_half, 'min_y': cy - width_half, 'max_y': cy + width_half}\n";
    code << "    elif escape_dir == 'up':  # Component extends up, pin1 at top\n";
    code << "        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - term_half, 'max_y': cy + body_half}\n";
    code << "    else:  # down - Component extends down, pin1 at bottom\n";
    code << "        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - body_half, 'max_y': cy + term_half}\n";
    code << "\n";
    code << "def _calc_center_from_offset(px, py, offset_mm, escape_dir, perp_offset=0):\n";
    code << "    \"\"\"Calculate companion center given IC pin pos, offset, escape direction, and perpendicular offset.\"\"\"\n";
    code << "    if escape_dir == 'left':\n";
    code << "        return snap_to_grid(px - offset_mm), snap_to_grid(py + perp_offset)\n";
    code << "    elif escape_dir == 'right':\n";
    code << "        return snap_to_grid(px + offset_mm), snap_to_grid(py + perp_offset)\n";
    code << "    elif escape_dir == 'up':\n";
    code << "        return snap_to_grid(px + perp_offset), snap_to_grid(py - offset_mm)\n";
    code << "    else:  # down\n";
    code << "        return snap_to_grid(px + perp_offset), snap_to_grid(py + offset_mm)\n";
    code << "\n";
    code << "def _find_clear_position(px, py, escape_dir, placed_companions, ic_bbox_expanded, lib_id=''):\n";
    code << "    \"\"\"Find a non-overlapping position prioritizing closeness to IC.\n";
    code << "    \n";
    code << "    Algorithm: For each distance from IC (starting at minimum), try all perpendicular\n";
    code << "    offsets before moving further out. This keeps companions as close as possible.\n";
    code << "    Also avoids existing labels and wire crossings.\n";
    code << "    \"\"\"\n";
    code << "    _debug.append(f'_find_clear_position called: px={px:.2f}, py={py:.2f}, escape={escape_dir}, lib_id={lib_id}, placed={len(placed_companions)}, labels={len(existing_labels)}, wires={len(placed_wires)}')\n";
    code << "    \n";
    code << "    # Perpendicular step = 2.54mm (2 grid units) for tight stacking of adjacent pins\n";
    code << "    perp_step = GRID_MM * 2\n";
    code << "    max_perp_steps = 8  # Max perpendicular offset = ±10mm\n";
    code << "    \n";
    code << "    # Outer loop: push-out distance (prioritize staying close to IC)\n";
    code << "    for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 5):\n";
    code << "        try_offset = try_grids * GRID_MM\n";
    code << "        \n";
    code << "        # Inner loop: perpendicular offsets (0, +step, -step, +2*step, -2*step, ...)\n";
    code << "        for perp_idx in range(max_perp_steps):\n";
    code << "            if perp_idx == 0:\n";
    code << "                perp_offset = 0\n";
    code << "            else:\n";
    code << "                # Alternate +/- : idx 1 -> +1*step, idx 2 -> -1*step, idx 3 -> +2*step, etc.\n";
    code << "                mult = (perp_idx + 1) // 2\n";
    code << "                sign = 1 if perp_idx % 2 == 1 else -1\n";
    code << "                perp_offset = mult * perp_step * sign\n";
    code << "            \n";
    code << "            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir, perp_offset)\n";
    code << "            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)\n";
    code << "            \n";
    code << "            # Check against IC bbox\n";
    code << "            if _bboxes_overlap(try_bbox, ic_bbox_expanded):\n";
    code << "                continue\n";
    code << "            \n";
    code << "            # Check against placed companions\n";
    code << "            overlaps = False\n";
    code << "            for placed in placed_companions:\n";
    code << "                placed_bbox = _calc_companion_bbox(placed['cx'], placed['cy'], placed['escape_dir'], placed.get('lib_id', ''))\n";
    code << "                if _bboxes_overlap(try_bbox, placed_bbox):\n";
    code << "                    overlaps = True\n";
    code << "                    break\n";
    code << "            \n";
    code << "            # Check against existing labels\n";
    code << "            if not overlaps:\n";
    code << "                for lbl_bbox in existing_labels:\n";
    code << "                    if _bboxes_overlap(try_bbox, lbl_bbox):\n";
    code << "                        overlaps = True\n";
    code << "                        _debug.append(f'  Label collision at ({try_cx:.1f},{try_cy:.1f})')\n";
    code << "                        break\n";
    code << "            \n";
    code << "            # Check if wire path would cross existing wires\n";
    code << "            if not overlaps:\n";
    code << "                try_wire_path = _compute_wire_path(px, py, try_cx, try_cy, escape_dir)\n";
    code << "                if _wire_path_crosses_existing(try_wire_path, placed_wires):\n";
    code << "                    overlaps = True\n";
    code << "                    _debug.append(f'  Wire crossing at ({try_cx:.1f},{try_cy:.1f})')\n";
    code << "            \n";
    code << "            if not overlaps:\n";
    code << "                _debug.append(f'  FOUND: offset={try_grids} perp={perp_offset:.1f} -> ({try_cx:.2f}, {try_cy:.2f})')\n";
    code << "                return try_cx, try_cy, try_offset, perp_offset\n";
    code << "    \n";
    code << "    # Fallback: use minimum offset with no perp shift\n";
    code << "    _debug.append(f'  FALLBACK: no clear position found')\n";
    code << "    cx, cy = _calc_center_from_offset(px, py, MIN_OFFSET_GRIDS * GRID_MM, escape_dir, 0)\n";
    code << "    return cx, cy, MIN_OFFSET_GRIDS * GRID_MM, 0\n";
    code << "\n";
    // Reference counter for auto-generating refdes
    code << "# Track reference counters for auto-generation\n";
    code << "ref_counters = {}\n";
    code << "def get_next_ref(prefix):\n";
    code << "    if prefix not in ref_counters:\n";
    code << "        # Find highest existing refdes for this prefix\n";
    code << "        highest = 0\n";
    code << "        for sym in sch.symbols.get_all():\n";
    code << "            ref = getattr(sym, 'reference', '')\n";
    code << "            if ref.startswith(prefix):\n";
    code << "                try:\n";
    code << "                    num = int(ref[len(prefix):])\n";
    code << "                    highest = max(highest, num)\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "        ref_counters[prefix] = highest\n";
    code << "    ref_counters[prefix] += 1\n";
    code << "    return f'{prefix}{ref_counters[prefix]}'\n";
    code << "\n";

    code << "try:\n";
    // Orientation transform for pin escape direction
    code << "    # Orientation transform: apply symbol rotation and mirroring\n";
    code << "    _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}\n";
    code << "    _rot_steps = round(getattr(ic_sym, 'angle', 0) / 90) % 4\n";
    code << "\n";
    code << "    def transform_orientation(orient):\n";
    code << "        o = orient\n";
    code << "        for _ in range(_rot_steps):\n";
    code << "            o = _rot90.get(o, o)\n";
    code << "        if getattr(ic_sym, 'mirror_x', False):\n";
    code << "            if o == 0: o = 1\n";
    code << "            elif o == 1: o = 0\n";
    code << "        if getattr(ic_sym, 'mirror_y', False):\n";
    code << "            if o == 2: o = 3\n";
    code << "            elif o == 3: o = 2\n";
    code << "        return o\n";
    code << "\n";

    // Process each companion in the array
    for( size_t i = 0; i < companions.size(); ++i )
    {
        auto companion = companions[i];

        std::string libId = companion.value( "lib_id", "" );
        std::string icPin = companion.value( "ic_pin", "" );
        int offsetGrids = companion.value( "offset_grids", 3 );
        bool reverse = companion.value( "reverse", false );

        if( libId.empty() || icPin.empty() )
        {
            code << "    results.append({'index': " << i << ", 'error': 'lib_id and ic_pin are required'})\n";
            continue;
        }

        // Check if this is a power-only companion (lib_id starts with "power:")
        bool isPowerOnly = libId.find( "power:" ) == 0;
        std::string powerName;
        if( isPowerOnly )
        {
            powerName = libId.substr( 6 );  // Extract power name after "power:"
        }

        double offsetMm = SnapToGrid( offsetGrids * 1.27 );

        code << "    # Companion " << i << ": " << libId << " at " << icPin;
        if( isPowerOnly )
            code << " (power-only)";
        if( reverse )
            code << " (reversed)";
        code << "\n";
        code << "    try:\n";
        code << "        lib_id_" << i << " = '" << EscapePythonString( libId ) << "'\n";
        code << "        ic_pin_" << i << " = '" << EscapePythonString( icPin ) << "'\n";
        code << "        offset_mm_" << i << " = " << offsetMm << "\n";
        code << "        is_power_only_" << i << " = " << ( isPowerOnly ? "True" : "False" ) << "\n";
        code << "        reverse_" << i << " = " << ( reverse ? "True" : "False" ) << "\n";
        code << "\n";

        // Get IC pin position and orientation
        code << "        pin_result_" << i << " = sch.symbols.get_transformed_pin_position(ic_sym, ic_pin_" << i << ")\n";
        code << "        if not pin_result_" << i << ":\n";
        code << "            raise ValueError(f'Pin not found on IC: {ic_pin_" << i << "}')\n";
        code << "\n";
        code << "        px_" << i << " = pin_result_" << i << "['position'].x / 1_000_000\n";
        code << "        py_" << i << " = pin_result_" << i << "['position'].y / 1_000_000\n";
        code << "        orient_" << i << " = transform_orientation(pin_result_" << i << ".get('orientation', 1))\n";
        code << "\n";

        // Calculate companion position based on pin escape direction
        // For 2-pin passives (R, C): pin 2 connects to IC, pin 1 for power/label
        // This matches standard circuit flow: power/label → pin1 → pin2 → IC
        // Resistor/Cap pins are at ±3.81mm from center along the pin axis
        code << "        # Calculate companion position based on escape direction\n";
        code << "        # orient: 0=PIN_RIGHT (escape left), 1=PIN_LEFT (escape right),\n";
        code << "        #         2=PIN_UP (escape down), 3=PIN_DOWN (escape up)\n";
        code << "        # Position companion so pin2 is toward IC, pin1 is away (for power/label)\n";
        code << "        if orient_" << i << " == 0:    # pin points right, escape left\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " - offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            escape_dir_" << i << " = 'left'\n";
        code << "        elif orient_" << i << " == 1:  # pin points left, escape right\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " + offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            escape_dir_" << i << " = 'right'\n";
        code << "        elif orient_" << i << " == 2:  # pin points up, escape down\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << " + offset_mm_" << i << ")\n";
        code << "            escape_dir_" << i << " = 'down'\n";
        code << "        elif orient_" << i << " == 3:  # pin points down, escape up\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << " - offset_mm_" << i << ")\n";
        code << "            escape_dir_" << i << " = 'up'\n";
        code << "        else:\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " + offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            escape_dir_" << i << " = 'right'\n";
        code << "\n";
        code << "        # Get rotation angle based on component type (handles LED/diode vs R/C)\n";
        code << "        comp_angle_" << i << " = _get_component_angle(escape_dir_" << i << ", lib_id_" << i << ")\n";
        code << "\n";

        // Handle reverse parameter - add 180° rotation and swap wire pin
        if( reverse )
        {
            code << "        # Apply reverse: rotate 180° more so pin 1 faces IC instead of pin 2\n";
            code << "        comp_angle_" << i << " = (comp_angle_" << i << " + 180) % 360\n";
            code << "\n";
        }

        // Power-only code path - place power symbol directly at IC pin instead of component
        if( isPowerOnly )
        {
            code << "        # Power-only companion: place power symbol directly at IC pin\n";
            code << "        pwr_offset = GRID_MM * 2  # 2 grid units away from IC pin\n";
            code << "        pwr_name_upper = '" << EscapePythonString( powerName ) << "'.upper()\n";
            code << "        is_gnd = 'GND' in pwr_name_upper or 'VSS' in pwr_name_upper\n";
            code << "        \n";
            code << "        # Calculate power symbol position based on escape direction\n";
            code << "        if escape_dir_" << i << " == 'left':\n";
            code << "            pwr_x_" << i << " = snap_to_grid(px_" << i << " - pwr_offset)\n";
            code << "            pwr_y_" << i << " = snap_to_grid(py_" << i << ")\n";
            code << "            pwr_angle_" << i << " = 270 if is_gnd else 90  # GND wire exits right, VCC wire exits right\n";
            code << "        elif escape_dir_" << i << " == 'right':\n";
            code << "            pwr_x_" << i << " = snap_to_grid(px_" << i << " + pwr_offset)\n";
            code << "            pwr_y_" << i << " = snap_to_grid(py_" << i << ")\n";
            code << "            pwr_angle_" << i << " = 90 if is_gnd else 270  # GND wire exits left, VCC wire exits left\n";
            code << "        elif escape_dir_" << i << " == 'down':\n";
            code << "            pwr_x_" << i << " = snap_to_grid(px_" << i << ")\n";
            code << "            pwr_y_" << i << " = snap_to_grid(py_" << i << " + pwr_offset)\n";
            code << "            pwr_angle_" << i << " = 0 if is_gnd else 180  # GND down (natural), VCC up\n";
            code << "        else:  # up\n";
            code << "            pwr_x_" << i << " = snap_to_grid(px_" << i << ")\n";
            code << "            pwr_y_" << i << " = snap_to_grid(py_" << i << " - pwr_offset)\n";
            code << "            pwr_angle_" << i << " = 180 if is_gnd else 0  # GND up (inverted), VCC up\n";
            code << "        \n";
            code << "        pwr_pos_" << i << " = Vector2.from_xy_mm(pwr_x_" << i << ", pwr_y_" << i << ")\n";
            code << "        pwr_sym_" << i << " = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pwr_pos_" << i << ", angle=pwr_angle_" << i << ")\n";
            code << "        \n";
            code << "        # Wire from IC pin to power symbol\n";
            code << "        sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(px_" << i << "), snap_to_grid(py_" << i << ")), pwr_pos_" << i << ")\n";
            code << "        _debug.append(f'Power-only: " << EscapePythonString( powerName ) << " at ({pwr_x_" << i << ":.2f},{pwr_y_" << i << ":.2f})')\n";
            code << "        \n";
            code << "        # Record power-only placement\n";
            code << "        placed_companions.append({\n";
            code << "            'index': " << i << ",\n";
            code << "            'lib_id': lib_id_" << i << ",\n";
            code << "            'ic_pin': ic_pin_" << i << ",\n";
            code << "            'ref': pwr_sym_" << i << ".reference if hasattr(pwr_sym_" << i << ", 'reference') else '" << EscapePythonString( powerName ) << "',\n";
            code << "            'cx': pwr_x_" << i << ",\n";
            code << "            'cy': pwr_y_" << i << ",\n";
            code << "            'position': [round(pwr_x_" << i << ", 2), round(pwr_y_" << i << ", 2)],\n";
            code << "            'escape_dir': escape_dir_" << i << ",\n";
            code << "            'power_only': True\n";
            code << "        })\n";
            code << "\n";

            // Close the try block for power-only
            code << "    except Exception as e_" << i << ":\n";
            code << "        results.append({'index': " << i << ", 'lib_id': '" << EscapePythonString( libId )
                 << "', 'ic_pin': '" << EscapePythonString( icPin ) << "', 'error': str(e_" << i << ")})\n";
            code << "\n";
            continue;  // Skip the rest of the component placement for power-only
        }

        // Find non-overlapping position using push-out + perpendicular staggering
        code << "        # Find clear position using push-out + perpendicular staggering\n";
        code << "        cx_" << i << ", cy_" << i << ", _offset_" << i << ", _perp_" << i << " = _find_clear_position(\n";
        code << "            px_" << i << ", py_" << i << ", escape_dir_" << i << ", placed_companions, ic_bbox_expanded, lib_id_" << i << ")\n";
        code << "        _debug.append(f'Comp" << i << ": offset={_offset_" << i << ":.2f}mm perp={_perp_" << i << ":.2f}mm at ({cx_" << i << ":.2f},{cy_" << i << ":.2f})')\n";
        code << "\n";

        // Create companion symbol using sch.symbols.add()
        code << "        # Create companion symbol\n";
        code << "        comp_pos_" << i << " = Vector2.from_xy_mm(cx_" << i << ", cy_" << i << ")\n";
        code << "        comp_sym_" << i << " = sch.symbols.add(\n";
        code << "            lib_id=lib_id_" << i << ",\n";
        code << "            position=comp_pos_" << i << ",\n";
        code << "            angle=comp_angle_" << i << "\n";
        code << "        )\n";
        code << "        if not comp_sym_" << i << ":\n";
        code << "            raise ValueError(f'Failed to create companion symbol: {lib_id_" << i << "}')\n";
        code << "\n";

        // NOTE: We add companion to body_bboxes AFTER drawing wires to it,
        // so the companion's own wires don't get blocked by its own body.

        // Set properties from input using the correct API
        if( companion.contains( "properties" ) && companion["properties"].is_object() )
        {
            for( auto it = companion["properties"].begin(); it != companion["properties"].end(); ++it )
            {
                std::string propName = it.key();
                std::string propValue = it.value().get<std::string>();

                if( propName == "Value" )
                {
                    code << "        sch.symbols.set_value(comp_sym_" << i << ", '"
                         << EscapePythonString( propValue ) << "')\n";
                }
                else if( propName == "Footprint" )
                {
                    code << "        sch.symbols.set_footprint(comp_sym_" << i << ", '"
                         << EscapePythonString( propValue ) << "')\n";
                }
                else
                {
                    // For other properties, use set_field
                    code << "        comp_sym_" << i << ".set_field('"
                         << EscapePythonString( propName ) << "', '"
                         << EscapePythonString( propValue ) << "')\n";
                    code << "        sch.crud.update_items(comp_sym_" << i << ")\n";
                }
            }
            code << "\n";
        }


        // Get companion pin position for wire stub
        // When reverse=false: pin 2 connects to IC (default)
        // When reverse=true: pin 1 connects to IC
        std::string wirePin = reverse ? "1" : "2";
        code << "        # Get companion pin position for wire stub (pin " << wirePin << " toward IC)\n";
        code << "        wire_pin_" << i << " = '" << wirePin << "'  # Pin facing IC\n";
        code << "        comp_wire_pin_" << i << " = sch.symbols.get_transformed_pin_position(comp_sym_" << i << ", wire_pin_" << i << ")\n";
        code << "        if comp_wire_pin_" << i << ":\n";
        code << "            cpwx_" << i << " = comp_wire_pin_" << i << "['position'].x / 1_000_000\n";
        code << "            cpwy_" << i << " = comp_wire_pin_" << i << "['position'].y / 1_000_000\n";
        code << "        else:\n";
        code << "            # Fallback: assume wire pin is at component position\n";
        code << "            cpwx_" << i << " = cx_" << i << "\n";
        code << "            cpwy_" << i << " = cy_" << i << "\n";
        code << "\n";

        // Draw L-shaped wire stub from IC pin to companion wire pin (orthogonal routing)
        // Routing strategy to avoid wire crossings:
        // - Horizontal escape: go vertical FIRST (to companion's Y), then horizontal
        // - Vertical escape: go horizontal FIRST (to companion's X), then vertical
        // This ensures horizontal segments are at companion Y levels (not IC pin Y levels),
        // so companions at different Y levels have non-crossing wires.
        code << "        # Draw L-shaped wire stub from IC pin to companion pin " << wirePin << "\n";
        code << "        _w" << i << "_x0 = snap_to_grid(px_" << i << ")\n";
        code << "        _w" << i << "_y0 = snap_to_grid(py_" << i << ")\n";
        code << "        _w" << i << "_x1 = snap_to_grid(cpwx_" << i << ")\n";
        code << "        _w" << i << "_y1 = snap_to_grid(cpwy_" << i << ")\n";
        code << "        _w" << i << "_len = (((_w" << i << "_x1 - _w" << i << "_x0)**2 + (_w" << i << "_y1 - _w" << i << "_y0)**2)**0.5)\n";
        code << "        _debug.append(f'Comp" << i << " wire stub: ({_w" << i << "_x0:.2f},{_w" << i << "_y0:.2f}) to ({_w" << i << "_x1:.2f},{_w" << i << "_y1:.2f}) len={_w" << i << "_len:.2f}')\n";
        code << "        if _w" << i << "_len >= 0.1:  # Only draw if non-zero length\n";
        code << "            # Staggered L-shaped routing to avoid wire crossings\n";
        code << "            # Each companion gets its corner at a different position based on index\n";
        code << "            _wire_idx_" << i << " = len(placed_companions)  # Current companion index\n";
        code << "            _corner_offset_" << i << " = GRID_MM * (1 + _wire_idx_" << i << ")  # Stagger corners\n";
        code << "            \n";
        code << "            if escape_dir_" << i << " in ('left', 'right'):\n";
        code << "                # Horizontal escape: always use L-shape with staggered vertical position\n";
        code << "                # Corner X is offset from IC pin to create separate vertical \"lanes\"\n";
        code << "                if escape_dir_" << i << " == 'right':\n";
        code << "                    _corner_x_" << i << " = snap_to_grid(_w" << i << "_x0 + _corner_offset_" << i << ")\n";
        code << "                else:  # left\n";
        code << "                    _corner_x_" << i << " = snap_to_grid(_w" << i << "_x0 - _corner_offset_" << i << ")\n";
        code << "                # Ensure corner doesn't go past the companion\n";
        code << "                if escape_dir_" << i << " == 'right' and _corner_x_" << i << " > _w" << i << "_x1:\n";
        code << "                    _corner_x_" << i << " = _w" << i << "_x1\n";
        code << "                elif escape_dir_" << i << " == 'left' and _corner_x_" << i << " < _w" << i << "_x1:\n";
        code << "                    _corner_x_" << i << " = _w" << i << "_x1\n";
        code << "                \n";
        code << "                # Draw: horizontal to corner X, vertical to companion Y, horizontal to companion\n";
        code << "                if abs(_w" << i << "_y0 - _w" << i << "_y1) < 0.01 and abs(_corner_x_" << i << " - _w" << i << "_x1) < 0.01:\n";
        code << "                    # Truly straight - single wire\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x0, _w" << i << "_y0), Vector2.from_xy_mm(_w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                    placed_wires.append((_w" << i << "_x0, _w" << i << "_y0, _w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                else:\n";
        code << "                    # L-shape: horizontal to corner, vertical to Y, horizontal to pin\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x0, _w" << i << "_y0), Vector2.from_xy_mm(_corner_x_" << i << ", _w" << i << "_y0))\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_corner_x_" << i << ", _w" << i << "_y0), Vector2.from_xy_mm(_corner_x_" << i << ", _w" << i << "_y1))\n";
        code << "                    if abs(_corner_x_" << i << " - _w" << i << "_x1) > 0.01:\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_corner_x_" << i << ", _w" << i << "_y1), Vector2.from_xy_mm(_w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                    placed_wires.append((_w" << i << "_x0, _w" << i << "_y0, _corner_x_" << i << ", _w" << i << "_y0))\n";
        code << "                    placed_wires.append((_corner_x_" << i << ", _w" << i << "_y0, _corner_x_" << i << ", _w" << i << "_y1))\n";
        code << "                    if abs(_corner_x_" << i << " - _w" << i << "_x1) > 0.01:\n";
        code << "                        placed_wires.append((_corner_x_" << i << ", _w" << i << "_y1, _w" << i << "_x1, _w" << i << "_y1))\n";
        code << "            else:\n";
        code << "                # Vertical escape: always use L-shape with staggered horizontal position\n";
        code << "                if escape_dir_" << i << " == 'down':\n";
        code << "                    _corner_y_" << i << " = snap_to_grid(_w" << i << "_y0 + _corner_offset_" << i << ")\n";
        code << "                else:  # up\n";
        code << "                    _corner_y_" << i << " = snap_to_grid(_w" << i << "_y0 - _corner_offset_" << i << ")\n";
        code << "                # Ensure corner doesn't go past the companion\n";
        code << "                if escape_dir_" << i << " == 'down' and _corner_y_" << i << " > _w" << i << "_y1:\n";
        code << "                    _corner_y_" << i << " = _w" << i << "_y1\n";
        code << "                elif escape_dir_" << i << " == 'up' and _corner_y_" << i << " < _w" << i << "_y1:\n";
        code << "                    _corner_y_" << i << " = _w" << i << "_y1\n";
        code << "                \n";
        code << "                # Draw: vertical to corner Y, horizontal to companion X, vertical to companion\n";
        code << "                if abs(_w" << i << "_x0 - _w" << i << "_x1) < 0.01 and abs(_corner_y_" << i << " - _w" << i << "_y1) < 0.01:\n";
        code << "                    # Truly straight - single wire\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x0, _w" << i << "_y0), Vector2.from_xy_mm(_w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                    placed_wires.append((_w" << i << "_x0, _w" << i << "_y0, _w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                else:\n";
        code << "                    # L-shape: vertical to corner, horizontal to X, vertical to pin\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x0, _w" << i << "_y0), Vector2.from_xy_mm(_w" << i << "_x0, _corner_y_" << i << "))\n";
        code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x0, _corner_y_" << i << "), Vector2.from_xy_mm(_w" << i << "_x1, _corner_y_" << i << "))\n";
        code << "                    if abs(_corner_y_" << i << " - _w" << i << "_y1) > 0.01:\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_w" << i << "_x1, _corner_y_" << i << "), Vector2.from_xy_mm(_w" << i << "_x1, _w" << i << "_y1))\n";
        code << "                    placed_wires.append((_w" << i << "_x0, _w" << i << "_y0, _w" << i << "_x0, _corner_y_" << i << "))\n";
        code << "                    placed_wires.append((_w" << i << "_x0, _corner_y_" << i << ", _w" << i << "_x1, _corner_y_" << i << "))\n";
        code << "                    if abs(_corner_y_" << i << " - _w" << i << "_y1) > 0.01:\n";
        code << "                        placed_wires.append((_w" << i << "_x1, _corner_y_" << i << ", _w" << i << "_x1, _w" << i << "_y1))\n";
        code << "\n";

        // Handle terminal_power - place power symbols
        if( companion.contains( "terminal_power" ) && companion["terminal_power"].is_object() )
        {
            code << "        # Place power symbols at terminals\n";

            for( auto it = companion["terminal_power"].begin(); it != companion["terminal_power"].end(); ++it )
            {
                std::string pinNum = it.key();
                std::string powerNameVal = it.value().get<std::string>();

                // When reverse is true, swap pin 1 and 2 for terminal placement
                // (pin 1 becomes the IC-facing pin, pin 2 becomes the away pin)
                if( reverse )
                {
                    if( pinNum == "1" )
                        pinNum = "2";
                    else if( pinNum == "2" )
                        pinNum = "1";
                }

                // Extract just the power name (remove library prefix if present)
                size_t colonPos = powerNameVal.find( ':' );
                if( colonPos != std::string::npos )
                    powerNameVal = powerNameVal.substr( colonPos + 1 );

                code << "        # Power symbol at pin " << pinNum << ": " << powerNameVal << "\n";
                code << "        try:\n";
                code << "            pwr_pin_" << i << "_" << pinNum << " = sch.symbols.get_transformed_pin_position(comp_sym_" << i << ", '" << EscapePythonString( pinNum ) << "')\n";
                code << "            if pwr_pin_" << i << "_" << pinNum << ":\n";
                code << "                pwr_px_" << i << "_" << pinNum << " = pwr_pin_" << i << "_" << pinNum << "['position'].x / 1_000_000\n";
                code << "                pwr_py_" << i << "_" << pinNum << " = pwr_pin_" << i << "_" << pinNum << "['position'].y / 1_000_000\n";
                code << "                pwr_orient_" << i << "_" << pinNum << " = transform_orientation(pwr_pin_" << i << "_" << pinNum << ".get('orientation', 1))\n";
                code << "\n";

                // Calculate power symbol position and rotation
                // GND normally faces down (0°), VCC normally faces up (180°)
                // For horizontal escape, rotate to point in escape direction
                code << "                # Calculate power symbol position and angle\n";
                code << "                # GND faces down, VCC faces up (standard). Rotate for horizontal escapes.\n";
                code << "                pwr_name_upper = '" << EscapePythonString( powerNameVal ) << "'.upper()\n";
                code << "                is_gnd = 'GND' in pwr_name_upper or 'VSS' in pwr_name_upper or 'GND' in pwr_name_upper\n";
                code << "                pwr_offset = GRID_MM\n";
                code << "                if escape_dir_" << i << " == 'left':\n";
                code << "                    pwr_x = snap_to_grid(pwr_px_" << i << "_" << pinNum << " - pwr_offset)\n";
                code << "                    pwr_y = snap_to_grid(pwr_py_" << i << "_" << pinNum << ")\n";
                code << "                    pwr_angle = 270 if is_gnd else 90  # GND left, VCC left (wire exits right)\n";
                code << "                elif escape_dir_" << i << " == 'right':\n";
                code << "                    pwr_x = snap_to_grid(pwr_px_" << i << "_" << pinNum << " + pwr_offset)\n";
                code << "                    pwr_y = snap_to_grid(pwr_py_" << i << "_" << pinNum << ")\n";
                code << "                    pwr_angle = 90 if is_gnd else 270  # GND right, VCC right (wire exits left)\n";
                code << "                elif escape_dir_" << i << " == 'down':\n";
                code << "                    pwr_x = snap_to_grid(pwr_px_" << i << "_" << pinNum << ")\n";
                code << "                    pwr_y = snap_to_grid(pwr_py_" << i << "_" << pinNum << " + pwr_offset)\n";
                code << "                    pwr_angle = 0 if is_gnd else 180  # GND down, VCC up (natural orientation)\n";
                code << "                elif escape_dir_" << i << " == 'up':\n";
                code << "                    pwr_x = snap_to_grid(pwr_px_" << i << "_" << pinNum << ")\n";
                code << "                    pwr_y = snap_to_grid(pwr_py_" << i << "_" << pinNum << " - pwr_offset)\n";
                code << "                    pwr_angle = 180 if is_gnd else 0  # GND up (inverted), VCC up (natural)\n";
                code << "                else:\n";
                code << "                    pwr_x = snap_to_grid(pwr_px_" << i << "_" << pinNum << " + pwr_offset)\n";
                code << "                    pwr_y = snap_to_grid(pwr_py_" << i << "_" << pinNum << ")\n";
                code << "                    pwr_angle = 90 if is_gnd else 270\n";
                code << "\n";
                code << "                pwr_pos = Vector2.from_xy_mm(pwr_x, pwr_y)\n";
                code << "                # Use sch.labels.add_power() which searches multiple libraries\n";
                code << "                pwr_sym = sch.labels.add_power('" << EscapePythonString( powerNameVal ) << "', pwr_pos, angle=pwr_angle)\n";
                code << "\n";
                code << "                # Wire from companion pin to power symbol\n";
                code << "                _pw_x0 = snap_to_grid(pwr_px_" << i << "_" << pinNum << ")\n";
                code << "                _pw_y0 = snap_to_grid(pwr_py_" << i << "_" << pinNum << ")\n";
                code << "                pwr_wire_start = Vector2.from_xy_mm(_pw_x0, _pw_y0)\n";
                code << "                pwr_wire_end = Vector2.from_xy_mm(pwr_x, pwr_y)\n";
                code << "                sch.wiring.add_wire(pwr_wire_start, pwr_wire_end)\n";
                code << "        except Exception as pwr_e:\n";
                code << "            results.append({'index': " << i << ", 'warning': f'Power symbol at pin " << pinNum << ": {str(pwr_e)}'})\n";
                code << "\n";
            }
        }

        // Handle terminal_labels - place text labels
        if( companion.contains( "terminal_labels" ) && companion["terminal_labels"].is_object() )
        {
            code << "        # Place text labels at terminals\n";

            for( auto it = companion["terminal_labels"].begin(); it != companion["terminal_labels"].end(); ++it )
            {
                std::string pinNum = it.key();
                std::string labelText = it.value().get<std::string>();

                // When reverse is true, swap pin 1 and 2 for terminal placement
                if( reverse )
                {
                    if( pinNum == "1" )
                        pinNum = "2";
                    else if( pinNum == "2" )
                        pinNum = "1";
                }

                code << "        # Label at pin " << pinNum << ": " << labelText << "\n";
                code << "        try:\n";
                code << "            lbl_pin_" << i << "_" << pinNum << " = sch.symbols.get_transformed_pin_position(comp_sym_" << i << ", '" << EscapePythonString( pinNum ) << "')\n";
                code << "            if lbl_pin_" << i << "_" << pinNum << ":\n";
                code << "                lbl_px_" << i << "_" << pinNum << " = lbl_pin_" << i << "_" << pinNum << "['position'].x / 1_000_000\n";
                code << "                lbl_py_" << i << "_" << pinNum << " = lbl_pin_" << i << "_" << pinNum << "['position'].y / 1_000_000\n";
                code << "                lbl_pos = Vector2.from_xy_mm(snap_to_grid(lbl_px_" << i << "_" << pinNum << "), snap_to_grid(lbl_py_" << i << "_" << pinNum << "))\n";
                code << "                sch.labels.add_local('" << EscapePythonString( labelText ) << "', lbl_pos)\n";
                code << "        except Exception as lbl_e:\n";
                code << "            results.append({'index': " << i << ", 'warning': f'Label at pin " << pinNum << ": {str(lbl_e)}'})\n";
                code << "\n";
            }
        }

        // Note: placed_companions list is used for collision detection
        // (no need for separate body_bboxes since we calculate bbox from cx/cy/escape_dir)
        code << "\n";

        // Record success (include cx/cy for collision detection)
        code << "        placed_companions.append({\n";
        code << "            'index': " << i << ",\n";
        code << "            'lib_id': lib_id_" << i << ",\n";
        code << "            'ic_pin': ic_pin_" << i << ",\n";
        code << "            'ref': comp_sym_" << i << ".reference,\n";
        code << "            'cx': cx_" << i << ",\n";
        code << "            'cy': cy_" << i << ",\n";
        code << "            'position': [round(cx_" << i << ", 2), round(cy_" << i << ", 2)],\n";
        code << "            'escape_dir': escape_dir_" << i << "\n";
        code << "        })\n";
        code << "\n";

        // Process chain items if present
        if( companion.contains( "chain" ) && companion["chain"].is_array() && !companion["chain"].empty() )
        {
            int globalChainIndex = 0;
            std::string parentSymVar = "comp_sym_" + std::to_string( i );
            std::string escapeDirVar = "escape_dir_" + std::to_string( i );
            std::string prefix = "ch_" + std::to_string( i ) + "_";

            GenerateChainCode( code, companion["chain"], parentSymVar, escapeDirVar, prefix, globalChainIndex, reverse );
        }

        code << "    except Exception as e_" << i << ":\n";
        code << "        results.append({'index': " << i << ", 'lib_id': '" << EscapePythonString( libId )
             << "', 'ic_pin': '" << EscapePythonString( icPin ) << "', 'error': str(e_" << i << ")})\n";
        code << "\n";
    }

    code << "except Exception as e:\n";
    code << "    results = [{'error': str(e)}]\n";
    code << "\n";

    // Output result
    code << "output = {\n";
    code << "    'status': 'success' if placed_companions else 'error',\n";
    code << "    'ic_ref': ic_ref,\n";
    code << "    'companions_placed': len(placed_companions),\n";
    code << "    'companions_failed': len([r for r in results if 'error' in r]),\n";
    code << "    'placed': placed_companions,\n";
    code << "    'errors': [r for r in results if 'error' in r],\n";
    code << "    'warnings': [r for r in results if 'warning' in r],\n";
    code << "    '_debug': _debug\n";
    code << "}\n";
    code << "print(json.dumps(output, indent=2))\n";

    return code.str();
}


void SCH_COMPANION_HANDLER::GenerateChainCode( std::ostringstream& code,
                                                const nlohmann::json& aChainItems,
                                                const std::string& aParentSymVar,
                                                const std::string& aEscapeDirVar,
                                                const std::string& aPrefix,
                                                int& aGlobalIndex,
                                                bool aParentReversed ) const
{
    if( !aChainItems.is_array() || aChainItems.empty() )
        return;

    size_t numItems = aChainItems.size();
    bool hasBranching = numItems > 1;

    code << "\n";
    code << "        # Chain items from " << aParentSymVar << " (" << numItems << " items)\n";

    for( size_t j = 0; j < numItems; ++j )
    {
        auto chainItem = aChainItems[j];
        std::string idx = aPrefix + std::to_string( aGlobalIndex );
        aGlobalIndex++;

        std::string libId = chainItem.value( "lib_id", "" );
        int offsetGrids = chainItem.value( "offset_grids", 3 );
        bool reverse = chainItem.value( "reverse", false );

        if( libId.empty() )
        {
            code << "        results.append({'chain_index': '" << idx << "', 'error': 'lib_id is required for chain item'})\n";
            continue;
        }

        double offsetMm = SnapToGrid( offsetGrids * 1.27 );

        // Check if this is a power-only chain item
        bool isPowerOnly = libId.find( "power:" ) == 0;
        std::string powerName;
        if( isPowerOnly )
            powerName = libId.substr( 6 );

        code << "        # Chain item " << idx << ": " << libId;
        if( isPowerOnly )
            code << " (power-only)";
        if( reverse )
            code << " (reversed)";
        if( hasBranching && j > 0 )
            code << " (branch " << j << ")";
        code << "\n";

        code << "        try:\n";

        // Get parent's "away" pin position (where chain connects)
        // When parent is NOT reversed: pin 2 faces IC, pin 1 is away → use pin 1
        // When parent IS reversed: pin 1 faces IC, pin 2 is away → use pin 2
        std::string parentAwayPin = aParentReversed ? "2" : "1";
        code << "            # Get parent's 'away' terminal pin position (chain extends from here)\n";
        code << "            # Parent reversed=" << ( aParentReversed ? "True" : "False" ) << " so away pin is " << parentAwayPin << "\n";
        code << "            parent_term_pin_" << idx << " = sch.symbols.get_transformed_pin_position(" << aParentSymVar << ", '" << parentAwayPin << "')\n";
        code << "            if not parent_term_pin_" << idx << ":\n";
        code << "                raise ValueError(f'Could not get parent terminal pin position')\n";
        code << "            ptx_" << idx << " = parent_term_pin_" << idx << "['position'].x / 1_000_000\n";
        code << "            pty_" << idx << " = parent_term_pin_" << idx << "['position'].y / 1_000_000\n";
        code << "\n";

        // Calculate perpendicular offset for branching
        if( hasBranching && j > 0 )
        {
            // Stagger perpendicular to escape direction
            // Use alternating +/- offsets: +1, -1, +2, -2, etc.
            int perpIndex = static_cast<int>( j );
            int perpSign = ( perpIndex % 2 == 1 ) ? 1 : -1;
            int perpMult = ( perpIndex + 1 ) / 2;
            double perpOffset = perpSign * perpMult * 5.08;  // 4 grid units per step

            code << "            # Branch offset (perpendicular stagger)\n";
            code << "            perp_offset_" << idx << " = " << perpOffset << "\n";
            code << "            if " << aEscapeDirVar << " in ('left', 'right'):\n";
            code << "                pty_" << idx << " = snap_to_grid(pty_" << idx << " + perp_offset_" << idx << ")\n";
            code << "            else:\n";
            code << "                ptx_" << idx << " = snap_to_grid(ptx_" << idx << " + perp_offset_" << idx << ")\n";
            code << "\n";
        }

        // Calculate chain item position based on escape direction
        code << "            # Calculate chain item position (continue along escape direction)\n";
        code << "            offset_mm_" << idx << " = " << offsetMm << "\n";
        code << "            lib_id_" << idx << " = '" << EscapePythonString( libId ) << "'\n";
        code << "            if " << aEscapeDirVar << " == 'left':\n";
        code << "                cx_" << idx << " = snap_to_grid(ptx_" << idx << " - offset_mm_" << idx << ")\n";
        code << "                cy_" << idx << " = snap_to_grid(pty_" << idx << ")\n";
        code << "            elif " << aEscapeDirVar << " == 'right':\n";
        code << "                cx_" << idx << " = snap_to_grid(ptx_" << idx << " + offset_mm_" << idx << ")\n";
        code << "                cy_" << idx << " = snap_to_grid(pty_" << idx << ")\n";
        code << "            elif " << aEscapeDirVar << " == 'down':\n";
        code << "                cx_" << idx << " = snap_to_grid(ptx_" << idx << ")\n";
        code << "                cy_" << idx << " = snap_to_grid(pty_" << idx << " + offset_mm_" << idx << ")\n";
        code << "            else:  # up\n";
        code << "                cx_" << idx << " = snap_to_grid(ptx_" << idx << ")\n";
        code << "                cy_" << idx << " = snap_to_grid(pty_" << idx << " - offset_mm_" << idx << ")\n";
        code << "\n";
        code << "            # Get rotation angle based on component type (handles LED/diode vs R/C)\n";
        code << "            comp_angle_" << idx << " = _get_component_angle(" << aEscapeDirVar << ", lib_id_" << idx << ")\n";
        code << "\n";

        // Handle reverse
        if( reverse )
        {
            code << "            # Apply reverse: rotate 180° more\n";
            code << "            comp_angle_" << idx << " = (comp_angle_" << idx << " + 180) % 360\n";
            code << "\n";
        }

        // Power-only chain item
        if( isPowerOnly )
        {
            code << "            # Power-only chain item: place power symbol\n";
            code << "            pwr_name_upper_" << idx << " = '" << EscapePythonString( powerName ) << "'.upper()\n";
            code << "            is_gnd_" << idx << " = 'GND' in pwr_name_upper_" << idx << " or 'VSS' in pwr_name_upper_" << idx << "\n";
            code << "            pwr_offset_" << idx << " = GRID_MM * 2\n";
            code << "            if " << aEscapeDirVar << " == 'left':\n";
            code << "                pwr_angle_" << idx << " = 270 if is_gnd_" << idx << " else 90\n";
            code << "            elif " << aEscapeDirVar << " == 'right':\n";
            code << "                pwr_angle_" << idx << " = 90 if is_gnd_" << idx << " else 270\n";
            code << "            elif " << aEscapeDirVar << " == 'down':\n";
            code << "                pwr_angle_" << idx << " = 0 if is_gnd_" << idx << " else 180\n";
            code << "            else:\n";
            code << "                pwr_angle_" << idx << " = 180 if is_gnd_" << idx << " else 0\n";
            code << "\n";
            code << "            pwr_pos_" << idx << " = Vector2.from_xy_mm(cx_" << idx << ", cy_" << idx << ")\n";
            code << "            pwr_sym_" << idx << " = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pwr_pos_" << idx << ", angle=pwr_angle_" << idx << ")\n";
            code << "\n";
            code << "            # Wire from parent terminal to power symbol\n";
            code << "            sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(ptx_" << idx << "), snap_to_grid(pty_" << idx << ")), pwr_pos_" << idx << ")\n";
            code << "\n";
            code << "            placed_companions.append({\n";
            code << "                'chain_index': '" << idx << "',\n";
            code << "                'lib_id': lib_id_" << idx << ",\n";
            code << "                'ref': pwr_sym_" << idx << ".reference if hasattr(pwr_sym_" << idx << ", 'reference') else '" << EscapePythonString( powerName ) << "',\n";
            code << "                'cx': cx_" << idx << ",\n";
            code << "                'cy': cy_" << idx << ",\n";
            code << "                'position': [round(cx_" << idx << ", 2), round(cy_" << idx << ", 2)],\n";
            code << "                'escape_dir': " << aEscapeDirVar << ",\n";
            code << "                'power_only': True,\n";
            code << "                'chain': True\n";
            code << "            })\n";
            code << "        except Exception as e_" << idx << ":\n";
            code << "            results.append({'chain_index': '" << idx << "', 'lib_id': '" << EscapePythonString( libId ) << "', 'error': str(e_" << idx << ")})\n";
            code << "\n";
            continue;
        }

        // Regular component chain item
        code << "            # Create chain component\n";
        code << "            comp_pos_" << idx << " = Vector2.from_xy_mm(cx_" << idx << ", cy_" << idx << ")\n";
        code << "            comp_sym_" << idx << " = sch.symbols.add(\n";
        code << "                lib_id='" << EscapePythonString( libId ) << "',\n";
        code << "                position=comp_pos_" << idx << ",\n";
        code << "                angle=comp_angle_" << idx << "\n";
        code << "            )\n";
        code << "            if not comp_sym_" << idx << ":\n";
        code << "                raise ValueError(f'Failed to create chain component: " << EscapePythonString( libId ) << "')\n";
        code << "\n";

        // Set properties
        if( chainItem.contains( "properties" ) && chainItem["properties"].is_object() )
        {
            for( auto it = chainItem["properties"].begin(); it != chainItem["properties"].end(); ++it )
            {
                std::string propName = it.key();
                std::string propValue = it.value().get<std::string>();

                if( propName == "Value" )
                {
                    code << "            sch.symbols.set_value(comp_sym_" << idx << ", '"
                         << EscapePythonString( propValue ) << "')\n";
                }
                else if( propName == "Footprint" )
                {
                    code << "            sch.symbols.set_footprint(comp_sym_" << idx << ", '"
                         << EscapePythonString( propValue ) << "')\n";
                }
            }
            code << "\n";
        }

        // Wire from parent terminal to chain component with staggered L-routing
        std::string wirePin = reverse ? "1" : "2";
        code << "            # Wire from parent terminal to chain component pin " << wirePin << " (staggered L-routing)\n";
        code << "            chain_wire_pin_" << idx << " = sch.symbols.get_transformed_pin_position(comp_sym_" << idx << ", '" << wirePin << "')\n";
        code << "            if chain_wire_pin_" << idx << ":\n";
        code << "                cwpx_" << idx << " = chain_wire_pin_" << idx << "['position'].x / 1_000_000\n";
        code << "                cwpy_" << idx << " = chain_wire_pin_" << idx << "['position'].y / 1_000_000\n";
        code << "                # Staggered L-shaped wire routing to avoid crossings\n";
        code << "                _cw_x0_" << idx << " = snap_to_grid(ptx_" << idx << ")\n";
        code << "                _cw_y0_" << idx << " = snap_to_grid(pty_" << idx << ")\n";
        code << "                _cw_x1_" << idx << " = snap_to_grid(cwpx_" << idx << ")\n";
        code << "                _cw_y1_" << idx << " = snap_to_grid(cwpy_" << idx << ")\n";
        code << "                _cw_wire_idx_" << idx << " = len(placed_companions)  # Unique index for staggering\n";
        code << "                _cw_corner_offset_" << idx << " = GRID_MM * (1 + _cw_wire_idx_" << idx << ")\n";
        code << "                \n";
        code << "                if " << aEscapeDirVar << " in ('left', 'right'):\n";
        code << "                    # Horizontal escape: staggered vertical corner\n";
        code << "                    if " << aEscapeDirVar << " == 'right':\n";
        code << "                        _cw_corner_x_" << idx << " = snap_to_grid(_cw_x0_" << idx << " + _cw_corner_offset_" << idx << ")\n";
        code << "                    else:\n";
        code << "                        _cw_corner_x_" << idx << " = snap_to_grid(_cw_x0_" << idx << " - _cw_corner_offset_" << idx << ")\n";
        code << "                    # Clamp corner to not go past destination\n";
        code << "                    if " << aEscapeDirVar << " == 'right' and _cw_corner_x_" << idx << " > _cw_x1_" << idx << ":\n";
        code << "                        _cw_corner_x_" << idx << " = _cw_x1_" << idx << "\n";
        code << "                    elif " << aEscapeDirVar << " == 'left' and _cw_corner_x_" << idx << " < _cw_x1_" << idx << ":\n";
        code << "                        _cw_corner_x_" << idx << " = _cw_x1_" << idx << "\n";
        code << "                    \n";
        code << "                    if abs(_cw_y0_" << idx << " - _cw_y1_" << idx << ") < 0.01 and abs(_cw_corner_x_" << idx << " - _cw_x1_" << idx << ") < 0.01:\n";
        code << "                        # Straight wire\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_y0_" << idx << "), Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_x0_" << idx << ", _cw_y0_" << idx << ", _cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                    else:\n";
        code << "                        # L-shape with staggered corner\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_y0_" << idx << "), Vector2.from_xy_mm(_cw_corner_x_" << idx << ", _cw_y0_" << idx << "))\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_corner_x_" << idx << ", _cw_y0_" << idx << "), Vector2.from_xy_mm(_cw_corner_x_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        if abs(_cw_corner_x_" << idx << " - _cw_x1_" << idx << ") > 0.01:\n";
        code << "                            sch.wiring.add_wire(Vector2.from_xy_mm(_cw_corner_x_" << idx << ", _cw_y1_" << idx << "), Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_x0_" << idx << ", _cw_y0_" << idx << ", _cw_corner_x_" << idx << ", _cw_y0_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_corner_x_" << idx << ", _cw_y0_" << idx << ", _cw_corner_x_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        if abs(_cw_corner_x_" << idx << " - _cw_x1_" << idx << ") > 0.01:\n";
        code << "                            placed_wires.append((_cw_corner_x_" << idx << ", _cw_y1_" << idx << ", _cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                else:\n";
        code << "                    # Vertical escape: staggered horizontal corner\n";
        code << "                    if " << aEscapeDirVar << " == 'down':\n";
        code << "                        _cw_corner_y_" << idx << " = snap_to_grid(_cw_y0_" << idx << " + _cw_corner_offset_" << idx << ")\n";
        code << "                    else:\n";
        code << "                        _cw_corner_y_" << idx << " = snap_to_grid(_cw_y0_" << idx << " - _cw_corner_offset_" << idx << ")\n";
        code << "                    # Clamp corner to not go past destination\n";
        code << "                    if " << aEscapeDirVar << " == 'down' and _cw_corner_y_" << idx << " > _cw_y1_" << idx << ":\n";
        code << "                        _cw_corner_y_" << idx << " = _cw_y1_" << idx << "\n";
        code << "                    elif " << aEscapeDirVar << " == 'up' and _cw_corner_y_" << idx << " < _cw_y1_" << idx << ":\n";
        code << "                        _cw_corner_y_" << idx << " = _cw_y1_" << idx << "\n";
        code << "                    \n";
        code << "                    if abs(_cw_x0_" << idx << " - _cw_x1_" << idx << ") < 0.01 and abs(_cw_corner_y_" << idx << " - _cw_y1_" << idx << ") < 0.01:\n";
        code << "                        # Straight wire\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_y0_" << idx << "), Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_x0_" << idx << ", _cw_y0_" << idx << ", _cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                    else:\n";
        code << "                        # L-shape with staggered corner\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_y0_" << idx << "), Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_corner_y_" << idx << "))\n";
        code << "                        sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x0_" << idx << ", _cw_corner_y_" << idx << "), Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_corner_y_" << idx << "))\n";
        code << "                        if abs(_cw_corner_y_" << idx << " - _cw_y1_" << idx << ") > 0.01:\n";
        code << "                            sch.wiring.add_wire(Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_corner_y_" << idx << "), Vector2.from_xy_mm(_cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_x0_" << idx << ", _cw_y0_" << idx << ", _cw_x0_" << idx << ", _cw_corner_y_" << idx << "))\n";
        code << "                        placed_wires.append((_cw_x0_" << idx << ", _cw_corner_y_" << idx << ", _cw_x1_" << idx << ", _cw_corner_y_" << idx << "))\n";
        code << "                        if abs(_cw_corner_y_" << idx << " - _cw_y1_" << idx << ") > 0.01:\n";
        code << "                            placed_wires.append((_cw_x1_" << idx << ", _cw_corner_y_" << idx << ", _cw_x1_" << idx << ", _cw_y1_" << idx << "))\n";
        code << "\n";

        // Handle terminal_power
        if( chainItem.contains( "terminal_power" ) && chainItem["terminal_power"].is_object() )
        {
            code << "            # Power symbols at chain terminals\n";

            for( auto it = chainItem["terminal_power"].begin(); it != chainItem["terminal_power"].end(); ++it )
            {
                std::string pinNum = it.key();
                std::string pwrName = it.value().get<std::string>();

                // Swap pins if reversed
                if( reverse )
                {
                    if( pinNum == "1" )
                        pinNum = "2";
                    else if( pinNum == "2" )
                        pinNum = "1";
                }

                // Extract power name
                size_t colonPos = pwrName.find( ':' );
                if( colonPos != std::string::npos )
                    pwrName = pwrName.substr( colonPos + 1 );

                code << "            try:\n";
                code << "                tp_pin_" << idx << "_" << pinNum << " = sch.symbols.get_transformed_pin_position(comp_sym_" << idx << ", '" << pinNum << "')\n";
                code << "                if tp_pin_" << idx << "_" << pinNum << ":\n";
                code << "                    tp_px_" << idx << " = tp_pin_" << idx << "_" << pinNum << "['position'].x / 1_000_000\n";
                code << "                    tp_py_" << idx << " = tp_pin_" << idx << "_" << pinNum << "['position'].y / 1_000_000\n";
                code << "                    tp_pwr_upper_" << idx << " = '" << EscapePythonString( pwrName ) << "'.upper()\n";
                code << "                    tp_is_gnd_" << idx << " = 'GND' in tp_pwr_upper_" << idx << " or 'VSS' in tp_pwr_upper_" << idx << "\n";
                code << "                    tp_offset = GRID_MM\n";
                code << "                    if " << aEscapeDirVar << " == 'left':\n";
                code << "                        tp_x_" << idx << " = snap_to_grid(tp_px_" << idx << " - tp_offset)\n";
                code << "                        tp_y_" << idx << " = snap_to_grid(tp_py_" << idx << ")\n";
                code << "                        tp_angle_" << idx << " = 270 if tp_is_gnd_" << idx << " else 90\n";
                code << "                    elif " << aEscapeDirVar << " == 'right':\n";
                code << "                        tp_x_" << idx << " = snap_to_grid(tp_px_" << idx << " + tp_offset)\n";
                code << "                        tp_y_" << idx << " = snap_to_grid(tp_py_" << idx << ")\n";
                code << "                        tp_angle_" << idx << " = 90 if tp_is_gnd_" << idx << " else 270\n";
                code << "                    elif " << aEscapeDirVar << " == 'down':\n";
                code << "                        tp_x_" << idx << " = snap_to_grid(tp_px_" << idx << ")\n";
                code << "                        tp_y_" << idx << " = snap_to_grid(tp_py_" << idx << " + tp_offset)\n";
                code << "                        tp_angle_" << idx << " = 0 if tp_is_gnd_" << idx << " else 180\n";
                code << "                    else:\n";
                code << "                        tp_x_" << idx << " = snap_to_grid(tp_px_" << idx << ")\n";
                code << "                        tp_y_" << idx << " = snap_to_grid(tp_py_" << idx << " - tp_offset)\n";
                code << "                        tp_angle_" << idx << " = 180 if tp_is_gnd_" << idx << " else 0\n";
                code << "                    tp_pos_" << idx << " = Vector2.from_xy_mm(tp_x_" << idx << ", tp_y_" << idx << ")\n";
                code << "                    sch.labels.add_power('" << EscapePythonString( pwrName ) << "', tp_pos_" << idx << ", angle=tp_angle_" << idx << ")\n";
                code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(snap_to_grid(tp_px_" << idx << "), snap_to_grid(tp_py_" << idx << ")), tp_pos_" << idx << ")\n";
                code << "            except Exception as tp_e_" << idx << ":\n";
                code << "                results.append({'chain_index': '" << idx << "', 'warning': f'Power at pin " << pinNum << ": {str(tp_e_" << idx << ")}'})\n";
                code << "\n";
            }
        }

        // Handle terminal_labels
        if( chainItem.contains( "terminal_labels" ) && chainItem["terminal_labels"].is_object() )
        {
            code << "            # Labels at chain terminals\n";

            for( auto it = chainItem["terminal_labels"].begin(); it != chainItem["terminal_labels"].end(); ++it )
            {
                std::string pinNum = it.key();
                std::string labelText = it.value().get<std::string>();

                // Swap pins if reversed
                if( reverse )
                {
                    if( pinNum == "1" )
                        pinNum = "2";
                    else if( pinNum == "2" )
                        pinNum = "1";
                }

                code << "            try:\n";
                code << "                tl_pin_" << idx << "_" << pinNum << " = sch.symbols.get_transformed_pin_position(comp_sym_" << idx << ", '" << pinNum << "')\n";
                code << "                if tl_pin_" << idx << "_" << pinNum << ":\n";
                code << "                    tl_px_" << idx << " = tl_pin_" << idx << "_" << pinNum << "['position'].x / 1_000_000\n";
                code << "                    tl_py_" << idx << " = tl_pin_" << idx << "_" << pinNum << "['position'].y / 1_000_000\n";
                code << "                    tl_pos_" << idx << " = Vector2.from_xy_mm(snap_to_grid(tl_px_" << idx << "), snap_to_grid(tl_py_" << idx << "))\n";
                code << "                    sch.labels.add_local('" << EscapePythonString( labelText ) << "', tl_pos_" << idx << ")\n";
                code << "            except Exception as tl_e_" << idx << ":\n";
                code << "                results.append({'chain_index': '" << idx << "', 'warning': f'Label at pin " << pinNum << ": {str(tl_e_" << idx << ")}'})\n";
                code << "\n";
            }
        }

        // Record success (include cx, cy, escape_dir, lib_id for collision detection and wire staggering)
        code << "            placed_companions.append({\n";
        code << "                'chain_index': '" << idx << "',\n";
        code << "                'lib_id': lib_id_" << idx << ",\n";
        code << "                'ref': comp_sym_" << idx << ".reference,\n";
        code << "                'cx': cx_" << idx << ",\n";
        code << "                'cy': cy_" << idx << ",\n";
        code << "                'position': [round(cx_" << idx << ", 2), round(cy_" << idx << ", 2)],\n";
        code << "                'escape_dir': " << aEscapeDirVar << ",\n";
        code << "                'chain': True\n";
        code << "            })\n";
        code << "\n";

        // Recursive chain processing
        if( chainItem.contains( "chain" ) && chainItem["chain"].is_array() && !chainItem["chain"].empty() )
        {
            std::string childSymVar = "comp_sym_" + idx;
            std::string childPrefix = idx + "_";

            GenerateChainCode( code, chainItem["chain"], childSymVar, aEscapeDirVar, childPrefix, aGlobalIndex, reverse );
        }

        code << "        except Exception as e_" << idx << ":\n";
        code << "            results.append({'chain_index': '" << idx << "', 'lib_id': '" << EscapePythonString( libId ) << "', 'error': str(e_" << idx << ")})\n";
        code << "\n";
    }
}
