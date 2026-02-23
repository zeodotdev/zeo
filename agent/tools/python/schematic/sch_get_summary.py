import json, sys, os

refresh_or_fail(sch)

file_path = TOOL_ARGS.get("file_path", "")

try:
    # Determine which sheet we're currently viewing
    _current_sheet = ''
    try:
        if hasattr(sch.document, 'sheet_path'):
            sp = sch.document.sheet_path
            if hasattr(sp, 'path') and sp.path:
                _sheet_uuids = [p.value for p in sp.path]
                if len(_sheet_uuids) <= 1:
                    _current_sheet = '/ (root)'
                else:
                    _hr = getattr(sp, 'path_human_readable', '') or ''
                    if _hr:
                        _current_sheet = _hr
                    else:
                        _current_sheet = '/' + '/'.join(_sheet_uuids[1:])
            else:
                _current_sheet = '/ (root)'
    except Exception as _cs_err:
        print(f'[sch_get_summary] current sheet detection failed: {_cs_err}', file=sys.stderr)

    # Query live state via IPC
    symbols = sch.symbols.get_all()
    print(f'[IPC DEBUG] Retrieved {len(symbols)} symbols', file=sys.stderr)
    wires = sch.crud.get_wires()
    junctions = sch.crud.get_junctions()
    labels = sch.labels.get_all()
    no_connects = sch.crud.get_no_connects()
    sheets = sch.crud.get_sheets()
    # Get bus entries if available
    bus_entries = []
    try:
        if hasattr(sch, 'buses') and hasattr(sch.buses, 'get_bus_entries'):
            bus_entries = sch.buses.get_bus_entries()
        elif hasattr(sch.crud, 'get_bus_entries'):
            bus_entries = sch.crud.get_bus_entries()
    except:
        pass

    # Get document info if available
    doc_info = {}
    try:
        doc = sch.document
        if hasattr(doc, 'version'):
            doc_info['version'] = doc.version
        if hasattr(doc, 'uuid'):
            doc_info['uuid'] = str(doc.uuid)
        if hasattr(doc, 'paper'):
            doc_info['paper'] = doc.paper
        if hasattr(doc, 'title'):
            doc_info['title'] = doc.title
    except:
        pass

    # Format symbols with pin positions
    symbol_data = []
    for sym in symbols:
        lib_id_str = get_lib_id_str(sym)

        # Get symbol position and angle for pin transformation
        sym_pos = get_pos(getattr(sym, 'position', None))
        sym_angle = sym.angle if hasattr(sym, 'angle') else 0
        mirror_x = getattr(sym, 'mirror_x', False)
        mirror_y = getattr(sym, 'mirror_y', False)

        sym_info = {
            'uuid': get_uuid_str(sym),
            'lib_id': lib_id_str,
            'ref': sym.reference if hasattr(sym, 'reference') else '',
            'value': sym.value if hasattr(sym, 'value') else '',
            'pos': sym_pos,
            'angle': sym_angle,
            'unit': sym.unit if hasattr(sym, 'unit') else 1,
            'pins': []
        }
        # Get pin positions using batch API for efficiency (single IPC call per symbol)
        if hasattr(sym, 'pins'):
            # Use batch API if available
            pin_map = {}
            if hasattr(sch.symbols, 'get_all_transformed_pin_positions'):
                try:
                    all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for p in all_pins:
                        pin_map[p['pin_number']] = get_pos(p)
                except:
                    pass

            for pin in sym.pins:
                try:
                    abs_pos = pin_map.get(pin.number)

                    # Fallback: pin.position on placed symbols is already absolute (transformed)
                    # Do NOT add sym_pos - it's already included in the position
                    if not abs_pos or (abs_pos[0] == 0 and abs_pos[1] == 0):
                        pin_pos = get_pos(getattr(pin, 'position', None))
                        if pin_pos and (pin_pos[0] != 0 or pin_pos[1] != 0):
                            abs_pos = pin_pos  # Already absolute, no transformation needed

                    if abs_pos:
                        sym_info['pins'].append({
                            'number': pin.number,
                            'name': getattr(pin, 'name', ''),
                            'pos': abs_pos
                        })
                except:
                    pass
        symbol_data.append(sym_info)

    # Format wires
    wire_data = []
    for wire in wires:
        wire_data.append({
            'uuid': get_uuid_str(wire),
            'start': get_pos(getattr(wire, 'start', None)),
            'end': get_pos(getattr(wire, 'end', None))
        })

    # Format junctions
    junction_data = []
    for junc in junctions:
        junction_data.append({
            'uuid': get_uuid_str(junc),
            'pos': get_pos(getattr(junc, 'position', None))
        })

    # Format labels
    label_data = []
    for lbl in labels:
        label_data.append({
            'uuid': get_uuid_str(lbl),
            'text': lbl.text if hasattr(lbl, 'text') else '',
            'pos': get_pos(getattr(lbl, 'position', None)),
            'type': type(lbl).__name__
        })

    # Format no_connects
    nc_data = []
    for nc in no_connects:
        nc_data.append({
            'uuid': get_uuid_str(nc),
            'pos': get_pos(getattr(nc, 'position', None))
        })

    # Format sheets with full details including hierarchical pins
    _side_map = {1: 'left', 2: 'right', 3: 'top', 4: 'bottom'}
    _shape_map = {1: 'input', 2: 'output', 3: 'bidirectional', 4: 'tri_state', 5: 'passive'}
    sheet_data = []
    for sheet in sheets:
        sheet_info = {
            'uuid': get_uuid_str(sheet),
            'name': sheet.name if hasattr(sheet, 'name') else '',
            'file': sheet.filename if hasattr(sheet, 'filename') else '',
            'pins': []
        }
        if hasattr(sheet, 'pins'):
            for pin in sheet.pins:
                try:
                    pin_info = {
                        'name': pin.name if hasattr(pin, 'name') else '',
                        'pos': get_pos(getattr(pin, 'position', None)),
                        'side': _side_map.get(pin.side, str(pin.side)) if hasattr(pin, 'side') else '',
                        'shape': _shape_map.get(pin.shape, str(pin.shape)) if hasattr(pin, 'shape') else ''
                    }
                    sheet_info['pins'].append(pin_info)
                except:
                    pass
        sheet_data.append(sheet_info)

    # Format bus entries
    bus_entry_data = []
    for entry in bus_entries:
        entry_info = {
            'uuid': get_uuid_str(entry),
            'pos': get_pos(getattr(entry, 'position', None)),
        }
        # Get size/end point if available
        if hasattr(entry, 'size'):
            entry_info['size'] = get_pos(entry.size)
        if hasattr(entry, 'end'):
            entry_info['end'] = get_pos(entry.end)
        bus_entry_data.append(entry_info)

    # Audit: detect orphaned items
    rnd = lambda v: round(v, 2)
    def wire_ep(w):
        return (rnd(w.start.x/1e6), rnd(w.start.y/1e6)), (rnd(w.end.x/1e6), rnd(w.end.y/1e6))

    wire_pts = set()
    wire_ep_list = []
    for w in wires:
        try:
            s, e = wire_ep(w)
            wire_pts.add(s)
            wire_pts.add(e)
            wire_ep_list.append((s, e))
        except:
            pass

    # Collect all symbol pin positions (for label/junction connectivity)
    # Use batch API to get all pin positions in a single IPC call per symbol
    all_pin_pts = set()
    for sym in symbols:
        if hasattr(sym, 'pins'):
            try:
                all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                for tp in all_pins:
                    all_pin_pts.add((rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6)))
            except:
                pass

    conn_pts = wire_pts | all_pin_pts

    orphaned_power = []
    for sym in symbols:
        try:
            if not hasattr(sym, 'reference') or not sym.reference.startswith('#PWR'):
                continue
            connected = False
            # Use batch API for efficiency
            try:
                all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                for tp in all_pins:
                    pp = (rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6))
                    if pp in wire_pts:
                        connected = True
                        break
            except:
                pass
            if not connected:
                orphaned_power.append(sym.reference)
        except:
            pass

    orphaned_labels = []
    for lbl in labels:
        try:
            lp = (rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6))
            if lp not in conn_pts:
                orphaned_labels.append({'text': lbl.text, 'type': type(lbl).__name__, 'pos': list(lp)})
        except:
            pass

    def _point_on_wire_interior(pt, s, e):
        """Check if pt lies on wire segment s->e but NOT at endpoints."""
        px, py = pt
        sx, sy = s
        ex, ey = e
        if sx == ex == px:  # vertical wire
            return min(sy, ey) < py < max(sy, ey)
        if sy == ey == py:  # horizontal wire
            return min(sx, ex) < px < max(sx, ex)
        return False

    orphaned_junctions = []
    for junc in junctions:
        try:
            jp = (rnd(junc.position.x/1e6), rnd(junc.position.y/1e6))
            wc = sum(1 for s, e in wire_ep_list if s == jp or e == jp)
            wc += sum(1 for s, e in wire_ep_list if _point_on_wire_interior(jp, s, e))
            if wc < 2:
                orphaned_junctions.append({'pos': list(jp)})
        except:
            pass

    audit = {}
    if orphaned_power:
        audit['orphaned_power_symbols'] = orphaned_power
    if orphaned_labels:
        audit['orphaned_labels'] = orphaned_labels
    if orphaned_junctions:
        audit['orphaned_junctions'] = orphaned_junctions

    summary = {
        'source': 'ipc',
        'current_sheet': _current_sheet,
        'file': os.path.basename(file_path) if file_path else '',
        'version': doc_info.get('version', 0),
        'uuid': doc_info.get('uuid', ''),
        'paper': doc_info.get('paper', ''),
        'title': doc_info.get('title', ''),
        'symbols': symbol_data,
        'wires': wire_data,
        'junctions': junction_data,
        'labels': label_data,
        'no_connects': nc_data,
        'sheets': sheet_data,
        'bus_entries': bus_entry_data,
        'counts': {
            'symbols': len(symbols),
            'wires': len(wires),
            'junctions': len(junctions),
            'labels': len(labels),
            'no_connects': len(no_connects),
            'sheets': len(sheets),
            'bus_entries': len(bus_entries)
        }
    }
    if audit:
        summary['audit'] = audit
    print(json.dumps(summary, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))
