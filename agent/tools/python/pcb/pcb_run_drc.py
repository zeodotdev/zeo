import json

refill_zones = TOOL_ARGS.get("refill_zones", True)
output_format = TOOL_ARGS.get("output_format", "summary")

try:
    errors, warnings, exclusions = board.drc.run(
        refill_zones=refill_zones,
        report_all_track_errors=False,
        test_footprints=False
    )
    result = {
        'status': 'success',
        'error_count': errors,
        'warning_count': warnings,
        'exclusion_count': exclusions
    }
    if output_format in ("detailed", "by_type"):
        violations = board.drc.get_violations()
        viol_list = []
        for v in violations:
            viol_list.append({
                'error_type': v.error_type,
                'message': v.message,
                'severity': v.severity,
                'position': [v.position.x / 1000000, v.position.y / 1000000] if v.position else None
            })
        result['violations'] = viol_list
    print(json.dumps(result, indent=2))
except Exception as e:
    print(json.dumps({'status': 'error', 'message': str(e)}, indent=2))
