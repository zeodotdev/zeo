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
// Used inline within sch_connect_net pin resolution.
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


// Phase 1.5: Connectivity pre-check.
// After pin resolution, queries each pin's net membership to detect pins
// that are already connected.  Groups them by shared net, and if some are
// already wired together, finds wire tap points on existing wires and
// replaces multi-pin groups with a single tap entry so the router only
// draws the minimum new wires (with T-junctions on wires, not at pins).
static const char* CONNECT_NET_PRECHECK = R"py(
    # Phase 1.5: Connectivity pre-check — avoid drawing wires on existing nets.
    # Query each pin's net membership and group pins sharing the same net.
    _pin_nets = []
    _n_components = len(pin_positions)
    _original_pin_count = len(pin_positions)
    for _pi, p in enumerate(pin_positions):
        _net = None
        try:
            if p.get('pin'):  # pin spec (not label)
                _sym = sch.symbols.get_by_ref(p['ref'])
                if _sym:
                    _net = sch.labels.get_pin_net(_sym, p['pin'])
                    if _net and 'unconnected' in _net.lower():
                        _net = None
            else:
                # Label spec: the label text itself is the net name
                _net = p.get('ref')
        except Exception:
            pass
        _pin_nets.append(_net)
        print(f'[route] pre-check {p["ref"]}:{p.get("pin","")} -> net={_net}', file=sys.stderr)

    # Assign each pin to a connected-component group.
    # Pins that share a net name are in the same group; unknown pins get their own.
    _net_to_group = {}
    _pin_group = []
    _next_group = 0
    for _pi, _net in enumerate(_pin_nets):
        if _net is not None and _net in _net_to_group:
            _pin_group.append(_net_to_group[_net])
        else:
            _pin_group.append(_next_group)
            if _net is not None:
                _net_to_group[_net] = _next_group
            _next_group += 1

    _n_components = len(set(_pin_group))
    print(f'[route] pre-check: {_n_components} disconnected component(s) among {len(pin_positions)} pins', file=sys.stderr)

    if _n_components < len(pin_positions) and _n_components > 1:
        # Some pins already connected — replace each multi-pin group with a
        # wire tap point on the group's existing wires.
        _groups = {}
        _group_nets = {}
        for _pi, _gi in enumerate(_pin_group):
            _groups.setdefault(_gi, []).append(_pi)
            if _pin_nets[_pi] is not None:
                _group_nets[_gi] = _pin_nets[_pi]

        # Collect wire geometry for each existing net
        _all_wires_list = sch.wiring.get_wires()
        _wire_by_id = {}
        for _w in _all_wires_list:
            if hasattr(_w, 'id') and hasattr(_w.id, 'value'):
                _wire_by_id[_w.id.value] = _w
        _net_wire_segs = {}  # net_name -> [(sx, sy, ex, ey), ...]
        _grid = 1.27
        for _net_name in set(_group_nets.values()):
            try:
                _ni = sch.connectivity.get_net_items(_net_name)
                _segs = []
                for _iid in _ni.item_ids:
                    if _iid.value in _wire_by_id:
                        _w = _wire_by_id[_iid.value]
                        _segs.append((_w.start.x / 1_000_000, _w.start.y / 1_000_000,
                                      _w.end.x / 1_000_000, _w.end.y / 1_000_000))
                _net_wire_segs[_net_name] = _segs
                print(f'[route] pre-check: net {_net_name} has {len(_segs)} wire segment(s)', file=sys.stderr)
            except Exception as _e:
                print(f'[route] pre-check: failed to get wires for {_net_name}: {_e}', file=sys.stderr)
                _net_wire_segs[_net_name] = []

        # Build set of resolved pin grid cells to avoid tapping at a pin
        _pin_avoid = set()
        for p in pin_positions:
            _pin_avoid.add((round(p['raw_x'] / _grid), round(p['raw_y'] / _grid)))

        def _find_wire_tap(wires, target_x, target_y, avoid_cells, grid=1.27):
            """Find closest grid-snapped point on any wire to (target_x, target_y),
            skipping grid cells in avoid_cells. Returns (tap_x, tap_y, wire_dir) or None."""
            best_dist = float('inf')
            best = None
            for sx, sy, ex, ey in wires:
                is_h = abs(sy - ey) < 0.01
                is_v = abs(sx - ex) < 0.01
                if not is_h and not is_v:
                    continue
                if is_h:
                    cx = max(min(target_x, max(sx, ex)), min(sx, ex))
                    cx = round(cx / grid) * grid
                    cx = max(min(cx, max(sx, ex)), min(sx, ex))
                    cy = round(sy / grid) * grid
                    wdir = 'h'
                else:
                    cx = round(sx / grid) * grid
                    cy = max(min(target_y, max(sy, ey)), min(sy, ey))
                    cy = round(cy / grid) * grid
                    cy = max(min(cy, max(sy, ey)), min(sy, ey))
                    wdir = 'v'
                gcell = (round(cx / grid), round(cy / grid))
                if gcell in avoid_cells:
                    # Try offsetting along the wire by 1-2 grid cells
                    found_alt = False
                    for off in [1, -1, 2, -2]:
                        if is_h:
                            alt = round((cx + off * grid) / grid) * grid
                            if min(sx, ex) <= alt <= max(sx, ex):
                                ag = (round(alt / grid), gcell[1])
                                if ag not in avoid_cells:
                                    cx = alt
                                    gcell = ag
                                    found_alt = True
                                    break
                        else:
                            alt = round((cy + off * grid) / grid) * grid
                            if min(sy, ey) <= alt <= max(sy, ey):
                                ag = (gcell[0], round(alt / grid))
                                if ag not in avoid_cells:
                                    cy = alt
                                    gcell = ag
                                    found_alt = True
                                    break
                    if not found_alt:
                        continue
                dist = abs(target_x - cx) + abs(target_y - cy)
                if dist < best_dist:
                    best_dist = dist
                    best = (cx, cy, wdir)
            return best

        # Compute centroid of all OTHER groups' pins for each group
        _all_cx = sum(p['raw_x'] for p in pin_positions) / len(pin_positions)
        _all_cy = sum(p['raw_y'] for p in pin_positions) / len(pin_positions)

        _new_positions = []
        for _gi in sorted(_groups.keys()):
            members = _groups[_gi]
            _gnet = _group_nets.get(_gi)
            if len(members) > 1 and _gnet and _gnet in _net_wire_segs and _net_wire_segs[_gnet]:
                # Multi-pin group with existing wires — find a wire tap point.
                # Target: centroid of all pins NOT in this group.
                _other = [pin_positions[i] for i in range(len(pin_positions)) if _pin_group[i] != _gi]
                if _other:
                    _tcx = sum(p['raw_x'] for p in _other) / len(_other)
                    _tcy = sum(p['raw_y'] for p in _other) / len(_other)
                else:
                    _tcx, _tcy = _all_cx, _all_cy
                tap = _find_wire_tap(_net_wire_segs[_gnet], _tcx, _tcy, _pin_avoid)
                if tap:
                    tx, ty, twdir = tap
                    # Escape direction: perpendicular to wire, toward target centroid
                    if twdir == 'h':
                        _esc_dy = _grid if _tcy > ty else -_grid
                        _tap_entry = {
                            'ref': _gnet, 'pin': '',
                            'x': snap_to_grid(tx), 'y': snap_to_grid(ty),
                            'raw_x': tx, 'raw_y': ty,
                            'esc_x': snap_to_grid(tx), 'esc_y': snap_to_grid(ty + _esc_dy),
                            'dir': 'v', 'out_dx': 0, 'out_dy': _esc_dy
                        }
                    else:
                        _esc_dx = _grid if _tcx > tx else -_grid
                        _tap_entry = {
                            'ref': _gnet, 'pin': '',
                            'x': snap_to_grid(tx), 'y': snap_to_grid(ty),
                            'raw_x': tx, 'raw_y': ty,
                            'esc_x': snap_to_grid(tx + _esc_dx), 'esc_y': snap_to_grid(ty),
                            'dir': 'h', 'out_dx': _esc_dx, 'out_dy': 0
                        }
                    _new_positions.append(_tap_entry)
                    print(f'[route] Pre-check: group {_gi} ({_gnet}, {len(members)} pins) -> wire tap at ({tx:.2f}, {ty:.2f})', file=sys.stderr)
                else:
                    # Fallback: pick closest member pin to other groups' centroid
                    best = min(members, key=lambda i: abs(pin_positions[i]['raw_x'] - _tcx) + abs(pin_positions[i]['raw_y'] - _tcy))
                    _new_positions.append(pin_positions[best])
                    print(f'[route] Pre-check: group {_gi} ({_gnet}) no tap found, using pin {pin_positions[best]["ref"]}:{pin_positions[best]["pin"]}', file=sys.stderr)
            else:
                # Single-pin group or no existing wires — keep the pin.
                if len(members) == 1:
                    _new_positions.append(pin_positions[members[0]])
                else:
                    # Multi-pin group but no wires found — pick closest to other centroid
                    _other = [pin_positions[i] for i in range(len(pin_positions)) if _pin_group[i] != _gi]
                    if _other:
                        _tcx = sum(p['raw_x'] for p in _other) / len(_other)
                        _tcy = sum(p['raw_y'] for p in _other) / len(_other)
                    else:
                        _tcx, _tcy = _all_cx, _all_cy
                    best = min(members, key=lambda i: abs(pin_positions[i]['raw_x'] - _tcx) + abs(pin_positions[i]['raw_y'] - _tcy))
                    _new_positions.append(pin_positions[best])

        _skipped = len(pin_positions) - len(_new_positions)
        print(f'[route] Pre-check: reduced {len(pin_positions)} pins to {len(_new_positions)} '
              f'targets (skipped {_skipped} already-connected)', file=sys.stderr)
        pin_positions = _new_positions

)py";


