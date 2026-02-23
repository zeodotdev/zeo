import json

refresh_or_fail(sch)

action = TOOL_ARGS.get("action", "get")

if action == "get":
    # === GET MODE ===
    result = {'status': 'success'}

    # Get page settings
    page = sch.page.get_settings()
    result['page'] = {
        'size_type': page.size_type,
        'size_type_name': page.size_type_name,
        'portrait': page.portrait,
        'width_mm': page.width_mm,
        'height_mm': page.height_mm
    }

    # Get title block
    tb = sch.page.get_title_block()
    result['title_block'] = {
        'title': tb.title,
        'date': tb.date,
        'revision': tb.revision,
        'company': tb.company,
        'comments': tb.comments
    }

    # Get grid settings
    result['grid'] = sch.page.get_grid_settings()

    # Get editor preferences (display settings)
    result['editor'] = sch.page.get_editor_preferences()

    # Get formatting settings (project-level from Schematic Setup)
    result['formatting'] = sch.page.get_formatting_settings()

    # Get ERC settings
    result['erc'] = sch.erc.get_settings()

    # Get annotation settings (project-level from Schematic Setup)
    result['annotation'] = sch.page.get_annotation_settings()

    # Get field name templates (project-level from Schematic Setup)
    result['field_name_templates'] = sch.page.get_field_name_templates()

    # Get pin conflict map (ERC pin-to-pin conflict settings)
    result['pin_conflict_map'] = sch.erc.get_pin_type_matrix()

    # Get net classes
    result['net_classes'] = sch.netclass.get_all()

    # Get net class assignments
    result['net_class_assignments'] = sch.netclass.get_assignments()

    # Get bus aliases
    result['bus_aliases'] = sch.bus_alias.get_all()

    # Get text variables (project-level)
    project = sch.get_project()
    text_vars = project.get_text_variables()
    result['text_variables'] = dict(text_vars.variables)

    print(json.dumps(result, indent=2, default=str))

