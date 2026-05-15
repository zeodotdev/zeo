"""Tests for SpatialIndex, bboxes_overlap, and wire_segments_to_bboxes from bbox.py.

These are pure function tests — no KiCad connection or mocks required.
The functions are normally prepended to tool scripts via python_tool_handler;
here we import them directly by manipulating sys.path.
"""
import sys
import os
import pytest

# bbox.py lives in the agent tools tree, not in kipy.  Add its parent so we can
# import it as a regular module for testing.
_BBOX_DIR = os.path.join(
    os.path.dirname(__file__), '..', '..', '..', '..', 'agent', 'tools', 'python', 'common'
)
sys.path.insert(0, os.path.abspath(_BBOX_DIR))

# bbox.py uses get_uuid_str from preamble.py which is normally prepended.
# Provide a stub so the module-level code doesn't fail.
import builtins
if not hasattr(builtins, 'get_uuid_str'):
    builtins.get_uuid_str = lambda obj: getattr(obj, '_uuid', '')

import bbox as bbox_mod

bboxes_overlap = bbox_mod.bboxes_overlap
SpatialIndex = bbox_mod.SpatialIndex


# ---------------------------------------------------------------------------
# bboxes_overlap
# ---------------------------------------------------------------------------

class TestBboxesOverlap:
    def test_clear_overlap(self):
        a = {'min_x': 0, 'max_x': 10, 'min_y': 0, 'max_y': 10}
        b = {'min_x': 5, 'max_x': 15, 'min_y': 5, 'max_y': 15}
        assert bboxes_overlap(a, b) is True

    def test_disjoint_x(self):
        a = {'min_x': 0, 'max_x': 5, 'min_y': 0, 'max_y': 10}
        b = {'min_x': 10, 'max_x': 20, 'min_y': 0, 'max_y': 10}
        assert bboxes_overlap(a, b) is False

    def test_disjoint_y(self):
        a = {'min_x': 0, 'max_x': 10, 'min_y': 0, 'max_y': 5}
        b = {'min_x': 0, 'max_x': 10, 'min_y': 10, 'max_y': 20}
        assert bboxes_overlap(a, b) is False

    def test_touching_edge_no_overlap(self):
        """Touching edges (within epsilon) should NOT count as overlap."""
        a = {'min_x': 0, 'max_x': 10, 'min_y': 0, 'max_y': 10}
        b = {'min_x': 10, 'max_x': 20, 'min_y': 0, 'max_y': 10}
        assert bboxes_overlap(a, b, eps=0.001) is False

    def test_contained(self):
        outer = {'min_x': 0, 'max_x': 20, 'min_y': 0, 'max_y': 20}
        inner = {'min_x': 5, 'max_x': 15, 'min_y': 5, 'max_y': 15}
        assert bboxes_overlap(outer, inner) is True
        assert bboxes_overlap(inner, outer) is True

    def test_near_miss_within_epsilon(self):
        """Gap smaller than epsilon should NOT overlap."""
        a = {'min_x': 0, 'max_x': 10, 'min_y': 0, 'max_y': 10}
        b = {'min_x': 10.0005, 'max_x': 20, 'min_y': 0, 'max_y': 10}
        assert bboxes_overlap(a, b, eps=0.001) is False

    def test_tiny_overlap_beyond_epsilon(self):
        """Overlap just beyond epsilon should register."""
        a = {'min_x': 0, 'max_x': 10, 'min_y': 0, 'max_y': 10}
        b = {'min_x': 9.99, 'max_x': 20, 'min_y': 0, 'max_y': 10}
        assert bboxes_overlap(a, b, eps=0.001) is True


# ---------------------------------------------------------------------------
# SpatialIndex
# ---------------------------------------------------------------------------

