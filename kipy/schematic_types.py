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

from typing import List, Optional

from kipy.proto.schematic import schematic_types_pb2
from kipy.proto.common.types import base_types_pb2
from kipy.proto.common.types.enums_pb2 import HA_LEFT, VA_BOTTOM
from kipy.wrapper import Wrapper, Item, register_wrapper
from kipy.common_types import LibraryIdentifier, TextAttributes
from kipy.geometry import Vector2

class SchematicLayer:
    """Constants for schematic layer types."""
    UNKNOWN = 0
    # Note: The actual layer enum values need to match SCH_LAYER_ID
    # LAYER_WIRE = 1, LAYER_BUS = 2, LAYER_NOTES = 3 (approximate)


@register_wrapper(schematic_types_pb2.Line)
class Wire(Item):
    """Represents a wire/bus/line in the schematic."""
    def __init__(self, proto: Optional[schematic_types_pb2.Line] = None):
        self._proto = schematic_types_pb2.Line()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def start(self) -> Vector2:
        return Vector2(self._proto.start)

    @start.setter
    def start(self, value: Vector2):
        self._proto.start.CopyFrom(value.proto)

    @property
    def end(self) -> Vector2:
        return Vector2(self._proto.end)

    @end.setter
    def end(self, value: Vector2):
        self._proto.end.CopyFrom(value.proto)

    @property
    def layer(self) -> int:
        return self._proto.layer

    @layer.setter
    def layer(self, value: int):
        self._proto.layer = value

    @classmethod
    def create(cls, start: Vector2, end: Vector2, layer: int = 0) -> "Wire":
        """Create a new wire with the given start/end points.

        Args:
            start: Start point of the wire
            end: End point of the wire
            layer: Layer type (default 0 for wire)

        Returns:
            New Wire instance
        """
        wire = cls()
        wire.start = start
        wire.end = end
        wire.layer = layer
        return wire


@register_wrapper(schematic_types_pb2.LocalLabel)
class LocalLabel(Item):
    """Represents a local net label in the schematic."""
    def __init__(self, proto: Optional[schematic_types_pb2.LocalLabel] = None):
        self._proto = schematic_types_pb2.LocalLabel()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def text(self) -> str:
        return self._proto.text.text

    @text.setter
    def text(self, value: str):
        self._proto.text.text = value

    @classmethod
    def create(cls, position: Vector2, text: str = "") -> "LocalLabel":
        """Create a new local label.

        Args:
            position: Position for the label
            text: The label text (net name)

        Returns:
            New LocalLabel instance
        """
        label = cls()
        label.position = position
        label.text = text
        label._proto.text.attributes.horizontal_alignment = HA_LEFT
        label._proto.text.attributes.vertical_alignment = VA_BOTTOM
        label._proto.text.attributes.size.x_nm = 1270000
        label._proto.text.attributes.size.y_nm = 1270000
        return label


@register_wrapper(schematic_types_pb2.GlobalLabel)
class GlobalLabel(Item):
    """Represents a global net label in the schematic."""
    def __init__(self, proto: Optional[schematic_types_pb2.GlobalLabel] = None):
        self._proto = schematic_types_pb2.GlobalLabel()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def text(self) -> str:
        return self._proto.text.text

    @text.setter
    def text(self, value: str):
        self._proto.text.text = value

    @classmethod
    def create(cls, position: Vector2, text: str = "") -> "GlobalLabel":
        """Create a new global label.

        Args:
            position: Position for the label
            text: The label text (net name)

        Returns:
            New GlobalLabel instance
        """
        label = cls()
        label.position = position
        label.text = text
        label._proto.text.attributes.horizontal_alignment = HA_LEFT
        label._proto.text.attributes.vertical_alignment = VA_BOTTOM
        label._proto.text.attributes.size.x_nm = 1270000
        label._proto.text.attributes.size.y_nm = 1270000
        return label


