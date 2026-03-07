import json

refresh_or_fail(sch)

ref = TOOL_ARGS.get("ref", "")

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
        # Build pin list using batch IPC for exact transformed positions (single call)
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
                pin_info = {
                    'number': pin.number,
                    'name': getattr(pin, 'name', '')
                }
                if pin.number in pin_map:
                    pin_info['position'] = pin_map[pin.number]['position']
                    pin_info['orientation'] = pin_map[pin.number]['orientation']
                else:
                    pin_info['position'] = get_pos(getattr(pin, 'position', None))
                pins.append(pin_info)

        result = {
            'status': 'success',
            'ref': ref,
            'lib_id': get_lib_id_str(sym),
            'position': get_pos(getattr(sym, 'position', None)),
            'angle': getattr(sym, 'angle', 0),
            'value': getattr(sym, 'value', ''),
            'pins': pins
        }
        print(json.dumps(result, indent=2))

except Exception as e:
    print(json.dumps({'status': 'error', 'message': str(e)}))
