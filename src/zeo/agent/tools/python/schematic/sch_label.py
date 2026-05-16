# Copyright (C) 2026, Zeo <team@zeo.dev>

import json
from kipy.geometry import Vector2
from kipy.proto.common.types.enums_pb2 import HA_LEFT, HA_RIGHT, VA_TOP, VA_BOTTOM
from kipy.schematic_types import LocalLabel, GlobalLabel, HierarchicalLabel

refresh_or_fail(sch)

ref = TOOL_ARGS.get("ref", "")
label_type = TOOL_ARGS.get("label_type", "local")
pin_labels = TOOL_ARGS.get("labels", {})
unit_filter = TOOL_ARGS.get("unit", None)  # Filter pins to specific unit (None = all pins)

# MBS detection: when target.doc_type:"mbs" routed through this tool, the
# bootstrap aliased sch=mbs. Module blocks are SCH_MODULE_BLOCK_T items
# (not regular schematic symbols) so the symbol-ref lookup below would
# miss them. Detect via DocumentType and resolve via mbs.multi_board.
_IS_MBS = False
try:
    from kipy.proto.common.types import DocumentType as _DocumentType
    _IS_MBS = sch._doc.type == _DocumentType.DOCTYPE_MBS_SCHEMATIC
except Exception:
    pass

def build_label(text, position, h_align, v_align, rotation=0):
    """Build a label proto in memory (no IPC call)."""
    if label_type == 'global':
        lbl = GlobalLabel.create(position, text)
    elif label_type == 'hierarchical':
        lbl = HierarchicalLabel.create(position, text)
    else:
        lbl = LocalLabel.create(position, text)
    lbl._proto.text.attributes.horizontal_alignment = h_align
    lbl._proto.text.attributes.vertical_alignment = v_align
    if rotation != 0:
        lbl._proto.text.attributes.angle.value_degrees = rotation
    return lbl

# Resolve ref as MBS module block (if MBS doc), then symbol, then sheet.
results = []
labels_to_create = []  # Collect all label protos for batch creation
_is_sheet = False
_is_mbs_block = False
sym = None
sheet = None
mbs_block = None  # SCH_MODULE_BLOCK proto when matched on MBS canvas

if _IS_MBS:
    try:
        # On MBS, ref typically matches the MBS-scoped annotation ('B1'..'B12')
        # set by mbs_refresh. Also accept matching component_ref ('CN1', 'J1')
        # as a fallback so the model can use whichever it has handy.
        #
        # Disambiguation: when two sub-projects own components with the same
        # ref (e.g. CN1 on both esp_cm and esp_cm_breakout), a bare ref maps
        # to multiple blocks. Two ways to disambiguate:
        #   - target.sub_project_uuid: filter to a specific sub-project
        #   - block_uuid: name the block directly (always unique)
        # If neither is provided and >1 blocks match, error out with the full
        # candidate list rather than silently picking one — silent picks have
        # caused agent edits to land on the wrong board.
        _target = TOOL_ARGS.get('target', {}) if isinstance(TOOL_ARGS, dict) else {}

        if not isinstance(_target, dict):
            _target = {}

        _filter_sub_uuid = _target.get('sub_project_uuid', '') or ''
        _explicit_block_uuid = TOOL_ARGS.get('block_uuid', '') if isinstance(TOOL_ARGS, dict) else ''

        def _block_uuid(_b):
            try:
                return _b.id.value
            except Exception:
                return ''

        _matches = []

        for _b in sch.multi_board.get_blocks():
            # Explicit block_uuid wins immediately and bypasses ref matching.
            if _explicit_block_uuid:
                if _block_uuid(_b) == _explicit_block_uuid:
                    _matches = [_b]
                    break
                continue

            if getattr(_b, 'mbs_reference', '') != ref \
                    and getattr(_b, 'component_ref', '') != ref:
                continue

            if _filter_sub_uuid \
                    and getattr(_b, 'sub_project_uuid', '') != _filter_sub_uuid:
                continue

            _matches.append(_b)

        if len(_matches) > 1:
            _candidates = [
                {
                    'mbs_reference': getattr(_m, 'mbs_reference', '') or '(empty)',
                    'component_ref': getattr(_m, 'component_ref', ''),
                    'sub_project_uuid': getattr(_m, 'sub_project_uuid', ''),
                    'display_name': getattr(_m, 'display_name', ''),
                    'block_uuid': _block_uuid(_m),
                }
                for _m in _matches
            ]
            results = [{
                'error': (
                    f"Ambiguous ref '{ref}' on MBS canvas: {len(_matches)} blocks match. "
                    f"Disambiguate with target.sub_project_uuid (the uuid of the "
                    f"sub-project that owns the intended block) or block_uuid (always "
                    f"unique). If a candidate's mbs_reference is '(empty)', save the "
                    f"MBS once to backfill it (auto-assigned at save) and re-run."
                ),
                'candidates': _candidates,
            }]
        elif len(_matches) == 1:
            mbs_block = _matches[0]
            _is_mbs_block = True
        elif _explicit_block_uuid:
            results = [{
                'error': (
                    f"block_uuid '{_explicit_block_uuid}' did not match any block "
                    f"on the MBS canvas. Use mbs_inspect (section=\"blocks\") to "
                    f"list valid block UUIDs."
                )
            }]
        # else: zero matches without explicit uuid → fall through to symbol/sheet lookup.
    except Exception as _e:
        tool_log(f'[sch_label] MBS block lookup failed: {_e}')

