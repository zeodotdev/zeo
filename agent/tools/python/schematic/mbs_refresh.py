import json

refresh_or_fail(mbs)

try:
    res = mbs.multi_board.refresh_from_sub_projects()

    output = {
        'status': 'success',
        'blocks_added':   res.blocks_added,
        'blocks_removed': res.blocks_removed,
        'pins_added':     res.pins_added,
        'pins_removed':   res.pins_removed,
        'pins_renamed':   res.pins_renamed,
        'paths_updated':  res.paths_updated,
        'uuids_stamped':  res.uuids_stamped,
        'summary':        res.summary,
    }

    total = (res.blocks_added + res.blocks_removed + res.pins_added
             + res.pins_removed + res.pins_renamed + res.paths_updated
             + res.uuids_stamped)
    output['no_changes'] = (total == 0)
except Exception as exc:
    import traceback
    output = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(output, indent=2))
