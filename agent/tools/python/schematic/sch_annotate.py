import json

# Parse input parameters
scope = TOOL_ARGS.get("scope", "all")
sort_by = TOOL_ARGS.get("sort_by", "x_position")
reset_existing = TOOL_ARGS.get("reset_existing", False)

# Map scope parameter to kipy API values
if scope == "unannotated_only":
    kipy_scope = "all"  # annotate all, but don't reset existing
elif scope == "current_sheet":
    kipy_scope = "current_sheet"
elif scope == "selection":
    kipy_scope = "selection"
else:
    kipy_scope = "all"

# Map sort_by to kipy order parameter
if sort_by == "y_position":
    kipy_order = "y_x"
else:
    kipy_order = "x_y"

refresh_or_fail(sch)

try:
    # Get symbols before annotation for comparison
    symbols_before = {}
    for sym in sch.symbols.get_all():
        ref = getattr(sym, 'reference', '?')
        sym_id = str(sym.id.value) if hasattr(sym, 'id') and hasattr(sym.id, 'value') else str(getattr(sym, 'id', ''))
        symbols_before[sym_id] = ref

    # Call the annotation API via sch.erc.annotate()
    # kipy API: annotate(scope, order, algorithm, start_number, reset_existing, recursive)
    response = sch.erc.annotate(
        scope=kipy_scope,
        order=kipy_order,
        algorithm='incremental',
        start_number=1,
        reset_existing=reset_existing or scope == "all",
        recursive=True
    )

    # Get symbols after annotation to show changes
    annotated = []
    for sym in sch.symbols.get_all():
        sym_id = str(sym.id.value) if hasattr(sym, 'id') and hasattr(sym.id, 'value') else str(getattr(sym, 'id', ''))
        new_ref = getattr(sym, 'reference', '?')
        old_ref = symbols_before.get(sym_id, '?')
        if old_ref != new_ref:
            annotated.append({'uuid': sym_id, 'old_ref': old_ref, 'new_ref': new_ref})

    # Get count from response if available
    count = getattr(response, 'symbols_annotated', len(annotated))

    result = {
        'status': 'success',
        'scope': scope,
        'sort_by': sort_by,
        'symbols_annotated': count,
        'changes': annotated
    }
    print(json.dumps(result, indent=2))

except Exception as e:
    import traceback
    print(json.dumps({'status': 'error', 'message': str(e), 'traceback': traceback.format_exc()}))
