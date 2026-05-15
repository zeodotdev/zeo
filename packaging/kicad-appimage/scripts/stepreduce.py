"""
Reduce STEP file size by deduplicating entities and removing orphans.

Based on C++ source code from https://gitlab.com/sethhillbrand/stepreduce

Copyright (C) 2018-2019 Kicad Services Corporation
Author: Seth Hillbrand

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.
"""

import argparse
import math
import re
import sys
from collections import defaultdict

_REF_PATTERN = re.compile(r"#(\d+)")

# Entity types with identity semantics that must never be deduplicated, even when their
# attribute values are textually identical. Each distinct instance represents a separate
# object in the product structure.
_IDENTITY_ENTITIES = frozenset((
    "PRODUCT",
    "PRODUCT_DEFINITION",
    "PRODUCT_DEFINITION_FORMATION",
    "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE",
    "PRODUCT_DEFINITION_SHAPE",
    "PRODUCT_DEFINITION_CONTEXT",
    "PRODUCT_DEFINITION_WITH_ASSOCIATED_DOCUMENTS",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
    "SHAPE_DEFINITION_REPRESENTATION",
    "SHAPE_REPRESENTATION",
    "SHAPE_REPRESENTATION_RELATIONSHIP",
    "ADVANCED_BREP_SHAPE_REPRESENTATION",
    "MANIFOLD_SOLID_BREP",
    "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
    "GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION",
    "GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION",
    "STYLED_ITEM",
    "OVER_RIDING_STYLED_ITEM",
    "PRESENTATION_LAYER_ASSIGNMENT",
    "APPLICATION_CONTEXT",
    "APPLICATION_PROTOCOL_DEFINITION",
    "PRODUCT_CONTEXT",
    "DESIGN_CONTEXT",
))

# Root entity types for garbage collection. Entities not reachable from
# instances of these types are considered orphans.
_GC_ROOT_ENTITIES = frozenset((
    "PRODUCT_DEFINITION",
    "APPLICATION_PROTOCOL_DEFINITION",
    "SHAPE_DEFINITION_REPRESENTATION",
    "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION",
    "DRAUGHTING_MODEL",
    "PRESENTATION_LAYER_ASSIGNMENT",
    "APPLICATION_CONTEXT",
))

_NUM_PATTERN = re.compile(
    r"(?<![A-Za-z_#])"
    r"(-?\d+\.\d*(?:[eE][+-]?\d+)?|-?\d+[eE][+-]?\d+|-?\.\d+(?:[eE][+-]?\d+)?)"
)

_NAME_PATTERN = re.compile(r"^([A-Z_]+\()'[^']*'")

_UNCERTAINTY_PATTERN = re.compile(
    r"UNCERTAINTY_MEASURE_WITH_UNIT\s*\(\s*LENGTH_MEASURE\s*\(\s*"
    r"([^)]+)\s*\)",
    re.IGNORECASE,
)


def _normalize_number(match):
    """Normalize a floating-point literal to a canonical form.

    Uses pure string manipulation to avoid any precision loss from float conversion.
    Strips trailing zeros, expands scientific notation, and normalizes negative zero.
    """
    s = match.group(1)

    exp_val = 0
    upper = s.upper()

    if "E" in upper:
        idx = upper.index("E")
        mantissa = s[:idx]
        exp_val = int(s[idx + 1:])
    else:
        mantissa = s

    negative = mantissa.startswith("-")

    if negative:
        mantissa = mantissa[1:]

    if "." in mantissa:
        int_part, frac_part = mantissa.split(".", 1)
    else:
        int_part = mantissa
        frac_part = ""

    if not int_part:
        int_part = "0"

    if exp_val > 0:
        if exp_val < len(frac_part):
            int_part += frac_part[:exp_val]
            frac_part = frac_part[exp_val:]
        else:
            int_part += frac_part + "0" * (exp_val - len(frac_part))
            frac_part = ""
    elif exp_val < 0:
        shift = -exp_val

        if shift < len(int_part):
            frac_part = int_part[-shift:] + frac_part
            int_part = int_part[:-shift]
        else:
            frac_part = "0" * (shift - len(int_part)) + int_part + frac_part
            int_part = "0"

    int_part = int_part.lstrip("0") or "0"
    frac_part = frac_part.rstrip("0")

    if int_part == "0" and not frac_part:
        return "0."

    result = int_part + "." + frac_part

    if negative:
        result = "-" + result

    return result


