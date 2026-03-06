import json, sys, fnmatch

refresh_or_fail(sch)

filter_pattern = TOOL_ARGS.get("filter", "")
include_unconnected = TOOL_ARGS.get("include_unconnected", False)

try:
    # Build pin UUID map: uuid_str -> (ref, pin_num, pin_name)
    symbols = sch.symbols.get_all()
    pin_uuid_map = {}
    for sym in symbols:
        ref = sym.reference if hasattr(sym, 'reference') else ''
        if hasattr(sym, 'pins'):
            for pin in sym.pins:
                uuid = pin._proto.id.value
                if uuid:
                    pin_uuid_map[uuid] = (ref, pin.number, getattr(pin, 'name', ''))

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
        # Skip internal/unnamed nets unless filter matches
        if not filter_pattern:
            if net.name.startswith('unconnected-') or net.name.startswith('Net-('):
                continue
        elif not fnmatch.fnmatch(net.name, filter_pattern):
            continue

        pins = []
        net_labels = set()

        for item_id in net.item_ids:
            uuid = item_id.value
            if uuid in pin_uuid_map:
                ref, pin_num, pin_name = pin_uuid_map[uuid]
                pins.append({'ref': ref, 'pin': pin_num, 'name': pin_name})
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
        for uuid, (ref, pin_num, pin_name) in pin_uuid_map.items():
            if uuid not in connected_pin_uuids and not ref.startswith('#PWR'):
                unconnected.append({'ref': ref, 'pin': pin_num, 'name': pin_name})
        if unconnected:
            output['unconnected_pins'] = unconnected

    print(json.dumps(output, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))
