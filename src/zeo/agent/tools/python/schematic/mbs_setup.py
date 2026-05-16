# Copyright (C) 2026, Zeo <team@zeo.dev>

"""mbs_setup — manage multi-board container projects.

One tool, multiple actions covering the full container setup surface:

  get / set      Aggregate read / write across every container subsystem.
                 Both require an MBSCH editor open (route through IPC so
                 the live PROJECT_FILE invalidates correctly and DRC picks
                 up rule changes immediately). The 'set' action is partial
                 — provide only the section(s) you want to change. Sections:

                   rules:        cross-board DRC/ERC rule sets
                   net_classes:  container netclasses (auto-propagates to
                                 sub-projects via MultiBoardPropagateNetSettings)
                   libraries:    READ ONLY in this slice (Phase 3 will add
                                 add / share / delete verbs)

  create_container       Create a new container .kicad_pro + empty .kicad_mbs.
  add_sub_project        Register an existing sub-project with the container;
                         writes the back-reference into the sub-project too.
  remove_sub_project     Unregister a sub-project; clears the back-reference.

Container actions edit .kicad_pro JSON files directly. If any of those files
is currently loaded in an editor, call open_editor action='close' first to
avoid the editor's autosave clobbering the JSON edits.
"""
import json
import os
import traceback
import uuid as _uuid


# ---------------------------------------------------------------------------
# Helpers (file-system + JSON utilities)
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
# Get helpers — rules
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


def _get_rules_dict(mbs):
    rules = mbs.multi_board.get_rules().rules
    return {
        'min_power_pins':         _block_to_minpin_dict(rules),
        'max_length_nm':          _block_to_maxlen_dict(rules),
        'cross_board_diff_pairs': _block_to_diffpair_dict(rules),
        'current_rules':          _block_to_current_dict(rules),
        'voltage_rules':          _block_to_voltage_dict(rules),
    }


# ---------------------------------------------------------------------------
# Get helpers — net classes (Phase 1 inspection report)
# ---------------------------------------------------------------------------

# IU conversion: NETCLASS field bag is sent over the wire in raw IU. PCB-side
# fields (clearance, vias, diff pair) are surfaced in mm; SCH-side fields
# (wire/bus widths) are surfaced in mils — same convention as the existing
# net-class panel.
_PCB_IU_PER_MM = 1_000_000   # pcbIUScale
_SCH_IU_PER_MIL = 100        # schIUScale

_NET_CLASS_STATUS_LABELS = {
    0: 'unspecified',
    1: 'source',
    2: 'shared',
    3: 'local',
    4: 'conflict',
}


def _iu_to_mm(iu):
    return round(iu / _PCB_IU_PER_MM, 4) if iu is not None else None


def _iu_to_mils(iu):
    return round(iu / _SCH_IU_PER_MIL, 2) if iu is not None else None


def _opt(field_msg, name):
    if not field_msg.HasField(name):
        return None
    return getattr(field_msg, name)


def _nc_fields_to_dict(fields):
    return {
        'name':           fields.name,
        'priority':       fields.priority,
        'tuning_profile': fields.tuning_profile,
        'pcb_color':      fields.pcb_color_css or None,
        'schematic_color': fields.schematic_color_css or None,
        'clearance_mm':         _iu_to_mm(_opt(fields, 'clearance_iu')),
        'track_width_mm':       _iu_to_mm(_opt(fields, 'track_width_iu')),
        'via_diameter_mm':      _iu_to_mm(_opt(fields, 'via_diameter_iu')),
        'via_drill_mm':         _iu_to_mm(_opt(fields, 'via_drill_iu')),
        'uvia_diameter_mm':     _iu_to_mm(_opt(fields, 'uvia_diameter_iu')),
        'uvia_drill_mm':        _iu_to_mm(_opt(fields, 'uvia_drill_iu')),
        'diff_pair_width_mm':   _iu_to_mm(_opt(fields, 'diff_pair_width_iu')),
        'diff_pair_gap_mm':     _iu_to_mm(_opt(fields, 'diff_pair_gap_iu')),
        'diff_pair_via_gap_mm': _iu_to_mm(_opt(fields, 'diff_pair_via_gap_iu')),
        'wire_width_mils':      _iu_to_mils(_opt(fields, 'wire_width_iu')),
        'bus_width_mils':       _iu_to_mils(_opt(fields, 'bus_width_iu')),
        'line_style':           _opt(fields, 'line_style'),
    }


def _nc_entry_to_dict(entry):
    return {
        'fields': _nc_fields_to_dict(entry.fields),
        'status': _NET_CLASS_STATUS_LABELS.get(entry.status, 'unknown'),
    }


