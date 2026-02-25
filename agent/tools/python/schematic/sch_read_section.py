import json, sys, re

refresh_or_fail(sch)

section = TOOL_ARGS.get("section", "all")
filter_str = TOOL_ARGS.get("filter", "")

def matches_filter(item, filter_str):
    if not filter_str:
        return True
    # Check UUID match
    item_uuid = str(item.id.value) if hasattr(item, 'id') else ''
    if filter_str == item_uuid:
        return True
    # Check reference match (for symbols)
    ref = getattr(item, 'reference', '')
    if ref:
        pattern = filter_str.replace('*', '.*').replace('?', '.')
        if re.match(f'^{pattern}$', ref, re.IGNORECASE):
            return True
    return False

try:
    result = {'section': section}

    if section in ('symbols', 'all'):
        symbols = sch.symbols.get_all()
        symbol_data = []
        for sym in symbols:
            if not matches_filter(sym, filter_str):
                continue
            lib_id_str = get_lib_id_str(sym)
            pins = []
            if hasattr(sym, 'pins'):
                # Use batch API for efficiency
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
                        if not abs_pos or (abs_pos[0] == 0 and abs_pos[1] == 0):
                            pin_pos = get_pos(getattr(pin, 'position', None))
                            if pin_pos and (pin_pos[0] != 0 or pin_pos[1] != 0):
                                abs_pos = pin_pos
                        if abs_pos:
                            pins.append({'number': pin.number, 'name': getattr(pin, 'name', ''), 'pos': abs_pos})
                    except:
                        pass
            sym_dict = {
                'uuid': get_uuid_str(sym),
                'lib_id': lib_id_str,
                'ref': sym.reference if hasattr(sym, 'reference') else '',
                'value': sym.value if hasattr(sym, 'value') else '',
                'footprint': getattr(sym, 'footprint', ''),
                'pos': get_pos(getattr(sym, 'position', None)),
                'angle': getattr(sym, 'angle', 0),
                'unit': getattr(sym, 'unit', 1),
                'pins': pins
            }
            symbol_data.append(sym_dict)
        result['symbols'] = symbol_data

    if section in ('wires', 'all'):
        wires = sch.crud.get_wires()
        result['wires'] = [{'uuid': get_uuid_str(w), 'start': get_pos(getattr(w, 'start', None)), 'end': get_pos(getattr(w, 'end', None))} for w in wires]

    if section in ('junctions', 'all'):
        junctions = sch.crud.get_junctions()
        result['junctions'] = [{'uuid': get_uuid_str(j), 'pos': get_pos(getattr(j, 'position', None))} for j in junctions]

    if section in ('labels', 'all'):
        labels = sch.labels.get_all()
        result['labels'] = [{'uuid': get_uuid_str(l), 'text': l.text if hasattr(l, 'text') else '', 'pos': get_pos(getattr(l, 'position', None)), 'type': type(l).__name__} for l in labels]

    if section in ('sheets', 'all'):
        sheets = sch.crud.get_sheets()
        result['sheets'] = [{'uuid': get_uuid_str(s), 'name': s.name if hasattr(s, 'name') else '', 'file': s.filename if hasattr(s, 'filename') else ''} for s in sheets]

    if section == 'header':
        doc = sch.document
        result['header'] = {
            'version': getattr(doc, 'version', 0),
            'uuid': str(doc.uuid) if hasattr(doc, 'uuid') else '',
            'paper': getattr(doc, 'paper', ''),
            'title': getattr(doc, 'title', '')
        }

    print(json.dumps(result, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))