// Obstacle map building, A* pathfinder, and wire placement helpers.
// Used by sch_connect_net for all routing modes.
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
        for a_margin, a_label, a_xcost in [(15, 'default', 2), (30, 'wider', 2), (50, 'wide', 2), (100, 'max (relaxed)', 0)]:
            wp = _astar(x0, y0, x1, y1, start_dir=sd, end_dir=ed, margin=a_margin, cross_cost=a_xcost)
            if wp is not None:
                if a_label != 'default':
                    print(f'[route]   found path with retry: {a_label}', file=sys.stderr)
                break
            print(f'[route]   no path with {a_label}, retrying...', file=sys.stderr)
        if wp is None:
            raise ValueError(f'No path found from {p0["ref"]}:{p0["pin"]} to {p1["ref"]}:{p1["pin"]}. Try repositioning components to clear the path.')
        print(f'[route]   path: {[(round(x,2),round(y,2)) for x,y in wp]}', file=sys.stderr)
        return wp

    def _path_length(waypoints):
        return sum(abs(waypoints[i+1][0]-waypoints[i][0]) + abs(waypoints[i+1][1]-waypoints[i][1])
                   for i in range(len(waypoints)-1))

    def _resolve_pin_escape(sym, pin_id):
        """Re-resolve a pin's position and escape direction after symbol rotation."""
        pr = sch.symbols.get_transformed_pin_position(sym, pin_id)
        if not pr:
            return None
        px = pr['position'].x / 1_000_000
        py = pr['position'].y / 1_000_000
        po = pr.get('orientation', None)
        if po is not None:
            _r90 = {0: 2, 1: 3, 2: 1, 3: 0}
            for _ in range(round(getattr(sym, 'angle', 0) / 90) % 4):
                po = _r90.get(po, po)
        if   po == 1: odx, ody, pd = 1.27, 0, 'h'
        elif po == 0: odx, ody, pd = -1.27, 0, 'h'
        elif po == 2: odx, ody, pd = 0, 1.27, 'v'
        elif po == 3: odx, ody, pd = 0, -1.27, 'v'
        else:         odx, ody, pd = 1.27, 0, 'h'
        return {'x': snap_to_grid(px), 'y': snap_to_grid(py),
                'raw_x': px, 'raw_y': py,
                'esc_x': snap_to_grid(px + odx), 'esc_y': snap_to_grid(py + ody),
                'dir': pd, 'out_dx': odx, 'out_dy': ody}

    def _try_auto_flip_power(p0, p1, waypoints):
        """If one endpoint is a #PWR symbol, try flipping it 180 degrees and
        re-routing. Keep whichever orientation produces the shorter path.

        Power symbols are electrically symmetric so flipping is always safe.
        The cost is one extra A* run per power-symbol connection.
        Returns (waypoints, p0, p1) — possibly updated."""
        if len(waypoints) < 2:
            return waypoints, p0, p1

        # Which endpoint is a power symbol?
        flip_p, other_p, is_p1 = None, None, False
        if p1['ref'].startswith('#PWR'):
            flip_p, other_p, is_p1 = p1, p0, True
        elif p0['ref'].startswith('#PWR'):
            flip_p, other_p, is_p1 = p0, p1, False
        if not flip_p:
            return waypoints, p0, p1

        sym = sch.symbols.get_by_ref(flip_p['ref'])
        if not sym:
            return waypoints, p0, p1

        old_angle = round(getattr(sym, 'angle', 0))
        new_angle = old_angle + 180 if old_angle < 180 else old_angle - 180
        plen = _path_length(waypoints)
        print(f'[route] {flip_p["ref"]} is power symbol — trying 180° flip '
              f'to find shorter path (current {plen:.1f}mm)', file=sys.stderr)

        try:
            sch.symbols.set_angle(sym, new_angle)
            resolved = _resolve_pin_escape(sym, flip_p['pin'])
            if not resolved:
                sch.symbols.set_angle(sym, old_angle)
                return waypoints, p0, p1

            new_pin = {**flip_p, **resolved}
            np0, np1 = (new_pin, other_p) if not is_p1 else (other_p, new_pin)
            new_wp = _route_pins(np0, np1)
            new_plen = _path_length(new_wp)

            keep_flip = False
            if new_plen < plen:
                keep_flip = True
                print(f'[route] flip improved: {plen:.1f}mm -> {new_plen:.1f}mm', file=sys.stderr)
            elif abs(new_plen - plen) < 0.1 and new_angle == 0:
                keep_flip = True
                print(f'[route] equal path length — preferring conventional orientation ({new_angle}° over {old_angle}°)', file=sys.stderr)
            else:
                print(f'[route] flip did not help ({new_plen:.1f}mm >= {plen:.1f}mm), reverting', file=sys.stderr)

            # ── Body-overlap guard ──────────────────────────────────
            # Reject the flip if the wire enters the pin from the body
            # side (opposite of the escape direction).
            if keep_flip and len(new_wp) >= 2:
                if not is_p1:  # flipped pin is at start of path
                    _awx = new_wp[1][0] - new_wp[0][0]
                    _awy = new_wp[1][1] - new_wp[0][1]
                else:          # flipped pin is at end of path
                    _awx = new_wp[-2][0] - new_wp[-1][0]
                    _awy = new_wp[-2][1] - new_wp[-1][1]
                if _awx * new_pin['out_dx'] + _awy * new_pin['out_dy'] < 0:
                    keep_flip = False
                    print(f'[route] flip rejected: wire enters pin from body side', file=sys.stderr)

            if keep_flip:
                return new_wp, np0, np1
            else:
                sch.symbols.set_angle(sym, old_angle)
                return waypoints, p0, p1
        except Exception as _fe:
            print(f'[route] flip failed: {_fe}, reverting', file=sys.stderr)
            try:
                sch.symbols.set_angle(sym, old_angle)
            except:
                pass
            return waypoints, p0, p1

)py";


