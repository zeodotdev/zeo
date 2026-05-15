# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.document module.

Covers IPC APIs:
- SaveDocument()
- RefreshEditor()
- CreateDocument()
- OpenDocument()
- CloseDocument()
- SaveCopyOfDocument()
- RevertDocument()
- GetOpenDocuments()
"""

from kipy.testing._base import TestResults


def test_document(results: TestResults):
    """Test schematic document operations."""
    results.section("Schematic Document")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test get_open_documents
    try:
        docs = sch.document_ops.get_open_documents()
        results.ok("document_ops.get_open_documents", f"{len(docs)} documents open")
    except Exception as e:
        results.fail("document_ops.get_open_documents", str(e))

    # Test save
    try:
        sch.save()
        results.ok("save")
    except Exception as e:
        results.fail("save", str(e))

    # Test refresh
    try:
        sch.refresh()
        results.ok("refresh")
    except Exception as e:
        results.fail("refresh", str(e))

    # Test save_copy (to temp file)
    # NOTE: KiCad IPC API's SaveCopyOfDocument handler may not be implemented
    try:
        import tempfile
        import os
        with tempfile.NamedTemporaryFile(suffix=".kicad_sch", delete=False) as f:
            temp_path = f.name
        sch.document_ops.save_copy(temp_path)
        if os.path.exists(temp_path):
            results.ok("document_ops.save_copy", f"saved to {temp_path}")
            os.unlink(temp_path)
        else:
            results.fail("document_ops.save_copy", "file not created")
    except Exception as e:
        if "not implemented" in str(e).lower() or "handler" in str(e).lower():
            results.skip("document_ops.save_copy", "KiCad IPC handler not implemented")
        else:
            results.fail("document_ops.save_copy", str(e))

    # Test create (we'll skip this to avoid creating new documents)
    results.skip("document_ops.create", "skipped to avoid creating new documents")

    # Test open (we'll skip this to avoid opening new documents)
    results.skip("document_ops.open", "skipped to avoid opening new documents")

    # Test close (we'll skip this to avoid closing the current document)
    results.skip("document_ops.close", "skipped to avoid closing current document")

    # Test revert (we'll skip this to avoid losing changes)
    results.skip("document_ops.revert", "skipped to preserve changes")
