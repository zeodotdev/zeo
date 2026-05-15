"""Tests for sch_place_companions geometry helpers and overlap detection logic.

Pure function tests for geometry helpers and mocked tests for obstacle collection.
The helper functions are defined inline in the tool script; we replicate them here
since the tool script requires a live KiCad connection for module-level code.
"""
import sys
import os
import pytest
from unittest.mock import Mock, MagicMock

# ---------------------------------------------------------------------------
# Import bbox utilities (same approach as test_spatial_index.py)
# ---------------------------------------------------------------------------
_BBOX_DIR = os.path.join(
    os.path.dirname(__file__), '..', '..', '..', '..', 'agent', 'tools', 'python', 'common'
)
sys.path.insert(0, os.path.abspath(_BBOX_DIR))

import builtins
if not hasattr(builtins, 'get_uuid_str'):
    builtins.get_uuid_str = lambda obj: getattr(obj, '_uuid', '')

import bbox as bbox_mod

bboxes_overlap = bbox_mod.bboxes_overlap
SpatialIndex = bbox_mod.SpatialIndex


# ---------------------------------------------------------------------------
# Replicate pure helper functions from sch_place_companions.py
# (these are defined inline in the tool script and can't be imported directly)
# ---------------------------------------------------------------------------
GRID_MM = 1.27
BBOX_MARGIN = 0.5
COMP_HALF_LEN = 3.81
TERMINAL_EXTENSION = 5.08
LABEL_CHAR_WIDTH = 1.0
LABEL_HEIGHT = 2.5
MIN_OFFSET_GRIDS = 3
MAX_OFFSET_GRIDS = 15


def snap_to_grid(v, grid=1.27):
    return round(round(v / grid) * grid, 4)


def _is_horizontal_pin_component(lib_id):
    lib_lower = lib_id.lower()
    return 'led' in lib_lower or ':d' in lib_lower or lib_lower.endswith(':d') or '_d_' in lib_lower or 'diode' in lib_lower


def _get_component_angle(escape_dir, lib_id):
    is_horiz = _is_horizontal_pin_component(lib_id)
    if is_horiz:
        angles = {'left': 0, 'right': 180, 'down': 270, 'up': 90}
    else:
        angles = {'left': 90, 'right': 270, 'down': 180, 'up': 0}
    return angles.get(escape_dir, 270)


def _calc_companion_bbox(cx, cy, escape_dir, lib_id=''):
    body_half = COMP_HALF_LEN + BBOX_MARGIN
    term_half = COMP_HALF_LEN + TERMINAL_EXTENSION + BBOX_MARGIN
    if _is_horizontal_pin_component(lib_id):
        width_half = 3.0 + BBOX_MARGIN
    else:
        width_half = 1.5 + BBOX_MARGIN
    if escape_dir == 'left':
        return {'min_x': cx - term_half, 'max_x': cx + body_half, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'right':
        return {'min_x': cx - body_half, 'max_x': cx + term_half, 'min_y': cy - width_half, 'max_y': cy + width_half}
    elif escape_dir == 'up':
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - term_half, 'max_y': cy + body_half}
    else:
        return {'min_x': cx - width_half, 'max_x': cx + width_half, 'min_y': cy - body_half, 'max_y': cy + term_half}


def _calc_center_from_offset(px, py, offset_mm, escape_dir):
    if escape_dir == 'left':
        return snap_to_grid(px - offset_mm), snap_to_grid(py)
    elif escape_dir == 'right':
        return snap_to_grid(px + offset_mm), snap_to_grid(py)
    elif escape_dir == 'up':
        return snap_to_grid(px), snap_to_grid(py - offset_mm)
    else:
        return snap_to_grid(px), snap_to_grid(py + offset_mm)


def _estimate_label_bbox(pos_x, pos_y, text, angle=0):
    text_len = len(text) if text else 3
    width = text_len * LABEL_CHAR_WIDTH + 1.0
    height = LABEL_HEIGHT
    margin = 1.0
    if angle in (0, 180):
        return {
            'min_x': pos_x - margin,
            'max_x': pos_x + width + margin,
            'min_y': pos_y - height / 2 - margin,
            'max_y': pos_y + height / 2 + margin
        }
    else:
        return {
            'min_x': pos_x - height / 2 - margin,
            'max_x': pos_x + height / 2 + margin,
            'min_y': pos_y - margin,
            'max_y': pos_y + width + margin
        }


