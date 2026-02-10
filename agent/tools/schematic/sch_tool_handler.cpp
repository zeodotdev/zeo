#include "sch_tool_handler.h"
#include <sstream>


// Static list of tool names this handler supports
static const char* SCH_TOOL_NAMES[] = {
    "sch_get_summary",
    "sch_read_section",
    "sch_get_pins"
};


bool SCH_TOOL_HANDLER::CanHandle( const std::string& aToolName ) const
{
    for( const char* name : SCH_TOOL_NAMES )
    {
        if( aToolName == name )
            return true;
    }
    return false;
}


std::string SCH_TOOL_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    if( aToolName == "sch_get_summary" )
        return "Error: sch_get_summary requires IPC execution. Schematic editor must be open.";
    else if( aToolName == "sch_get_pins" )
        return "Error: sch_get_pins requires IPC execution. Schematic editor must be open.";
    else if( aToolName == "sch_read_section" )
        return "Error: sch_read_section requires IPC execution. Schematic editor must be open.";

    return "Error: Unknown schematic tool: " + aToolName;
}


std::string SCH_TOOL_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_get_summary" )
        return "Getting schematic summary";
    else if( aToolName == "sch_get_pins" )
    {
        std::string ref = aInput.value( "ref", "" );
        return "Getting pins for " + ( ref.empty() ? "symbol" : ref );
    }
    else if( aToolName == "sch_read_section" )
    {
        std::string section = aInput.value( "section", "all" );
        return "Reading schematic " + section;
    }
    return "Executing " + aToolName;
}


bool SCH_TOOL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_get_summary" ||
           aToolName == "sch_read_section" ||
           aToolName == "sch_get_pins";
}


