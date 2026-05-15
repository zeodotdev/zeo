import json
import fnmatch

filter_pattern = TOOL_ARGS.get("filter", "")
include_pads = TOOL_ARGS.get("include_pads", True)
unrouted_only = TOOL_ARGS.get("unrouted_only", False)

# Get all nets
nets = board.get_nets()
result_nets = []

# Build pad map if needed
net_pads = {}
if include_pads:
    # Build net->pads map
    all_fps = board.get_footprints()
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'
        if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):
            for pad in fp.definition.pads:
                net_name = pad.net.name if hasattr(pad, 'net') else ''
                if net_name:
                    if net_name not in net_pads:
                        net_pads[net_name] = []
                    net_pads[net_name].append({'ref': ref, 'pad': str(pad.number)})

# Get actual routing status from KiCad connectivity engine
unrouted_info = {}  # net_name -> {routed, unrouted, is_complete}
try:
    unrouted_nets = board.connectivity.get_unrouted_nets()
    for info in unrouted_nets:
        unrouted_info[info.net_name] = {
            'routed_connections': info.routed_connections,
            'unrouted_connections': info.unrouted_connections,
            'is_complete': info.is_complete
        }
except Exception as e:
    # Fallback if connectivity API unavailable
    pass

for net in nets:
    if filter_pattern and not fnmatch.fnmatch(net.name, filter_pattern):
        continue

    if unrouted_only:
        # Skip nets that are fully routed (or have < 2 pads)
        conn_info = unrouted_info.get(net.name)
        if conn_info and conn_info['is_complete']:
            continue
        if include_pads:
            pads = net_pads.get(net.name, [])
            if len(pads) < 2:
                continue

    net_info = {'name': net.name}

    if include_pads:
        net_info['pads'] = net_pads.get(net.name, [])

    # Add routing status from connectivity engine
    conn_info = unrouted_info.get(net.name)
    if conn_info:
        net_info['routed_connections'] = conn_info['routed_connections']
        net_info['unrouted_connections'] = conn_info['unrouted_connections']
        net_info['is_complete'] = conn_info['is_complete']
    else:
        # Net not in unrouted list means it's complete (or single pad)
        net_info['is_complete'] = True
        net_info['unrouted_connections'] = 0
    result_nets.append(net_info)

print(json.dumps({'status': 'success', 'nets': result_nets}, indent=2))
