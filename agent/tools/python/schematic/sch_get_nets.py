import json, sys, fnmatch

refresh_or_fail(sch)

filter_pattern = TOOL_ARGS.get("filter", "")
include_unconnected = TOOL_ARGS.get("include_unconnected", False)
unit_filter = TOOL_ARGS.get("unit", None)  # Filter pins to specific unit (None = all pins)

try:
    # Cache library symbol info for unit counts and pin unit assignments
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

    # Build pin UUID map: uuid_str -> (ref, pin_num, pin_name, sym_unit, pin_unit)
    symbols = sch.symbols.get_all()
    pin_uuid_map = {}
    for sym in symbols:
        ref = sym.reference if hasattr(sym, 'reference') else ''
        sym_unit = getattr(sym, 'unit', 1)  # Which unit this symbol instance is
        lib_id = get_lib_id_str(sym)
        lib_info = get_lib_info(lib_id)
        pin_units = lib_info['pin_units']

        if hasattr(sym, 'pins'):
            for pin in sym.pins:
                uuid = pin._proto.id.value
                if uuid:
                    pin_unit = pin_units.get(pin.number, 0)  # 0 = shared across units
                    pin_uuid_map[uuid] = (ref, pin.number, getattr(pin, 'name', ''), sym_unit, pin_unit)

    # Build label UUID map: uuid_str -> label_text
    labels = sch.labels.get_all()
    label_uuid_map = {}
    for lbl in labels:
        if hasattr(lbl, 'id'):
            uuid = str(lbl.id.value)
            if uuid:
                label_uuid_map[uuid] = lbl.text if hasattr(lbl, 'text') else ''

    # Get all nets from KiCad's connection graph
    nets_resp = sch.connectivity.get_nets()

    result_nets = []
    connected_pin_uuids = set()

    for net in nets_resp.nets:
        # Apply filter if provided
        if filter_pattern and not fnmatch.fnmatch(net.name, filter_pattern):
            continue
        # Always skip truly unconnected stub nets (no useful info)
        if net.name.startswith('unconnected-'):
            continue

        pins = []
        net_labels = set()

        for item_id in net.item_ids:
            uuid = item_id.value
            if uuid in pin_uuid_map:
                ref, pin_num, pin_name, sym_unit, pin_unit = pin_uuid_map[uuid]

                # Filter by unit if requested (pin_unit 0 = shared, always include)
                if unit_filter is not None:
                    if pin_unit != 0 and pin_unit != unit_filter:
                        continue

                pin_data = {'ref': ref, 'pin': pin_num, 'name': pin_name, 'sym_unit': sym_unit, 'pin_unit': pin_unit}
                pins.append(pin_data)
                connected_pin_uuids.add(uuid)
            if uuid in label_uuid_map:
                net_labels.add(label_uuid_map[uuid])

        if pins:  # only include nets that connect to at least one symbol pin
            net_info = {'name': net.name, 'pins': pins}
            if net_labels:
                net_info['labels'] = sorted(net_labels)
            result_nets.append(net_info)

    output = {'nets': result_nets}

    # Optionally find unconnected pins
    if include_unconnected:
        unconnected = []
        for uuid, (ref, pin_num, pin_name, sym_unit, pin_unit) in pin_uuid_map.items():
            if uuid not in connected_pin_uuids and not ref.startswith('#PWR'):
                # Filter by unit if requested (pin_unit 0 = shared, always include)
                if unit_filter is not None:
                    if pin_unit != 0 and pin_unit != unit_filter:
                        continue
                unconnected.append({'ref': ref, 'pin': pin_num, 'name': pin_name, 'sym_unit': sym_unit, 'pin_unit': pin_unit})
        if unconnected:
            output['unconnected_pins'] = unconnected

    print(json.dumps(output, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))