def _segments_intersect(seg1, seg2):
    x1, y1, x2, y2 = seg1
    x3, y3, x4, y4 = seg2
    eps = 0.01

    def pts_equal(ax, ay, bx, by):
        return abs(ax - bx) < eps and abs(ay - by) < eps

    if (pts_equal(x1, y1, x3, y3) or pts_equal(x1, y1, x4, y4) or
        pts_equal(x2, y2, x3, y3) or pts_equal(x2, y2, x4, y4)):
        return False
    if abs(y1 - y2) < eps and abs(y3 - y4) < eps:
        if abs(y1 - y3) < eps:
            return max(min(x1, x2), min(x3, x4)) < min(max(x1, x2), max(x3, x4))
        return False
    if abs(x1 - x2) < eps and abs(x3 - x4) < eps:
        if abs(x1 - x3) < eps:
            return max(min(y1, y2), min(y3, y4)) < min(max(y1, y2), max(y3, y4))
        return False
    if abs(y1 - y2) < eps:
        h_y, h_x1, h_x2 = y1, min(x1, x2), max(x1, x2)
        v_x, v_y1, v_y2 = x3, min(y3, y4), max(y3, y4)
    elif abs(x1 - x2) < eps:
        v_x, v_y1, v_y2 = x1, min(y1, y2), max(y1, y2)
        h_y, h_x1, h_x2 = y3, min(x3, x4), max(x3, x4)
    else:
        return False
    return (h_x1 + eps < v_x < h_x2 - eps) and (v_y1 + eps < h_y < v_y2 - eps)


def _compute_wire_path(px, py, cx, cy, escape_dir):
    px, py = snap_to_grid(px), snap_to_grid(py)
    cx, cy = snap_to_grid(cx), snap_to_grid(cy)
    if escape_dir in ('left', 'right'):
        if abs(py - cy) < 0.01:
            return [(px, py, cx, cy)]
        else:
            return [(px, py, px, cy), (px, cy, cx, cy)]
    else:
        if abs(px - cx) < 0.01:
            return [(px, py, cx, cy)]
        else:
            return [(px, py, cx, py), (cx, py, cx, cy)]


# ---------------------------------------------------------------------------
# Tests: _calc_companion_bbox
# ---------------------------------------------------------------------------

class TestCalcCompanionBbox:
    def test_right_escape_asymmetric(self):
        """Pin 1 side (right/away from IC) should have terminal extension."""
        bb = _calc_companion_bbox(50, 50, 'right', 'Device:R')
        # Right side (away) should extend further than left side (toward IC)
        away_extent = bb['max_x'] - 50  # pin 1 side
        toward_extent = 50 - bb['min_x']  # pin 2 side
        assert away_extent > toward_extent

    def test_left_escape_asymmetric(self):
        bb = _calc_companion_bbox(50, 50, 'left', 'Device:C')
        away_extent = 50 - bb['min_x']
        toward_extent = bb['max_x'] - 50
        assert away_extent > toward_extent

    def test_up_escape(self):
        bb = _calc_companion_bbox(50, 50, 'up', 'Device:R')
        away_extent = 50 - bb['min_y']
        toward_extent = bb['max_y'] - 50
        assert away_extent > toward_extent

    def test_down_escape(self):
        bb = _calc_companion_bbox(50, 50, 'down', 'Device:R')
        away_extent = bb['max_y'] - 50
        toward_extent = 50 - bb['min_y']
        assert away_extent > toward_extent

    def test_horizontal_component_wider(self):
        """LED/diode should have wider bbox than resistor."""
        bb_led = _calc_companion_bbox(50, 50, 'right', 'Device:LED')
        bb_r = _calc_companion_bbox(50, 50, 'right', 'Device:R')
        led_height = bb_led['max_y'] - bb_led['min_y']
        r_height = bb_r['max_y'] - bb_r['min_y']
        assert led_height > r_height

    def test_all_directions_contain_center(self):
        for d in ('left', 'right', 'up', 'down'):
            bb = _calc_companion_bbox(100, 100, d, 'Device:C')
            assert bb['min_x'] < 100 < bb['max_x']
            assert bb['min_y'] < 100 < bb['max_y']


