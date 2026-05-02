"""mbs_setup — manage multi-board container projects.

Five actions covered by one tool:

  Rule sets (require an MBSCH editor open — route through IPC so the live
  PROJECT_FILE invalidates correctly and DRC picks up changes immediately):
    - get
    - set                  Replace one or more rule sets on the container .kicad_pro

  Container actions (operate directly on .kicad_pro JSON files; do NOT
  require an MBSCH editor open):
    - create_container     Create a new container .kicad_pro + empty .kicad_mbs
    - add_sub_project      Register an existing sub-project's .kicad_pro with
                           the container; writes the back-reference into the
                           sub-project too
    - remove_sub_project   Unregister a sub-project; clears the back-reference

Container actions edit .kicad_pro files on disk directly. If any of those
files is currently loaded in an editor, the editor's autosave on close
will overwrite our edits — call open_editor action="close" first when
needed.
"""
import json
import os
import traceback
import uuid as _uuid


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _read_pro(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def _write_pro(path, data):
    """Atomic write: tempfile + replace, so a crash mid-write doesn't
    corrupt a project file."""
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)
        f.write('\n')
    os.replace(tmp, path)


def _ensure_pro_path(path, must_exist=True):
    if not path:
        raise ValueError("path is required")

    abs_path = os.path.abspath(path)

    if not abs_path.endswith('.kicad_pro'):
        raise ValueError(f".kicad_pro path required; got {abs_path}")

    if must_exist and not os.path.exists(abs_path):
        raise FileNotFoundError(f"file not found: {abs_path}")

    return abs_path


def _relative_path(from_path, to_path):
    """Compute a relative path from from_path's directory to to_path."""
    return os.path.relpath(to_path, os.path.dirname(from_path))


def _multi_board(data):
    """Return data['multi_board'], creating it if absent."""
    return data.setdefault('multi_board', {})


def _new_uuid():
    return str(_uuid.uuid4())


_MBS_TEMPLATE = """(kicad_sch
\t(version 20250610)
\t(generator "zeo")
\t(generator_version "9.99")
\t(uuid "{uuid}")
\t(paper "A3")
\t(title_block
\t\t(title "{title}")
\t)
\t(lib_symbols)
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
)
"""


def _write_mbs_template(mbs_abs_path, container_name):
    title = f"{container_name} - Multi-Board Schematic"
    content = _MBS_TEMPLATE.format(uuid=_new_uuid(), title=title)
    with open(mbs_abs_path, 'w', encoding='utf-8') as f:
        f.write(content)


# Minimal seed for a fresh container .kicad_pro. We only fill the keys
# required for KiCad to load the file as a container; everything else
# falls back to PROJECT_FILE defaults on first open.
def _seed_container_pro(name, mbs_filename):
    return {
        "meta": {
            "filename": f"{name}.kicad_pro",
            "version": 3,
        },
        "multi_board": {
            "container": True,
            "mbs_file": mbs_filename,
            "sub_projects": [],
            "container_project_relative_path": "",
            "cross_board_diff_pairs": [],
            "cross_board_nets": [],
            "min_power_pins": {},
            "max_length_nm": {},
            "current_rules": {},
            "voltage_rules": {},
        },
    }


# ---------------------------------------------------------------------------
# Action: rules (get/set)
# ---------------------------------------------------------------------------

def _block_to_minpin_dict(rules):
    return [{'net_name': r.net_name, 'min_pins': r.min_pins}
            for r in rules.min_power_pins]


def _block_to_maxlen_dict(rules):
    return [{'net_name': r.net_name, 'max_length_nm': r.max_length_nm}
            for r in rules.max_length_nm]


def _block_to_diffpair_dict(rules):
    return [{'p': r.p, 'n': r.n} for r in rules.cross_board_diff_pairs]


def _block_to_current_dict(rules):
    return [{'net_name': r.net_name,
             'expected_amps': r.expected_amps,
             'pin_rating_amps': r.pin_rating_amps}
            for r in rules.current_rules]


def _block_to_voltage_dict(rules):
    return [{'net_name': r.net_name,
             'expected_amps': r.expected_amps,
             'max_drop_mv': r.max_drop_mv,
             'trace_width_um': r.trace_width_um,
             'trace_sheet_r_milliohm_per_sq': r.trace_sheet_r_milliohm_per_sq,
             'contact_r_per_pin_milliohm': r.contact_r_per_pin_milliohm}
            for r in rules.voltage_rules]


