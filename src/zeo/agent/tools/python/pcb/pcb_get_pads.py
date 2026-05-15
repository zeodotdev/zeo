import json

ref = TOOL_ARGS.get("ref", "")
if not ref:
    print(json.dumps({'status': 'error', 'message': 'ref is required'}))
else:
    # Find footprint by reference
    target_fp = board.footprints.get_by_reference(ref)

    if not target_fp:
        print(json.dumps({'status': 'error', 'message': f'Footprint not found: {ref}'}))
    else:
        # Get pads from footprint definition
        # NOTE: pad.position is already in ABSOLUTE board coordinates
        # KiCad API returns transformed positions, no manual transformation needed
        pads = []

        if hasattr(target_fp, 'definition') and hasattr(target_fp.definition, 'pads'):
            for pad in target_fp.definition.pads:
                pad_info = {
                    'number': str(pad.number) if hasattr(pad, 'number') else '',
                    'position': [pad.position.x / 1000000, pad.position.y / 1000000],
                    'net': pad.net.name if hasattr(pad, 'net') else ''
                }
                pads.append(pad_info)

        result = {
            'status': 'success',
            'ref': ref,
            'pads': pads
        }
        print(json.dumps(result, indent=2))