# Symbol / sheet lookups don't apply when a module block already matched.
# When unit is specified, we need to find the specific symbol instance with that unit
# because multiple units of the same ref can be on the same sheet (e.g., U1 units 10-14)
if not _is_mbs_block:
    if unit_filter is not None:
        # Find all symbols with this ref and select the one with matching unit
        all_symbols = sch.symbols.get_all()
        matching_symbols = [s for s in all_symbols if getattr(s, 'reference', '') == ref]
        for s in matching_symbols:
            if getattr(s, 'unit', 1) == unit_filter:
                sym = s
                break
        if not sym and matching_symbols:
            # Unit not found - report available units
            available_units = sorted(set(getattr(s, 'unit', 1) for s in matching_symbols))
            results = [{'error': f'Unit {unit_filter} of {ref} not found on this sheet. Available units: {available_units}'}]
    else:
        try:
            sym = sch.symbols.get_by_ref(ref)
            if not sym:
                raise ValueError('not found')
        except Exception:
            sym = None

    if not sym and not results:
        for _s in sch.crud.get_sheets():
            if _s.name == ref:
                sheet = _s
                _is_sheet = True
                break

    if not sym and not sheet and not results:
        if _IS_MBS:
            # On MBS the only valid refs are module-block annotations (B1..)
            # or component_refs (CN1, J1). Surface a clearer error so the
            # model can correct.
            results = [{'error': f'No module block found on MBS matching ref \'{ref}\'. '
                                 f'Use mbs_inspect (section=\"blocks\") to see valid '
                                 f'mbs_reference / component_ref values.'}]
        else:
            results = [{'error': f'No symbol or sheet found matching: {ref}'}]