# ---------------------------------------------------------------------------
# Tests: _calc_center_from_offset
# ---------------------------------------------------------------------------

class TestCalcCenterFromOffset:
    # Use grid-aligned base position: 50.8 = 40 * 1.27
    _BASE = 50.8

    def test_right(self):
        cx, cy = _calc_center_from_offset(self._BASE, self._BASE, 5.08, 'right')
        assert cx > self._BASE
        assert cy == pytest.approx(self._BASE, abs=0.01)

    def test_left(self):
        cx, cy = _calc_center_from_offset(self._BASE, self._BASE, 5.08, 'left')
        assert cx < self._BASE
        assert cy == pytest.approx(self._BASE, abs=0.01)

    def test_up(self):
        cx, cy = _calc_center_from_offset(self._BASE, self._BASE, 5.08, 'up')
        assert cx == pytest.approx(self._BASE, abs=0.01)
        assert cy < self._BASE

    def test_down(self):
        cx, cy = _calc_center_from_offset(self._BASE, self._BASE, 5.08, 'down')
        assert cx == pytest.approx(self._BASE, abs=0.01)
        assert cy > self._BASE

    def test_grid_snapped(self):
        cx, cy = _calc_center_from_offset(self._BASE, self._BASE, 5.0, 'right')
        # Result should be on 1.27mm grid
        assert abs(cx / GRID_MM - round(cx / GRID_MM)) < 0.001


# ---------------------------------------------------------------------------
# Tests: _estimate_label_bbox
# ---------------------------------------------------------------------------

class TestEstimateLabelBbox:
    def test_horizontal_width_scales(self):
        short = _estimate_label_bbox(0, 0, 'AB')
        long = _estimate_label_bbox(0, 0, 'ABCDEFGH')
        assert (long['max_x'] - long['min_x']) > (short['max_x'] - short['min_x'])

    def test_vertical_rotated(self):
        h = _estimate_label_bbox(10, 10, 'TEST', angle=0)
        v = _estimate_label_bbox(10, 10, 'TEST', angle=90)
        h_width = h['max_x'] - h['min_x']
        h_height = h['max_y'] - h['min_y']
        v_width = v['max_x'] - v['min_x']
        v_height = v['max_y'] - v['min_y']
        # Rotated should swap width/height approximately
        assert v_height > v_width
        assert h_width > h_height


# ---------------------------------------------------------------------------
# Tests: _segments_intersect
# ---------------------------------------------------------------------------

class TestSegmentsIntersect:
    def test_perpendicular_crossing(self):
        """Horizontal and vertical segments that cross."""
        h = (0, 5, 10, 5)
        v = (5, 0, 5, 10)
        assert _segments_intersect(h, v) is True

    def test_parallel_no_overlap(self):
        s1 = (0, 0, 10, 0)
        s2 = (0, 5, 10, 5)
        assert _segments_intersect(s1, s2) is False

    def test_collinear_overlapping(self):
        s1 = (0, 0, 10, 0)
        s2 = (5, 0, 15, 0)
        assert _segments_intersect(s1, s2) is True

    def test_collinear_no_overlap(self):
        s1 = (0, 0, 5, 0)
        s2 = (10, 0, 15, 0)
        assert _segments_intersect(s1, s2) is False

    def test_shared_endpoint(self):
        """Shared endpoints are NOT crossings."""
        s1 = (0, 0, 5, 0)
        s2 = (5, 0, 5, 10)
        assert _segments_intersect(s1, s2) is False

    def test_t_junction_not_crossing(self):
        """Perpendicular but meeting at endpoint of one."""
        h = (0, 5, 10, 5)  # horizontal
        v = (5, 5, 5, 10)  # vertical starting at midpoint of h
        assert _segments_intersect(h, v) is False

    def test_no_crossing_diagonal(self):
        """Non-axis-aligned segments always return False."""
        s1 = (0, 0, 10, 10)
        s2 = (0, 10, 10, 0)
        assert _segments_intersect(s1, s2) is False


