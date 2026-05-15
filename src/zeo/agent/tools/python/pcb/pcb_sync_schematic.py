import json

delete_unused = TOOL_ARGS.get("delete_unused", False)
replace_footprints = TOOL_ARGS.get("replace_footprints", False)
update_fields = TOOL_ARGS.get("update_fields", True)
dry_run = TOOL_ARGS.get("dry_run", False)

# Update PCB from Schematic
try:
    result = board.update_from_schematic(
        delete_unused_footprints=delete_unused,
        replace_footprints=replace_footprints,
        update_fields=update_fields,
        dry_run=dry_run
    )

    # Only include warnings and errors — counts already summarize the rest
    messages = []
    for c in result.changes:
        if c.type not in (8, 9):  # CT_WARNING, CT_ERROR
            continue
        entry = {
            'severity': 'error' if c.type == 9 else 'warning',
            'message': c.message
        }
        if c.reference:
            entry['ref'] = c.reference
        messages.append(entry)

    output = {
        'footprints_added': result.footprints_added,
        'footprints_replaced': result.footprints_replaced,
        'footprints_deleted': result.footprints_deleted,
        'footprints_updated': result.footprints_updated,
        'nets_changed': result.nets_changed,
        'warnings': result.warnings,
        'errors': result.errors,
        'changes_applied': result.changes_applied,
        'messages': messages
    }
    print(json.dumps(output, indent=2))
except Exception as e:
    print(f'Error: {e}')
