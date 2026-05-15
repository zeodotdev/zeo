import json
import math
from kipy.geometry import Vector2, PolygonWithHoles, PolyLineNode
from kipy.board_types import Track, Via, Zone, BoardSegment, BoardCircle, BoardRectangle, BoardText, BoardArc, Net
from kipy.proto.board.board_types_pb2 import BoardLayer
from kipy.proto.board import board_types_pb2

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
        'In3.Cu': BoardLayer.BL_In3_Cu, 'In4.Cu': BoardLayer.BL_In4_Cu,
        'In5.Cu': BoardLayer.BL_In5_Cu, 'In6.Cu': BoardLayer.BL_In6_Cu,
        'F.SilkS': BoardLayer.BL_F_SilkS, 'B.SilkS': BoardLayer.BL_B_SilkS,
        'F.Mask': BoardLayer.BL_F_Mask, 'B.Mask': BoardLayer.BL_B_Mask,
        'Edge.Cuts': BoardLayer.BL_Edge_Cuts,
        'F.Fab': BoardLayer.BL_F_Fab, 'B.Fab': BoardLayer.BL_B_Fab,
        'F.CrtYd': BoardLayer.BL_F_CrtYd, 'B.CrtYd': BoardLayer.BL_B_CrtYd,
    }

    # Via type mapping
    via_type_map = {
        'through': board_types_pb2.ViaType.VT_THROUGH,
        'blind_buried': board_types_pb2.ViaType.VT_BLIND_BURIED,
        'blind': board_types_pb2.ViaType.VT_BLIND,
        'buried': board_types_pb2.ViaType.VT_BURIED,
        'micro': board_types_pb2.ViaType.VT_MICRO,
    }

    for idx, elem in enumerate(elements):
        elem_type = elem.get('element_type', '')
        try:
            if elem_type == 'track':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                width = mm_to_nm(elem.get('width', 0.25))
                net = elem.get('net', '')
                locked = elem.get('locked', False)
                points = elem.get('points', [])
                if len(points) < 2:
                    errors.append({'index': idx, 'error': 'track requires at least 2 points'})
                    continue
                # Use board.route_track for multi-point tracks
                point_vectors = [Vector2.from_xy(mm_to_nm(p[0]), mm_to_nm(p[1])) for p in points]
                tracks = board.route_track(points=point_vectors, width=width, layer=layer, net=net if net else None)
                if locked and tracks:
                    for t in tracks:
                        t.locked = True
                    board.update_items(tracks)
                created.append({'element_type': 'track', 'segments': len(tracks), 'ids': [str(t.id.value) for t in tracks]})

            elif elem_type == 'via':
                pos = elem.get('position', [0, 0])
                size = mm_to_nm(elem.get('size', 0.8))
                drill = mm_to_nm(elem.get('drill', 0.4))
                net = elem.get('net', '')
                locked = elem.get('locked', False)
                via_type_str = elem.get('via_type', 'through')
                start_layer_name = elem.get('start_layer', 'F.Cu')
                end_layer_name = elem.get('end_layer', 'B.Cu')

                position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))
                via_type = via_type_map.get(via_type_str, board_types_pb2.ViaType.VT_THROUGH)

                # Create via object
                via = Via()
                via.position = position
                via.type = via_type
                via.diameter = int(size)
                via.drill_diameter = int(drill)

                if net:
                    net_obj = Net()
                    net_obj.name = net
                    via.net = net_obj

                # Set start/end layers for non-through vias
                if via_type != board_types_pb2.ViaType.VT_THROUGH:
                    via.padstack.drill.start_layer = layer_map.get(start_layer_name, BoardLayer.BL_F_Cu)
                    via.padstack.drill.end_layer = layer_map.get(end_layer_name, BoardLayer.BL_B_Cu)

                result = board.create_items([via])
                via_result = result[0] if result else via
                if locked and result:
                    via_result.locked = True
                    board.update_items([via_result])
                created.append({
                    'element_type': 'via',
                    'position': pos,
                    'id': str(via_result.id.value),
                    'via_type': via_type_str
                })

            elif elem_type == 'zone':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                net = elem.get('net', '')
                priority = elem.get('priority', 0)
                clearance = elem.get('clearance')
                min_thickness = elem.get('min_thickness')
                locked = elem.get('locked', False)
                outline_pts = elem.get('outline', [])
                poly = PolygonWithHoles()
                for pt in outline_pts:
                    poly.outline.append(PolyLineNode.from_xy(mm_to_nm(pt[0]), mm_to_nm(pt[1])))
                poly.outline.closed = True
                zone = board.add_zone(outline=poly, layers=[layer], net=net if net else None, priority=priority)

                # Set optional properties
                needs_update = False
                if clearance is not None:
                    zone.clearance = mm_to_nm(clearance)
                    needs_update = True
                if min_thickness is not None:
                    zone.min_thickness = mm_to_nm(min_thickness)
                    needs_update = True
                if locked:
                    zone.locked = True
                    needs_update = True
                if needs_update:
                    board.update_items([zone])

                created.append({'element_type': 'zone', 'layer': layer_name, 'id': str(zone.id.value)})

            elif elem_type == 'line':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                points = elem.get('points', [[0,0], [10,10]])
                width = elem.get('width')
                locked = elem.get('locked', False)
                seg = BoardSegment()
                seg.layer = layer
                seg.start = Vector2.from_xy(mm_to_nm(points[0][0]), mm_to_nm(points[0][1]))
                seg.end = Vector2.from_xy(mm_to_nm(points[1][0]), mm_to_nm(points[1][1]))
                if width is not None:
                    seg.attributes.stroke_width = mm_to_nm(width)
                result = board.create_items([seg])
                if result and locked:
                    result[0].locked = True
                    board.update_items([result[0]])
                created.append({'element_type': 'line', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'rectangle':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                tl = elem.get('top_left', [0, 0])
                br = elem.get('bottom_right', [10, 10])
                locked = elem.get('locked', False)
                rect = BoardRectangle()
                rect.layer = layer
                rect.top_left = Vector2.from_xy(mm_to_nm(tl[0]), mm_to_nm(tl[1]))
                rect.bottom_right = Vector2.from_xy(mm_to_nm(br[0]), mm_to_nm(br[1]))
                result = board.create_items([rect])
                if result and locked:
                    result[0].locked = True
                    board.update_items([result[0]])
                created.append({'element_type': 'rectangle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'circle':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                center = elem.get('center', [0, 0])
                radius = elem.get('radius', 5.0)
                locked = elem.get('locked', False)
                circ = BoardCircle()
                circ.layer = layer
                circ.center = Vector2.from_xy(mm_to_nm(center[0]), mm_to_nm(center[1]))
                circ.radius_point = Vector2.from_xy(mm_to_nm(center[0] + radius), mm_to_nm(center[1]))
                result = board.create_items([circ])
                if result and locked:
                    result[0].locked = True
                    board.update_items([result[0]])
                created.append({'element_type': 'circle', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'text':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                text_content = elem.get('text', '')
                pos = elem.get('position', [0, 0])
                locked = elem.get('locked', False)
                txt = BoardText()
                txt.layer = layer
                txt.position = Vector2.from_xy(mm_to_nm(pos[0]), mm_to_nm(pos[1]))
                txt.value = text_content
                result = board.create_items([txt])
                if result and locked:
                    result[0].locked = True
                    board.update_items([result[0]])
                created.append({'element_type': 'text', 'text': text_content, 'id': str(result[0].id.value) if result else ''})

            elif elem_type == 'keepout':
                layer_name = elem.get('layer', 'F.Cu')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_Cu)
                locked = elem.get('locked', False)
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
                if locked:
                    zone.locked = True
                board.update_items([zone])
                created.append({'element_type': 'keepout', 'layer': layer_name, 'id': str(zone.id.value)})

            elif elem_type == 'arc':
                layer_name = elem.get('layer', 'F.SilkS')
                layer = layer_map.get(layer_name, BoardLayer.BL_F_SilkS)
                center = elem.get('center', [0, 0])
                radius = elem.get('radius', 5.0)
                start_angle = elem.get('start_angle', 0)
                end_angle = elem.get('end_angle', 90)
                locked = elem.get('locked', False)
                arc_obj = BoardArc()
                arc_obj.layer = layer
                sa = math.radians(start_angle)
                ea = math.radians(end_angle)
                ma = (sa + ea) / 2
                arc_obj.start = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(sa)), mm_to_nm(center[1] + radius * math.sin(sa)))
                arc_obj.mid = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(ma)), mm_to_nm(center[1] + radius * math.sin(ma)))
                arc_obj.end = Vector2.from_xy(mm_to_nm(center[0] + radius * math.cos(ea)), mm_to_nm(center[1] + radius * math.sin(ea)))
                result = board.create_items([arc_obj])
                if result and locked:
                    result[0].locked = True
                    board.update_items([result[0]])
                created.append({'element_type': 'arc', 'layer': layer_name, 'id': str(result[0].id.value) if result else ''})

            else:
                errors.append({'index': idx, 'error': f'Unknown element_type: {elem_type}'})

        except Exception as e:
            errors.append({'index': idx, 'element_type': elem_type, 'error': str(e)})

    status = 'success' if not errors else ('partial' if created else 'error')
    print(json.dumps({'status': status, 'created': created, 'errors': errors}, indent=2))