# ---------------------------------------------------------------------------
# Tests: _get_component_angle
# ---------------------------------------------------------------------------

class TestGetComponentAngle:
    def test_resistor_right(self):
        assert _get_component_angle('right', 'Device:R') == 270

    def test_resistor_left(self):
        assert _get_component_angle('left', 'Device:R') == 90

    def test_resistor_up(self):
        assert _get_component_angle('up', 'Device:R') == 0

    def test_resistor_down(self):
        assert _get_component_angle('down', 'Device:R') == 180

    def test_led_right(self):
        assert _get_component_angle('right', 'Device:LED') == 180

    def test_led_left(self):
        assert _get_component_angle('left', 'Device:LED') == 0

    def test_led_up(self):
        assert _get_component_angle('up', 'Device:LED') == 90

    def test_led_down(self):
        assert _get_component_angle('down', 'Device:LED') == 270


# ---------------------------------------------------------------------------
# Tests: _compute_wire_path
# ---------------------------------------------------------------------------

class TestComputeWirePath:
    def test_straight_horizontal(self):
        """Aligned horizontally -> single segment."""
        path = _compute_wire_path(10, 20, 30, 20, 'right')
        assert len(path) == 1

    def test_straight_vertical(self):
        """Aligned vertically -> single segment."""
        path = _compute_wire_path(10, 20, 10, 40, 'down')
        assert len(path) == 1

    def test_l_shaped_horizontal_escape(self):
        """Misaligned with horizontal escape -> L-shaped (2 segments)."""
        path = _compute_wire_path(10, 20, 30, 30, 'right')
        assert len(path) == 2

    def test_l_shaped_vertical_escape(self):
        """Misaligned with vertical escape -> L-shaped (2 segments)."""
        path = _compute_wire_path(10, 20, 20, 40, 'down')
        assert len(path) == 2


# ---------------------------------------------------------------------------
# Tests: _find_clear_position with SpatialIndex (simulated)
# ---------------------------------------------------------------------------

