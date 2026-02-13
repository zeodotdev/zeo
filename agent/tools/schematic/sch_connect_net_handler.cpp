#include "sch_connect_net_handler.h"
#include <sstream>


bool SCH_CONNECT_NET_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_connect_net";
}


std::string SCH_CONNECT_NET_HANDLER::Execute( const std::string& aToolName,
                                               const nlohmann::json& aInput )
{
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_CONNECT_NET_HANDLER::GetDescription( const std::string& aToolName,
                                                      const nlohmann::json& aInput ) const
{
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

// Preamble: imports, document refresh, snap helper
static const char* CONNECT_NET_PREAMBLE = R"py(import json, sys
from kipy.geometry import Vector2

# Refresh document to handle close/reopen cycles
if hasattr(sch, 'refresh_document'):
    if not sch.refresh_document():
        raise RuntimeError('Schematic editor not open or document not available')

def snap_to_grid(val, grid=1.27):
    return round(val / grid) * grid

)py";

// Body: pin resolution, routing algorithm, wire/junction placement.
// Expects `pin_specs` list to be defined before this code runs.
static const char* CONNECT_NET_BODY = R"py(
try:
    # Phase 1: Resolve pin positions
    pin_positions = []
    _sym_bbox = {}  # Cache pin bounding box per symbol
    for ref, pin_id in pin_specs:
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
        else:
            px = pin_result['position'].x / 1_000_000
            py = pin_result['position'].y / 1_000_000
        # Determine pin direction from edge proximity on symbol pin bbox
        if ref not in _sym_bbox:
            bpxs, bpys = [], []
            for sp in sym.pins:
                try:
                    tp = sch.symbols.get_transformed_pin_position(sym, sp.number)
                    if tp:
                        bpxs.append(tp['position'].x / 1_000_000)
                        bpys.append(tp['position'].y / 1_000_000)
                except:
                    pass
            _sym_bbox[ref] = (min(bpxs), max(bpxs), min(bpys), max(bpys)) if len(bpxs) >= 2 else None
        bbox = _sym_bbox[ref]
        if bbox:
            bmin_x, bmax_x, bmin_y, bmax_y = bbox
            d_lr = min(abs(snap_to_grid(px) - bmin_x), abs(snap_to_grid(px) - bmax_x))
            d_tb = min(abs(snap_to_grid(py) - bmin_y), abs(snap_to_grid(py) - bmax_y))
            if d_lr < d_tb:
                pin_dir = 'h'
            elif d_tb < d_lr:
                pin_dir = 'v'
            else:
                # Tie: use bbox aspect ratio
                pin_dir = 'v' if (bmax_y - bmin_y) >= (bmax_x - bmin_x) else 'h'
        else:
            sym_cx = sym.position.x / 1_000_000
            sym_cy = sym.position.y / 1_000_000
            pin_dir = 'v' if abs(py - sym_cy) >= abs(px - sym_cx) else 'h'
        # Compute outward escape direction (away from symbol center)
        sym_cx = sym.position.x / 1_000_000
        sym_cy = sym.position.y / 1_000_000
        if pin_dir == 'h':
            out_dx = 1.27 if px > sym_cx else -1.27
            out_dy = 0
        else:
            out_dx = 0
            out_dy = 1.27 if py > sym_cy else -1.27
        pin_positions.append({'ref': ref, 'pin': pin_id, 'x': snap_to_grid(px), 'y': snap_to_grid(py), 'raw_x': px, 'raw_y': py, 'sym': sym, 'dir': pin_dir, 'out_dx': out_dx, 'out_dy': out_dy})

    wire_count = 0
    junction_count = 0

    _wire_ep = {}
    def _track(x, y):
        key = (round(x, 2), round(y, 2))
        _wire_ep[key] = _wire_ep.get(key, 0) + 1

    # Build obstacle map from graphical bounding boxes of ALL symbols.
    # Shrink bbox edges that have pins so wires can reach pin tips
    # without the body registering as an obstacle.
    obstacles = []
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
            # Get pin positions to detect which edges have pins
            pin_xs, pin_ys = [], []
            for sp in obs_sym.pins:
                try:
                    tp = sch.symbols.get_transformed_pin_position(obs_sym, sp.number)
                    if tp:
                        pin_xs.append(tp['position'].x / 1_000_000)
                        pin_ys.append(tp['position'].y / 1_000_000)
                except:
                    pass
            # Shrink each edge that has a pin within 2mm of it
            shrink = 1.27
            if any(abs(px - bx0) < 2.0 for px in pin_xs):
                bx0 += shrink
            if any(abs(px - bx1) < 2.0 for px in pin_xs):
                bx1 -= shrink
            if any(abs(py - by0) < 2.0 for py in pin_ys):
                by0 += shrink
            if any(abs(py - by1) < 2.0 for py in pin_ys):
                by1 -= shrink
            if bx0 < bx1 and by0 < by1:
                obstacles.append({'min_x': bx0, 'max_x': bx1, 'min_y': by0, 'max_y': by1})
    except:
        pass

    def _cell_blocked(cx, cy, grid=1.27):
        """Check if a grid cell center is inside any obstacle."""
        half = grid / 2 - 0.01
        for obs in obstacles:
            if cx + half > obs['min_x'] and cx - half < obs['max_x'] and cy + half > obs['min_y'] and cy - half < obs['max_y']:
                return True
        return False

    import heapq
    def _astar(x0, y0, x1, y1, grid=1.27, bend_cost=3, start_dir=-1):
        """A* pathfinding on the schematic grid. Returns list of (x,y) waypoints.
        start_dir: if >= 0, preferred initial direction (0=right, 1=left, 2=down, 3=up).
        Non-preferred first-step directions incur bend_cost penalty."""
        # Snap start/end to grid
        gx0, gy0 = round(x0 / grid), round(y0 / grid)
        gx1, gy1 = round(x1 / grid), round(y1 / grid)
        if gx0 == gx1 and gy0 == gy1:
            return [(x0, y0), (x1, y1)]
        # Search bounds: bounding box of start/end + generous margin
        margin = 15
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
                move_cost = 1
                if prev_d >= 0 and di != prev_d:
                    move_cost += bend_cost
                new_g = g + move_cost
                nkey = (nx, ny, di)
                if new_g < g_scores.get(nkey, float('inf')):
                    g_scores[nkey] = new_g
                    h = abs(gx1 - nx) + abs(gy1 - ny)
                    heapq.heappush(open_set, (new_g + h, new_g, nx, ny, di))
                    came_from[nkey] = current_key
        # No path found — fall back to direct L-wire
        return [(x0, y0), (x0, y1), (x1, y1)]

    def _place_path(waypoints):
        """Place wire segments along a list of waypoints."""
        count = 0
        for i in range(len(waypoints) - 1):
            ax, ay = waypoints[i]
            bx, by = waypoints[i + 1]
            sch.wiring.add_wire(Vector2.from_xy_mm(ax, ay), Vector2.from_xy_mm(bx, by))
            count += 1
        return count

    def _dir_index(dx, dy):
        """Convert (dx, dy) outward direction to A* direction index."""
        if dx > 0: return 0  # right
        if dx < 0: return 1  # left
        if dy > 0: return 2  # down
        if dy < 0: return 3  # up
        return -1

    def _route_pins(p0, p1):
        """Route between two pins using A* with forced start direction."""
        x0, y0 = p0['raw_x'], p0['raw_y']
        x1, y1 = p1['raw_x'], p1['raw_y']
        sd = _dir_index(p0['out_dx'], p0['out_dy'])
        return _astar(x0, y0, x1, y1, start_dir=sd)

    if routing_mode == 'chain':
        # Chain mode: A* route each consecutive pin pair
        for ci in range(len(pin_positions) - 1):
            p0, p1 = pin_positions[ci], pin_positions[ci + 1]
            waypoints = _route_pins(p0, p1)
            wire_count += _place_path(waypoints)

    elif len(pin_positions) == 2:
        # 2-pin star: A* route
        waypoints = _route_pins(pin_positions[0], pin_positions[1])
        wire_count = _place_path(waypoints)
    else:
        # 3+ pins: trunk-and-branch routing with obstacle avoidance
        xs = [p['x'] for p in pin_positions]
        ys = [p['y'] for p in pin_positions]
        x_spread = max(xs) - min(xs)
        y_spread = max(ys) - min(ys)
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
            return False

        # Choose trunk position (median of perpendicular coords)
        if is_horizontal:
            perp_coords = sorted(ys)
            trunk_min = snap_to_grid(min(xs))
            trunk_max = snap_to_grid(max(xs))
        else:
            perp_coords = sorted(xs)
            trunk_min = snap_to_grid(min(ys))
            trunk_max = snap_to_grid(max(ys))

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

        # Collect on-trunk pin projections (along trunk axis) to avoid junctions on pins
        _on_trunk_projs = set()
        for p in pin_positions:
            if is_horizontal:
                if abs(p['y'] - trunk_perp) <= 0.01:
                    _on_trunk_projs.add(round(snap_to_grid(p['x']), 4))
            else:
                if abs(p['x'] - trunk_perp) <= 0.01:
                    _on_trunk_projs.add(round(snap_to_grid(p['y']), 4))

        # Compute trunk midpoint for offset direction
        _all_projs = [snap_to_grid(p['x'] if is_horizontal else p['y']) for p in pin_positions]
        _proj_mid = (min(_all_projs) + max(_all_projs)) / 2

        # Count off-trunk pins at each endpoint to decide offset
        _ep_counts = {}
        _on_trunk_at = set()  # Track which endpoints have on-trunk pins
        for p in pin_positions:
            if is_horizontal:
                proj = snap_to_grid(p['x'])
                off = abs(p['y'] - trunk_perp) > 0.01
                at_ep = abs(proj - trunk_min) < 0.01 or abs(proj - trunk_max) < 0.01
            else:
                proj = snap_to_grid(p['y'])
                off = abs(p['x'] - trunk_perp) > 0.01
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

        # Pre-compute branch connection points on the trunk
        trunk_conn = []
        for p in pin_positions:
            if is_horizontal:
                proj = snap_to_grid(p['x'])
                off = abs(p['y'] - trunk_perp) > 0.01
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
                proj = snap_to_grid(p['y'])
                off = abs(p['x'] - trunk_perp) > 0.01
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

        # Place trunk wire
        if trunk_min != trunk_max:
            if is_horizontal:
                t0, t1 = (trunk_min, trunk_perp), (trunk_max, trunk_perp)
            else:
                t0, t1 = (trunk_perp, trunk_min), (trunk_perp, trunk_max)
            sch.wiring.add_wire(Vector2.from_xy_mm(*t0), Vector2.from_xy_mm(*t1))
            _track(t0[0], t0[1])
            _track(t1[0], t1[1])
            wire_count += 1

        # Place branches using A* with preferred start direction
        for p in pin_positions:
            if is_horizontal:
                off_trunk = abs(p['y'] - trunk_perp) > 0.01
            else:
                off_trunk = abs(p['x'] - trunk_perp) > 0.01

            if off_trunk:
                pin_x, pin_y = p['raw_x'], p['raw_y']
                tgt_x, tgt_y = p['_tgt']
                sd = _dir_index(p['out_dx'], p['out_dy'])
                waypoints = _astar(pin_x, pin_y, tgt_x, tgt_y, start_dir=sd)
                w = _place_path(waypoints)
                wire_count += w
                # Track endpoints for junction detection
                _track(pin_x, pin_y)
                _track(tgt_x, tgt_y)

        # Count pins as branches at their positions
        for p in pin_positions:
            _track(p['raw_x'], p['raw_y'])

        # Account for trunk passing through interior points
        for key in list(_wire_ep.keys()):
            kx, ky = key
            if is_horizontal:
                if abs(ky - trunk_perp) < 0.01 and kx > trunk_min + 0.01 and kx < trunk_max - 0.01:
                    _wire_ep[key] += 2
            else:
                if abs(kx - trunk_perp) < 0.01 and ky > trunk_min + 0.01 and ky < trunk_max - 0.01:
                    _wire_ep[key] += 2

        # Place junctions where 3+ branches meet
        _junctions_done = set()
        for key, count in _wire_ep.items():
            if count >= 3 and key not in _junctions_done:
                sch.wiring.add_junction(Vector2.from_xy_mm(key[0], key[1]))
                junction_count += 1
                _junctions_done.add(key)

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

    // Parse and validate pin specifiers at C++ time
    std::vector<std::pair<std::string, std::string>> pinSpecs;
    for( size_t i = 0; i < pins.size(); ++i )
    {
        std::string spec = pins[i].get<std::string>();
        size_t colonPos = spec.find( ':' );
        if( colonPos == std::string::npos )
        {
            return "import json\n"
                   "print(json.dumps({'status': 'error', 'message': "
                   "'Invalid pin specifier (missing colon): "
                   + EscapePythonString( spec ) + "'}))\n";
        }
        pinSpecs.emplace_back( spec.substr( 0, colonPos ), spec.substr( colonPos + 1 ) );
    }

    std::string mode = aInput.value( "mode", "chain" );

    // Build pin_specs Python list and mode (only dynamic parts)
    std::ostringstream pinSpecCode;
    pinSpecCode << "pin_specs = [\n";
    for( const auto& [ref, pin] : pinSpecs )
    {
        pinSpecCode << "    ('" << EscapePythonString( ref )
                    << "', '" << EscapePythonString( pin ) << "'),\n";
    }
    pinSpecCode << "]\n";
    pinSpecCode << "routing_mode = '" << EscapePythonString( mode ) << "'\n";

    // Concatenate: preamble + dynamic pin_specs + static body
    return std::string( CONNECT_NET_PREAMBLE ) + pinSpecCode.str()
           + std::string( CONNECT_NET_BODY );
}
