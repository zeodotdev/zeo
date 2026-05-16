# Copyright (C) 2026, Zeo <team@zeo.dev>

import json
import traceback

refresh_or_fail(mbs)


def _scale_pos(pos):
    """Convert a raw proto Vector2 (x_nm, y_nm in nm) to (x, y) mm rounded to 4 decimals."""
    if pos is None:
        return [0, 0]
    return [round(pos.x_nm / 1_000_000, 4), round(pos.y_nm / 1_000_000, 4)]


def _block_to_dict(block):
    return {
        'mbs_reference': block.mbs_reference,
        'component_ref': block.component_ref,
        'sub_project_uuid': block.sub_project_uuid,
        'sub_project_path': block.sub_project_path,
        'display_name': block.display_name,
        'position_mm': _scale_pos(block.position),
        'size_mm': _scale_pos(block.size),
        'pin_count': len(list(block.pins)),
    }


def _net_to_dict(net):
    return {
        'name': net.name,
        'endpoint_count': len(list(net.endpoints)),
        'sub_projects': sorted({ep.sub_project_uuid for ep in net.endpoints}),
    }


def _sub_project_to_dict(sp):
    return {
        'uuid': sp.uuid,
        'name': sp.name,
        'relative_path': sp.relative_path,
        'absolute_path': sp.absolute_path,
    }


try:
    container = mbs.multi_board.get_container_info()
    blocks = mbs.multi_board.get_blocks()
    nets = mbs.multi_board.get_cross_board_nets()

    result = {
        'status': 'success',
        'container': {
            'name': container.container_name,
            'pro_path': container.container_pro_path,
            'mbs_file_path': container.mbs_file_path,
            'sub_project_count': len(list(container.sub_projects)),
            'sub_projects': [_sub_project_to_dict(sp) for sp in container.sub_projects],
        },
        'blocks': {
            'count': len(blocks),
            'items': [_block_to_dict(b) for b in blocks],
        },
        'cross_board_nets': {
            'count': len(nets),
            'items': [_net_to_dict(n) for n in nets],
        },
    }
except Exception as exc:
    result = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(result, indent=2))
