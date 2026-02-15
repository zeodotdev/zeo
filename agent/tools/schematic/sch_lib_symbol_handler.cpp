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
         << "lib_id = " << nlohmann::json( libId ).dump() << "\n"
         << "include_pins = " << ( includePins ? "True" : "False" ) << "\n"
         << "max_suggestions = " << maxSuggestions << "\n"
         << "pattern_type = " << nlohmann::json( patternType ).dump() << "\n"
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
         << "def get_full_info(search_result):\n"
         << "    \"\"\"Get full symbol info with pins via get_symbol_info.\n"
         << "    search() doesn't populate pin data; get_symbol_info() does.\"\"\"\n"
         << "    try:\n"
         << "        return sch.library.get_symbol_info(search_result.lib_id)\n"
         << "    except Exception:\n"
         << "        return search_result\n"
         << "\n"
         << "def format_symbol(info):\n"
         << "    result = {\n"
         << "        'lib_id': info.lib_id,\n"
         << "        'name': info.name,\n"
         << "        'description': getattr(info, 'description', ''),\n"
         << "        'keywords': getattr(info, 'keywords', ''),\n"
         << "        'unit_count': getattr(info, 'unit_count', 1),\n"
         << "        'is_power': getattr(info, 'is_power', False),\n"
         << "        'pin_count': getattr(info, 'pin_count', 0),\n"
         << "    }\n"
         << "    # Bounding box (body + pins) in mm\n"
         << "    bbox_min_x = getattr(info, 'body_bbox_min_x_nm', 0)\n"
         << "    bbox_min_y = getattr(info, 'body_bbox_min_y_nm', 0)\n"
         << "    bbox_max_x = getattr(info, 'body_bbox_max_x_nm', 0)\n"
         << "    bbox_max_y = getattr(info, 'body_bbox_max_y_nm', 0)\n"
         << "    if bbox_max_x != bbox_min_x or bbox_max_y != bbox_min_y:\n"
         << "        result['body_size'] = {\n"
         << "            'width': round((bbox_max_x - bbox_min_x) / 1_000_000, 2),\n"
         << "            'height': round((bbox_max_y - bbox_min_y) / 1_000_000, 2),\n"
         << "        }\n"
         << "    if include_pins:\n"
         << "        pins = []\n"
         << "        if hasattr(info, 'pins') and info.pins:\n"
         << "            for pin in info.pins:\n"
         << "                pin_info = {\n"
         << "                    'number': getattr(pin, 'number', ''),\n"
         << "                    'name': getattr(pin, 'name', ''),\n"
         << "                }\n"
         << "                pos_x = getattr(pin, 'position_x', 0)\n"
         << "                pos_y = getattr(pin, 'position_y', 0)\n"
         << "                pin_info['pos'] = [pos_x / 1_000_000, pos_y / 1_000_000]\n"
         << "                pin_info['orientation'] = getattr(pin, 'orientation', 0)\n"
         << "                pin_info['electrical_type'] = getattr(pin, 'electrical_type', '')\n"
         << "                pins.append(pin_info)\n"
         << "        elif hasattr(info, 'pin_names') and info.pin_names:\n"
         << "            pins = [{'number': str(i+1), 'name': name} for i, name in enumerate(info.pin_names)]\n"
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
         << "def get_search_term(s):\n"
         << "    base = ''\n"
         << "    for c in s:\n"
         << "        if c in '*?[]{}()|\\\\+^$':\n"
         << "            break\n"
         << "        base += c\n"
         << "    if ':' in base:\n"
         << "        base = base.split(':')[1]\n"
         << "    return base\n"
         << "\n"
         << "try:\n"
         << "    if pattern_type == 'exact':\n"
         << "        if ':' in lib_id:\n"
         << "            # Full Library:Symbol format - direct lookup (returns pin data)\n"
         << "            try:\n"
         << "                info = sch.library.get_symbol_info(lib_id)\n"
         << "                output = {'status': 'found', 'symbol': format_symbol(info)}\n"
         << "                print(json.dumps(output, indent=2))\n"
         << "            except Exception as e:\n"
         << "                lib_name, search_term = lib_id.split(':', 1)\n"
         << "                results = sch.library.search(search_term, libraries=[lib_name], max_results=max_suggestions)\n"
         << "                exact = [r for r in results if r.lib_id == lib_id]\n"
         << "                if exact:\n"
         << "                    output = {'status': 'found', 'symbol': format_symbol(get_full_info(exact[0]))}\n"
         << "                else:\n"
         << "                    suggestions = [format_suggestion(r) for r in results[:max_suggestions]]\n"
         << "                    output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}\n"
         << "                print(json.dumps(output, indent=2))\n"
         << "        else:\n"
         << "            # Symbol name only - search then get full info for pin data\n"
         << "            results = sch.library.search(lib_id, max_results=50)\n"
         << "            exact = [r for r in results if r.name.lower() == lib_id.lower()]\n"
         << "            if len(exact) == 1:\n"
         << "                output = {'status': 'found', 'symbol': format_symbol(get_full_info(exact[0]))}\n"
         << "            elif len(exact) > 1:\n"
         << "                output = {\n"
         << "                    'status': 'multiple_matches',\n"
         << "                    'query': lib_id,\n"
         << "                    'count': len(exact),\n"
         << "                    'symbols': [format_suggestion(r) for r in exact[:max_suggestions]]\n"
         << "                }\n"
         << "            else:\n"
         << "                suggestions = [format_suggestion(r) for r in results[:max_suggestions]]\n"
         << "                output = {'status': 'not_found', 'query': lib_id, 'suggestions': suggestions}\n"
         << "            print(json.dumps(output, indent=2))\n"
         << "    else:\n"
         << "        # Pattern search\n"
         << "        search_term = get_search_term(lib_id)\n"
         << "        results = sch.library.search(search_term, max_results=200)\n"
         << "        \n"
         << "        has_lib_prefix = ':' in lib_id\n"
         << "        if pattern_type == 'regex':\n"
         << "            pattern = re.compile(lib_id)\n"
         << "            if has_lib_prefix:\n"
         << "                filtered = [r for r in results if pattern.fullmatch(r.lib_id)]\n"
         << "            else:\n"
         << "                filtered = [r for r in results if pattern.fullmatch(r.lib_id) or pattern.fullmatch(r.name)]\n"
         << "        else:\n"
         << "            if has_lib_prefix:\n"
         << "                filtered = [r for r in results if fnmatch.fnmatch(r.lib_id, lib_id)]\n"
         << "            else:\n"
         << "                filtered = [r for r in results if fnmatch.fnmatch(r.lib_id, lib_id) or fnmatch.fnmatch(r.name, lib_id)]\n"
         << "        \n"
         << "        if len(filtered) == 1:\n"
         << "            output = {'status': 'found', 'symbol': format_symbol(get_full_info(filtered[0]))}\n"
         << "        else:\n"
         << "            symbols = [format_suggestion(r) for r in filtered[:max_suggestions]]\n"
         << "            output = {\n"
         << "                'status': 'search_results',\n"
         << "                'pattern': lib_id,\n"
         << "                'pattern_type': pattern_type,\n"
         << "                'count': len(filtered),\n"
         << "                'symbols': symbols\n"
         << "            }\n"
         << "        print(json.dumps(output, indent=2))\n"
         << "except Exception as e:\n"
         << "    error_msg = str(e)\n"
         << "    if 'no handler available' in error_msg and 'GetOpenDocuments' in error_msg:\n"
         << "        error_msg = 'Schematic editor must be open. Use open_schematic tool first.'\n"
         << "    output = {'status': 'error', 'message': error_msg}\n"
         << "    print(json.dumps(output, indent=2))\n";

    return "run_shell sch " + code.str();
}