def _get_netclasses_dict(mbs):
    report = mbs.multi_board.get_netclass_report()

    # Per-status counters across container + every sub-project so the agent
    # can quickly summarise replication health without re-walking the tree.
    status_counts = {'source': 0, 'shared': 0, 'local': 0, 'conflict': 0}

    def _bump(entry):
        label = _NET_CLASS_STATUS_LABELS.get(entry.status, 'unknown')
        status_counts[label] = status_counts.get(label, 0) + 1

    for entry in report.container_classes:
        _bump(entry)

    for sub in report.sub_projects:
        for entry in sub.classes:
            _bump(entry)

    return {
        'summary': {
            'container_class_count': len(report.container_classes),
            'sub_project_count':     len(report.sub_projects),
            'class_status_counts':   status_counts,
        },
        'container_classes': [_nc_entry_to_dict(e) for e in report.container_classes],
        'sub_projects': [
            {
                'sub_project_uuid': bucket.sub_project_uuid,
                'display_name':     bucket.display_name,
                'absolute_path':    bucket.absolute_path,
                'loaded':           bucket.loaded,
                'read_error':       bucket.read_error or None,
                'classes':          [_nc_entry_to_dict(e) for e in bucket.classes],
            }
            for bucket in report.sub_projects
        ],
    }


# ---------------------------------------------------------------------------
# Get helpers — libraries (Phase 1 inspection report)
# ---------------------------------------------------------------------------

_LIB_KIND_LABELS = {0: 'unspecified', 1: 'symbol', 2: 'footprint'}
_LIB_SCOPE_LABELS = {0: 'unspecified', 1: 'global', 2: 'container', 3: 'project'}
_LIB_STATUS_LABELS = {0: 'unspecified', 1: 'local', 2: 'shared', 3: 'conflict'}


def _lib_entry_to_dict(entry):
    return {
        'nickname':    entry.nickname,
        'uri':         entry.uri,
        'description': entry.description,
        'options':     entry.options,
        'enabled':     entry.enabled,
        'visible':     entry.visible,
        'kind':        _LIB_KIND_LABELS.get(entry.kind, 'unknown'),
        'scope':       _LIB_SCOPE_LABELS.get(entry.scope, 'unknown'),
        'status':      _LIB_STATUS_LABELS.get(entry.status, 'unknown'),
    }


def _get_libraries_dict(mbs):
    report = mbs.multi_board.get_library_report()

    status_counts = {'shared': 0, 'local': 0, 'conflict': 0}

    def _bump(entry):
        label = _LIB_STATUS_LABELS.get(entry.status, 'unknown')
        status_counts[label] = status_counts.get(label, 0) + 1

    for entry in report.global_rows:
        _bump(entry)
    for entry in report.container_rows:
        _bump(entry)
    for sub in report.sub_projects:
        for entry in sub.rows:
            _bump(entry)

    return {
        'summary': {
            'global_row_count':    len(report.global_rows),
            'container_row_count': len(report.container_rows),
            'sub_project_count':   len(report.sub_projects),
            'row_status_counts':   status_counts,
        },
        'global_rows':    [_lib_entry_to_dict(e) for e in report.global_rows],
        'container_rows': [_lib_entry_to_dict(e) for e in report.container_rows],
        'sub_projects': [
            {
                'sub_project_uuid': bucket.sub_project_uuid,
                'display_name':     bucket.display_name,
                'absolute_path':    bucket.absolute_path,
                'loaded':           bucket.loaded,
                'read_error':       bucket.read_error or None,
                'rows':             [_lib_entry_to_dict(r) for r in bucket.rows],
            }
            for bucket in report.sub_projects
        ],
    }


# ---------------------------------------------------------------------------
# Set helpers — net classes (Phase 2 mutation)
# ---------------------------------------------------------------------------