class TestFindClearPositionLogic:
    """Test the _find_clear_position logic using a SpatialIndex directly."""

    def test_clear_path_returns_min_offset(self):
        """With no obstacles, should return at MIN_OFFSET_GRIDS."""
        spatial = SpatialIndex(cell_size=10.0)
        px, py = 100.0, 100.0
        escape_dir = 'right'

        # Manually replicate _find_clear_position logic
        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
            try_offset = try_grids * GRID_MM
            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir)
            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, 'Device:C')
            if not spatial.any_overlap(try_bbox):
                assert try_grids == MIN_OFFSET_GRIDS
                return
        pytest.fail('Should have found a clear position')

    def test_obstacle_forces_further_offset(self):
        """An obstacle at min offset should push placement further out."""
        spatial = SpatialIndex(cell_size=10.0)
        px, py = 100.0, 100.0
        escape_dir = 'right'

        # Place obstacle at the min offset position
        min_cx, min_cy = _calc_center_from_offset(px, py, MIN_OFFSET_GRIDS * GRID_MM, escape_dir)
        obstacle = _calc_companion_bbox(min_cx, min_cy, escape_dir, 'Device:C')
        spatial.insert(obstacle)

        # Find first clear position
        found_grids = None
        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
            try_offset = try_grids * GRID_MM
            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir)
            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, 'Device:C')
            if not spatial.any_overlap(try_bbox):
                found_grids = try_grids
                break

        assert found_grids is not None
        assert found_grids > MIN_OFFSET_GRIDS

    def test_ic_bbox_avoided(self):
        """IC bbox in spatial index should be avoided."""
        spatial = SpatialIndex(cell_size=10.0)
        # Large IC bbox covering the right side
        ic_bbox = {'min_x': 95, 'max_x': 115, 'min_y': 95, 'max_y': 105}
        spatial.insert(ic_bbox)

        px, py = 110.0, 100.0  # Pin on right edge of IC
        escape_dir = 'right'

        found_clear = False
        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
            try_offset = try_grids * GRID_MM
            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir)
            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, 'Device:C')
            if not spatial.any_overlap(try_bbox):
                # Verify the found position is beyond the IC
                assert try_cx > 115
                found_clear = True
                break

        assert found_clear

    def test_blocked_escape_staggers_perpendicular(self):
        """When the entire escape-direction path is blocked (e.g. by a first
        companion plus other obstacles), the search must stagger perpendicular
        rather than falling back on top of the blocker.

        This reproduces the bug where two inductors on the same IC pin were
        placed overlapping because the old code had no perpendicular stagger.
        """
        spatial = SpatialIndex(cell_size=10.0)
        px, py = 160.0, 100.33  # IC pin position
        escape_dir = 'right'
        lib_id = 'Device:L'

        # Block the ENTIRE escape direction at y=100.33 with a wide obstacle
        # (simulates first companion + surrounding items filling the path)
        wall = {
            'min_x': 162.0, 'max_x': 200.0,
            'min_y': py - 2.5, 'max_y': py + 2.5,
        }
        spatial.insert(wall)

        # --- OLD logic (no perpendicular stagger) — would fall back ---
        old_result = None
        for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
            try_offset = try_grids * GRID_MM
            try_cx, try_cy = _calc_center_from_offset(px, py, try_offset, escape_dir)
            try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)
            if not spatial.any_overlap(try_bbox):
                old_result = (try_cx, try_cy, try_bbox)
                break

        # Old logic finds NOTHING — would use fallback (placing on top of wall)
        assert old_result is None, 'Expected old logic to fail finding a clear position'

        # --- NEW logic (with perpendicular stagger) — should succeed ---
        sample_bbox = _calc_companion_bbox(0, 0, escape_dir, lib_id)
        perp_size = sample_bbox['max_y'] - sample_bbox['min_y']
        perp_step = snap_to_grid(perp_size + GRID_MM)

        perp_offsets = [0]
        for s in range(1, 5):
            perp_offsets.append(s * perp_step)
            perp_offsets.append(-s * perp_step)

        new_result = None
        for perp_off in perp_offsets:
            spx, spy = px, snap_to_grid(py + perp_off)
            for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
                try_offset = try_grids * GRID_MM
                try_cx, try_cy = _calc_center_from_offset(spx, spy, try_offset, escape_dir)
                try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)
                if not spatial.any_overlap(try_bbox):
                    new_result = (try_cx, try_cy, try_bbox)
                    break
            if new_result:
                break

        assert new_result is not None, 'New logic should find a position via perpendicular stagger'
        cx, cy, bbox = new_result

        # The found position must not overlap the wall
        assert not bboxes_overlap(bbox, wall)

        # It must be at a different y than the original pin (staggered)
        assert abs(cy - py) > 1.0, (
            f'Expected perpendicular stagger but cy={cy:.2f} is same as pin py={py:.2f}'
        )

    def test_10_element_chain_no_overlaps(self):
        """Simulate a 10-deep chain: each component's away pin feeds the next.

        Verifies that all 10 placements are non-overlapping when using the
        spatial index, exercising the recursive chain logic without a depth limit.
        """
        spatial = SpatialIndex(cell_size=10.0)
        escape_dir = 'right'
        lib_id = 'Device:C'

        # Start from an IC pin
        parent_away_x, parent_away_y = 160.0, 100.0
        placed = []  # list of (cx, cy, bbox)

        for i in range(10):
            # Replicate _find_clear_position (straight-line search from parent away pin)
            found = None
            for try_grids in range(MIN_OFFSET_GRIDS, MAX_OFFSET_GRIDS + 10):
                try_offset = try_grids * GRID_MM
                try_cx, try_cy = _calc_center_from_offset(
                    parent_away_x, parent_away_y, try_offset, escape_dir
                )
                try_bbox = _calc_companion_bbox(try_cx, try_cy, escape_dir, lib_id)
                if not spatial.any_overlap(try_bbox):
                    found = (try_cx, try_cy, try_bbox)
                    break

            assert found is not None, f'Chain element {i} could not find a clear position'
            cx, cy, bbox = found
            spatial.insert(bbox)
            placed.append((cx, cy, bbox))

            # Simulate "away pin" = pin 1 side (far end in escape direction)
            # For escape_dir='right', away pin is at cx + COMP_HALF_LEN
            parent_away_x = cx + COMP_HALF_LEN
            parent_away_y = cy

        # Verify all 10 placements are non-overlapping
        for i in range(len(placed)):
            for j in range(i + 1, len(placed)):
                assert not bboxes_overlap(placed[i][2], placed[j][2]), (
                    f'Chain elements {i} and {j} overlap: '
                    f'{i} at ({placed[i][0]:.2f},{placed[i][1]:.2f}), '
                    f'{j} at ({placed[j][0]:.2f},{placed[j][1]:.2f})'
                )

        # Verify they're ordered along the escape direction (increasing x)
        for i in range(len(placed) - 1):
            assert placed[i][0] < placed[i + 1][0], (
                f'Chain element {i+1} not further right than {i}'
            )


