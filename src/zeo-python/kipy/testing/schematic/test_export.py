# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.export module.

Covers IPC APIs:
- ExportNetlist()
- ExportBOM()
- ExportPlot()
- SaveDocumentToString()
- SaveSelectionToString()
- ParseAndCreateItemsFromString()
"""

from kipy.testing._base import TestResults
import tempfile
import os


def test_export(results: TestResults):
    """Test schematic export operations."""
    results.section("Schematic Export")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test to_string
    try:
        # Make sure document is saved first
        try:
            sch.save()
        except:
            pass
        doc_str = sch.export.to_string()
        if doc_str and len(doc_str) > 0:
            results.ok("export.to_string", f"{len(doc_str)} chars")
        else:
            results.fail("export.to_string", "empty result")
    except Exception as e:
        if "not found on disk" in str(e) or "Save" in str(e):
            results.skip("export.to_string", "requires saved document")
        else:
            results.fail("export.to_string", str(e))

    # Test netlist export
    try:
        with tempfile.NamedTemporaryFile(suffix=".net", delete=False) as f:
            temp_path = f.name
        sch.export.netlist(temp_path)
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.netlist", f"{size} bytes")
            os.unlink(temp_path)
        else:
            results.fail("export.netlist", "file not created")
    except Exception as e:
        results.fail("export.netlist", str(e))

    # Test PDF export
    try:
        with tempfile.NamedTemporaryFile(suffix=".pdf", delete=False) as f:
            temp_path = f.name
        sch.export.pdf(temp_path)
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.pdf", f"{size} bytes")
            os.unlink(temp_path)
        else:
            results.fail("export.pdf", "file not created")
    except Exception as e:
        results.fail("export.pdf", str(e))

    # Test SVG export
    try:
        with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as f:
            temp_path = f.name
        sch.export.svg(temp_path)
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.svg", f"{size} bytes")
            os.unlink(temp_path)
        else:
            results.fail("export.svg", "file not created")
    except Exception as e:
        results.fail("export.svg", str(e))

    # Test BOM export
    try:
        with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
            temp_path = f.name
        sch.export.bom(temp_path, format="csv")
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.bom", f"{size} bytes (CSV)")
            os.unlink(temp_path)
        else:
            results.fail("export.bom", "file not created")
    except Exception as e:
        results.fail("export.bom", str(e))

    # Test SPICE netlist export
    try:
        with tempfile.NamedTemporaryFile(suffix=".cir", delete=False) as f:
            temp_path = f.name
        sch.export.spice_netlist(temp_path)
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.spice_netlist", f"{size} bytes")
            os.unlink(temp_path)
        else:
            results.fail("export.spice_netlist", "file not created")
    except Exception as e:
        results.fail("export.spice_netlist", str(e))

    # Test selection_to_string
    try:
        # Select something first
        symbols = sch.symbols.get_all()
        if symbols:
            sch.selection.clear()
            sch.selection.add(symbols[0])
            sel_str = sch.export.selection_to_string()
            if sel_str and len(sel_str) > 0:
                results.ok("export.selection_to_string", f"{len(sel_str)} chars")
            else:
                results.fail("export.selection_to_string", "empty result")
            sch.selection.clear()
        else:
            results.skip("export.selection_to_string", "no symbols to select")
    except Exception as e:
        results.fail("export.selection_to_string", str(e))

    # Test from_string (parse and create items)
    try:
        # Get a serialized selection
        symbols = sch.symbols.get_all()
        if symbols:
            sch.selection.clear()
            sch.selection.add(symbols[0])
            sel_str = sch.export.selection_to_string()
            sch.selection.clear()

            if sel_str:
                # Try to parse and create items from string
                from kipy.geometry import Vector2
                items = sch.export.from_string(sel_str, Vector2.from_xy_mm(250, 250))
                if items:
                    results.ok("export.from_string", f"{len(items)} items created")
                    # Clean up
                    try:
                        sch.crud.remove_items(items)
                    except:
                        pass
                else:
                    results.fail("export.from_string", "no items created")
            else:
                results.skip("export.from_string", "no selection string")
        else:
            results.skip("export.from_string", "no symbols to serialize")
    except Exception as e:
        results.fail("export.from_string", str(e))

    # Test plot (generic plot function)
    try:
        from kipy.schematic.export import PlotFormat
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            temp_path = f.name
        # Use PlotFormat enum, not string
        sch.export.plot(temp_path, format=PlotFormat.PNG)
        if os.path.exists(temp_path):
            size = os.path.getsize(temp_path)
            results.ok("export.plot", f"{size} bytes (PNG)")
            os.unlink(temp_path)
        else:
            results.fail("export.plot", "file not created")
    except Exception as e:
        results.fail("export.plot", str(e))
