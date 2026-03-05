import json, sys

refresh_or_fail(sch)

sheet_path = TOOL_ARGS.get("sheet_path", "")
result = None

try:
    # First try get_hierarchy which provides proper paths for navigation
    hierarchy_tree = None
    hierarchy_nodes = []  # Flattened list of (node, path_str) tuples

    def flatten_hierarchy(node, parent_path='', parent_human_path='/'):
        """Recursively flatten hierarchy tree into list of nodes with path strings."""
        nodes = []
        name = getattr(node, 'name', '') or ''
        uuid = ''
        if hasattr(node, 'path') and node.path:
            sp = node.path
            if hasattr(sp, 'path') and sp.path:
                uuid = sp.path[-1].value if sp.path else ''

        current_path = parent_path + '/' + uuid if uuid else parent_path
        if not current_path:
            current_path = '/'

        # Always build human_path from hierarchy structure (proto value is unreliable
        # for multi-root schematics where PathHumanReadable() can't resolve root names)
        if not name:
            human_readable = '/'
        else:
            p = parent_human_path if parent_human_path.endswith('/') else parent_human_path + '/'
            human_readable = p + name + '/'

        nodes.append((node, name, uuid, current_path, human_readable))

        if hasattr(node, 'children'):
            for child in node.children:
                nodes.extend(flatten_hierarchy(child, current_path, human_readable))
        return nodes

    if hasattr(sch.sheets, 'get_hierarchy'):
        try:
            hierarchy_tree = sch.sheets.get_hierarchy()
            hierarchy_nodes = flatten_hierarchy(hierarchy_tree)
            tool_log(f'[sch_switch_sheet] Hierarchy tree has {len(hierarchy_nodes)} nodes')
        except Exception as he:
            tool_log(f'[sch_switch_sheet] get_hierarchy failed: {he}')

    # Also get sheet items for fallback
    sheets = sch.crud.get_sheets()
    tool_log(f'[sch_switch_sheet] Found {len(sheets)} sheet items')

    # Build lookup dictionaries
    hierarchy = []
    sheet_by_name = {}   # name -> list of (node, info)
    sheet_by_file = {}
    sheet_by_uuid = {}

    # Prefer hierarchy nodes (they have proper paths for navigation)
    for node, name, uuid, path_str, human_path in hierarchy_nodes:
        # Skip virtual root node (empty name, no uuid)
        if not name and not uuid:
            continue
        filename = getattr(node, 'filename', '') or ''
        info = {'name': name, 'file': filename, 'sheet_path': human_path}
        hierarchy.append(info)
        if name:
            sheet_by_name.setdefault(name, []).append((node, info))
        if filename:
            sheet_by_file[filename] = (node, info)
        if uuid:
            sheet_by_uuid[uuid] = (node, info)

    # Add any sheet items not in hierarchy (fallback)
    for sheet in sheets:
        name = getattr(sheet, 'name', '')
        filename = getattr(sheet, 'filename', '')
        uuid = str(sheet.id.value) if hasattr(sheet, 'id') and hasattr(sheet.id, 'value') else str(getattr(sheet, 'id', getattr(sheet, 'uuid', '')))
        if uuid and uuid not in sheet_by_uuid:
            info = {'name': name, 'file': filename}
            hierarchy.append(info)
            if name:
                sheet_by_name.setdefault(name, []).append((sheet, info))
            if filename:
                sheet_by_file[filename] = (sheet, info)
            sheet_by_uuid[uuid] = (sheet, info)

    target_sheet = None
    target_info = None
    navigated = False

    # Handle sheet_path - can be '/' for root, or '/uuid1/uuid2' format, or sheet name
    if sheet_path:
        if sheet_path == '/':
            # Navigate to root sheet
            if hasattr(sch.sheets, 'navigate_to_root'):
                sch.sheets.navigate_to_root()
                navigated = True
            elif hasattr(sch.sheets, 'leave') and hasattr(sch.sheets, 'get_current_path'):
                # Leave all sheets until at root
                while True:
                    path = sch.sheets.get_current_path() if hasattr(sch.sheets, 'get_current_path') else '/'
                    if path == '/' or not hasattr(sch.sheets, 'leave'):
                        break
                    sch.sheets.leave()
                navigated = True
            target_info = {'name': 'Root', 'file': '', 'sheet_path': '/'}
        elif sheet_path.startswith('/'):
            # Path format - could be '/uuid1/uuid2' or '/name' or '/name/'
            parts = [p for p in sheet_path.split('/') if p]
            if parts:
                last_part = parts[-1]
                # Try as UUID first (full path format)
                if last_part in sheet_by_uuid:
                    target_sheet, target_info = sheet_by_uuid[last_part]
                # Try as sheet name (e.g., '/Power/')
                elif last_part in sheet_by_name:
                    matches = sheet_by_name[last_part]
                    if len(matches) > 1:
                        dupes = [m[1] for m in matches]
                        result = {'status': 'error', 'message': f"Ambiguous sheet name '{last_part}' matches {len(matches)} sheets. Use UUID to disambiguate.", 'matches': dupes, 'available_sheets': hierarchy}
                        print(json.dumps(result, indent=2))
                        sys.exit(0)
                    target_sheet, target_info = matches[0]
                # Try as filename (e.g., '/Power.kicad_sch/')
                elif last_part in sheet_by_file:
                    target_sheet, target_info = sheet_by_file[last_part]
                # Try adding .kicad_sch extension
                elif last_part + '.kicad_sch' in sheet_by_file:
                    target_sheet, target_info = sheet_by_file[last_part + '.kicad_sch']
        else:
            # Try as sheet name first
            if sheet_path in sheet_by_name:
                matches = sheet_by_name[sheet_path]
                if len(matches) > 1:
                    dupes = [m[1] for m in matches]
                    result = {'status': 'error', 'message': f"Ambiguous sheet name '{sheet_path}' matches {len(matches)} sheets. Use UUID to disambiguate.", 'matches': dupes, 'available_sheets': hierarchy}
                    print(json.dumps(result, indent=2))
                    sys.exit(0)
                target_sheet, target_info = matches[0]
            # Try as UUID
            elif sheet_path in sheet_by_uuid:
                target_sheet, target_info = sheet_by_uuid[sheet_path]
            # Try as filename
            elif sheet_path in sheet_by_file:
                target_sheet, target_info = sheet_by_file[sheet_path]
            # Try adding .kicad_sch extension
            elif sheet_path + '.kicad_sch' in sheet_by_file:
                target_sheet, target_info = sheet_by_file[sheet_path + '.kicad_sch']

    # Navigate to target sheet if found
    if target_sheet and not navigated:
        tool_log(f'[sch_switch_sheet] Attempting to navigate to sheet')
        # First try navigate_to with the SheetPath (most reliable for hierarchy nodes)
        if hasattr(sch.sheets, 'navigate_to') and hasattr(target_sheet, 'path') and target_sheet.path:
            try:
                tool_log(f'[sch_switch_sheet] Using navigate_to with path')
                sch.sheets.navigate_to(target_sheet.path)
                navigated = True
            except Exception as nav_err:
                tool_log(f'[sch_switch_sheet] navigate_to failed: {nav_err}')

        if not navigated and hasattr(sch.sheets, 'enter'):
            try:
                # enter might work with the node directly
                tool_log(f'[sch_switch_sheet] Trying enter method')
                sch.sheets.enter(target_sheet)
                navigated = True
            except Exception as enter_err:
                tool_log(f'[sch_switch_sheet] enter failed: {enter_err}')

        if not navigated and hasattr(sch.sheets, 'open'):
            try:
                tool_log(f'[sch_switch_sheet] Trying open method')
                sch.sheets.open(target_sheet)
                navigated = True
            except Exception as open_err:
                tool_log(f'[sch_switch_sheet] open failed: {open_err}')

    # Build result
    if navigated:
        result = {
            'status': 'success',
            'action': 'navigated',
            'target': target_info,
            'available_sheets': hierarchy
        }
    elif not sheet_path:
        # No target specified - list available sheets
        if not hierarchy:
            result = {
                'status': 'info',
                'message': 'Flat schematic with no hierarchical sheets.',
                'is_flat_design': True,
                'available_sheets': []
            }
        else:
            result = {
                'status': 'success',
                'message': 'Listing available sheets. Use sheet_path to navigate.',
                'available_sheets': hierarchy
            }
    else:
        # Target specified but not found
        result = {
            'status': 'error',
            'message': f'Sheet not found: {sheet_path}',
            'available_sheets': hierarchy
        }

except Exception as e:
    import traceback
    result = {'status': 'error', 'message': str(e), 'traceback': traceback.format_exc()}

print(json.dumps(result, indent=2))