@register_wrapper(schematic_types_pb2.HierarchicalLabel)
class HierarchicalLabel(Item):
    """Represents a hierarchical label in the schematic."""
    def __init__(self, proto: Optional[schematic_types_pb2.HierarchicalLabel] = None):
        self._proto = schematic_types_pb2.HierarchicalLabel()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def text(self) -> str:
        return self._proto.text.text

    @text.setter
    def text(self, value: str):
        self._proto.text.text = value

    @classmethod
    def create(cls, position: Vector2, text: str = "") -> "HierarchicalLabel":
        """Create a new hierarchical label.

        Args:
            position: Position for the label
            text: The label text (net name)

        Returns:
            New HierarchicalLabel instance
        """
        label = cls()
        label.position = position
        label.text = text
        label._proto.text.attributes.horizontal_alignment = HA_LEFT
        label._proto.text.attributes.vertical_alignment = VA_BOTTOM
        label._proto.text.attributes.size.x_nm = 1270000
        label._proto.text.attributes.size.y_nm = 1270000
        return label


@register_wrapper(schematic_types_pb2.DirectiveLabel)
class DirectiveLabel(Item):
    """Represents a directive label (SPICE/simulation directive) in the schematic."""
    def __init__(self, proto: Optional[schematic_types_pb2.DirectiveLabel] = None):
        self._proto = schematic_types_pb2.DirectiveLabel()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def text(self) -> str:
        return self._proto.text.text

    @text.setter
    def text(self, value: str):
        self._proto.text.text = value

    @classmethod
    def create(cls, position: Vector2, text: str = "") -> "DirectiveLabel":
        """Create a new directive label.

        Args:
            position: Position for the label
            text: The directive text (e.g., ".tran 1m")

        Returns:
            New DirectiveLabel instance
        """
        label = cls()
        label.position = position
        label.text = text
        return label


# Junction and NoConnect wrappers - require proto regeneration
# These will be registered once the proto files are regenerated
try:
    @register_wrapper(schematic_types_pb2.Junction)
    class Junction(Item):
        """Represents a junction (connection point) in the schematic."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.Junction()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def position(self) -> Vector2:
            return Vector2(self._proto.position)

        @position.setter
        def position(self, value: Vector2):
            self._proto.position.CopyFrom(value.proto)

        @property
        def diameter(self) -> int:
            return self._proto.diameter

        @diameter.setter
        def diameter(self, value: int):
            self._proto.diameter = value

        @classmethod
        def create(cls, position: Vector2, diameter: int = 0) -> "Junction":
            """Create a new junction at the given position."""
            junction = cls()
            junction.position = position
            junction.diameter = diameter
            return junction

    @register_wrapper(schematic_types_pb2.NoConnect)
    class NoConnect(Item):
        """Represents a no-connect marker in the schematic."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.NoConnect()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def position(self) -> Vector2:
            return Vector2(self._proto.position)

        @position.setter
        def position(self, value: Vector2):
            self._proto.position.CopyFrom(value.proto)

        @classmethod
        def create(cls, position: Vector2) -> "NoConnect":
            """Create a new no-connect marker at the given position."""
            nc = cls()
            nc.position = position
            return nc

except AttributeError:
    # Proto types not yet available - will be after regeneration
    Junction = None
    NoConnect = None


@register_wrapper(schematic_types_pb2.Pin)
class Pin(Wrapper):
    def __init__(self, proto: Optional[schematic_types_pb2.Pin] = None):
        self._proto = schematic_types_pb2.Pin()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def name(self) -> str:
        return self._proto.name

    @name.setter
    def name(self, value: str):
        self._proto.name = value

    @property
    def number(self) -> str:
        return self._proto.number

    @number.setter
    def number(self, value: str):
        self._proto.number = value

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def angle(self) -> float:
        return self._proto.angle.value_degrees

    @angle.setter
    def angle(self, value: float):
        self._proto.angle.value_degrees = value


