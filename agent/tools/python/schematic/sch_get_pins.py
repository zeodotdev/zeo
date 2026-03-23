import json

refresh_or_fail(sch)

ref = TOOL_ARGS.get("ref", "")
unit_filter = TOOL_ARGS.get("unit", None)  # Optional: filter pins by unit

try:
    sym = sch.symbols.get_by_ref(ref)
    if not sym:
        # List available symbols
        all_syms = sch.symbols.get_all()
        available = [s.reference for s in all_syms[:20] if hasattr(s, 'reference')]
        print(json.dumps({
            'status': 'error',
            'message': f'Symbol not found: {ref}',
            'available': available
        }))
    else:
        # Get symbol's unit info
        sym_unit = getattr(sym, 'unit', 1)

        # Get library symbol info for unit_count and pin unit assignments
        lib_id = get_lib_id_str(sym)
        unit_count = 1
        lib_pin_units = {}  # pin_number -> unit
        try:
            lib_sym = sch.library.get_symbol_info(lib_id)
            if lib_sym:
                unit_count = getattr(lib_sym, 'unit_count', 1)
                # Map pin numbers to their unit assignments from library
                for lib_pin in getattr(lib_sym, 'pins', []):
                    lib_pin_units[lib_pin.number] = getattr(lib_pin, 'unit', 0)
        except Exception as e:
            tool_log(f"get_symbol_info failed: {e}")

        # Build pin list using batch IPC for exact transformed positions
        pins = []
        if hasattr(sym, 'pins'):
            # Use batch API for efficiency
            pin_map = {}
            if hasattr(sch.symbols, 'get_all_transformed_pin_positions'):
                try:
                    all_pins = sch.symbols.get_all_transformed_pin_positions(sym)
                    for p in all_pins:
                        pin_map[p['pin_number']] = {
                            'position': get_pos(p.get('position')),
                            'orientation': p.get('orientation', 0)
                        }
                except Exception as e:
                    tool_log(f"get_all_transformed_pin_positions failed: {e}")

            for pin in sym.pins:
                pin_num = pin.number
                # Get pin's unit from library (0 = shared across all units)
                pin_unit = lib_pin_units.get(pin_num, 0)

                # Filter by unit if requested
                # pin_unit 0 means shared (e.g., power pins) - always include
                # Otherwise only include pins matching the filter
                if unit_filter is not None:
                    if pin_unit != 0 and pin_unit != unit_filter:
                        continue

                pin_info = {
                    'number': pin_num,
                    'name': getattr(pin, 'name', ''),
                    'unit': pin_unit  # 0 = shared, 1+ = specific unit
                }
                if pin_num in pin_map:
                    pin_info['position'] = pin_map[pin_num]['position']
                    pin_info['orientation'] = pin_map[pin_num]['orientation']
                else:
                    pin_info['position'] = get_pos(getattr(pin, 'position', None))
                pins.append(pin_info)

        result = {
            'status': 'success',
            'ref': ref,
            'lib_id': lib_id,
            'position': get_pos(getattr(sym, 'position', None)),
            'angle': getattr(sym, 'angle', 0),
            'value': getattr(sym, 'value', ''),
            'unit': sym_unit,           # Which unit this symbol instance is
            'unit_count': unit_count,   # Total units in this symbol
            'pins': pins
        }
        print(json.dumps(result, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': str(e)}))
