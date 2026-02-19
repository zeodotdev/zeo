#include "sch_connect_net_handler.h"
#include <sstream>


bool SCH_CONNECT_NET_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_connect_net" ||
           aToolName == "sch_connect_to_power";
}


std::string SCH_CONNECT_NET_HANDLER::Execute( const std::string& aToolName,
                                               const nlohmann::json& aInput )
{
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_CONNECT_NET_HANDLER::GetDescription( const std::string& aToolName,
                                                      const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_connect_to_power" )
    {
        std::string power = aInput.value( "power", "" );
        if( aInput.contains( "pins" ) && aInput["pins"].is_array() )
        {
            size_t count = aInput["pins"].size();
            if( count == 1 )
                return "Connecting " + aInput["pins"][0].get<std::string>() + " to " + power;
            return "Connecting " + std::to_string( count ) + " pins to " + power;
        }
        return "Connecting to " + power;
    }

    if( aInput.contains( "pins" ) && aInput["pins"].is_array() )
    {
        size_t count = aInput["pins"].size();
        if( count <= 3 )
        {
            std::string pinList;
            for( size_t i = 0; i < count; ++i )
            {
                if( i > 0 ) pinList += ", ";
                pinList += aInput["pins"][i].get<std::string>();
            }
            return "Connecting " + pinList;
        }
        return "Connecting " + std::to_string( count ) + " pins";
    }
    return "Connecting pins";
}


bool SCH_CONNECT_NET_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return true;
}


std::string SCH_CONNECT_NET_HANDLER::GetIPCCommand( const std::string& aToolName,
                                                     const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_connect_to_power" )
        return "run_shell sch " + GenerateConnectToPowerCode( aInput );

    return "run_shell sch " + GenerateConnectNetCode( aInput );
}


std::string SCH_CONNECT_NET_HANDLER::EscapePythonString( const std::string& aStr ) const
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


// ---------------------------------------------------------------------------
// Python script templates as raw string literals
// ---------------------------------------------------------------------------

// Preamble: imports, document refresh, snap helper (shared by all routing tools)
static const char* ROUTING_PREAMBLE = R"py(import json, sys, re
from kipy.geometry import Vector2

# Refresh document to handle close/reopen cycles
if hasattr(sch, 'refresh_document'):
    if not sch.refresh_document():
        raise RuntimeError('Schematic editor not open or document not available')

def snap_to_grid(val, grid=1.27):
    return round(val / grid) * grid

)py";


// Pin resolution helper: resolves a symbol pin's position and escape direction.
// Expects `ref` and `pin_id` to be set, appends result to `pin_positions`.
// Used inline within both sch_connect_net and sch_connect_to_power.
static const char* PIN_RESOLVE_CODE = R"py(
            ref = name
            sym = sch.symbols.get_by_ref(ref)
            if not sym:
                raise ValueError(f'Symbol not found: {ref}')
            pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)
            if not pin_result:
                pin_pos = sch.symbols.get_pin_position(sym, pin_id)
                if not pin_pos:
                    raise ValueError(f'Pin not found: {pin_id} on {ref}')
                px = pin_pos.x / 1_000_000
                py = pin_pos.y / 1_000_000
                pin_orientation = None
            else:
                px = pin_result['position'].x / 1_000_000
                py = pin_result['position'].y / 1_000_000
                pin_orientation = pin_result.get('orientation', None)
            # The API returns library-level pin orientation, not transformed
            # by symbol rotation.  Apply the symbol's rotation manually.
            # Each 90 CCW step in schematic coords (Y-down) maps:
            #   RIGHT(0)->UP(2), LEFT(1)->DOWN(3), UP(2)->LEFT(1), DOWN(3)->RIGHT(0)
            if pin_orientation is not None:
                _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
                _rot_steps = round(getattr(sym, 'angle', 0) / 90) % 4
                for _ in range(_rot_steps):
                    pin_orientation = _rot90.get(pin_orientation, pin_orientation)
            # Compute outward escape direction from pin orientation enum.
            #   0 = PIN_RIGHT (toward body) -> escape LEFT
            #   1 = PIN_LEFT  (toward body) -> escape RIGHT
            #   2 = PIN_UP    (toward body) -> escape DOWN
            #   3 = PIN_DOWN  (toward body) -> escape UP
            if pin_orientation is not None:
                if pin_orientation == 1:
                    out_dx, out_dy = 1.27, 0       # PIN_LEFT -> escape right
                    pin_dir = 'h'
                elif pin_orientation == 0:
                    out_dx, out_dy = -1.27, 0      # PIN_RIGHT -> escape left
                    pin_dir = 'h'
                elif pin_orientation == 2:
                    out_dx, out_dy = 0, 1.27       # PIN_UP -> escape down
                    pin_dir = 'v'
                elif pin_orientation == 3:
                    out_dx, out_dy = 0, -1.27      # PIN_DOWN -> escape up
                    pin_dir = 'v'
                else:
                    out_dx, out_dy = 1.27, 0
                    pin_dir = 'h'
            else:
                # Fallback: guess from symbol center
                sym_cx = sym.position.x / 1_000_000
                sym_cy = sym.position.y / 1_000_000
                if abs(px - sym_cx) >= abs(py - sym_cy):
                    out_dx = 1.27 if px > sym_cx else -1.27
                    out_dy = 0
                    pin_dir = 'h'
                else:
                    out_dx = 0
                    out_dy = 1.27 if py > sym_cy else -1.27
                    pin_dir = 'v'
            _dir_name = {(1.27,0):'RIGHT', (-1.27,0):'LEFT', (0,-1.27):'UP', (0,1.27):'DOWN'}.get((out_dx,out_dy), '?')
            print(f'[route] {ref}:{pin_id} pos=({px:.2f},{py:.2f}) orient={pin_orientation} ang={getattr(sym, "angle", 0)} -> {_dir_name} ({out_dx},{out_dy})', file=sys.stderr)
            pin_positions.append({'ref': ref, 'pin': pin_id, 'x': snap_to_grid(px), 'y': snap_to_grid(py), 'raw_x': px, 'raw_y': py, 'esc_x': snap_to_grid(px + out_dx), 'esc_y': snap_to_grid(py + out_dy), 'dir': pin_dir, 'out_dx': out_dx, 'out_dy': out_dy})
)py";


// sch_connect_net Phase 1: pin/label resolution.
// Expects `pin_specs` list to be defined. Opens try: block.
static const char* CONNECT_NET_RESOLVE = R"py(
try:
    # Phase 1: Resolve pin/label positions
    pin_positions = []
    for spec_type, name, pin_id in pin_specs:
        if spec_type == 'label':
            # Label spec: find existing label by text
            ref = name
            all_labels = sch.labels.get_all()
            matches = [l for l in all_labels if hasattr(l, 'text') and l.text == name]
            if not matches:
                raise ValueError(f'Label not found: {name}')
            lbl = matches[0]
            px = lbl.position.x / 1_000_000
            py = lbl.position.y / 1_000_000
            out_dx, out_dy = 0, 0
            pin_dir = 'h'
            print(f'[route] label:{name} pos=({px:.2f},{py:.2f})', file=sys.stderr)
        else:
)py";


