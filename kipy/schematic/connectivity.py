# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Net and bus connectivity operations.
"""

from typing import TYPE_CHECKING, List, Optional
import re

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2
from kipy.proto.common.commands import project_commands_pb2
from kipy.proto.common.types import MapMergeMode
from kipy.project_types import NetClass

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class ConnectivityOperations:
    """Net and bus connectivity operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    def get_nets(self):
        """Get all electrical nets.

        Returns:
            GetNetsResponse with list of NetInfo

        Example:
            >>> nets = sch.connectivity.get_nets()
            >>> for net in nets.nets:
            ...     print(f"Net: {net.name}")
        """
        cmd = schematic_commands_pb2.GetNets()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetsResponse)

    def get_buses(self):
        """Get all buses.

        Returns:
            GetBusesResponse with list of BusInfo

        Example:
            >>> buses = sch.connectivity.get_buses()
            >>> for bus in buses.buses:
            ...     print(f"Bus: {bus.name}")
        """
        cmd = schematic_commands_pb2.GetBuses()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(cmd, schematic_commands_pb2.GetBusesResponse)

    def get_net_for_item(self, item_id: str):
        """Get connectivity info for an item.

        Args:
            item_id: KIID of the item

        Returns:
            GetNetForItemResponse with is_connected and connection info

        Example:
            >>> result = sch.connectivity.get_net_for_item(wire.id.value)
            >>> if result.is_connected:
            ...     print(f"On net: {result.connection.name}")
        """
        cmd = schematic_commands_pb2.GetNetForItem()
        cmd.document.CopyFrom(self._doc)
        cmd.item_id.value = item_id
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetForItemResponse)

    def get_bus_members(self, bus_name: str):
        """Get member nets of a bus.

        Args:
            bus_name: Bus name (e.g., "D[0..7]")

        Returns:
            GetBusMembersResponse with members list

        Example:
            >>> result = sch.connectivity.get_bus_members("D[0..7]")
            >>> print(f"Members: {list(result.members)}")
        """
        cmd = schematic_commands_pb2.GetBusMembers()
        cmd.document.CopyFrom(self._doc)
        cmd.bus_name = bus_name
        return self._kicad.send(cmd, schematic_commands_pb2.GetBusMembersResponse)

    def get_net_items(self, net_name: str):
        """Get all items on a net.

        Args:
            net_name: Name of the net

        Returns:
            GetNetItemsResponse with item_ids and connection_points

        Example:
            >>> result = sch.connectivity.get_net_items("VCC")
            >>> print(f"Items on VCC: {len(result.item_ids)}")
        """
        cmd = schematic_commands_pb2.GetNetItems()
        cmd.document.CopyFrom(self._doc)
        cmd.net_name = net_name
        return self._kicad.send(cmd, schematic_commands_pb2.GetNetItemsResponse)

    def get_unconnected_pins(self) -> List[dict]:
        """Get all unconnected pins.

        Returns:
            List of dicts with net_name, symbol_ref, pin_number

        Example:
            >>> unconnected = sch.connectivity.get_unconnected_pins()
            >>> for pin in unconnected:
            ...     print(f"Unconnected: {pin['symbol_ref']}-{pin['pin_number']}")
        """
        nets = self.get_nets()
        unconnected = []

        for net in nets.nets:
            if "unconnected" in net.name.lower():
                info = {"net_name": net.name}

                match = re.search(r'\(([^)]+)-Pad(\d+)\)', net.name)
                if match:
                    info["symbol_ref"] = match.group(1)
                    info["pin_number"] = match.group(2)
                else:
                    info["symbol_ref"] = None
                    info["pin_number"] = None

                unconnected.append(info)

        return unconnected

    def assign_net_to_class(self, net_name: str, net_class_name: str) -> None:
        """Assign a net to a specific net class.

        Args:
            net_name: Name of the net to assign
            net_class_name: Name of the net class (must exist, or "Default")

        Example:
            >>> sch.connectivity.assign_net_to_class("VCC", "Power")
            >>> sch.connectivity.assign_net_to_class("CLK", "HighSpeed")
        """
        cmd = schematic_commands_pb2.AssignNetToClass()
        cmd.document.CopyFrom(self._doc)
        cmd.net_name = net_name
        cmd.net_class_name = net_class_name
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Net Class CRUD Operations
    # =========================================================================

    def get_net_classes(self) -> List[NetClass]:
        """Get all net classes defined in the project.

        Returns:
            List of NetClass objects

        Example:
            >>> net_classes = sch.connectivity.get_net_classes()
            >>> for nc in net_classes:
            ...     print(f"{nc.name}: wire_width={nc.wire_width}")
        """
        cmd = project_commands_pb2.GetNetClasses()
        response = self._kicad.send(cmd, project_commands_pb2.NetClassesResponse)
        return [NetClass(p) for p in response.net_classes]

    def get_net_class(self, name: str) -> Optional[NetClass]:
        """Get a specific net class by name.

        Args:
            name: Net class name (e.g., "Default", "Power")

        Returns:
            NetClass object, or None if not found

        Example:
            >>> power_class = sch.connectivity.get_net_class("Power")
            >>> if power_class:
            ...     print(f"Wire width: {power_class.wire_width}")
        """
        for nc in self.get_net_classes():
            if nc.name == name:
                return nc
        return None

    def create_net_class(
        self,
        name: str,
        wire_width: Optional[int] = None,
        bus_width: Optional[int] = None,
        priority: int = 0,
    ) -> NetClass:
        """Create a new net class.

        Args:
            name: Name for the new net class
            wire_width: Wire width in nm (schematic)
            bus_width: Bus width in nm (schematic)
            priority: Priority (higher = more important)

        Returns:
            The created NetClass object

        Raises:
            ApiError: If net class already exists or name is invalid

        Example:
            >>> power_class = sch.connectivity.create_net_class(
            ...     "Power",
            ...     wire_width=500000,  # 0.5mm
            ... )
        """
        from kipy.client import ApiError

        nc = NetClass()
        nc.name = name
        nc.priority = priority

        if wire_width is not None:
            nc.wire_width = wire_width
        if bus_width is not None:
            nc.bus_width = bus_width

        cmd = project_commands_pb2.CreateNetClass()
        cmd.net_class.CopyFrom(nc.proto)
        response = self._kicad.send(cmd, project_commands_pb2.CreateNetClassResponse)

        if response.status != project_commands_pb2.CNCS_OK:
            status_messages = {
                project_commands_pb2.CNCS_ALREADY_EXISTS: f"Net class '{name}' already exists",
                project_commands_pb2.CNCS_INVALID_NAME: "Invalid net class name",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to create net class: {error_msg}")

        return nc

    def update_net_class(self, net_class: NetClass) -> None:
        """Update an existing net class.

        Uses SetNetClasses with merge mode to update the net class properties.

        Args:
            net_class: NetClass object with updated properties

        Example:
            >>> power_class = sch.connectivity.get_net_class("Power")
            >>> power_class.wire_width = 600000  # 0.6mm
            >>> sch.connectivity.update_net_class(power_class)
        """
        cmd = project_commands_pb2.SetNetClasses()
        cmd.net_classes.append(net_class.proto)
        cmd.merge_mode = MapMergeMode.MMM_MERGE
        self._kicad.send(cmd, Empty)

    def delete_net_class(self, name: str) -> None:
        """Delete a net class by name.

        Note: Cannot delete the "Default" net class.

        Args:
            name: Name of the net class to delete

        Raises:
            ApiError: If net class not found or is Default

        Example:
            >>> sch.connectivity.delete_net_class("HighSpeed")
        """
        from kipy.client import ApiError

        cmd = project_commands_pb2.DeleteNetClass()
        cmd.name = name
        response = self._kicad.send(cmd, project_commands_pb2.DeleteNetClassResponse)

        if response.status != project_commands_pb2.DNCS_OK:
            status_messages = {
                project_commands_pb2.DNCS_NOT_FOUND: f"Net class '{name}' not found",
                project_commands_pb2.DNCS_CANNOT_DELETE_DEFAULT: "Cannot delete the Default net class",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to delete net class: {error_msg}")
