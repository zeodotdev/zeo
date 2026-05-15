# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
Unit tests for schematic helper functions (Phase 1 & 2 API).

These tests verify the pure functions that don't require a KiCad connection.
For integration tests with KiCad, see kipy/testing.py.
"""

import pytest
from unittest.mock import Mock, MagicMock
from kipy.geometry import Vector2


class TestGetGridSettings:
    """Tests for Schematic.get_grid_settings()"""

    def test_returns_dict_with_required_keys(self):
        """Should return dict with size_mm, size_mils, size_nm, is_default"""
        from kipy.schematic import Schematic

        # Create a mock schematic object
        sch = Mock(spec=Schematic)
        # Call the actual method
        result = Schematic.get_grid_settings(sch)

        assert isinstance(result, dict)
        assert "size_mm" in result
        assert "size_mils" in result
        assert "size_nm" in result
        assert "is_default" in result

    def test_default_grid_is_50_mils(self):
        """Default KiCad grid is 50 mils"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        result = Schematic.get_grid_settings(sch)

        assert result["size_mils"] == 50

    def test_grid_mm_equals_1_27(self):
        """50 mils = 1.27mm"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        result = Schematic.get_grid_settings(sch)

        assert result["size_mm"] == pytest.approx(1.27, rel=1e-6)

    def test_grid_nm_conversion(self):
        """Grid in nm should be mm * 1,000,000"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        result = Schematic.get_grid_settings(sch)

        assert result["size_nm"] == int(result["size_mm"] * 1_000_000)

    def test_units_are_consistent(self):
        """All units should represent the same physical size"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        result = Schematic.get_grid_settings(sch)

        # 1 mil = 0.0254 mm
        mm_from_mils = result["size_mils"] * 0.0254
        assert result["size_mm"] == pytest.approx(mm_from_mils, rel=1e-6)

        # nm = mm * 1e6
        nm_from_mm = result["size_mm"] * 1_000_000
        assert result["size_nm"] == pytest.approx(nm_from_mm, rel=1e-6)

    def test_is_default_flag_is_true(self):
        """Should indicate these are default values since no API to query actual grid"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        result = Schematic.get_grid_settings(sch)

        assert result["is_default"] is True


class TestGetUsableArea:
    """Tests for Schematic.get_usable_area()"""

    def test_returns_dict_with_all_required_keys(self):
        """Should return dict with all documented keys"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        # Mock get_page_settings to return A4 landscape
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        # Check min/max style keys
        assert "min_x_mm" in result
        assert "max_x_mm" in result
        assert "min_y_mm" in result
        assert "max_y_mm" in result
        # Check left/right/top/bottom style keys
        assert "left_mm" in result
        assert "right_mm" in result
        assert "top_mm" in result
        assert "bottom_mm" in result
        # Check dimension keys
        assert "width_mm" in result
        assert "height_mm" in result
        assert "center_x_mm" in result
        assert "center_y_mm" in result
        # Check page size keys
        assert "page_width_mm" in result
        assert "page_height_mm" in result

    def test_min_max_equals_left_right_top_bottom(self):
        """Both naming conventions should return same values"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        assert result["min_x_mm"] == result["left_mm"]
        assert result["max_x_mm"] == result["right_mm"]
        assert result["min_y_mm"] == result["top_mm"]
        assert result["max_y_mm"] == result["bottom_mm"]

    def test_center_is_calculated_correctly(self):
        """Center should be midpoint of usable area"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        expected_center_x = (result["left_mm"] + result["right_mm"]) / 2
        expected_center_y = (result["top_mm"] + result["bottom_mm"]) / 2

        assert result["center_x_mm"] == pytest.approx(expected_center_x)
        assert result["center_y_mm"] == pytest.approx(expected_center_y)

    def test_dimensions_are_calculated_correctly(self):
        """Width and height should match bounds"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        assert result["width_mm"] == pytest.approx(result["max_x_mm"] - result["min_x_mm"])
        assert result["height_mm"] == pytest.approx(result["max_y_mm"] - result["min_y_mm"])

    def test_page_dimensions_are_included(self):
        """Should include full page dimensions for reference"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        assert result["page_width_mm"] == 297.0
        assert result["page_height_mm"] == 210.0

    def test_usable_area_is_smaller_than_page(self):
        """Usable area should be smaller than page due to margins"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        page_info = Mock()
        page_info.width_mm = 297.0
        page_info.height_mm = 210.0
        sch.get_page_settings = Mock(return_value=page_info)

        result = Schematic.get_usable_area(sch)

        assert result["width_mm"] < result["page_width_mm"]
        assert result["height_mm"] < result["page_height_mm"]


