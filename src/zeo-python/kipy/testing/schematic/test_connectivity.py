# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.connectivity module.

Covers IPC APIs:
- GetNets()
- GetBuses()
- GetNetForItem()
- GetBusMembers()
- GetNetItems()

Covers QoL functions:
- connectivity.get_nets()
- connectivity.get_buses()
- connectivity.get_net_for_item()
- connectivity.get_bus_members()
- connectivity.get_net_items()
- connectivity.get_unconnected_pins()
"""

from kipy.testing._base import TestResults


def test_connectivity(results: TestResults):
    """Test schematic connectivity operations."""
    results.section("Schematic Connectivity")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_nets
    nets_response = None
    try:
        nets_response = sch.connectivity.get_nets()
        net_count = len(nets_response.nets) if hasattr(nets_response, 'nets') else 0
        results.ok("connectivity.get_nets", f"{net_count} nets")
    except Exception as e:
        results.fail("connectivity.get_nets", str(e))

    # Test get_buses
    buses_response = None
    try:
        buses_response = sch.connectivity.get_buses()
        bus_count = len(buses_response.buses) if hasattr(buses_response, 'buses') else 0
        results.ok("connectivity.get_buses", f"{bus_count} buses")
    except Exception as e:
        results.fail("connectivity.get_buses", str(e))

    # Test get_net_for_item
    try:
        symbols = sch.symbols.get_all()
        if symbols:
            symbol_id = symbols[0].id if hasattr(symbols[0], 'id') else None
            if symbol_id:
                net = sch.connectivity.get_net_for_item(symbol_id)
                if net:
                    results.ok("connectivity.get_net_for_item", f"net='{net}'")
                else:
                    results.ok("connectivity.get_net_for_item", "no net (unconnected)")
            else:
                results.skip("connectivity.get_net_for_item", "no symbol ID")
        else:
            results.skip("connectivity.get_net_for_item", "no symbols")
    except Exception as e:
        results.fail("connectivity.get_net_for_item", str(e))

    # Test get_bus_members
    try:
        if buses_response and hasattr(buses_response, 'buses') and buses_response.buses:
            bus_name = buses_response.buses[0].name if hasattr(buses_response.buses[0], 'name') else None
            if bus_name:
                members = sch.connectivity.get_bus_members(bus_name)
                results.ok("connectivity.get_bus_members", f"{len(members)} members in '{bus_name}'")
            else:
                results.skip("connectivity.get_bus_members", "no bus name")
        else:
            results.skip("connectivity.get_bus_members", "no buses in schematic")
    except Exception as e:
        results.fail("connectivity.get_bus_members", str(e))

    # Test get_net_items
    try:
        if nets_response and hasattr(nets_response, 'nets') and nets_response.nets:
            net_name = nets_response.nets[0].name if hasattr(nets_response.nets[0], 'name') else None
            if net_name:
                response = sch.connectivity.get_net_items(net_name)
                results.ok("connectivity.get_net_items", f"{len(response.item_ids)} items on '{net_name}'")
            else:
                results.skip("connectivity.get_net_items", "no net name")
        else:
            results.skip("connectivity.get_net_items", "no nets in schematic")
    except Exception as e:
        results.fail("connectivity.get_net_items", str(e))

    # Test get_unconnected_pins
    try:
        unconnected = sch.connectivity.get_unconnected_pins()
        results.ok("connectivity.get_unconnected_pins", f"{len(unconnected)} unconnected pins")
    except Exception as e:
        results.fail("connectivity.get_unconnected_pins", str(e))