class TestSpatialIndex:
    def test_empty_index(self):
        si = SpatialIndex(cell_size=10.0)
        query = {'min_x': 0, 'max_x': 5, 'min_y': 0, 'max_y': 5}
        assert si.any_overlap(query) is False
        assert si.query_overlaps(query) == []

    def test_insert_and_query(self):
        si = SpatialIndex(cell_size=10.0)
        box_a = {'min_x': 0, 'max_x': 5, 'min_y': 0, 'max_y': 5}
        si.insert(box_a)

        # Overlapping query
        assert si.any_overlap({'min_x': 3, 'max_x': 8, 'min_y': 3, 'max_y': 8}) is True
        # Non-overlapping query
        assert si.any_overlap({'min_x': 20, 'max_x': 25, 'min_y': 20, 'max_y': 25}) is False

    def test_query_overlaps_returns_correct_boxes(self):
        si = SpatialIndex(cell_size=10.0)
        box_a = {'min_x': 0, 'max_x': 5, 'min_y': 0, 'max_y': 5}
        box_b = {'min_x': 20, 'max_x': 25, 'min_y': 20, 'max_y': 25}
        box_c = {'min_x': 3, 'max_x': 8, 'min_y': 3, 'max_y': 8}
        si.insert(box_a)
        si.insert(box_b)
        si.insert(box_c)

        query = {'min_x': 2, 'max_x': 6, 'min_y': 2, 'max_y': 6}
        overlaps = si.query_overlaps(query)
        assert len(overlaps) == 2  # box_a and box_c
        assert box_a in overlaps
        assert box_c in overlaps
        assert box_b not in overlaps

    def test_no_false_negatives_across_cells(self):
        """A bbox spanning multiple cells should still be found."""
        si = SpatialIndex(cell_size=5.0)
        # This box spans cells (0,0), (1,0), (2,0), etc.
        wide_box = {'min_x': 0, 'max_x': 15, 'min_y': 0, 'max_y': 3}
        si.insert(wide_box)

        # Query at far end
        assert si.any_overlap({'min_x': 12, 'max_x': 14, 'min_y': 1, 'max_y': 2}) is True

    def test_large_bbox_larger_than_cell(self):
        si = SpatialIndex(cell_size=5.0)
        big = {'min_x': -20, 'max_x': 20, 'min_y': -20, 'max_y': 20}
        si.insert(big)
        # Should be found anywhere inside
        assert si.any_overlap({'min_x': -15, 'max_x': -10, 'min_y': 5, 'max_y': 10}) is True
        # Should not be found outside
        assert si.any_overlap({'min_x': 25, 'max_x': 30, 'min_y': 25, 'max_y': 30}) is False

    def test_negative_coordinates(self):
        si = SpatialIndex(cell_size=10.0)
        box = {'min_x': -15, 'max_x': -5, 'min_y': -15, 'max_y': -5}
        si.insert(box)
        assert si.any_overlap({'min_x': -12, 'max_x': -8, 'min_y': -12, 'max_y': -8}) is True
        assert si.any_overlap({'min_x': 0, 'max_x': 5, 'min_y': 0, 'max_y': 5}) is False

    def test_many_inserts(self):
        """Stress test: insert 100 non-overlapping boxes, query each."""
        si = SpatialIndex(cell_size=10.0)
        boxes = []
        for i in range(100):
            b = {'min_x': i * 5, 'max_x': i * 5 + 3, 'min_y': 0, 'max_y': 3}
            boxes.append(b)
            si.insert(b)

        # Query overlapping box 50
        assert si.any_overlap({'min_x': 251, 'max_x': 252, 'min_y': 1, 'max_y': 2}) is True
        # Query in a gap
        assert si.any_overlap({'min_x': 253.5, 'max_x': 254.5, 'min_y': 1, 'max_y': 2}) is False

    def test_no_duplicates_in_query(self):
        """A bbox touching multiple cells should appear only once in results."""
        si = SpatialIndex(cell_size=5.0)
        box = {'min_x': 4, 'max_x': 6, 'min_y': 4, 'max_y': 6}  # spans 4 cells
        si.insert(box)

        overlaps = si.query_overlaps({'min_x': 3, 'max_x': 7, 'min_y': 3, 'max_y': 7})
        assert len(overlaps) == 1


# ---------------------------------------------------------------------------
# wire_segments_to_bboxes (needs mock wire objects)
# ---------------------------------------------------------------------------

class _MockWire:
    """Minimal wire mock with start/end position attributes."""
    def __init__(self, sx_mm, sy_mm, ex_mm, ey_mm):
        self.start = _MockPos(sx_mm, sy_mm)
        self.end = _MockPos(ex_mm, ey_mm)

class _MockPos:
    def __init__(self, x_mm, y_mm):
        self.x = int(x_mm * 1e6)
        self.y = int(y_mm * 1e6)


class TestWireSegmentsToBboxes:
    def test_normal_segment(self):
        wires = [_MockWire(10, 20, 30, 40)]
        result = bbox_mod.wire_segments_to_bboxes(wires)
        assert len(result) == 1
        bb = result[0]
        assert bb['kind'] == 'wire'
        assert bb['min_x'] == pytest.approx(10.0, abs=0.1)
        assert bb['max_x'] == pytest.approx(30.0, abs=0.1)
        assert bb['min_y'] == pytest.approx(20.0, abs=0.1)
        assert bb['max_y'] == pytest.approx(40.0, abs=0.1)

    def test_vertical_zero_width_expansion(self):
        """Vertical wire should be expanded by 0.01mm on x-axis."""
        wires = [_MockWire(10, 20, 10, 40)]
        result = bbox_mod.wire_segments_to_bboxes(wires)
        bb = result[0]
        assert bb['min_x'] < 10.0
        assert bb['max_x'] > 10.0

    def test_horizontal_zero_height_expansion(self):
        """Horizontal wire should be expanded by 0.01mm on y-axis."""
        wires = [_MockWire(10, 20, 30, 20)]
        result = bbox_mod.wire_segments_to_bboxes(wires)
        bb = result[0]
        assert bb['min_y'] < 20.0
        assert bb['max_y'] > 20.0

    def test_degenerate_point_wire(self):
        """Point wire (start == end) should still produce expanded bbox."""
        wires = [_MockWire(15, 25, 15, 25)]
        result = bbox_mod.wire_segments_to_bboxes(wires)
        bb = result[0]
        assert bb['min_x'] < bb['max_x']
        assert bb['min_y'] < bb['max_y']

    def test_empty_wire_list(self):
        result = bbox_mod.wire_segments_to_bboxes([])
        assert result == []