class TestSnapToGrid:
    """Tests for Schematic.snap_to_grid()"""

    def test_already_on_grid(self):
        """Coordinates already on grid should not change"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        # 100.33 is approximately on the 1.27mm grid (79 * 1.27 = 100.33)
        x, y = Schematic.snap_to_grid(sch, 100.33, 76.2)

        assert x == pytest.approx(100.33, rel=1e-3)
        assert y == pytest.approx(76.2, rel=1e-3)

    def test_snaps_to_nearest_grid_point(self):
        """Should snap to nearest grid point"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        # 100.5 should snap to 100.33 (79 * 1.27) or 101.6 (80 * 1.27)
        # 100.5 is closer to 100.33 (diff 0.17) than 101.6 (diff 1.1)
        x, y = Schematic.snap_to_grid(sch, 100.5, 75.0)

        # Check x snapped correctly
        assert x == pytest.approx(100.33, rel=1e-2)
        # 75.0 -> nearest is 74.93 (59 * 1.27) or 76.2 (60 * 1.27)
        # 75.0 is closer to 74.93
        assert y == pytest.approx(74.93, rel=1e-2)

    def test_custom_grid_size(self):
        """Should use custom grid size when provided"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        # Use 2.54mm grid (100 mils)
        x, y = Schematic.snap_to_grid(sch, 100.0, 75.0, grid_mm=2.54)

        # 100.0 / 2.54 = 39.37 -> round to 39 -> 39 * 2.54 = 99.06
        assert x == pytest.approx(99.06, rel=1e-2)
        # 75.0 / 2.54 = 29.53 -> round to 30 -> 30 * 2.54 = 76.2
        assert y == pytest.approx(76.2, rel=1e-2)

    def test_negative_coordinates(self):
        """Should handle negative coordinates correctly"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        x, y = Schematic.snap_to_grid(sch, -100.5, -75.3)

        # Should snap to nearest grid point
        assert x == pytest.approx(-100.33, rel=1e-2)
        assert y == pytest.approx(-75.565, rel=1e-2)

    def test_zero_coordinates(self):
        """Zero should remain zero"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        x, y = Schematic.snap_to_grid(sch, 0.0, 0.0)

        assert x == 0.0
        assert y == 0.0

    def test_returns_tuple(self):
        """Should return a tuple of two floats"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)
        sch.get_grid_settings = Mock(return_value={"size_mm": 1.27})

        result = Schematic.snap_to_grid(sch, 100.0, 75.0)

        assert isinstance(result, tuple)
        assert len(result) == 2
        assert isinstance(result[0], float)
        assert isinstance(result[1], float)