def _round_number(match, max_decimals):
    """Truncate a floating-point literal to at most max_decimals fractional digits."""
    normalized = _normalize_number(match)

    if "." not in normalized:
        return normalized

    negative = normalized.startswith("-")

    if negative:
        normalized = normalized[1:]

    int_part, frac_part = normalized.split(".", 1)
    frac_part = frac_part[:max_decimals]
    frac_part = frac_part.rstrip("0")

    if int_part == "0" and not frac_part:
        return "0."

    result = int_part + "." + frac_part

    if negative:
        result = "-" + result

    return result


def _uncertainty_to_decimals(uncertainty):
    """Convert an uncertainty tolerance value to a number of safe decimal places."""
    if uncertainty <= 0:
        return None

    return int(math.ceil(-math.log10(uncertainty))) + 1


def _extract_uncertainty(data_lines):
    """Find the UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(val),...) in data lines."""
    for line in data_lines:
        m = _UNCERTAINTY_PATTERN.search(line)

        if m:
            try:
                return float(m.group(1))
            except ValueError:
                continue

    return None


def _normalize_entity_name(rhs):
    """Replace the first single-quoted 'name' attribute with '' for dedup comparison."""
    m = _NAME_PATTERN.match(rhs)

    if m:
        rest_start = m.end()
        return m.group(1) + "''" + rhs[rest_start:]

    return rhs


def _normalize_numbers_in_line(rhs, max_decimals=None):
    """Normalize all floating-point numbers in an entity line."""
    if max_decimals is not None:
        return _NUM_PATTERN.sub(lambda m: _round_number(m, max_decimals), rhs)

    return _NUM_PATTERN.sub(_normalize_number, rhs)


def _get_entity_type(rhs):
    """Extract the entity type name from the RHS of a STEP data line."""
    paren = rhs.find("(")

    if paren == -1:
        return rhs.strip()

    return rhs[:paren].strip()


def _is_identity_entity(entity_type):
    """Check whether the entity type has identity semantics."""
    return entity_type in _IDENTITY_ENTITIES


def _remap_references(rhs, lookup):
    """Replace all #N references in rhs using the lookup table."""
    def _replace(match):
        old = int(match.group(1))
        return f"#{lookup.get(old, old)}"

    return _REF_PATTERN.sub(_replace, rhs)


def _collect_references(rhs):
    """Return the set of entity IDs referenced by this entity line."""
    return {int(m.group(1)) for m in _REF_PATTERN.finditer(rhs)}


def _remove_orphans(lines):
    """Remove entities not reachable from root entity types."""
    id_to_rhs = {}
    id_to_refs = {}

    for line in lines:
        lhs, _, rhs = line.partition("=")
        eid = int(lhs[1:])
        id_to_rhs[eid] = rhs
        id_to_refs[eid] = _collect_references(rhs)

    referenced_by = defaultdict(set)

    for eid, refs in id_to_refs.items():
        for ref in refs:
            referenced_by[ref].add(eid)

    reachable = set()
    stack = []

    for eid, rhs in id_to_rhs.items():
        etype = _get_entity_type(rhs)

        if etype in _GC_ROOT_ENTITIES:
            stack.append(eid)
            reachable.add(eid)

    while stack:
        eid = stack.pop()

        for ref in id_to_refs.get(eid, set()):

            if ref not in reachable and ref in id_to_rhs:
                reachable.add(ref)
                stack.append(ref)

    if not reachable:
        return lines

    out = []
    renumber = {}

    for line in lines:
        lhs, _, rhs = line.partition("=")
        eid = int(lhs[1:])

        if eid in reachable:
            new_id = len(out) + 1
            renumber[eid] = new_id
            out.append((new_id, rhs))

    result = []

    for new_id, rhs in out:
        rhs = _remap_references(rhs, renumber)
        result.append(f"#{new_id}={rhs}")

    return result


