import json

refresh_or_fail(mbs)

try:
    res = mbs.multi_board.sync_to_pcb()

    output = {
        'status': 'success',
        'sub_projects_touched': res.sub_projects_touched,
        'endpoints_applied':    res.endpoints_applied,
        'endpoints_missing':    res.endpoints_missing,
        'nets_renamed':         res.nets_renamed,
        'conflicts': [
            {'chosen': c.chosen, 'rejected': list(c.rejected)}
            for c in res.conflicts
        ],
        'summary': res.summary,
    }
except Exception as exc:
    import traceback
    output = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(output, indent=2))
