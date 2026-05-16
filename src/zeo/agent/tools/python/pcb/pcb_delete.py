# Copyright (C) 2026, Zeo <team@zeo.dev>

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

    # Index pads by uuid AND by (footprint_ref, pad_number). Pads are not
    # top-level items; deleting one means removing it from the parent
    # footprint's items list and re-saving via update_items([fp]).
    pad_uuid_to_pad = {}
    pad_uuid_to_fp = {}
    fp_pad_lookup = {}
    for fp in all_fps:
        fp_ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        for pad in fp.definition.pads:
            puid = str(pad.id.value)
            pad_uuid_to_pad[puid] = pad
            pad_uuid_to_fp[puid] = fp
            if fp_ref:
                fp_pad_lookup[(fp_ref, pad.number)] = (pad, fp)

    items_to_delete = []
    pads_to_delete = []   # list of (pad_uuid, parent_fp, keep_marker)
    footprints_dirty = {}  # fp_uuid -> fp
    not_found = []
    pad_targets_seen = []

    for target in targets:
        # Pad target: dict with pad_uuid OR footprint_ref+pad_number, optionally
        # keep_connector_marker:true to leave the UUID in m_connectorPads.
        if isinstance(target, dict) and (
                target.get('target_type') == 'pad' or 'pad_uuid' in target
                or ('footprint_ref' in target and 'pad_number' in target)):
            pad_uuid = target.get('pad_uuid', '')
            footprint_ref = target.get('footprint_ref', '')
            pad_number = target.get('pad_number', '')
            keep_marker = bool(target.get('keep_connector_marker', False))

            pad = None
            parent_fp = None
            label = pad_uuid or f'{footprint_ref}.{pad_number}'

            if pad_uuid and pad_uuid in pad_uuid_to_pad:
                pad = pad_uuid_to_pad[pad_uuid]
                parent_fp = pad_uuid_to_fp[pad_uuid]
            elif footprint_ref and pad_number is not None:
                key = (footprint_ref, str(pad_number))
                if key in fp_pad_lookup:
                    pad, parent_fp = fp_pad_lookup[key]

            if not pad:
                not_found.append(label)
                continue

            pads_to_delete.append((str(pad.id.value), parent_fp, keep_marker, label))
            footprints_dirty[str(parent_fp.id.value)] = parent_fp
            pad_targets_seen.append(label)
            continue

        # Try as footprint reference first
        if target in ref_to_fp:
            items_to_delete.append(ref_to_fp[target])
        # Then try as UUID
        elif target in id_to_item:
            items_to_delete.append(id_to_item[target])
        else:
            not_found.append(target)

    deleted_count = 0
    pads_deleted = 0
    connector_pads_cleared = 0

    if items_to_delete:
        try:
            board.remove_items(items_to_delete)
            deleted_count = len(items_to_delete)
        except Exception as e:
            not_found.append(str(e))

    # Pad deletion is currently DISABLED at the tool layer.
    #
    # Two implementations attempted, both consistently crashed the editor
    # process with no clean breadcrumb:
    #   1. Mutating fp.definition._unwrapped_items + board.update_items(fp)
    #      → FOOTPRINT::Deserialize's Pads().clear() + re-add cycle hit a
    #      silent std::terminate partway through commit Push.
    #   2. board.remove_items([pad]) → DeleteItems IPC → BOARD_COMMIT::Push
    #      → silent process exit preceded by `wxFileName::Mkdir("")` log.
    #
    # The marker-clear side IS still honored — keep_connector_marker:false
    # entries clear the UUID from BOARD::m_connectorPads — so tests that
    # need to set up an "orphan-pad" or "marker-cleared" state still work
    # without triggering the unstable delete path. To actually remove the
    # pad, edit the .kicad_pcb directly via run_terminal or do it from
    # the editor UI.
    if pads_to_delete:
        marker_clears = [puid for puid, _, keep, _ in pads_to_delete if not keep]

        if marker_clears:
            try:
                _, removed = board.update_connector_pad_set(remove=marker_clears)
                connector_pads_cleared = removed
            except Exception as e:
                not_found.append(f'connector-pad clear failed: {e}')

        for puid, _, _, label in pads_to_delete:
            not_found.append({
                'stage': 'pad_delete',
                'target': label,
                'error': 'pad deletion via the IPC API is currently disabled '
                         '(known crash in BOARD_COMMIT::Push downstream of '
                         'remove_items). Workarounds: delete the pad in the '
                         'PCB editor UI, or edit the .kicad_pcb directly via '
                         'run_terminal. The connector-pad marker side WAS '
                         'still applied where keep_connector_marker:false.',
            })

    out = {
        'status': 'success' if not not_found else 'partial',
        'deleted': deleted_count,
        'not_found': not_found,
    }
    if pads_to_delete:
        # pads_deleted stays 0 — the IPC delete path is disabled (see above).
        out['pads_deleted'] = 0
        out['connector_pads_cleared'] = connector_pads_cleared
        out['pads_disabled_reason'] = (
            'pad deletion via the IPC API is currently disabled — '
            'see not_found[].error entries with stage:"pad_delete"'
        )
    print(json.dumps(out, indent=2))

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
