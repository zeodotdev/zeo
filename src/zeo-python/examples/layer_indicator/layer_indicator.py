#!/usr/bin/env python3

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

from typing import cast

from kipy import KiCad
from kipy.geometry import Vector2
from kipy.board import BoardLayer, BoardLayerClass
from kipy.board_types import BoardText, FootprintInstance


if __name__=='__main__':
    kicad = KiCad()
    board = kicad.get_board()
    stackup = board.get_stackup()
    defaults = board.get_graphics_defaults()[BoardLayerClass.BLC_COPPER]

    sizing_text = BoardText()
    sizing_text.layer = BoardLayer.BL_F_Cu
    sizing_text.position = Vector2.from_xy(0, 0)
    sizing_text.value = "0"
    sizing_text.attributes = defaults.text

    char_width = kicad.get_text_extents(sizing_text.as_text()).size.x

    copper_layers = [layer for layer in stackup.layers
                     if layer.layer <= BoardLayer.BL_B_Cu
                     and layer.layer >= BoardLayer.BL_F_Cu]

    fpi = FootprintInstance()
    fpi.layer = BoardLayer.BL_F_Cu
    fpi.reference_field.text.value = "STACKUP1"
    fpi.reference_field.text.attributes = defaults.text
    fpi.reference_field.visible = False
    fpi.value_field.text.attributes = defaults.text
    fpi.value_field.visible = False
    fpi.attributes.not_in_schematic = True
    fpi.attributes.exclude_from_bill_of_materials = True
    fpi.attributes.exclude_from_position_files = True
    fp = fpi.definition

    offset = 0
    layer_idx = 1
    for copper_layer in copper_layers:
        layer_text = BoardText()
        layer_text.layer = copper_layer.layer
        layer_text.value = "%d" % layer_idx
        layer_text.position = Vector2.from_xy(offset, 0)
        layer_text.attributes = defaults.text
        fp.add_item(layer_text)

        padding = 1 if layer_idx == 9 else 0.5
        item_width = int((len(layer_text.value) + padding) * char_width)

        offset += item_width
        layer_idx += 1

    created = [cast(FootprintInstance, i) for i in board.create_items(fpi)]

    if len(created) == 1:
        board.interactive_move(created[0].id)
