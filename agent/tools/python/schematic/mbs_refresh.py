import json

refresh_or_fail(mbs)

# Default behavior: PREVIEW. Caller must explicitly pass apply=True to commit.
# Mirrors the desktop "Refresh from Sub-Projects" dialog: see what would
# change first, optionally filter, then approve.
apply = bool(TOOL_ARGS.get('apply', False))
apply_indices = TOOL_ARGS.get('apply_indices', None)

try:
    res = mbs.multi_board.refresh_from_sub_projects(
        dry_run=not apply,
        apply_indices=apply_indices,
    )

    proposed = []
    for c in res.proposed_changes:
        entry = {
            'index': c.index,
            'kind': c.kind,
            'description': c.description,
            'sub_project_name': c.sub_project_name,
            'component_ref': c.component_ref,
        }
        if c.pin_number:
            entry['pin_number'] = c.pin_number
        if c.old_label:
            entry['old_label'] = c.old_label
        if c.new_label:
            entry['new_label'] = c.new_label
        proposed.append(entry)

    output = {
        'status': 'success',
        'mode': 'preview' if res.dry_run else 'apply',
        'proposed_changes': proposed,
        'change_count': len(proposed),
    }

    if not res.dry_run:
        output.update({
            'blocks_added':   res.blocks_added,
            'blocks_removed': res.blocks_removed,
            'pins_added':     res.pins_added,
            'pins_removed':   res.pins_removed,
            'pins_renamed':   res.pins_renamed,
            'paths_updated':  res.paths_updated,
            'uuids_stamped':  res.uuids_stamped,
            'summary':        res.summary,
        })

        total = (res.blocks_added + res.blocks_removed + res.pins_added
                 + res.pins_removed + res.pins_renamed + res.paths_updated
                 + res.uuids_stamped)
        output['no_changes'] = (total == 0)
    else:
        output['summary'] = res.summary
        output['hint'] = ('Review the proposed_changes list with the user. '
                          'To commit, call mbs_refresh again with apply=true. '
                          'To accept only some changes, pass apply=true plus '
                          'apply_indices=[0,2,5,...] with the indices to apply.')
except Exception as exc:
    import traceback
    output = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(output, indent=2))
