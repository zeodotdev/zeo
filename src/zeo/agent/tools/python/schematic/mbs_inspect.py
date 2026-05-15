import json
import re
import traceback

refresh_or_fail(mbs)

section = TOOL_ARGS.get('section', 'all')
filter_arg = TOOL_ARGS.get('filter', '').strip()


def _scale_pos(pos):
    if pos is None:
        return [0, 0]
    return [round(pos.x_nm / 1_000_000, 4), round(pos.y_nm / 1_000_000, 4)]


def _matches_filter(value):
    """Glob-style filter (e.g. 'B*', 'J1', 'GND*'). Empty filter accepts all."""
    if not filter_arg:
        return True
    pattern = '^' + re.escape(filter_arg).replace(r'\*', '.*').replace(r'\?', '.') + '$'
    try:
        return bool(re.match(pattern, value or '', re.IGNORECASE))
    except re.error:
        return False


def _pin_to_dict(pin):
    return {
        'pin_number': pin.pin_number,
        'component_ref': pin.component_ref,
        'text': pin.text,
        'position_mm': _scale_pos(pin.position),
        'side': int(pin.side),
        'electrical_type': int(pin.electrical_type),
    }


def _block_full(block):
    return {
        'id': block.id.value,
        'mbs_reference': block.mbs_reference,
        'component_ref': block.component_ref,
        'sub_project_uuid': block.sub_project_uuid,
        'sub_project_path': block.sub_project_path,
        'display_name': block.display_name,
        'position_mm': _scale_pos(block.position),
        'size_mm': _scale_pos(block.size),
        'pins': [_pin_to_dict(p) for p in block.pins],
    }


def _net_full(net):
    return {
        'name': net.name,
        'endpoints': [
            {
                'sub_project_uuid': ep.sub_project_uuid,
                'component_ref': ep.component_ref,
                'pin_number': ep.pin_number,
            }
            for ep in net.endpoints
        ],
    }


def _container_full(info):
    return {
        'container_name': info.container_name,
        'container_pro_path': info.container_pro_path,
        'mbs_file_path': info.mbs_file_path,
        'sub_projects': [
            {
                'uuid': sp.uuid,
                'name': sp.name,
                'relative_path': sp.relative_path,
                'absolute_path': sp.absolute_path,
            }
            for sp in info.sub_projects
        ],
    }


valid_sections = {'blocks', 'nets', 'container', 'all'}
if section not in valid_sections:
    print(json.dumps({
        'status': 'error',
        'message': f"Unknown section '{section}'. Valid: {sorted(valid_sections)}",
    }, indent=2))
else:
    try:
        result = {'status': 'success', 'section': section}

        if section in ('blocks', 'all'):
            blocks = mbs.multi_board.get_blocks()
            filtered = [
                _block_full(b)
                for b in blocks
                if _matches_filter(b.mbs_reference) or _matches_filter(b.component_ref)
            ]
            result['blocks'] = filtered

        if section in ('nets', 'all'):
            nets = mbs.multi_board.get_cross_board_nets()
            filtered = [_net_full(n) for n in nets if _matches_filter(n.name)]
            result['cross_board_nets'] = filtered

        if section in ('container', 'all'):
            result['container'] = _container_full(mbs.multi_board.get_container_info())

    except Exception as exc:
        result = {
            'status': 'error',
            'message': str(exc) or repr(exc),
            'exception_type': type(exc).__name__,
            'traceback': traceback.format_exc(),
            'section': section,
        }

    print(json.dumps(result, indent=2))
