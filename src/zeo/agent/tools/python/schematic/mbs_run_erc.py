# Copyright (C) 2026, Zeo <team@zeo.dev>

import json

# ERC code descriptions match those in sch_run_erc.py — the underlying ERC
# engine is the same; this mirror keeps the LLM-facing output consistent.
ERC_DESCRIPTIONS = {
    0: 'Unspecified',
    1: 'Duplicate sheet name',
    2: 'Pin or wire endpoint off grid',
    3: 'Pin not connected',
    4: 'Pin not driven',
    5: 'Power pin not driven',
    6: 'Hierarchical label mismatch',
    7: 'No-connect connected to pins',
    8: 'No-connect not connected',
    9: 'Label not connected',
    10: 'Similar labels (case mismatch)',
    11: 'Similar power symbols',
    12: 'Similar label and power',
    13: 'Single global label',
    14: 'Same local/global label',
    15: 'Different unit footprints',
    16: 'Missing power input pin',
    17: 'Missing input pin',
    18: 'Missing bidirectional pin',
    19: 'Missing unit',
    20: 'Different unit net',
    21: 'Bus alias conflict',
    22: 'Driver conflict',
    23: 'Bus entry conflict',
    24: 'Bus to bus conflict',
    25: 'Bus to net conflict',
    26: 'Ground pin not on ground net',
    27: 'Label on single pin',
    28: 'Unresolved variable',
    29: 'Undefined netclass',
    30: 'Simulation model error',
    31: 'Dangling wire',
    32: 'Library symbol issues',
    33: 'Library symbol mismatch',
    34: 'Footprint link issues',
    35: 'Footprint filter mismatch',
    36: 'Unannotated symbol',
    37: 'Extra units',
    38: 'Different unit values',
    39: 'Duplicate reference',
    40: 'Bus entry needed',
    41: 'Four-way junction',
    42: 'Label on multiple wires',
    43: 'Unconnected wire endpoint',
    44: 'Stacked pin syntax',
}


def get_erc_description(code):
    try:
        code_int = int(code) if isinstance(code, str) else code
        return ERC_DESCRIPTIONS.get(code_int, f'Unknown error code {code}')
    except Exception:
        return str(code)


output_format = TOOL_ARGS.get("output_format", "summary")
include_warnings = TOOL_ARGS.get("include_warnings", True)

# `mbs` is bound by the BuildModeInitCode bootstrap when app: "mbs". The
# inherited API_HANDLER_MBS_SCH (subclass of API_HANDLER_SCH) handles RunERC
# transparently — same connectivity engine, plus the cross-board ERC
# checks (M5.8) that surface unwired module pins, single-endpoint nets,
# and pin-type mismatches at the boundary.
result = mbs.erc.analyze()

if output_format == "summary":
    lines = [f"MBS ERC Results: {result['error_count']} errors, {result['warning_count']} warnings", '']
    if result.get('by_type'):
        for code, items in sorted(result['by_type'].items(), key=lambda x: -len(x[1])):
            desc = get_erc_description(code)
            errors = sum(1 for v in items if v.get('severity') == 'error')
            warnings = len(items) - errors
            if errors > 0 and warnings > 0:
                lines.append(f"  {desc}: {errors} errors, {warnings} warnings")
            elif errors > 0:
                lines.append(f"  {desc}: {errors} errors")
            else:
                lines.append(f"  {desc}: {warnings} warnings")
    print('\n'.join(lines))

elif output_format == "detailed":
    if not include_warnings:
        result['violations'] = [v for v in result['violations'] if v.get('severity') == 'error']

    for v in result.get('violations', []):
        if 'code' in v:
            v['description'] = get_erc_description(v['code'])
    print(json.dumps({
        'error_count': result['error_count'],
        'warning_count': result['warning_count'],
        'violations': result.get('violations', []),
    }, indent=2))

elif output_format == "by_type":
    by_type_readable = {}
    for code, items in result.get('by_type', {}).items():
        desc = get_erc_description(code)
        errors = sum(1 for v in items if v.get('severity') == 'error')
        warnings = len(items) - errors
        by_type_readable[desc] = {'total': len(items), 'errors': errors, 'warnings': warnings}
    print(json.dumps({
        'error_count': result['error_count'],
        'warning_count': result['warning_count'],
        'by_type': by_type_readable,
    }, indent=2))