def _do_get_rules():
    mbs = kicad.get_mbs_schematic()
    response = mbs.multi_board.get_rules()
    rules = response.rules
    return {
        'status': 'success',
        'action': 'get',
        'rules': {
            'min_power_pins': _block_to_minpin_dict(rules),
            'max_length_nm': _block_to_maxlen_dict(rules),
            'cross_board_diff_pairs': _block_to_diffpair_dict(rules),
            'current_rules': _block_to_current_dict(rules),
            'voltage_rules': _block_to_voltage_dict(rules),
        },
    }


def _do_set_rules():
    rules_in = TOOL_ARGS.get('rules', {}) or {}
    kwargs = {}
    for key in ('min_power_pins', 'max_length_nm',
                'cross_board_diff_pairs', 'current_rules',
                'voltage_rules'):
        if key in rules_in:
            kwargs[key] = rules_in[key]

    if not kwargs:
        return {
            'status': 'error',
            'action': 'set',
            'message': "action='set' requires at least one rule set under "
                       "the 'rules' object: min_power_pins, max_length_nm, "
                       "cross_board_diff_pairs, current_rules, voltage_rules. "
                       "Pass an empty list to clear a set.",
        }

    mbs = kicad.get_mbs_schematic()
    updated = mbs.multi_board.set_rules(**kwargs)
    return {
        'status': 'success',
        'action': 'set',
        'updated': bool(updated),
        'replaced_sets': sorted(kwargs.keys()),
    }


# ---------------------------------------------------------------------------
# Action: create_container
# ---------------------------------------------------------------------------

def _do_create_container():
    target = TOOL_ARGS.get('container_pro_path', '')
    mbs_filename = TOOL_ARGS.get('mbs_file_name', '')

    if not target:
        raise ValueError("container_pro_path is required for create_container")

    abs_pro = _ensure_pro_path(target, must_exist=False)

    if os.path.exists(abs_pro):
        return {
            'status': 'error',
            'action': 'create_container',
            'message': f"refusing to overwrite existing file: {abs_pro}",
        }

    container_dir = os.path.dirname(abs_pro)
    name = os.path.splitext(os.path.basename(abs_pro))[0]

    if not mbs_filename:
        mbs_filename = f"{name}.kicad_mbs"

    if not mbs_filename.endswith('.kicad_mbs'):
        raise ValueError(f"mbs_file_name must end with .kicad_mbs; got {mbs_filename}")

    abs_mbs = os.path.join(container_dir, mbs_filename)

    if os.path.exists(abs_mbs):
        return {
            'status': 'error',
            'action': 'create_container',
            'message': f"refusing to overwrite existing MBS file: {abs_mbs}",
        }

    os.makedirs(container_dir, exist_ok=True)
    _write_pro(abs_pro, _seed_container_pro(name, mbs_filename))
    _write_mbs_template(abs_mbs, name)

    return {
        'status': 'success',
        'action': 'create_container',
        'container_pro_path': abs_pro,
        'mbs_file_path': abs_mbs,
    }


# ---------------------------------------------------------------------------
# Action: add_sub_project
# ---------------------------------------------------------------------------

