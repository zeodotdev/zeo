# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.simulation module.

Covers IPC APIs:
- GetSimulationSettings()
- SetSimulationSettings()
- RunSimulation()
- GetSimulationResults()
"""

from kipy.testing._base import TestResults


def test_simulation(results: TestResults):
    """Test schematic simulation operations."""
    results.section("Schematic Simulation (SPICE)")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_settings
    try:
        settings = sch.simulation.get_settings()
        if settings:
            results.ok("simulation.get_settings", "retrieved")
        else:
            results.ok("simulation.get_settings", "no settings (SPICE not configured)")
    except Exception as e:
        if "spice" in str(e).lower() or "simulator" in str(e).lower():
            results.skip("simulation.get_settings", "SPICE simulator not available")
        else:
            results.fail("simulation.get_settings", str(e))

    # Test set_settings
    try:
        settings = sch.simulation.get_settings()
        if settings:
            # Unpack only the settable parameters (not 'simulator' which is read-only)
            sch.simulation.set_settings(
                spice_command=settings.get('spice_command'),
                include_paths=settings.get('include_paths'),
                model_overrides=settings.get('model_overrides'),
            )
            results.ok("simulation.set_settings", "updated")
        else:
            results.skip("simulation.set_settings", "no settings to modify")
    except Exception as e:
        if "spice" in str(e).lower() or "simulator" in str(e).lower():
            results.skip("simulation.set_settings", "SPICE simulator not available")
        else:
            results.fail("simulation.set_settings", str(e))

    # Test run (this may fail if schematic isn't set up for simulation)
    try:
        sch.simulation.run(".tran 1u 1m")
        results.ok("simulation.run", "executed .tran command")
    except Exception as e:
        error_str = str(e).lower()
        if "spice" in error_str or "simulator" in error_str or "ngspice" in error_str:
            results.skip("simulation.run", "SPICE simulator not available")
        elif "netlist" in error_str or "circuit" in error_str:
            results.skip("simulation.run", "schematic not set up for simulation")
        else:
            results.fail("simulation.run", str(e))

    # Test get_results
    try:
        results_data = sch.simulation.get_results()
        if results_data:
            results.ok("simulation.get_results", "retrieved")
        else:
            results.ok("simulation.get_results", "no results (simulation not run)")
    except Exception as e:
        if "spice" in str(e).lower() or "simulator" in str(e).lower():
            results.skip("simulation.get_results", "SPICE simulator not available")
        elif "no simulation" in str(e).lower() or "not run" in str(e).lower():
            results.skip("simulation.get_results", "no simulation has been run")
        else:
            results.fail("simulation.get_results", str(e))