def _mm_to_iu_kwargs(spec, dest_kwargs):
    """Translate the human-friendly *_mm / *_mils keys in a netclass set
    spec into the IU keys the kipy set_netclass binding expects. Mirrors
    the inverse of _nc_fields_to_dict."""
    pcb_pairs = [
        ('clearance_mm',         'clearance_iu'),
        ('track_width_mm',       'track_width_iu'),
        ('via_diameter_mm',      'via_diameter_iu'),
        ('via_drill_mm',         'via_drill_iu'),
        ('uvia_diameter_mm',     'uvia_diameter_iu'),
        ('uvia_drill_mm',        'uvia_drill_iu'),
        ('diff_pair_width_mm',   'diff_pair_width_iu'),
        ('diff_pair_gap_mm',     'diff_pair_gap_iu'),
        ('diff_pair_via_gap_mm', 'diff_pair_via_gap_iu'),
    ]
    sch_pairs = [
        ('wire_width_mils', 'wire_width_iu'),
        ('bus_width_mils',  'bus_width_iu'),
    ]

    for src, dst in pcb_pairs:
        if src in spec and spec[src] is not None:
            dest_kwargs[dst] = int(round(float(spec[src]) * _PCB_IU_PER_MM))

    for src, dst in sch_pairs:
        if src in spec and spec[src] is not None:
            dest_kwargs[dst] = int(round(float(spec[src]) * _SCH_IU_PER_MIL))

    if 'line_style' in spec and spec['line_style'] is not None:
        dest_kwargs['line_style'] = int(spec['line_style'])

    if 'tuning_profile' in spec:
        dest_kwargs['tuning_profile'] = str(spec['tuning_profile'] or '')

    if 'pcb_color' in spec:
        dest_kwargs['pcb_color_css'] = str(spec['pcb_color'] or '')

    if 'schematic_color' in spec:
        dest_kwargs['schematic_color_css'] = str(spec['schematic_color'] or '')


def _set_netclasses_partial(mbs, nc_in):
    """Apply a net_classes section of a 'set' request: { create, update,
    delete }. Returns a per-action result block summarising what happened.
    """
    result = {'created': [], 'updated': [], 'deleted': [], 'errors': []}

    # Deletes first — keeps the "rename" pattern (delete old + create new
    # in one set call) ordering-correct.
    for name in (nc_in.get('delete') or []):
        try:
            resp = mbs.multi_board.delete_netclass(name)
            result['deleted'].append({'name': name, 'deleted': resp.deleted})
        except Exception as exc:
            result['errors'].append({'op': 'delete', 'name': name, 'message': str(exc)})

    for spec in (nc_in.get('create') or []):
        if 'name' not in spec:
            result['errors'].append({'op': 'create', 'message': "'name' is required"})
            continue

        kwargs = {}
        _mm_to_iu_kwargs(spec, kwargs)

        try:
            resp = mbs.multi_board.set_netclass(spec['name'], **kwargs)
            result['created'].append({
                'name': spec['name'],
                'created': resp.created,
                'sub_projects_touched': resp.sub_projects_touched,
                'classes_added':       resp.classes_added,
                'classes_unchanged':   resp.classes_unchanged,
                'classes_overwritten': resp.classes_overwritten,
                'classes_kept':        resp.classes_kept,
                'classes_skipped':     resp.classes_skipped,
            })
        except Exception as exc:
            result['errors'].append({'op': 'create', 'name': spec['name'],
                                     'message': str(exc)})

    for spec in (nc_in.get('update') or []):
        if 'name' not in spec:
            result['errors'].append({'op': 'update', 'message': "'name' is required"})
            continue

        kwargs = {}
        _mm_to_iu_kwargs(spec, kwargs)

        try:
            resp = mbs.multi_board.set_netclass(spec['name'], **kwargs)
            result['updated'].append({
                'name': spec['name'],
                'created': resp.created,   # should be False for an update
                'sub_projects_touched': resp.sub_projects_touched,
                'classes_overwritten': resp.classes_overwritten,
                'classes_unchanged':   resp.classes_unchanged,
            })
        except Exception as exc:
            result['errors'].append({'op': 'update', 'name': spec['name'],
                                     'message': str(exc)})

    return result


# ---------------------------------------------------------------------------
# Action: get (rules + net_classes + libraries)
# ---------------------------------------------------------------------------

def _do_get():
    mbs = kicad.get_mbs_schematic()
    return {
        'status': 'success',
        'action': 'get',
        'rules':       _get_rules_dict(mbs),
        'net_classes': _get_netclasses_dict(mbs),
        'libraries':   _get_libraries_dict(mbs),
    }


# ---------------------------------------------------------------------------
# Set helpers — libraries (Phase 3 mutation, container-scope only)
# ---------------------------------------------------------------------------

