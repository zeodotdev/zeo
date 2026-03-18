import json, re, sys

refresh_or_fail(sch)

targets = TOOL_ARGS.get("targets", [])
file_path = TOOL_ARGS.get("file_path", "")
delete_net = TOOL_ARGS.get("delete_net", False)

# Separate string targets from query targets
string_targets = [t for t in targets if isinstance(t, str)]
query_targets = [t for t in targets if isinstance(t, dict)]

use_ipc = True
result = None
target_uuids = []

# Try IPC first
try:
    items_to_delete = []
    not_found = []
    query_not_found = []

    for target in string_targets:
        is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))
        if is_uuid:
            items = sch.crud.get_by_id([target])
            if items:
                items_to_delete.append(items[0])
                target_uuids.append(target)
            else:
                not_found.append(target)
        else:
            item = sch.symbols.get_by_ref(target)
            if item:
                items_to_delete.append(item)
                item_uuid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', getattr(item, 'uuid', '')))
                target_uuids.append(item_uuid)
            else:
                not_found.append(target)

    # Process query-based targets
    def pos_match(actual_nm, expected_mm, tol=0.1):
        ax = actual_nm.x / 1e6
        ay = actual_nm.y / 1e6
        return abs(ax - expected_mm[0]) <= tol and abs(ay - expected_mm[1]) <= tol

    for q in query_targets:
        q_type = q.get('type', '')
        q_text = q.get('text', None)
        q_pos = q.get('position', None)
        q_start = q.get('start', None)
        q_end = q.get('end', None)
        matched = []

        if q_type == 'wire':
            for w in sch.crud.get_wires():
                ok = True
                if q_start is not None and not pos_match(w.start, q_start):
                    ok = False
                if q_end is not None and not pos_match(w.end, q_end):
                    ok = False
                if q_pos is not None:
                    if not pos_match(w.start, q_pos) and not pos_match(w.end, q_pos):
                        ok = False
                if ok:
                    matched.append(w)

        elif q_type in ('label', 'global_label', 'hierarchical_label'):
            type_map = {'label': 'LocalLabel', 'global_label': 'GlobalLabel', 'hierarchical_label': 'HierarchicalLabel'}
            expected_class = type_map.get(q_type, '')
            q_ref = q.get('ref', None)

            # If ref is specified, collect that symbol's pin positions (in nm)
            ref_pin_positions = set()
            if q_ref:
                ref_sym = sch.symbols.get_by_ref(q_ref)
                if ref_sym:
                    try:
                        all_pins = sch.symbols.get_all_transformed_pin_positions(ref_sym)
                        for tp in all_pins:
                            ref_pin_positions.add((tp['position'].x, tp['position'].y))
                    except Exception:
                        pass

            for lbl in sch.labels.get_all():
                ok = True
                if expected_class and type(lbl).__name__ != expected_class:
                    ok = False
                if q_text is not None and getattr(lbl, 'text', '') != q_text:
                    ok = False
                if q_pos is not None and not pos_match(lbl.position, q_pos):
                    ok = False
                if q_ref is not None and (lbl.position.x, lbl.position.y) not in ref_pin_positions:
                    ok = False
                if ok:
                    matched.append(lbl)

        elif q_type == 'junction':
            for j in sch.crud.get_junctions():
                if q_pos is not None and not pos_match(j.position, q_pos):
                    continue
                matched.append(j)

        elif q_type == 'no_connect':
            for nc in sch.crud.get_no_connects():
                if q_pos is not None and not pos_match(nc.position, q_pos):
                    continue
                matched.append(nc)

        elif q_type == 'bus_entry':
            be_list = []
            try:
                if hasattr(sch, 'buses') and hasattr(sch.buses, 'get_bus_entries'):
                    be_list = sch.buses.get_bus_entries()
                elif hasattr(sch.crud, 'get_bus_entries'):
                    be_list = sch.crud.get_bus_entries()
            except Exception:
                pass
            for be in be_list:
                if q_pos is not None and not pos_match(be.position, q_pos):
                    continue
                matched.append(be)

        else:
            query_not_found.append(q)
            continue

        if matched:
            for m in matched:
                items_to_delete.append(m)
                uid = str(m.id.value) if hasattr(m, 'id') and hasattr(m.id, 'value') else str(getattr(m, 'id', ''))
                target_uuids.append(uid)
        else:
            query_not_found.append(q)

    # Chain delete: geometric BFS from initial items through connected wires/junctions/labels
    net_deleted_count = 0
    if delete_net:
        from collections import defaultdict, deque

        def _get_points(it):
            pts = []
            t = type(it).__name__
            if t == 'Wire' and hasattr(it, 'start') and hasattr(it, 'end'):
                pts.append((it.start.x, it.start.y))
                pts.append((it.end.x, it.end.y))
            elif hasattr(it, 'position'):
                pts.append((it.position.x, it.position.y))
            return pts

        # Build spatial index from ALL wires, junctions, labels, and no-connects
        all_items = {}  # uid -> item
        point_to_uids = defaultdict(set)
        uid_to_points = {}

        def _index_item(it):
            uid = str(it.id.value) if hasattr(it, 'id') and hasattr(it.id, 'value') else ''
            if uid:
                all_items[uid] = it
                pts = _get_points(it)
                uid_to_points[uid] = pts
                for p in pts:
                    point_to_uids[p].add(uid)

        for w in sch.crud.get_wires():
            _index_item(w)
        for j in sch.crud.get_junctions():
            _index_item(j)
        for lbl in sch.labels.get_all():
            _index_item(lbl)
        try:
            for nc in sch.crud.get_no_connects():
                _index_item(nc)
        except Exception:
            pass

        # Collect symbol pin positions as BFS stop points (batch API for efficiency)
        pin_positions = set()
        for sym in sch.symbols.get_all():
            try:
                all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                for tp in all_pins:
                    pin_positions.add((tp['position'].x, tp['position'].y))
            except Exception:
                pass

        # BFS flood-fill from initial items
        visited = set()
        for item in items_to_delete:
            uid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', ''))
            visited.add(uid)
        queue = deque()
        for item in items_to_delete:
            for p in _get_points(item):
                queue.append(p)
        deletable_types = ('Wire', 'Junction', 'NetLabel', 'GlobalLabel', 'HierarchicalLabel', 'NoConnect')
        while queue:
            pt = queue.popleft()
            if pt in pin_positions:
                continue
            for neighbor_uid in point_to_uids.get(pt, []):
                if neighbor_uid in visited:
                    continue
                visited.add(neighbor_uid)
                ni = all_items.get(neighbor_uid)
                if ni and type(ni).__name__ in deletable_types:
                    items_to_delete.append(ni)
                    target_uuids.append(neighbor_uid)
                    net_deleted_count += 1
                    for p2 in uid_to_points.get(neighbor_uid, []):
                        queue.append(p2)

    # Record pin positions before deletion and collect orphaned labels in the same pass
    deleted_pin_positions = []
    orphaned_labels = []
    orphaned_wires = []
    orphaned_junctions = []
    orphaned_power = []
    rnd = lambda v: round(v, 2)

    for item in items_to_delete:
        if hasattr(item, 'pins'):
            try:
                all_pins = sch.symbols.get_all_transformed_pin_positions(item)
                for tp in all_pins:
                    dpx = round(tp['position'].x / 1_000_000, 4)
                    dpy = round(tp['position'].y / 1_000_000, 4)
                    deleted_pin_positions.append((dpx, dpy))
            except Exception:
                pass

    # Collect labels at pin positions before deletion so we can batch-delete with the symbols
    if deleted_pin_positions:
        dead = set((rnd(px), rnd(py)) for px, py in deleted_pin_positions)
        for lbl in sch.labels.get_all():
            try:
                lp = (rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6))
                if lp in dead:
                    items_to_delete.append(lbl)
                    uid = str(lbl.id.value) if hasattr(lbl, 'id') and hasattr(lbl.id, 'value') else str(getattr(lbl, 'id', ''))
                    orphaned_labels.append({'uuid': uid, 'text': getattr(lbl, 'text', ''), 'type': type(lbl).__name__, 'position': list(lp)})
            except Exception:
                pass

    if items_to_delete:
        sch.crud.remove_items(items_to_delete)

    # Clean up orphaned wires, junctions, and power symbols at deleted pin positions
    if deleted_pin_positions:
        try:

            def wire_ep(w):
                return (rnd(w.start.x/1e6), rnd(w.start.y/1e6)), (rnd(w.end.x/1e6), rnd(w.end.y/1e6))

            # Collect all remaining connection points (symbol pins + labels) - batch API for efficiency
            conn_pts = set()
            for sym in sch.symbols.get_all():
                try:
                    all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for tp in all_pins:
                        conn_pts.add((rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6)))
                except Exception:
                    pass
            for lbl in sch.labels.get_all():
                try:
                    conn_pts.add((rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6)))
                except Exception:
                    pass
            checked = set()

            for _iter in range(10):  # safety cap
                to_check = dead - checked
                if not to_check:
                    break
                checked |= to_check

                cur_wires = sch.crud.get_wires()
                cur_juncs = sch.crud.get_junctions()

                rm_wires = []
                freed = set()
                for w in cur_wires:
                    try:
                        s, e = wire_ep(w)
                        s_hit = s in to_check
                        e_hit = e in to_check
                        if s_hit or e_hit:
                            rm_wires.append(w)
                            uid = str(w.id.value) if hasattr(w, 'id') else ''
                            orphaned_wires.append({'uuid': uid, 'start': list(s), 'end': list(e)})
                            if s_hit:
                                freed.add(e)
                            if e_hit:
                                freed.add(s)
                    except Exception:
                        pass

                rm_juncs = []
                for j in cur_juncs:
                    try:
                        jp = (rnd(j.position.x/1e6), rnd(j.position.y/1e6))
                        if jp in to_check:
                            rm_juncs.append(j)
                            uid = str(j.id.value) if hasattr(j, 'id') else ''
                            orphaned_junctions.append({'uuid': uid, 'position': list(jp)})
                    except Exception:
                        pass

                if not rm_wires and not rm_juncs:
                    break
                sch.crud.remove_items(rm_wires + rm_juncs)

                # Cascade: freed endpoints become dead if not at a component pin
                # and have <= 1 remaining wire (i.e. dangling)
                remaining = sch.crud.get_wires()
                for fp in freed - conn_pts - checked:
                    wc = 0
                    for w in remaining:
                        try:
                            s, e = wire_ep(w)
                            if s == fp or e == fp:
                                wc += 1
                        except Exception:
                            pass
                    if wc <= 1:
                        dead.add(fp)

            # Check for orphaned power symbols (#PWR) after wire cleanup
            remaining_wires = sch.crud.get_wires()
            wire_pts = set()
            for w in remaining_wires:
                try:
                    s, e = wire_ep(w)
                    wire_pts.add(s)
                    wire_pts.add(e)
                except Exception:
                    pass
            for sym in sch.symbols.get_all():
                try:
                    if not sym.reference.startswith('#PWR'):
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
                    except Exception:
                        pass
                    if not connected:
                        orphaned_power.append({'ref': sym.reference, 'uuid': str(sym.id.value) if hasattr(sym, 'id') else ''})
                except Exception:
                    pass
            if orphaned_power:
                pwr_items = []
                for op in orphaned_power:
                    s = sch.symbols.get_by_ref(op['ref'])
                    if s:
                        pwr_items.append(s)
                if pwr_items:
                    sch.crud.remove_items(pwr_items)

        except Exception as cleanup_err:
            print(f'Orphan cleanup warning: {cleanup_err}', file=sys.stderr)

    result = {'status': 'success', 'source': 'ipc', 'deleted': len(items_to_delete)}
    if orphaned_labels:
        result['orphaned_labels_removed'] = len(orphaned_labels)
        result['orphaned_labels'] = orphaned_labels
    if orphaned_wires:
        result['orphaned_wires_removed'] = len(orphaned_wires)
        result['orphaned_wires'] = orphaned_wires
    if orphaned_junctions:
        result['orphaned_junctions_removed'] = len(orphaned_junctions)
    if orphaned_power:
        result['orphaned_power_removed'] = len(orphaned_power)
        result['orphaned_power'] = orphaned_power
    if not_found:
        result['not_found'] = not_found
    if query_not_found:
        result['queries_not_matched'] = query_not_found
    if net_deleted_count:
        result['net_deleted_extra'] = net_deleted_count

