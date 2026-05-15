import json
from kipy.proto.board.board_types_pb2 import BoardLayer

ref = TOOL_ARGS.get("ref", "")
if not ref:
    print(json.dumps({'status': 'error', 'message': 'ref is required'}))
else:
    # Find footprint by reference
    fp = board.footprints.get_by_reference(ref)

    if not fp:
        print(json.dumps({'status': 'error', 'message': f'Footprint not found: {ref}'}))
    else:
        # Get pads from footprint definition - pad.position is already absolute
        pads = []
        if hasattr(fp, 'definition') and hasattr(fp.definition, 'pads'):
            for pad in fp.definition.pads:
                # pad.position contains absolute board coordinates (already transformed)
                pad_info = {
                    'number': str(pad.number) if hasattr(pad, 'number') else '',
                    'position': [pad.position.x / 1000000, pad.position.y / 1000000],
                    'net': pad.net.name if hasattr(pad, 'net') else ''
                }
                pads.append(pad_info)

        fp_layer = 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'
        result = {
            'status': 'success',
            'ref': ref,
            'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',
            'position': [fp.position.x / 1000000, fp.position.y / 1000000],
            'angle': fp.orientation.degrees if hasattr(fp, 'orientation') and hasattr(fp.orientation, 'degrees') else 0,
            'layer': fp_layer,
            'locked': getattr(fp, 'locked', False),
            'pads': pads
        }
        print(json.dumps(result, indent=2))
