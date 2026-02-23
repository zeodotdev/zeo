import json, re, fnmatch, sys

lib_id = TOOL_ARGS.get("lib_id", "")
include_pins = TOOL_ARGS.get("include_pins", True)
max_suggestions = TOOL_ARGS.get("max_suggestions", 10)
pattern_type = TOOL_ARGS.get("pattern_type", "")

# Auto-detect pattern type if not specified
def detect_pattern_type(s):
    if any(c in s for c in '[]{}()|\\+^$'):
        return 'regex'
    elif '*' in s or '?' in s:
        return 'wildcard'
    return 'exact'

if not pattern_type:
    pattern_type = detect_pattern_type(lib_id)

def get_full_info(search_result):
    """Get full symbol info with pins via get_symbol_info.
    search() doesn't populate pin data; get_symbol_info() does."""
    try:
        return sch.library.get_symbol_info(search_result.lib_id)
    except Exception:
        return search_result

def format_symbol(info):
    result = {
        'lib_id': info.lib_id,
        'name': info.name,
        'description': getattr(info, 'description', ''),
        'keywords': getattr(info, 'keywords', ''),
        'unit_count': getattr(info, 'unit_count', 1),
        'is_power': getattr(info, 'is_power', False),
        'pin_count': getattr(info, 'pin_count', 0),
        'footprint_filters': getattr(info, 'footprint_filters', []),
    }
    ds = getattr(info, 'datasheet', '')
    if ds:
        result['datasheet'] = ds
    # Bounding box (body + pins) in mm
    bbox_min_x = getattr(info, 'body_bbox_min_x_nm', 0)
    bbox_min_y = getattr(info, 'body_bbox_min_y_nm', 0)
    bbox_max_x = getattr(info, 'body_bbox_max_x_nm', 0)
    bbox_max_y = getattr(info, 'body_bbox_max_y_nm', 0)
    if bbox_max_x != bbox_min_x or bbox_max_y != bbox_min_y:
        result['body_size'] = {
            'width': round((bbox_max_x - bbox_min_x) / 1_000_000, 2),
            'height': round((bbox_max_y - bbox_min_y) / 1_000_000, 2),
        }
    if include_pins:
        pins = []
        if hasattr(info, 'pins') and info.pins:
            for pin in info.pins:
                pin_info = {
                    'number': getattr(pin, 'number', ''),
                    'name': getattr(pin, 'name', ''),
                }
                pos_x = getattr(pin, 'position_x', 0)
                pos_y = getattr(pin, 'position_y', 0)
                pin_info['pos'] = [pos_x / 1_000_000, pos_y / 1_000_000]
                pin_info['orientation'] = getattr(pin, 'orientation', 0)
                pin_info['electrical_type'] = getattr(pin, 'electrical_type', '')
                pins.append(pin_info)
        elif hasattr(info, 'pin_names') and info.pin_names:
            pins = [{'number': str(i+1), 'name': name} for i, name in enumerate(info.pin_names)]
        result['pins'] = pins
    return result

def get_search_term(s):
    base = ''
    for c in s:
        if c in '*?[]{}()|\\+^$':
            break
        base += c
    if ':' in base:
        base = base.split(':')[1]
    return base

try:
    if pattern_type == 'exact':
        if ':' in lib_id:
            # Full Library:Symbol format - direct lookup (returns pin data)
            try:
                info = sch.library.get_symbol_info(lib_id)
                output = {'status': 'found', 'symbol': format_symbol(info)}
                print(json.dumps(output, indent=2))
            except Exception as e:
                lib_name, search_term = lib_id.split(':', 1)
                results = sch.library.search(search_term, libraries=[lib_name], max_results=max_suggestions)
                exact = [r for r in results if r.lib_id == lib_id]
                if exact:
                    output = {'status': 'found', 'symbol': format_symbol(get_full_info(exact[0]))}
                else:
                    suggestions = [format_symbol(get_full_info(r)) for r in results[:max_suggestions]]
                    output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}
                print(json.dumps(output, indent=2))
        else:
            # Symbol name only - search then get full info for pin data
            results = sch.library.search(lib_id, max_results=50)
            exact = [r for r in results if r.name.lower() == lib_id.lower()]
            if len(exact) == 1:
                output = {'status': 'found', 'symbol': format_symbol(get_full_info(exact[0]))}
            elif len(exact) > 1:
                output = {
                    'status': 'multiple_matches',
                    'query': lib_id,
                    'count': len(exact),
                    'symbols': [format_symbol(get_full_info(r)) for r in exact[:max_suggestions]]
                }
            else:
                suggestions = [format_symbol(get_full_info(r)) for r in results[:max_suggestions]]
                output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}
            print(json.dumps(output, indent=2))
    else:
        # Pattern search
        search_term = get_search_term(lib_id)
        results = sch.library.search(search_term, max_results=200)

        has_lib_prefix = ':' in lib_id
        if pattern_type == 'regex':
            pattern = re.compile(lib_id)
            if has_lib_prefix:
                filtered = [r for r in results if pattern.fullmatch(r.lib_id)]
            else:
                filtered = [r for r in results if pattern.fullmatch(r.lib_id) or pattern.fullmatch(r.name)]
        else:
            if has_lib_prefix:
                filtered = [r for r in results if fnmatch.fnmatch(r.lib_id, lib_id)]
            else:
                filtered = [r for r in results if fnmatch.fnmatch(r.lib_id, lib_id) or fnmatch.fnmatch(r.name, lib_id)]

        if len(filtered) == 1:
            output = {'status': 'found', 'symbol': format_symbol(get_full_info(filtered[0]))}
        else:
            symbols = [format_symbol(get_full_info(r)) for r in filtered[:max_suggestions]]
            output = {
                'status': 'search_results',
                'pattern': lib_id,
                'pattern_type': pattern_type,
                'count': len(filtered),
                'symbols': symbols
            }
        print(json.dumps(output, indent=2))
except Exception as e:
    error_msg = str(e)
    if 'no handler available' in error_msg and 'GetOpenDocuments' in error_msg:
        error_msg = 'Schematic editor must be open. Use open_schematic tool first.'
    output = {'status': 'error', 'message': error_msg}
    print(json.dumps(output, indent=2))
