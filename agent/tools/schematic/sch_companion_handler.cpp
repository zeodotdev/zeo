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
    code << "_debug = []\n";
    code << "\n";

    // Constants
    code << "GRID_MM = 1.27  # 50 mil grid\n";
    code << "BBOX_MARGIN = 1.5  # Margin around components for spacing\n";
    code << "MAX_OFFSET_GRIDS = 10  # Start far out (12.7mm)\n";
    code << "MIN_OFFSET_GRIDS = 5  # Minimum offset (6.35mm) - ensures wire stub\n";
    code << "COMP_HALF_LEN = 3.81  # Half-length of 2-pin passive (center to pin)\n";
    code << "TERMINAL_EXTENSION = 5.08  # Extra space for power symbols/labels at pin 1 (away from IC)\n";
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

    // Helper functions for collision detection
    code << "def _bboxes_overlap(a, b):\n";
    code << "    \"\"\"Check if two bboxes overlap.\"\"\"\n";
    code << "    return (a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and\n";
    code << "            a['min_y'] < b['max_y'] and a['max_y'] > b['min_y'])\n";
    code << "\n";
    code << "def _calc_companion_bbox(cx, cy, escape_dir):\n";
    code << "    \"\"\"Calculate companion bbox given center and escape direction.\n";
    code << "    Bbox is asymmetric: larger on pin1 side (away from IC) to include power symbols/labels.\n";
    code << "    \"\"\"\n";
    code << "    body_half = COMP_HALF_LEN + BBOX_MARGIN  # Body extent toward IC (pin 2 side)\n";
    code << "    term_half = COMP_HALF_LEN + TERMINAL_EXTENSION + BBOX_MARGIN  # Terminal extent away from IC (pin 1 side)\n";
    code << "    width_half = 1.5 + BBOX_MARGIN  # Width perpendicular to component axis\n";
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
    code << "def _find_clear_position(px, py, escape_dir, placed_companions, ic_bbox_expanded):\n";
    code << "    \"\"\"Find a non-overlapping position using push-out + perpendicular staggering.\"\"\"\n";
    code << "    _debug.append(f'_find_clear_position called: px={px:.2f}, py={py:.2f}, escape={escape_dir}, placed={len(placed_companions)}')\n";
    code << "    # Component dimensions for staggering\n";
    code << "    comp_height = (COMP_HALF_LEN + BBOX_MARGIN) * 2  # ~10.6mm\n";
    code << "    comp_width = (1.5 + BBOX_MARGIN) * 2  # ~6mm\n";
    code << "    perp_step = comp_height if escape_dir in ('left', 'right') else comp_width\n";
    code << "    \n";
    code << "    # Try perpendicular offsets: 0, +step, -step, +2*step, -2*step, ...\n";
    code << "    for perp_idx in range(5):  # Max 5 perpendicular steps\n";
    code << "        perp_offset = (perp_idx // 2 + 1) * perp_step * (1 if perp_idx % 2 == 0 else -1) if perp_idx > 0 else 0\n";
    code << "        \n";
    code << "        # Try push-out at this perpendicular offset\n";
    code << "        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):\n";
    code << "            try_offset = try_grids * GRID_MM\n";
    code << "            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir, perp_offset)\n";
    code << "            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir)\n";
    code << "            \n";
    code << "            # Check against IC bbox\n";
    code << "            if _bboxes_overlap(try_bbox, ic_bbox_expanded):\n";
    code << "                _debug.append(f'  offset={try_grids} perp={perp_offset:.1f}: overlaps IC')\n";
    code << "                continue\n";
    code << "            \n";
    code << "            # Check against placed companions\n";
    code << "            overlaps = False\n";
    code << "            overlap_ref = ''\n";
    code << "            for placed in placed_companions:\n";
    code << "                placed_bbox = _calc_companion_bbox(placed['cx'], placed['cy'], placed['escape_dir'])\n";
    code << "                if _bboxes_overlap(try_bbox, placed_bbox):\n";
    code << "                    overlaps = True\n";
    code << "                    overlap_ref = placed.get('ref', '?')\n";
    code << "                    break\n";
    code << "            \n";
    code << "            if overlaps:\n";
    code << "                _debug.append(f'  offset={try_grids} perp={perp_offset:.1f}: overlaps {overlap_ref}')\n";
    code << "                continue\n";
    code << "            \n";
    code << "            _debug.append(f'  FOUND: offset={try_grids} perp={perp_offset:.1f} -> ({try_cx:.2f}, {try_cy:.2f})')\n";
    code << "            return try_cx, try_cy, try_offset, perp_offset\n";
    code << "    \n";
    code << "    # Fallback: use max offset with no perp shift (will overlap but at least place it)\n";
    code << "    _debug.append(f'  FALLBACK: no clear position found')\n";
    code << "    cx, cy = _calc_center_from_offset(px, py, MAX_OFFSET_GRIDS * GRID_MM, escape_dir, 0)\n";
    code << "    return cx, cy, MAX_OFFSET_GRIDS * GRID_MM, 0\n";
    code << "\n";
    code << "# Debug: draw bbox rectangle (as 4 wire segments)\n";
    code << "def _draw_debug_bbox(bbox, label=''):\n";
    code << "    \"\"\"Draw a debug rectangle around a bounding box.\"\"\"\n";
    code << "    try:\n";
    code << "        x0, y0 = bbox['min_x'], bbox['min_y']\n";
    code << "        x1, y1 = bbox['max_x'], bbox['max_y']\n";
    code << "        # Draw 4 sides as wires\n";
    code << "        sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y0))  # bottom\n";
    code << "        sch.wiring.add_wire(Vector2.from_xy_mm(x1, y0), Vector2.from_xy_mm(x1, y1))  # right\n";
    code << "        sch.wiring.add_wire(Vector2.from_xy_mm(x1, y1), Vector2.from_xy_mm(x0, y1))  # top\n";
    code << "        sch.wiring.add_wire(Vector2.from_xy_mm(x0, y1), Vector2.from_xy_mm(x0, y0))  # left\n";
    code << "    except:\n";
    code << "        pass\n";
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
    code << "    # DEBUG: Draw IC expanded bbox\n";
    code << "    _draw_debug_bbox(ic_bbox_expanded, ic_ref)\n";
    code << "\n";

    // Process each companion in the array
    for( size_t i = 0; i < companions.size(); ++i )
    {
        auto companion = companions[i];

        std::string libId = companion.value( "lib_id", "" );
        std::string icPin = companion.value( "ic_pin", "" );
        int offsetGrids = companion.value( "offset_grids", 3 );

        if( libId.empty() || icPin.empty() )
        {
            code << "    results.append({'index': " << i << ", 'error': 'lib_id and ic_pin are required'})\n";
            continue;
        }

        double offsetMm = SnapToGrid( offsetGrids * 1.27 );

        code << "    # Companion " << i << ": " << libId << " at " << icPin << "\n";
        code << "    try:\n";
        code << "        lib_id_" << i << " = '" << EscapePythonString( libId ) << "'\n";
        code << "        ic_pin_" << i << " = '" << EscapePythonString( icPin ) << "'\n";
        code << "        offset_mm_" << i << " = " << offsetMm << "\n";
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
        code << "            # Companion rotated 90deg: pin2 at center+3.81 (toward IC), pin1 at center-3.81 (away)\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " - offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            comp_angle_" << i << " = 90\n";
        code << "            escape_dir_" << i << " = 'left'\n";
        code << "        elif orient_" << i << " == 1:  # pin points left, escape right\n";
        code << "            # Companion rotated 270deg: pin2 at center-3.81 (toward IC), pin1 at center+3.81 (away)\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " + offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            comp_angle_" << i << " = 270\n";
        code << "            escape_dir_" << i << " = 'right'\n";
        code << "        elif orient_" << i << " == 2:  # pin points up, escape down\n";
        code << "            # Companion at 180deg: pin2 at center-3.81 (toward IC), pin1 at center+3.81 (away)\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << " + offset_mm_" << i << ")\n";
        code << "            comp_angle_" << i << " = 180\n";
        code << "            escape_dir_" << i << " = 'down'\n";
        code << "        elif orient_" << i << " == 3:  # pin points down, escape up\n";
        code << "            # Companion at 0deg: pin2 at center+3.81 (toward IC), pin1 at center-3.81 (away)\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << " - offset_mm_" << i << ")\n";
        code << "            comp_angle_" << i << " = 0\n";
        code << "            escape_dir_" << i << " = 'up'\n";
        code << "        else:\n";
        code << "            cx_" << i << " = snap_to_grid(px_" << i << " + offset_mm_" << i << ")\n";
        code << "            cy_" << i << " = snap_to_grid(py_" << i << ")\n";
        code << "            comp_angle_" << i << " = 270\n";
        code << "            escape_dir_" << i << " = 'right'\n";
        code << "\n";

        // Find non-overlapping position using push-out + perpendicular staggering
        code << "        # Find clear position using push-out + perpendicular staggering\n";
        code << "        cx_" << i << ", cy_" << i << ", _offset_" << i << ", _perp_" << i << " = _find_clear_position(\n";
        code << "            px_" << i << ", py_" << i << ", escape_dir_" << i << ", placed_companions, ic_bbox_expanded)\n";
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


        // Get companion pin 2 position for wire stub (pin 2 connects to IC)
        code << "        # Get companion pin 2 position for wire stub (pin2 toward IC)\n";
        code << "        comp_pin2_" << i << " = sch.symbols.get_transformed_pin_position(comp_sym_" << i << ", '2')\n";
        code << "        if comp_pin2_" << i << ":\n";
        code << "            cp2x_" << i << " = comp_pin2_" << i << "['position'].x / 1_000_000\n";
        code << "            cp2y_" << i << " = comp_pin2_" << i << "['position'].y / 1_000_000\n";
        code << "        else:\n";
        code << "            # Fallback: assume pin 2 is at component position\n";
        code << "            cp2x_" << i << " = cx_" << i << "\n";
        code << "            cp2y_" << i << " = cy_" << i << "\n";
        code << "\n";

        // Draw wire stub from IC pin to companion pin 2 (ALWAYS draw, no pin-to-pin)
        code << "        # Draw wire stub from IC pin to companion pin 2\n";
        code << "        _w" << i << "_x0 = snap_to_grid(px_" << i << ")\n";
        code << "        _w" << i << "_y0 = snap_to_grid(py_" << i << ")\n";
        code << "        _w" << i << "_x1 = snap_to_grid(cp2x_" << i << ")\n";
        code << "        _w" << i << "_y1 = snap_to_grid(cp2y_" << i << ")\n";
        code << "        _w" << i << "_len = (((_w" << i << "_x1 - _w" << i << "_x0)**2 + (_w" << i << "_y1 - _w" << i << "_y0)**2)**0.5)\n";
        code << "        _debug.append(f'Comp" << i << " wire stub: ({_w" << i << "_x0:.2f},{_w" << i << "_y0:.2f}) to ({_w" << i << "_x1:.2f},{_w" << i << "_y1:.2f}) len={_w" << i << "_len:.2f}')\n";
        code << "        if _w" << i << "_len >= 0.1:  # Only draw if non-zero length\n";
        code << "            wire_start_" << i << " = Vector2.from_xy_mm(_w" << i << "_x0, _w" << i << "_y0)\n";
        code << "            wire_end_" << i << " = Vector2.from_xy_mm(_w" << i << "_x1, _w" << i << "_y1)\n";
        code << "            sch.wiring.add_wire(wire_start_" << i << ", wire_end_" << i << ")\n";
        code << "\n";

        // Handle terminal_power - place power symbols
        if( companion.contains( "terminal_power" ) && companion["terminal_power"].is_object() )
        {
            code << "        # Place power symbols at terminals\n";

            for( auto it = companion["terminal_power"].begin(); it != companion["terminal_power"].end(); ++it )
            {
                std::string pinNum = it.key();
                std::string powerName = it.value().get<std::string>();

                // Extract just the power name (remove library prefix if present)
                size_t colonPos = powerName.find( ':' );
                if( colonPos != std::string::npos )
                    powerName = powerName.substr( colonPos + 1 );

                code << "        # Power symbol at pin " << pinNum << ": " << powerName << "\n";
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
                code << "                pwr_name_upper = '" << EscapePythonString( powerName ) << "'.upper()\n";
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
                code << "                pwr_sym = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pwr_pos, angle=pwr_angle)\n";
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
        code << "        # DEBUG: Draw bbox rectangle around companion\n";
        code << "        _debug_bbox_" << i << " = _calc_companion_bbox(cx_" << i << ", cy_" << i << ", escape_dir_" << i << ")\n";
        code << "        _draw_debug_bbox(_debug_bbox_" << i << ", comp_sym_" << i << ".reference)\n";
        code << "\n";

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