else:
    # === SET MODE ===
    # Page size type mapping
    SIZE_MAP = {
        'A5': 1, 'A4': 2, 'A3': 3, 'A2': 4, 'A1': 5, 'A0': 6,
        'A': 7, 'B': 8, 'C': 9, 'D': 10, 'E': 11,
        'GERBER': 12, 'USLetter': 13, 'USLegal': 14, 'USLedger': 15, 'USER': 16
    }

    # Handle page settings
    if 'page' in TOOL_ARGS:
        page = TOOL_ARGS['page']
        kwargs = {}
        if 'size' in page:
            kwargs['size_type'] = SIZE_MAP.get(page['size'], 2)
        if 'portrait' in page:
            kwargs['portrait'] = page['portrait']
        if 'width_mm' in page:
            kwargs['width_mm'] = page['width_mm']
        if 'height_mm' in page:
            kwargs['height_mm'] = page['height_mm']
        sch.page.set_settings(**kwargs)

    # Handle title block settings
    if 'title_block' in TOOL_ARGS:
        tb = TOOL_ARGS['title_block']
        kwargs = {}
        if 'title' in tb:
            kwargs['title'] = tb['title']
        if 'date' in tb:
            kwargs['date'] = tb['date']
        if 'revision' in tb:
            kwargs['revision'] = tb['revision']
        if 'company' in tb:
            kwargs['company'] = tb['company']
        if 'comments' in tb:
            comments = tb['comments']
            kwargs['comments'] = {int(k.replace('comment', '')): v for k, v in comments.items() if k.startswith('comment')}
        sch.page.set_title_block(**kwargs)

    # Handle grid settings
    if 'grid' in TOOL_ARGS:
        grid = TOOL_ARGS['grid']
        kwargs = {}
        if 'size_mm' in grid:
            kwargs['grid_mm'] = grid['size_mm']
        if 'size_mils' in grid:
            kwargs['grid_mils'] = grid['size_mils']
        if 'visible' in grid:
            kwargs['visible'] = grid['visible']
        if 'snap' in grid:
            kwargs['snap_to_grid'] = grid['snap']
        sch.page.set_grid(**kwargs)

    # Handle formatting settings (the comprehensive settings)
    if 'formatting' in TOOL_ARGS:
        fmt = TOOL_ARGS['formatting']
        kwargs = {}

        # Text section
        if 'text' in fmt:
            text = fmt['text']
            if 'default_text_size_mils' in text:
                kwargs['default_text_size_mils'] = text['default_text_size_mils']
            if 'overbar_offset_ratio' in text:
                kwargs['overbar_offset_ratio'] = text['overbar_offset_ratio']
            if 'label_offset_ratio' in text:
                kwargs['label_offset_ratio'] = text['label_offset_ratio']
            if 'global_label_margin_ratio' in text:
                kwargs['global_label_margin_ratio'] = text['global_label_margin_ratio']

        # Symbols section
        if 'symbols' in fmt:
            sym = fmt['symbols']
            if 'default_line_width_mils' in sym:
                kwargs['default_line_width_mils'] = sym['default_line_width_mils']
            if 'pin_symbol_size_mils' in sym:
                kwargs['pin_symbol_size_mils'] = sym['pin_symbol_size_mils']

        # Connections section
        if 'connections' in fmt:
            conn = fmt['connections']
            if 'junction_size_choice' in conn:
                kwargs['junction_size_choice'] = conn['junction_size_choice']
            if 'hop_over_size_choice' in conn:
                kwargs['hop_over_size_choice'] = conn['hop_over_size_choice']
            if 'connection_grid_mils' in conn:
                kwargs['connection_grid_mils'] = conn['connection_grid_mils']

        # Inter-sheet references section
        if 'intersheet_refs' in fmt:
            isr = fmt['intersheet_refs']
            if 'show' in isr:
                kwargs['intersheet_refs_show'] = isr['show']
            if 'list_own_page' in isr:
                kwargs['intersheet_refs_list_own_page'] = isr['list_own_page']
            if 'format_short' in isr:
                kwargs['intersheet_refs_format_short'] = isr['format_short']
            if 'prefix' in isr:
                kwargs['intersheet_refs_prefix'] = isr['prefix']
            if 'suffix' in isr:
                kwargs['intersheet_refs_suffix'] = isr['suffix']

        # Dashed lines section
        if 'dashed_lines' in fmt:
            dl = fmt['dashed_lines']
            if 'dash_ratio' in dl:
                kwargs['dashed_line_dash_ratio'] = dl['dash_ratio']
            if 'gap_ratio' in dl:
                kwargs['dashed_line_gap_ratio'] = dl['gap_ratio']

        # Operating-point overlay section
        if 'opo' in fmt:
            opo = fmt['opo']
            if 'voltage_precision' in opo:
                kwargs['opo_voltage_precision'] = opo['voltage_precision']
            if 'voltage_range' in opo:
                kwargs['opo_voltage_range'] = opo['voltage_range']
            if 'current_precision' in opo:
                kwargs['opo_current_precision'] = opo['current_precision']
            if 'current_range' in opo:
                kwargs['opo_current_range'] = opo['current_range']

        sch.page.set_formatting_settings(**kwargs)

    # Handle ERC settings
    if 'erc' in TOOL_ARGS:
        erc = TOOL_ARGS['erc']
        if 'rule_severities' in erc:
            sch.erc.set_settings(rule_severities=erc['rule_severities'])

    # Handle annotation settings
    if 'annotation' in TOOL_ARGS:
        ann = TOOL_ARGS['annotation']
        kwargs = {}

        # Units section
        if 'units' in ann:
            units = ann['units']
            if 'symbol_unit_notation' in units:
                kwargs['symbol_unit_notation'] = units['symbol_unit_notation']

        # Order section
        if 'order' in ann:
            order = ann['order']
            if 'sort_order' in order:
                kwargs['sort_order'] = order['sort_order']

        # Numbering section
        if 'numbering' in ann:
            num = ann['numbering']
            if 'method' in num:
                kwargs['numbering_method'] = num['method']
            if 'start_number' in num:
                kwargs['start_number'] = num['start_number']
            if 'allow_reference_reuse' in num:
                kwargs['allow_reference_reuse'] = num['allow_reference_reuse']

        sch.page.set_annotation_settings(**kwargs)

    # Handle field name templates
    if 'field_name_templates' in TOOL_ARGS:
        templates = TOOL_ARGS['field_name_templates']
        template_list = []
        for tmpl in templates:
            if 'name' not in tmpl:
                continue
            entry = {
                'name': tmpl['name'],
                'visible': tmpl.get('visible', False),
                'url': tmpl.get('url', False)
            }
            template_list.append(entry)
        sch.page.set_field_name_templates(template_list)

    # Handle pin conflict map settings
    if 'pin_conflict_map' in TOOL_ARGS:
        pcm = TOOL_ARGS['pin_conflict_map']

        # Check for reset_to_defaults
        if pcm.get('reset_to_defaults', False):
            sch.erc.set_pin_type_matrix(reset_to_defaults=True)

        # Handle individual entry updates
        if 'entries' in pcm:
            entries = pcm['entries']
            entry_dict = {}
            for entry in entries:
                if 'first_pin_type' in entry and 'second_pin_type' in entry and 'error_type' in entry:
                    entry_dict[(entry['first_pin_type'], entry['second_pin_type'])] = entry['error_type']
            sch.erc.set_pin_type_matrix(entries=entry_dict)

    # Handle net classes
    if 'net_classes' in TOOL_ARGS:
        nc = TOOL_ARGS['net_classes']

        # Handle deletions first
        if 'delete' in nc:
            for name in nc['delete']:
                sch.netclass.delete(name)

        # Handle creates
        if 'create' in nc:
            for netclass in nc['create']:
                if 'name' not in netclass:
                    continue
                kwargs = {'name': netclass['name']}
                for key in ('wire_width_mils', 'bus_width_mils', 'color', 'line_style', 'description', 'priority'):
                    if key in netclass:
                        kwargs[key] = netclass[key]
                sch.netclass.create(**kwargs)

        # Handle updates
        if 'update' in nc:
            for netclass in nc['update']:
                if 'name' not in netclass:
                    continue
                kwargs = {'name': netclass['name']}
                for key in ('wire_width_mils', 'bus_width_mils', 'color', 'line_style', 'description', 'priority'):
                    if key in netclass:
                        kwargs[key] = netclass[key]
                sch.netclass.update(**kwargs)

    # Handle net class assignments
    if 'net_class_assignments' in TOOL_ARGS:
        nca = TOOL_ARGS['net_class_assignments']

        # Check for replace_all flag - replaces all assignments
        if 'replace_all' in nca:
            assignments = []
            for a in nca['replace_all']:
                if 'pattern' in a and 'netclass' in a:
                    assignments.append({'pattern': a['pattern'], 'netclass': a['netclass']})
            sch.netclass.set_assignments(assignments)

        # Handle add assignments
        if 'add' in nca:
            for a in nca['add']:
                if 'pattern' in a and 'netclass' in a:
                    sch.netclass.add_assignment(a['pattern'], a['netclass'])

        # Handle remove assignments
        if 'remove' in nca:
            for pattern in nca['remove']:
                sch.netclass.remove_assignment(pattern)

    # Handle bus aliases
    if 'bus_aliases' in TOOL_ARGS:
        ba = TOOL_ARGS['bus_aliases']

        # Handle deletions first
        if 'delete' in ba:
            for name in ba['delete']:
                sch.bus_alias.delete(name)

        # Handle replace_all - replaces all bus aliases at once
        if 'replace_all' in ba:
            alias_list = []
            for alias in ba['replace_all']:
                if 'name' not in alias:
                    continue
                alias_list.append({'name': alias['name'], 'members': alias.get('members', [])})
            sch.bus_alias.set_all(alias_list)

        # Handle creates
        if 'create' in ba:
            for alias in ba['create']:
                if 'name' not in alias:
                    continue
                sch.bus_alias.create(alias['name'], alias.get('members', []))

        # Handle updates
        if 'update' in ba:
            for alias in ba['update']:
                if 'name' not in alias:
                    continue
                sch.bus_alias.update(alias['name'], alias.get('members', []))

    # Handle text variables
    if 'text_variables' in TOOL_ARGS:
        tv = TOOL_ARGS['text_variables']

        project = sch.get_project()
        from kipy.project_types import TextVariables
        from kipy.proto.common.types.base_types_pb2 import MapMergeMode

        # Handle replace_all - replaces all text variables
        if 'replace_all' in tv:
            new_vars = TextVariables()
            new_vars.variables = tv['replace_all']
            project.set_text_variables(new_vars, MapMergeMode.MMM_REPLACE)

        # Handle set - merges with existing variables
        if 'set' in tv:
            merge_vars = TextVariables()
            merge_vars.variables = tv['set']
            project.set_text_variables(merge_vars, MapMergeMode.MMM_MERGE)

        # Handle delete - remove specific variables
        if 'delete' in tv:
            current_vars = project.get_text_variables()
            updated = dict(current_vars.variables)
            for name in tv['delete']:
                updated.pop(name, None)
            new_vars = TextVariables()
            new_vars.variables = updated
            project.set_text_variables(new_vars, MapMergeMode.MMM_REPLACE)

    print(json.dumps({'status': 'success', 'message': 'Settings updated'}))