@register_wrapper(schematic_types_pb2.Field)
class Field(Wrapper):
    def __init__(self, proto: Optional[schematic_types_pb2.Field] = None):
        self._proto = schematic_types_pb2.Field()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def text(self) -> str:
        return self._proto.text

    @text.setter
    def text(self, value: str):
        self._proto.text = value

    @property
    def name(self) -> str:
        return self._proto.name

    @name.setter
    def name(self, value: str):
        self._proto.name = value

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def attributes(self) -> TextAttributes:
        return TextAttributes(self._proto.attributes)

    @attributes.setter
    def attributes(self, value: TextAttributes):
        self._proto.attributes.CopyFrom(value.proto)


@register_wrapper(schematic_types_pb2.Symbol)
class Symbol(Item):
    """Represents a symbol in the schematic"""
    def __init__(self, proto: Optional[schematic_types_pb2.Symbol] = None):
        self._proto = schematic_types_pb2.Symbol()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def lib_id(self) -> LibraryIdentifier:
        return LibraryIdentifier(self._proto.lib_id)

    @lib_id.setter
    def lib_id(self, value: LibraryIdentifier):
        self._proto.lib_id.CopyFrom(value.proto)

    @property
    def unit(self) -> int:
        return self._proto.unit

    @unit.setter
    def unit(self, value: int):
        self._proto.unit = value

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def angle(self) -> float:
        return self._proto.angle.value_degrees

    @angle.setter
    def angle(self, value: float):
        self._proto.angle.value_degrees = value

    @property
    def mirror_x(self) -> bool:
        return self._proto.mirror_x

    @mirror_x.setter
    def mirror_x(self, value: bool):
        self._proto.mirror_x = value

    @property
    def mirror_y(self) -> bool:
        return self._proto.mirror_y

    @mirror_y.setter
    def mirror_y(self, value: bool):
        self._proto.mirror_y = value

    @property
    def dnp(self) -> bool:
        return self._proto.dnp

    @dnp.setter
    def dnp(self, value: bool):
        self._proto.dnp = value

    @property
    def fields(self) -> List[Field]:
        return [Field(f) for f in self._proto.fields]

    def get_field(self, name: str) -> Optional[Field]:
        """Get a field by name.

        Args:
            name: Field name (e.g., "Reference", "Value", "Footprint", "Datasheet")

        Returns:
            Field object if found, None otherwise
        """
        for f in self._proto.fields:
            if f.name == name:
                return Field(f)
        return None

    def set_field(self, name: str, value: str) -> bool:
        """Set a field value by name.

        Args:
            name: Field name (e.g., "Reference", "Value", "Footprint", "Datasheet")
            value: New value for the field

        Returns:
            True if field was found and updated, False if field doesn't exist

        Note:
            After calling this, you must call schematic.update_items(symbol)
            to persist the changes to KiCad.

        Example:
            >>> symbol.set_field("Value", "10k")
            >>> symbol.set_field("Footprint", "Resistor_SMD:R_0603_1608Metric")
            >>> schematic.update_items(symbol)  # Apply changes
        """
        for f in self._proto.fields:
            if f.name == name:
                f.text = value
                return True
        return False

    def add_field(self, name: str, value: str, position: Optional[Vector2] = None) -> Field:
        """Add a new field to the symbol.

        Args:
            name: Field name
            value: Field value/text
            position: Optional position (defaults to symbol position)

        Returns:
            The newly created Field object

        Note:
            After calling this, you must call schematic.update_items(symbol)
            to persist the changes to KiCad.
        """
        new_field = self._proto.fields.add()
        new_field.name = name
        new_field.text = value
        if position:
            new_field.position.CopyFrom(position.proto)
        else:
            new_field.position.CopyFrom(self._proto.position)
        return Field(new_field)

    @property
    def reference(self) -> str:
        """Get the reference designator (e.g., 'R1', 'C2', 'U3')."""
        field = self.get_field("Reference")
        return field.text if field else ""

    @reference.setter
    def reference(self, value: str):
        """Set the reference designator.

        Note: After setting, call schematic.update_items(symbol) to apply.
        """
        self.set_field("Reference", value)

    @property
    def value(self) -> str:
        """Get the component value (e.g., '10k', '100nF', 'ATmega328P')."""
        field = self.get_field("Value")
        return field.text if field else ""

    @value.setter
    def value(self, val: str):
        """Set the component value.

        Note: After setting, call schematic.update_items(symbol) to apply.
        """
        self.set_field("Value", val)

    @property
    def footprint(self) -> str:
        """Get the footprint (e.g., 'Resistor_SMD:R_0603_1608Metric')."""
        field = self.get_field("Footprint")
        return field.text if field else ""

    @footprint.setter
    def footprint(self, value: str):
        """Set the footprint.

        Note: After setting, call schematic.update_items(symbol) to apply.
        """
        self.set_field("Footprint", value)

    @property
    def datasheet(self) -> str:
        """Get the datasheet URL/path."""
        field = self.get_field("Datasheet")
        return field.text if field else ""

    @datasheet.setter
    def datasheet(self, value: str):
        """Set the datasheet URL/path.

        Note: After setting, call schematic.update_items(symbol) to apply.
        """
        self.set_field("Datasheet", value)

    @property
    def pins(self) -> List[Pin]:
        return [Pin(p) for p in self._proto.pins]

    def get_pin(self, pin_id: str) -> Optional[Pin]:
        """Get a pin by name or number.

        Args:
            pin_id: Pin name (e.g., "VCC", "GND") or number (e.g., "1", "2")

        Returns:
            Pin object if found, None otherwise
        """
        for p in self._proto.pins:
            if p.name == pin_id or p.number == pin_id:
                return Pin(p)
        return None