# ---------------------------------------------------------------------------
# Tests: collect_all_obstacle_bboxes (mocked)
# ---------------------------------------------------------------------------

class _MockItem:
    def __init__(self, uuid_str, ref='?', text=None, name=None):
        self._uuid = uuid_str
        self.id = Mock()
        self.id.value = uuid_str
        self.reference = ref
        if text is not None:
            self.text = text
        if name is not None:
            self.name = name


class _MockWire:
    def __init__(self, sx_mm, sy_mm, ex_mm, ey_mm):
        self.start = Mock(x=int(sx_mm * 1e6), y=int(sy_mm * 1e6))
        self.end = Mock(x=int(ex_mm * 1e6), y=int(ey_mm * 1e6))


class TestCollectAllObstacleBboxes:
    def _make_sch(self, symbols=None, labels=None, sheets=None, wires=None):
        sch = Mock()
        sch.symbols.get_all.return_value = symbols or []
        sch.labels.get_all.return_value = labels or []
        sch.crud.get_sheets.return_value = sheets or []
        sch.crud.get_wires.return_value = wires or []

        def fake_bbox(item, units='mm', include_text=True):
            # Return a 10x10 box centered at (0,0) for simplicity
            return {'min_x': -5, 'max_x': 5, 'min_y': -5, 'max_y': 5}

        sch.transform.get_bounding_box = Mock(side_effect=fake_bbox)
        return sch

    def test_collects_symbols(self):
        sym = _MockItem('uuid-1', ref='R1')
        sch = self._make_sch(symbols=[sym])
        result = bbox_mod.collect_all_obstacle_bboxes(sch)
        sym_items = [r for r in result if r.get('kind') == 'symbol']
        assert len(sym_items) == 1
        assert sym_items[0]['ref'] == 'R1'

    def test_collects_labels(self):
        lbl = _MockItem('uuid-2', text='VCC')
        sch = self._make_sch(labels=[lbl])
        result = bbox_mod.collect_all_obstacle_bboxes(sch)
        lbl_items = [r for r in result if r.get('kind') == 'label']
        assert len(lbl_items) == 1
        assert lbl_items[0]['ref'] == 'VCC'

    def test_collects_sheets(self):
        sht = _MockItem('uuid-3', name='PowerSupply')
        sch = self._make_sch(sheets=[sht])
        result = bbox_mod.collect_all_obstacle_bboxes(sch)
        sht_items = [r for r in result if r.get('kind') == 'sheet']
        assert len(sht_items) == 1

    def test_collects_wires(self):
        wire = _MockWire(10, 20, 30, 20)
        sch = self._make_sch(wires=[wire])
        result = bbox_mod.collect_all_obstacle_bboxes(sch)
        wire_items = [r for r in result if r.get('kind') == 'wire']
        assert len(wire_items) == 1

    def test_exclude_ids(self):
        sym1 = _MockItem('uuid-keep', ref='R1')
        sym2 = _MockItem('uuid-skip', ref='U1')
        sch = self._make_sch(symbols=[sym1, sym2])
        result = bbox_mod.collect_all_obstacle_bboxes(sch, exclude_ids={'uuid-skip'})
        sym_items = [r for r in result if r.get('kind') == 'symbol']
        assert len(sym_items) == 1
        assert sym_items[0]['ref'] == 'R1'

    def test_label_shrink_applied(self):
        lbl = _MockItem('uuid-lbl', text='NET')
        sch = self._make_sch(labels=[lbl])

        # Without shrink
        r_no_shrink = bbox_mod.collect_all_obstacle_bboxes(sch, label_shrink=0.0)
        lbl_ns = [r for r in r_no_shrink if r.get('kind') == 'label'][0]

        # With shrink
        r_shrink = bbox_mod.collect_all_obstacle_bboxes(sch, label_shrink=0.5)
        lbl_s = [r for r in r_shrink if r.get('kind') == 'label'][0]

        # Shrunk bbox should be smaller
        assert lbl_s['min_x'] > lbl_ns['min_x']
        assert lbl_s['max_x'] < lbl_ns['max_x']

    def test_all_item_types_combined(self):
        sym = _MockItem('uuid-s', ref='R1')
        lbl = _MockItem('uuid-l', text='VCC')
        sht = _MockItem('uuid-sh', name='Sheet1')
        wire = _MockWire(0, 0, 10, 0)
        sch = self._make_sch(symbols=[sym], labels=[lbl], sheets=[sht], wires=[wire])
        result = bbox_mod.collect_all_obstacle_bboxes(sch)
        kinds = {r.get('kind') for r in result}
        assert kinds == {'symbol', 'label', 'sheet', 'wire'}


