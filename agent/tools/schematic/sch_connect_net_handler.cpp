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


std::string SCH_CONNECT_NET_HANDLER::GenerateRefreshPreamble() const
{
    return
        "# Refresh document to handle close/reopen cycles\n"
        "if hasattr(sch, 'refresh_document'):\n"
        "    if not sch.refresh_document():\n"
        "        raise RuntimeError('Schematic editor not open or document not available')\n";
}


std::string SCH_CONNECT_NET_HANDLER::GenerateConnectNetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "pins" ) || !aInput["pins"].is_array() ||
        aInput["pins"].size() < 2 )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': "
             << "'pins array with at least 2 entries is required'}))\n";
        return code.str();
    }

    auto pins = aInput["pins"];

    // Parse pin specifiers at C++ time, validate format
    std::vector<std::pair<std::string, std::string>> pinSpecs;
    for( size_t i = 0; i < pins.size(); ++i )
    {
        std::string spec = pins[i].get<std::string>();
        size_t colonPos = spec.find( ':' );
        if( colonPos == std::string::npos )
        {
            code << "import json\n";
            code << "print(json.dumps({'status': 'error', 'message': "
                 << "'Invalid pin specifier (missing colon): "
                 << EscapePythonString( spec ) << "'}))\n";
            return code.str();
        }
        pinSpecs.emplace_back( spec.substr( 0, colonPos ), spec.substr( colonPos + 1 ) );
    }

    // Preamble
    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";

    // Helper: snap to grid
    code << "def snap_to_grid(val, grid=1.27):\n";
    code << "    return round(val / grid) * grid\n";
    code << "\n";

    // Inject pin specifiers as Python literals
    code << "pin_specs = [\n";
    for( const auto& [ref, pin] : pinSpecs )
    {
        code << "    ('" << EscapePythonString( ref )
             << "', '" << EscapePythonString( pin ) << "'),\n";
    }
    code << "]\n\n";

    code << "try:\n";

    // Phase 1: Resolve all pin positions
    code << "    # Phase 1: Resolve pin positions\n";
    code << "    pin_positions = []\n";
    code << "    _sym_bbox = {}  # Cache pin bounding box per symbol\n";
    code << "    for ref, pin_id in pin_specs:\n";
    code << "        sym = sch.symbols.get_by_ref(ref)\n";
    code << "        if not sym:\n";
    code << "            raise ValueError(f'Symbol not found: {ref}')\n";
    code << "        pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)\n";
    code << "        if not pin_result:\n";
    code << "            pin_pos = sch.symbols.get_pin_position(sym, pin_id)\n";
    code << "            if not pin_pos:\n";
    code << "                raise ValueError(f'Pin not found: {pin_id} on {ref}')\n";
    code << "            px = pin_pos.x / 1_000_000\n";
    code << "            py = pin_pos.y / 1_000_000\n";
    code << "        else:\n";
    code << "            px = pin_result['position'].x / 1_000_000\n";
    code << "            py = pin_result['position'].y / 1_000_000\n";
    code << "        # Determine pin direction from edge proximity on symbol pin bbox\n";
    code << "        if ref not in _sym_bbox:\n";
    code << "            bpxs, bpys = [], []\n";
    code << "            for sp in sym.pins:\n";
    code << "                try:\n";
    code << "                    tp = sch.symbols.get_transformed_pin_position(sym, sp.number)\n";
    code << "                    if tp:\n";
    code << "                        bpxs.append(tp['position'].x / 1_000_000)\n";
    code << "                        bpys.append(tp['position'].y / 1_000_000)\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "            _sym_bbox[ref] = (min(bpxs), max(bpxs), min(bpys), max(bpys)) "
         << "if len(bpxs) >= 2 else None\n";
    code << "        bbox = _sym_bbox[ref]\n";
    code << "        if bbox:\n";
    code << "            bmin_x, bmax_x, bmin_y, bmax_y = bbox\n";
    code << "            d_lr = min(abs(snap_to_grid(px) - bmin_x), "
         << "abs(snap_to_grid(px) - bmax_x))\n";
    code << "            d_tb = min(abs(snap_to_grid(py) - bmin_y), "
         << "abs(snap_to_grid(py) - bmax_y))\n";
    code << "            if d_lr < d_tb:\n";
    code << "                pin_dir = 'h'\n";
    code << "            elif d_tb < d_lr:\n";
    code << "                pin_dir = 'v'\n";
    code << "            else:\n";
    code << "                # Tie: use bbox aspect ratio\n";
    code << "                pin_dir = 'v' if (bmax_y - bmin_y) >= (bmax_x - bmin_x) else 'h'\n";
    code << "        else:\n";
    code << "            sym_cx = sym.position.x / 1_000_000\n";
    code << "            sym_cy = sym.position.y / 1_000_000\n";
    code << "            pin_dir = 'v' if abs(py - sym_cy) >= abs(px - sym_cx) else 'h'\n";
    code << "        pin_positions.append({'ref': ref, 'pin': pin_id, "
         << "'x': snap_to_grid(px), 'y': snap_to_grid(py), 'sym': sym, "
         << "'dir': pin_dir})\n";
    code << "\n";

    code << "    wire_count = 0\n";
    code << "    junction_count = 0\n";
    code << "\n";

    // 2-pin case: midpoint-bend routing (avoids overlapping adjacent pins)
    code << "    if len(pin_positions) == 2:\n";
    code << "        p0, p1 = pin_positions[0], pin_positions[1]\n";
    code << "        x0, y0 = p0['x'], p0['y']\n";
    code << "        x1, y1 = p1['x'], p1['y']\n";
    code << "        if abs(x0 - x1) < 0.01 or abs(y0 - y1) < 0.01:\n";
    code << "            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), "
         << "Vector2.from_xy_mm(x1, y1))\n";
    code << "            wire_count = 1\n";
    code << "        else:\n";
    code << "            # 3-segment step route — first segment follows pin direction\n";
    code << "            pin0_vertical = p0['dir'] == 'v'\n";
    code << "            pin1_vertical = p1['dir'] == 'v'\n";
    code << "            # If both agree, use that. If mixed, prefer vertical-first.\n";
    code << "            extend_vertical = pin0_vertical or pin1_vertical\n";
    code << "            if extend_vertical:\n";
    code << "                # Vertical first: midpoint Y\n";
    code << "                mid_y = snap_to_grid((y0 + y1) / 2)\n";
    code << "                c0 = Vector2.from_xy_mm(x0, mid_y)\n";
    code << "                c1 = Vector2.from_xy_mm(x1, mid_y)\n";
    code << "            else:\n";
    code << "                # Horizontal first: midpoint X\n";
    code << "                mid_x = snap_to_grid((x0 + x1) / 2)\n";
    code << "                c0 = Vector2.from_xy_mm(mid_x, y0)\n";
    code << "                c1 = Vector2.from_xy_mm(mid_x, y1)\n";
    code << "            sch.wiring.add_wire(Vector2.from_xy_mm(x0, y0), c0)\n";
    code << "            sch.wiring.add_wire(c0, c1)\n";
    code << "            sch.wiring.add_wire(c1, Vector2.from_xy_mm(x1, y1))\n";
    code << "            wire_count = 3\n";
    code << "    else:\n";

    // 3+ pin case: trunk-and-branch with obstacle avoidance
    code << "        # 3+ pins: trunk-and-branch routing with obstacle avoidance\n";
    code << "        xs = [p['x'] for p in pin_positions]\n";
    code << "        ys = [p['y'] for p in pin_positions]\n";
    code << "        x_spread = max(xs) - min(xs)\n";
    code << "        y_spread = max(ys) - min(ys)\n";
    code << "        is_horizontal = x_spread >= y_spread\n";
    code << "\n";

    // Build obstacle map
    code << "        # Build obstacle map from all symbols not in this net\n";
    code << "        connected_refs = {p['ref'] for p in pin_positions}\n";
    code << "        obstacles = []\n";
    code << "        try:\n";
    code << "            all_symbols = sch.symbols.get_all()\n";
    code << "            for obs_sym in all_symbols:\n";
    code << "                obs_ref = getattr(obs_sym, 'reference', '')\n";
    code << "                if obs_ref in connected_refs:\n";
    code << "                    continue\n";
    code << "                obs_pos = [obs_sym.position.x / 1_000_000, "
         << "obs_sym.position.y / 1_000_000]\n";
    code << "                obs_pxs = [obs_pos[0]]\n";
    code << "                obs_pys = [obs_pos[1]]\n";
    code << "                for obs_pin in obs_sym.pins:\n";
    code << "                    try:\n";
    code << "                        tp = sch.symbols.get_transformed_pin_position("
         << "obs_sym, obs_pin.number)\n";
    code << "                        if tp:\n";
    code << "                            obs_pxs.append(tp['position'].x / 1_000_000)\n";
    code << "                            obs_pys.append(tp['position'].y / 1_000_000)\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "                padding = 1.27\n";
    code << "                obstacles.append({\n";
    code << "                    'min_x': min(obs_pxs) - padding,\n";
    code << "                    'max_x': max(obs_pxs) + padding,\n";
    code << "                    'min_y': min(obs_pys) - padding,\n";
    code << "                    'max_y': max(obs_pys) + padding,\n";
    code << "                })\n";
    code << "        except:\n";
    code << "            pass  # If obstacle query fails, proceed without avoidance\n";
    code << "\n";

    // Obstacle intersection check
    code << "        def trunk_hits_obstacle(trunk_val, t_min, t_max):\n";
    code << "            for obs in obstacles:\n";
    code << "                if is_horizontal:\n";
    code << "                    if obs['min_y'] <= trunk_val <= obs['max_y']:\n";
    code << "                        if obs['max_x'] > t_min and obs['min_x'] < t_max:\n";
    code << "                            return True\n";
    code << "                else:\n";
    code << "                    if obs['min_x'] <= trunk_val <= obs['max_x']:\n";
    code << "                        if obs['max_y'] > t_min and obs['min_y'] < t_max:\n";
    code << "                            return True\n";
    code << "            return False\n";
    code << "\n";

    // Choose trunk position (median of perpendicular coords)
    code << "        if is_horizontal:\n";
    code << "            perp_coords = sorted(ys)\n";
    code << "            trunk_min = snap_to_grid(min(xs))\n";
    code << "            trunk_max = snap_to_grid(max(xs))\n";
    code << "        else:\n";
    code << "            perp_coords = sorted(xs)\n";
    code << "            trunk_min = snap_to_grid(min(ys))\n";
    code << "            trunk_max = snap_to_grid(max(ys))\n";
    code << "\n";
    code << "        mid = len(perp_coords) // 2\n";
    code << "        if len(perp_coords) % 2 == 0:\n";
    code << "            trunk_perp = snap_to_grid("
         << "(perp_coords[mid-1] + perp_coords[mid]) / 2)\n";
    code << "        else:\n";
    code << "            trunk_perp = snap_to_grid(perp_coords[mid])\n";
    code << "\n";

    // Obstacle avoidance: shift trunk if needed
    code << "        if trunk_hits_obstacle(trunk_perp, trunk_min, trunk_max):\n";
    code << "            best = None\n";
    code << "            for offset in range(1, 20):\n";
    code << "                for direction in [1, -1]:\n";
    code << "                    candidate = snap_to_grid("
         << "trunk_perp + direction * offset * 1.27)\n";
    code << "                    if not trunk_hits_obstacle("
         << "candidate, trunk_min, trunk_max):\n";
    code << "                        if best is None or abs("
         << "candidate - trunk_perp) < abs(best - trunk_perp):\n";
    code << "                            best = candidate\n";
    code << "                        break\n";
    code << "                if best is not None and offset > 1:\n";
    code << "                    break\n";
    code << "            if best is not None:\n";
    code << "                trunk_perp = best\n";
    code << "\n";

    // Pre-compute branch targets and adjust trunk extent
    code << "        # Count off-trunk pins at each endpoint to decide offset\n";
    code << "        _ep_counts = {}\n";
    code << "        for p in pin_positions:\n";
    code << "            if is_horizontal:\n";
    code << "                proj = snap_to_grid(p['x'])\n";
    code << "                off = abs(p['y'] - trunk_perp) > 0.01\n";
    code << "                at_ep = abs(proj - trunk_min) < 0.01 or "
         << "abs(proj - trunk_max) < 0.01\n";
    code << "            else:\n";
    code << "                proj = snap_to_grid(p['y'])\n";
    code << "                off = abs(p['x'] - trunk_perp) > 0.01\n";
    code << "                at_ep = abs(proj - trunk_min) < 0.01 or "
         << "abs(proj - trunk_max) < 0.01\n";
    code << "            if off and at_ep:\n";
    code << "                key = round(proj, 4)\n";
    code << "                _ep_counts[key] = _ep_counts.get(key, 0) + 1\n";
    code << "\n";

    code << "        # Pre-compute branch connection points on the trunk\n";
    code << "        trunk_conn = []  # axis-coordinates of all points on the trunk\n";
    code << "        for p in pin_positions:\n";
    code << "            if is_horizontal:\n";
    code << "                proj = snap_to_grid(p['x'])\n";
    code << "                off = abs(p['y'] - trunk_perp) > 0.01\n";
    code << "                at_ep = abs(proj - trunk_min) < 0.01 or "
         << "abs(proj - trunk_max) < 0.01\n";
    code << "                solo_ep = off and at_ep and "
         << "_ep_counts.get(round(proj, 4), 0) == 1\n";
    code << "                if solo_ep:\n";
    code << "                    inward = 2.54 if abs(proj - trunk_max) < 0.01 else -2.54\n";
    code << "                    tgt_along = snap_to_grid(proj - inward)\n";
    code << "                    p['_tgt'] = (tgt_along, trunk_perp)\n";
    code << "                    p['_ep'] = True\n";
    code << "                    trunk_conn.append(tgt_along)\n";
    code << "                elif off:\n";
    code << "                    p['_tgt'] = (proj, trunk_perp)\n";
    code << "                    trunk_conn.append(proj)\n";
    code << "                else:\n";
    code << "                    trunk_conn.append(proj)\n";
    code << "            else:\n";
    code << "                proj = snap_to_grid(p['y'])\n";
    code << "                off = abs(p['x'] - trunk_perp) > 0.01\n";
    code << "                at_ep = abs(proj - trunk_min) < 0.01 or "
         << "abs(proj - trunk_max) < 0.01\n";
    code << "                solo_ep = off and at_ep and "
         << "_ep_counts.get(round(proj, 4), 0) == 1\n";
    code << "                if solo_ep:\n";
    code << "                    inward = 2.54 if abs(proj - trunk_max) < 0.01 else -2.54\n";
    code << "                    tgt_along = snap_to_grid(proj - inward)\n";
    code << "                    p['_tgt'] = (trunk_perp, tgt_along)\n";
    code << "                    p['_ep'] = True\n";
    code << "                    trunk_conn.append(tgt_along)\n";
    code << "                elif off:\n";
    code << "                    p['_tgt'] = (trunk_perp, proj)\n";
    code << "                    trunk_conn.append(proj)\n";
    code << "                else:\n";
    code << "                    trunk_conn.append(proj)\n";
    code << "\n";
    code << "        # Adjust trunk extent to actual connection points\n";
    code << "        trunk_min = snap_to_grid(min(trunk_conn))\n";
    code << "        trunk_max = snap_to_grid(max(trunk_conn))\n";
    code << "\n";

    // Place trunk wire
    code << "        if trunk_min != trunk_max:\n";
    code << "            if is_horizontal:\n";
    code << "                sch.wiring.add_wire("
         << "Vector2.from_xy_mm(trunk_min, trunk_perp), "
         << "Vector2.from_xy_mm(trunk_max, trunk_perp))\n";
    code << "            else:\n";
    code << "                sch.wiring.add_wire("
         << "Vector2.from_xy_mm(trunk_perp, trunk_min), "
         << "Vector2.from_xy_mm(trunk_perp, trunk_max))\n";
    code << "            wire_count += 1\n";
    code << "\n";

    // Place branches
    code << "        for p in pin_positions:\n";
    code << "            if is_horizontal:\n";
    code << "                off_trunk = abs(p['y'] - trunk_perp) > 0.01\n";
    code << "            else:\n";
    code << "                off_trunk = abs(p['x'] - trunk_perp) > 0.01\n";
    code << "\n";
    code << "            if off_trunk:\n";
    code << "                pin_x, pin_y = p['x'], p['y']\n";
    code << "                tgt_x, tgt_y = p['_tgt']\n";
    code << "                # Route branch to target\n";
    code << "                if abs(pin_x - tgt_x) < 0.01 or abs(pin_y - tgt_y) < 0.01:\n";
    code << "                    # Collinear: straight wire\n";
    code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), "
         << "Vector2.from_xy_mm(tgt_x, tgt_y))\n";
    code << "                    wire_count += 1\n";
    code << "                elif p.get('_ep'):\n";
    code << "                    # Endpoint branch: L-bend following pin direction\n";
    code << "                    if p['dir'] == 'h':\n";
    code << "                        corner = Vector2.from_xy_mm(tgt_x, pin_y)\n";
    code << "                    else:\n";
    code << "                        corner = Vector2.from_xy_mm(pin_x, tgt_y)\n";
    code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), corner)\n";
    code << "                    sch.wiring.add_wire(corner, "
         << "Vector2.from_xy_mm(tgt_x, tgt_y))\n";
    code << "                    wire_count += 2\n";
    code << "                else:\n";
    code << "                    # Interior branch: midpoint-bend step route\n";
    code << "                    if p['dir'] == 'v':\n";
    code << "                        mid = snap_to_grid((pin_y + tgt_y) / 2)\n";
    code << "                        c0 = Vector2.from_xy_mm(pin_x, mid)\n";
    code << "                        c1 = Vector2.from_xy_mm(tgt_x, mid)\n";
    code << "                    else:\n";
    code << "                        mid = snap_to_grid((pin_x + tgt_x) / 2)\n";
    code << "                        c0 = Vector2.from_xy_mm(mid, pin_y)\n";
    code << "                        c1 = Vector2.from_xy_mm(mid, tgt_y)\n";
    code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(pin_x, pin_y), c0)\n";
    code << "                    sch.wiring.add_wire(c0, c1)\n";
    code << "                    sch.wiring.add_wire(c1, Vector2.from_xy_mm(tgt_x, tgt_y))\n";
    code << "                    wire_count += 3\n";
    code << "\n";
    code << "\n";

    // Place junctions where 3+ wires meet (use exact wire coordinates)
    code << "        # Place junctions where 3+ wires meet\n";
    code << "        for i, p in enumerate(pin_positions):\n";
    code << "            if '_tgt' in p:\n";
    code << "                jx, jy = p['_tgt']\n";
    code << "                along = jx if is_horizontal else jy\n";
    code << "            else:\n";
    code << "                jx, jy = p['x'], p['y']\n";
    code << "                along = jx if is_horizontal else jy\n";
    code << "            at_end = abs(along - trunk_min) < 0.01 "
         << "or abs(along - trunk_max) < 0.01\n";
    code << "            if at_end:\n";
    code << "                # Endpoint: junction only if another pin connects here too\n";
    code << "                others = 0\n";
    code << "                for j, q in enumerate(pin_positions):\n";
    code << "                    if i == j:\n";
    code << "                        continue\n";
    code << "                    qa = (q['_tgt'][0] if is_horizontal "
         << "else q['_tgt'][1]) if '_tgt' in q "
         << "else (q['x'] if is_horizontal else q['y'])\n";
    code << "                    if abs(qa - along) < 0.01:\n";
    code << "                        others += 1\n";
    code << "                if others > 0:\n";
    code << "                    sch.wiring.add_junction("
         << "Vector2.from_xy_mm(jx, jy))\n";
    code << "                    junction_count += 1\n";
    code << "            else:\n";
    code << "                # Interior: trunk passes through, always needs junction\n";
    code << "                sch.wiring.add_junction("
         << "Vector2.from_xy_mm(jx, jy))\n";
    code << "                junction_count += 1\n";
    code << "\n";

    // Build result
    code << "    pin_info = [{'ref': p['ref'], 'pin': p['pin'], "
         << "'position': [p['x'], p['y']]} for p in pin_positions]\n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'source': 'ipc',\n";
    code << "        'pins': pin_info,\n";
    code << "        'wire_count': wire_count,\n";
    code << "        'junction_count': junction_count\n";
    code << "    }\n";
    code << "\n";
    code << "except Exception as e:\n";
    code << "    result = {'status': 'error', 'message': str(e)}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}