@register_wrapper(schematic_types_pb2.SheetPin)
class SheetPin(Item):
    """Represents a sheet pin (hierarchical connection point on a sheet)"""
    def __init__(self, proto: Optional[schematic_types_pb2.SheetPin] = None):
        self._proto = schematic_types_pb2.SheetPin()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def name(self) -> str:
        return self._proto.name

    @name.setter
    def name(self, value: str):
        self._proto.name = value

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def side(self) -> int:
        """Sheet pin side (SPS_LEFT=1, SPS_RIGHT=2, SPS_TOP=3, SPS_BOTTOM=4)"""
        return self._proto.side

    @side.setter
    def side(self, value: int):
        self._proto.side = value

    @property
    def shape(self) -> int:
        """Label shape (LS_INPUT=1, LS_OUTPUT=2, LS_BIDI=3, LS_TRISTATE=4, LS_UNSPECIFIED=5)"""
        return self._proto.shape

    @shape.setter
    def shape(self, value: int):
        self._proto.shape = value


@register_wrapper(schematic_types_pb2.Sheet)
class Sheet(Item):
    """Represents a hierarchical sheet in the schematic"""
    def __init__(self, proto: Optional[schematic_types_pb2.Sheet] = None):
        self._proto = schematic_types_pb2.Sheet()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.position.CopyFrom(value.proto)

    @property
    def size(self) -> Vector2:
        return Vector2(self._proto.size)

    @size.setter
    def size(self, value: Vector2):
        self._proto.size.CopyFrom(value.proto)

    @property
    def name(self) -> str:
        return self._proto.name

    @name.setter
    def name(self, value: str):
        self._proto.name = value

    @property
    def filename(self) -> str:
        return self._proto.filename

    @filename.setter
    def filename(self, value: str):
        self._proto.filename = value

    @property
    def page_number(self) -> str:
        return self._proto.page_number

    @property
    def dnp(self) -> bool:
        return self._proto.dnp

    @dnp.setter
    def dnp(self, value: bool):
        self._proto.dnp = value

    @property
    def exclude_from_bom(self) -> bool:
        return self._proto.exclude_from_bom

    @exclude_from_bom.setter
    def exclude_from_bom(self, value: bool):
        self._proto.exclude_from_bom = value

    @property
    def exclude_from_board(self) -> bool:
        return self._proto.exclude_from_board

    @exclude_from_board.setter
    def exclude_from_board(self, value: bool):
        self._proto.exclude_from_board = value

    @property
    def exclude_from_sim(self) -> bool:
        return self._proto.exclude_from_sim

    @exclude_from_sim.setter
    def exclude_from_sim(self, value: bool):
        self._proto.exclude_from_sim = value

    @property
    def pins(self) -> List[SheetPin]:
        return [SheetPin(p) for p in self._proto.pins]

    @property
    def fields(self) -> List[Field]:
        return [Field(f) for f in self._proto.fields]

    def get_field(self, name: str) -> Optional[Field]:
        for f in self._proto.fields:
            if f.name == name:
                return Field(f)
        return None


