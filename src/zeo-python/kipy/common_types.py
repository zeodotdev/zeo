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

import sys
from typing import Optional, Sequence
from kipy.proto.common import types
from kipy.proto.common.types import base_types_pb2
from kipy.proto.common.types.base_types_pb2 import KIID
from kipy.geometry import (
    Box2,
    PolygonWithHoles,
    Vector2,
    arc_angle,
    arc_center,
    arc_radius,
    arc_start_angle,
    arc_end_angle,
)
from kipy.wrapper import Wrapper

# Re-exported protobuf enum types
from kipy.proto.common.types.enums_pb2 import (  # noqa
    HorizontalAlignment,
    VerticalAlignment,
)

if sys.version_info >= (3, 13):
    from warnings import deprecated
else:
    from typing_extensions import deprecated

class Commit:
    def __init__(self, id: KIID):
        self._id = id

    @property
    def id(self) -> KIID:
        return self._id


class SheetPath(Wrapper):
    """Represents the path to a unique sheet instance or symbol instance in a schematic"""

    def __init__(
        self,
        proto: Optional[types.SheetPath] = None,
        proto_ref: Optional[types.SheetPath] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.SheetPath()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def path(self) -> list[KIID]:
        return list(self._proto.path)

    @path.setter
    def path(self, path: list[KIID]):
        del self._proto.path[:]
        self._proto.path.extend(path)

    @property
    def path_human_readable(self) -> str:
        """The sheet path with human-readable sheet names.  May not be available in all contexts
        (for example, is not present in contexts where the SheetPath is sourced from a board
        object)"""
        return self._proto.path_human_readable

class Color(Wrapper):
    def __init__(
        self,
        proto: Optional[types.Color] = None,
        proto_ref: Optional[types.Color] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.Color()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def red(self) -> float:
        return self._proto.r

    @red.setter
    def red(self, red: float):
        self._proto.r = red

    @property
    def green(self) -> float:
        return self._proto.g

    @green.setter
    def green(self, green: float):
        self._proto.g = green

    @property
    def blue(self) -> float:
        return self._proto.b

    @blue.setter
    def blue(self, blue: float):
        self._proto.b = blue

    @property
    def alpha(self) -> float:
        return self._proto.a

    @alpha.setter
    def alpha(self, alpha: float):
        self._proto.a = alpha


class TextAttributes(Wrapper):
    def __init__(
        self,
        proto: Optional[types.TextAttributes] = None,
        proto_ref: Optional[types.TextAttributes] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.TextAttributes()

        if proto is not None:
            self._proto.CopyFrom(proto)

    def __repr__(self) -> str:
        return (
            f"TextAttributes(font_name={self.font_name}, angle={self.angle}, "
            f"line_spacing={self.line_spacing}, italic={self.italic}, bold={self.bold}, "
            f"underlined={self.underlined}, mirrored={self.mirrored}, multiline={self.multiline}, "
            f"keep_upright={self.keep_upright}, size={self.size})"
        )

    @property
    @deprecated("This property will always return True in KiCad 9, and will be removed in KiCad 10")
    def visible(self) -> bool:
        """
        .. deprecated:: 0.3.0 removed in KiCad 9.0.1
        Text items are always visible as of 9.0.1, only Fields can be set to hidden
        """
        return self._proto.visible

    @visible.setter
    def visible(self, visible: bool):
        self._proto.visible = visible

    @property
    def font_name(self) -> str:
        return self._proto.font_name

    @font_name.setter
    def font_name(self, font_name: str):
        self._proto.font_name = font_name

    @property
    def angle(self) -> float:
        """The orientation of the text in degrees"""
        return self._proto.angle.value_degrees

    @angle.setter
    def angle(self, angle: float):
        self._proto.angle.value_degrees = angle

    @property
    def line_spacing(self) -> float:
        return self._proto.line_spacing

    @line_spacing.setter
    def line_spacing(self, line_spacing: float):
        self._proto.line_spacing = line_spacing

    @property
    def stroke_width(self) -> int:
        return self._proto.stroke_width.value_nm

    @stroke_width.setter
    def stroke_width(self, stroke_width: int):
        self._proto.stroke_width.value_nm = stroke_width

    @property
    def italic(self) -> bool:
        return self._proto.italic

    @italic.setter
    def italic(self, italic: bool):
        self._proto.italic = italic

    @property
    def bold(self) -> bool:
        return self._proto.bold

    @bold.setter
    def bold(self, bold: bool):
        self._proto.bold = bold

    @property
    def underlined(self) -> bool:
        return self._proto.underlined

    @underlined.setter
    def underlined(self, underlined: bool):
        self._proto.underlined = underlined

    @property
    def mirrored(self) -> bool:
        return self._proto.mirrored

    @mirrored.setter
    def mirrored(self, mirrored: bool):
        self._proto.mirrored = mirrored

    @property
    def multiline(self) -> bool:
        return self._proto.multiline

    @multiline.setter
    def multiline(self, multiline: bool):
        self._proto.multiline = multiline

    @property
    def keep_upright(self) -> bool:
        return self._proto.keep_upright

    @keep_upright.setter
    def keep_upright(self, keep_upright: bool):
        self._proto.keep_upright = keep_upright

    @property
    def size(self) -> Vector2:
        return Vector2(self._proto.size)

    @size.setter
    def size(self, size: Vector2):
        self._proto.size.CopyFrom(size.proto)

    @property
    def horizontal_alignment(self) -> types.HorizontalAlignment.ValueType:
        return self._proto.horizontal_alignment

    @horizontal_alignment.setter
    def horizontal_alignment(self, alignment: types.HorizontalAlignment.ValueType):
        self._proto.horizontal_alignment = alignment

    @property
    def vertical_alignment(self) -> types.VerticalAlignment.ValueType:
        return self._proto.vertical_alignment

    @vertical_alignment.setter
    def vertical_alignment(self, alignment: types.VerticalAlignment.ValueType):
        self._proto.vertical_alignment = alignment


class LibraryIdentifier(Wrapper):
    """A KiCad library identifier (LIB_ID), consisting of a library nickname and entry name"""

    def __init__(
        self,
        proto: Optional[types.LibraryIdentifier] = None,
        proto_ref: Optional[types.LibraryIdentifier] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.LibraryIdentifier()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def library(self) -> str:
        return self._proto.library_nickname

    @library.setter
    def library(self, library: str):
        self._proto.library_nickname = library

    @property
    def name(self) -> str:
        return self._proto.entry_name

    @name.setter
    def name(self, name: str):
        self._proto.entry_name = name

    def __str__(self) -> str:
        return f"{self.library}:{self.name}"


class StrokeAttributes(Wrapper):
    def __init__(
        self,
        proto: Optional[types.StrokeAttributes] = None,
        proto_ref: Optional[types.StrokeAttributes] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.StrokeAttributes()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def color(self) -> Color:
        """The stroke color.  Only supported in schematic graphics."""
        return Color(proto_ref=self._proto.color)

    @color.setter
    def color(self, color: Color):
        self._proto.color.CopyFrom(color.proto)

    @property
    def width(self) -> int:
        """The stroke line width in nanometers"""
        return self._proto.width.value_nm

    @width.setter
    def width(self, width: int):
        self._proto.width.value_nm = width

    @property
    def style(self) -> types.StrokeLineStyle.ValueType:
        return self._proto.style

    @style.setter
    def style(self, style: types.StrokeLineStyle.ValueType):
        self._proto.style = style


class GraphicFillAttributes(Wrapper):
    def __init__(
        self,
        proto: Optional[types.GraphicFillAttributes] = None,
        proto_ref: Optional[types.GraphicFillAttributes] = None,
    ):
        self._proto = (
            proto_ref if proto_ref is not None else types.GraphicFillAttributes()
        )

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def filled(self) -> bool:
        return self._proto.fill_type == types.GraphicFillType.GFT_FILLED

    @filled.setter
    def filled(self, fill: bool):
        self._proto.fill_type = (
            types.GraphicFillType.GFT_FILLED
            if fill
            else types.GraphicFillType.GFT_UNFILLED
        )

    @property
    def color(self) -> Color:
        """The fill color.  Only supported in schematic graphics."""
        return Color(proto_ref=self._proto.color)

    @color.setter
    def color(self, color: Color):
        self._proto.color.CopyFrom(color.proto)


class GraphicAttributes(Wrapper):
    def __init__(
        self,
        proto: Optional[types.GraphicAttributes] = None,
        proto_ref: Optional[types.GraphicAttributes] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.GraphicAttributes()

        if proto is not None:
            self._proto.CopyFrom(proto)

        self._stroke = StrokeAttributes(proto_ref=self._proto.stroke)
        self._fill = GraphicFillAttributes(proto_ref=self._proto.fill)

    @property
    def stroke(self) -> StrokeAttributes:
        return self._stroke

    @property
    def fill(self) -> GraphicFillAttributes:
        return self._fill


class Text(Wrapper):
    """Common text properties (wrapper for KiCad's EDA_TEXT) shared between board and schematic"""

    def __init__(
        self, proto: Optional[types.Text] = None, proto_ref: Optional[types.Text] = None
    ):
        self._proto = proto_ref if proto_ref is not None else types.Text()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def position(self) -> Vector2:
        return Vector2(self._proto.position)

    @position.setter
    def position(self, pos: Vector2):
        self._proto.position.CopyFrom(pos.proto)

    @property
    def value(self) -> str:
        return self._proto.text

    @value.setter
    def value(self, text: str):
        self._proto.text = text

    @property
    def attributes(self) -> TextAttributes:
        return TextAttributes(proto_ref=self._proto.attributes)

    @attributes.setter
    def attributes(self, attributes: TextAttributes):
        self._proto.attributes.CopyFrom(attributes.proto)


class TextBox(Wrapper):
    def __init__(
        self,
        proto: Optional[types.TextBox] = None,
        proto_ref: Optional[types.TextBox] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.TextBox()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def top_left(self) -> Vector2:
        return Vector2(self._proto.top_left)

    @top_left.setter
    def top_left(self, pos: Vector2):
        self._proto.top_left.CopyFrom(pos.proto)

    @property
    def bottom_right(self) -> Vector2:
        return Vector2(self._proto.bottom_right)

    @bottom_right.setter
    def bottom_right(self, pos: Vector2):
        self._proto.bottom_right.CopyFrom(pos.proto)

    @property
    def attributes(self) -> TextAttributes:
        return TextAttributes(proto_ref=self._proto.attributes)

    @attributes.setter
    def attributes(self, attributes: TextAttributes):
        self._proto.attributes.CopyFrom(attributes.proto)

    @property
    def value(self) -> str:
        return self._proto.text

    @value.setter
    def value(self, text: str):
        self._proto.text = text

    @property
    def size(self) -> Vector2:
        return self.bottom_right - self.top_left

    @size.setter
    def size(self, size: Vector2):
        new_br = self.top_left + size
        self._proto.bottom_right.CopyFrom(new_br.proto)


class GraphicShape(Wrapper):
    """Represents an abstract graphic shape (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

    @property
    def attributes(self) -> GraphicAttributes:
        return GraphicAttributes(proto_ref=self._graphic_proto.attributes)

    @attributes.setter
    def attributes(self, attributes: GraphicAttributes):
        self._graphic_proto.attributes.CopyFrom(attributes.proto)

    def bounding_box(self) -> Box2:
        raise NotImplementedError(
            f"bounding_box() not implemented for {type(self).__name__}"
        )


class Segment(GraphicShape):
    """Represents a base graphic segment (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "segment"

    @property
    def start(self) -> Vector2:
        return Vector2(self._graphic_proto.segment.start)

    @start.setter
    def start(self, point: Vector2):
        self._graphic_proto.segment.start.CopyFrom(point.proto)

    @property
    def end(self) -> Vector2:
        return Vector2(self._graphic_proto.segment.end)

    @end.setter
    def end(self, point: Vector2):
        self._graphic_proto.segment.end.CopyFrom(point.proto)

    def bounding_box(self) -> Box2:
        """Calculates the bounding box of the segment"""
        box = Box2()
        box.merge(self.start)
        box.merge(self.end)
        return box


class Arc(GraphicShape):
    """Represents a generic graphical arc (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "arc"

    @property
    def start(self) -> Vector2:
        return Vector2(self._graphic_proto.arc.start)

    @start.setter
    def start(self, point: Vector2):
        self._graphic_proto.arc.start.CopyFrom(point.proto)

    @property
    def mid(self) -> Vector2:
        return Vector2(self._graphic_proto.arc.mid)

    @mid.setter
    def mid(self, point: Vector2):
        self._graphic_proto.arc.mid.CopyFrom(point.proto)

    @property
    def end(self) -> Vector2:
        return Vector2(self._graphic_proto.arc.end)

    @end.setter
    def end(self, point: Vector2):
        self._graphic_proto.arc.end.CopyFrom(point.proto)

    def center(self) -> Optional[Vector2]:
        """
        Calculates the center of the arc.  Uses a different algorithm than KiCad so may have
        slightly different results.  The KiCad API preserves the start, middle, and end points of
        the arc, so any other properties such as the center point and angles must be calculated

        :return: The center of the arc, or None if the arc is degenerate
        """
        # TODO we may want to add an API call to get KiCad to calculate this for us,
        # for situations where matching KiCad's behavior exactly is important
        return arc_center(self.start, self.mid, self.end)

    def radius(self) -> float:
        """
        Calculates the radius of the arc.  Uses a different algorithm than KiCad so may have
        slightly different results.  The KiCad API preserves the start, middle, and end points of
        the arc, so any other properties such as the center point and angles must be calculated

        :return: The radius of the arc, or 0 if the arc is degenerate
        """
        # TODO we may want to add an API call to get KiCad to calculate this for us,
        # for situations where matching KiCad's behavior exactly is important
        return arc_radius(self.start, self.mid, self.end)

    def start_angle(self) -> Optional[float]:
        return arc_start_angle(self.start, self.mid, self.end)

    def end_angle(self) -> Optional[float]:
        return arc_end_angle(self.start, self.mid, self.end)
    
    def angle(self) -> Optional[float]:
        """Calculates the angle between the start and end of the arc in radians

        :return: The angle of the arc, or None if the arc is degenerate
        .. versionadded:: 0.4.0"""
        return arc_angle(self.start, self.mid, self.end)

    def bounding_box(self) -> Box2:
        box = Box2()
        box.merge(self.start)
        box.merge(self.end)
        box.merge(self.mid)
        return box


class Circle(GraphicShape):
    """Represents a graphic circle (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "circle"

    @property
    def center(self) -> Vector2:
        return Vector2(self._graphic_proto.circle.center)

    @center.setter
    def center(self, point: Vector2):
        self._graphic_proto.circle.center.CopyFrom(point.proto)

    @property
    def radius_point(self) -> Vector2:
        return Vector2(self._graphic_proto.circle.radius_point)

    @radius_point.setter
    def radius_point(self, radius_point: Vector2):
        self._graphic_proto.circle.radius_point.CopyFrom(radius_point.proto)

    def radius(self) -> float:
        """Calculates the radius of the circle"""
        return (self.radius_point - self.center).length()

    def bounding_box(self) -> Box2:
        """Calculates the bounding box of the circle"""
        box = Box2()
        box.merge(self.center)
        box.inflate(int(self.radius() + 0.5))
        return box


class Rectangle(GraphicShape):
    """Represents a graphic rectangle (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "rectangle"

    @property
    def top_left(self) -> Vector2:
        return Vector2(self._graphic_proto.rectangle.top_left)

    @top_left.setter
    def top_left(self, point: Vector2):
        self._graphic_proto.rectangle.top_left.CopyFrom(point.proto)

    @property
    def bottom_right(self) -> Vector2:
        return Vector2(self._graphic_proto.rectangle.bottom_right)

    @bottom_right.setter
    def bottom_right(self, point: Vector2):
        self._graphic_proto.rectangle.bottom_right.CopyFrom(point.proto)

    def bounding_box(self) -> Box2:
        """Calculates the bounding box of the rectangle"""
        return Box2.from_pos_size(self.top_left, self.bottom_right - self.top_left)


class Polygon(GraphicShape):
    """Represents a graphic polygon (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "polygon"
        self._polygons = [
            PolygonWithHoles(proto_ref=p) for p in self._graphic_proto.polygon.polygons
        ]

    def _pack(self):
        self._graphic_proto.polygon.ClearField('polygons')
        self._graphic_proto.polygon.polygons.extend([
            polygon.proto for polygon in self._polygons
        ])

    @property
    def polygons(self) -> list[PolygonWithHoles]:
        return self._polygons

    def bounding_box(self) -> Box2:
        """Calculates the bounding box of the polygon"""
        box = None
        for polygon in self.polygons:
            if box is None:
                box = polygon.bounding_box()
            else:
                box.merge(polygon.bounding_box())
        return box if box is not None else Box2()


class Bezier(GraphicShape):
    """Represents a graphic bezier curve (not a board or schematic item)"""

    def __init__(self, proto: Optional[base_types_pb2.GraphicShape] = None,
                 proto_ref: Optional[base_types_pb2.GraphicShape] = None):
        self._graphic_proto = proto_ref if proto_ref is not None else base_types_pb2.GraphicShape()

        if proto is not None:
            self._graphic_proto.CopyFrom(proto)

        assert self._graphic_proto.WhichOneof("geometry") == "bezier"

    @property
    def start(self) -> Vector2:
        return Vector2(self._graphic_proto.bezier.start)

    @start.setter
    def start(self, point: Vector2):
        self._graphic_proto.bezier.start.CopyFrom(point.proto)

    @property
    def control1(self) -> Vector2:
        return Vector2(self._graphic_proto.bezier.control1)

    @control1.setter
    def control1(self, point: Vector2):
        self._graphic_proto.bezier.control1.CopyFrom(point.proto)

    @property
    def control2(self) -> Vector2:
        return Vector2(self._graphic_proto.bezier.control2)

    @control2.setter
    def control2(self, point: Vector2):
        self._graphic_proto.bezier.control2.CopyFrom(point.proto)

    @property
    def end(self) -> Vector2:
        return Vector2(self._graphic_proto.bezier.end)

    @end.setter
    def end(self, point: Vector2):
        self._graphic_proto.bezier.end.CopyFrom(point.proto)

    def bounding_box(self) -> Box2:
        # TODO: maybe bring in a library for Bezier curve math so we can generate an
        # bounding box from the curve approximation like KiCad does?
        raise NotImplementedError()


def to_concrete_shape(shape: GraphicShape) -> Optional[GraphicShape]:
    cls = {
        "segment": Segment,
        "arc": Arc,
        "circle": Circle,
        "rectangle": Rectangle,
        "polygon": Polygon,
        "bezier": Bezier,
        None: None,
    }.get(shape._graphic_proto.WhichOneof("geometry"), None)

    return cls(shape._graphic_proto) if cls is not None else None


class CompoundShape(Wrapper):
    """Represents a compound shape (a collection of other shapes)"""

    def __init__(self, proto: Optional[base_types_pb2.CompoundShape] = None):
        self._proto = base_types_pb2.CompoundShape()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def shapes(self) -> Sequence[GraphicShape]:
        return [
            shape
            for shape in (
                to_concrete_shape(GraphicShape(subshape))
                for subshape in self._proto.shapes
            )
            if shape is not None
        ]

    def __iter__(self):
        return iter(self.shapes)

    def __len__(self):
        return len(self._proto.shapes)

    def __getitem__(self, index):
        return self.shapes[index]

    def __setitem__(self, index, shape: GraphicShape):
        self._proto.shapes[index].CopyFrom(shape._graphic_proto)

    def __delitem__(self, index):
        del self._proto.shapes[index]

    def append(self, shape: GraphicShape):
        new_shape = self._proto.shapes.add()
        new_shape.CopyFrom(shape.proto)


class TitleBlockInfo(Wrapper):
    def __init__(
        self,
        proto: Optional[types.TitleBlockInfo] = None,
        proto_ref: Optional[types.TitleBlockInfo] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else types.TitleBlockInfo()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def title(self) -> str:
        return self._proto.title

    @title.setter
    def title(self, title: str):
        self._proto.title = title

    @property
    def date(self) -> str:
        return self._proto.date

    @date.setter
    def date(self, date: str):
        self._proto.date = date

    @property
    def revision(self) -> str:
        return self._proto.revision

    @revision.setter
    def revision(self, revision: str):
        self._proto.revision = revision

    @property
    def company(self) -> str:
        return self._proto.company

    @company.setter
    def company(self, company: str):
        self._proto.company = company

    @property
    def comments(self) -> dict[int, str]:
        return {
            1: self._proto.comment1,
            2: self._proto.comment2,
            3: self._proto.comment3,
            4: self._proto.comment4,
            5: self._proto.comment5,
            6: self._proto.comment6,
            7: self._proto.comment7,
            8: self._proto.comment8,
            9: self._proto.comment9,
        }

    @comments.setter
    def comments(self, comments: dict[int, str]):
        if 1 in comments:
            self._proto.comment1 = comments[1]
        if 2 in comments:
            self._proto.comment2 = comments[2]
        if 3 in comments:
            self._proto.comment3 = comments[3]
        if 4 in comments:
            self._proto.comment4 = comments[4]
        if 5 in comments:
            self._proto.comment5 = comments[5]
        if 6 in comments:
            self._proto.comment6 = comments[6]
        if 7 in comments:
            self._proto.comment7 = comments[7]
        if 8 in comments:
            self._proto.comment8 = comments[8]
        if 9 in comments:
            self._proto.comment9 = comments[9]


# Page size type constants matching the proto enum
class PageSizeType:
    """Standard paper size types."""
    UNKNOWN = 0
    A5 = 1
    A4 = 2
    A3 = 3
    A2 = 4
    A1 = 5
    A0 = 6
    ANSI_A = 7       # ANSI A (Letter)
    ANSI_B = 8       # ANSI B (Tabloid/Ledger)
    ANSI_C = 9       # ANSI C
    ANSI_D = 10      # ANSI D
    ANSI_E = 11      # ANSI E
    GERBER = 12
    US_LETTER = 13
    US_LEGAL = 14
    US_LEDGER = 15
    USER = 16        # Custom size

    # String names for each type
    _NAMES = {
        0: "Unknown", 1: "A5", 2: "A4", 3: "A3", 4: "A2", 5: "A1", 6: "A0",
        7: "ANSI A", 8: "ANSI B", 9: "ANSI C", 10: "ANSI D", 11: "ANSI E",
        12: "Gerber", 13: "US Letter", 14: "US Legal", 15: "US Ledger", 16: "Custom"
    }

    @classmethod
    def name(cls, value: int) -> str:
        """Get the display name for a page size type."""
        return cls._NAMES.get(value, "Unknown")


class PageInfo(Wrapper):
    """Page/paper settings for a document.

    Attributes:
        size_type: Paper size type (A4, Letter, etc.) - see PageSizeType constants
        portrait: True for portrait orientation, False for landscape
        width_mm: Page width in millimeters
        height_mm: Page height in millimeters
    """

    def __init__(
        self,
        proto: Optional[base_types_pb2.PageInfo] = None,
        proto_ref: Optional[base_types_pb2.PageInfo] = None,
    ):
        self._proto = proto_ref if proto_ref is not None else base_types_pb2.PageInfo()

        if proto is not None:
            self._proto.CopyFrom(proto)

    @property
    def size_type(self) -> int:
        """Paper size type (use PageSizeType constants)."""
        return self._proto.size_type

    @size_type.setter
    def size_type(self, value: int):
        self._proto.size_type = value

    @property
    def size_type_name(self) -> str:
        """Human-readable name for the paper size type."""
        return PageSizeType.name(self._proto.size_type)

    @property
    def portrait(self) -> bool:
        """True for portrait, False for landscape."""
        return self._proto.portrait

    @portrait.setter
    def portrait(self, value: bool):
        self._proto.portrait = value

    @property
    def width_mm(self) -> float:
        """Page width in millimeters."""
        return self._proto.width_mm

    @width_mm.setter
    def width_mm(self, value: float):
        self._proto.width_mm = value

    @property
    def height_mm(self) -> float:
        """Page height in millimeters."""
        return self._proto.height_mm

    @height_mm.setter
    def height_mm(self, value: float):
        self._proto.height_mm = value

    def __repr__(self) -> str:
        orientation = "portrait" if self.portrait else "landscape"
        return f"PageInfo({self.size_type_name}, {orientation}, {self.width_mm:.1f}x{self.height_mm:.1f}mm)"