class TestGetPinPosition:
    """Tests for Schematic.get_pin_position()"""

    def test_finds_pin_by_number(self):
        """Should find pin by pin number"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        # Create mock symbol with pins
        pin1 = Mock()
        pin1.name = "~"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(100, 80)

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "2"
        pin2.position = Vector2.from_xy_mm(100, 90)

        symbol = Mock()
        symbol.pins = [pin1, pin2]

        result = Schematic.get_pin_position(sch, symbol, "2")

        assert result is not None
        assert result.x == pin2.position.x
        assert result.y == pin2.position.y

    def test_finds_pin_by_name(self):
        """Should find pin by pin name"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        # Create mock symbol with named pins
        pin1 = Mock()
        pin1.name = "VCC"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(100, 80)

        pin2 = Mock()
        pin2.name = "GND"
        pin2.number = "2"
        pin2.position = Vector2.from_xy_mm(100, 90)

        symbol = Mock()
        symbol.pins = [pin1, pin2]

        result = Schematic.get_pin_position(sch, symbol, "GND")

        assert result is not None
        assert result.x == pin2.position.x
        assert result.y == pin2.position.y

    def test_returns_none_for_nonexistent_pin(self):
        """Should return None if pin not found"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        pin1 = Mock()
        pin1.name = "A"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(100, 80)

        symbol = Mock()
        symbol.pins = [pin1]

        result = Schematic.get_pin_position(sch, symbol, "nonexistent")

        assert result is None

    def test_returns_none_for_symbol_without_pins(self):
        """Should return None if symbol has no pins attribute"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        symbol = Mock(spec=[])  # No pins attribute

        result = Schematic.get_pin_position(sch, symbol, "1")

        assert result is None


class TestWirePins:
    """Tests for Schematic.wire_pins()"""

    def test_creates_wire_between_pins(self):
        """Should create wire using exact pin positions"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        # Create mock pins
        pin1 = Mock()
        pin1.name = "~"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(100, 80)

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "1"
        pin2.position = Vector2.from_xy_mm(100, 110)

        sym1 = Mock()
        sym1.pins = [pin1]

        sym2 = Mock()
        sym2.pins = [pin2]

        mock_wire = Mock()
        sch.add_wire = Mock(return_value=mock_wire)
        sch.get_pin_position = Schematic.get_pin_position

        result = Schematic.wire_pins(sch, sym1, "1", sym2, "1")

        # Should have called add_wire with exact pin positions
        sch.add_wire.assert_called_once()
        call_args = sch.add_wire.call_args[0]
        assert call_args[0].x == pin1.position.x
        assert call_args[0].y == pin1.position.y
        assert call_args[1].x == pin2.position.x
        assert call_args[1].y == pin2.position.y

    def test_returns_none_if_pin1_not_found(self):
        """Should return None if first pin not found"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        sym1 = Mock()
        sym1.pins = []

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "1"
        pin2.position = Vector2.from_xy_mm(100, 110)

        sym2 = Mock()
        sym2.pins = [pin2]

        sch.get_pin_position = Schematic.get_pin_position

        result = Schematic.wire_pins(sch, sym1, "1", sym2, "1")

        assert result is None

    def test_returns_none_if_pin2_not_found(self):
        """Should return None if second pin not found"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        pin1 = Mock()
        pin1.name = "~"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(100, 80)

        sym1 = Mock()
        sym1.pins = [pin1]

        sym2 = Mock()
        sym2.pins = []

        sch.get_pin_position = Schematic.get_pin_position

        result = Schematic.wire_pins(sch, sym1, "1", sym2, "1")

        assert result is None


class TestWirePath:
    """Tests for Schematic.wire_path()"""

    def test_creates_wire_segments_through_waypoints(self):
        """Should create multiple wire segments through waypoints"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        # Create mock pins
        pin1 = Mock()
        pin1.name = "~"
        pin1.number = "2"
        pin1.position = Vector2.from_xy_mm(80, 80)

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "1"
        pin2.position = Vector2.from_xy_mm(120, 100)

        sym1 = Mock()
        sym1.pins = [pin1]

        sym2 = Mock()
        sym2.pins = [pin2]

        mock_wire = Mock()
        sch.add_wire = Mock(return_value=mock_wire)
        sch.get_pin_position = Schematic.get_pin_position

        waypoints = [(100, 80), (100, 100)]
        result = Schematic.wire_path(sch, (sym1, "2"), waypoints, (sym2, "1"))

        # Should create 3 wire segments:
        # pin1 -> waypoint1 -> waypoint2 -> pin2
        assert sch.add_wire.call_count == 3
        assert len(result) == 3

    def test_returns_empty_list_if_start_pin_not_found(self):
        """Should return empty list if start pin not found"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        sym1 = Mock()
        sym1.pins = []

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "1"
        pin2.position = Vector2.from_xy_mm(120, 100)

        sym2 = Mock()
        sym2.pins = [pin2]

        sch.get_pin_position = Schematic.get_pin_position

        result = Schematic.wire_path(sch, (sym1, "2"), [(100, 80)], (sym2, "1"))

        assert result == []

    def test_direct_connection_with_no_waypoints(self):
        """Should handle empty waypoints list"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        pin1 = Mock()
        pin1.name = "~"
        pin1.number = "1"
        pin1.position = Vector2.from_xy_mm(80, 80)

        pin2 = Mock()
        pin2.name = "~"
        pin2.number = "1"
        pin2.position = Vector2.from_xy_mm(120, 80)

        sym1 = Mock()
        sym1.pins = [pin1]

        sym2 = Mock()
        sym2.pins = [pin2]

        mock_wire = Mock()
        sch.add_wire = Mock(return_value=mock_wire)
        sch.get_pin_position = Schematic.get_pin_position

        result = Schematic.wire_path(sch, (sym1, "1"), [], (sym2, "1"))

        # Should create 1 direct wire segment
        assert sch.add_wire.call_count == 1
        assert len(result) == 1


