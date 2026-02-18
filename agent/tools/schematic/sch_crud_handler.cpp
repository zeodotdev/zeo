#include "sch_crud_handler.h"
#include <sstream>
#include <iomanip>

// Overlap detection padding per side (mm). Total clearance = 2x this value.
// Set to 0 — bounding boxes are already shrunk (include_text=False) to expose
// pin tips, and any padding causes false rejections for labels/wires near pins.
static constexpr double BBOX_MARGIN_MM = 0.0;


bool SCH_CRUD_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_switch_sheet" ||
           aToolName == "sch_add_sheet";
}


std::string SCH_CRUD_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    // All CRUD tools require IPC execution - should not be called directly
    return "Error: " + aToolName + " requires IPC execution. Use GetIPCCommand() instead.";
}


std::string SCH_CRUD_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    if( aToolName == "sch_add" )
    {
        if( aInput.contains( "elements" ) && aInput["elements"].is_array() )
        {
            size_t count = aInput["elements"].size();
            if( count == 1 )
            {
                auto elem = aInput["elements"][0];
                std::string elementType = elem.value( "element_type", "element" );
                std::string libId = elem.value( "lib_id", "" );
                if( elementType == "symbol" && !libId.empty() )
                {
                    size_t colonPos = libId.find( ':' );
                    std::string symbolName = ( colonPos != std::string::npos )
                                                 ? libId.substr( colonPos + 1 )
                                                 : libId;
                    return "Adding " + symbolName;
                }
                return "Adding " + elementType;
            }
            return "Adding " + std::to_string( count ) + " elements";
        }
        return "Adding elements";
    }
    else if( aToolName == "sch_update" )
    {
        if( aInput.contains( "updates" ) && aInput["updates"].is_array() )
        {
            size_t count = aInput["updates"].size();
            if( count == 1 )
            {
                std::string target = aInput["updates"][0].value( "target", "" );
                if( !target.empty() )
                    return "Updating " + target;
            }
            return "Updating " + std::to_string( count ) + " elements";
        }
        return "Updating elements";
    }
    else if( aToolName == "sch_delete" )
    {
        bool chainDelete = aInput.value( "chain_delete", false );
        std::string prefix = chainDelete ? "Chain-deleting net for " : "Deleting ";

        if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
        {
            size_t count = aInput["targets"].size();
            if( count == 1 )
            {
                const auto& t = aInput["targets"][0];

                if( t.is_string() )
                    return prefix + t.get<std::string>();
                else if( t.is_object() )
                    return prefix + t.value( "type", std::string( "element" ) )
                           + ( t.contains( "text" )
                                   ? " '" + t["text"].get<std::string>() + "'"
                                   : "" );
            }
            return prefix + std::to_string( count ) + " elements";
        }
        return prefix + "elements";
    }
    else if( aToolName == "sch_add_sheet" )
    {
        std::string name = aInput.value( "sheet_name", "sheet" );
        return "Adding sheet: " + name;
    }
    else if( aToolName == "sch_switch_sheet" )
    {
        std::string sheetPath = aInput.value( "sheet_path", "" );
        if( !sheetPath.empty() )
            return "Switching to sheet: " + sheetPath;
        return "Listing available sheets";
    }
    return "Executing " + aToolName;
}


bool SCH_CRUD_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_switch_sheet" ||
           aToolName == "sch_add_sheet";
}


std::string SCH_CRUD_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "sch_add" )
        code = GenerateAddBatchCode( aInput );  // Now uses elements array
    else if( aToolName == "sch_update" )
        code = GenerateUpdateBatchCode( aInput );  // Now uses updates array
    else if( aToolName == "sch_delete" )
        code = GenerateBatchDeleteCode( aInput );  // Now uses targets array
    else if( aToolName == "sch_switch_sheet" )
        code = GenerateSwitchSheetCode( aInput );
    else if( aToolName == "sch_add_sheet" )
        code = GenerateAddSheetCode( aInput );

    return "run_shell sch " + code;
}