@register_wrapper(schematic_types_pb2.Text)
class SchematicText(Item):
    """Represents a text item in the schematic (SCH_TEXT)."""
    def __init__(self, proto: Optional[schematic_types_pb2.Text] = None):
        self._proto = schematic_types_pb2.Text()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def text(self) -> str:
        """The text content."""
        return self._proto.text.text

    @text.setter
    def text(self, value: str):
        self._proto.text.text = value

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.text.position)

    @position.setter
    def position(self, value: Vector2):
        self._proto.text.position.CopyFrom(value.proto)

    @property
    def hyperlink(self) -> str:
        """Optional hyperlink associated with the text."""
        return self._proto.text.hyperlink

    @hyperlink.setter
    def hyperlink(self, value: str):
        self._proto.text.hyperlink = value

    @property
    def layer(self) -> int:
        return self._proto.layer

    @layer.setter
    def layer(self, value: int):
        self._proto.layer = value

    @classmethod
    def create(cls, text: str, position: Vector2, layer: int = 0) -> "SchematicText":
        """Create a new schematic text item.

        Args:
            text: The text content
            position: Position in schematic internal units
            layer: Schematic layer (default 0)

        Returns:
            New SchematicText instance
        """
        sch_text = cls()
        sch_text.text = text
        sch_text.position = position
        sch_text.layer = layer
        return sch_text


