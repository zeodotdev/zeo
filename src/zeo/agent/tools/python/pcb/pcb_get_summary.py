# Copyright (C) 2026, Zeo <team@zeo.dev>
# SPDX-License-Identifier: AGPL-3.0-or-later

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
    'board_outline': None,
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

# Board outline lives on Edge.Cuts as a set of graphic shapes (segments, arcs,
# circles, rectangles, polygons). Surface a count + bounding box in mm so the
# agent can tell the outline exists and roughly where it is — previously this
# field was hardcoded to None, which made the LLM report "no outline" even
# when one was clearly present.
try:
    edge_shapes = [s for s in board.get_shapes() if getattr(s, 'layer', None) == BoardLayer.BL_Edge_Cuts]

    if edge_shapes:
        xs, ys = [], []

        for s in edge_shapes:
            for attr in ('start', 'end', 'mid', 'center', 'top_left', 'bottom_right'):
                v = getattr(s, attr, None)
                if v is not None and hasattr(v, 'x') and hasattr(v, 'y'):
                    xs.append(v.x)
                    ys.append(v.y)

            # Circle: include the full extent via radius, not just the center.
            try:
                if callable(getattr(s, 'radius', None)) and hasattr(s, 'center'):
                    r = int(s.radius() + 0.5)
                    c = s.center
                    xs.extend([c.x - r, c.x + r])
                    ys.extend([c.y - r, c.y + r])
            except Exception:
                pass

            # Polygon: walk outline nodes from each PolygonWithHoles.
            try:
                for poly in getattr(s, 'polygons', []) or []:
                    for node in poly.outline.nodes:
                        if getattr(node, 'has_point', False):
                            p = node.point
                            xs.append(p.x)
                            ys.append(p.y)
            except Exception:
                pass

        bbox = None
        if xs and ys:
            bbox = {
                'x_min_mm': min(xs) / 1000000,
                'y_min_mm': min(ys) / 1000000,
                'x_max_mm': max(xs) / 1000000,
                'y_max_mm': max(ys) / 1000000,
                'width_mm': (max(xs) - min(xs)) / 1000000,
                'height_mm': (max(ys) - min(ys)) / 1000000,
            }

        summary['board_outline'] = {
            'edge_cut_shape_count': len(edge_shapes),
            'bounding_box': bbox,
        }
except Exception:
    pass

print(json.dumps(summary, indent=2))
