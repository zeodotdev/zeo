import json
import math
from kipy.geometry import Vector2, PolygonWithHoles, PolyLineNode
from kipy.board_types import Track, Via, Zone, BoardSegment, BoardCircle, BoardRectangle, BoardText, BoardArc
from kipy.proto.board.board_types_pb2 import BoardLayer

elements = TOOL_ARGS.get("elements", [])
if not elements:
    print(json.dumps({'status': 'error', 'message': 'elements array is required'}))
else:
    created = []
    errors = []

    def mm_to_nm(mm):
        return round(mm * 1000000)

    # Layer name to BoardLayer enum mapping
    layer_map = {
        'F.Cu': BoardLayer.BL_F_Cu, 'B.Cu': BoardLayer.BL_B_Cu,
        'In1.Cu': BoardLayer.BL_In1_Cu, 'In2.Cu': BoardLayer.BL_In2_Cu,
        'F.SilkS': BoardLayer.BL_F_SilkS, 'B.SilkS': BoardLayer.BL_B_SilkS,
        'F.Mask': BoardLayer.BL_F_Mask, 'B.Mask': BoardLayer.BL_B_Mask,
        'Edge.Cuts': BoardLayer.BL_Edge_Cuts,
        'F.Fab': BoardLayer.BL_F_Fab, 'B.Fab': BoardLayer.BL_B_Fab,
        'F.CrtYd': BoardLayer.BL_F_CrtYd, 'B.CrtYd': BoardLayer.BL_B_CrtYd,
    }

    for idx, elem in enumerate(elements):
        elem_type = elem.get('element_type', '')
        try:
            if elem_type == 'track':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                width = mm_to_nm(elem.get('width', 0.25))
                net = elem.get('net', '')
                points = elem.get('points', [])
                if len(points) < 2:
                    errors.append({'index': idx, 'error': 'track requires at least 2 points'})
                    continue
                # Use board.route_track for multi-point tracks
                point_vectors = [Vector2.from_xy(mm_to_nm(p[0]), mm_to_nm(p[1])) for p in points]
                tracks = board.route_track(points=point_vectors, width=width, layer=layer, net=net if net else None)
                created.append({'element_type': 'track', 'segments': len(tracks), 'ids': [str(t.id.value) for t in tracks]})

            elif elem_type == 'via':
                pos = elem.get('position', [0, 0])
                size = mm_to_nm(elem.get('size', 0.8))
                drill = mm_to_nm(elem.get('drill', 0.4))
                net = elem.get('net', '')
                position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))
                via = board.routing.add_via(position=position, diameter=int(size), drill=int(drill), net=net if net else None)
                created.append({'element_type': 'via', 'position': pos, 'id': str(via.id.value)})

            elif elem_type == 'zone':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                net = elem.get('net', '')
                priority = elem.get('priority', 0)
                outline_pts = elem.get('outline', [])
                poly = PolygonWithHoles()
                for pt in outline_pts:
                    poly.outline.append(PolyLineNode.from_xy(mm_to_nm(pt[0]), mm_to_nm(pt[1])))
                poly.outline.closed = True
                zone = board.add_zone(outline=poly, layers=[layer], net=net if net else None, priority=priority)
                created.append({'element_type': 'zone', 'layer': layer_name, 'id': str(zone.id.value)})

            elif elem_type == 'line':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                points = elem.get('points', [[0,0], [10,10]])
                seg = BoardSegment()
                seg.layer = layer
                seg.start = Vector2.from_xy(mm_to_nm(points[0][0]), mm_to_nm(points[0][1]))
                seg.end = Vector2.from_xy(mm_to_nm(points[1][0]), mm_to_nm(points[1][1]))
                result = board.create_items([seg])
                created.append({'element_type': 'line', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'rectangle':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                tl = elem.get('top_left', [0, 0])
                br = elem.get('bottom_right', [10, 10])
                rect = BoardRectangle()
                rect.layer = layer
                rect.top_left = Vector2.from_xy(mm_to_nm(tl[0]), mm_to_nm(tl[1]))
                rect.bottom_right = Vector2.from_xy(mm_to_nm(br[0]), mm_to_nm(br[1]))
                result = board.create_items([rect])
                created.append({'element_type': 'rectangle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'circle':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                center = elem.get('center', [0, 0])
                radius = elem.get('radius', 5.0)
                circ = BoardCircle()
                circ.layer = layer
                circ.center = Vector2.from_xy(mm_to_nm(center[0]), mm_to_nm(center[1]))
                circ.radius_point = Vector2.from_xy(mm_to_nm(center[0] + radius), mm_to_nm(center[1]))
                result = board.create_items([circ])
                created.append({'element_type': 'circle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'text':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                text_content = elem.get('text', '')
                pos = elem.get('position', [0, 0])
                txt = BoardText()
                txt.layer = layer
                txt.position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))
                txt.value = text_content
                result = board.create_items([txt])
                created.append({'element_type': 'text', 'text': text_content, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'keepout':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                outline_pts = elem.get('outline', [])
                poly = PolygonWithHoles()
                for pt in outline_pts:
                    poly.outline.append(PolyLineNode.from_xy(mm_to_nm(pt[0]), mm_to_nm(pt[1])))
                poly.outline.closed = True
                zone = board.add_zone(outline=poly, layers=[layer])
                zone.is_keepout = True
                zone.keepout_copper = elem.get('no_copper', True)
                zone.keepout_vias = elem.get('no_vias', True)
                zone.keepout_tracks = elem.get('no_tracks', True)
                board.update_items([zone])
                created.append({'element_type': 'keepout', 'layer': layer_name, 'id': str(zone.id.value)})

            elif elem_type == 'arc':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                center = elem.get('center', [0, 0])
                radius = elem.get('radius', 5.0)
                start_angle = elem.get('start_angle', 0)
                end_angle = elem.get('end_angle', 90)
                arc_obj = BoardArc()
                arc_obj.layer = layer
                sa = math.radians(start_angle)
                ea = math.radians(end_angle)
                ma = (sa + ea) / 2
                arc_obj.start = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(sa)), mm_to_nm(center[1] + radius * math.sin(sa)))
                arc_obj.mid = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(ma)), mm_to_nm(center[1] + radius * math.sin(ma)))
                arc_obj.end = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(ea)), mm_to_nm(center[1] + radius * math.sin(ea)))
                result = board.create_items([arc_obj])
                created.append({'element_type': 'arc', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            else:
                errors.append({'index': idx, 'error': f'Unknown element_type: {elem_type}'})

        except Exception as e:
            errors.append({'index': idx, 'element_type': elem_type, 'error': str(e)})

    status = 'success' if not errors else ('partial' if created else 'error')
    print(json.dumps({'status': status, 'created': created, 'errors': errors}, indent=2))