@register_wrapper(schematic_types_pb2.SchematicGraphicShape)
class SchematicGraphicShape(Item):
    """Represents a graphic shape in the schematic (SCH_SHAPE)."""
    def __init__(self, proto: Optional[schematic_types_pb2.SchematicGraphicShape] = None):
        self._proto = schematic_types_pb2.SchematicGraphicShape()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def shape(self):
        """The underlying GraphicShape proto."""
        return self._proto.shape

    @property
    def layer(self) -> int:
        return self._proto.layer

    @layer.setter
    def layer(self, value: int):
        self._proto.layer = value

    @classmethod
    def create_rectangle(
        cls,
        top_left: Vector2,
        bottom_right: Vector2,
        stroke_width: int = 0,
        filled: bool = False,
        layer: int = 0,
    ) -> "SchematicGraphicShape":
        """Create a rectangle shape.

        Args:
            top_left: Top-left corner position
            bottom_right: Bottom-right corner position
            stroke_width: Line width in internal units (default 0 = default width)
            filled: Whether to fill the shape
            layer: Schematic layer (default 0)

        Returns:
            New SchematicGraphicShape instance
        """
        shape = cls()
        shape._proto.shape.rectangle.top_left.CopyFrom(top_left.proto)
        shape._proto.shape.rectangle.bottom_right.CopyFrom(bottom_right.proto)
        if stroke_width:
            shape._proto.shape.attributes.stroke.width.value_nm = stroke_width
        if filled:
            shape._proto.shape.attributes.fill.fill_type = 2  # GFT_FILLED
        shape.layer = layer
        return shape

    @classmethod
    def create_circle(
        cls,
        center: Vector2,
        radius_point: Vector2,
        stroke_width: int = 0,
        filled: bool = False,
        layer: int = 0,
    ) -> "SchematicGraphicShape":
        """Create a circle shape.

        Args:
            center: Center point of the circle
            radius_point: Point on the circle (defines radius)
            stroke_width: Line width in internal units (default 0 = default width)
            filled: Whether to fill the shape
            layer: Schematic layer (default 0)

        Returns:
            New SchematicGraphicShape instance
        """
        shape = cls()
        shape._proto.shape.circle.center.CopyFrom(center.proto)
        shape._proto.shape.circle.radius_point.CopyFrom(radius_point.proto)
        if stroke_width:
            shape._proto.shape.attributes.stroke.width.value_nm = stroke_width
        if filled:
            shape._proto.shape.attributes.fill.fill_type = 2  # GFT_FILLED
        shape.layer = layer
        return shape

    @classmethod
    def create_line(
        cls,
        start: Vector2,
        end: Vector2,
        stroke_width: int = 0,
        layer: int = 0,
    ) -> "SchematicGraphicShape":
        """Create a line segment shape.

        Args:
            start: Start point of the line
            end: End point of the line
            stroke_width: Line width in internal units (default 0 = default width)
            layer: Schematic layer (default 0)

        Returns:
            New SchematicGraphicShape instance
        """
        shape = cls()
        shape._proto.shape.segment.start.CopyFrom(start.proto)
        shape._proto.shape.segment.end.CopyFrom(end.proto)
        if stroke_width:
            shape._proto.shape.attributes.stroke.width.value_nm = stroke_width
        shape.layer = layer
        return shape

    @classmethod
    def create_arc(
        cls,
        start: Vector2,
        mid: Vector2,
        end: Vector2,
        stroke_width: int = 0,
        layer: int = 0,
    ) -> "SchematicGraphicShape":
        """Create an arc shape.

        Args:
            start: Start point of the arc
            mid: Mid point of the arc (defines curvature)
            end: End point of the arc
            stroke_width: Line width in internal units (default 0 = default width)
            layer: Schematic layer (default 0)

        Returns:
            New SchematicGraphicShape instance
        """
        shape = cls()
        shape._proto.shape.arc.start.CopyFrom(start.proto)
        shape._proto.shape.arc.mid.CopyFrom(mid.proto)
        shape._proto.shape.arc.end.CopyFrom(end.proto)
        if stroke_width:
            shape._proto.shape.attributes.stroke.width.value_nm = stroke_width
        shape.layer = layer
        return shape


@register_wrapper(schematic_types_pb2.TextBox)
class SchematicTextBox(Item):
    """Represents a text box in the schematic (SCH_TEXTBOX)."""
    def __init__(self, proto: Optional[schematic_types_pb2.TextBox] = None):
        self._proto = schematic_types_pb2.TextBox()
        if proto:
            self._proto.CopyFrom(proto)

    @property
    def id(self) -> base_types_pb2.KIID:
        return self._proto.id

    @property
    def text(self) -> str:
        """The text content."""
        return self._proto.text_box.text

    @text.setter
    def text(self, value: str):
        self._proto.text_box.text = value

    @property
    def layer(self) -> int:
        return self._proto.layer

    @layer.setter
    def layer(self, value: int):
        self._proto.layer = value

    @classmethod
    def create(
        cls,
        text: str,
        top_left: Vector2,
        bottom_right: Vector2,
        layer: int = 0,
    ) -> "SchematicTextBox":
        """Create a new schematic text box.

        Args:
            text: The text content
            top_left: Top-left corner position
            bottom_right: Bottom-right corner position
            layer: Schematic layer (default 0)

        Returns:
            New SchematicTextBox instance
        """
        textbox = cls()
        textbox.text = text
        textbox._proto.text_box.top_left.CopyFrom(top_left.proto)
        textbox._proto.text_box.bottom_right.CopyFrom(bottom_right.proto)
        textbox.layer = layer
        return textbox


