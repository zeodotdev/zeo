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

    # Format symbols
    symbol_data = []
    for sym in symbols:
        lib_id_str = get_lib_id_str(sym)
        pin_count = len(sym.pins) if hasattr(sym, 'pins') else 0
        symbol_data.append({
            'ref': sym.reference if hasattr(sym, 'reference') else '',
            'value': sym.value if hasattr(sym, 'value') else '',
            'lib_id': lib_id_str,
            'pos': get_pos(getattr(sym, 'position', None)),
            'pin_count': pin_count
        })

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
                except Exception as _e:
                    tool_log(f'[sch_get_summary] orphaned power pin check failed: {_e}')
                if not connected:
                    orphaned_power.append(sym.reference)
            except Exception as _e:
                tool_log(f'[sch_get_summary] orphaned power detection failed: {_e}')

        orphaned_labels = []
        for lbl in labels:
            try:
                lp = (rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6))
                if lp not in conn_pts:
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

        audit = {}
        if orphaned_power:
            audit['orphaned_power_symbols'] = orphaned_power
        if orphaned_labels:
            audit['orphaned_labels'] = orphaned_labels
        if orphaned_junctions:
            audit['orphaned_junctions'] = orphaned_junctions
        if audit:
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
