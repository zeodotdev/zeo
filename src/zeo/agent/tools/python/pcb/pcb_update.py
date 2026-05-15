import json
from kipy.geometry import Vector2, Angle
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.proto.board import board_types_pb2
from kipy.board_types import Net, Via

updates = TOOL_ARGS.get("updates", [])
if not updates:
    print(json.dumps({'status': 'error', 'message': 'updates array is required'}))
else:
    def mm_to_nm(mm):
        return round(mm * 1000000)

    # Layer name to BoardLayer enum mapping
    layer_map = {
        'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,
        'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,
        'In3.Cu': BoardLayer.BL_In3_Cu, 'In4.Cu': BoardLayer.BL_In4_Cu,
        'In5.Cu': BoardLayer.BL_In5_Cu, 'In6.Cu': BoardLayer.BL_In6_Cu,
        'F.SilkS': BoardLayer.BL_F_SilkS, 'B.SilkS': BoardLayer.BL_B_SilkS,
        'F.Mask': BoardLayer.BL_F_Mask, 'B.Mask': BoardLayer.BL_B_Mask,
        'Edge.Cuts': BoardLayer.BL_Edge_Cuts,
        'F.Fab': BoardLayer.BL_F_Fab, 'B.Fab': BoardLayer.BL_B_Fab,
        'F.CrtYd': BoardLayer.BL_F_CrtYd, 'B.CrtYd': BoardLayer.BL_B_CrtYd,
    }

    # Via type mapping
    via_type_map = {
        'through': board_types_pb2.ViaType.VT_THROUGH,
        'blind_buried': board_types_pb2.ViaType.VT_BLIND_BURIED,
        'blind': board_types_pb2.ViaType.VT_BLIND,
        'buried': board_types_pb2.ViaType.VT_BURIED,
        'micro': board_types_pb2.ViaType.VT_MICRO,
    }

    # Build ref->footprint map for reference lookups
    all_fps = board.get_footprints()
    ref_to_fp = {}
    for fp in all_fps:
        ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        if ref:
            ref_to_fp[ref] = fp

    # Build id->item map for UUID lookups
    id_to_item = {}
    for fp in all_fps:
        id_to_item[str(fp.id.value)] = ('footprint', fp)
    for item in board.get_tracks():
        id_to_item[str(item.id.value)] = ('track', item)
    for item in board.get_vias():
        id_to_item[str(item.id.value)] = ('via', item)
    for item in board.get_shapes():
        id_to_item[str(item.id.value)] = ('shape', item)
    for item in board.get_text():
        id_to_item[str(item.id.value)] = ('text', item)
    for item in board.get_zones():
        id_to_item[str(item.id.value)] = ('zone', item)

    # Index pads by uuid AND by (footprint_ref, pad_number) for lookup.
    # Pads are children of footprints — mutating a pad requires re-saving
    # the parent footprint via update_items([fp]).
    pad_uuid_to_pad = {}
    pad_uuid_to_fp = {}
    fp_pad_lookup = {}  # (fp_ref, pad_number) -> (pad, fp)
    for fp in all_fps:
        fp_ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else None
        for pad in fp.definition.pads:
            puid = str(pad.id.value)
            pad_uuid_to_pad[puid] = pad
            pad_uuid_to_fp[puid] = fp
            if fp_ref:
                fp_pad_lookup[(fp_ref, pad.number)] = (pad, fp)

    # Pending connector-pad set updates (batched into one IPC at end)
    connector_pads_to_add = []
    connector_pads_to_remove = []
    # Footprints that need update_items called once at the end (avoid double-sends)
    footprints_dirty = set()

    updated = []
    not_found = []
    errors = []

    for upd in updates:
        target = upd.get('target', '')
        target_type = upd.get('target_type', '')
        pad_uuid = upd.get('pad_uuid', '')
        footprint_ref = upd.get('footprint_ref', '')
        pad_number = upd.get('pad_number', '')

        # Pad target — mutates a pad inside a footprint. Use update_items()
        # on the parent footprint to persist; some updates (is_connector_pad)
        # also require the connector-pad set IPC.
        if target_type == 'pad' or pad_uuid or (footprint_ref and pad_number):
            pad = None
            parent_fp = None

            if pad_uuid and pad_uuid in pad_uuid_to_pad:
                pad = pad_uuid_to_pad[pad_uuid]
                parent_fp = pad_uuid_to_fp[pad_uuid]
            elif footprint_ref and pad_number is not None:
                key = (footprint_ref, str(pad_number))
                if key in fp_pad_lookup:
                    pad, parent_fp = fp_pad_lookup[key]

            if not pad:
                not_found.append(pad_uuid or f'{footprint_ref}.{pad_number}')
                continue

            try:
                pad_changed = False

                if 'net' in upd:
                    net_obj = Net()
                    net_obj.name = upd['net']
                    pad.net = net_obj
                    pad_changed = True

                if 'position' in upd and len(upd['position']) >= 2:
                    pad.position = Vector2.from_xy(mm_to_nm(upd['position'][0]),
                                                   mm_to_nm(upd['position'][1]))
                    pad_changed = True

                if 'is_connector_pad' in upd:
                    puid = str(pad.id.value)
                    if upd['is_connector_pad']:
                        connector_pads_to_add.append(puid)
                    else:
                        connector_pads_to_remove.append(puid)
                    # Marker change does not require update_items(); it goes
                    # via update_connector_pad_set() at the end.

                if pad_changed:
                    footprints_dirty.add(str(parent_fp.id.value))
                    # Stash the parent fp once for batch re-save below.
                    id_to_item.setdefault(str(parent_fp.id.value),
                                           ('footprint', parent_fp))
                    updated.append(pad_uuid or f'{footprint_ref}.{pad_number}')
                elif 'is_connector_pad' in upd:
                    # Marker-only change still counts as updated.
                    updated.append(pad_uuid or f'{footprint_ref}.{pad_number}')
            except Exception as e:
                errors.append({'target': pad_uuid or f'{footprint_ref}.{pad_number}',
                               'error': str(e)})
            continue

        if not target:
            errors.append({'error': 'missing target'})
            continue

        item = None
        is_footprint = False

        # Try as footprint reference first
        if target in ref_to_fp:
            item = ref_to_fp[target]
            is_footprint = True
        # Then try as UUID
        elif target in id_to_item:
            item_type, item = id_to_item[target]
            is_footprint = (item_type == 'footprint')

        if not item:
            not_found.append(target)
            continue

        try:
            changed = False

            # Reference (footprint property) rename — gap 5
            if is_footprint and 'properties' in upd \
                    and isinstance(upd['properties'], dict) \
                    and 'Reference' in upd['properties']:
                new_ref = upd['properties']['Reference']
                if hasattr(item, 'reference_field') and hasattr(item.reference_field, 'text'):
                    item.reference_field.text.value = new_ref
                    changed = True

            # Position update
            if 'position' in upd and len(upd['position']) >= 2:
                item.position = Vector2.from_xy(mm_to_nm(upd['position'][0]), mm_to_nm(upd['position'][1]))
                changed = True

            # Angle/rotation update (for footprints)
            if 'angle' in upd and is_footprint:
                item.orientation = Angle.from_degrees(upd['angle'])
                changed = True

            # Layer update
            if 'layer' in upd:
                layer_name = upd['layer']
                item.layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                changed = True

            # Locked update
            if 'locked' in upd:
                item.locked = upd['locked']
                changed = True

            # Width update (tracks, vias)
            if 'width' in upd and hasattr(item, 'width'):
                item.width = mm_to_nm(upd['width'])
                changed = True

            # Net update (tracks, vias, zones)
            if 'net' in upd and hasattr(item, 'net'):
                net_obj = Net()
                net_obj.name = upd['net']
                item.net = net_obj
                changed = True

            # Text/value update (text items)
            if 'text' in upd and hasattr(item, 'value'):
                item.value = upd['text']
                changed = True

            # Via diameter
            if 'diameter' in upd and hasattr(item, 'diameter'):
                item.diameter = mm_to_nm(upd['diameter'])
                changed = True

            # Via drill diameter
            if 'drill_diameter' in upd and hasattr(item, 'drill_diameter'):
                item.drill_diameter = mm_to_nm(upd['drill_diameter'])
                changed = True

            # Via type
            if 'via_type' in upd and hasattr(item, 'type'):
                item.type = via_type_map.get(upd['via_type'], board_types_pb2.ViaType.VT_THROUGH)
                changed = True

            # Via start/end layers
            if 'start_layer' in upd and hasattr(item, 'padstack'):
                item.padstack.drill.start_layer = layer_map.get(upd['start_layer'], BoardLayer.BL_F_Cu)
                changed = True

            if 'end_layer' in upd and hasattr(item, 'padstack'):
                item.padstack.drill.end_layer = layer_map.get(upd['end_layer'], BoardLayer.BL_B_Cu)
                changed = True

            # Track start/end points
            if 'start' in upd and hasattr(item, 'start') and len(upd['start']) >= 2:
                item.start = Vector2.from_xy(mm_to_nm(upd['start'][0]), mm_to_nm(upd['start'][1]))
                changed = True

            if 'end' in upd and hasattr(item, 'end') and len(upd['end']) >= 2:
                item.end = Vector2.from_xy(mm_to_nm(upd['end'][0]), mm_to_nm(upd['end'][1]))
                changed = True

            # Zone priority
            if 'priority' in upd and hasattr(item, 'priority'):
                item.priority = upd['priority']
                changed = True

            # Zone clearance
            if 'clearance' in upd and hasattr(item, 'clearance'):
                item.clearance = mm_to_nm(upd['clearance'])
                changed = True

            # Zone min thickness
            if 'min_thickness' in upd and hasattr(item, 'min_thickness'):
                item.min_thickness = mm_to_nm(upd['min_thickness'])
                changed = True

            # Rectangle top_left/bottom_right
            if 'top_left' in upd and hasattr(item, 'top_left') and len(upd['top_left']) >= 2:
                item.top_left = Vector2.from_xy(mm_to_nm(upd['top_left'][0]), mm_to_nm(upd['top_left'][1]))
                changed = True

            if 'bottom_right' in upd and hasattr(item, 'bottom_right') and len(upd['bottom_right']) >= 2:
                item.bottom_right = Vector2.from_xy(mm_to_nm(upd['bottom_right'][0]), mm_to_nm(upd['bottom_right'][1]))
                changed = True

            # Circle center/radius
            if 'center' in upd and hasattr(item, 'center') and len(upd['center']) >= 2:
                item.center = Vector2.from_xy(mm_to_nm(upd['center'][0]), mm_to_nm(upd['center'][1]))
                changed = True

            if 'radius' in upd and hasattr(item, 'radius_point') and hasattr(item, 'center'):
                # Set radius_point relative to center
                cx = item.center.x
                cy = item.center.y
                item.radius_point = Vector2.from_xy(cx + mm_to_nm(upd['radius']), cy)
                changed = True

            # Arc mid point
            if 'mid' in upd and hasattr(item, 'mid') and len(upd['mid']) >= 2:
                item.mid = Vector2.from_xy(mm_to_nm(upd['mid'][0]), mm_to_nm(upd['mid'][1]))
                changed = True

            # Keepout flags (zones with is_keepout=True)
            if 'no_copper' in upd and hasattr(item, 'keepout_copper'):
                item.keepout_copper = upd['no_copper']
                changed = True

            if 'no_vias' in upd and hasattr(item, 'keepout_vias'):
                item.keepout_vias = upd['no_vias']
                changed = True

            if 'no_tracks' in upd and hasattr(item, 'keepout_tracks'):
                item.keepout_tracks = upd['no_tracks']
                changed = True

            if changed:
                board.update_items([item])
                updated.append(target)

        except Exception as e:
            errors.append({'target': target, 'error': str(e)})

    # Flush batched footprint updates from pad mutations.
    if footprints_dirty:
        try:
            fps_to_save = [id_to_item[fp_uuid][1] for fp_uuid in footprints_dirty
                           if fp_uuid in id_to_item]
            if fps_to_save:
                board.update_items(fps_to_save)
        except Exception as e:
            errors.append({'error': f'pad-parent footprint save failed: {e}'})

    # Flush batched connector-pad set changes.
    connector_pad_changes = None
    if connector_pads_to_add or connector_pads_to_remove:
        try:
            added, removed = board.update_connector_pad_set(
                add=connector_pads_to_add or None,
                remove=connector_pads_to_remove or None
            )
            connector_pad_changes = {'added': added, 'removed': removed}
        except Exception as e:
            errors.append({'error': f'connector-pad set update failed: {e}'})

    status = 'success' if not errors and not not_found else 'partial'
    out = {'status': status, 'updated': updated, 'not_found': not_found, 'errors': errors}
    if connector_pad_changes is not None:
        out['connector_pad_changes'] = connector_pad_changes
    print(json.dumps(out, indent=2))
