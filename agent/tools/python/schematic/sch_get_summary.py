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
        tool_log(f'[sch_get_summary] current sheet detection failed: {_cs_err}')

    # Query live state via IPC
    symbols = sch.symbols.get_all()
    wires = sch.crud.get_wires()
    junctions = sch.crud.get_junctions()
    labels = sch.labels.get_all()
    no_connects = sch.crud.get_no_connects()
    sheets = sch.crud.get_sheets()
    bus_entries = []
    try:
        if hasattr(sch, 'buses') and hasattr(sch.buses, 'get_bus_entries'):
            bus_entries = sch.buses.get_bus_entries()
        elif hasattr(sch.crud, 'get_bus_entries'):
            bus_entries = sch.crud.get_bus_entries()
    except:
        pass

    # Get document title
    _title = ''
    try:
        if hasattr(sch.document, 'title'):
            _title = sch.document.title or ''
    except:
        pass

    # Format symbols — lightweight: ref, value, lib_id, pos, pin_count
    # Use sch_get_pins or sch_read_section for full pin details
    symbol_data = []
    for sym in symbols:
        lib_id_str = get_lib_id_str(sym)
        pin_count = len(sym.pins) if hasattr(sym, 'pins') else 0
        sym_info = {
            'ref': sym.reference if hasattr(sym, 'reference') else '',
            'value': sym.value if hasattr(sym, 'value') else '',
            'lib_id': lib_id_str,
            'pos': get_pos(getattr(sym, 'position', None)),
            'pin_count': pin_count
        }
        symbol_data.append(sym_info)

    # Format labels — text and type are key for understanding net connectivity
    label_data = []
    for lbl in labels:
        label_data.append({
            'text': lbl.text if hasattr(lbl, 'text') else '',
            'type': type(lbl).__name__,
            'pos': get_pos(getattr(lbl, 'position', None))
        })

    # Format sheets — name and file for hierarchy, pin_count for interface size
    sheet_data = []
    for sheet in sheets:
        pin_count = len(sheet.pins) if hasattr(sheet, 'pins') else 0
        sheet_data.append({
            'name': sheet.name if hasattr(sheet, 'name') else '',
            'file': sheet.filename if hasattr(sheet, 'filename') else '',
            'pin_count': pin_count
        })

    # Audit: detect orphaned items
    # (uses raw wire/pin data internally but only reports findings)
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
        'current_sheet': _current_sheet,
        'file': os.path.basename(file_path) if file_path else '',
        'title': _title,
        'symbols': symbol_data,
        'labels': label_data,
        'sheets': sheet_data,
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
