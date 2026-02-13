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
        pin_positions.append({'ref': ref, 'pin': pin_id, 'x': snap_to_grid(px), 'y': snap_to_grid(py), 'raw_x': px, 'raw_y': py, 'sym': sym, 'dir': pin_dir})

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
                bbox = sch.transform.get_bounding_box(obs_sym, units='mm')
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

    def _seg_hits_obs(sx0, sy0, sx1, sy1):
        smin_x, smax_x = min(sx0, sx1), max(sx0, sx1)
        smin_y, smax_y = min(sy0, sy1), max(sy0, sy1)
        for obs in obstacles:
            if smax_x > obs['min_x'] and smin_x < obs['max_x'] and smax_y > obs['min_y'] and smin_y < obs['max_y']:
                return True
        return False

    if routing_mode == 'chain':
        # Chain mode: wire pins sequentially as 2-pin pairs
        for ci in range(len(pin_positions) - 1):
            p0, p1 = pin_positions[ci], pin_positions[ci + 1]
            x0, y0 = p0['raw_x'], p0['raw_y']
            x1, y1 = p1['raw_x'], p1['raw_y']
            if abs(x0 - x1) < 0.01 or abs(y0 - y1) < 0.01:
                # Collinear: straight wire
                sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
                wire_count += 1
            else:
                # L-shaped: pick bend direction from pin directions
                pin0_vertical = p0['dir'] == 'v'
                pin1_vertical = p1['dir'] == 'v'
                if pin0_vertical:
                    # Extend p0 vertically, then horizontal to p1
                    cx, cy = x0, y1
                elif pin1_vertical:
                    # Extend p1 vertically, then horizontal from p0
                    cx, cy = x1, y0
                else:
                    # Both horizontal: default vertical-first
                    cx, cy = x0, y1
                sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(cx, cy))
                sch.wiring.add_wire(Vector2.from_xy_mm(cx, cy), Vector2.from_xy_mm(x1, y1))
                wire_count += 2

    elif len(pin_positions) == 2:
        # 2-pin case: midpoint-bend routing
        p0, p1 = pin_positions[0], pin_positions[1]
        x0, y0 = p0['raw_x'], p0['raw_y']
        x1, y1 = p1['raw_x'], p1['raw_y']
        if abs(x0 - x1) < 0.01 or abs(y0 - y1) < 0.01:
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), Vector2.from_xy_mm(x1, y1))
            wire_count = 1
        else:
            # 3-segment step route -- first segment follows pin direction
            pin0_vertical = p0['dir'] == 'v'
            pin1_vertical = p1['dir'] == 'v'
            # If both agree, use that. If mixed, prefer vertical-first.
            extend_vertical = pin0_vertical or pin1_vertical
            if extend_vertical:
                # Vertical first: midpoint Y
                mid_y = snap_to_grid((y0 + y1) / 2)
                c0 = Vector2.from_xy_mm(x0, mid_y)
                c1 = Vector2.from_xy_mm(x1, mid_y)
            else:
                # Horizontal first: midpoint X
                mid_x = snap_to_grid((x0 + x1) / 2)
                c0 = Vector2.from_xy_mm(mid_x, y0)
                c1 = Vector2.from_xy_mm(mid_x, y1)
            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), c0)
            sch.wiring.add_wire(c0, c1)
            sch.wiring.add_wire(c1, Vector2.from_xy_mm(x1, y1))
            wire_count = 3
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
                    p['_tgt'] = (proj, trunk_perp)
                    trunk_conn.append(proj)
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
                    p['_tgt'] = (trunk_perp, proj)
                    trunk_conn.append(proj)
                else:
                    trunk_conn.append(proj)

        # Adjust trunk extent to actual connection points
        trunk_min = snap_to_grid(min(trunk_conn))
        trunk_max = snap_to_grid(max(trunk_conn))

        # Place trunk wire
        if trunk_min != trunk_max:
            if is_horizontal:
                sch.wiring.add_wire(Vector2.from_xy_mm(trunk_min, trunk_perp), Vector2.from_xy_mm(trunk_max, trunk_perp))
                _track(trunk_min, trunk_perp)
                _track(trunk_max, trunk_perp)
            else:
                sch.wiring.add_wire(Vector2.from_xy_mm(trunk_perp, trunk_min), Vector2.from_xy_mm(trunk_perp, trunk_max))
                _track(trunk_perp, trunk_min)
                _track(trunk_perp, trunk_max)
            wire_count += 1

        # Place branches
        for p in pin_positions:
            if is_horizontal:
                off_trunk = abs(p['y'] - trunk_perp) > 0.01
            else:
                off_trunk = abs(p['x'] - trunk_perp) > 0.01

            if off_trunk:
                pin_x, pin_y = p['raw_x'], p['raw_y']
                tgt_x, tgt_y = p['_tgt']
                if abs(pin_x - tgt_x) < 0.01 or abs(pin_y - tgt_y) < 0.01:
                    # Collinear: straight wire
                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), Vector2.from_xy_mm(tgt_x, tgt_y))
                    _track(pin_x, pin_y)
                    _track(tgt_x, tgt_y)
                    wire_count += 1
                elif p.get('_ep'):
                    # Endpoint branch: L-bend following pin direction
                    if p['dir'] == 'h':
                        cx, cy = tgt_x, pin_y
                    else:
                        cx, cy = pin_x, tgt_y
                    corner = Vector2.from_xy_mm(cx, cy)
                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), corner)
                    sch.wiring.add_wire(corner, Vector2.from_xy_mm(tgt_x, tgt_y))
                    _track(pin_x, pin_y)
                    _track(cx, cy)
                    _track(cx, cy)
                    _track(tgt_x, tgt_y)
                    wire_count += 2
                else:
                    # Interior branch: midpoint-bend step route
                    if p['dir'] == 'v':
                        mid = snap_to_grid((pin_y + tgt_y) / 2)
                        c0x, c0y = pin_x, mid
                        c1x, c1y = tgt_x, mid
                    else:
                        mid = snap_to_grid((pin_x + tgt_x) / 2)
                        c0x, c0y = mid, pin_y
                        c1x, c1y = mid, tgt_y
                    c0 = Vector2.from_xy_mm(c0x, c0y)
                    c1 = Vector2.from_xy_mm(c1x, c1y)
                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), c0)
                    sch.wiring.add_wire(c0, c1)
                    sch.wiring.add_wire(c1, Vector2.from_xy_mm(tgt_x, tgt_y))
                    _track(pin_x, pin_y)
                    _track(c0x, c0y)
                    _track(c0x, c0y)
                    _track(c1x, c1y)
                    _track(c1x, c1y)
                    _track(tgt_x, tgt_y)
                    wire_count += 3

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
