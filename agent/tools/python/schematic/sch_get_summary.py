import json, sys, os

refresh_or_fail(sch)

file_path = TOOL_ARGS.get("file_path", "")
all_sheets = TOOL_ARGS.get("all_sheets", False)


def get_current_sheet_name():
    """Return a human-readable name for the currently active sheet."""
    try:
        if hasattr(sch.document, 'sheet_path'):
            sp = sch.document.sheet_path
            if hasattr(sp, 'path') and sp.path:
                _sheet_uuids = [p.value for p in sp.path]
                if len(_sheet_uuids) <= 1:
                    return '/ (root)'
                _hr = getattr(sp, 'path_human_readable', '') or ''
                return _hr if _hr else '/' + '/'.join(_sheet_uuids[1:])
            return '/ (root)'
    except Exception as _cs_err:
        tool_log(f'[sch_get_summary] current sheet detection failed: {_cs_err}')
    return ''


def collect_sheet_summary(include_audit=True):
    """Collect summary data for the currently active sheet."""
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
    except Exception as _e:
        tool_log(f'[sch_get_summary] bus entries fetch failed: {_e}')

    # Cache library info for unit counts and pin units
    lib_info_cache = {}

    def get_lib_info(lib_id):
        """Get cached library symbol info (unit_count, pin_units)."""
        if lib_id not in lib_info_cache:
            try:
                lib_sym = sch.library.get_symbol_info(lib_id)
                if lib_sym:
                    lib_info_cache[lib_id] = {
                        'unit_count': getattr(lib_sym, 'unit_count', 1),
                        'pin_units': {p.number: getattr(p, 'unit', 0) for p in getattr(lib_sym, 'pins', [])}
                    }
                else:
                    lib_info_cache[lib_id] = {'unit_count': 1, 'pin_units': {}}
            except Exception:
                lib_info_cache[lib_id] = {'unit_count': 1, 'pin_units': {}}
        return lib_info_cache[lib_id]

    # Format symbols
    symbol_data = []
    for sym in symbols:
        lib_id_str = get_lib_id_str(sym)
        sym_unit = getattr(sym, 'unit', 1)
        lib_info = get_lib_info(lib_id_str)
        unit_count = lib_info['unit_count']
        pin_units = lib_info['pin_units']

        # Count only pins that belong to this unit (or are shared, unit 0)
        if hasattr(sym, 'pins'):
            unit_pin_count = sum(1 for pin in sym.pins
                                 if pin_units.get(pin.number, 0) in (0, sym_unit))
        else:
            unit_pin_count = 0

        sym_entry = {
            'ref': sym.reference if hasattr(sym, 'reference') else '',
            'value': sym.value if hasattr(sym, 'value') else '',
            'lib_id': lib_id_str,
            'pos': get_pos(getattr(sym, 'position', None)),
            'pin_count': unit_pin_count
        }

        # Add unit info for multi-unit symbols
        if unit_count > 1:
            sym_entry['unit'] = sym_unit
            sym_entry['unit_count'] = unit_count

        symbol_data.append(sym_entry)

    # Format labels
    label_data = []
    for lbl in labels:
        label_data.append({
            'text': lbl.text if hasattr(lbl, 'text') else '',
            'type': type(lbl).__name__,
            'pos': get_pos(getattr(lbl, 'position', None))
        })

    # Format child sheets
    sheet_data = []
    for sheet in sheets:
        pin_count = len(sheet.pins) if hasattr(sheet, 'pins') else 0
        sheet_data.append({
            'name': sheet.name if hasattr(sheet, 'name') else '',
            'file': sheet.filename if hasattr(sheet, 'filename') else '',
            'pin_count': pin_count
        })

    counts = {
        'symbols': len(symbols),
        'wires': len(wires),
        'junctions': len(junctions),
        'labels': len(labels),
        'no_connects': len(no_connects),
        'sheets': len(sheets),
        'bus_entries': len(bus_entries)
    }

    result = {
        'symbols': symbol_data,
        'labels': label_data,
        'sheets': sheet_data,
        'counts': counts
    }

    # Audit: detect orphaned items (skip when iterating all sheets for speed)
    if include_audit:
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
            except Exception as _e:
                tool_log(f'[sch_get_summary] wire endpoint extraction failed: {_e}')

        all_pin_pts = set()
        for sym in symbols:
            if hasattr(sym, 'pins'):
                try:
                    all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for tp in all_pins:
                        all_pin_pts.add((rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6)))
                except Exception as _e:
                    tool_log(f'[sch_get_summary] pin position fetch failed: {_e}')

        # Collect hierarchical sheet pin positions - labels on sheet pins are valid connections
        sheet_pin_pts = set()
        for sheet in sheets:
            if hasattr(sheet, 'pins'):
                try:
                    for pin in sheet.pins:
                        if hasattr(pin, 'position'):
                            pp = (rnd(pin.position.x/1e6), rnd(pin.position.y/1e6))
                            sheet_pin_pts.add(pp)
                except Exception as _e:
                    tool_log(f'[sch_get_summary] sheet pin position fetch failed: {_e}')

        conn_pts = wire_pts | all_pin_pts | sheet_pin_pts

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
                except Exception as _e:
                    tool_log(f'[sch_get_summary] orphaned power pin check failed: {_e}')
                if not connected:
                    orphaned_power.append(sym.reference)
            except Exception as _e:
                tool_log(f'[sch_get_summary] orphaned power detection failed: {_e}')

        def _point_on_wire_segment(pt, wire_list):
            """Check if point lies on any wire segment (including interior, not just endpoints)."""
            px, py = pt
            for (sx, sy), (ex, ey) in wire_list:
                # Check endpoints
                if (px, py) == (sx, sy) or (px, py) == (ex, ey):
                    return True
                # Check wire interior
                if sx == ex == px:  # vertical wire
                    if min(sy, ey) <= py <= max(sy, ey):
                        return True
                elif sy == ey == py:  # horizontal wire
                    if min(sx, ex) <= px <= max(sx, ex):
                        return True
            return False

        def _near_any_point(pt, point_set, tolerance=0.05):
            """Check if pt is within tolerance (in mm) of any point in the set."""
            px, py = pt
            for (cx, cy) in point_set:
                if abs(px - cx) <= tolerance and abs(py - cy) <= tolerance:
                    return True
            return False

        orphaned_labels = []
        for lbl in labels:
            try:
                lp = (rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6))

                # Check 1: Exact coordinate match with wire endpoint, pin, or sheet pin
                if lp in conn_pts:
                    continue

                # Check 2: Label is on a wire segment (including wire interior)
                if _point_on_wire_segment(lp, wire_ep_list):
                    continue

                # Check 3: Proximity check - label within 0.05mm of any connection point
                # This catches labels placed on pins where coordinates don't exactly match
                if _near_any_point(lp, conn_pts, tolerance=0.05):
                    continue

                # Label is not connected by any method - mark as orphaned
                orphaned_labels.append({'text': lbl.text, 'type': type(lbl).__name__, 'pos': list(lp)})
            except Exception as _e:
                tool_log(f'[sch_get_summary] orphaned label detection failed: {_e}')

        def _point_on_wire_interior(pt, s, e):
            px, py = pt
            sx, sy = s
            ex, ey = e
            if sx == ex == px:
                return min(sy, ey) < py < max(sy, ey)
            if sy == ey == py:
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
            except Exception as _e:
                tool_log(f'[sch_get_summary] orphaned junction detection failed: {_e}')

        # Detect unmatched labels (labels that appear only once on this sheet)
        # Only flag local labels (NetLabel) since global/hierarchical labels connect across sheets
        unmatched_labels = []
        label_counts = {}
        for lbl in labels:
            try:
                lbl_type = type(lbl).__name__
                # Only check local labels - global and hierarchical labels connect across sheets
                if lbl_type not in ('NetLabel', 'Label'):
                    continue
                text = getattr(lbl, 'text', '')
                if text:
                    label_counts[text] = label_counts.get(text, 0) + 1
            except Exception as _e:
                tool_log(f'[sch_get_summary] label counting failed: {_e}')

        for lbl in labels:
            try:
                lbl_type = type(lbl).__name__
                if lbl_type not in ('NetLabel', 'Label'):
                    continue
                text = getattr(lbl, 'text', '')
                if text and label_counts.get(text, 0) == 1:
                    lp = (rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6))
                    unmatched_labels.append({'text': text, 'type': lbl_type, 'pos': list(lp)})
            except Exception as _e:
                tool_log(f'[sch_get_summary] unmatched label detection failed: {_e}')

        # Always include audit section so the agent knows the audit ran
        audit = {
            'orphaned_power_symbols': orphaned_power if orphaned_power else [],
            'orphaned_labels': orphaned_labels if orphaned_labels else [],
            'unmatched_labels': unmatched_labels if unmatched_labels else [],
            'orphaned_junctions': orphaned_junctions if orphaned_junctions else []
        }
        result['audit'] = audit

    return result