def _parse_data_section(lines):
    """Parse raw file lines into header, data entities, and footer."""
    out_lines = []
    footer = []
    header = []
    past_header = False
    past_data = False
    continuing = False

    for line in lines:

        if past_header:

            if past_data or "ENDSEC;" in line:
                past_data = True
                footer.append(line)
            else:
                line = line.strip()

                if continuing:

                    if line[0].isalpha():
                        out_lines[-1] += " "

                    out_lines[-1] += line
                else:
                    out_lines.append(line)

                continuing = line[-1] != ";"
        else:

            if "DATA;" in line:
                past_header = True

            header.append(line)

    return header, out_lines, footer


def _deduplicate(data_lines, max_decimals=None):
    """Iteratively deduplicate STEP data lines until a fixed point is reached."""
    out_lines = data_lines

    while True:
        in_lines = out_lines[:]
        out_lines = []
        uniques = {}
        lookup = {}

        for line in in_lines:
            lhs, _, rhs = line.partition("=")
            oldnum = int(lhs[1:])
            rhs = rhs.strip()

            entity_type = _get_entity_type(rhs)

            norm_rhs = _normalize_numbers_in_line(rhs, max_decimals)
            norm_rhs = _normalize_entity_name(norm_rhs)

            if _is_identity_entity(entity_type):
                while norm_rhs in uniques:
                    norm_rhs += " "

                uniques[norm_rhs] = len(out_lines) + 1
                lookup[oldnum] = len(out_lines) + 1
                out_lines.append(f"#{len(out_lines) + 1}={rhs}")
            elif norm_rhs in uniques:
                lookup[oldnum] = uniques[norm_rhs]
            else:
                uniques[norm_rhs] = len(out_lines) + 1
                lookup[oldnum] = len(out_lines) + 1
                out_lines.append(f"#{len(out_lines) + 1}={rhs}")

        for i in range(len(out_lines)):
            lhs, _, rhs = out_lines[i].partition("=")
            out_lines[i] = lhs + "=" + _remap_references(rhs, lookup)

        if len(in_lines) <= len(out_lines):
            break

    return out_lines


def _strip_header_whitespace(header):
    """Remove unnecessary trailing whitespace from header lines."""
    return [line.rstrip() for line in header]


def stepreduce(input_file, output_file, verbose=False, max_decimals=None,
               use_step_precision=False):
    try:
        with open(input_file, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
    except UnicodeDecodeError:
        with open(input_file, "r", encoding="latin-1") as f:
            lines = f.read().splitlines()

    n_lines = len(lines)

    header, data_lines, footer = _parse_data_section(lines)
    header = _strip_header_whitespace(header)

    if use_step_precision:
        uncertainty = _extract_uncertainty(data_lines)

        if uncertainty is not None:
            step_decimals = _uncertainty_to_decimals(uncertainty)

            if verbose:
                print(f"stepreduce: STEP uncertainty={uncertainty}, "
                      f"derived {step_decimals} decimal places")

            if max_decimals is not None:
                max_decimals = min(max_decimals, step_decimals)
            else:
                max_decimals = step_decimals

    data_lines = _deduplicate(data_lines, max_decimals)
    data_lines = _remove_orphans(data_lines)

    with open(output_file, "w", newline="\n") as f:

        for line in header:
            f.write(line + "\n")

        for line in data_lines:
            f.write(line + "\n")

        for line in footer:
            f.write(line + "\n")

    if verbose:
        out_total = len(data_lines) + len(header) + len(footer)
        print(f"stepreduce: {input_file} {n_lines} shrunk to {out_total}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Reduce STEP file size by deduplicating entities and removing orphans."
    )
    parser.add_argument("input", help="input STEP file")
    parser.add_argument("output", help="output STEP file (may be the same as input)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print reduction statistics")
    parser.add_argument("-p", "--precision", type=int, default=None, metavar="N",
                        help="maximum decimal places for numeric comparison "
                             "(values differing only beyond this are collapsed)")
    parser.add_argument("--use-step-precision", action="store_true",
                        help="read the UNCERTAINTY_MEASURE_WITH_UNIT from the STEP file "
                             "and derive precision from it (combined with -p, the "
                             "coarser value wins)")
    args = parser.parse_args()
    stepreduce(args.input, args.output, verbose=args.verbose,
               max_decimals=args.precision, use_step_precision=args.use_step_precision)