std::string SCH_TOOL_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    // Handle sch_get_pins - lightweight single-symbol pin lookup
    if( aToolName == "sch_get_pins" )
    {
        std::string ref = aInput.value( "ref", "" );
        if( ref.empty() )
            return "";  // Will be handled as error in Execute

        std::ostringstream code;
        code << "import json, sys\n"
             << "\n"
             << "# Refresh document to handle close/reopen cycles\n"
             << "if hasattr(sch, 'refresh_document'):\n"
             << "    if not sch.refresh_document():\n"
             << "        raise RuntimeError('Schematic editor not open or document not available')\n"
             << "\n"
             << "ref = '" << ref << "'\n"
             << "\n"
             << "try:\n"
             << "    sym = sch.symbols.get_by_ref(ref)\n"
             << "    if not sym:\n"
             << "        # List available symbols\n"
             << "        all_syms = sch.symbols.get_all()\n"
             << "        available = [s.reference for s in all_syms[:20] if hasattr(s, 'reference')]\n"
             << "        print(json.dumps({\n"
             << "            'status': 'error',\n"
             << "            'message': f'Symbol not found: {ref}',\n"
             << "            'available': available\n"
             << "        }))\n"
             << "        sys.exit(0)\n"
             << "\n"
             << "    # Helper to get position in mm\n"
             << "    def get_pos(obj, scale=1000000):\n"
             << "        if obj is None:\n"
             << "            return [0, 0]\n"
             << "        if hasattr(obj, 'x') and hasattr(obj, 'y'):\n"
             << "            return [obj.x / scale, obj.y / scale]\n"
             << "        return [0, 0]\n"
             << "\n"
             << "    # Build pin list using IPC for exact transformed positions\n"
             << "    pins = []\n"
             << "    if hasattr(sym, 'pins'):\n"
             << "        for pin in sym.pins:\n"
             << "            pin_info = {\n"
             << "                'number': pin.number,\n"
             << "                'name': getattr(pin, 'name', '')\n"
             << "            }\n"
             << "            # Get exact transformed position via IPC\n"
             << "            if hasattr(sch.symbols, 'get_transformed_pin_position'):\n"
             << "                try:\n"
             << "                    result = sch.symbols.get_transformed_pin_position(sym, pin.number)\n"
             << "                    if result:\n"
             << "                        pin_info['position'] = get_pos(result['position'])\n"
             << "                        pin_info['orientation'] = result.get('orientation', 0)\n"
             << "                except:\n"
             << "                    pin_info['position'] = get_pos(getattr(pin, 'position', None))\n"
             << "            else:\n"
             << "                pin_info['position'] = get_pos(getattr(pin, 'position', None))\n"
             << "            pins.append(pin_info)\n"
             << "\n"
             << "    # Get lib_id as string\n"
             << "    lib_id_str = ''\n"
             << "    if hasattr(sym, 'lib_id'):\n"
             << "        lib_id = sym.lib_id\n"
             << "        if hasattr(lib_id, 'to_string'):\n"
             << "            lib_id_str = lib_id.to_string()\n"
             << "        else:\n"
             << "            lib_id_str = str(lib_id)\n"
             << "\n"
             << "    result = {\n"
             << "        'status': 'success',\n"
             << "        'ref': ref,\n"
             << "        'lib_id': lib_id_str,\n"
             << "        'position': get_pos(getattr(sym, 'position', None)),\n"
             << "        'angle': getattr(sym, 'angle', 0),\n"
             << "        'value': getattr(sym, 'value', ''),\n"
             << "        'pins': pins\n"
             << "    }\n"
             << "    print(json.dumps(result, indent=2))\n"
             << "\n"
             << "except Exception as e:\n"
             << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

        return "run_shell sch " + code.str();
    }

    if( aToolName != "sch_get_summary" && aToolName != "sch_read_section" )
        return "";

    // Handle sch_read_section IPC code generation
    if( aToolName == "sch_read_section" )
        return GenerateReadSectionIPCCommand( aInput );

    std::string filePath = aInput.value( "file_path", "" );

    std::ostringstream code;

    code << "import json, sys, os\n"
         << "\n"
         << "# Refresh document to handle close/reopen cycles\n"
         << "if hasattr(sch, 'refresh_document'):\n"
         << "    if not sch.refresh_document():\n"
         << "        raise RuntimeError('Schematic editor not open or document not available')\n"
         << "\n"
         << "file_path = " << nlohmann::json( filePath ).dump() << "\n"
         << "\n"
         << "try:\n"
         << "    # Debug: Print document info to stderr for diagnostics\n"
         << "    print(f'[IPC DEBUG] Document specifier: {sch.document}', file=sys.stderr)\n"
         << "    if hasattr(sch.document, 'sheet_path'):\n"
         << "        sp = sch.document.sheet_path\n"
         << "        print(f'[IPC DEBUG] Sheet path: {sp.path_human_readable if hasattr(sp, \"path_human_readable\") else \"N/A\"}', file=sys.stderr)\n"
         << "        if hasattr(sp, 'path') and sp.path:\n"
         << "            print(f'[IPC DEBUG] Sheet UUIDs: {[p.value for p in sp.path]}', file=sys.stderr)\n"
         << "\n"
         << "    # Query live state via IPC\n"
         << "    symbols = sch.symbols.get_all()\n"
         << "    print(f'[IPC DEBUG] Retrieved {len(symbols)} symbols', file=sys.stderr)\n"
         << "    wires = sch.crud.get_wires()\n"
         << "    junctions = sch.crud.get_junctions()\n"
         << "    labels = sch.labels.get_all()\n"
         << "    no_connects = sch.crud.get_no_connects()\n"
         << "    sheets = sch.crud.get_sheets()\n"
         << "    # Get bus entries if available\n"
         << "    bus_entries = []\n"
         << "    try:\n"
         << "        if hasattr(sch, 'buses') and hasattr(sch.buses, 'get_bus_entries'):\n"
         << "            bus_entries = sch.buses.get_bus_entries()\n"
         << "        elif hasattr(sch.crud, 'get_bus_entries'):\n"
         << "            bus_entries = sch.crud.get_bus_entries()\n"
         << "    except:\n"
         << "        pass\n"
         << "\n"
         << "    # Get document info if available\n"
         << "    doc_info = {}\n"
         << "    try:\n"
         << "        doc = sch.document\n"
         << "        if hasattr(doc, 'version'):\n"
         << "            doc_info['version'] = doc.version\n"
         << "        if hasattr(doc, 'uuid'):\n"
         << "            doc_info['uuid'] = str(doc.uuid)\n"
         << "        if hasattr(doc, 'paper'):\n"
         << "            doc_info['paper'] = doc.paper\n"
         << "        if hasattr(doc, 'title'):\n"
         << "            doc_info['title'] = doc.title\n"
         << "    except:\n"
         << "        pass\n"
         << "\n"
         << "    # Helper to extract position from various formats (Vector2, dict, tuple)\n"
         << "    def get_pos(obj, scale=1000000):\n"
         << "        if obj is None:\n"
         << "            return [0, 0]\n"
         << "        if hasattr(obj, 'x') and hasattr(obj, 'y'):\n"
         << "            return [obj.x / scale, obj.y / scale]\n"
         << "        if isinstance(obj, dict):\n"
         << "            return [obj.get('x', 0) / scale, obj.get('y', 0) / scale]\n"
         << "        if isinstance(obj, (list, tuple)) and len(obj) >= 2:\n"
         << "            return [obj[0] / scale, obj[1] / scale]\n"
         << "        return [0, 0]\n"
         << "\n"
         << "    # Helper to rotate a point around origin by angle (degrees)\n"
         << "    import math\n"
         << "    def rotate_point(x, y, angle_deg):\n"
         << "        if angle_deg == 0:\n"
         << "            return x, y\n"
         << "        angle_rad = math.radians(angle_deg)\n"
         << "        cos_a = math.cos(angle_rad)\n"
         << "        sin_a = math.sin(angle_rad)\n"
         << "        return x * cos_a - y * sin_a, x * sin_a + y * cos_a\n"
         << "\n"
         << "    # Format symbols with pin positions\n"
         << "    symbol_data = []\n"
         << "    for sym in symbols:\n"
         << "        # Convert lib_id to string (it may be a LibraryIdentifier object)\n"
         << "        lib_id_str = ''\n"
         << "        if hasattr(sym, 'lib_id'):\n"
         << "            lib_id = sym.lib_id\n"
         << "            if hasattr(lib_id, 'to_string'):\n"
         << "                lib_id_str = lib_id.to_string()\n"
         << "            elif hasattr(lib_id, '__str__'):\n"
         << "                lib_id_str = str(lib_id)\n"
         << "            else:\n"
         << "                lib_id_str = repr(lib_id)\n"
         << "        \n"
         << "        # Get symbol position and angle for pin transformation\n"
         << "        sym_pos = get_pos(getattr(sym, 'position', None))\n"
         << "        sym_angle = sym.angle if hasattr(sym, 'angle') else 0\n"
         << "        mirror_x = getattr(sym, 'mirror_x', False)\n"
         << "        mirror_y = getattr(sym, 'mirror_y', False)\n"
         << "        \n"
         << "        sym_info = {\n"
         << "            'uuid': str(sym.id.value) if hasattr(sym, 'id') else '',\n"
         << "            'lib_id': lib_id_str,\n"
         << "            'ref': sym.reference if hasattr(sym, 'reference') else '',\n"
         << "            'value': sym.value if hasattr(sym, 'value') else '',\n"
         << "            'pos': sym_pos,\n"
         << "            'angle': sym_angle,\n"
         << "            'unit': sym.unit if hasattr(sym, 'unit') else 1,\n"
         << "            'pins': []\n"
         << "        }\n"
         << "        # Get pin positions - for placed symbols, pin.position is already absolute\n"
         << "        if hasattr(sym, 'pins'):\n"
         << "            for pin in sym.pins:\n"
         << "                try:\n"
         << "                    abs_pos = None\n"
         << "                    \n"
         << "                    # First try the IPC method for transformed position\n"
         << "                    if hasattr(sch.symbols, 'get_transformed_pin_position'):\n"
         << "                        try:\n"
         << "                            pin_pos = sch.symbols.get_transformed_pin_position(sym, pin.number)\n"
         << "                            if pin_pos:\n"
         << "                                abs_pos = get_pos(pin_pos)\n"
         << "                        except:\n"
         << "                            pass\n"
         << "                    \n"
         << "                    # Fallback: pin.position on placed symbols is already absolute (transformed)\n"
         << "                    # Do NOT add sym_pos - it's already included in the position\n"
         << "                    if not abs_pos or (abs_pos[0] == 0 and abs_pos[1] == 0):\n"
         << "                        pin_pos = get_pos(getattr(pin, 'position', None))\n"
         << "                        if pin_pos and (pin_pos[0] != 0 or pin_pos[1] != 0):\n"
         << "                            abs_pos = pin_pos  # Already absolute, no transformation needed\n"
         << "                    \n"
         << "                    if abs_pos:\n"
         << "                        sym_info['pins'].append({\n"
         << "                            'number': pin.number,\n"
         << "                            'name': getattr(pin, 'name', ''),\n"
         << "                            'pos': abs_pos\n"
         << "                        })\n"
         << "                except:\n"
         << "                    pass\n"
         << "        symbol_data.append(sym_info)\n"
         << "\n"
         << "    # Format wires\n"
         << "    wire_data = []\n"
         << "    for wire in wires:\n"
         << "        wire_data.append({\n"
         << "            'uuid': str(wire.id.value) if hasattr(wire, 'id') else '',\n"
         << "            'start': get_pos(getattr(wire, 'start', None)),\n"
         << "            'end': get_pos(getattr(wire, 'end', None))\n"
         << "        })\n"
         << "\n"
         << "    # Format junctions\n"
         << "    junction_data = []\n"
         << "    for junc in junctions:\n"
         << "        junction_data.append({\n"
         << "            'uuid': str(junc.id.value) if hasattr(junc, 'id') else '',\n"
         << "            'pos': get_pos(getattr(junc, 'position', None))\n"
         << "        })\n"
         << "\n"
         << "    # Format labels\n"
         << "    label_data = []\n"
         << "    for lbl in labels:\n"
         << "        label_data.append({\n"
         << "            'uuid': str(lbl.id.value) if hasattr(lbl, 'id') else '',\n"
         << "            'text': lbl.text if hasattr(lbl, 'text') else '',\n"
         << "            'pos': get_pos(getattr(lbl, 'position', None)),\n"
         << "            'type': type(lbl).__name__\n"
         << "        })\n"
         << "\n"
         << "    # Format no_connects\n"
         << "    nc_data = []\n"
         << "    for nc in no_connects:\n"
         << "        nc_data.append({\n"
         << "            'uuid': str(nc.id.value) if hasattr(nc, 'id') else '',\n"
         << "            'pos': get_pos(getattr(nc, 'position', None))\n"
         << "        })\n"
         << "\n"
         << "    # Format sheets with full details\n"
         << "    sheet_data = []\n"
         << "    for sheet in sheets:\n"
         << "        sheet_data.append({\n"
         << "            'uuid': str(sheet.id.value) if hasattr(sheet, 'id') else '',\n"
         << "            'name': sheet.name if hasattr(sheet, 'name') else '',\n"
         << "            'file': sheet.filename if hasattr(sheet, 'filename') else ''\n"
         << "        })\n"
         << "\n"
         << "    # Format bus entries\n"
         << "    bus_entry_data = []\n"
         << "    for entry in bus_entries:\n"
         << "        entry_info = {\n"
         << "            'uuid': str(entry.id.value) if hasattr(entry, 'id') else '',\n"
         << "            'pos': get_pos(getattr(entry, 'position', None)),\n"
         << "        }\n"
         << "        # Get size/end point if available\n"
         << "        if hasattr(entry, 'size'):\n"
         << "            entry_info['size'] = get_pos(entry.size)\n"
         << "        if hasattr(entry, 'end'):\n"
         << "            entry_info['end'] = get_pos(entry.end)\n"
         << "        bus_entry_data.append(entry_info)\n"
         << "\n"
         << "    summary = {\n"
         << "        'source': 'ipc',\n"
         << "        'file': os.path.basename(file_path) if file_path else '',\n"
         << "        'version': doc_info.get('version', 0),\n"
         << "        'uuid': doc_info.get('uuid', ''),\n"
         << "        'paper': doc_info.get('paper', ''),\n"
         << "        'title': doc_info.get('title', ''),\n"
         << "        'symbols': symbol_data,\n"
         << "        'wires': wire_data,\n"
         << "        'junctions': junction_data,\n"
         << "        'labels': label_data,\n"
         << "        'no_connects': nc_data,\n"
         << "        'sheets': sheet_data,\n"
         << "        'bus_entries': bus_entry_data,\n"
         << "        'counts': {\n"
         << "            'symbols': len(symbols),\n"
         << "            'wires': len(wires),\n"
         << "            'junctions': len(junctions),\n"
         << "            'labels': len(labels),\n"
         << "            'no_connects': len(no_connects),\n"
         << "            'sheets': len(sheets),\n"
         << "            'bus_entries': len(bus_entries)\n"
         << "        }\n"
         << "    }\n"
         << "    print(json.dumps(summary, indent=2))\n"
         << "\n"
         << "except Exception as e:\n";

    code << "    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))\n";

    return "run_shell sch " + code.str();
}


