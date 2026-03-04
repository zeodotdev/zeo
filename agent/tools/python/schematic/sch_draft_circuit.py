import json, sys, re, math
from kipy.geometry import Vector2
from kipy.proto.common.types.enums_pb2 import HA_LEFT, HA_RIGHT, VA_TOP, VA_BOTTOM

refresh_or_fail(sch)

# Build map of used references for auto-numbering
used_refs = {}
for _s in sch.symbols.get_all():
    _r = getattr(_s, 'reference', '')
    _m = re.match(r'^([A-Za-z#]+)(\d+)$', _r)
    if _m:
        used_refs.setdefault(_m.group(1), set()).add(int(_m.group(2)))

def next_ref(prefix):
    nums = used_refs.get(prefix, set())
    n = 1
    while n in nums:
        n += 1
    used_refs.setdefault(prefix, set()).add(n)
    return f'{prefix}{n}'

# Agent_Wiring field constants
AGENT_WIRING_FIELD = "Agent_Wiring"
WIRING_ARROW = "→"
WIRING_SEPARATOR = "; "

results = []
placed_items = {}  # temp_id -> { 'sym': symbol, 'ref': reference, 'type': 'symbol'|'power' }
_power_temp_ids = set()  # Track which temp_ids are power symbols

# ---------------------------------------------------------------------------
# Parse connections into per-symbol wiring entries
# ---------------------------------------------------------------------------
# connections format: [["mcu:PA0", "r1:2"], ["vcc1:1", "c1:1"], ...]
# Result: { temp_id: { pin: target_ref:pin | net_name } }
#
# We store wiring recommendations on BOTH ends of each connection, so that:
# - Regular symbols get Agent_Wiring fields showing all their connections
# - Power symbol connections are stored on the non-power end

connections = TOOL_ARGS.get("connections", [])
wiring_map = {}  # temp_id -> { pin -> target }
_debug_info = []  # Debug trace for troubleshooting

_debug_info.append(f"connections input: {connections}")

# First pass: collect all power symbol temp_ids from power_symbols array
_power_symbol_ids = set()
for pwr in TOOL_ARGS.get("power_symbols", []):
    _power_symbol_ids.add(pwr.get("id", ""))

_debug_info.append(f"power_symbol_ids: {_power_symbol_ids}")

for conn in connections:
    if not isinstance(conn, (list, tuple)) or len(conn) != 2:
        continue
    source_str, target_str = conn[0], conn[1]

    # Parse both endpoints
    if ':' not in source_str or ':' not in target_str:
        continue
    src_id, src_pin = source_str.rsplit(':', 1)
    tgt_id, tgt_pin = target_str.rsplit(':', 1)

    # Add to source's wiring map (if source is not a power symbol)
    if src_id not in _power_symbol_ids:
        wiring_map.setdefault(src_id, {})[src_pin] = target_str

    # Add to target's wiring map (if target is not a power symbol)
    # This ensures connections FROM power symbols are recorded on the component
    if tgt_id not in _power_symbol_ids:
        wiring_map.setdefault(tgt_id, {})[tgt_pin] = source_str

_debug_info.append(f"wiring_map after parsing: {wiring_map}")

# ---------------------------------------------------------------------------
# Collect bounding boxes of all existing symbols for overlap detection
# ---------------------------------------------------------------------------
placed_bboxes = []
try:
    _all_existing = sch.symbols.get_all()
    for _esym in _all_existing:
        try:
            _ebb = sch.transform.get_bounding_box(_esym, units='mm', include_text=False)
        except:
            continue
        if _ebb:
            placed_bboxes.append({
                'ref': getattr(_esym, 'reference', '?'),
                'min_x': _ebb['min_x'] - _BBOX_MARGIN,
                'max_x': _ebb['max_x'] + _BBOX_MARGIN,
                'min_y': _ebb['min_y'] - _BBOX_MARGIN,
                'max_y': _ebb['max_y'] + _BBOX_MARGIN
            })
except:
    pass

