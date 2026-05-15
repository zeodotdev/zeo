from kipy import KiCad
from kipy.geometry import Vector2, Angle
from math import cos, sin, pi

board = KiCad().get_board()
footprints = board.get_footprints()

sorted_footprints = sorted(footprints, key=lambda fp: fp.reference_field.text.value)

center_x, center_y = 100, 100

commit = board.begin_commit()
radius = 50
angle_deg = 60
for i, footprint in enumerate(sorted_footprints):
	pos_x = center_x + radius * cos(i*angle_deg*pi/180)
	pos_y = center_y + radius * sin(i*angle_deg*pi/180)
	footprint.position = Vector2.from_xy_mm(pos_x, pos_y)
	footprint.orientation = Angle.from_degrees(-i*angle_deg)
board.update_items(footprints)
board.push_commit(commit)