def _do_add_sub_project():
    container_path = TOOL_ARGS.get('container_pro_path', '')
    sub_path = TOOL_ARGS.get('sub_pro_path', '')

    if not container_path or not sub_path:
        raise ValueError("container_pro_path AND sub_pro_path are required")

    abs_container = _ensure_pro_path(container_path, must_exist=True)
    abs_sub = _ensure_pro_path(sub_path, must_exist=True)

    name = TOOL_ARGS.get('name') or os.path.splitext(os.path.basename(abs_sub))[0]
    display_name = TOOL_ARGS.get('display_name') or name
    role = TOOL_ARGS.get('role', 'standard')

    container_data = _read_pro(abs_container)
    mb = _multi_board(container_data)

    if not mb.get('container', False):
        return {
            'status': 'error',
            'action': 'add_sub_project',
            'message': f"{abs_container} is not a multi-board container "
                       f"(multi_board.container is not true). "
                       f"Use action='create_container' first or fix the file.",
        }

    sub_projects = mb.setdefault('sub_projects', [])
    rel_path_in_container = _relative_path(abs_container, abs_sub)

    # Reject duplicate by path OR by uuid (if the sub already has one).
    sub_data = _read_pro(abs_sub)
    sub_mb = sub_data.get('multi_board', {})
    existing_uuid = (sub_mb.get('container_project_relative_path') and None)  # informational

    for entry in sub_projects:
        if entry.get('path') == rel_path_in_container:
            return {
                'status': 'error',
                'action': 'add_sub_project',
                'message': f"sub-project already registered at "
                           f"path '{rel_path_in_container}' "
                           f"(uuid={entry.get('uuid')}).",
            }

    new_uuid = TOOL_ARGS.get('uuid') or _new_uuid()
    sub_projects.append({
        'uuid': new_uuid,
        'name': name,
        'path': rel_path_in_container,
        'display_name': display_name,
        'role': role,
    })

    _write_pro(abs_container, container_data)

    # Write back-reference into the sub-project so container detection is O(1).
    sub_mb_w = _multi_board(sub_data)
    sub_mb_w['container_project_relative_path'] = _relative_path(abs_sub, abs_container)
    sub_mb_w.setdefault('container', False)
    _write_pro(abs_sub, sub_data)

    return {
        'status': 'success',
        'action': 'add_sub_project',
        'sub_project_uuid': new_uuid,
        'name': name,
        'relative_path': rel_path_in_container,
        'container_back_reference': sub_mb_w['container_project_relative_path'],
    }


# ---------------------------------------------------------------------------
# Action: remove_sub_project
# ---------------------------------------------------------------------------

def _do_remove_sub_project():
    container_path = TOOL_ARGS.get('container_pro_path', '')
    target_uuid = TOOL_ARGS.get('sub_project_uuid', '')
    target_path_arg = TOOL_ARGS.get('sub_pro_path', '')

    if not container_path:
        raise ValueError("container_pro_path is required")

    if not target_uuid and not target_path_arg:
        raise ValueError("sub_project_uuid OR sub_pro_path is required")

    abs_container = _ensure_pro_path(container_path, must_exist=True)
    container_data = _read_pro(abs_container)
    mb = _multi_board(container_data)

    if not mb.get('container', False):
        return {
            'status': 'error',
            'action': 'remove_sub_project',
            'message': f"{abs_container} is not a multi-board container.",
        }

    sub_projects = mb.setdefault('sub_projects', [])
    container_dir = os.path.dirname(abs_container)

    rel_path_match = None
    if target_path_arg:
        abs_sub = os.path.abspath(target_path_arg)
        rel_path_match = os.path.relpath(abs_sub, container_dir)

    removed = None
    kept = []
    for entry in sub_projects:
        if (target_uuid and entry.get('uuid') == target_uuid) \
                or (rel_path_match and entry.get('path') == rel_path_match):
            removed = entry
        else:
            kept.append(entry)

    if not removed:
        return {
            'status': 'error',
            'action': 'remove_sub_project',
            'message': f"sub-project not found "
                       f"(uuid='{target_uuid}', path='{target_path_arg}').",
        }

    mb['sub_projects'] = kept
    _write_pro(abs_container, container_data)

    # Clear back-reference in the sub-project's own .kicad_pro (best-effort;
    # the file may have been moved out from under us).
    abs_sub = os.path.abspath(os.path.join(container_dir, removed['path']))
    back_ref_cleared = False
    if os.path.exists(abs_sub):
        sub_data = _read_pro(abs_sub)
        sub_mb = _multi_board(sub_data)
        if sub_mb.get('container_project_relative_path'):
            sub_mb['container_project_relative_path'] = ''
            _write_pro(abs_sub, sub_data)
            back_ref_cleared = True

    return {
        'status': 'success',
        'action': 'remove_sub_project',
        'sub_project_uuid': removed['uuid'],
        'sub_pro_path': abs_sub,
        'back_reference_cleared': back_ref_cleared,
    }


# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------

action = TOOL_ARGS.get('action', 'get')
_handlers = {
    'get': _do_get_rules,
    'set': _do_set_rules,
    'create_container': _do_create_container,
    'add_sub_project': _do_add_sub_project,
    'remove_sub_project': _do_remove_sub_project,
}

try:
    handler = _handlers.get(action)
    if handler is None:
        result = {
            'status': 'error',
            'message': (f"Unknown action '{action}'. Valid: "
                        f"{sorted(_handlers.keys())}."),
        }
    else:
        result = handler()
except Exception as exc:
    result = {
        'status': 'error',
        'action': action,
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(result, indent=2))