std::string SCH_TOOL_HANDLER::GenerateReadSectionIPCCommand( const nlohmann::json& aInput ) const
{
    std::string section = aInput.value( "section", "all" );
    std::string filter = aInput.value( "filter", "" );

    std::ostringstream code;

    code << "import json, sys, math, re\n"
         << "\n"
         << "# Refresh document to handle close/reopen cycles\n"
         << "if hasattr(sch, 'refresh_document'):\n"
         << "    if not sch.refresh_document():\n"
         << "        raise RuntimeError('Schematic editor not open or document not available')\n"
         << "\n"
         << "section = " << nlohmann::json( section ).dump() << "\n"
         << "filter_str = " << nlohmann::json( filter ).dump() << "\n"
         << "\n"
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
         << "def matches_filter(item, filter_str):\n"
         << "    if not filter_str:\n"
         << "        return True\n"
         << "    # Check UUID match\n"
         << "    item_uuid = str(item.id.value) if hasattr(item, 'id') else ''\n"
         << "    if filter_str == item_uuid:\n"
         << "        return True\n"
         << "    # Check reference match (for symbols)\n"
         << "    ref = getattr(item, 'reference', '')\n"
         << "    if ref:\n"
         << "        pattern = filter_str.replace('*', '.*').replace('?', '.')\n"
         << "        if re.match(f'^{pattern}$', ref, re.IGNORECASE):\n"
         << "            return True\n"
         << "    return False\n"
         << "\n"
         << "try:\n"
         << "    result = {'section': section}\n"
         << "\n"
         << "    if section in ('symbols', 'all'):\n"
         << "        symbols = sch.symbols.get_all()\n"
         << "        symbol_data = []\n"
         << "        for sym in symbols:\n"
         << "            if not matches_filter(sym, filter_str):\n"
         << "                continue\n"
         << "            lib_id_str = ''\n"
         << "            if hasattr(sym, 'lib_id'):\n"
         << "                lib_id = sym.lib_id\n"
         << "                lib_id_str = lib_id.to_string() if hasattr(lib_id, 'to_string') else str(lib_id)\n"
         << "            pins = []\n"
         << "            if hasattr(sym, 'pins'):\n"
         << "                for pin in sym.pins:\n"
         << "                    try:\n"
         << "                        abs_pos = None\n"
         << "                        if hasattr(sch.symbols, 'get_transformed_pin_position'):\n"
         << "                            try:\n"
         << "                                pin_pos = sch.symbols.get_transformed_pin_position(sym, pin.number)\n"
         << "                                if pin_pos:\n"
         << "                                    abs_pos = get_pos(pin_pos)\n"
         << "                            except:\n"
         << "                                pass\n"
         << "                        if not abs_pos or (abs_pos[0] == 0 and abs_pos[1] == 0):\n"
         << "                            pin_pos = get_pos(getattr(pin, 'position', None))\n"
         << "                            if pin_pos and (pin_pos[0] != 0 or pin_pos[1] != 0):\n"
         << "                                abs_pos = pin_pos\n"
         << "                        if abs_pos:\n"
         << "                            pins.append({'number': pin.number, 'name': getattr(pin, 'name', ''), 'pos': abs_pos})\n"
         << "                    except:\n"
         << "                        pass\n"
         << "            symbol_data.append({\n"
         << "                'uuid': str(sym.id.value) if hasattr(sym, 'id') else '',\n"
         << "                'lib_id': lib_id_str,\n"
         << "                'ref': sym.reference if hasattr(sym, 'reference') else '',\n"
         << "                'value': sym.value if hasattr(sym, 'value') else '',\n"
         << "                'pos': get_pos(getattr(sym, 'position', None)),\n"
         << "                'angle': getattr(sym, 'angle', 0),\n"
         << "                'unit': getattr(sym, 'unit', 1),\n"
         << "                'pins': pins\n"
         << "            })\n"
         << "        result['symbols'] = symbol_data\n"
         << "\n"
         << "    if section in ('wires', 'all'):\n"
         << "        wires = sch.crud.get_wires()\n"
         << "        result['wires'] = [{'uuid': str(w.id.value) if hasattr(w, 'id') else '', 'start': get_pos(getattr(w, 'start', None)), 'end': get_pos(getattr(w, 'end', None))} for w in wires]\n"
         << "\n"
         << "    if section in ('junctions', 'all'):\n"
         << "        junctions = sch.crud.get_junctions()\n"
         << "        result['junctions'] = [{'uuid': str(j.id.value) if hasattr(j, 'id') else '', 'pos': get_pos(getattr(j, 'position', None))} for j in junctions]\n"
         << "\n"
         << "    if section in ('labels', 'all'):\n"
         << "        labels = sch.labels.get_all()\n"
         << "        result['labels'] = [{'uuid': str(l.id.value) if hasattr(l, 'id') else '', 'text': l.text if hasattr(l, 'text') else '', 'pos': get_pos(getattr(l, 'position', None)), 'type': type(l).__name__} for l in labels]\n"
         << "\n"
         << "    if section in ('sheets', 'all'):\n"
         << "        sheets = sch.crud.get_sheets()\n"
         << "        result['sheets'] = [{'uuid': str(s.id.value) if hasattr(s, 'id') else '', 'name': s.name if hasattr(s, 'name') else '', 'file': s.filename if hasattr(s, 'filename') else ''} for s in sheets]\n"
         << "\n"
         << "    if section == 'header':\n"
         << "        doc = sch.document\n"
         << "        result['header'] = {\n"
         << "            'version': getattr(doc, 'version', 0),\n"
         << "            'uuid': str(doc.uuid) if hasattr(doc, 'uuid') else '',\n"
         << "            'paper': getattr(doc, 'paper', ''),\n"
         << "            'title': getattr(doc, 'title', '')\n"
         << "        }\n"
         << "\n"
         << "    print(json.dumps(result, indent=2))\n"
         << "\n"
         << "except Exception as e:\n"
         << "    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))\n";

    return "run_shell sch " + code.str();
}
