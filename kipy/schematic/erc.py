# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
ERC (Electrical Rules Check) and validation operations.
"""

from typing import TYPE_CHECKING, List, Optional
import re

from google.protobuf.empty_pb2 import Empty
from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


# Complete ERC error code descriptions (from erc_settings.h)
# Organized by category as shown in Schematic Setup -> Electrical Rules -> Violation Severity
ERC_CODES = {
    # Connections
    "ERCE_PIN_NOT_CONNECTED": "Pin not connected",
    "ERCE_PIN_NOT_DRIVEN": "Input pin not driven by any Output pins",
    "ERCE_POWERPIN_NOT_DRIVEN": "Input Power pin not driven by any Output Power pins",
    "ERCE_NOCONNECT_CONNECTED": "A pin with a 'no connection' flag is connected",
    "ERCE_NOCONNECT_NOT_CONNECTED": "Unconnected 'no connection' flag",
    "ERCE_LABEL_NOT_CONNECTED": "Label not connected",
    "ERCE_LABEL_SINGLE_PIN": "Label connected to only one pin",
    "ERCE_SINGLE_GLOBAL_LABEL": "Global label only appears once in the schematic",
    "ERCE_SAME_LOCAL_GLOBAL_LABEL": "Local and global labels have same name",
    "ERCE_WIRE_DANGLING": "Wires not connected to anything",
    "ERCE_BUS_ENTRY_NEEDED": "Bus Entry needed",
    "ERCE_ENDPOINT_OFF_GRID": "Symbol pin or wire end off connection grid",
    "ERCE_FOUR_WAY_JUNCTION": "Four connection points are joined together",
    "ERCE_LABEL_MULTIPLE_WIRES": "Label connects more than one wire",
    "ERCE_UNCONNECTED_WIRE_ENDPOINT": "Unconnected wire endpoint",

    # Conflicts
    "ERCE_DUPLICATE_REFERENCE": "Duplicate reference designators",
    "ERCE_DIFFERENT_UNIT_VALUE": "Units of same symbol have different values",
    "ERCE_DIFFERENT_UNIT_FP": "Different footprint assigned in another unit of the symbol",
    "ERCE_DIFFERENT_UNIT_NET": "Different net assigned to a shared pin in another unit of the symbol",
    "ERCE_DUPLICATE_SHEET_NAME": "Duplicate sheet names within a given sheet",
    "ERCE_HIERACHICAL_LABEL": "Mismatch between hierarchical labels and sheet pins",
    "ERCE_DRIVER_CONFLICT": "More than one name given to this bus or net",
    "ERCE_BUS_ALIAS_CONFLICT": "Conflict between bus alias definitions across schematic sheets",
    "ERCE_BUS_TO_BUS_CONFLICT": "Buses are graphically connected but share no bus members",
    "ERCE_BUS_ENTRY_CONFLICT": "Invalid connection between bus and net items",
    "ERCE_BUS_TO_NET_CONFLICT": "Net is graphically connected to a bus but not a bus member",
    "ERCE_GROUND_PIN_NOT_GROUND": "Ground pin not connected to ground net",

    # Miscellaneous
    "ERCE_STACKED_PIN_SYNTAX": "Pin name resembles stacked pin",
    "ERCE_UNANNOTATED": "Symbol is not annotated",
    "ERCE_UNRESOLVED_VARIABLE": "Unresolved text variable",
    "ERCE_UNDEFINED_NETCLASS": "Undefined netclass",
    "ERCE_SIMULATION_MODEL": "SPICE model issue",
    "ERCE_SIMILAR_LABELS": "Labels are similar (lower/upper case difference only)",
    "ERCE_SIMILAR_POWER": "Power pins are similar (lower/upper case difference only)",
    "ERCE_SIMILAR_LABEL_AND_POWER": "Power pin and label are similar (lower/upper case difference only)",
    "ERCE_LIB_SYMBOL_ISSUES": "Library symbol issue",
    "ERCE_LIB_SYMBOL_MISMATCH": "Symbol doesn't match copy in library",
    "ERCE_FOOTPRINT_LINK_ISSUES": "Footprint link issue",
    "ERCE_FOOTPRINT_FILTERS": "Assigned footprint doesn't match footprint filters",
    "ERCE_EXTRA_UNITS": "Symbol has more units than are defined",
    "ERCE_MISSING_UNIT": "Symbol has units that are not placed",
    "ERCE_MISSING_INPUT_PIN": "Symbol has input pins that are not placed",
    "ERCE_MISSING_BIDI_PIN": "Symbol has bidirectional pins that are not placed",
    "ERCE_MISSING_POWER_INPUT_PIN": "Symbol has power input pins that are not placed",

    # Pin-to-pin conflicts (handled by Pin Conflicts Map)
    "ERCE_PIN_TO_PIN_WARNING": "Conflict problem between pins (from Pin Conflicts Map)",
    "ERCE_PIN_TO_PIN_ERROR": "Conflict problem between pins (from Pin Conflicts Map)",
}

SEVERITY_MAP = {
    1: "error",
    2: "warning",
    3: "excluded",
}


class ERCOperations:
    """ERC (Electrical Rules Check) and validation operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # ERC Execution
    # =========================================================================

    def run(self, run_all_tests: bool = True):
        """Run ERC on the schematic.

        Args:
            run_all_tests: If True, run all tests; else only enabled tests

        Returns:
            RunERCResponse with error/warning counts and violations

        Example:
            >>> result = sch.erc.run()
            >>> print(f"Errors: {result.error_count}")
            >>> for v in result.violations:
            ...     print(f"{v.error_code}: {v.description}")
        """
        cmd = schematic_commands_pb2.RunERC()
        cmd.document.CopyFrom(self._doc)
        cmd.run_all_tests = run_all_tests
        return self._kicad.send(cmd, schematic_commands_pb2.RunERCResponse)

    def get_violations(self, severity: Optional[str] = None):
        """Get current ERC violations without re-running.

        Args:
            severity: Optional filter - "error", "warning", or "excluded"

        Returns:
            GetERCViolationsResponse
        """
        cmd = schematic_commands_pb2.GetERCViolations()
        cmd.document.CopyFrom(self._doc)

        if severity:
            severity_map = {
                "error": schematic_commands_pb2.ERC_SEV_ERROR,
                "warning": schematic_commands_pb2.ERC_SEV_WARNING,
                "excluded": schematic_commands_pb2.ERC_SEV_EXCLUDED,
            }
            if severity.lower() in severity_map:
                cmd.filter_severity = severity_map[severity.lower()]

        return self._kicad.send(cmd, schematic_commands_pb2.GetERCViolationsResponse)

    def clear_markers(self, marker_ids: Optional[List[str]] = None):
        """Clear ERC markers.

        Args:
            marker_ids: List of marker IDs to clear, or None for all

        Returns:
            ClearERCMarkersResponse with count
        """
        cmd = schematic_commands_pb2.ClearERCMarkers()
        cmd.document.CopyFrom(self._doc)

        if marker_ids:
            for mid in marker_ids:
                cmd.marker_ids.add().value = mid

        return self._kicad.send(cmd, schematic_commands_pb2.ClearERCMarkersResponse)

    def exclude_violation(self, marker_id: str):
        """Exclude (ignore) a specific ERC violation.

        Args:
            marker_id: KIID of the marker to exclude
        """
        cmd = schematic_commands_pb2.ExcludeERCViolation()
        cmd.document.CopyFrom(self._doc)
        cmd.marker_id.value = marker_id
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Analysis and Reporting
    # =========================================================================

    def analyze(self) -> dict:
        """Run ERC and return detailed analysis.

        Returns:
            Dictionary with:
            - error_count: Number of errors
            - warning_count: Number of warnings
            - violations: List of violation dicts
            - by_type: Violations grouped by error code
            - by_severity: Violations grouped by severity
            - summary: Human-readable summary

        Example:
            >>> analysis = sch.erc.analyze()
            >>> print(analysis['summary'])
        """
        result = self.run()

        violations = []
        by_type = {}
        by_severity = {"error": [], "warning": [], "excluded": []}

        for v in result.violations:
            error_code = v.error_code
            # Stable string code (e.g. "pin_to_pin_error"); preferred over
            # error_code (numeric, may shift) for assertions.
            error_type_code = getattr(v, "error_type_code", "") or ""
            severity = SEVERITY_MAP.get(v.severity, "unknown")
            description = v.description or ERC_CODES.get(error_code, error_code)

            violation_info = {
                "id": v.id.value if v.id else None,
                "error_code": error_code,
                "error_type_code": error_type_code,
                "description": description,
                "severity": severity,
                "position_nm": (v.position.x_nm, v.position.y_nm) if v.position else None,
                "position_mm": (v.position.x_nm / 1e6, v.position.y_nm / 1e6) if v.position else None,
                "item_ids": [item.value for item in v.item_ids] if v.item_ids else [],
            }

            violations.append(violation_info)

            if error_code not in by_type:
                by_type[error_code] = []
            by_type[error_code].append(violation_info)

            if severity in by_severity:
                by_severity[severity].append(violation_info)

        summary_lines = [
            f"ERC Results: {result.error_count} errors, {result.warning_count} warnings"
        ]
        if by_type:
            summary_lines.append("\nViolations by type:")
            for code, items in sorted(by_type.items()):
                desc = ERC_CODES.get(code, code)
                summary_lines.append(f"  {code}: {len(items)} - {desc}")

        return {
            "error_count": result.error_count,
            "warning_count": result.warning_count,
            "violations": violations,
            "by_type": by_type,
            "by_severity": by_severity,
            "summary": "\n".join(summary_lines),
        }

    def get_error_codes(self) -> dict:
        """Get dictionary of ERC error codes and descriptions.

        Note:
            ERC settings (severity per rule) are stored in the project
            file and cannot be modified via API. Use KiCad GUI:
            Inspect -> Electrical Rules Checker -> Settings

        Returns:
            Dictionary mapping error codes to descriptions
        """
        return ERC_CODES.copy()

    # =========================================================================
    # Annotation Validation
    # =========================================================================

    def check_annotation(self, scope: str = "all", recursive: bool = True):
        """Check for annotation errors.

        Args:
            scope: "all", "current_sheet", or "selection"
            recursive: Include subsheets

        Returns:
            CheckAnnotationResponse with error count and details
        """
        cmd = schematic_commands_pb2.CheckAnnotation()
        cmd.document.CopyFrom(self._doc)

        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)
        cmd.recursive = recursive

        return self._kicad.send(cmd, schematic_commands_pb2.CheckAnnotationResponse)

    def annotate(
        self,
        scope: str = "all",
        order: str = "x_y",
        algorithm: str = "incremental",
        start_number: int = 1,
        reset_existing: bool = False,
        recursive: bool = True,
    ):
        """Annotate symbols with reference designators.

        Args:
            scope: "all", "current_sheet", or "selection"
            order: "x_y" (left-to-right) or "y_x"
            algorithm: "incremental" (keep existing) or "restart"
            start_number: Starting number (default 1)
            reset_existing: Clear existing annotation first
            recursive: Include subsheets

        Returns:
            AnnotateSymbolsResponse with count
        """
        cmd = schematic_commands_pb2.AnnotateSymbols()
        cmd.document.CopyFrom(self._doc)

        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)

        order_map = {
            "x_y": schematic_commands_pb2.AO_X_Y,
            "y_x": schematic_commands_pb2.AO_Y_X,
        }
        cmd.order = order_map.get(order.lower(), schematic_commands_pb2.AO_X_Y)

        algo_map = {
            "incremental": schematic_commands_pb2.AA_INCREMENTAL,
            "restart": schematic_commands_pb2.AA_RESTART,
        }
        cmd.algorithm = algo_map.get(algorithm.lower(), schematic_commands_pb2.AA_INCREMENTAL)

        cmd.start_number = start_number
        cmd.reset_existing = reset_existing
        cmd.recursive = recursive

        return self._kicad.send(cmd, schematic_commands_pb2.AnnotateSymbolsResponse)

    def clear_annotation(self, scope: str = "all", recursive: bool = True):
        """Clear annotation from symbols.

        Args:
            scope: "all", "current_sheet", or "selection"
            recursive: Include subsheets

        Returns:
            ClearAnnotationResponse with count
        """
        cmd = schematic_commands_pb2.ClearAnnotation()
        cmd.document.CopyFrom(self._doc)

        scope_map = {
            "all": schematic_commands_pb2.AS_ALL,
            "current_sheet": schematic_commands_pb2.AS_CURRENT_SHEET,
            "selection": schematic_commands_pb2.AS_SELECTION,
        }
        cmd.scope = scope_map.get(scope.lower(), schematic_commands_pb2.AS_ALL)
        cmd.recursive = recursive

        return self._kicad.send(cmd, schematic_commands_pb2.ClearAnnotationResponse)

    # =========================================================================
    # Comprehensive Validation
    # =========================================================================

    def validate(self) -> dict:
        """Perform comprehensive schematic validation.

        Combines annotation checks and ERC.

        Returns:
            Dictionary with:
            - is_valid: True if no errors
            - annotation_errors: List of annotation issues
            - erc_errors: List of ERC errors
            - erc_warnings: List of ERC warnings
            - unconnected_pins: List of unconnected pins
            - summary: Human-readable summary

        Example:
            >>> result = sch.erc.validate()
            >>> if result['is_valid']:
            ...     print("Schematic is valid!")
            ... else:
            ...     print(result['summary'])
        """
        issues = {
            "is_valid": True,
            "annotation_errors": [],
            "erc_errors": [],
            "erc_warnings": [],
            "unconnected_pins": [],
            "summary": "",
        }

        summary_lines = []

        # Check annotation
        try:
            ann_result = self.check_annotation()
            if ann_result.error_count > 0:
                issues["is_valid"] = False
                for err in ann_result.errors:
                    issues["annotation_errors"].append({
                        "type": err.error_type,
                        "message": err.message,
                    })
                summary_lines.append(f"Annotation errors: {ann_result.error_count}")
        except Exception as e:
            summary_lines.append(f"Annotation check failed: {e}")

        # Run ERC
        try:
            analysis = self.analyze()
            issues["erc_errors"] = analysis["by_severity"]["error"]
            issues["erc_warnings"] = analysis["by_severity"]["warning"]

            if analysis["error_count"] > 0:
                issues["is_valid"] = False
                summary_lines.append(f"ERC errors: {analysis['error_count']}")

            if analysis["warning_count"] > 0:
                summary_lines.append(f"ERC warnings: {analysis['warning_count']}")
        except Exception as e:
            summary_lines.append(f"ERC check failed: {e}")

        # Check unconnected pins
        try:
            issues["unconnected_pins"] = self._sch.connectivity.get_unconnected_pins()
            if issues["unconnected_pins"]:
                summary_lines.append(f"Unconnected pins: {len(issues['unconnected_pins'])}")
        except Exception as e:
            summary_lines.append(f"Unconnected pin check failed: {e}")

        if issues["is_valid"]:
            issues["summary"] = "Schematic validation passed (no errors found)"
        else:
            issues["summary"] = "Validation FAILED:\n" + "\n".join(summary_lines)

        return issues

    # =========================================================================
    # ERC Configuration (Limited - Settings stored in project file)
    # =========================================================================

    def get_settings(self) -> dict:
        """Get ERC settings.

        Returns:
            Dictionary with:
            - rule_severities: Dict mapping error codes to severity
            - check_bus_driver_conflicts: bool
            - check_bus_to_net_conflicts: bool
            - check_bus_entry_conflicts: bool
            - check_similar_labels: bool
            - check_unique_global_labels: bool

        Example:
            >>> settings = sch.erc.get_settings()
            >>> print(settings['rule_severities'])
        """
        cmd = schematic_commands_pb2.GetERCSettings()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetERCSettingsResponse)

        severity_names = {
            schematic_commands_pb2.ERC_SEV_ERROR: "error",
            schematic_commands_pb2.ERC_SEV_WARNING: "warning",
            schematic_commands_pb2.ERC_SEV_EXCLUDED: "ignore",
        }

        rule_severities = {}
        for rule in response.settings.rule_severities:
            sev = severity_names.get(rule.severity, "unknown")
            rule_severities[rule.error_code] = sev

        return {
            "rule_severities": rule_severities,
            "check_bus_driver_conflicts": response.settings.check_bus_driver_conflicts,
            "check_bus_to_net_conflicts": response.settings.check_bus_to_net_conflicts,
            "check_bus_entry_conflicts": response.settings.check_bus_entry_conflicts,
            "check_similar_labels": response.settings.check_similar_labels,
            "check_unique_global_labels": response.settings.check_unique_global_labels,
            "available_rules": list(ERC_CODES.keys()),
        }

    def set_settings(
        self,
        rule_severities: dict = None,
    ) -> None:
        """Set ERC rule severities.

        Args:
            rule_severities: Dict mapping error codes to severity ("error", "warning", "ignore")

        Example:
            >>> sch.erc.set_settings(rule_severities={
            ...     "ERCE_PIN_NOT_CONNECTED": "ignore",
            ...     "ERCE_SIMILAR_LABELS": "error",
            ... })
        """
        cmd = schematic_commands_pb2.SetERCSettings()
        cmd.document.CopyFrom(self._doc)

        if rule_severities:
            severity_map = {
                "error": schematic_commands_pb2.ERC_SEV_ERROR,
                "warning": schematic_commands_pb2.ERC_SEV_WARNING,
                "ignore": schematic_commands_pb2.ERC_SEV_EXCLUDED,
            }

            for code, sev in rule_severities.items():
                rule = cmd.settings.rule_severities.add()
                rule.error_code = code
                rule.severity = severity_map.get(sev.lower(), schematic_commands_pb2.ERC_SEV_WARNING)

        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Pin Type Matrix Operations
    # =========================================================================

    # Pin type names for reference
    PIN_TYPES = [
        "input",
        "output",
        "bidirectional",
        "tri_state",
        "passive",
        "free",           # not internally connected
        "unspecified",
        "power_in",
        "power_out",
        "open_collector",
        "open_emitter",
        "no_connect",
    ]

    # Pin error types
    PIN_ERROR_TYPES = ["ok", "warning", "error", "unconnected"]

    def get_pin_type_matrix(self) -> dict:
        """Get the ERC pin type matrix.

        The pin type matrix defines what happens when two pins of different
        electrical types are connected. For example, connecting an output
        to another output might be an error, while input to output is ok.

        Returns:
            Dictionary with:
            - matrix: 2D dict where matrix[pin1][pin2] = error_type
            - pin_types: List of pin type names
            - error_types: List of error type names

        Example:
            >>> matrix_info = sch.erc.get_pin_type_matrix()
            >>> # Check what happens when output connects to output
            >>> print(matrix_info['matrix']['output']['output'])
            'error'
        """
        cmd = schematic_commands_pb2.GetPinTypeMatrix()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(cmd, schematic_commands_pb2.GetPinTypeMatrixResponse)

        error_map = {
            schematic_commands_pb2.PET_OK: "ok",
            schematic_commands_pb2.PET_WARNING: "warning",
            schematic_commands_pb2.PET_ERROR: "error",
            schematic_commands_pb2.PET_UNCONNECTED: "unconnected",
        }

        # Build 2D dict
        matrix = {pt: {} for pt in self.PIN_TYPES}

        for entry in response.matrix.entries:
            first = self.PIN_TYPES[entry.first_pin_type] if entry.first_pin_type < len(self.PIN_TYPES) else "unknown"
            second = self.PIN_TYPES[entry.second_pin_type] if entry.second_pin_type < len(self.PIN_TYPES) else "unknown"
            error = error_map.get(entry.error_type, "unknown")
            if first in matrix:
                matrix[first][second] = error

        return {
            "matrix": matrix,
            "pin_types": self.PIN_TYPES.copy(),
            "error_types": self.PIN_ERROR_TYPES.copy(),
        }

    def set_pin_type_matrix(
        self,
        entries: dict = None,
        reset_to_defaults: bool = False,
    ) -> None:
        """Set entries in the ERC pin type matrix.

        Args:
            entries: Dict mapping (pin1, pin2) tuples to error type
                    e.g., {("output", "output"): "error"}
            reset_to_defaults: If True, reset matrix to defaults before applying

        Example:
            >>> # Make output-to-output a warning instead of error
            >>> sch.erc.set_pin_type_matrix({
            ...     ("output", "output"): "warning",
            ... })
            >>> # Reset to defaults
            >>> sch.erc.set_pin_type_matrix(reset_to_defaults=True)
        """
        cmd = schematic_commands_pb2.SetPinTypeMatrix()
        cmd.document.CopyFrom(self._doc)
        cmd.reset_to_defaults = reset_to_defaults

        if entries:
            error_map = {
                "ok": schematic_commands_pb2.PET_OK,
                "warning": schematic_commands_pb2.PET_WARNING,
                "error": schematic_commands_pb2.PET_ERROR,
                "unconnected": schematic_commands_pb2.PET_UNCONNECTED,
            }

            for (pin1, pin2), error_type in entries.items():
                entry = cmd.entries.add()

                if pin1 in self.PIN_TYPES:
                    entry.first_pin_type = self.PIN_TYPES.index(pin1)
                if pin2 in self.PIN_TYPES:
                    entry.second_pin_type = self.PIN_TYPES.index(pin2)

                entry.error_type = error_map.get(error_type.lower(), schematic_commands_pb2.PET_OK)

        self._kicad.send(cmd, Empty)

    def _get_violation_field(self, violation, field: str, default=None):
        """Get a field from a violation, handling both dict and proto objects."""
        if isinstance(violation, dict):
            return violation.get(field, default)
        # Proto object - access attribute directly
        return getattr(violation, field, default)

    def filter_violations(
        self,
        violations: list,
        include_codes: list = None,
        exclude_codes: list = None,
        severity: str = None,
    ) -> list:
        """Filter ERC violations by criteria.

        Args:
            violations: List of violation dicts (from analyze()) or proto objects
            include_codes: Only include these error codes
            exclude_codes: Exclude these error codes
            severity: Filter by severity ("error", "warning")

        Returns:
            Filtered list of violations

        Example:
            >>> result = sch.erc.analyze()
            >>> # Get only pin connection errors
            >>> pin_errors = sch.erc.filter_violations(
            ...     result['violations'],
            ...     include_codes=['ERCE_PIN_NOT_CONNECTED', 'ERCE_PIN_NOT_DRIVEN']
            ... )
        """
        # Convert proto repeated field to list if needed
        if hasattr(violations, '__iter__') and not isinstance(violations, (list, tuple)):
            violations = list(violations)

        filtered = violations

        if include_codes:
            filtered = [v for v in filtered if self._get_violation_field(v, 'error_code') in include_codes]

        if exclude_codes:
            filtered = [v for v in filtered if self._get_violation_field(v, 'error_code') not in exclude_codes]

        if severity:
            filtered = [v for v in filtered if self._get_violation_field(v, 'severity') == severity]

        return filtered

    def group_by_symbol(self, violations: list) -> dict:
        """Group ERC violations by symbol reference.

        Args:
            violations: List of violation dicts (from analyze()) or proto objects

        Returns:
            Dictionary mapping symbol references to their violations

        Example:
            >>> result = sch.erc.analyze()
            >>> by_symbol = sch.erc.group_by_symbol(result['violations'])
            >>> for ref, issues in by_symbol.items():
            ...     print(f"{ref}: {len(issues)} issues")
        """
        # Convert proto repeated field to list if needed
        if hasattr(violations, '__iter__') and not isinstance(violations, (list, tuple)):
            violations = list(violations)

        by_symbol = {}

        for v in violations:
            # Try to extract symbol reference from description
            desc = self._get_violation_field(v, 'description', '')
            ref_match = re.search(r'\b([A-Z]+\d+)\b', desc)
            if ref_match:
                ref = ref_match.group(1)
                if ref not in by_symbol:
                    by_symbol[ref] = []
                by_symbol[ref].append(v)

        return by_symbol

    def get_fixable_issues(self) -> dict:
        """Analyze schematic and categorize issues by fix type.

        Returns:
            Dictionary with categories:
            - annotation_needed: Symbols needing annotation
            - unconnected_pins: Pins that need wiring or no-connect
            - duplicate_refs: Duplicate reference designators
            - other: Other issues

        Example:
            >>> issues = sch.erc.get_fixable_issues()
            >>> if issues['annotation_needed']:
            ...     sch.erc.annotate()
        """
        result = self.analyze()

        fixable = {
            "annotation_needed": [],
            "unconnected_pins": [],
            "duplicate_refs": [],
            "label_issues": [],
            "other": [],
        }

        for v in result['violations']:
            code = v.get('error_code', '')

            if code == 'ERCE_UNANNOTATED':
                fixable['annotation_needed'].append(v)
            elif code in ['ERCE_PIN_NOT_CONNECTED', 'ERCE_PIN_NOT_DRIVEN']:
                fixable['unconnected_pins'].append(v)
            elif code == 'ERCE_DUPLICATE_REFERENCE':
                fixable['duplicate_refs'].append(v)
            elif code in ['ERCE_LABEL_NOT_CONNECTED', 'ERCE_SIMILAR_LABELS']:
                fixable['label_issues'].append(v)
            else:
                fixable['other'].append(v)

        return fixable

    def auto_fix(self, fix_types: list = None) -> dict:
        """Attempt to automatically fix certain ERC issues.

        Args:
            fix_types: List of fix types to apply. Options:
                - "annotation": Run annotation to fix unannotated symbols
                - "no_connect": Add no-connect markers to unconnected pins
                       (NOT IMPLEMENTED - requires manual review)

        Returns:
            Dictionary with results of each fix type

        Example:
            >>> result = sch.erc.auto_fix(["annotation"])
            >>> print(f"Annotated {result['annotation']['count']} symbols")
        """
        if fix_types is None:
            fix_types = ["annotation"]

        results = {}

        if "annotation" in fix_types:
            try:
                ann_result = self.annotate(reset_existing=False)
                results["annotation"] = {
                    "success": True,
                    "count": ann_result.symbols_annotated,
                }
            except Exception as e:
                results["annotation"] = {
                    "success": False,
                    "error": str(e),
                }

        if "no_connect" in fix_types:
            # This would require identifying truly unconnected pins
            # vs pins that should be wired. Too risky to auto-fix.
            results["no_connect"] = {
                "success": False,
                "error": "Auto no-connect not implemented - requires manual review",
            }

        return results

    def generate_report(self, format: str = "text") -> str:
        """Generate a detailed ERC report.

        Args:
            format: "text" or "markdown"

        Returns:
            Formatted report string

        Example:
            >>> report = sch.erc.generate_report(format="markdown")
            >>> print(report)
        """
        result = self.analyze()

        if format == "markdown":
            lines = [
                "# ERC Report",
                "",
                f"**Errors:** {result['error_count']}",
                f"**Warnings:** {result['warning_count']}",
                "",
            ]

            if result['by_type']:
                lines.append("## Violations by Type")
                lines.append("")
                for code, items in sorted(result['by_type'].items()):
                    desc = ERC_CODES.get(code, code)
                    lines.append(f"### {code} ({len(items)})")
                    lines.append(f"*{desc}*")
                    lines.append("")
                    for item in items[:10]:  # Limit to 10 per type
                        pos = item.get('position_mm')
                        if pos:
                            lines.append(f"- At ({pos[0]:.1f}, {pos[1]:.1f}) mm")
                        else:
                            lines.append(f"- {item.get('description', 'No details')}")
                    if len(items) > 10:
                        lines.append(f"- ... and {len(items) - 10} more")
                    lines.append("")

            return "\n".join(lines)

        else:  # text format
            lines = [
                "=" * 60,
                "ERC REPORT",
                "=" * 60,
                f"Errors:   {result['error_count']}",
                f"Warnings: {result['warning_count']}",
                "",
            ]

            if result['by_type']:
                lines.append("VIOLATIONS BY TYPE:")
                lines.append("-" * 40)
                for code, items in sorted(result['by_type'].items()):
                    desc = ERC_CODES.get(code, code)
                    lines.append(f"\n{code}: {len(items)}")
                    lines.append(f"  ({desc})")
                    for item in items[:5]:
                        pos = item.get('position_mm')
                        if pos:
                            lines.append(f"    - At ({pos[0]:.1f}, {pos[1]:.1f}) mm")
                    if len(items) > 5:
                        lines.append(f"    ... and {len(items) - 5} more")

            lines.append("")
            lines.append("=" * 60)
            return "\n".join(lines)