# Bus entry wrapper - wrapped in try/except since proto may not exist yet
try:
    @register_wrapper(schematic_types_pb2.BusEntry)
    class BusEntry(Item):
        """Represents a bus entry (wire-to-bus or bus-to-bus) in the schematic."""
        def __init__(self, proto: Optional[schematic_types_pb2.BusEntry] = None):
            self._proto = schematic_types_pb2.BusEntry()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def position(self) -> Vector2:
            return Vector2(self._proto.position)

        @position.setter
        def position(self, value: Vector2):
            self._proto.position.CopyFrom(value.proto)

        @property
        def size(self) -> Vector2:
            return Vector2(self._proto.size)

        @size.setter
        def size(self, value: Vector2):
            self._proto.size.CopyFrom(value.proto)

        @property
        def entry_type(self) -> int:
            """Bus entry type: BET_WIRE_TO_BUS (1) or BET_BUS_TO_BUS (2)"""
            return self._proto.type

        @entry_type.setter
        def entry_type(self, value: int):
            self._proto.type = value

        @classmethod
        def create_wire_to_bus(cls, position: Vector2, size: Optional[Vector2] = None) -> "BusEntry":
            """Create a wire-to-bus entry.

            Args:
                position: Position of the bus entry
                size: Size/offset to end position. Default is (2.54mm, 2.54mm).

            Returns:
                New BusEntry instance
            """
            entry = cls()
            entry.position = position
            if size:
                entry.size = size
            else:
                # Default size: 2.54mm diagonal (in nm)
                entry._proto.size.x_nm = 2540000
                entry._proto.size.y_nm = 2540000
            entry._proto.type = schematic_types_pb2.BET_WIRE_TO_BUS
            return entry

        @classmethod
        def create_bus_to_bus(cls, position: Vector2, size: Optional[Vector2] = None) -> "BusEntry":
            """Create a bus-to-bus entry.

            Args:
                position: Position of the bus entry
                size: Size/offset to end position. Default is (2.54mm, 2.54mm).

            Returns:
                New BusEntry instance
            """
            entry = cls()
            entry.position = position
            if size:
                entry.size = size
            else:
                # Default size: 2.54mm diagonal (in nm)
                entry._proto.size.x_nm = 2540000
                entry._proto.size.y_nm = 2540000
            entry._proto.type = schematic_types_pb2.BET_BUS_TO_BUS
            return entry

except AttributeError:
    # Proto type not yet available - will be after regeneration
    BusEntry = None