// Obstacle map building, A* pathfinder, and wire placement helpers.
// Shared by sch_connect_net and sch_connect_to_power.
// Continues inside try: block at 4-space indent.
static const char* ROUTING_INFRASTRUCTURE = R"py(
    wire_count = 0
    junction_count = 0

    # Build obstacle map from graphical bounding boxes of ALL symbols and labels.
    # Shrink bbox edges that have pins so wires can reach pin tips
    # without the body registering as an obstacle.
    # Also collect all pin-tip grid cells so the router won't cross
    # intermediate pins on multi-pin components (prevents MergeOverlap shorts).
    obstacles = []
    pin_cells = set()
    _grid = 1.27
    try:
        all_symbols = sch.symbols.get_all()
        for obs_sym in all_symbols:
            try:
                bbox = sch.transform.get_bounding_box(obs_sym, units='mm', include_text=False)
            except:
                continue
            if not bbox:
                continue
            bx0, bx1 = bbox['min_x'], bbox['max_x']
            by0, by1 = bbox['min_y'], bbox['max_y']
            # Shrink each edge to the pin tips that exit from it, using pin
            # orientation rotated by symbol angle (same transform the router uses).
            # This moves each edge inward to exactly where the outermost pin is,
            # so pin tips sit at the bbox boundary rather than inside it.
            _edge_left = []   # pin x-coords exiting left
            _edge_right = []  # pin x-coords exiting right
            _edge_top = []    # pin y-coords exiting up
            _edge_bottom = [] # pin y-coords exiting down
            _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
            _rot_steps = round(getattr(obs_sym, 'angle', 0) / 90) % 4
            for sp in obs_sym.pins:
                try:
                    tp = sch.symbols.get_transformed_pin_position(obs_sym, sp.number)
                    if not tp:
                        continue
                    po = tp.get('orientation', None)
                    if po is None:
                        continue
                    px = tp['position'].x / 1_000_000
                    py = tp['position'].y / 1_000_000
                    for _ in range(_rot_steps):
                        po = _rot90.get(po, po)
                    if po == 0: _edge_left.append(px)      # PIN_RIGHT toward body -> escape left
                    elif po == 1: _edge_right.append(px)    # PIN_LEFT toward body -> escape right
                    elif po == 2: _edge_bottom.append(py)   # PIN_UP toward body -> escape down
                    elif po == 3: _edge_top.append(py)      # PIN_DOWN toward body -> escape up
                    pin_cells.add((round(px / _grid), round(py / _grid)))
                except:
                    pass
            # Push each edge grid/2 past the outermost pin so that
            # _cell_blocked (which uses half = grid/2 - 0.01) treats
            # pin-tip cells as outside the bbox, not on the boundary.
            _shrink = 1.27 / 2
            if _edge_left: bx0 = max(bx0, max(_edge_left) + _shrink)
            if _edge_right: bx1 = min(bx1, min(_edge_right) - _shrink)
            if _edge_top: by0 = max(by0, max(_edge_top) + _shrink)
            if _edge_bottom: by1 = min(by1, min(_edge_bottom) - _shrink)
            if bx0 < bx1 and by0 < by1:
                obstacles.append({'min_x': bx0, 'max_x': bx1, 'min_y': by0, 'max_y': by1})
    except:
        pass
    try:
        for obs_lbl in sch.labels.get_all():
            try:
                bbox = sch.transform.get_bounding_box(obs_lbl, units='mm')
            except:
                continue
            if not bbox:
                continue
            obstacles.append({'min_x': bbox['min_x'], 'max_x': bbox['max_x'], 'min_y': bbox['min_y'], 'max_y': bbox['max_y']})
    except:
        pass

    print(f'[route] Pin obstacle cells: {len(pin_cells)}', file=sys.stderr)

    # Build directional wire obstacle sets.
    # Horizontal wires block horizontal movement; vertical wires block vertical movement.
    # This prevents collinear overlap which triggers MergeOverlap (false connections).
    # Perpendicular crossings are safe and remain unblocked.
    h_wire_cells = set()
    v_wire_cells = set()
    try:
        for w in sch.wiring.get_wires():
            wx0 = w.start.x / 1_000_000
            wy0 = w.start.y / 1_000_000
            wx1 = w.end.x / 1_000_000
            wy1 = w.end.y / 1_000_000
            gwx0, gwy0 = round(wx0 / _grid), round(wy0 / _grid)
            gwx1, gwy1 = round(wx1 / _grid), round(wy1 / _grid)
            if gwy0 == gwy1 and gwx0 != gwx1:
                for gx in range(min(gwx0, gwx1), max(gwx0, gwx1) + 1):
                    h_wire_cells.add((gx, gwy0))
            elif gwx0 == gwx1 and gwy0 != gwy1:
                for gy in range(min(gwy0, gwy1), max(gwy0, gwy1) + 1):
                    v_wire_cells.add((gwx0, gy))
    except Exception:
        pass
    print(f'[route] Wire obstacles: {len(h_wire_cells)} horizontal cells, {len(v_wire_cells)} vertical cells', file=sys.stderr)

    def _add_waypoints_to_wire_sets(waypoints, grid=1.27):
        """Register a just-placed path in the directional wire cell sets."""
        for i in range(len(waypoints) - 1):
            ax, ay = waypoints[i]
            bx, by = waypoints[i + 1]
            gx0, gy0 = round(ax / grid), round(ay / grid)
            gx1, gy1 = round(bx / grid), round(by / grid)
            if gy0 == gy1 and gx0 != gx1:
                for gx in range(min(gx0, gx1), max(gx0, gx1) + 1):
                    h_wire_cells.add((gx, gy0))
            elif gx0 == gx1 and gy0 != gy1:
                for gy in range(min(gy0, gy1), max(gy0, gy1) + 1):
                    v_wire_cells.add((gx0, gy))

    def _cell_blocked(cx, cy, grid=1.27):
        """Check if a grid cell center is inside any obstacle."""
        half = grid / 2 - 0.01
        for obs in obstacles:
            if cx + half > obs['min_x'] and cx - half < obs['max_x'] and cy + half > obs['min_y'] and cy - half < obs['max_y']:
                return True
        return False

    import heapq
    def _astar(x0, y0, x1, y1, grid=1.27, bend_cost=3, cross_cost=2, start_dir=-1, end_dir=-1, margin=15):
        """A* pathfinding on the schematic grid. Returns list of (x,y) waypoints,
        or None if no path found within the search bounds.
        bend_cost: extra cost for changing direction (default 3).
        cross_cost: extra cost for crossing an existing wire perpendicularly (default 2).
        start_dir: if >= 0, preferred first-step direction (0=right, 1=left, 2=down, 3=up).
                   Deviating costs an extra bend penalty (soft constraint).
        end_dir: if >= 0, preferred approach direction into the goal cell.
                 Arriving from a different direction costs an extra bend penalty."""
        # Snap start/end to grid
        gx0, gy0 = round(x0 / grid), round(y0 / grid)
        gx1, gy1 = round(x1 / grid), round(y1 / grid)
        if gx0 == gx1 and gy0 == gy1:
            return [(x0, y0), (x1, y1)]
        # Search bounds: bounding box of start/end + margin
        g_min_x = min(gx0, gx1) - margin
        g_max_x = max(gx0, gx1) + margin
        g_min_y = min(gy0, gy1) - margin
        g_max_y = max(gy0, gy1) + margin
        # Directions: right, left, down, up
        dirs = [(1, 0), (-1, 0), (0, 1), (0, -1)]
        # Use start_dir as initial prev_d so first step prefers outward direction
        init_d = start_dir if start_dir >= 0 else -1
        start_key = (gx0, gy0, init_d)
        goal = (gx1, gy1)
        open_set = [(abs(gx1 - gx0) + abs(gy1 - gy0), 0, gx0, gy0, init_d)]
        g_scores = {start_key: 0}
        came_from = {start_key: None}
        while open_set:
            f, g, gx, gy, prev_d = heapq.heappop(open_set)
            if (gx, gy) == goal:
                # Reconstruct path as grid coords
                path_g = [(gx, gy)]
                key = (gx, gy, prev_d)
                while came_from.get(key) is not None:
                    key = came_from[key]
                    path_g.append((key[0], key[1]))
                path_g.reverse()
                # Convert to mm and simplify to waypoints (collapse collinear segments)
                waypoints = [(path_g[0][0] * grid, path_g[0][1] * grid)]
                for i in range(1, len(path_g) - 1):
                    px, py = path_g[i - 1]
                    cx, cy = path_g[i]
                    nx, ny = path_g[i + 1]
                    if (nx - cx) != (cx - px) or (ny - cy) != (cy - py):
                        waypoints.append((cx * grid, cy * grid))
                waypoints.append((path_g[-1][0] * grid, path_g[-1][1] * grid))
                # Snap first/last to exact pin positions
                waypoints[0] = (x0, y0)
                waypoints[-1] = (x1, y1)
                return waypoints
            current_key = (gx, gy, prev_d)
            if g > g_scores.get(current_key, float('inf')):
                continue
            for di, (dx, dy) in enumerate(dirs):
                nx, ny = gx + dx, gy + dy
                if nx < g_min_x or nx > g_max_x or ny < g_min_y or ny > g_max_y:
                    continue
                # Allow start and goal cells even if blocked
                if (nx, ny) != goal and (nx, ny) != (gx0, gy0):
                    if _cell_blocked(nx * grid, ny * grid, grid):
                        continue
                    # Block movement into pin-tip cells of other components.
                    # Pin tips are outside their component's bbox (due to shrinking)
                    # but wires touching them create MergeOverlap connections.
                    if (nx, ny) in pin_cells:
                        continue
                    # Block parallel movement along existing wires (prevents MergeOverlap).
                    # Skip when leaving the start cell — arrival wires from previous
                    # chain segments form junctions at the pin, not illegal overlaps.
                    if (gx, gy) != (gx0, gy0):
                        if dy == 0 and (nx, ny) in h_wire_cells:
                            continue
                        if dx == 0 and (nx, ny) in v_wire_cells:
                            continue
                move_cost = 1
                if prev_d >= 0 and di != prev_d:
                    move_cost += bend_cost
                # Soft penalty for crossing an existing wire perpendicularly.
                # Parallel overlap is hard-blocked above; perpendicular crossings
                # are allowed but discouraged so the router prefers clean paths.
                if dy == 0 and (nx, ny) in v_wire_cells:
                    move_cost += cross_cost
                elif dx == 0 and (nx, ny) in h_wire_cells:
                    move_cost += cross_cost
                # Soft penalty for arriving at goal from a direction that
                # doesn't match the destination pin's escape direction.
                if (nx, ny) == goal and end_dir >= 0 and di != end_dir:
                    move_cost += bend_cost
                new_g = g + move_cost
                nkey = (nx, ny, di)
                if new_g < g_scores.get(nkey, float('inf')):
                    g_scores[nkey] = new_g
                    h = abs(gx1 - nx) + abs(gy1 - ny)
                    heapq.heappush(open_set, (new_g + h, new_g, nx, ny, di))
                    came_from[nkey] = current_key
        return None

    def _path_hits_obstacle(waypoints, grid=1.27):
        """Check if any intermediate cell on the path is inside an obstacle
        or would overlap an existing wire in the same direction.
        Excludes the first and last cells (pin endpoints sit inside their own
        component's bounding box, matching A*'s start/goal exclusion).
        Returns (hit, obs_desc) — hit is True if the path overlaps a component or wire."""
        # Pin endpoint cells to exclude
        ep_start = (round(waypoints[0][0] / grid), round(waypoints[0][1] / grid))
        ep_end = (round(waypoints[-1][0] / grid), round(waypoints[-1][1] / grid))
        # Cells adjacent to endpoints — wire overlap is expected here (junctions)
        _ep_adj = {ep_start, ep_end}
        for _ep in [ep_start, ep_end]:
            for _ddx, _ddy in [(1,0),(-1,0),(0,1),(0,-1)]:
                _ep_adj.add((_ep[0]+_ddx, _ep[1]+_ddy))
        for i in range(len(waypoints) - 1):
            ax, ay = waypoints[i]
            bx, by = waypoints[i + 1]
            gx0, gy0 = round(ax / grid), round(ay / grid)
            gx1, gy1 = round(bx / grid), round(by / grid)
            if gx0 == gx1:  # vertical segment
                for gy in range(min(gy0, gy1), max(gy0, gy1) + 1):
                    if (gx0, gy) == ep_start or (gx0, gy) == ep_end:
                        continue
                    cx, cy = gx0 * grid, gy * grid
                    if _cell_blocked(cx, cy, grid):
                        return True, f'({cx:.2f}, {cy:.2f})'
                    if (gx0, gy) in pin_cells:
                        return True, f'pin at ({cx:.2f}, {cy:.2f})'
                    if (gx0, gy) not in _ep_adj and (gx0, gy) in v_wire_cells:
                        return True, f'wire overlap at ({cx:.2f}, {cy:.2f})'
            else:  # horizontal segment
                for gx in range(min(gx0, gx1), max(gx0, gx1) + 1):
                    if (gx, gy0) == ep_start or (gx, gy0) == ep_end:
                        continue
                    cx, cy = gx * grid, gy0 * grid
                    if _cell_blocked(cx, cy, grid):
                        return True, f'({cx:.2f}, {cy:.2f})'
                    if (gx, gy0) in pin_cells:
                        return True, f'pin at ({cx:.2f}, {cy:.2f})'
                    if (gx, gy0) not in _ep_adj and (gx, gy0) in h_wire_cells:
                        return True, f'wire overlap at ({cx:.2f}, {cy:.2f})'
        return False, None

    def _place_path(waypoints):
        """Place wire segments along a list of waypoints. Returns (count, wires)."""
        wires = []
        for i in range(len(waypoints) - 1):
            ax, ay = waypoints[i]
            bx, by = waypoints[i + 1]
            w = sch.wiring.add_wire(Vector2.from_xy_mm(ax, ay), Vector2.from_xy_mm(bx, by))
            wires.append(w)
        return len(wires), wires

    def _place_needed_junctions(placed_wires):
        """Query KiCad for needed junctions and place them."""
        if not placed_wires:
            return 0
        positions = sch.wiring.get_needed_junctions(placed_wires)
        for pos in positions:
            sch.wiring.add_junction(pos)
        return len(positions)

    def _dir_index(dx, dy):
        """Convert (dx, dy) outward direction to A* direction index."""
        if dx > 0: return 0  # right
        if dx < 0: return 1  # left
        if dy > 0: return 2  # down
        if dy < 0: return 3  # up
        return -1

    def _route_pins(p0, p1):
        """Route between two pins using A* with soft pin direction penalties.
        The pin escape direction sets the preferred initial/final wire direction;
        deviating costs an extra bend penalty but is not forbidden."""
        x0, y0 = p0['raw_x'], p0['raw_y']
        x1, y1 = p1['raw_x'], p1['raw_y']
        sd = _dir_index(p0['out_dx'], p0['out_dy'])
        ed = _dir_index(p1['out_dx'], p1['out_dy'])
        # end_dir is opposite of outward (wire approaches from outside, moving inward)
        ed = ed ^ 1 if ed >= 0 else -1
        _dn = {0:'RIGHT', 1:'LEFT', 2:'DOWN', 3:'UP', -1:'NONE'}
        print(f'[route] A* {p0["ref"]}:{p0["pin"]} -> {p1["ref"]}:{p1["pin"]}  start_dir={_dn.get(sd)} end_dir={_dn.get(ed)}', file=sys.stderr)
        # Try progressively wider search margins
        wp = None
        for a_margin, a_label in [(15, 'default'), (30, 'wider margin'), (50, 'max margin')]:
            wp = _astar(x0, y0, x1, y1, start_dir=sd, end_dir=ed, margin=a_margin)
            if wp is not None:
                if a_label != 'default':
                    print(f'[route]   found path with retry: {a_label}', file=sys.stderr)
                break
            print(f'[route]   no path with {a_label}, retrying...', file=sys.stderr)
        if wp is None:
            raise ValueError(f'No path found from {p0["ref"]}:{p0["pin"]} to {p1["ref"]}:{p1["pin"]}. Try repositioning components to clear the path.')
        print(f'[route]   path: {[(round(x,2),round(y,2)) for x,y in wp]}', file=sys.stderr)
        return wp

)py";


