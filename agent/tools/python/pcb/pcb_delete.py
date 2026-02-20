import json
from kipy.proto.board.board_types_pb2 import BoardLayer

targets = TOOL_ARGS.get("targets", None)
query = TOOL_ARGS.get("query", None)

if targets is not None:
    # Build ref->footprint map for reference lookups
    all_fps = board.get_footprints()
    ref_to_fp = {}
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if ref:
            ref_to_fp[ref] = fp

    # Build id->item map for UUID lookups (shapes, text, tracks, vias, zones)
    id_to_item = {}
    for fp in all_fps:
        id_to_item[str(fp.id.value)] = fp
    for item in board.get_shapes():
        id_to_item[str(item.id.value)] = item
    for item in board.get_text():
        id_to_item[str(item.id.value)] = item
    for item in board.get_tracks():
        id_to_item[str(item.id.value)] = item
    for item in board.get_vias():
        id_to_item[str(item.id.value)] = item
    for item in board.get_zones():
        id_to_item[str(item.id.value)] = item

    items_to_delete = []
    not_found = []

    for target in targets:
        # Try as footprint reference first
        if target in ref_to_fp:
            items_to_delete.append(ref_to_fp[target])
        # Then try as UUID
        elif target in id_to_item:
            items_to_delete.append(id_to_item[target])
        else:
            not_found.append(target)

    deleted_count = 0
    if items_to_delete:
        try:
            board.remove_items(items_to_delete)
            deleted_count = len(items_to_delete)
        except Exception as e:
            not_found.append(str(e))

    print(json.dumps({'status': 'success', 'deleted': deleted_count, 'not_found': not_found}, indent=2))

elif query is not None and isinstance(query, dict):
    # Delete by query
    query_layer = query.get('layer', '')
    query_type = query.get('type', '')
    query_net = query.get('net', '')

    if query_type == 'track':
        items = board.get_tracks()
    elif query_type == 'via':
        items = board.get_vias()
    elif query_type == 'zone':
        items = board.get_zones()
    elif query_type in ('shape', 'line', 'rectangle', 'circle'):
        items = board.get_shapes()
    elif query_type == 'text':
        items = board.get_text()
    elif query_type == 'footprint':
        items = board.get_footprints()
    else:
        items = []

    items_to_delete = []
    for item in items:
        if query_layer and hasattr(item, 'layer') and BoardLayer.Name(item.layer) != query_layer:
            continue
        if query_net and hasattr(item, 'net') and item.net.name != query_net:
            continue
        items_to_delete.append(item)

    deleted_count = 0
    if items_to_delete:
        board.remove_items(items_to_delete)
        deleted_count = len(items_to_delete)

    print(json.dumps({'status': 'success', 'deleted': deleted_count}, indent=2))

else:
    print(json.dumps({'status': 'error', 'message': 'targets array or query object required'}))