def _bboxes_overlap(a, b):
    return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']

# Get sheet dimensions
_sheet_w, _sheet_h = 297.0, 210.0
try:
    _page = sch.page.get_settings()
    _sheet_w = _page.width_mm
    _sheet_h = _page.height_mm
except:
    pass

class _OOB(Exception): pass

def _check_bounds(x, y, idx):
    if not (0 <= x <= _sheet_w and 0 <= y <= _sheet_h):
        results.append({'index': idx, 'error': f'Position ({x}, {y}) is outside sheet ({_sheet_w}x{_sheet_h}mm)'})
        raise _OOB()


def build_agent_wiring_field(temp_id, wiring_entries, id_to_ref, power_ids):
    """Build Agent_Wiring field value from wiring entries.

    Args:
        temp_id: The temporary ID of the source symbol
        wiring_entries: Dict of { pin -> target_str }
        id_to_ref: Dict mapping temp_id to actual reference name
        power_ids: Set of temp_ids that are power symbols

    Returns:
        String like "1→VCC; 2→U1:PA0" or empty string
    """
    if not wiring_entries:
        return ""

    entries = []
    for pin, target_str in wiring_entries.items():
        # Resolve target temp_id to actual reference
        if ':' in target_str:
            target_id, target_pin = target_str.rsplit(':', 1)
            if target_id in id_to_ref:
                if target_id in power_ids:
                    # Power symbol - just use net name (e.g., "VCC" not "VCC:1")
                    resolved_target = id_to_ref[target_id]
                else:
                    # Regular symbol - include pin reference (e.g., "U1:PA0")
                    resolved_target = f"{id_to_ref[target_id]}:{target_pin}"
            else:
                # Keep original (might be a net name like "VCC:1" which is malformed, or unresolved ref)
                resolved_target = target_str
        else:
            # Net name without pin (e.g., "VCC")
            resolved_target = target_str

        entries.append(f"{pin}{WIRING_ARROW}{resolved_target}")

    return WIRING_SEPARATOR.join(entries)


# ---------------------------------------------------------------------------
# Phase 1: Place all symbols and power symbols first (to get references)
# ---------------------------------------------------------------------------

symbols = TOOL_ARGS.get("symbols", [])
power_symbols = TOOL_ARGS.get("power_symbols", [])

# First pass: place symbols without Agent_Wiring (we need refs first)
_temp_to_sym = {}  # temp_id -> placed symbol object
_temp_to_ref = {}  # temp_id -> reference string