std::string SCH_CRUD_HANDLER::EscapePythonString( const std::string& aStr ) const
{
    std::string result;
    result.reserve( aStr.size() + 10 );

    for( char c : aStr )
    {
        switch( c )
        {
        case '\\': result += "\\\\"; break;
        case '\'': result += "\\'"; break;
        case '\"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }

    return result;
}


double SCH_CRUD_HANDLER::SnapToGrid( double aMm, double aGrid )
{
    double gridUnits = std::round( aMm / aGrid );
    // Round to 4 decimal places to eliminate IEEE 754 artifacts
    // (e.g. 51 * 1.27 might produce 64.77000000000001 instead of 64.77)
    return std::round( gridUnits * aGrid * 1e4 ) / 1e4;
}


std::string SCH_CRUD_HANDLER::MmToNm( double aMm ) const
{
    int64_t nm = std::llround( aMm * 1000000.0 );
    return std::to_string( nm );
}


std::string SCH_CRUD_HANDLER::GenerateRefreshPreamble() const
{
    return
        "# Refresh document to handle close/reopen cycles\n"
        "if hasattr(sch, 'refresh_document'):\n"
        "    if not sch.refresh_document():\n"
        "        raise RuntimeError('Schematic editor not open or document not available')\n";
}


std::string SCH_CRUD_HANDLER::GenerateEditorOpenCheck() const
{
    // Python code to check if editor is still open when IPC fails
    // Returns True if editor is open (blocking file operations), False if closed
    return
        "# Check if editor is still open - don't mix IPC and file operations\n"
        "editor_still_open = False\n"
        "try:\n"
        "    if hasattr(sch, 'refresh_document'):\n"
        "        editor_still_open = sch.refresh_document()\n"
        "except:\n"
        "    pass  # Editor likely closed\n"
        "\n";
}


std::string SCH_CRUD_HANDLER::GenerateFileFallbackHeader() const
{
    // Python code for file-based fallback operations
    // Indented with 8 spaces to fit inside: if ... try: block
    std::ostringstream code;

    code << "        # File-based fallback functions\n"
         << "        import re, uuid, os\n"
         << "\n"
         << "        def file_read(path):\n"
         << "            with open(path, 'r', encoding='utf-8') as f:\n"
         << "                return f.read()\n"
         << "\n"
         << "        def file_write(path, content):\n"
         << "            # Create backup\n"
         << "            if os.path.exists(path):\n"
         << "                backup = path + '.bak'\n"
         << "                with open(path, 'r', encoding='utf-8') as f:\n"
         << "                    with open(backup, 'w', encoding='utf-8') as bf:\n"
         << "                        bf.write(f.read())\n"
         << "            with open(path, 'w', encoding='utf-8') as f:\n"
         << "                f.write(content)\n"
         << "\n"
         << "        def generate_uuid():\n"
         << "            return str(uuid.uuid4())\n"
         << "\n"
         << "        def find_insert_position(content, element_type):\n"
         << "            \"\"\"Find position to insert new element before sheet_instances.\"\"\"\n"
         << "            # Insert before sheet_instances if present, else before final paren\n"
         << "            sheet_inst = content.find('(sheet_instances')\n"
         << "            if sheet_inst != -1:\n"
         << "                return sheet_inst\n"
         << "            # Find last closing paren\n"
         << "            return content.rfind(')')\n"
         << "\n"
         << "        def add_element_to_file(file_path, element_type, element_sexpr):\n"
         << "            \"\"\"Add an element to the schematic file.\"\"\"\n"
         << "            content = file_read(file_path)\n"
         << "            pos = find_insert_position(content, element_type)\n"
         << "            new_content = content[:pos] + element_sexpr + '\\n\\n  ' + content[pos:]\n"
         << "            file_write(file_path, new_content)\n"
         << "            return True\n"
         << "\n"
         << "        def delete_element_from_file(file_path, target_uuid):\n"
         << "            \"\"\"Delete element by UUID from schematic file.\"\"\"\n"
         << "            content = file_read(file_path)\n"
         << "            # Find element containing this UUID\n"
         << "            uuid_pos = content.find(target_uuid)\n"
         << "            if uuid_pos == -1:\n"
         << "                return False\n"
         << "            # Walk back to find element start\n"
         << "            depth = 0\n"
         << "            start = uuid_pos\n"
         << "            while start > 0:\n"
         << "                if content[start] == ')':\n"
         << "                    depth += 1\n"
         << "                elif content[start] == '(':\n"
         << "                    if depth == 0:\n"
         << "                        break\n"
         << "                    depth -= 1\n"
         << "                start -= 1\n"
         << "            # Walk forward to find element end\n"
         << "            depth = 1\n"
         << "            end = start + 1\n"
         << "            while end < len(content) and depth > 0:\n"
         << "                if content[end] == '(':\n"
         << "                    depth += 1\n"
         << "                elif content[end] == ')':\n"
         << "                    depth -= 1\n"
         << "                end += 1\n"
         << "            # Remove element and surrounding whitespace\n"
         << "            while end < len(content) and content[end] in ' \\t\\n':\n"
         << "                end += 1\n"
         << "            new_content = content[:start] + content[end:]\n"
         << "            file_write(file_path, new_content)\n"
         << "            return True\n"
         << "\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateUpdateBatchCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "updates" ) || !aInput["updates"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'updates array is required'}))\n";
        return code.str();
    }

    auto updates = aInput["updates"];
    std::string filePath = aInput.value( "file_path", "" );

    code << "import json, re, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "results = []\n";
    code << "\n";

    // --- Overlap detection preamble (only if any update has position) ---
    bool hasPositionUpdate = false;
    for( size_t i = 0; i < updates.size(); ++i )
    {
        if( updates[i].contains( "position" ) && updates[i]["position"].is_array() &&
            updates[i]["position"].size() >= 2 )
        {
            hasPositionUpdate = true;
            break;
        }
    }

    if( hasPositionUpdate )
    {
        code << "# Collect bounding boxes of all existing symbols and labels for overlap detection\n";
        code << "_BBOX_MARGIN = " << BBOX_MARGIN_MM << "\n";
        code << "placed_bboxes = []\n";
        code << "try:\n";
        code << "    _all_existing = sch.symbols.get_all()\n";
        code << "    for _esym in _all_existing:\n";
        code << "        try:\n";
        code << "            _ebb = sch.transform.get_bounding_box(_esym, units='mm', include_text=False)\n";
        code << "        except:\n";
        code << "            continue\n";
        code << "        if _ebb:\n";
        code << "            placed_bboxes.append({'id': str(_esym.id.value), 'ref': getattr(_esym, 'reference', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})\n";
        code << "except:\n";
        code << "    pass\n";
        code << "try:\n";
        code << "    for _elbl in sch.labels.get_all():\n";
        code << "        try:\n";
        code << "            _ebb = sch.transform.get_bounding_box(_elbl, units='mm', include_text=False)\n";
        code << "        except:\n";
        code << "            continue\n";
        code << "        if _ebb:\n";
        code << "            placed_bboxes.append({'id': str(_elbl.id.value), 'ref': getattr(_elbl, 'text', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})\n";
        code << "except:\n";
        code << "    pass\n";
        code << "\n";
        code << "def _bboxes_overlap(a, b):\n";
        code << "    _eps = 0.001\n";
        code << "    return a['min_x'] < b['max_x'] - _eps and a['max_x'] > b['min_x'] + _eps and a['min_y'] < b['max_y'] - _eps and a['max_y'] > b['min_y'] + _eps\n";
        code << "\n";
    }

    code << "try:\n";

    // Process each update in the array
    for( size_t i = 0; i < updates.size(); ++i )
    {
        auto update = updates[i];
        std::string target = update.value( "target", "" );

        if( target.empty() )
        {
            code << "    results.append({'index': " << i << ", 'error': 'target is required'})\n";
            continue;
        }

        code << "    # Update " << i << ": " << target << "\n";
        code << "    try:\n";
        code << "        target_" << i << " = '" << EscapePythonString( target ) << "'\n";
        code << "        is_uuid_" << i << " = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target_" << i << "))\n";
        code << "        item_" << i << " = None\n";
        code << "        if is_uuid_" << i << ":\n";
        code << "            items = sch.crud.get_by_id([target_" << i << "])\n";
        code << "            if items:\n";
        code << "                item_" << i << " = items[0]\n";
        code << "        else:\n";
        code << "            item_" << i << " = sch.symbols.get_by_ref(target_" << i << ")\n";
        code << "        if not item_" << i << ":\n";
        code << "            raise ValueError(f'Element not found: {target_" << i << "}')\n";
        code << "        updated_" << i << " = False\n";

        if( update.contains( "position" ) && update["position"].is_array() &&
            update["position"].size() >= 2 )
        {
            double posX = SnapToGrid( update["position"][0].get<double>() );
            double posY = SnapToGrid( update["position"][1].get<double>() );

            // Capture old symbol position and pin positions before move (for wire dragging)
            code << "        _old_sym_x_" << i << " = round(item_" << i << ".position.x / 1e6, 2)\n";
            code << "        _old_sym_y_" << i << " = round(item_" << i << ".position.y / 1e6, 2)\n";
            code << "        _old_pins_" << i << " = []\n";
            code << "        for _pin in item_" << i << ".pins:\n";
            code << "            try:\n";
            code << "                _tp = sch.symbols.get_transformed_pin_position(item_" << i << ", _pin.number)\n";
            code << "                if _tp:\n";
            code << "                    _old_pins_" << i << ".append((round(_tp['position'].x / 1e6, 2), round(_tp['position'].y / 1e6, 2)))\n";
            code << "            except: pass\n";


            // Move the symbol
            code << "        new_pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        item_" << i << " = sch.symbols.move(item_" << i << ", new_pos_" << i << ")\n";


            // Overlap detection: check new position against all existing (excluding self)
            code << "        _overlap_" << i << " = False\n";
            code << "        _item_id_" << i << " = str(item_" << i << ".id.value)\n";
            code << "        try:\n";
            code << "            _bb_" << i << " = sch.transform.get_bounding_box(item_" << i << ", units='mm', include_text=False)\n";
            code << "            if _bb_" << i << ":\n";
            code << "                _new_bbox_" << i << " = {'min_x': _bb_" << i << "['min_x'] - _BBOX_MARGIN, 'max_x': _bb_" << i << "['max_x'] + _BBOX_MARGIN, 'min_y': _bb_" << i << "['min_y'] - _BBOX_MARGIN, 'max_y': _bb_" << i << "['max_y'] + _BBOX_MARGIN}\n";
            code << "                _obstacle_ref_" << i << " = '?'\n";
            code << "                for _pb in placed_bboxes:\n";
            code << "                    if _pb.get('id') == _item_id_" << i << ":\n";
            code << "                        continue\n";
            code << "                    if _bboxes_overlap(_new_bbox_" << i << ", _pb):\n";
            code << "                        _overlap_" << i << " = True\n";
            code << "                        _obstacle_ref_" << i << " = _pb.get('ref', '?')\n";
            code << "                        break\n";
            code << "        except:\n";
            code << "            pass\n";
            code << "        if _overlap_" << i << ":\n";
            code << "            item_" << i << " = sch.symbols.move(item_" << i << ", Vector2.from_xy_mm(_old_sym_x_" << i << ", _old_sym_y_" << i << "))\n";
            code << "            raise ValueError(f'Move rejected: overlaps {_obstacle_ref_" << i << "}')\n";

            // Update placed_bboxes with new position
            code << "        if _bb_" << i << ":\n";
            code << "            for _idx, _pb in enumerate(placed_bboxes):\n";
            code << "                if _pb.get('id') == _item_id_" << i << ":\n";
            code << "                    placed_bboxes[_idx] = {'id': _item_id_" << i << ", 'min_x': _new_bbox_" << i << "['min_x'], 'max_x': _new_bbox_" << i << "['max_x'], 'min_y': _new_bbox_" << i << "['min_y'], 'max_y': _new_bbox_" << i << "['max_y']}\n";
            code << "                    break\n";


            // Compute move delta from known target position (avoids stale position reads)
            code << "        _dx_" << i << " = round(" << posX << " - _old_sym_x_" << i << ", 4)\n";
            code << "        _dy_" << i << " = round(" << posY << " - _old_sym_y_" << i << ", 4)\n";
            // Build pos_map by applying delta to old pin positions
            code << "        _pos_map_" << i << " = {}\n";
            code << "        if abs(_dx_" << i << ") > 0.001 or abs(_dy_" << i << ") > 0.001:\n";
            code << "            for _op in _old_pins_" << i << ":\n";
            code << "                _np = (round(_op[0] + _dx_" << i << ", 2), round(_op[1] + _dy_" << i << ", 2))\n";
            code << "                _pos_map_" << i << "[_op] = _np\n";
            code << "        if _pos_map_" << i << ":\n";
            code << "            _rnd = lambda v: round(v, 2)\n";
            code << "            def _mpos(p):\n";
            code << "                for k, v in _pos_map_" << i << ".items():\n";
            code << "                    if abs(p[0]-k[0]) < 0.05 and abs(p[1]-k[1]) < 0.05:\n";
            code << "                        return v\n";
            code << "                return None\n";
            // Pre-scan for overshoot: if a pin moves past the far end of a wire,
            // add the far end to pos_map so the connected wire gets an L-bend instead
            code << "            for _w in sch.crud.get_wires():\n";
            code << "                _ws2 = (_rnd(_w.start.x / 1e6), _rnd(_w.start.y / 1e6))\n";
            code << "                _we2 = (_rnd(_w.end.x / 1e6), _rnd(_w.end.y / 1e6))\n";
            code << "                _ns2 = _mpos(_ws2)\n";
            code << "                _ne2 = _mpos(_we2)\n";
            code << "                if _ns2 and not _ne2:\n";
            code << "                    if abs(_ws2[0]-_we2[0]) < 0.01 and (_ws2[1]-_we2[1])*(_ns2[1]-_we2[1]) < 0:\n";
            code << "                        _pos_map_" << i << "[_we2] = (_we2[0], _ns2[1])\n";
            code << "                    elif abs(_ws2[1]-_we2[1]) < 0.01 and (_ws2[0]-_we2[0])*(_ns2[0]-_we2[0]) < 0:\n";
            code << "                        _pos_map_" << i << "[_we2] = (_ns2[0], _we2[1])\n";
            code << "                elif _ne2 and not _ns2:\n";
            code << "                    if abs(_ws2[0]-_we2[0]) < 0.01 and (_we2[1]-_ws2[1])*(_ne2[1]-_ws2[1]) < 0:\n";
            code << "                        _pos_map_" << i << "[_ws2] = (_ws2[0], _ne2[1])\n";
            code << "                    elif abs(_ws2[1]-_we2[1]) < 0.01 and (_we2[0]-_ws2[0])*(_ne2[0]-_ws2[0]) < 0:\n";
            code << "                        _pos_map_" << i << "[_ws2] = (_ne2[0], _ws2[1])\n";
            // Update wires with orthogonal bend routing (like KiCad drag)
            code << "            _all_w = sch.crud.get_wires()\n";
            code << "            _rm_w = []\n";
            code << "            _add_segs = []\n";
            code << "            for _w in _all_w:\n";
            code << "                _ws = (_rnd(_w.start.x / 1e6), _rnd(_w.start.y / 1e6))\n";
            code << "                _we = (_rnd(_w.end.x / 1e6), _rnd(_w.end.y / 1e6))\n";
            code << "                _ns = _mpos(_ws)\n";
            code << "                _ne = _mpos(_we)\n";
            code << "                if _ns or _ne:\n";
            code << "                    _rm_w.append(_w)\n";
            code << "                    _s = _ns or _ws\n";
            code << "                    _e = _ne or _we\n";
            code << "                    if abs(_s[0] - _e[0]) < 0.01 or abs(_s[1] - _e[1]) < 0.01:\n";
            code << "                        _add_segs.append((_s, _e))\n";
            code << "                    else:\n";
            code << "                        _horiz = abs(_ws[1] - _we[1]) < abs(_ws[0] - _we[0])\n";
            code << "                        if (_ns and _horiz) or (_ne and not _horiz):\n";
            code << "                            _c = (_e[0], _s[1])\n";
            code << "                        else:\n";
            code << "                            _c = (_s[0], _e[1])\n";
            code << "                        _add_segs.append((_s, _c))\n";
            code << "                        _add_segs.append((_c, _e))\n";
            // Also check for wires passing THROUGH old pin positions (pin between start/end)
            code << "                elif not _ns and not _ne:\n";
            code << "                    _is_h = abs(_ws[1] - _we[1]) < 0.01\n";
            code << "                    _is_v = abs(_ws[0] - _we[0]) < 0.01\n";
            code << "                    if _is_h or _is_v:\n";
            code << "                        for _op, _np in _pos_map_" << i << ".items():\n";
            code << "                            if _is_h and abs(_op[1] - _ws[1]) < 0.01:\n";
            code << "                                _lo = min(_ws[0], _we[0])\n";
            code << "                                _hi = max(_ws[0], _we[0])\n";
            code << "                                if _lo + 0.01 < _op[0] < _hi - 0.01:\n";
            code << "                                    _rm_w.append(_w)\n";
            code << "                                    _add_segs.append((_ws, _np))\n";
            code << "                                    _add_segs.append((_np, _we))\n";
            code << "                                    break\n";
            code << "                            elif _is_v and abs(_op[0] - _ws[0]) < 0.01:\n";
            code << "                                _lo = min(_ws[1], _we[1])\n";
            code << "                                _hi = max(_ws[1], _we[1])\n";
            code << "                                if _lo + 0.01 < _op[1] < _hi - 0.01:\n";
            code << "                                    _rm_w.append(_w)\n";
            code << "                                    _add_segs.append((_ws, _np))\n";
            code << "                                    _add_segs.append((_np, _we))\n";
            code << "                                    break\n";
            // Filter out zero-length segments (from overshoot collapsing)
            code << "            _add_segs = [seg for seg in _add_segs if abs(seg[0][0]-seg[1][0]) > 0.01 or abs(seg[0][1]-seg[1][1]) > 0.01]\n";
            code << "            if _rm_w:\n";
            code << "                sch.crud.remove_items(_rm_w)\n";
            code << "                for (_s, _e) in _add_segs:\n";
            code << "                    sch.wiring.add_wire(Vector2.from_xy_mm(_s[0], _s[1]), Vector2.from_xy_mm(_e[0], _e[1]))\n";
            // Update junctions
            code << "            _all_j = sch.crud.get_junctions()\n";
            code << "            _rm_j = []\n";
            code << "            _add_j = []\n";
            code << "            for _j in _all_j:\n";
            code << "                _jp = (_rnd(_j.position.x / 1e6), _rnd(_j.position.y / 1e6))\n";
            code << "                _jm = _mpos(_jp)\n";
            code << "                if _jm:\n";
            code << "                    _rm_j.append(_j)\n";
            code << "                    _add_j.append(_jm)\n";
            code << "            if _rm_j:\n";
            code << "                sch.crud.remove_items(_rm_j)\n";
            code << "                for _np in _add_j:\n";
            code << "                    sch.wiring.add_junction(Vector2.from_xy_mm(_np[0], _np[1]))\n";
            // Update no-connects
            code << "            _all_nc = sch.crud.get_no_connects()\n";
            code << "            _rm_nc = []\n";
            code << "            _add_nc = []\n";
            code << "            for _nc in _all_nc:\n";
            code << "                _ncp = (_rnd(_nc.position.x / 1e6), _rnd(_nc.position.y / 1e6))\n";
            code << "                _ncm = _mpos(_ncp)\n";
            code << "                if _ncm:\n";
            code << "                    _rm_nc.append(_nc)\n";
            code << "                    _add_nc.append(_ncm)\n";
            code << "            if _rm_nc:\n";
            code << "                sch.crud.remove_items(_rm_nc)\n";
            code << "                for _np in _add_nc:\n";
            code << "                    sch.wiring.add_no_connect(Vector2.from_xy_mm(_np[0], _np[1]))\n";


            code << "        updated_" << i << " = True\n";
        }

        if( update.contains( "angle" ) )
        {
            double angle = update.value( "angle", 0.0 );
            code << "        current_angle_" << i << " = getattr(item_" << i << ", 'angle', 0) or 0\n";
            code << "        desired_angle_" << i << " = " << angle << "\n";
            code << "        delta_" << i << " = (desired_angle_" << i << " - current_angle_" << i << ") % 360\n";
            code << "        if delta_" << i << " != 0:\n";
            code << "            item_" << i << " = sch.symbols.rotate(item_" << i << ", delta_" << i << ")\n";
            code << "        updated_" << i << " = True\n";
        }

        if( update.contains( "properties" ) && update["properties"].is_object() )
        {
            code << "        props_" << i << " = " << update["properties"].dump() << "\n";
            code << "        if 'Value' in props_" << i << ":\n";
            code << "            sch.symbols.set_value(item_" << i << ", props_" << i << "['Value'])\n";
            code << "            updated_" << i << " = True\n";
            code << "        if 'Footprint' in props_" << i << ":\n";
            code << "            sch.symbols.set_footprint(item_" << i << ", props_" << i << "['Footprint'])\n";
            code << "            updated_" << i << " = True\n";
        }

        // Reposition text fields relative to symbol center
        if( update.contains( "fields" ) && update["fields"].is_object() )
        {
            code << "        sym_pos_" << i << " = item_" << i << ".position\n";

            for( auto& [fieldName, fieldSpec] : update["fields"].items() )
            {
                if( !fieldSpec.is_object() )
                    continue;

                bool hasOffset = fieldSpec.contains( "offset" ) &&
                                 fieldSpec["offset"].is_array() && fieldSpec["offset"].size() >= 2;
                bool hasAngle = fieldSpec.contains( "angle" ) && fieldSpec["angle"].is_number();

                if( !hasOffset && !hasAngle )
                    continue;

                code << "        for _f in item_" << i << "._proto.fields:\n";
                code << "            if _f.name == '" << EscapePythonString( fieldName ) << "':\n";

                if( hasOffset )
                {
                    double dx = SnapToGrid( fieldSpec["offset"][0].get<double>() );
                    double dy = SnapToGrid( fieldSpec["offset"][1].get<double>() );
                    code << "                _f.position.x_nm = sym_pos_" << i << ".x + round("
                         << dx << " * 1_000_000)\n";
                    code << "                _f.position.y_nm = sym_pos_" << i << ".y + round("
                         << dy << " * 1_000_000)\n";
                }

                if( hasAngle )
                {
                    double angle = fieldSpec["angle"].get<double>();
                    code << "                _f.attributes.angle.value_degrees = " << angle << "\n";
                }

                code << "                break\n";
            }

            code << "        _upd_" << i << " = sch.crud.update_items(item_" << i << ")\n";
            code << "        if _upd_" << i << ":\n";
            code << "            item_" << i << " = _upd_" << i << "[0]\n";
            code << "        updated_" << i << " = True\n";
        }

        // Build state info
        code << "        state_" << i << " = {}\n";
        code << "        if hasattr(item_" << i << ", 'position'):\n";
        code << "            pos = item_" << i << ".position\n";
        code << "            state_" << i << "['position'] = [pos.x / 1_000_000, pos.y / 1_000_000]\n";
        code << "        if hasattr(item_" << i << ", 'angle'):\n";
        code << "            state_" << i << "['angle'] = item_" << i << ".angle\n";
        code << "        if hasattr(item_" << i << ", 'reference'):\n";
        code << "            state_" << i << "['reference'] = item_" << i << ".reference\n";
        code << "        results.append({'index': " << i << ", 'target': target_" << i << ", 'updated': updated_" << i << ", 'state': state_" << i << "})\n";
        code << "    except Exception as e_" << i << ":\n";
        code << "        results.append({'index': " << i << ", 'target': '" << EscapePythonString( target ) << "', 'error': str(e_" << i << ")})\n";
        code << "\n";
    }

    code << "\n";
    code << "    _fail = sum(1 for r in results if 'error' in r)\n";
    code << "    result = {\n";
    code << "        'status': 'success' if _fail == 0 else 'partial',\n";
    code << "        'source': 'ipc',\n";
    code << "        'total': " << updates.size() << ",\n";
    code << "        'succeeded': len(results) - _fail,\n";
    code << "        'failed': _fail,\n";
    code << "        'results': results\n";
    code << "    }\n";
    code << "\n";
    code << "except Exception as batch_error:\n";
    code << "    result = {'status': 'error', 'message': str(batch_error), 'results': results}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateBatchDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "targets" ) || !aInput["targets"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'targets array is required'}))\n";
        return code.str();
    }

    auto targets = aInput["targets"];
    std::string filePath = aInput.value( "file_path", "" );
    bool cleanupWires = aInput.value( "cleanup_wires", true );
    bool chainDelete = aInput.value( "chain_delete", false );

    code << "import json, re, sys\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";

    // Separate string targets from query targets
    code << "string_targets = [";
    bool firstStr = true;
    for( size_t i = 0; i < targets.size(); ++i )
    {
        if( targets[i].is_string() )
        {
            if( !firstStr )
                code << ", ";
            code << "'" << EscapePythonString( targets[i].get<std::string>() ) << "'";
            firstStr = false;
        }
    }
    code << "]\n";

    code << "query_targets = [";
    bool firstQuery = true;
    for( size_t i = 0; i < targets.size(); ++i )
    {
        if( targets[i].is_object() )
        {
            if( !firstQuery )
                code << ", ";
            code << targets[i].dump();
            firstQuery = false;
        }
    }
    code << "]\n";

    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "cleanup_wires = " << ( cleanupWires ? "True" : "False" ) << "\n";
    code << "chain_delete = " << ( chainDelete ? "True" : "False" ) << "\n";
    code << "use_ipc = True\n";
    code << "result = None\n";
    code << "target_uuids = []\n";
    code << "\n";

    // IPC attempt
    code << "# Try IPC first\n";
    code << "try:\n";
    code << "    items_to_delete = []\n";
    code << "    not_found = []\n";
    code << "    query_not_found = []\n";
    code << "\n";

    // String target resolution (existing logic)
    code << "    for target in string_targets:\n";
    code << "        is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "        if is_uuid:\n";
    code << "            items = sch.crud.get_by_id([target])\n";
    code << "            if items:\n";
    code << "                items_to_delete.append(items[0])\n";
    code << "                target_uuids.append(target)\n";
    code << "            else:\n";
    code << "                not_found.append(target)\n";
    code << "        else:\n";
    code << "            item = sch.symbols.get_by_ref(target)\n";
    code << "            if item:\n";
    code << "                items_to_delete.append(item)\n";
    code << "                item_uuid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', getattr(item, 'uuid', '')))\n";
    code << "                target_uuids.append(item_uuid)\n";
    code << "            else:\n";
    code << "                not_found.append(target)\n";
    code << "\n";

    // Query target resolution (new)
    code << "    # Process query-based targets\n";
    code << "    def pos_match(actual_nm, expected_mm):\n";
    code << "        ax = round(actual_nm.x / 1e6, 2)\n";
    code << "        ay = round(actual_nm.y / 1e6, 2)\n";
    code << "        return abs(ax - expected_mm[0]) <= 0.01 and abs(ay - expected_mm[1]) <= 0.01\n";
    code << "\n";
    code << "    for q in query_targets:\n";
    code << "        q_type = q.get('type', '')\n";
    code << "        q_text = q.get('text', None)\n";
    code << "        q_pos = q.get('position', None)\n";
    code << "        q_start = q.get('start', None)\n";
    code << "        q_end = q.get('end', None)\n";
    code << "        matched = []\n";
    code << "\n";
    code << "        if q_type == 'wire':\n";
    code << "            for w in sch.crud.get_wires():\n";
    code << "                ok = True\n";
    code << "                if q_start is not None and not pos_match(w.start, q_start):\n";
    code << "                    ok = False\n";
    code << "                if q_end is not None and not pos_match(w.end, q_end):\n";
    code << "                    ok = False\n";
    code << "                if q_pos is not None:\n";
    code << "                    if not pos_match(w.start, q_pos) and not pos_match(w.end, q_pos):\n";
    code << "                        ok = False\n";
    code << "                if ok:\n";
    code << "                    matched.append(w)\n";
    code << "\n";
    code << "        elif q_type in ('label', 'global_label', 'hierarchical_label'):\n";
    code << "            type_map = {'label': 'NetLabel', 'global_label': 'GlobalLabel', 'hierarchical_label': 'HierLabel'}\n";
    code << "            expected_class = type_map.get(q_type, '')\n";
    code << "            for lbl in sch.labels.get_all():\n";
    code << "                ok = True\n";
    code << "                if expected_class and type(lbl).__name__ != expected_class:\n";
    code << "                    ok = False\n";
    code << "                if q_text is not None and getattr(lbl, 'text', '') != q_text:\n";
    code << "                    ok = False\n";
    code << "                if q_pos is not None and not pos_match(lbl.position, q_pos):\n";
    code << "                    ok = False\n";
    code << "                if ok:\n";
    code << "                    matched.append(lbl)\n";
    code << "\n";
    code << "        elif q_type == 'junction':\n";
    code << "            for j in sch.crud.get_junctions():\n";
    code << "                if q_pos is not None and not pos_match(j.position, q_pos):\n";
    code << "                    continue\n";
    code << "                matched.append(j)\n";
    code << "\n";
    code << "        elif q_type == 'no_connect':\n";
    code << "            for nc in sch.crud.get_no_connects():\n";
    code << "                if q_pos is not None and not pos_match(nc.position, q_pos):\n";
    code << "                    continue\n";
    code << "                matched.append(nc)\n";
    code << "\n";
    code << "        elif q_type == 'bus_entry':\n";
    code << "            be_list = []\n";
    code << "            try:\n";
    code << "                if hasattr(sch, 'buses') and hasattr(sch.buses, 'get_bus_entries'):\n";
    code << "                    be_list = sch.buses.get_bus_entries()\n";
    code << "                elif hasattr(sch.crud, 'get_bus_entries'):\n";
    code << "                    be_list = sch.crud.get_bus_entries()\n";
    code << "            except:\n";
    code << "                pass\n";
    code << "            for be in be_list:\n";
    code << "                if q_pos is not None and not pos_match(be.position, q_pos):\n";
    code << "                    continue\n";
    code << "                matched.append(be)\n";
    code << "\n";
    code << "        else:\n";
    code << "            query_not_found.append(q)\n";
    code << "            continue\n";
    code << "\n";
    code << "        if matched:\n";
    code << "            for m in matched:\n";
    code << "                items_to_delete.append(m)\n";
    code << "                uid = str(m.id.value) if hasattr(m, 'id') and hasattr(m.id, 'value') else str(getattr(m, 'id', ''))\n";
    code << "                target_uuids.append(uid)\n";
    code << "        else:\n";
    code << "            query_not_found.append(q)\n";
    code << "\n";
    // Chain delete: expand wire/junction/label targets to full net
    code << "    # Chain delete: expand wire/junction/label targets to full net\n";
    code << "    chain_deleted_nets = []\n";
    code << "    if chain_delete:\n";
    code << "        nets_to_delete = set()\n";
    code << "        for item in items_to_delete[:]:\n";
    code << "            item_type = type(item).__name__\n";
    code << "            if item_type in ('Wire', 'Junction', 'NetLabel', 'GlobalLabel', 'HierLabel'):\n";
    code << "                try:\n";
    code << "                    item_id = item.id.value if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', ''))\n";
    code << "                    net_resp = sch.connectivity.get_net_for_item(item_id)\n";
    code << "                    if net_resp.is_connected and net_resp.connection.name:\n";
    code << "                        net_name = net_resp.connection.name\n";
    code << "                        if 'unconnected' not in net_name.lower():\n";
    code << "                            nets_to_delete.add(net_name)\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "\n";
    code << "        existing_ids = set()\n";
    code << "        for item in items_to_delete:\n";
    code << "            uid = str(item.id.value) if hasattr(item, 'id') and hasattr(item.id, 'value') else str(getattr(item, 'id', ''))\n";
    code << "            existing_ids.add(uid)\n";
    code << "\n";
    code << "        for net_name in nets_to_delete:\n";
    code << "            try:\n";
    code << "                net_items_resp = sch.connectivity.get_net_items(net_name)\n";
    code << "                net_item_ids = [item_id.value for item_id in net_items_resp.item_ids]\n";
    code << "                new_ids = [nid for nid in net_item_ids if nid not in existing_ids]\n";
    code << "                if new_ids:\n";
    code << "                    net_items = sch.crud.get_by_id(new_ids)\n";
    code << "                    deletable_types = ('Wire', 'Junction', 'NetLabel', 'GlobalLabel', 'HierLabel', 'NoConnect')\n";
    code << "                    for ni in net_items:\n";
    code << "                        if type(ni).__name__ in deletable_types:\n";
    code << "                            items_to_delete.append(ni)\n";
    code << "                            uid = str(ni.id.value) if hasattr(ni, 'id') and hasattr(ni.id, 'value') else str(getattr(ni, 'id', ''))\n";
    code << "                            target_uuids.append(uid)\n";
    code << "                            existing_ids.add(uid)\n";
    code << "                # Also get labels with matching text\n";
    code << "                for lbl in sch.labels.get_all():\n";
    code << "                    if hasattr(lbl, 'text') and lbl.text == net_name:\n";
    code << "                        uid = str(lbl.id.value) if hasattr(lbl, 'id') and hasattr(lbl.id, 'value') else ''\n";
    code << "                        if uid and uid not in existing_ids:\n";
    code << "                            items_to_delete.append(lbl)\n";
    code << "                            target_uuids.append(uid)\n";
    code << "                            existing_ids.add(uid)\n";
    code << "                chain_deleted_nets.append(net_name)\n";
    code << "            except Exception as chain_err:\n";
    code << "                print(f'Chain delete warning for net {net_name}: {chain_err}', file=sys.stderr)\n";
    code << "\n";

    code << "    # Record pin positions before deletion (for optional orphan cleanup)\n";
    code << "    deleted_pin_positions = []\n";
    code << "    if cleanup_wires:\n";
    code << "        for item in items_to_delete:\n";
    code << "            if hasattr(item, 'pins'):\n";
    code << "                for p in item.pins:\n";
    code << "                    try:\n";
    code << "                        tp = sch.symbols.get_transformed_pin_position(item, p.number)\n";
    code << "                        if tp:\n";
    code << "                            dpx = round(tp['position'].x / 1_000_000, 4)\n";
    code << "                            dpy = round(tp['position'].y / 1_000_000, 4)\n";
    code << "                            deleted_pin_positions.append((dpx, dpy))\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "\n";
    code << "    if items_to_delete:\n";
    code << "        sch.crud.remove_items(items_to_delete)\n";
    code << "\n";
    code << "    # Recursively clean up orphaned wires, junctions, and power symbols\n";
    code << "    orphaned_wires = []\n";
    code << "    orphaned_junctions = []\n";
    code << "    orphaned_power = []\n";
    code << "    if deleted_pin_positions:\n";
    code << "        try:\n";
    code << "            rnd = lambda v: round(v, 2)\n";
    code << "            def wire_ep(w):\n";
    code << "                return (rnd(w.start.x/1e6), rnd(w.start.y/1e6)), (rnd(w.end.x/1e6), rnd(w.end.y/1e6))\n";
    code << "\n";
    code << "            # Collect all remaining connection points (symbol pins + labels)\n";
    code << "            conn_pts = set()\n";
    code << "            for sym in sch.symbols.get_all():\n";
    code << "                for p in sym.pins:\n";
    code << "                    try:\n";
    code << "                        tp = sch.symbols.get_transformed_pin_position(sym, p.number)\n";
    code << "                        if tp:\n";
    code << "                            conn_pts.add((rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6)))\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "            for lbl in sch.labels.get_all():\n";
    code << "                try:\n";
    code << "                    conn_pts.add((rnd(lbl.position.x/1e6), rnd(lbl.position.y/1e6)))\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "\n";
    code << "            dead = set((rnd(px), rnd(py)) for px, py in deleted_pin_positions)\n";
    code << "            checked = set()\n";
    code << "\n";
    code << "            for _iter in range(10):  # safety cap\n";
    code << "                to_check = dead - checked\n";
    code << "                if not to_check:\n";
    code << "                    break\n";
    code << "                checked |= to_check\n";
    code << "\n";
    code << "                cur_wires = sch.crud.get_wires()\n";
    code << "                cur_juncs = sch.crud.get_junctions()\n";
    code << "\n";
    code << "                rm_wires = []\n";
    code << "                freed = set()\n";
    code << "                for w in cur_wires:\n";
    code << "                    try:\n";
    code << "                        s, e = wire_ep(w)\n";
    code << "                        s_hit = s in to_check\n";
    code << "                        e_hit = e in to_check\n";
    code << "                        if s_hit or e_hit:\n";
    code << "                            rm_wires.append(w)\n";
    code << "                            uid = str(w.id.value) if hasattr(w, 'id') else ''\n";
    code << "                            orphaned_wires.append({'uuid': uid, 'start': list(s), 'end': list(e)})\n";
    code << "                            if s_hit:\n";
    code << "                                freed.add(e)\n";
    code << "                            if e_hit:\n";
    code << "                                freed.add(s)\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "\n";
    code << "                rm_juncs = []\n";
    code << "                for j in cur_juncs:\n";
    code << "                    try:\n";
    code << "                        jp = (rnd(j.position.x/1e6), rnd(j.position.y/1e6))\n";
    code << "                        if jp in to_check:\n";
    code << "                            rm_juncs.append(j)\n";
    code << "                            uid = str(j.id.value) if hasattr(j, 'id') else ''\n";
    code << "                            orphaned_junctions.append({'uuid': uid, 'position': list(jp)})\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "\n";
    code << "                if not rm_wires and not rm_juncs:\n";
    code << "                    break\n";
    code << "                sch.crud.remove_items(rm_wires + rm_juncs)\n";
    code << "\n";
    code << "                # Cascade: freed endpoints become dead if not at a component pin\n";
    code << "                # and have <= 1 remaining wire (i.e. dangling)\n";
    code << "                remaining = sch.crud.get_wires()\n";
    code << "                for fp in freed - conn_pts - checked:\n";
    code << "                    wc = 0\n";
    code << "                    for w in remaining:\n";
    code << "                        try:\n";
    code << "                            s, e = wire_ep(w)\n";
    code << "                            if s == fp or e == fp:\n";
    code << "                                wc += 1\n";
    code << "                        except:\n";
    code << "                            pass\n";
    code << "                    if wc <= 1:\n";
    code << "                        dead.add(fp)\n";
    code << "\n";
    code << "            # Check for orphaned power symbols (#PWR) after wire cleanup\n";
    code << "            remaining_wires = sch.crud.get_wires()\n";
    code << "            wire_pts = set()\n";
    code << "            for w in remaining_wires:\n";
    code << "                try:\n";
    code << "                    s, e = wire_ep(w)\n";
    code << "                    wire_pts.add(s)\n";
    code << "                    wire_pts.add(e)\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "            for sym in sch.symbols.get_all():\n";
    code << "                try:\n";
    code << "                    if not sym.reference.startswith('#PWR'):\n";
    code << "                        continue\n";
    code << "                    connected = False\n";
    code << "                    for p in sym.pins:\n";
    code << "                        try:\n";
    code << "                            tp = sch.symbols.get_transformed_pin_position(sym, p.number)\n";
    code << "                            if tp:\n";
    code << "                                pp = (rnd(tp['position'].x/1e6), rnd(tp['position'].y/1e6))\n";
    code << "                                if pp in wire_pts:\n";
    code << "                                    connected = True\n";
    code << "                                    break\n";
    code << "                        except:\n";
    code << "                            pass\n";
    code << "                    if not connected:\n";
    code << "                        orphaned_power.append({'ref': sym.reference, 'uuid': str(sym.id.value) if hasattr(sym, 'id') else ''})\n";
    code << "                except:\n";
    code << "                    pass\n";
    code << "            if orphaned_power:\n";
    code << "                pwr_items = []\n";
    code << "                for op in orphaned_power:\n";
    code << "                    s = sch.symbols.get_by_ref(op['ref'])\n";
    code << "                    if s:\n";
    code << "                        pwr_items.append(s)\n";
    code << "                if pwr_items:\n";
    code << "                    sch.crud.remove_items(pwr_items)\n";
    code << "\n";
    code << "        except Exception as cleanup_err:\n";
    code << "            print(f'Orphan cleanup warning: {cleanup_err}', file=sys.stderr)\n";
    code << "\n";
    code << "    result = {'status': 'success', 'source': 'ipc', 'deleted': len(items_to_delete)}\n";
    code << "    if orphaned_wires:\n";
    code << "        result['orphaned_wires_removed'] = len(orphaned_wires)\n";
    code << "        result['orphaned_wires'] = orphaned_wires\n";
    code << "    if orphaned_junctions:\n";
    code << "        result['orphaned_junctions_removed'] = len(orphaned_junctions)\n";
    code << "    if orphaned_power:\n";
    code << "        result['orphaned_power_removed'] = len(orphaned_power)\n";
    code << "        result['orphaned_power'] = orphaned_power\n";
    code << "    if not_found:\n";
    code << "        result['not_found'] = not_found\n";
    code << "    if query_not_found:\n";
    code << "        result['queries_not_matched'] = query_not_found\n";
    code << "    if chain_deleted_nets:\n";
    code << "        result['chain_deleted_nets'] = chain_deleted_nets\n";
    code << "\n";
    code << "except Exception as ipc_error:\n";
    code << "    use_ipc = False\n";
    code << "    ipc_error_msg = str(ipc_error)\n";
    code << "    print(f'IPC failed: {ipc_error_msg}', file=sys.stderr)\n";
    code << "\n";

    // Check if editor is still open before attempting file fallback
    code << GenerateEditorOpenCheck();
    code << "\n";

    // File fallback - only if editor is closed
    code << "# File-based fallback if IPC failed AND editor is closed\n";
    code << "if not use_ipc and editor_still_open:\n";
    code << "    # Editor is open - don't mix IPC and file operations\n";
    code << "    result = {'status': 'error', 'message': f'IPC failed but schematic editor is still open. Close the editor to use file-based operations, or investigate the IPC error: {ipc_error_msg}'}\n";
    code << "elif not use_ipc and file_path and target_uuids:\n";
    code << "    try:\n";
    code << GenerateFileFallbackHeader();
    code << "        deleted_count = 0\n";
    code << "        for uuid in target_uuids:\n";
    code << "            if delete_element_from_file(file_path, uuid):\n";
    code << "                deleted_count += 1\n";
    code << "        result = {'status': 'success', 'source': 'file', 'deleted': deleted_count}\n";
    code << "    except Exception as file_error:\n";
    code << "        result = {'status': 'error', 'message': f'File fallback failed (editor closed). File error: {str(file_error)}'}\n";
    code << "elif not use_ipc:\n";
    code << "    result = {'status': 'error', 'message': f'IPC failed: {ipc_error_msg}'}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateSwitchSheetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string sheetPath = aInput.value( "sheet_path", "" );

    code << "import json, sys\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "sheet_path = " << nlohmann::json( sheetPath ).dump() << "\n";
    code << "result = None\n";
    code << "\n";
    code << "try:\n";
    code << "    # First try get_hierarchy which provides proper paths for navigation\n";
    code << "    hierarchy_tree = None\n";
    code << "    hierarchy_nodes = []  # Flattened list of (node, path_str) tuples\n";
    code << "    \n";
    code << "    def flatten_hierarchy(node, parent_path=''):\n";
    code << "        \"\"\"Recursively flatten hierarchy tree into list of nodes with path strings.\"\"\"\n";
    code << "        nodes = []\n";
    code << "        name = getattr(node, 'name', '') or ''\n";
    code << "        uuid = ''\n";
    code << "        if hasattr(node, 'path') and node.path:\n";
    code << "            sp = node.path\n";
    code << "            if hasattr(sp, 'path') and sp.path:\n";
    code << "                uuid = sp.path[-1].value if sp.path else ''\n";
    code << "        \n";
    code << "        current_path = parent_path + '/' + uuid if uuid else parent_path\n";
    code << "        if not current_path:\n";
    code << "            current_path = '/'\n";
    code << "        \n";
    code << "        nodes.append((node, name, uuid, current_path))\n";
    code << "        \n";
    code << "        if hasattr(node, 'children'):\n";
    code << "            for child in node.children:\n";
    code << "                nodes.extend(flatten_hierarchy(child, current_path))\n";
    code << "        return nodes\n";
    code << "    \n";
    code << "    if hasattr(sch.sheets, 'get_hierarchy'):\n";
    code << "        try:\n";
    code << "            hierarchy_tree = sch.sheets.get_hierarchy()\n";
    code << "            hierarchy_nodes = flatten_hierarchy(hierarchy_tree)\n";
    code << "            print(f'[sch_switch_sheet] Hierarchy tree has {len(hierarchy_nodes)} nodes', file=sys.stderr)\n";
    code << "        except Exception as he:\n";
    code << "            print(f'[sch_switch_sheet] get_hierarchy failed: {he}', file=sys.stderr)\n";
    code << "    \n";
    code << "    # Also get sheet items for fallback\n";
    code << "    sheets = sch.crud.get_sheets()\n";
    code << "    print(f'[sch_switch_sheet] Found {len(sheets)} sheet items', file=sys.stderr)\n";
    code << "    \n";
    code << "    # Build lookup dictionaries\n";
    code << "    hierarchy = []\n";
    code << "    sheet_by_name = {}\n";
    code << "    sheet_by_file = {}\n";
    code << "    sheet_by_uuid = {}\n";
    code << "    \n";
    code << "    # Prefer hierarchy nodes (they have proper paths for navigation)\n";
    code << "    for node, name, uuid, path_str in hierarchy_nodes:\n";
    code << "        filename = getattr(node, 'filename', '') or ''\n";
    code << "        info = {'name': name, 'file': filename, 'uuid': uuid, 'path': path_str}\n";
    code << "        hierarchy.append(info)\n";
    code << "        if name:\n";
    code << "            sheet_by_name[name] = (node, info)\n";
    code << "        if filename:\n";
    code << "            sheet_by_file[filename] = (node, info)\n";
    code << "        if uuid:\n";
    code << "            sheet_by_uuid[uuid] = (node, info)\n";
    code << "    \n";
    code << "    # Add any sheet items not in hierarchy (fallback)\n";
    code << "    for sheet in sheets:\n";
    code << "        name = getattr(sheet, 'name', '')\n";
    code << "        filename = getattr(sheet, 'filename', '')\n";
    code << "        uuid = str(sheet.id.value) if hasattr(sheet, 'id') and hasattr(sheet.id, 'value') else str(getattr(sheet, 'id', getattr(sheet, 'uuid', '')))\n";
    code << "        if uuid and uuid not in sheet_by_uuid:\n";
    code << "            info = {'name': name, 'file': filename, 'uuid': uuid}\n";
    code << "            hierarchy.append(info)\n";
    code << "            if name:\n";
    code << "                sheet_by_name[name] = (sheet, info)\n";
    code << "            if filename:\n";
    code << "                sheet_by_file[filename] = (sheet, info)\n";
    code << "            sheet_by_uuid[uuid] = (sheet, info)\n";
    code << "    \n";
    code << "    target_sheet = None\n";
    code << "    target_info = None\n";
    code << "    navigated = False\n";
    code << "    \n";
    code << "    # Handle sheet_path - can be '/' for root, or '/uuid1/uuid2' format, or sheet name\n";
    code << "    if sheet_path:\n";
    code << "        if sheet_path == '/':\n";
    code << "            # Navigate to root sheet\n";
    code << "            if hasattr(sch.sheets, 'navigate_to_root'):\n";
    code << "                sch.sheets.navigate_to_root()\n";
    code << "                navigated = True\n";
    code << "            elif hasattr(sch.sheets, 'leave') and hasattr(sch.sheets, 'get_current_path'):\n";
    code << "                # Leave all sheets until at root\n";
    code << "                while True:\n";
    code << "                    path = sch.sheets.get_current_path() if hasattr(sch.sheets, 'get_current_path') else '/'\n";
    code << "                    if path == '/' or not hasattr(sch.sheets, 'leave'):\n";
    code << "                        break\n";
    code << "                    sch.sheets.leave()\n";
    code << "                navigated = True\n";
    code << "            target_info = {'name': 'Root', 'file': '', 'uuid': '', 'path': '/'}\n";
    code << "        elif sheet_path.startswith('/'):\n";
    code << "            # Path format - could be '/uuid1/uuid2' or '/name' or '/name/'\n";
    code << "            parts = [p for p in sheet_path.split('/') if p]\n";
    code << "            if parts:\n";
    code << "                last_part = parts[-1]\n";
    code << "                # Try as UUID first (full path format)\n";
    code << "                if last_part in sheet_by_uuid:\n";
    code << "                    target_sheet, target_info = sheet_by_uuid[last_part]\n";
    code << "                # Try as sheet name (e.g., '/Power/')\n";
    code << "                elif last_part in sheet_by_name:\n";
    code << "                    target_sheet, target_info = sheet_by_name[last_part]\n";
    code << "                # Try as filename (e.g., '/Power.kicad_sch/')\n";
    code << "                elif last_part in sheet_by_file:\n";
    code << "                    target_sheet, target_info = sheet_by_file[last_part]\n";
    code << "                # Try adding .kicad_sch extension\n";
    code << "                elif last_part + '.kicad_sch' in sheet_by_file:\n";
    code << "                    target_sheet, target_info = sheet_by_file[last_part + '.kicad_sch']\n";
    code << "        else:\n";
    code << "            # Try as sheet name first\n";
    code << "            if sheet_path in sheet_by_name:\n";
    code << "                target_sheet, target_info = sheet_by_name[sheet_path]\n";
    code << "            # Try as UUID\n";
    code << "            elif sheet_path in sheet_by_uuid:\n";
    code << "                target_sheet, target_info = sheet_by_uuid[sheet_path]\n";
    code << "            # Try as filename\n";
    code << "            elif sheet_path in sheet_by_file:\n";
    code << "                target_sheet, target_info = sheet_by_file[sheet_path]\n";
    code << "            # Try adding .kicad_sch extension\n";
    code << "            elif sheet_path + '.kicad_sch' in sheet_by_file:\n";
    code << "                target_sheet, target_info = sheet_by_file[sheet_path + '.kicad_sch']\n";
    code << "    \n";
    code << "    # Navigate to target sheet if found\n";
    code << "    if target_sheet and not navigated:\n";
    code << "        print(f'[sch_switch_sheet] Attempting to navigate to sheet', file=sys.stderr)\n";
    code << "        # First try navigate_to with the SheetPath (most reliable for hierarchy nodes)\n";
    code << "        if hasattr(sch.sheets, 'navigate_to') and hasattr(target_sheet, 'path') and target_sheet.path:\n";
    code << "            try:\n";
    code << "                print(f'[sch_switch_sheet] Using navigate_to with path', file=sys.stderr)\n";
    code << "                sch.sheets.navigate_to(target_sheet.path)\n";
    code << "                navigated = True\n";
    code << "            except Exception as nav_err:\n";
    code << "                print(f'[sch_switch_sheet] navigate_to failed: {nav_err}', file=sys.stderr)\n";
    code << "        \n";
    code << "        if not navigated and hasattr(sch.sheets, 'enter'):\n";
    code << "            try:\n";
    code << "                # enter might work with the node directly\n";
    code << "                print(f'[sch_switch_sheet] Trying enter method', file=sys.stderr)\n";
    code << "                sch.sheets.enter(target_sheet)\n";
    code << "                navigated = True\n";
    code << "            except Exception as enter_err:\n";
    code << "                print(f'[sch_switch_sheet] enter failed: {enter_err}', file=sys.stderr)\n";
    code << "        \n";
    code << "        if not navigated and hasattr(sch.sheets, 'open'):\n";
    code << "            try:\n";
    code << "                print(f'[sch_switch_sheet] Trying open method', file=sys.stderr)\n";
    code << "                sch.sheets.open(target_sheet)\n";
    code << "                navigated = True\n";
    code << "            except Exception as open_err:\n";
    code << "                print(f'[sch_switch_sheet] open failed: {open_err}', file=sys.stderr)\n";
    code << "    \n";
    code << "    # Build result\n";
    code << "    if navigated:\n";
    code << "        result = {\n";
    code << "            'status': 'success',\n";
    code << "            'action': 'navigated',\n";
    code << "            'target': target_info,\n";
    code << "            'available_sheets': hierarchy\n";
    code << "        }\n";
    code << "    elif not sheet_path:\n";
    code << "        # No target specified - list available sheets\n";
    code << "        if not hierarchy:\n";
    code << "            result = {\n";
    code << "                'status': 'info',\n";
    code << "                'message': 'Flat schematic with no hierarchical sheets.',\n";
    code << "                'is_flat_design': True,\n";
    code << "                'available_sheets': []\n";
    code << "            }\n";
    code << "        else:\n";
    code << "            result = {\n";
    code << "                'status': 'success',\n";
    code << "                'message': 'Listing available sheets. Use sheet_path to navigate.',\n";
    code << "                'available_sheets': hierarchy\n";
    code << "            }\n";
    code << "    else:\n";
    code << "        # Target specified but not found\n";
    code << "        result = {\n";
    code << "            'status': 'error',\n";
    code << "            'message': f'Sheet not found: {sheet_path}',\n";
    code << "            'available_sheets': hierarchy\n";
    code << "        }\n";
    code << "\n";
    code << "except Exception as e:\n";
    code << "    import traceback\n";
    code << "    result = {'status': 'error', 'message': str(e), 'traceback': traceback.format_exc()}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateAddSheetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string sheetName = aInput.value( "sheet_name", "Subsheet" );
    std::string sheetFile = aInput.value( "sheet_file", "" );

    double posX = 0, posY = 0;
    if( aInput.contains( "position" ) && aInput["position"].is_array() &&
        aInput["position"].size() >= 2 )
    {
        posX = SnapToGrid( aInput["position"][0].get<double>() );
        posY = SnapToGrid( aInput["position"][1].get<double>() );
    }

    double sizeW = 50, sizeH = 50;
    if( aInput.contains( "size" ) && aInput["size"].is_array() && aInput["size"].size() >= 2 )
    {
        sizeW = aInput["size"][0].get<double>();
        sizeH = aInput["size"][1].get<double>();
    }

    code << R"(import json, sys
from kipy.geometry import Vector2

)";
    code << GenerateRefreshPreamble();
    code << R"(