// sch_connect_net routing modes: chain and star topology.
// Continues inside try: block. Closes with except handler.
static const char* CONNECT_NET_ROUTING = R"py(
    print(f'[route] MODE={routing_mode} pins={len(pin_positions)}', file=sys.stderr)

    if routing_mode == 'chain':
        # Chain mode: A* route each consecutive pin pair
        _all_wires = []
        for ci in range(len(pin_positions) - 1):
            p0, p1 = pin_positions[ci], pin_positions[ci + 1]
            waypoints = _route_pins(p0, p1)
            hit, loc = _path_hits_obstacle(waypoints)
            if hit:
                raise ValueError(f'Wire from {p0["ref"]}:{p0["pin"]} to {p1["ref"]}:{p1["pin"]} would pass through a component at {loc}. Try repositioning components to clear the path.')
            _n, _ws = _place_path(waypoints)
            wire_count += _n
            _all_wires.extend(_ws)
            _add_waypoints_to_wire_sets(waypoints)

        junction_count = _place_needed_junctions(_all_wires)

    elif len(pin_positions) == 2:
        # 2-pin: direct A* route
        p0, p1 = pin_positions[0], pin_positions[1]
        waypoints = _route_pins(p0, p1)
        hit, loc = _path_hits_obstacle(waypoints)
        if hit:
            raise ValueError(f'Wire from {p0["ref"]}:{p0["pin"]} to {p1["ref"]}:{p1["pin"]} would pass through a component at {loc}. Try repositioning components to clear the path.')
        _n, _ws = _place_path(waypoints)
        wire_count += _n
        junction_count = _place_needed_junctions(_ws)
    else:
        # 3+ pins: trunk-and-branch routing with pre-escaped positions.
        # Use escaped positions so the trunk never extends into component bboxes.
        esc_xs = [p['esc_x'] for p in pin_positions]
        esc_ys = [p['esc_y'] for p in pin_positions]
        x_spread = max(esc_xs) - min(esc_xs)
        y_spread = max(esc_ys) - min(esc_ys)
        is_horizontal = x_spread >= y_spread

        def trunk_hits_obstacle(trunk_val, t_min, t_max):
            for obs in obstacles:
                if is_horizontal:
                    if obs['min_y'] <= trunk_val <= obs['max_y']:
                        if obs['max_x'] > t_min and obs['min_x'] < t_max:
                            return True
                else:
                    if obs['min_x'] <= trunk_val <= obs['max_x']:
                        if obs['max_y'] > t_min and obs['min_y'] < t_max:
                            return True
            # Check wire overlap and pin crossings along the trunk line
            _tg = 1.27
            g_trunk = round(trunk_val / _tg)
            g_min = round(t_min / _tg)
            g_max = round(t_max / _tg)
            if is_horizontal:
                for gx in range(g_min, g_max + 1):
                    if (gx, g_trunk) in h_wire_cells:
                        return True
                    if (gx, g_trunk) in pin_cells:
                        return True
            else:
                for gy in range(g_min, g_max + 1):
                    if (g_trunk, gy) in v_wire_cells:
                        return True
                    if (g_trunk, gy) in pin_cells:
                        return True
            return False

        # Choose trunk position (median of perpendicular escaped coords)
        if is_horizontal:
            perp_coords = sorted(esc_ys)
            trunk_min = snap_to_grid(min(esc_xs))
            trunk_max = snap_to_grid(max(esc_xs))
        else:
            perp_coords = sorted(esc_xs)
            trunk_min = snap_to_grid(min(esc_ys))
            trunk_max = snap_to_grid(max(esc_ys))

        mid = len(perp_coords) // 2
        if len(perp_coords) % 2 == 0:
            trunk_perp = snap_to_grid((perp_coords[mid-1] + perp_coords[mid]) / 2)
        else:
            trunk_perp = snap_to_grid(perp_coords[mid])

        # Obstacle avoidance: shift trunk if needed
        if trunk_hits_obstacle(trunk_perp, trunk_min, trunk_max):
            best = None
            for offset in range(1, 20):
                for direction in [1, -1]:
                    candidate = snap_to_grid(trunk_perp + direction * offset * 1.27)
                    if not trunk_hits_obstacle(candidate, trunk_min, trunk_max):
                        if best is None or abs(candidate - trunk_perp) < abs(best - trunk_perp):
                            best = candidate
                        break
                if best is not None and offset > 1:
                    break
            if best is not None:
                trunk_perp = best

        # Collect on-trunk escape projections (along trunk axis) to avoid branch collisions
        _on_trunk_projs = set()
        for p in pin_positions:
            if is_horizontal:
                if abs(p['esc_y'] - trunk_perp) <= 0.01:
                    _on_trunk_projs.add(round(p['esc_x'], 4))
            else:
                if abs(p['esc_x'] - trunk_perp) <= 0.01:
                    _on_trunk_projs.add(round(p['esc_y'], 4))

        # Compute trunk midpoint for offset direction
        _all_projs = [p['esc_x'] if is_horizontal else p['esc_y'] for p in pin_positions]
        _proj_mid = (min(_all_projs) + max(_all_projs)) / 2

        # Count off-trunk pins at each endpoint to decide offset
        _ep_counts = {}
        _on_trunk_at = set()  # Track which endpoints have on-trunk escapes
        for p in pin_positions:
            if is_horizontal:
                proj = p['esc_x']
                off = abs(p['esc_y'] - trunk_perp) > 0.01
                at_ep = abs(proj - trunk_min) < 0.01 or abs(proj - trunk_max) < 0.01
            else:
                proj = p['esc_y']
                off = abs(p['esc_x'] - trunk_perp) > 0.01
                at_ep = abs(proj - trunk_min) < 0.01 or abs(proj - trunk_max) < 0.01
            if off and at_ep:
                key = round(proj, 4)
                _ep_counts[key] = _ep_counts.get(key, 0) + 1
            elif not off and at_ep:
                _on_trunk_at.add(round(proj, 4))

        def _offset_from_pin(proj):
            """Offset a trunk projection away from on-trunk pins toward trunk center."""
            if round(proj, 4) not in _on_trunk_projs:
                return proj
            offset_dir = 1.27 if proj < _proj_mid else -1.27
            return snap_to_grid(proj + offset_dir)

        # Pre-compute branch connection points on the trunk using escaped positions
        trunk_conn = []
        for p in pin_positions:
            if is_horizontal:
                proj = p['esc_x']
                off = abs(p['esc_y'] - trunk_perp) > 0.01
                at_ep = abs(proj - trunk_min) < 0.01 or abs(proj - trunk_max) < 0.01
                solo_ep = off and at_ep and _ep_counts.get(round(proj, 4), 0) == 1 and round(proj, 4) not in _on_trunk_at
                if solo_ep:
                    inward = 2.54 if abs(proj - trunk_max) < 0.01 else -2.54
                    tgt_along = snap_to_grid(proj - inward)
                    p['_tgt'] = (tgt_along, trunk_perp)
                    p['_ep'] = True
                    trunk_conn.append(tgt_along)
                elif off:
                    tgt_proj = _offset_from_pin(proj)
                    if is_horizontal:
                        p['_tgt'] = (tgt_proj, trunk_perp)
                    else:
                        p['_tgt'] = (trunk_perp, tgt_proj)
                    trunk_conn.append(tgt_proj)
                else:
                    trunk_conn.append(proj)
            else:
                proj = p['esc_y']
                off = abs(p['esc_x'] - trunk_perp) > 0.01
                at_ep = abs(proj - trunk_min) < 0.01 or abs(proj - trunk_max) < 0.01
                solo_ep = off and at_ep and _ep_counts.get(round(proj, 4), 0) == 1 and round(proj, 4) not in _on_trunk_at
                if solo_ep:
                    inward = 2.54 if abs(proj - trunk_max) < 0.01 else -2.54
                    tgt_along = snap_to_grid(proj - inward)
                    p['_tgt'] = (trunk_perp, tgt_along)
                    p['_ep'] = True
                    trunk_conn.append(tgt_along)
                elif off:
                    tgt_proj = _offset_from_pin(proj)
                    if is_horizontal:
                        p['_tgt'] = (tgt_proj, trunk_perp)
                    else:
                        p['_tgt'] = (trunk_perp, tgt_proj)
                    trunk_conn.append(tgt_proj)
                else:
                    trunk_conn.append(proj)

        # Adjust trunk extent to actual connection points
        trunk_min = snap_to_grid(min(trunk_conn))
        trunk_max = snap_to_grid(max(trunk_conn))

        # Collect trunk split points: off-trunk branch targets + on-trunk interior escapes
        _branch_conn_pts = set()
        for p in pin_positions:
            if is_horizontal:
                off = abs(p['esc_y'] - trunk_perp) > 0.01
            else:
                off = abs(p['esc_x'] - trunk_perp) > 0.01
            if off and '_tgt' in p:
                tgt = p['_tgt']
                proj = tgt[0] if is_horizontal else tgt[1]
                if trunk_min < proj < trunk_max:
                    _branch_conn_pts.add(round(proj, 4))
            elif not off:
                # On-trunk escape in trunk interior needs a split point
                esc_proj = p['esc_x'] if is_horizontal else p['esc_y']
                if trunk_min + 0.01 < esc_proj < trunk_max - 0.01:
                    _branch_conn_pts.add(round(esc_proj, 4))

        _all_wires = []
        if trunk_min != trunk_max:
            if is_horizontal:
                t0, t1 = (trunk_min, trunk_perp), (trunk_max, trunk_perp)
            else:
                t0, t1 = (trunk_perp, trunk_min), (trunk_perp, trunk_max)
            trunk_wp = [t0, t1]
            hit, loc = _path_hits_obstacle(trunk_wp)
            if hit:
                raise ValueError(f'Trunk wire would pass through a component at {loc}. Try repositioning components to clear the path.')
            # Build sorted list of split points along trunk axis
            _split_vals = sorted(_branch_conn_pts)
            _trunk_points = [trunk_min] + _split_vals + [trunk_max]
            # Remove duplicates while preserving order
            _trunk_points_dedup = []
            for _tv in _trunk_points:
                if not _trunk_points_dedup or abs(_tv - _trunk_points_dedup[-1]) > 0.001:
                    _trunk_points_dedup.append(_tv)
            _trunk_points = _trunk_points_dedup
            if _branch_conn_pts:
                print(f'[route] TRUNK split at {sorted(_branch_conn_pts)} (segs={len(_trunk_points)-1})', file=sys.stderr)
            for _ti in range(len(_trunk_points) - 1):
                if is_horizontal:
                    _ts = (_trunk_points[_ti], trunk_perp)
                    _te = (_trunk_points[_ti + 1], trunk_perp)
                else:
                    _ts = (trunk_perp, _trunk_points[_ti])
                    _te = (trunk_perp, _trunk_points[_ti + 1])
                _tw = sch.wiring.add_wire(Vector2.from_xy_mm(*_ts), Vector2.from_xy_mm(*_te))
                _all_wires.append(_tw)
                wire_count += 1
                _add_waypoints_to_wire_sets([_ts, _te])

        # Place branches from escaped position to trunk target (no forced escape)
        for p in pin_positions:
            if is_horizontal:
                off_trunk = abs(p['esc_y'] - trunk_perp) > 0.01
            else:
                off_trunk = abs(p['esc_x'] - trunk_perp) > 0.01

            if off_trunk:
                esc_x, esc_y = p['esc_x'], p['esc_y']
                tgt_x, tgt_y = p['_tgt']
                waypoints = None
                for _bm in [15, 30, 50]:
                    waypoints = _astar(esc_x, esc_y, tgt_x, tgt_y, margin=_bm)
                    if waypoints is not None:
                        break
                if waypoints is None:
                    raise ValueError(f'No path found for branch from {p["ref"]}:{p["pin"]} to trunk. Try repositioning components to clear the path.')
                hit, loc = _path_hits_obstacle(waypoints)
                if hit:
                    raise ValueError(f'Wire from {p["ref"]}:{p["pin"]} would pass through a component at {loc}. Try repositioning components to clear the path.')
                _n, _ws = _place_path(waypoints)
                wire_count += _n
                _all_wires.extend(_ws)
                _add_waypoints_to_wire_sets(waypoints)

        # Place pin-to-escape wires for all pins
        for p in pin_positions:
            px, py = p['raw_x'], p['raw_y']
            ex, ey = p['esc_x'], p['esc_y']
            if abs(px - ex) > 0.001 or abs(py - ey) > 0.001:
                _tw = sch.wiring.add_wire(Vector2.from_xy_mm(px, py), Vector2.from_xy_mm(ex, ey))
                _all_wires.append(_tw)
                wire_count += 1
                _add_waypoints_to_wire_sets([(px, py), (ex, ey)])

        junction_count = _place_needed_junctions(_all_wires)

    pin_info = [{'ref': p['ref'], 'pin': p['pin'], 'position': [p['raw_x'], p['raw_y']]} for p in pin_positions]
    all_rx = [p['raw_x'] for p in pin_positions]
    all_ry = [p['raw_y'] for p in pin_positions]
    wire_span = (max(all_rx) - min(all_rx)) + (max(all_ry) - min(all_ry))
    result = {
        'status': 'success',
        'source': 'ipc',
        'pins': pin_info,
        'wire_count': wire_count,
        'junction_count': junction_count
    }
    if wire_span > 50.0:
        result['warning'] = f'Long wire path ({wire_span:.1f}mm). Verify pins are on the correct sheet section.'