# --- Dispatch phase (independent of lookup) ---
# Each branch handles one resolution kind. `results` may already contain
# errors from the lookup phase; in that case all branches fall through.
if _is_mbs_block:
    # MBS module block branch — pins are addressed by pin_number, not name.
    # Mirrors the sheet-pin alignment logic since MBS pins use the same
    # SHEET_SIDE enum (1=LEFT, 2=RIGHT, 3=TOP, 4=BOTTOM).
    _pin_by_number = {p.pin_number: p for p in mbs_block.pins}

    for pin_id, label_text in pin_labels.items():
        try:
            _pin = _pin_by_number.get(str(pin_id))
            if not _pin:
                results.append({'pin': pin_id, 'label': label_text,
                                'error': f'MBS block pin not found: {pin_id}'})
                continue

            px = _pin.position.x_nm / 1_000_000
            py = _pin.position.y_nm / 1_000_000
            _side = _pin.side

            rotation = 0
            if _side == 1:    # LEFT side — label flows leftward
                h_align, v_align = HA_RIGHT, VA_BOTTOM
                direction = 'left'
            elif _side == 2:  # RIGHT side — label flows rightward
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'
            elif _side == 3:  # TOP side
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'up'
                rotation = 90
            elif _side == 4:  # BOTTOM side
                h_align, v_align = HA_LEFT, VA_TOP
                direction = 'down'
                rotation = 270
            else:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'

            label_pos = Vector2.from_xy_mm(px, py)
            lbl = build_label(label_text, label_pos, h_align, v_align, rotation)
            labels_to_create.append(lbl)
            results.append({'pin': pin_id, 'label': label_text,
                            'position': [round(px, 2), round(py, 2)],
                            'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

elif _is_sheet:
    _pin_by_name = {p.name: p for p in sheet.pins}

    for pin_id, label_text in pin_labels.items():
        try:
            _pin = _pin_by_name.get(pin_id)
            if not _pin:
                results.append({'pin': pin_id, 'label': label_text, 'error': f'Sheet pin not found: {pin_id}'})
                continue

            px = _pin.position.x / 1_000_000
            py = _pin.position.y / 1_000_000
            _side = _pin.side

            # Sheet pin side -> label alignment (already world-space)
            rotation = 0
            if _side == 1:
                h_align, v_align = HA_RIGHT, VA_BOTTOM
                direction = 'left'
            elif _side == 2:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'
            elif _side == 3:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'up'
                rotation = 90
            elif _side == 4:
                h_align, v_align = HA_LEFT, VA_TOP
                direction = 'down'
                rotation = 270
            else:
                h_align, v_align = HA_LEFT, VA_BOTTOM
                direction = 'right'

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            lbl = build_label(label_text, label_pos, h_align, v_align, rotation)
            labels_to_create.append(lbl)
            results.append({'pin': pin_id, 'label': label_text, 'position': [round(lbl_x, 2), round(lbl_y, 2)], 'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

elif sym:
    # Get library info for pin unit assignments if unit filter is specified
    lib_pin_units = {}
    if unit_filter is not None:
        try:
            lib_id = get_lib_id_str(sym)
            lib_sym = sch.library.get_symbol_info(lib_id)
            if lib_sym:
                lib_pin_units = {p.number: getattr(p, 'unit', 0) for p in getattr(lib_sym, 'pins', [])}
        except Exception:
            pass

    # Orientation transform: apply symbol rotation and mirroring
    _rot90 = {0: 2, 1: 3, 2: 1, 3: 0}
    _rot_steps = round(getattr(sym, 'angle', 0) / 90) % 4

    def transform_orientation(orient):
        o = orient
        for _ in range(_rot_steps):
            o = _rot90.get(o, o)
        if getattr(sym, 'mirror_x', False):
            if o == 2: o = 3
            elif o == 3: o = 2
        if getattr(sym, 'mirror_y', False):
            if o == 0: o = 1
            elif o == 1: o = 0
        return o

    for pin_id, label_text in pin_labels.items():
        # Filter by unit if requested (pin_unit 0 = shared, always include)
        if unit_filter is not None and lib_pin_units:
            pin_unit = lib_pin_units.get(pin_id, 0)
            if pin_unit != 0 and pin_unit != unit_filter:
                results.append({'pin': pin_id, 'label': label_text, 'skipped': True, 'reason': f'Pin belongs to unit {pin_unit}, not unit {unit_filter}'})
                continue
        try:
            pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)
            if not pin_result:
                pin_pos = sch.symbols.get_pin_position(sym, pin_id)
                if not pin_pos:
                    results.append({'pin': pin_id, 'label': label_text, 'error': f'Pin not found: {pin_id}'})
                    continue
                px = pin_pos.x / 1_000_000
                py = pin_pos.y / 1_000_000
                orient = None
            else:
                px = pin_result['position'].x / 1_000_000
                py = pin_result['position'].y / 1_000_000
                orient = pin_result.get('orientation', None)

            out_dx, out_dy = 0, 0
            rotation = 0
            if orient is not None:
                orient = transform_orientation(orient)
                if orient == 0:    # escape left
                    h_align, v_align = HA_RIGHT, VA_BOTTOM
                    direction = 'left'
                    out_dx, out_dy = -1, 0
                elif orient == 1:  # escape right
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
                elif orient == 2:  # escape down (vertical pin)
                    h_align, v_align = HA_LEFT, VA_TOP
                    direction = 'down'
                    out_dx, out_dy = 0, 1
                    rotation = 270
                elif orient == 3:  # escape up (vertical pin)
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'up'
                    out_dx, out_dy = 0, -1
                    rotation = 90
                else:
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
            else:
                sym_cx = sym.position.x / 1_000_000
                sym_cy = sym.position.y / 1_000_000
                if px > sym_cx:
                    h_align, v_align = HA_LEFT, VA_BOTTOM
                    direction = 'right'
                    out_dx, out_dy = 1, 0
                else:
                    h_align, v_align = HA_RIGHT, VA_BOTTOM
                    direction = 'left'
                    out_dx, out_dy = -1, 0

            lbl_x, lbl_y = px, py
            label_pos = Vector2.from_xy_mm(lbl_x, lbl_y)
            lbl = build_label(label_text, label_pos, h_align, v_align, rotation)
            labels_to_create.append(lbl)
            results.append({'pin': pin_id, 'label': label_text, 'position': [round(lbl_x, 2), round(lbl_y, 2)], 'direction': direction})

        except Exception as e:
            results.append({'pin': pin_id, 'label': label_text, 'error': str(e)})

# Batch-create all labels in a single IPC call
created_items = []
if labels_to_create:
    created_items = sch.crud.create_items(labels_to_create) or []

# Attach UUIDs from created items to results
if created_items:
    success_results = [r for r in results if 'error' not in r]
    for i, item in enumerate(created_items):
        if i < len(success_results):
            uid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', ''))
            if uid:
                success_results[i]['uuid'] = uid

# Auto-sync sheet pins when placing hierarchical labels
if label_type == 'hierarchical':
    try:
        sch.sheets.sync_pins()
    except Exception as _e:
        tool_log(f'[sch_label] sheet pin sync failed: {_e}')

print(json.dumps({'status': 'success', 'ref': ref, 'labels_placed': len([r for r in results if 'error' not in r and 'skipped' not in r]), 'labels_skipped': len([r for r in results if r.get('skipped')]), 'labels_failed': len([r for r in results if 'error' in r]), 'results': results}, indent=2))