def _set_libraries_partial(mbs, libs_in):
    """Apply a libraries section of a 'set' request: { add, delete, share }.
    All three ops route through container-scope LIBRARY_MANAGER helpers
    (cascade to every sub-project on disk). Local-only sub-project rows
    are NOT mutated here — that surface stays in the desktop sub-project
    library panels.
    """
    result = {'added': [], 'deleted': [], 'shared': [], 'errors': []}

    for spec in (libs_in.get('add') or []):
        for required in ('kind', 'nickname', 'uri'):
            if required not in spec or spec[required] in (None, ''):
                result['errors'].append({'op': 'add',
                                         'message': f"'{required}' is required"})
                spec = None
                break

        if spec is None:
            continue

        try:
            resp = mbs.multi_board.add_library(
                kind=spec['kind'],
                nickname=spec['nickname'],
                uri=spec['uri'],
                type=spec.get('type', ''),
                description=spec.get('description', ''),
                options=spec.get('options', ''),
                enabled=spec.get('enabled', True),
                visible=spec.get('visible', True),
            )
            result['added'].append({
                'nickname':            spec['nickname'],
                'kind':                spec['kind'],
                'added':               resp.added,
                'peers_replicated':    resp.peers_replicated,
                'peers_with_conflict': resp.peers_with_conflict,
            })
        except Exception as exc:
            result['errors'].append({'op': 'add', 'nickname': spec.get('nickname'),
                                     'message': str(exc)})

    for spec in (libs_in.get('delete') or []):
        for required in ('kind', 'nickname'):
            if required not in spec or spec[required] in (None, ''):
                result['errors'].append({'op': 'delete',
                                         'message': f"'{required}' is required"})
                spec = None
                break

        if spec is None:
            continue

        try:
            resp = mbs.multi_board.delete_library(
                kind=spec['kind'],
                nickname=spec['nickname'],
            )
            result['deleted'].append({
                'nickname':       spec['nickname'],
                'kind':           spec['kind'],
                'deleted':        resp.deleted,
                'peers_cleared':  resp.peers_cleared,
            })
        except Exception as exc:
            result['errors'].append({'op': 'delete', 'nickname': spec.get('nickname'),
                                     'message': str(exc)})

    for spec in (libs_in.get('share') or []):
        for required in ('kind', 'nickname'):
            if required not in spec or spec[required] in (None, ''):
                result['errors'].append({'op': 'share',
                                         'message': f"'{required}' is required"})
                spec = None
                break

        if spec is None:
            continue

        if not (spec.get('source_sub_project_uuid') or spec.get('source_sub_project_path')):
            result['errors'].append({
                'op': 'share', 'nickname': spec.get('nickname'),
                'message': "either 'source_sub_project_uuid' or "
                           "'source_sub_project_path' is required",
            })
            continue

        try:
            resp = mbs.multi_board.share_library(
                kind=spec['kind'],
                nickname=spec['nickname'],
                source_sub_project_uuid=spec.get('source_sub_project_uuid', ''),
                source_sub_project_path=spec.get('source_sub_project_path', ''),
            )
            result['shared'].append({
                'nickname':            spec['nickname'],
                'kind':                spec['kind'],
                'shared':              resp.shared,
                'peers_replicated':    resp.peers_replicated,
                'peers_with_conflict': resp.peers_with_conflict,
            })
        except Exception as exc:
            result['errors'].append({'op': 'share', 'nickname': spec.get('nickname'),
                                     'message': str(exc)})

    return result


# ---------------------------------------------------------------------------
# Action: set (rules + net_classes + libraries — partial dispatch)
# ---------------------------------------------------------------------------

def _set_rules_partial(mbs, rules_in):
    kwargs = {}
    for key in ('min_power_pins', 'max_length_nm',
                'cross_board_diff_pairs', 'current_rules',
                'voltage_rules'):
        if key in rules_in:
            kwargs[key] = rules_in[key]

    if not kwargs:
        return {'updated': False, 'replaced_sets': []}

    updated = mbs.multi_board.set_rules(**kwargs)
    return {'updated': bool(updated), 'replaced_sets': sorted(kwargs.keys())}


def _do_set():
    rules_in = TOOL_ARGS.get('rules')
    nc_in    = TOOL_ARGS.get('net_classes')
    libs_in  = TOOL_ARGS.get('libraries')

    if rules_in is None and nc_in is None and libs_in is None:
        return {
            'status': 'error',
            'action': 'set',
            'message': "action='set' requires at least one section in the body: "
                       "'rules', 'net_classes' ({create, update, delete}), or "
                       "'libraries' ({add, delete, share}). Pass an empty list "
                       "under a sub-key to clear that subset.",
        }

    mbs = kicad.get_mbs_schematic()

    out = {'status': 'success', 'action': 'set'}

    if rules_in is not None:
        out['rules'] = _set_rules_partial(mbs, rules_in)

    if nc_in is not None:
        out['net_classes'] = _set_netclasses_partial(mbs, nc_in)

    if libs_in is not None:
        out['libraries'] = _set_libraries_partial(mbs, libs_in)

    return out


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

    sub_data = _read_pro(abs_sub)
    sub_mb = sub_data.get('multi_board', {})

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
    'get': _do_get,
    'set': _do_set,
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