# ---------------------------------------------------------------------------
# Tests: auto-junction placement (mocked wiring API)
# ---------------------------------------------------------------------------

class TestAutoJunctionPlacement:
    """Verify that wire objects are collected and junction APIs are called correctly.

    The actual junction detection is a KiCad IPC call; here we mock it to
    verify the integration: wire objects collected → get_needed_junctions called
    → add_junction called for each returned position.
    """

    def test_junctions_placed_for_collected_wires(self):
        """Simulate the junction placement loop from sch_place_companions."""
        # Mock wire objects (as returned by sch.wiring.add_wire)
        wire1 = Mock(name='wire1')
        wire2 = Mock(name='wire2')
        wire3 = Mock(name='wire3')
        placed_wire_objs = [wire1, wire2, wire3]

        # Mock junction positions returned by KiCad
        junc_pos1 = Mock(name='junc_at_branch')
        junc_pos2 = Mock(name='junc_at_tee')

        mock_wiring = Mock()
        mock_wiring.get_needed_junctions.return_value = [junc_pos1, junc_pos2]

        # Replicate the junction placement loop from sch_place_companions
        junction_count = 0
        if placed_wire_objs:
            positions = mock_wiring.get_needed_junctions(placed_wire_objs)
            for pos in positions:
                mock_wiring.add_junction(pos)
                junction_count += 1

        # Verify get_needed_junctions was called with all wire objects
        mock_wiring.get_needed_junctions.assert_called_once_with(placed_wire_objs)
        # Verify add_junction was called for each position
        assert mock_wiring.add_junction.call_count == 2
        mock_wiring.add_junction.assert_any_call(junc_pos1)
        mock_wiring.add_junction.assert_any_call(junc_pos2)
        assert junction_count == 2

    def test_no_junctions_when_no_wires(self):
        """No wire objects → get_needed_junctions should not be called."""
        placed_wire_objs = []
        mock_wiring = Mock()

        junction_count = 0
        if placed_wire_objs:
            positions = mock_wiring.get_needed_junctions(placed_wire_objs)
            for pos in positions:
                mock_wiring.add_junction(pos)
                junction_count += 1

        mock_wiring.get_needed_junctions.assert_not_called()
        assert junction_count == 0

    def test_no_junctions_needed(self):
        """Wire objects exist but KiCad says no junctions needed."""
        wire1 = Mock(name='wire1')
        placed_wire_objs = [wire1]

        mock_wiring = Mock()
        mock_wiring.get_needed_junctions.return_value = []

        junction_count = 0
        if placed_wire_objs:
            positions = mock_wiring.get_needed_junctions(placed_wire_objs)
            for pos in positions:
                mock_wiring.add_junction(pos)
                junction_count += 1

        mock_wiring.get_needed_junctions.assert_called_once_with(placed_wire_objs)
        mock_wiring.add_junction.assert_not_called()
        assert junction_count == 0