// sch_connect_net routing modes: chain and star topology.
// Continues inside try: block. Closes with except handler.
static const char* CONNECT_NET_ROUTING = R"py(
    print(f'[route] MODE={routing_mode} pins={len(pin_positions)}', file=sys.stderr)

    if _n_components == 1:
        # All pins already on the same net — no wiring needed
        print('[route] All pins already connected on same net — skipping routing', file=sys.stderr)

    elif routing_mode == 'chain':
        # Chain mode: A* route each consecutive pin pair
        _all_wires = []
        for ci in range(len(pin_positions) - 1):
            p0, p1 = pin_positions[ci], pin_positions[ci + 1]
            waypoints = _route_pins(p0, p1)
            waypoints, p0, p1 = _try_auto_flip_power(p0, p1, waypoints)
            pin_positions[ci], pin_positions[ci + 1] = p0, p1
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
        waypoints, p0, p1 = _try_auto_flip_power(p0, p1, waypoints)
        pin_positions[0], pin_positions[1] = p0, p1
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
    if _n_components == 1:
        result['message'] = 'All specified pins are already connected on the same net — no wiring needed.'
    elif _n_components < _original_pin_count:
        result['message'] = f'Skipped {_original_pin_count - _n_components} already-connected pin(s) — tapped into existing wire.'
    if wire_span > 50.0 and wire_count > 0:
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

    // Concatenate: preamble + dynamic pin_specs + resolve + precheck + infrastructure + routing
    return std::string( ROUTING_PREAMBLE ) + pinSpecCode.str()
           + std::string( CONNECT_NET_RESOLVE )
           + std::string( PIN_RESOLVE_CODE )
           + std::string( CONNECT_NET_PRECHECK )
           + std::string( ROUTING_INFRASTRUCTURE )
           + std::string( CONNECT_NET_ROUTING );
}
