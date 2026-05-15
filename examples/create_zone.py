# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
from kipy import KiCad
from kipy.board_types import (
    BoardLayer,
    Zone
)
from kipy.common_types import PolygonWithHoles
from kipy.geometry import PolyLine, PolyLineNode
from kipy.util import from_mm

if __name__=='__main__':
    kicad = KiCad()
    board = kicad.get_board()

    outline = PolyLine()
    outline.append(PolyLineNode.from_xy(from_mm(100), from_mm(100)))
    outline.append(PolyLineNode.from_xy(from_mm(110), from_mm(100)))
    outline.append(PolyLineNode.from_xy(from_mm(110), from_mm(110)))
    outline.append(PolyLineNode.from_xy(from_mm(100), from_mm(110)))
    outline.append(PolyLineNode.from_xy(from_mm(100), from_mm(100)))
    polygon = PolygonWithHoles()
    polygon.outline = outline
    zone = Zone()
    zone.layers = [BoardLayer.BL_F_Cu, BoardLayer.BL_B_Cu]
    zone.outline = polygon
    board.create_items(zone)