def get_id(obj):
    if obj is None:
        return ''
    if hasattr(obj, 'id'):
        id_obj = obj.id
        if hasattr(id_obj, 'value'):
            return str(id_obj.value)
        return str(id_obj)
    if hasattr(obj, 'uuid'):
        return str(obj.uuid)
    if isinstance(obj, str):
        return obj
    return str(obj)

try:
)";
    code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
    code << "    size = Vector2.from_xy_mm(" << sizeW << ", " << sizeH << ")\n";
    code << "    sheet = sch.sheets.create(\n";
    code << "        name='" << EscapePythonString( sheetName ) << "',\n";
    code << "        filename='" << EscapePythonString( sheetFile.empty() ? sheetName + ".kicad_sch" : sheetFile ) << "',\n";
    code << "        position=pos,\n";
    code << "        size=size\n";
    code << "    )\n";
    code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(sheet), 'name': '" << EscapePythonString( sheetName ) << "'}\n";
    code << R"(
except Exception as e:
    result = {'status': 'error', 'message': str(e)}

print(json.dumps(result, indent=2))
)";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateAddBatchCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "elements" ) || !aInput["elements"].is_array() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'elements array is required'}))\n";
        return code.str();
    }

    auto elements = aInput["elements"];

    code << "import json, sys, re\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "# Build map of used references for auto-numbering\n";
    code << "used_refs = {}\n";
    code << "for _s in sch.symbols.get_all():\n";
    code << "    _r = getattr(_s, 'reference', '')\n";
    code << "    _m = re.match(r'^([A-Za-z#]+)(\\d+)$', _r)\n";
    code << "    if _m:\n";
    code << "        used_refs.setdefault(_m.group(1), set()).add(int(_m.group(2)))\n";
    code << "\n";
    code << "def next_ref(prefix):\n";
    code << "    nums = used_refs.get(prefix, set())\n";
    code << "    n = 1\n";
    code << "    while n in nums:\n";
    code << "        n += 1\n";
    code << "    used_refs.setdefault(prefix, set()).add(n)\n";
    code << "    return f'{prefix}{n}'\n";
    code << "\n";
    code << "results = []\n";
    code << "_placed_syms = {}\n";
    code << "_placed_wires = []\n";
    code << "\n";

    // --- Overlap detection preamble ---
    code << "# Collect bounding boxes of all existing symbols and labels for overlap detection\n";
    code << "_BBOX_MARGIN = " << BBOX_MARGIN_MM << "\n";
    code << "placed_bboxes = []\n";
    code << "try:\n";
    code << "    _all_existing = sch.symbols.get_all()\n";
    code << "    for _esym in _all_existing:\n";
    code << "        try:\n";
    code << "            _ebb = sch.transform.get_bounding_box(_esym, units='mm', include_text=False)\n";
    code << "        except:\n";
    code << "            continue\n";
    code << "        if _ebb:\n";
    code << "            placed_bboxes.append({'ref': getattr(_esym, 'reference', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})\n";
    code << "except:\n";
    code << "    pass\n";
    code << "try:\n";
    code << "    for _elbl in sch.labels.get_all():\n";
    code << "        try:\n";
    code << "            _ebb = sch.transform.get_bounding_box(_elbl, units='mm', include_text=False)\n";
    code << "        except:\n";
    code << "            continue\n";
    code << "        if _ebb:\n";
    code << "            placed_bboxes.append({'ref': getattr(_elbl, 'text', '?'), 'min_x': _ebb['min_x'] - _BBOX_MARGIN, 'max_x': _ebb['max_x'] + _BBOX_MARGIN, 'min_y': _ebb['min_y'] - _BBOX_MARGIN, 'max_y': _ebb['max_y'] + _BBOX_MARGIN})\n";
    code << "except:\n";
    code << "    pass\n";
    code << "\n";
    code << "def _bboxes_overlap(a, b):\n";
    code << "    return a['min_x'] < b['max_x'] and a['max_x'] > b['min_x'] and a['min_y'] < b['max_y'] and a['max_y'] > b['min_y']\n";
    code << "\n";

    code << "try:\n";

    // Process each element in the batch
    for( size_t i = 0; i < elements.size(); ++i )
    {
        auto elem = elements[i];
        std::string elementType = elem.value( "element_type", "" );

        code << "    # Element " << i << ": " << elementType << "\n";
        code << "    try:\n";

        if( elementType == "symbol" )
        {
            std::string libId = elem.value( "lib_id", "" );
            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }
            double angle = elem.value( "angle", 0.0 );
            std::string mirror = elem.value( "mirror", "none" );
            bool mirrorX = ( mirror == "x" );
            bool mirrorY = ( mirror == "y" );
            int unit = elem.value( "unit", 1 );

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        sym_" << i << " = sch.symbols.add(\n";
            code << "            lib_id='" << EscapePythonString( libId ) << "',\n";
            code << "            position=pos_" << i << ",\n";
            code << "            unit=" << unit << ",\n";
            code << "            angle=" << angle << ",\n";
            code << "            mirror_x=" << ( mirrorX ? "True" : "False" ) << ",\n";
            code << "            mirror_y=" << ( mirrorY ? "True" : "False" ) << "\n";
            code << "        )\n";

            // Handle properties
            if( elem.contains( "properties" ) && elem["properties"].is_object() )
            {
                code << "        props_" << i << " = " << elem["properties"].dump() << "\n";
                code << "        if 'Value' in props_" << i << ":\n";
                code << "            sch.symbols.set_value(sym_" << i << ", props_" << i << "['Value'])\n";
                code << "        if 'Footprint' in props_" << i << ":\n";
                code << "            sch.symbols.set_footprint(sym_" << i << ", props_" << i << "['Footprint'])\n";
            }

            // --- Overlap check for symbol ---
            code << "        _overlap_" << i << " = False\n";
            code << "        try:\n";
            code << "            _bb_" << i << " = sch.transform.get_bounding_box(sym_" << i << ", units='mm', include_text=False)\n";
            code << "            if _bb_" << i << ":\n";
            code << "                _new_bbox_" << i << " = {'min_x': _bb_" << i << "['min_x'] - _BBOX_MARGIN, 'max_x': _bb_" << i << "['max_x'] + _BBOX_MARGIN, 'min_y': _bb_" << i << "['min_y'] - _BBOX_MARGIN, 'max_y': _bb_" << i << "['max_y'] + _BBOX_MARGIN}\n";
            code << "                _obstacle_ref_" << i << " = '?'\n";
            code << "                for _pb in placed_bboxes:\n";
            code << "                    if _bboxes_overlap(_new_bbox_" << i << ", _pb):\n";
            code << "                        _overlap_" << i << " = True\n";
            code << "                        _obstacle_ref_" << i << " = _pb.get('ref', '?')\n";
            code << "                        break\n";
            code << "        except:\n";
            code << "            pass\n";
            code << "        if _overlap_" << i << ":\n";
            code << "            sch.crud.remove_items([sym_" << i << "])\n";
            code << "            results.append({'index': " << i << ", 'error': f'Placement rejected: overlaps {_obstacle_ref_" << i << "}'})\n";
            code << "        else:\n";
            code << "            _prefix_" << i << " = re.match(r'^([A-Za-z#]+)', getattr(sym_" << i << ", 'reference', 'X')).group(1)\n";
            code << "            _new_ref_" << i << " = next_ref(_prefix_" << i << ")\n";
            code << "            for _f in sym_" << i << "._proto.fields:\n";
            code << "                if _f.name == 'Reference':\n";
            code << "                    _f.text = _new_ref_" << i << "\n";
            code << "                    break\n";
            code << "            sch.crud.update_items(sym_" << i << ")\n";
            code << "            if _bb_" << i << ":\n";
            code << "                placed_bboxes.append({'ref': _new_ref_" << i << ", **_new_bbox_" << i << "})\n";
            code << "            _placed_syms[" << i << "] = sym_" << i << "\n";
            code << "            results.append({'index': " << i << ", 'element_type': 'symbol', 'reference': _new_ref_" << i << "})\n";
        }
        else if( elementType == "power" )
        {
            std::string libId = elem.value( "lib_id", "" );
            std::string powerName = libId;
            size_t colonPos = libId.find( ':' );
            if( colonPos != std::string::npos )
                powerName = libId.substr( colonPos + 1 );

            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }
            double angle = elem.value( "angle", 0.0 );

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        pwr_" << i << " = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pos_" << i << ", angle=" << angle << ")\n";

            // --- Overlap check for power ---
            code << "        _overlap_" << i << " = False\n";
            code << "        try:\n";
            code << "            _bb_" << i << " = sch.transform.get_bounding_box(pwr_" << i << ", units='mm', include_text=False)\n";
            code << "            if _bb_" << i << ":\n";
            code << "                _new_bbox_" << i << " = {'min_x': _bb_" << i << "['min_x'] - _BBOX_MARGIN, 'max_x': _bb_" << i << "['max_x'] + _BBOX_MARGIN, 'min_y': _bb_" << i << "['min_y'] - _BBOX_MARGIN, 'max_y': _bb_" << i << "['max_y'] + _BBOX_MARGIN}\n";
            code << "                _obstacle_ref_" << i << " = '?'\n";
            code << "                for _pb in placed_bboxes:\n";
            code << "                    if _bboxes_overlap(_new_bbox_" << i << ", _pb):\n";
            code << "                        _overlap_" << i << " = True\n";
            code << "                        _obstacle_ref_" << i << " = _pb.get('ref', '?')\n";
            code << "                        break\n";
            code << "        except:\n";
            code << "            pass\n";
            code << "        if _overlap_" << i << ":\n";
            code << "            sch.crud.remove_items([pwr_" << i << "])\n";
            code << "            results.append({'index': " << i << ", 'error': f'Placement rejected: overlaps {_obstacle_ref_" << i << "}'})\n";
            code << "        else:\n";
            code << "            _pwr_ref_" << i << " = next_ref('#PWR')\n";
            code << "            for _f in pwr_" << i << "._proto.fields:\n";
            code << "                if _f.name == 'Reference':\n";
            code << "                    _f.text = _pwr_ref_" << i << "\n";
            code << "                    break\n";
            code << "            sch.crud.update_items(pwr_" << i << ")\n";
            code << "            if _bb_" << i << ":\n";
            code << "                placed_bboxes.append({'ref': _pwr_ref_" << i << ", **_new_bbox_" << i << "})\n";
            code << "            results.append({'index': " << i << ", 'element_type': 'power', 'reference': _pwr_ref_" << i << "})\n";
        }
        else if( elementType == "label" )
        {
            std::string text = elem.value( "text", "" );
            std::string labelType = elem.value( "label_type", "local" );

            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";

            if( labelType == "global" )
                code << "        lbl_" << i << " = sch.labels.add_global('" << EscapePythonString( text ) << "', pos_" << i << ")\n";
            else if( labelType == "hierarchical" )
                code << "        lbl_" << i << " = sch.labels.add_hierarchical('" << EscapePythonString( text ) << "', pos_" << i << ")\n";
            else
                code << "        lbl_" << i << " = sch.labels.add_local('" << EscapePythonString( text ) << "', pos_" << i << ")\n";

            // --- Overlap check for label ---
            code << "        _overlap_" << i << " = False\n";
            code << "        try:\n";
            code << "            _bb_" << i << " = sch.transform.get_bounding_box(lbl_" << i << ", units='mm', include_text=False)\n";
            code << "            if _bb_" << i << ":\n";
            code << "                _new_bbox_" << i << " = {'min_x': _bb_" << i << "['min_x'] - _BBOX_MARGIN, 'max_x': _bb_" << i << "['max_x'] + _BBOX_MARGIN, 'min_y': _bb_" << i << "['min_y'] - _BBOX_MARGIN, 'max_y': _bb_" << i << "['max_y'] + _BBOX_MARGIN}\n";
            code << "                _obstacle_ref_" << i << " = '?'\n";
            code << "                for _pb in placed_bboxes:\n";
            code << "                    if _bboxes_overlap(_new_bbox_" << i << ", _pb):\n";
            code << "                        _overlap_" << i << " = True\n";
            code << "                        _obstacle_ref_" << i << " = _pb.get('ref', '?')\n";
            code << "                        break\n";
            code << "        except:\n";
            code << "            pass\n";
            code << "        if _overlap_" << i << ":\n";
            code << "            sch.crud.remove_items([lbl_" << i << "])\n";
            code << "            results.append({'index': " << i << ", 'error': f'Placement rejected: overlaps {_obstacle_ref_" << i << "}'})\n";
            code << "        else:\n";
            code << "            if _bb_" << i << ":\n";
            code << "                placed_bboxes.append({'ref': '" << EscapePythonString( elem.value( "text", "label" ) ) << "', **_new_bbox_" << i << "})\n";
            code << "            results.append({'index': " << i << ", 'element_type': 'label'})\n";
        }
        else if( elementType == "wire" )
        {
            // Handle wire with points — place segments between consecutive coordinate pairs
            if( elem.contains( "points" ) && elem["points"].is_array() &&
                elem["points"].size() >= 2 )
            {
                auto points = elem["points"];

                // Build list of snapped (x,y) tuples
                code << "        _wpts_" << i << " = [\n";
                for( size_t j = 0; j < points.size(); ++j )
                {
                    if( points[j].is_array() && points[j].size() >= 2 )
                    {
                        double x = SnapToGrid( points[j][0].get<double>() );
                        double y = SnapToGrid( points[j][1].get<double>() );
                        code << "            (" << x << ", " << y << "),\n";
                    }
                }
                code << "        ]\n";

                // Check each wire segment against component bounding boxes
                code << "        _wire_blocked_" << i << " = False\n";
                code << "        _wire_obstacle_" << i << " = '?'\n";
                code << "        for _si in range(len(_wpts_" << i << ") - 1):\n";
                code << "            _wa, _wb = _wpts_" << i << "[_si], _wpts_" << i << "[_si + 1]\n";
                code << "            for _pb in placed_bboxes:\n";
                code << "                _ax, _ay = min(_wa[0], _wb[0]), min(_wa[1], _wb[1])\n";
                code << "                _bx, _by = max(_wa[0], _wb[0]), max(_wa[1], _wb[1])\n";
                code << "                if _bboxes_overlap({'min_x': _ax, 'max_x': _bx, 'min_y': _ay, 'max_y': _by}, _pb):\n";
                code << "                    _wire_blocked_" << i << " = True\n";
                code << "                    _wire_obstacle_" << i << " = _pb.get('ref', '?')\n";
                code << "                    break\n";
                code << "            if _wire_blocked_" << i << ": break\n";
                code << "        if _wire_blocked_" << i << ":\n";
                code << "            results.append({'index': " << i << ", 'error': f'Wire rejected: crosses {_wire_obstacle_" << i << "}'})\n";
                code << "        else:\n";
                code << "            _wc_" << i << " = 0\n";
                code << "            for _si in range(len(_wpts_" << i << ") - 1):\n";
                code << "                _w = sch.wiring.add_wire(Vector2.from_xy_mm(*_wpts_" << i << "[_si]), Vector2.from_xy_mm(*_wpts_" << i << "[_si + 1]))\n";
                code << "                _placed_wires.append(_w)\n";
                code << "                _wc_" << i << " += 1\n";
                code << "            results.append({'index': " << i << ", 'element_type': 'wire', 'segments': _wc_" << i << "})\n";
            }
            else
            {
                code << "        results.append({'index': " << i << ", 'error': 'Wire requires points array with at least 2 coordinate pairs'})\n";
            }
        }
        else if( elementType == "no_connect" )
        {
            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        nc_" << i << " = sch.wiring.add_no_connect(pos_" << i << ")\n";
            code << "        results.append({'index': " << i << ", 'element_type': 'no_connect'})\n";
        }
        else if( elementType == "bus_entry" )
        {
            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }

            std::string direction = elem.value( "direction", "right_down" );

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        be_" << i << " = sch.buses.add_bus_entry(pos_" << i << ", direction='" << EscapePythonString( direction ) << "')\n";
            code << "        results.append({'index': " << i << ", 'element_type': 'bus_entry'})\n";
        }
        else
        {
            code << "        results.append({'index': " << i << ", 'error': 'Unknown element_type: " << EscapePythonString( elementType ) << "'})\n";
        }

        code << "    except Exception as e_" << i << ":\n";
        code << "        results.append({'index': " << i << ", 'error': str(e_" << i << ")})\n";
        code << "\n";
    }

    // --- Pin position enrichment (best-effort, after all placements) ---
    // Done as a separate pass so placement is never blocked by pin lookups.
    // Each pin lookup is an IPC round-trip, so we wrap individually to degrade gracefully.
    code << "\n";
    code << "    # --- Collect pin positions for placed symbols (best-effort) ---\n";
    code << "    for _idx, _sym in _placed_syms.items():\n";
    code << "        try:\n";
    code << "            _pins = []\n";
    code << "            if hasattr(_sym, 'pins'):\n";
    code << "                for _p in _sym.pins:\n";
    code << "                    _pin_info = {'number': _p.number, 'name': getattr(_p, 'name', '')}\n";
    code << "                    try:\n";
    code << "                        _tr = sch.symbols.get_transformed_pin_position(_sym, _p.number)\n";
    code << "                        if _tr:\n";
    code << "                            _pin_info['position'] = [round(_tr['position'].x / 1_000_000, 4), round(_tr['position'].y / 1_000_000, 4)]\n";
    code << "                    except:\n";
    code << "                        pass\n";
    code << "                    _pins.append(_pin_info)\n";
    code << "            for _r in results:\n";
    code << "                if _r.get('index') == _idx and 'error' not in _r:\n";
    code << "                    _r['pins'] = _pins\n";
    code << "                    break\n";
    code << "        except:\n";
    code << "            pass\n";

    code << "\n";
    code << "    # --- Auto-place junctions for any wires placed in this batch ---\n";
    code << "    _junction_count = 0\n";
    code << "    if _placed_wires:\n";
    code << "        try:\n";
    code << "            _junc_positions = sch.wiring.get_needed_junctions(_placed_wires)\n";
    code << "            for _jp in _junc_positions:\n";
    code << "                sch.wiring.add_junction(_jp)\n";
    code << "                _junction_count += 1\n";
    code << "        except:\n";
    code << "            pass\n";
    code << "\n";
    code << "    _fail = sum(1 for r in results if 'error' in r)\n";
    code << "    result = {\n";
    code << "        'status': 'success' if _fail == 0 else 'partial',\n";
    code << "        'total': " << elements.size() << ",\n";
    code << "        'succeeded': len(results) - _fail,\n";
    code << "        'failed': _fail,\n";
    code << "        'results': results\n";
    code << "    }\n";
    code << "    if _junction_count > 0:\n";
    code << "        result['junctions_placed'] = _junction_count\n";
    code << "\n";
    code << "except Exception as batch_error:\n";
    code << "    result = {'status': 'error', 'message': str(batch_error), 'results': results}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}
