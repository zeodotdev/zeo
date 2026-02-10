#include "sch_lib_symbol_handler.h"
#include <sstream>


bool SCH_LIB_SYMBOL_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_find_symbol";
}


std::string SCH_LIB_SYMBOL_HANDLER::Execute( const std::string& aToolName,
                                              const nlohmann::json& aInput )
{
    // This tool requires IPC execution - should not be called directly
    return "Error: sch_find_symbol requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_LIB_SYMBOL_HANDLER::GetDescription( const std::string& aToolName,
                                                     const nlohmann::json& aInput ) const
{
    std::string libId;
    if( aInput.is_object() && aInput.contains( "lib_id" ) )
        libId = aInput.value( "lib_id", "" );

    if( libId.empty() )
        return "Getting library symbol info";

    // Check if it's a pattern
    bool hasWildcard = libId.find( '*' ) != std::string::npos ||
                       libId.find( '?' ) != std::string::npos;
    bool hasRegex = libId.find_first_of( "[]{}()|\\+^$" ) != std::string::npos;

    if( hasRegex )
        return "Searching symbols matching regex: " + libId;
    else if( hasWildcard )
        return "Searching symbols matching: " + libId;
    else
        return "Getting symbol info: " + libId;
}


bool SCH_LIB_SYMBOL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_find_symbol";
}


