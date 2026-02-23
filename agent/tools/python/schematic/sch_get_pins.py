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
        # Build pin list using IPC for exact transformed positions
        pins = []
        if hasattr(sym, 'pins'):
            for pin in sym.pins:
                pin_info = {
                    'number': pin.number,
                    'name': getattr(pin, 'name', '')
                }
                # Get exact transformed position via IPC
                if hasattr(sch.symbols, 'get_transformed_pin_position'):
                    try:
                        result = sch.symbols.get_transformed_pin_position(sym, pin.number)
                        if result:
                            pin_info['position'] = get_pos(result['position'])
                            pin_info['orientation'] = result.get('orientation', 0)
                    except:
                        pin_info['position'] = get_pos(getattr(pin, 'position', None))
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