def flatten_hierarchy(node, parent_human_path='/'):
    """Recursively flatten hierarchy tree into list of (node, human_path) tuples."""
    nodes = []
    name = getattr(node, 'name', '') or ''

    if not name:
        human_readable = '/'
    else:
        p = parent_human_path if parent_human_path.endswith('/') else parent_human_path + '/'
        human_readable = p + name + '/'

    nodes.append((node, human_readable))

    if hasattr(node, 'children'):
        for child in node.children:
            nodes.extend(flatten_hierarchy(child, human_readable))
    return nodes


try:
    if all_sheets:
        # ── All-sheets mode: navigate to each sheet, collect summary, navigate back ──
        tool_log('[sch_get_summary] all_sheets mode — collecting summaries for all sheets')

        # Save current sheet so we can restore it
        saved_sheet_path = None
        try:
            if hasattr(sch.document, 'sheet_path'):
                saved_sheet_path = sch.document.sheet_path
        except Exception:
            pass

        # Get hierarchy
        hierarchy_nodes = []
        if hasattr(sch.sheets, 'get_hierarchy'):
            try:
                tree = sch.sheets.get_hierarchy()
                hierarchy_nodes = flatten_hierarchy(tree)
                tool_log(f'[sch_get_summary] Found {len(hierarchy_nodes)} sheets in hierarchy')
            except Exception as he:
                tool_log(f'[sch_get_summary] get_hierarchy failed: {he}')

        if not hierarchy_nodes:
            # Flat schematic — just return single-sheet summary
            tool_log('[sch_get_summary] No hierarchy found, returning single sheet summary')
            _current_sheet = get_current_sheet_name()
            summary = collect_sheet_summary(include_audit=True)
            summary['current_sheet'] = _current_sheet
            summary['file'] = os.path.basename(file_path) if file_path else ''
            _title = ''
            try:
                if hasattr(sch.document, 'title'):
                    _title = sch.document.title or ''
            except Exception:
                pass
            summary['title'] = _title
            print(json.dumps(summary, indent=2))
        else:
            # Iterate through all sheets
            sheet_summaries = []
            for node, human_path in hierarchy_nodes:
                # Navigate to this sheet
                try:
                    if hasattr(node, 'path') and node.path and hasattr(sch.sheets, 'navigate_to'):
                        sch.sheets.navigate_to(node.path)
                        # Refresh document state after navigation
                        refresh_or_fail(sch)
                    elif human_path == '/':
                        if hasattr(sch.sheets, 'navigate_to_root'):
                            sch.sheets.navigate_to_root()
                            refresh_or_fail(sch)
                except Exception as nav_err:
                    tool_log(f'[sch_get_summary] Failed to navigate to {human_path}: {nav_err}')
                    continue

                # Collect summary for this sheet (skip audit for speed)
                try:
                    sheet_summary = collect_sheet_summary(include_audit=False)
                    sheet_summary['sheet_path'] = human_path
                    sheet_summary['sheet_name'] = getattr(node, 'name', '') or 'Root'
                    sheet_summaries.append(sheet_summary)
                    tool_log(f'[sch_get_summary] Collected summary for {human_path}: '
                             f'{sheet_summary["counts"]["symbols"]} symbols')
                except Exception as col_err:
                    tool_log(f'[sch_get_summary] Failed to collect summary for {human_path}: {col_err}')

            # Restore original sheet
            if saved_sheet_path and hasattr(sch.sheets, 'navigate_to'):
                try:
                    sch.sheets.navigate_to(saved_sheet_path)
                except Exception as restore_err:
                    tool_log(f'[sch_get_summary] Failed to restore original sheet: {restore_err}')

            # Get title
            _title = ''
            try:
                if hasattr(sch.document, 'title'):
                    _title = sch.document.title or ''
            except Exception:
                pass

            result = {
                'title': _title,
                'file': os.path.basename(file_path) if file_path else '',
                'sheet_count': len(sheet_summaries),
                'sheets': sheet_summaries
            }
            print(json.dumps(result, indent=2))

    else:
        # ── Single-sheet mode: existing behavior ──
        _current_sheet = get_current_sheet_name()

        # Get document title
        _title = ''
        try:
            if hasattr(sch.document, 'title'):
                _title = sch.document.title or ''
        except Exception as _e:
            tool_log(f'[sch_get_summary] title fetch failed: {_e}')

        summary = collect_sheet_summary(include_audit=True)
        summary['current_sheet'] = _current_sheet
        summary['file'] = os.path.basename(file_path) if file_path else ''
        summary['title'] = _title
        print(json.dumps(summary, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))