std::string SCH_LIB_SYMBOL_HANDLER::GetIPCCommand( const std::string& aToolName,
                                                    const nlohmann::json& aInput ) const
{
    std::string libId;
    bool includePins = true;
    int maxSuggestions = 10;
    std::string patternType;

    if( aInput.is_object() )
    {
        libId = aInput.value( "lib_id", "" );
        includePins = aInput.value( "include_pins", true );
        maxSuggestions = aInput.value( "max_suggestions", 10 );
        patternType = aInput.value( "pattern_type", "" );
    }

    std::ostringstream code;

    code << "import json, re, fnmatch, sys\n"
         << "\n"
         << "print('[sch_find_symbol] Starting tool execution', file=sys.stderr, flush=True)\n"
         << "lib_id = " << nlohmann::json( libId ).dump() << "\n"
         << "include_pins = " << ( includePins ? "True" : "False" ) << "\n"
         << "max_suggestions = " << maxSuggestions << "\n"
         << "pattern_type = " << nlohmann::json( patternType ).dump() << "\n"
         << "print(f'[sch_find_symbol] lib_id={lib_id}, pattern_type={pattern_type}', file=sys.stderr, flush=True)\n"
         << "\n"
         << "# Auto-detect pattern type if not specified\n"
         << "def detect_pattern_type(s):\n"
         << "    if any(c in s for c in '[]{}()|\\\\+^$'):\n"
         << "        return 'regex'\n"
         << "    elif '*' in s or '?' in s:\n"
         << "        return 'wildcard'\n"
         << "    return 'exact'\n"
         << "\n"
         << "if not pattern_type:\n"
         << "    pattern_type = detect_pattern_type(lib_id)\n"
         << "\n"
         << "# Helper to extract position from various formats (Vector2, dict, tuple)\n"
         << "def get_pos(obj, scale=1000000):\n"
         << "    if obj is None:\n"
         << "        return [0, 0]\n"
         << "    if hasattr(obj, 'x') and hasattr(obj, 'y'):\n"
         << "        return [obj.x / scale, obj.y / scale]\n"
         << "    if isinstance(obj, dict):\n"
         << "        return [obj.get('x', 0) / scale, obj.get('y', 0) / scale]\n"
         << "    if isinstance(obj, (list, tuple)) and len(obj) >= 2:\n"
         << "        return [obj[0] / scale, obj[1] / scale]\n"
         << "    return [0, 0]\n"
         << "\n"
         << "def format_symbol(info):\n"
         << "    \"\"\"Format SymbolInfo for output.\n"
         << "    \n"
         << "    SymbolInfo has: name, library, lib_id, description, keywords,\n"
         << "                   pin_names (list), pin_count, unit_count, is_power\n"
         << "    \"\"\"\n"
         << "    result = {\n"
         << "        'lib_id': info.lib_id,\n"
         << "        'name': info.name,\n"
         << "        'description': getattr(info, 'description', ''),\n"
         << "        'keywords': getattr(info, 'keywords', ''),\n"
         << "        'unit_count': getattr(info, 'unit_count', 1),\n"
         << "        'is_power': getattr(info, 'is_power', False),\n"
         << "        'pin_count': getattr(info, 'pin_count', 0),\n"
         << "    }\n"
         << "    # Include pin details with positions if requested\n"
         << "    if include_pins:\n"
         << "        pins = []\n"
         << "        # Try to get full pin details with positions from the library\n"
         << "        try:\n"
         << "            # Try get_symbol_full for detailed symbol with pin positions\n"
         << "            if hasattr(sch.library, 'get_symbol_full'):\n"
         << "                full_sym = sch.library.get_symbol_full(info.lib_id)\n"
         << "                if full_sym and hasattr(full_sym, 'pins'):\n"
         << "                    for pin in full_sym.pins:\n"
         << "                        pin_info = {\n"
         << "                            'number': getattr(pin, 'number', ''),\n"
         << "                            'name': getattr(pin, 'name', ''),\n"
         << "                        }\n"
         << "                        # Get pin position (library-relative, in mm)\n"
         << "                        if hasattr(pin, 'position'):\n"
         << "                            pin_info['pos'] = get_pos(pin.position)\n"
         << "                        if hasattr(pin, 'length'):\n"
         << "                            pin_info['length'] = pin.length / 1000000.0 if pin.length > 1000 else pin.length\n"
         << "                        if hasattr(pin, 'orientation'):\n"
         << "                            pin_info['orientation'] = str(pin.orientation)\n"
         << "                        pins.append(pin_info)\n"
         << "            # Also try get_symbol_pins directly\n"
         << "            elif hasattr(sch.library, 'get_symbol_pins'):\n"
         << "                sym_pins = sch.library.get_symbol_pins(info.lib_id)\n"
         << "                for pin in sym_pins:\n"
         << "                    pin_info = {\n"
         << "                        'number': getattr(pin, 'number', ''),\n"
         << "                        'name': getattr(pin, 'name', ''),\n"
         << "                    }\n"
         << "                    if hasattr(pin, 'position'):\n"
         << "                        pin_info['pos'] = get_pos(pin.position)\n"
         << "                    pins.append(pin_info)\n"
         << "        except Exception as pin_e:\n"
         << "            print(f'[sch_find_symbol] Could not get pin positions: {pin_e}', file=sys.stderr, flush=True)\n"
         << "        \n"
         << "        # Fallback: use pin_names if no positions available\n"
         << "        if not pins and hasattr(info, 'pin_names') and info.pin_names:\n"
         << "            pins = [{'number': str(i+1), 'name': name} for i, name in enumerate(info.pin_names)]\n"
         << "        \n"
         << "        result['pins'] = pins\n"
         << "    return result\n"
         << "\n"
         << "def format_suggestion(info):\n"
         << "    return {\n"
         << "        'lib_id': info.lib_id,\n"
         << "        'description': getattr(info, 'description', ''),\n"
         << "        'pin_count': getattr(info, 'pin_count', 0),\n"
         << "    }\n"
         << "\n"
         << "# Extract search term for pattern matching\n"
         << "def get_search_term(s):\n"
         << "    # For patterns, extract the base term before special chars\n"
         << "    base = ''\n"
         << "    for c in s:\n"
         << "        if c in '*?[]{}()|\\\\+^$':\n"
         << "            break\n"
         << "        base += c\n"
         << "    # Get the symbol name part (after :)\n"
         << "    if ':' in base:\n"
         << "        base = base.split(':')[1]\n"
         << "    return base\n"
         << "\n"
         << "try:\n"
         << "    if pattern_type == 'exact':\n"
         << "        if ':' in lib_id:\n"
         << "            # Full Library:Symbol format - try direct lookup\n"
         << "            try:\n"
         << "                print(f'[sch_find_symbol] Calling get_symbol_info({lib_id})', file=sys.stderr, flush=True)\n"
         << "                info = sch.library.get_symbol_info(lib_id)\n"
         << "                output = {'status': 'found', 'symbol': format_symbol(info)}\n"
         << "                print(json.dumps(output, indent=2))\n"
         << "            except Exception as e:\n"
         << "                print(f'[sch_find_symbol] get_symbol_info failed: {e}', file=sys.stderr, flush=True)\n"
         << "                lib_name, search_term = lib_id.split(':', 1)\n"
         << "                print(f'[sch_find_symbol] Calling search({search_term}, libraries=[{lib_name}])', file=sys.stderr, flush=True)\n"
         << "                results = sch.library.search(search_term, libraries=[lib_name], max_results=max_suggestions)\n"
         << "                print(f'[sch_find_symbol] search returned {len(results)} results', file=sys.stderr, flush=True)\n"
         << "                exact = [r for r in results if r.lib_id == lib_id]\n"
         << "                if exact:\n"
         << "                    print(f'[sch_find_symbol] Found exact match in search results', file=sys.stderr, flush=True)\n"
         << "                    output = {'status': 'found', 'symbol': format_symbol(exact[0])}\n"
         << "                else:\n"
         << "                    suggestions = [format_suggestion(r) for r in results[:max_suggestions]]\n"
         << "                    output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}\n"
         << "                print(json.dumps(output, indent=2))\n"
         << "        else:\n"
         << "            # Symbol name only - search all libraries for exact name match\n"
         << "            print(f'[sch_find_symbol] Searching all libraries for {lib_id}', file=sys.stderr, flush=True)\n"
         << "            results = sch.library.search(lib_id, max_results=50)\n"
         << "            # Filter to exact name matches (case-insensitive)\n"
         << "            exact = [r for r in results if r.name.lower() == lib_id.lower()]\n"
         << "            print(f'[sch_find_symbol] Found {len(exact)} exact matches', file=sys.stderr, flush=True)\n"
         << "            if len(exact) == 1:\n"
         << "                output = {'status': 'found', 'symbol': format_symbol(exact[0])}\n"
         << "            elif len(exact) > 1:\n"
         << "                # Multiple libraries have this symbol - list them\n"
         << "                output = {\n"
         << "                    'status': 'multiple_matches',\n"
         << "                    'query': lib_id,\n"
         << "                    'count': len(exact),\n"
         << "                    'symbols': [format_suggestion(r) for r in exact[:max_suggestions]]\n"
         << "                }\n"
         << "            else:\n"
         << "                # No exact match - return search suggestions\n"
         << "                suggestions = [format_suggestion(r) for r in results[:max_suggestions]]\n"
         << "                output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}\n"
         << "            print(json.dumps(output, indent=2))\n"
         << "    else:\n"
         << "        # Pattern search\n"
         << "        search_term = get_search_term(lib_id)\n"
         << "        print(f'[sch_find_symbol] Pattern search: calling search({search_term})', file=sys.stderr, flush=True)\n"
         << "        results = sch.library.search(search_term, max_results=200)\n"
         << "        print(f'[sch_find_symbol] search returned {len(results)} results', file=sys.stderr, flush=True)\n"
         << "        \n"
         << "        # Filter by pattern\n"
         << "        if pattern_type == 'regex':\n"
         << "            pattern = re.compile(lib_id)\n"
         << "            filtered = [r for r in results if pattern.fullmatch(r.lib_id)]\n"
         << "        else:  # wildcard\n"
         << "            filtered = [r for r in results if fnmatch.fnmatch(r.lib_id, lib_id)]\n"
         << "        print(f'[sch_find_symbol] {len(filtered)} results after filtering', file=sys.stderr, flush=True)\n"
         << "        \n"
         << "        if len(filtered) == 1:\n"
         << "            # Single match - return full details\n"
         << "            print(f'[sch_find_symbol] Single match, returning details', file=sys.stderr, flush=True)\n"
         << "            output = {'status': 'found', 'symbol': format_symbol(filtered[0])}\n"
         << "        else:\n"
         << "            # Multiple or no matches\n"
         << "            symbols = [format_suggestion(r) for r in filtered[:max_suggestions]]\n"
         << "            output = {\n"
         << "                'status': 'search_results',\n"
         << "                'pattern': lib_id,\n"
         << "                'pattern_type': pattern_type,\n"
         << "                'count': len(filtered),\n"
         << "                'symbols': symbols\n"
         << "            }\n"
         << "        print(json.dumps(output, indent=2))\n"
         << "    print('[sch_find_symbol] Completed successfully', file=sys.stderr, flush=True)\n"
         << "except Exception as e:\n"
         << "    print(f'[sch_find_symbol] Exception: {e}', file=sys.stderr, flush=True)\n"
         << "    error_msg = str(e)\n"
         << "    if 'no handler available' in error_msg and 'GetOpenDocuments' in error_msg:\n"
         << "        error_msg = 'Schematic editor must be open. Use open_schematic tool first.'\n"
         << "    output = {'status': 'error', 'message': error_msg}\n"
         << "    print(json.dumps(output, indent=2))\n";

    return "run_shell sch " + code.str();
}
