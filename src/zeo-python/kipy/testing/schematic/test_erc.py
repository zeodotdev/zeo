# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Tests for kipy.schematic.erc module.

Covers IPC APIs:
- RunERC()
- GetERCViolations()
- ClearERCMarkers()
- ExcludeERCViolation()
- GetERCSettings()
- SetERCSettings()
- GetPinTypeMatrix()
- SetPinTypeMatrix()
- AnnotateSymbols()
- ClearAnnotation()
- CheckAnnotation()

Covers QoL functions:
- erc.run()
- erc.get_violations()
- erc.clear_markers()
- erc.exclude_violation()
- erc.get_settings()
- erc.set_settings()
- erc.get_pin_matrix()
- erc.set_pin_matrix()
- erc.annotate()
- erc.clear_annotation()
- erc.check_annotation()
- erc.analyze()
- erc.filter_violations()
- erc.group_by_symbol()
- erc.get_error_codes()
- erc.validate()
- erc.generate_report()
"""

from kipy.testing._base import TestResults


def test_erc(results: TestResults):
    """Test schematic ERC operations."""
    results.section("Schematic ERC")

    from kipy import KiCad

    kicad = KiCad()
    sch = kicad.get_schematic()

    # Test run
    try:
        sch.erc.run()
        results.ok("erc.run")
    except Exception as e:
        results.fail("erc.run", str(e))

    # Test get_violations
    violations_response = None
    try:
        violations_response = sch.erc.get_violations()
        violation_count = len(violations_response.violations) if hasattr(violations_response, 'violations') else 0
        results.ok("erc.get_violations", f"{violation_count} violations")
    except Exception as e:
        results.fail("erc.get_violations", str(e))

    # Test check_annotation
    try:
        result = sch.erc.check_annotation()
        results.ok("erc.check_annotation", "checked")
    except Exception as e:
        results.fail("erc.check_annotation", str(e))

    # Test clear_markers
    try:
        sch.erc.clear_markers()
        results.ok("erc.clear_markers")
    except Exception as e:
        results.fail("erc.clear_markers", str(e))

    # Test get_settings
    try:
        settings = sch.erc.get_settings()
        if settings:
            results.ok("erc.get_settings", "retrieved")
        else:
            results.fail("erc.get_settings", "no settings returned")
    except Exception as e:
        results.fail("erc.get_settings", str(e))

    # Test set_settings
    try:
        settings = sch.erc.get_settings()
        if settings and settings.get('rule_severities'):
            # set_settings expects rule_severities dict
            sch.erc.set_settings(rule_severities=settings.get('rule_severities'))
            results.ok("erc.set_settings", "updated")
        else:
            results.skip("erc.set_settings", "no rule severities to modify")
    except Exception as e:
        results.fail("erc.set_settings", str(e))

    # Test get_pin_type_matrix (correct method name)
    pin_matrix = None
    try:
        pin_matrix = sch.erc.get_pin_type_matrix()
        if pin_matrix:
            results.ok("erc.get_pin_type_matrix", "retrieved")
        else:
            results.fail("erc.get_pin_type_matrix", "no matrix returned")
    except Exception as e:
        results.fail("erc.get_pin_type_matrix", str(e))

    # Test set_pin_type_matrix (correct method name)
    try:
        if pin_matrix and pin_matrix.get('matrix'):
            # set_pin_type_matrix expects entries dict or reset_to_defaults
            # Just test with an empty entries dict to avoid changing settings
            sch.erc.set_pin_type_matrix(entries={})
            results.ok("erc.set_pin_type_matrix", "called (no changes)")
        else:
            results.skip("erc.set_pin_type_matrix", "no matrix to modify")
    except Exception as e:
        results.fail("erc.set_pin_type_matrix", str(e))

    # Test exclude_violation
    try:
        # Run ERC first to get violations
        sch.erc.run()
        violations_response = sch.erc.get_violations()
        if violations_response and hasattr(violations_response, 'violations') and violations_response.violations:
            violation = violations_response.violations[0]
            violation_id = violation.id if hasattr(violation, 'id') else None
            if violation_id:
                sch.erc.exclude_violation(violation_id)
                results.ok("erc.exclude_violation", "excluded")
            else:
                results.skip("erc.exclude_violation", "no violation ID")
        else:
            results.skip("erc.exclude_violation", "no violations to exclude")
    except Exception as e:
        results.fail("erc.exclude_violation", str(e))

    # Test annotate
    try:
        sch.erc.annotate()
        results.ok("erc.annotate", "annotated")
    except Exception as e:
        results.fail("erc.annotate", str(e))

    # Test clear_annotation (be careful with this)
    # We'll skip actually clearing annotation to avoid disrupting the schematic
    results.skip("erc.clear_annotation", "skipped to preserve schematic state")

    # Test analyze (QoL function)
    try:
        analysis = sch.erc.analyze()
        if analysis:
            results.ok("erc.analyze", "analysis complete")
        else:
            results.ok("erc.analyze", "no issues")
    except Exception as e:
        results.fail("erc.analyze", str(e))

    # Test filter_violations (QoL function)
    try:
        # analyze() returns a dict with 'violations' list
        analysis = sch.erc.analyze()
        violations = analysis.get('violations', [])
        filtered = sch.erc.filter_violations(violations, severity="error")
        results.ok("erc.filter_violations", f"{len(filtered)} errors")
    except Exception as e:
        results.fail("erc.filter_violations", str(e))

    # Test group_by_symbol (QoL function)
    try:
        # group_by_symbol requires violations list parameter
        analysis = sch.erc.analyze()
        violations = analysis.get('violations', [])
        grouped = sch.erc.group_by_symbol(violations)
        results.ok("erc.group_by_symbol", f"{len(grouped)} symbols with violations")
    except Exception as e:
        results.fail("erc.group_by_symbol", str(e))

    # Test get_error_codes (QoL function)
    try:
        codes = sch.erc.get_error_codes()
        results.ok("erc.get_error_codes", f"{len(codes)} error codes")
    except Exception as e:
        results.fail("erc.get_error_codes", str(e))

    # Test validate (QoL function)
    try:
        is_valid = sch.erc.validate()
        results.ok("erc.validate", f"valid={is_valid}")
    except Exception as e:
        results.fail("erc.validate", str(e))

    # Test generate_report (QoL function)
    try:
        report = sch.erc.generate_report()
        if report:
            results.ok("erc.generate_report", f"{len(report)} chars")
        else:
            results.ok("erc.generate_report", "empty report (no issues)")
    except Exception as e:
        results.fail("erc.generate_report", str(e))