class TestGetUnconnectedPins:
    """Tests for Schematic.get_unconnected_pins()"""

    def test_finds_unconnected_pins(self):
        """Should identify nets with 'unconnected' in name"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        # Create mock nets response
        net1 = Mock()
        net1.name = "VCC"

        net2 = Mock()
        net2.name = "unconnected-(R1-Pad1)"

        net3 = Mock()
        net3.name = "GND"

        net4 = Mock()
        net4.name = "unconnected-(C1-Pad2)"

        nets_response = Mock()
        nets_response.nets = [net1, net2, net3, net4]
        sch.get_nets = Mock(return_value=nets_response)

        result = Schematic.get_unconnected_pins(sch)

        assert len(result) == 2
        assert result[0]["net_name"] == "unconnected-(R1-Pad1)"
        assert result[1]["net_name"] == "unconnected-(C1-Pad2)"

    def test_parses_symbol_ref_and_pin_number(self):
        """Should extract symbol ref and pin number from net name"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        net = Mock()
        net.name = "unconnected-(R1-Pad1)"

        nets_response = Mock()
        nets_response.nets = [net]
        sch.get_nets = Mock(return_value=nets_response)

        result = Schematic.get_unconnected_pins(sch)

        assert len(result) == 1
        assert result[0]["symbol_ref"] == "R1"
        assert result[0]["pin_number"] == "1"

    def test_returns_empty_list_when_all_connected(self):
        """Should return empty list when no unconnected pins"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        net1 = Mock()
        net1.name = "VCC"

        net2 = Mock()
        net2.name = "GND"

        nets_response = Mock()
        nets_response.nets = [net1, net2]
        sch.get_nets = Mock(return_value=nets_response)

        result = Schematic.get_unconnected_pins(sch)

        assert result == []

    def test_handles_non_standard_net_names(self):
        """Should handle unconnected nets without standard format"""
        from kipy.schematic import Schematic

        sch = Mock(spec=Schematic)

        net = Mock()
        net.name = "unconnected-something-else"

        nets_response = Mock()
        nets_response.nets = [net]
        sch.get_nets = Mock(return_value=nets_response)

        result = Schematic.get_unconnected_pins(sch)

        assert len(result) == 1
        assert result[0]["net_name"] == "unconnected-something-else"
        assert result[0]["symbol_ref"] is None
        assert result[0]["pin_number"] is None
