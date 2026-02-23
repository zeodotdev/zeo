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

    output = {
        'footprints_added': result.footprints_added,
        'footprints_replaced': result.footprints_replaced,
        'footprints_deleted': result.footprints_deleted,
        'footprints_updated': result.footprints_updated,
        'nets_changed': result.nets_changed,
        'warnings': result.warnings,
        'errors': result.errors,
        'changes_applied': result.changes_applied
    }
    print(json.dumps(output, indent=2))
except Exception as e:
    print(f'Error: {e}')
