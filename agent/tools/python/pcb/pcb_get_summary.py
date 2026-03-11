import json
from kipy.proto.board.board_types_pb2 import BoardLayer

# Get PCB summary
summary = {
    'footprints': [],
    'tracks': 0,
    'vias': 0,
    'zones': 0,
    'nets': [],
    'layers': [],
    'board_outline': None
}

# Get footprints using correct API
footprints = board.get_footprints()
for fp in footprints:
    ref = fp.reference_field.text.value if hasattr(fp, 'reference_field') else '?'
    pos = fp.position
    summary['footprints'].append({
        'ref': ref,
        'lib_id': f'{fp.definition.id.library}:{fp.definition.id.name}' if hasattr(fp, 'definition') else '',
        'position': [pos.x / 1000000, pos.y / 1000000],
        'layer': 'B.Cu' if fp.layer == BoardLayer.BL_B_Cu else 'F.Cu'
    })

# Count tracks, vias, zones using correct API
tracks = board.get_tracks()
summary['tracks'] = len(tracks)

vias = board.get_vias()
summary['vias'] = len(vias)

zones = board.get_zones()
summary['zones'] = len(zones)

# Get nets
try:
    nets = board.get_nets()
    summary['nets'] = [{'name': n.name} for n in nets[:50]]  # Limit to 50
except Exception:
    pass

# Get layers
try:
    layers = board.get_enabled_layers()
    summary['layers'] = [BoardLayer.Name(l).replace('BL_', '').replace('_', '.') for l in layers] if layers else []
except Exception:
    pass

print(json.dumps(summary, indent=2))
