import json, fnmatch

refresh_or_fail(sch)

# Parse args
filter_pattern = TOOL_ARGS.get("filter", "")
library_filter = TOOL_ARGS.get("library", "")
refs_list = TOOL_ARGS.get("refs", [])
include_library_info = TOOL_ARGS.get("include_library_info", False)
include_connectivity = TOOL_ARGS.get("include_connectivity", True)

try:
    # Get all symbols
    all_symbols = sch.symbols.get_all()

    # Filter symbols
    filtered_symbols = []
    for sym in all_symbols:
        ref = getattr(sym, 'reference', '')
        lib_id = get_lib_id_str(sym)

        # Skip power symbols (they have #PWR refs)
        if ref.startswith('#PWR'):
            continue

        # Apply filters
        if refs_list:
            if ref not in refs_list:
                continue
        elif filter_pattern:
            if not fnmatch.fnmatch(ref, filter_pattern):
                continue

        if library_filter:
            # lib_id is like "Device:R", extract library name
            lib_name = lib_id.split(':')[0] if ':' in lib_id else lib_id
            if lib_name != library_filter:
                continue

        filtered_symbols.append(sym)

    # Build connectivity data if requested
    wire_endpoints = set()
    wire_segments = []
    label_positions = {}  # (x, y) -> net_name
    no_connect_positions = set()

    if include_connectivity:
        rnd = lambda v: round(v, 2)

        # Collect wire endpoints and segments
        wires = sch.crud.get_wires()
        for w in wires:
            start = (rnd(w.start.x / 1e6), rnd(w.start.y / 1e6))
            end = (rnd(w.end.x / 1e6), rnd(w.end.y / 1e6))
            wire_endpoints.add(start)
            wire_endpoints.add(end)
            wire_segments.append((start, end))

        # Collect labels
        labels = sch.labels.get_all()
        for lbl in labels:
            pos = (rnd(lbl.position.x / 1e6), rnd(lbl.position.y / 1e6))
            text = getattr(lbl, 'text', '')
            label_positions[pos] = text

        # Collect no-connects
        no_connects = sch.crud.get_no_connects()
        for nc in no_connects:
            pos = (rnd(nc.position.x / 1e6), rnd(nc.position.y / 1e6))
            no_connect_positions.add(pos)

    def point_on_wire(pt):
        """Check if point is at a wire endpoint or lies on a wire segment."""
        if pt in wire_endpoints:
            return True
        px, py = pt
        for (sx, sy), (ex, ey) in wire_segments:
            # Check if point lies on wire segment
            if sx == ex == px:  # vertical wire
                if min(sy, ey) <= py <= max(sy, ey):
                    return True
            elif sy == ey == py:  # horizontal wire
                if min(sx, ex) <= px <= max(sx, ex):
                    return True
        return False

    def find_net_name(start_pt):
        """BFS through wire network to find connected label (net name)."""
        if start_pt in label_positions:
            return label_positions[start_pt]

        visited = set()
        queue = [start_pt]

        while queue:
            pt = queue.pop(0)
            if pt in visited:
                continue
            visited.add(pt)

            # Check if there's a label here
            if pt in label_positions:
                return label_positions[pt]

            # Find connected wire endpoints
            for (sx, sy), (ex, ey) in wire_segments:
                start = (sx, sy)
                end = (ex, ey)
                if start == pt and end not in visited:
                    queue.append(end)
                elif end == pt and start not in visited:
                    queue.append(start)
                # Also check if pt lies on wire interior
                elif start != pt and end != pt:
                    px, py = pt
                    if sx == ex == px and min(sy, ey) < py < max(sy, ey):
                        if start not in visited:
                            queue.append(start)
                        if end not in visited:
                            queue.append(end)
                    elif sy == ey == py and min(sx, ex) < px < max(sx, ex):
                        if start not in visited:
                            queue.append(start)
                        if end not in visited:
                            queue.append(end)

        return None

    # Build output
    symbols_out = []
    rnd = lambda v: round(v, 2)

    for sym in filtered_symbols:
        ref = getattr(sym, 'reference', '')
        value = getattr(sym, 'value', '')
        lib_id = get_lib_id_str(sym)
        pos = get_pos(getattr(sym, 'position', None))
        angle = getattr(sym, 'angle', 0) or 0
        dnp = getattr(sym, 'dnp', False)

        # Get footprint from properties
        footprint = ''
        if hasattr(sym, '_proto') and hasattr(sym._proto, 'fields'):
            for field in sym._proto.fields:
                if field.name == 'Footprint':
                    footprint = field.value.text if hasattr(field.value, 'text') else str(field.value)
                    break

        sym_data = {
            'ref': ref,
            'value': value,
            'lib_id': lib_id,
            'footprint': footprint,
            'position': [rnd(pos[0]), rnd(pos[1])],
            'angle': angle,
            'dnp': dnp
        }

        # Get pins with positions
        pins_out = []
        if hasattr(sym, 'pins'):
            # Use batch API for pin positions
            pin_map = {}
            if hasattr(sch.symbols, 'get_all_transformed_pin_positions'):
                try:
                    all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for p in all_pins:
                        pin_map[p['pin_number']] = {
                            'position': (rnd(p['position'].x / 1e6), rnd(p['position'].y / 1e6))
                        }
                except:
                    pass

            for pin in sym.pins:
                pin_num = pin.number
                pin_name = getattr(pin, 'name', '')

                # Get position from map or fallback
                if pin_num in pin_map:
                    pin_pos = pin_map[pin_num]['position']
                else:
                    raw_pos = get_pos(getattr(pin, 'position', None))
                    pin_pos = (rnd(raw_pos[0]), rnd(raw_pos[1]))

                pin_data = {
                    'number': pin_num,
                    'name': pin_name,
                    'position': list(pin_pos)
                }

                # Add connectivity info
                if include_connectivity:
                    connected = point_on_wire(pin_pos)
                    pin_data['connected'] = connected

                    if connected:
                        net = find_net_name(pin_pos)
                        if net:
                            pin_data['net'] = net
                    else:
                        # Check if it has a no-connect
                        pin_data['no_connect'] = pin_pos in no_connect_positions

                pins_out.append(pin_data)

        sym_data['pins'] = pins_out

        # Include library info if requested
        if include_library_info:
            try:
                lib_sym = sch.symbols.get_library_symbol(lib_id)
                if lib_sym:
                    if hasattr(lib_sym, 'description'):
                        sym_data['description'] = lib_sym.description
                    if hasattr(lib_sym, 'footprint_filters'):
                        filters = lib_sym.footprint_filters
                        if hasattr(filters, '__iter__'):
                            sym_data['footprint_filters'] = list(filters)
                        else:
                            sym_data['footprint_filters'] = []
            except:
                pass

        symbols_out.append(sym_data)

    result = {
        'status': 'success',
        'count': len(symbols_out),
        'symbols': symbols_out
    }
    print(json.dumps(result, indent=2))

except Exception as e:
    import traceback
    print(json.dumps({
        'status': 'error',
        'message': str(e),
        'traceback': traceback.format_exc()
    }))