except Exception as e:
    result = {'status': 'error', 'message': str(e)}

print(json.dumps(result, indent=2))
)py";


// sch_connect_to_power: pin resolution.
// Expects `pin_specs` list (pin-only, no labels). Opens try: block.
static const char* CONNECT_TO_POWER_RESOLVE = R"py(
try:
    # Phase 1: Resolve pin positions and escape directions
    pin_positions = []
    for name, pin_id in pin_specs:
)py";


// sch_connect_to_power: power symbol placement and auto-routed wiring.
// Expects `power_name`, `pwr_offset`, `force_angle` to be set.
// Continues inside try: block. Closes with except handler.
static const char* CONNECT_TO_POWER_ROUTING = R"py(
    import math

    # Build map of used #PWR references for auto-numbering
    used_refs = {}
    for _s in sch.symbols.get_all():
        _r = getattr(_s, 'reference', '')
        _m = re.match(r'^([A-Za-z#]+)(\d+)$', _r)
        if _m:
            used_refs.setdefault(_m.group(1), set()).add(int(_m.group(2)))

    def next_ref(prefix):
        nums = used_refs.get(prefix, set())
        n = 1
        while n in nums:
            n += 1
        used_refs.setdefault(prefix, set()).add(n)
        return f'{prefix}{n}'

    # --- Overlap detection for placed power symbols ---
    _BBOX_MARGIN = 1.0
    placed_bboxes = []
    try:
        for _esym in sch.symbols.get_all():
            try:
                _ebb = sch.transform.get_bounding_box(_esym, units='mm', include_text=False)
            except:
                continue
            if _ebb:
                placed_bboxes.append({'ref': getattr(_esym, 'reference', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})
    except:
        pass
    try:
        for _elbl in sch.labels.get_all():
            try:
                _ebb = sch.transform.get_bounding_box(_elbl, units='mm')
            except:
                continue
            if _ebb:
                placed_bboxes.append({'ref': getattr(_elbl, 'text', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})
    except:
        pass

    def _bboxes_overlap(a, b):
        return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']

    def _find_crossing_wire(bbox):
        """Return AABB dict of the first wire segment crossing bbox, or None."""
        for _w in sch.crud.get_wires():
            _sx, _sy = round(_w.start.x/1e6, 2), round(_w.start.y/1e6, 2)
            _ex, _ey = round(_w.end.x/1e6, 2), round(_w.end.y/1e6, 2)
            _wbb = {'min_x': min(_sx,_ex), 'max_x': max(_sx,_ex), 'min_y': min(_sy,_ey), 'max_y': max(_sy,_ey)}
            if _wbb['min_x'] == _wbb['max_x']:
                _wbb['min_x'] -= 0.01
                _wbb['max_x'] += 0.01
            if _wbb['min_y'] == _wbb['max_y']:
                _wbb['min_y'] -= 0.01
                _wbb['max_y'] += 0.01
            if _wbb['max_x'] > bbox['min_x'] and _wbb['min_x'] < bbox['max_x'] and _wbb['max_y'] > bbox['min_y'] and _wbb['min_y'] < bbox['max_y']:
                return _wbb
        return None

    def _overlap_info(new_bb):
        """Find first overlap and return descriptive string with overlap amounts."""
        for _pb in placed_bboxes:
            if _bboxes_overlap(new_bb, _pb):
                ox = min(new_bb['max_x'], _pb['max_x']) - max(new_bb['min_x'], _pb['min_x'])
                oy = min(new_bb['max_y'], _pb['max_y']) - max(new_bb['min_y'], _pb['min_y'])
                ref = _pb.get('ref', '?')
                return f"Overlaps '{ref}' by {ox:.1f}mm horizontal, {oy:.1f}mm vertical"
        _wcross = _find_crossing_wire(new_bb)
        if _wcross:
            return 'Overlaps a wire segment'
        return 'Overlaps existing element(s)'

    _SLIDE_GRID = 1.27
    _SLIDE_MAX_ITER = 5
    _SLIDE_MAX_MM = 30.0

    def _snap_slide(v, grid=_SLIDE_GRID):
        return round(round(v / grid) * grid, 4)

    def _slide_off(item, raw_bb, margined_bb):
        """Slide item to clear position via repulsion. Returns (ok, dx, dy)."""
        total_dx, total_dy = 0.0, 0.0
        r_bb = dict(raw_bb)
        m_bb = dict(margined_bb)
        for _iter in range(_SLIDE_MAX_ITER):
            obstacle = None
            use_margin = True
            for _pb in placed_bboxes:
                if _bboxes_overlap(m_bb, _pb):
                    obstacle = _pb
                    break
            if obstacle is None:
                _wcross = _find_crossing_wire(r_bb)
                if _wcross:
                    obstacle = _wcross
                    use_margin = False
            if obstacle is None:
                return (True, total_dx, total_dy)
            comp_cx = (r_bb['min_x'] + r_bb['max_x']) / 2
            comp_cy = (r_bb['min_y'] + r_bb['max_y']) / 2
            obs_cx = (obstacle['min_x'] + obstacle['max_x']) / 2
            obs_cy = (obstacle['min_y'] + obstacle['max_y']) / 2
            dir_x = comp_cx - obs_cx
            dir_y = comp_cy - obs_cy
            mag = math.sqrt(dir_x * dir_x + dir_y * dir_y)
            if mag < 1e-6:
                dir_x, dir_y, mag = 1.0, 0.0, 1.0
            dir_x /= mag
            dir_y /= mag
            if use_margin:
                hw_c = (m_bb['max_x'] - m_bb['min_x']) / 2
                hh_c = (m_bb['max_y'] - m_bb['min_y']) / 2
            else:
                hw_c = (r_bb['max_x'] - r_bb['min_x']) / 2
                hh_c = (r_bb['max_y'] - r_bb['min_y']) / 2
            hw_o = (obstacle['max_x'] - obstacle['min_x']) / 2
            hh_o = (obstacle['max_y'] - obstacle['min_y']) / 2
            candidates = []
            if abs(dir_x) > 1e-9:
                candidates.append((hw_c + hw_o) / abs(dir_x))
            if abs(dir_y) > 1e-9:
                candidates.append((hh_c + hh_o) / abs(dir_y))
            if not candidates:
                return (False, total_dx, total_dy)
            t = min(candidates)
            new_cx = obs_cx + dir_x * t
            new_cy = obs_cy + dir_y * t
            dx = _snap_slide(new_cx - comp_cx)
            dy = _snap_slide(new_cy - comp_cy)
            if abs(dx) < 1e-6 and abs(dy) < 1e-6:
                if abs(dir_x) >= abs(dir_y):
                    dx = _SLIDE_GRID if dir_x >= 0 else -_SLIDE_GRID
                else:
                    dy = _SLIDE_GRID if dir_y >= 0 else -_SLIDE_GRID
            total_dx += dx
            total_dy += dy
            if abs(total_dx) > _SLIDE_MAX_MM or abs(total_dy) > _SLIDE_MAX_MM:
                return (False, total_dx, total_dy)
            r_bb = {'min_x': r_bb['min_x']+dx, 'max_x': r_bb['max_x']+dx, 'min_y': r_bb['min_y']+dy, 'max_y': r_bb['max_y']+dy}
            m_bb = {'min_x': m_bb['min_x']+dx, 'max_x': m_bb['max_x']+dx, 'min_y': m_bb['min_y']+dy, 'max_y': m_bb['max_y']+dy}
        return (False, total_dx, total_dy)

    is_gnd = 'gnd' in power_name.lower() or 'vss' in power_name.lower()

    placed_powers = []
    _all_wires = []

    for p in pin_positions:
        px, py = p['raw_x'], p['raw_y']
        esc_dx, esc_dy = p['out_dx'], p['out_dy']

        # Normalize escape direction to unit vector
        esc_len = (esc_dx**2 + esc_dy**2)**0.5
        if esc_len > 0.001:
            unit_dx = esc_dx / esc_len
            unit_dy = esc_dy / esc_len
        else:
            unit_dx, unit_dy = 0, -1  # Default: place above

        # Compute power symbol position
        power_x = snap_to_grid(px + unit_dx * pwr_offset)
        power_y = snap_to_grid(py + unit_dy * pwr_offset)

        # Auto-rotate power symbol based on pin escape direction.
        # GND at 0deg: stem faces UP.  VCC at 0deg: stem faces DOWN.
        # We orient the stem to face the incoming wire (from pin side).
        if force_angle is not None:
            power_angle = force_angle
        elif abs(pwr_offset) < 0.01:
            power_angle = 0  # No wire, use default orientation
        elif abs(unit_dy) > abs(unit_dx):
            # Vertical escape (up or down)
            if is_gnd:
                power_angle = 0 if unit_dy > 0 else 180
            else:
                power_angle = 180 if unit_dy > 0 else 0
        else:
            # Horizontal escape (left or right)
            if is_gnd:
                power_angle = 90 if unit_dx > 0 else 270
            else:
                power_angle = 270 if unit_dx > 0 else 90

        print(f'[power] {p["ref"]}:{p["pin"]} -> {power_name} at ({power_x:.2f},{power_y:.2f}) angle={power_angle}', file=sys.stderr)

        # Place the power symbol
        power_pos = Vector2.from_xy_mm(power_x, power_y)
        power_sym = sch.labels.add_power(power_name, power_pos, angle=power_angle)

        # --- Overlap detection + slide-off ---
        _shifted = False
        _rejected = False
        try:
            _bb = sch.transform.get_bounding_box(power_sym, units='mm', include_text=False)
            if _bb:
                _raw_bb = dict(_bb)
                _margined_bb = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                _has_conflict = any(_bboxes_overlap(_margined_bb, _pb) for _pb in placed_bboxes) or (_find_crossing_wire(_raw_bb) is not None)
                if _has_conflict:
                    _sok, _sdx, _sdy = _slide_off(power_sym, _raw_bb, _margined_bb)
                    if _sok and (abs(_sdx) > 1e-6 or abs(_sdy) > 1e-6):
                        sch.transform.move(power_sym, delta_x_mm=_sdx, delta_y_mm=_sdy)
                        power_x = snap_to_grid(power_x + _sdx)
                        power_y = snap_to_grid(power_y + _sdy)
                        _bb = sch.transform.get_bounding_box(power_sym, units='mm', include_text=False)
                        if _bb:
                            _margined_bb = {'min_x': _bb['min_x'] - _BBOX_MARGIN, 'max_x': _bb['max_x'] + _BBOX_MARGIN, 'min_y': _bb['min_y'] - _BBOX_MARGIN, 'max_y': _bb['max_y'] + _BBOX_MARGIN}
                        _shifted = True
                        print(f'[power] slid {power_name} by ({_sdx:.2f},{_sdy:.2f}) to ({power_x:.2f},{power_y:.2f})', file=sys.stderr)
                    elif not _sok:
                        _rejected = True
        except:
            pass

        if _rejected:
            sch.crud.remove_items([power_sym])
            raise ValueError(f'Power symbol {power_name} for {p["ref"]}:{p["pin"]}: {_overlap_info(_margined_bb)}. Could not auto-slide to clear position.')

        # Set #PWR reference
        _pwr_ref = next_ref('#PWR')
        for _f in power_sym._proto.fields:
            if _f.name == 'Reference':
                _f.text = _pwr_ref
                break
        sch.crud.update_items(power_sym)

        # Track placed bbox for subsequent power symbols in the same batch
        if _bb:
            placed_bboxes.append({'ref': _pwr_ref, **_margined_bb})

        _pwr_result = {'ref': _pwr_ref, 'position': [power_x, power_y], 'angle': power_angle}
        if _shifted:
            _pwr_result['shifted'] = True
        placed_powers.append(_pwr_result)

        # Route wire from pin to power symbol using A*
        if abs(pwr_offset) >= 0.01 or _shifted:
            sd = _dir_index(esc_dx, esc_dy)
            # end_dir: wire approaches power symbol from opposite of escape direction
            ed = sd ^ 1 if sd >= 0 else -1
            print(f'[power] routing {p["ref"]}:{p["pin"]} -> power at ({power_x:.2f},{power_y:.2f})', file=sys.stderr)
            wp = None
            for a_margin in [15, 30, 50]:
                wp = _astar(px, py, power_x, power_y, start_dir=sd, end_dir=ed, margin=a_margin)
                if wp is not None:
                    break
            if wp is None:
                raise ValueError(f'No path found from {p["ref"]}:{p["pin"]} to {power_name}. Try repositioning components or increasing offset.')
            hit, loc = _path_hits_obstacle(wp)
            if hit:
                raise ValueError(f'Wire from {p["ref"]}:{p["pin"]} to {power_name} would pass through a component at {loc}. Try repositioning components.')
            _n, _ws = _place_path(wp)
            wire_count += _n
            _all_wires.extend(_ws)
            _add_waypoints_to_wire_sets(wp)

    junction_count = _place_needed_junctions(_all_wires)

    result = {
        'status': 'success',
        'source': 'ipc',
        'power': power_name,
        'connections': len(pin_positions),
        'wire_count': wire_count,
        'junction_count': junction_count,
        'placed_powers': placed_powers
    }

except Exception as e:
    result = {'status': 'error', 'message': str(e)}

print(json.dumps(result, indent=2))
)py";


std::string SCH_CONNECT_NET_HANDLER::GenerateConnectNetCode( const nlohmann::json& aInput ) const
{
    if( !aInput.contains( "pins" ) || !aInput["pins"].is_array() ||
        aInput["pins"].size() < 2 )
    {
        return "import json\n"
               "print(json.dumps({'status': 'error', 'message': "
               "'pins array with at least 2 entries is required'}))\n";
    }

    auto pins = aInput["pins"];

    // Parse pin/label specifiers at C++ time.
    // With colon  → pin spec:   ('pin',   'R1', '1')
    // Without colon → label spec: ('label', 'VCC', '')
    struct PinOrLabel { std::string type; std::string name; std::string pin; };
    std::vector<PinOrLabel> pinSpecs;
    for( size_t i = 0; i < pins.size(); ++i )
    {
        std::string spec = pins[i].get<std::string>();
        size_t colonPos = spec.find( ':' );
        if( colonPos == std::string::npos )
            pinSpecs.push_back( { "label", spec, "" } );
        else
            pinSpecs.push_back( { "pin", spec.substr( 0, colonPos ), spec.substr( colonPos + 1 ) } );
    }

    std::string mode = aInput.value( "mode", "chain" );

    // Build pin_specs Python list and mode (only dynamic parts)
    std::ostringstream pinSpecCode;
    pinSpecCode << "pin_specs = [\n";
    for( const auto& ps : pinSpecs )
    {
        pinSpecCode << "    ('" << EscapePythonString( ps.type )
                    << "', '" << EscapePythonString( ps.name )
                    << "', '" << EscapePythonString( ps.pin ) << "'),\n";
    }
    pinSpecCode << "]\n";
    pinSpecCode << "routing_mode = '" << EscapePythonString( mode ) << "'\n";

    // Concatenate: preamble + dynamic pin_specs + resolve + infrastructure + routing
    return std::string( ROUTING_PREAMBLE ) + pinSpecCode.str()
           + std::string( CONNECT_NET_RESOLVE )
           + std::string( PIN_RESOLVE_CODE )
           + std::string( ROUTING_INFRASTRUCTURE )
           + std::string( CONNECT_NET_ROUTING );
}


std::string SCH_CONNECT_NET_HANDLER::GenerateConnectToPowerCode( const nlohmann::json& aInput ) const
{
    if( !aInput.contains( "pins" ) || !aInput["pins"].is_array() ||
        aInput["pins"].empty() )
    {
        return "import json\n"
               "print(json.dumps({'status': 'error', 'message': "
               "'pins array with at least 1 entry is required'}))\n";
    }

    std::string power = aInput.value( "power", "" );
    if( power.empty() )
    {
        return "import json\n"
               "print(json.dumps({'status': 'error', 'message': "
               "'power parameter is required'}))\n";
    }

    auto pins = aInput["pins"];
    double offset = aInput.value( "offset", 5.08 );
    bool hasAngle = aInput.contains( "angle" );
    double angle = aInput.value( "angle", 0.0 );

    // Parse pin specifiers (pin-only, no labels - all must have REF:PIN format)
    struct PinSpec { std::string ref; std::string pin; };
    std::vector<PinSpec> pinSpecs;
    for( size_t i = 0; i < pins.size(); ++i )
    {
        std::string spec = pins[i].get<std::string>();
        size_t colonPos = spec.find( ':' );
        if( colonPos == std::string::npos )
        {
            return "import json\n"
                   "print(json.dumps({'status': 'error', 'message': "
                   "'Pin specifier must be REF:PIN format (e.g. U1:VCC), got: "
                   + spec + "'}))\n";
        }
        pinSpecs.push_back( { spec.substr( 0, colonPos ), spec.substr( colonPos + 1 ) } );
    }

    // Build dynamic Python variables
    std::ostringstream code;
    code << "pin_specs = [\n";
    for( const auto& ps : pinSpecs )
    {
        code << "    ('" << EscapePythonString( ps.ref )
             << "', '" << EscapePythonString( ps.pin ) << "'),\n";
    }
    code << "]\n";
    code << "power_name = '" << EscapePythonString( power ) << "'\n";
    code << "pwr_offset = " << offset << "\n";
    if( hasAngle )
        code << "force_angle = " << angle << "\n";
    else
        code << "force_angle = None\n";

    // Concatenate: preamble + dynamic vars + resolve + infrastructure + power routing
    return std::string( ROUTING_PREAMBLE ) + code.str()
           + std::string( CONNECT_TO_POWER_RESOLVE )
           + std::string( PIN_RESOLVE_CODE )
           + std::string( ROUTING_INFRASTRUCTURE )
           + std::string( CONNECT_TO_POWER_ROUTING );
}