# Bitmap wrapper - wrapped in try/except since proto may not exist yet
try:
    @register_wrapper(schematic_types_pb2.Bitmap)
    class Bitmap(Item):
        """Represents a bitmap/image in the schematic."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.Bitmap()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def position(self) -> Vector2:
            return Vector2(self._proto.position)

        @position.setter
        def position(self, value: Vector2):
            self._proto.position.CopyFrom(value.proto)

        @property
        def scale(self) -> float:
            """Scale factor (1.0 = original size)."""
            return self._proto.scale

        @scale.setter
        def scale(self, value: float):
            self._proto.scale = value

        @property
        def data(self) -> bytes:
            """Image data (PNG format)."""
            return self._proto.data

        @data.setter
        def data(self, value: bytes):
            self._proto.data = value

        @classmethod
        def create(cls, position: Vector2, image_path: str, scale: float = 1.0) -> "Bitmap":
            """Create a new bitmap from an image file.

            Args:
                position: Position for the image center
                image_path: Path to the image file (PNG recommended)
                scale: Scale factor (1.0 = original size)

            Returns:
                New Bitmap instance
            """
            bitmap = cls()
            bitmap.position = position
            bitmap.scale = scale
            # Read image data
            with open(image_path, 'rb') as f:
                bitmap.data = f.read()
            return bitmap

except AttributeError:
    # Proto type not yet available - will be after regeneration
    Bitmap = None


# Table wrapper - wrapped in try/except since proto may not exist yet
try:
    @register_wrapper(schematic_types_pb2.TableCell)
    class TableCell(Item):
        """Represents a table cell in the schematic."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.TableCell()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def text(self) -> str:
            return self._proto.text

        @text.setter
        def text(self, value: str):
            self._proto.text = value

        @property
        def col_span(self) -> int:
            return self._proto.col_span

        @col_span.setter
        def col_span(self, value: int):
            self._proto.col_span = value

        @property
        def row_span(self) -> int:
            return self._proto.row_span

        @row_span.setter
        def row_span(self, value: int):
            self._proto.row_span = value

    @register_wrapper(schematic_types_pb2.Table)
    class Table(Item):
        """Represents a table in the schematic."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.Table()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def position(self) -> Vector2:
            return Vector2(self._proto.position)

        @position.setter
        def position(self, value: Vector2):
            self._proto.position.CopyFrom(value.proto)

        @property
        def columns(self) -> int:
            return self._proto.columns

        @columns.setter
        def columns(self, value: int):
            self._proto.columns = value

        @property
        def rows(self) -> int:
            return self._proto.rows

        @rows.setter
        def rows(self, value: int):
            self._proto.rows = value

        @property
        def cells(self) -> List[TableCell]:
            return [TableCell(c) for c in self._proto.cells]

        @property
        def header_on(self) -> bool:
            return self._proto.header_on

        @header_on.setter
        def header_on(self, value: bool):
            self._proto.header_on = value

        @classmethod
        def create(
            cls,
            position: Vector2,
            columns: int,
            rows: int,
            col_width: int = 0,
            row_height: int = 0,
            header_on: bool = False,
        ) -> "Table":
            """Create a new table.

            Args:
                position: Position for the table
                columns: Number of columns
                rows: Number of rows
                col_width: Default column width in nm (0 for default)
                row_height: Default row height in nm (0 for default)
                header_on: Whether first row is a header

            Returns:
                New Table instance
            """
            table = cls()
            table.position = position
            table.columns = columns
            table.rows = rows
            table._proto.col_width = col_width
            table._proto.row_height = row_height
            table.header_on = header_on
            return table

except AttributeError:
    # Proto types not yet available - will be after regeneration
    TableCell = None
    Table = None


# SchematicGroup wrapper - wrapped in try/except since proto may not exist yet
try:
    @register_wrapper(schematic_types_pb2.SchematicGroup)
    class SchematicGroup(Item):
        """Represents a group of schematic items."""
        def __init__(self, proto=None):
            self._proto = schematic_types_pb2.SchematicGroup()
            if proto:
                self._proto.CopyFrom(proto)

        @property
        def id(self) -> base_types_pb2.KIID:
            return self._proto.id

        @property
        def name(self) -> str:
            return self._proto.name

        @name.setter
        def name(self, value: str):
            self._proto.name = value

        @property
        def members(self) -> List[base_types_pb2.KIID]:
            """List of member item IDs."""
            return list(self._proto.members)

        @property
        def locked(self) -> bool:
            return self._proto.locked

        @locked.setter
        def locked(self, value: bool):
            self._proto.locked = value

        @classmethod
        def create(cls, name: str, members: List[str] = None) -> "SchematicGroup":
            """Create a new group.

            Args:
                name: Name for the group
                members: Optional list of member item IDs

            Returns:
                New SchematicGroup instance
            """
            group = cls()
            group.name = name
            if members:
                for member_id in members:
                    kiid = base_types_pb2.KIID()
                    kiid.value = member_id
                    group._proto.members.append(kiid)
            return group

except AttributeError:
    # Proto type not yet available - will be after regeneration
    SchematicGroup = None