except Exception as ipc_error:
    use_ipc = False
    ipc_error_msg = str(ipc_error)
    print(f'IPC failed: {ipc_error_msg}', file=sys.stderr)

# Check if editor is still open - don't mix IPC and file operations
editor_still_open = False
try:
    if hasattr(sch, 'refresh_document'):
        editor_still_open = sch.refresh_document()
except Exception:
    pass  # Editor likely closed

# File-based fallback if IPC failed AND editor is closed
if not use_ipc and editor_still_open:
    # Editor is open - don't mix IPC and file operations
    result = {'status': 'error', 'message': f'IPC failed but schematic editor is still open. Close the editor to use file-based operations, or investigate the IPC error: {ipc_error_msg}'}
elif not use_ipc and file_path and target_uuids:
    try:
        # File-based fallback functions
        import re, uuid, os

        def file_read(path):
            with open(path, 'r', encoding='utf-8') as f:
                return f.read()

        def file_write(path, content):
            # Create backup
            if os.path.exists(path):
                backup = path + '.bak'
                with open(path, 'r', encoding='utf-8') as f:
                    with open(backup, 'w', encoding='utf-8') as bf:
                        bf.write(f.read())
            with open(path, 'w', encoding='utf-8') as f:
                f.write(content)

        def delete_element_from_file(file_path, target_uuid):
            """Delete element by UUID from schematic file."""
            content = file_read(file_path)
            # Find element containing this UUID
            uuid_pos = content.find(target_uuid)
            if uuid_pos == -1:
                return False
            # Walk back to find element start
            depth = 0
            start = uuid_pos
            while start > 0:
                if content[start] == ')':
                    depth += 1
                elif content[start] == '(':
                    if depth == 0:
                        break
                    depth -= 1
                start -= 1
            # Walk forward to find element end
            depth = 1
            end = start + 1
            while end < len(content) and depth > 0:
                if content[end] == '(':
                    depth += 1
                elif content[end] == ')':
                    depth -= 1
                end += 1
            # Remove element and surrounding whitespace
            while end < len(content) and content[end] in ' \t\n':
                end += 1
            new_content = content[:start] + content[end:]
            file_write(file_path, new_content)
            return True

        deleted_count = 0
        for target_uuid in target_uuids:
            if delete_element_from_file(file_path, target_uuid):
                deleted_count += 1
        result = {'status': 'success', 'source': 'file', 'deleted': deleted_count}
    except Exception as file_error:
        result = {'status': 'error', 'message': f'File fallback failed (editor closed). File error: {str(file_error)}'}
elif not use_ipc:
    result = {'status': 'error', 'message': f'IPC failed: {ipc_error_msg}'}

print(json.dumps(result, indent=2))