try:
    for i, elem in enumerate(symbols):
        lib_id = elem.get("lib_id", "")
        temp_id = elem.get("id", f"sym_{i}")
        pos = elem.get("position", [0, 0])
        pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
        pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0
        angle = elem.get("angle", 0.0)
        mirror = elem.get("mirror", "none")
        mirror_x = (mirror == "x")
        mirror_y = (mirror == "y")
        unit = elem.get("unit", 1)

        try:
            _check_bounds(pos_x, pos_y, i)

            sym = sch.symbols.add(
                lib_id=lib_id,
                position=Vector2.from_xy_mm(pos_x, pos_y),
                unit=unit,
                angle=angle,
                mirror_x=mirror_x,
                mirror_y=mirror_y
            )

            # Handle properties
            props = elem.get("properties", {})
            if 'Value' in props:
                sch.symbols.set_value(sym, props['Value'])
            if 'Footprint' in props:
                sch.symbols.set_footprint(sym, props['Footprint'])

            # Auto-assign reference
            _prefix = re.match(r'^([A-Za-z#]+)', getattr(sym, 'reference', 'X')).group(1)
            _new_ref = next_ref(_prefix)
            for _f in sym._proto.fields:
                if _f.name == 'Reference':
                    _f.text = _new_ref
                    break
            sch.crud.update_items(sym)

            # Track for later wiring field update
            _temp_to_sym[temp_id] = sym
            _temp_to_ref[temp_id] = _new_ref

            # Record bbox
            try:
                _bb = sch.transform.get_bounding_box(sym, units='mm', include_text=False)
                if _bb:
                    placed_bboxes.append({
                        'ref': _new_ref,
                        'min_x': _bb['min_x'] - _BBOX_MARGIN,
                        'max_x': _bb['max_x'] + _BBOX_MARGIN,
                        'min_y': _bb['min_y'] - _BBOX_MARGIN,
                        'max_y': _bb['max_y'] + _BBOX_MARGIN
                    })
            except:
                pass

            # Build result entry
            _res = {
                'index': i,
                'type': 'symbol',
                'temp_id': temp_id,
                'reference': _new_ref,
                'lib_id': lib_id,
                'position': [round(pos_x, 2), round(pos_y, 2)]
            }

            # Get pin positions
            try:
                _pins = []
                _pin_map = {}
                try:
                    _all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for _ap in _all_pins:
                        _pin_map[_ap['pin_number']] = [
                            round(_ap['position'].x / 1_000_000, 4),
                            round(_ap['position'].y / 1_000_000, 4)
                        ]
                except:
                    pass

                for _p in sym.pins:
                    _pin_info = {'number': _p.number, 'name': getattr(_p, 'name', '')}
                    if _p.number in _pin_map:
                        _pin_info['position'] = _pin_map[_p.number]
                    _pins.append(_pin_info)
                _res['pins'] = _pins
            except:
                pass

            results.append(_res)
            placed_items[temp_id] = {'sym': sym, 'ref': _new_ref, 'type': 'symbol'}

        except _OOB:
            pass
        except Exception as e:
            results.append({'index': i, 'type': 'symbol', 'temp_id': temp_id, 'error': str(e)})

    # Place power symbols
    for i, elem in enumerate(power_symbols):
        pwr_name = elem.get("name", "")
        temp_id = elem.get("id", f"pwr_{i}")
        pos = elem.get("position", [0, 0])
        pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
        pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0
        angle = elem.get("angle", 0.0)

        idx = len(symbols) + i

        try:
            _check_bounds(pos_x, pos_y, idx)

            pwr = sch.labels.add_power(pwr_name, Vector2.from_xy_mm(pos_x, pos_y), angle=angle)

            # Auto-assign reference
            _pwr_ref = next_ref('#PWR')
            for _f in pwr._proto.fields:
                if _f.name == 'Reference':
                    _f.text = _pwr_ref
                    break
            sch.crud.update_items(pwr)

            _temp_to_ref[temp_id] = pwr_name  # Power symbols use net name, not ref
            _power_temp_ids.add(temp_id)  # Mark as power symbol

            # Record bbox
            try:
                _bb = sch.transform.get_bounding_box(pwr, units='mm', include_text=False)
                if _bb:
                    placed_bboxes.append({
                        'ref': _pwr_ref,
                        'min_x': _bb['min_x'] - _BBOX_MARGIN,
                        'max_x': _bb['max_x'] + _BBOX_MARGIN,
                        'min_y': _bb['min_y'] - _BBOX_MARGIN,
                        'max_y': _bb['max_y'] + _BBOX_MARGIN
                    })
            except:
                pass

            results.append({
                'index': idx,
                'type': 'power',
                'temp_id': temp_id,
                'reference': _pwr_ref,
                'net_name': pwr_name,
                'position': [round(pos_x, 2), round(pos_y, 2)]
            })

            placed_items[temp_id] = {'sym': pwr, 'ref': _pwr_ref, 'type': 'power', 'net': pwr_name}

        except _OOB:
            pass
        except Exception as e:
            results.append({'index': idx, 'type': 'power', 'temp_id': temp_id, 'error': str(e)})

    # ---------------------------------------------------------------------------
    # Phase 2: Update symbols with Agent_Wiring fields
    # ---------------------------------------------------------------------------

    wiring_info = []  # Track what wiring recommendations were set

    _debug_info.append(f"Phase 2: _temp_to_sym keys: {list(_temp_to_sym.keys())}")
    _debug_info.append(f"Phase 2: _temp_to_ref: {_temp_to_ref}")
    _debug_info.append(f"Phase 2: wiring_map keys: {list(wiring_map.keys())}")

    for temp_id, sym_obj in _temp_to_sym.items():
        if temp_id not in wiring_map:
            _debug_info.append(f"Phase 2: temp_id '{temp_id}' not in wiring_map, skipping")
            continue

        wiring_entries = wiring_map[temp_id]
        _debug_info.append(f"Phase 2: processing temp_id '{temp_id}' with entries: {wiring_entries}")

        agent_wiring_value = build_agent_wiring_field(temp_id, wiring_entries, _temp_to_ref, _power_temp_ids)
        _debug_info.append(f"Phase 2: agent_wiring_value = '{agent_wiring_value}'")

        if agent_wiring_value:
            try:
                # Add the Agent_Wiring field (use add_field, not set_field, since it's a new field)
                sym_obj.add_field(AGENT_WIRING_FIELD, agent_wiring_value)
                sch.crud.update_items(sym_obj)
                _debug_info.append(f"Phase 2: Successfully added Agent_Wiring field to {temp_id}")

                wiring_info.append({
                    'temp_id': temp_id,
                    'reference': _temp_to_ref[temp_id],
                    'agent_wiring': agent_wiring_value
                })
            except Exception as e:
                _debug_info.append(f"Phase 2: FAILED to add field to {temp_id}: {str(e)}")
                # Log but don't fail - symbol is already placed
                results.append({
                    'type': 'warning',
                    'temp_id': temp_id,
                    'message': f'Failed to set Agent_Wiring: {str(e)}'
                })

    # ---------------------------------------------------------------------------
    # Phase 3: Place labels
    # ---------------------------------------------------------------------------

    labels = TOOL_ARGS.get("labels", [])

    for i, elem in enumerate(labels):
        text = elem.get("text", "")
        label_type = elem.get("type", "local")
        pos = elem.get("position", [0, 0])
        pos_x = snap_to_grid(pos[0]) if len(pos) >= 2 else 0
        pos_y = snap_to_grid(pos[1]) if len(pos) >= 2 else 0

        idx = len(symbols) + len(power_symbols) + i

        try:
            _check_bounds(pos_x, pos_y, idx)
            pos_vec = Vector2.from_xy_mm(pos_x, pos_y)

            if label_type == "global":
                lbl = sch.labels.add_global(text, pos_vec)
            elif label_type == "hierarchical":
                lbl = sch.labels.add_hierarchical(text, pos_vec)
            else:
                lbl = sch.labels.add_local(text, pos_vec)

            results.append({
                'index': idx,
                'type': 'label',
                'text': text,
                'label_type': label_type,
                'position': [round(pos_x, 2), round(pos_y, 2)]
            })

        except _OOB:
            pass
        except Exception as e:
            results.append({'index': idx, 'type': 'label', 'error': str(e)})

    # ---------------------------------------------------------------------------
    # Build final result
    # ---------------------------------------------------------------------------

    _fail = sum(1 for r in results if 'error' in r)
    _total = len(symbols) + len(power_symbols) + len(labels)

    result = {
        'status': 'success' if _fail == 0 else 'partial',
        'total': _total,
        'succeeded': _total - _fail,
        'failed': _fail,
        'results': results,
        'wiring_recommendations': wiring_info,
        'connections_received': len(connections),
        'wiring_map_entries': len(wiring_map),
        '_debug': _debug_info,
        'message': (
            f"Placed {len(symbols)} symbols and {len(power_symbols)} power symbols. "
            f"{len(wiring_info)} symbols have wiring recommendations in their Agent_Wiring field. "
            "Review the diff overlay to approve placements, then wire the recommended connections."
        ) if _fail == 0 else None
    }

except Exception as batch_error:
    result = {'status': 'error', 'message': str(batch_error), 'results': results, '_debug': _debug_info if '_debug_info' in dir() else []}

print(json.dumps(result, indent=2))
