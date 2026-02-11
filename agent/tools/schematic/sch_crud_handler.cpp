#include "sch_crud_handler.h"
#include <sstream>
#include <iomanip>


bool SCH_CRUD_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_open_sheet" ||
           aToolName == "sch_connect_to_power";
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
        if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
        {
            size_t count = aInput["targets"].size();
            if( count == 1 )
                return "Deleting " + aInput["targets"][0].get<std::string>();
            return "Deleting " + std::to_string( count ) + " elements";
        }
        return "Deleting elements";
    }
    else if( aToolName == "sch_open_sheet" )
    {
        std::string sheetPath = aInput.value( "sheet_path", "" );
        std::string filePath = aInput.value( "file_path", "" );
        if( !sheetPath.empty() )
            return "Navigating to sheet: " + sheetPath;
        if( !filePath.empty() )
            return "Opening sheet: " + filePath;
        return "Getting current sheet";
    }
    else if( aToolName == "sch_connect_to_power" )
    {
        std::string ref = aInput.value( "ref", "" );
        std::string pin = aInput.value( "pin", "" );
        std::string power = aInput.value( "power", "" );
        return "Connecting " + ref + ":" + pin + " to " + power;
    }
    return "Executing " + aToolName;
}


bool SCH_CRUD_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_open_sheet" ||
           aToolName == "sch_connect_to_power";
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
    else if( aToolName == "sch_open_sheet" )
        code = GenerateOpenSheetCode( aInput );
    else if( aToolName == "sch_connect_to_power" )
        code = GenerateConnectToPowerCode( aInput );

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
    return std::round( aMm / aGrid ) * aGrid;
}


std::string SCH_CRUD_HANDLER::MmToNm( double aMm ) const
{
    int64_t nm = static_cast<int64_t>( aMm * 1000000.0 );
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


std::string SCH_CRUD_HANDLER::GenerateAddCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string elementType = aInput.value( "element_type", "" );
    std::string filePath = aInput.value( "file_path", "" );

    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "# Helper to safely extract ID from various object types\n";
    code << "def get_id(obj):\n";
    code << "    if obj is None:\n";
    code << "        return ''\n";
    code << "    if hasattr(obj, 'id'):\n";
    code << "        id_obj = obj.id\n";
    code << "        if hasattr(id_obj, 'value'):\n";
    code << "            return str(id_obj.value)\n";
    code << "        return str(id_obj)\n";
    code << "    if hasattr(obj, 'uuid'):\n";
    code << "        return str(obj.uuid)\n";
    code << "    if isinstance(obj, str):\n";
    code << "        return obj\n";
    code << "    return str(obj)\n";
    code << "\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "use_ipc = True\n";
    code << "result = None\n";
    code << "\n";

    // IPC attempt
    code << "# Try IPC first\n";
    code << "try:\n";

    if( elementType == "symbol" )
    {
        std::string libId = aInput.value( "lib_id", "" );
        if( libId.empty() )
        {
            code << "    print(json.dumps({'status': 'error', 'message': 'lib_id is required for symbol'}))\n";
            return code.str();
        }

        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        double angle = aInput.value( "angle", 0.0 );
        std::string mirror = aInput.value( "mirror", "none" );
        bool mirrorX = ( mirror == "x" );
        bool mirrorY = ( mirror == "y" );
        int unit = aInput.value( "unit", 1 );
        std::string reference = aInput.value( "reference", "" );

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    symbol = sch.symbols.add(\n";
        code << "        lib_id='" << EscapePythonString( libId ) << "',\n";
        code << "        position=pos,\n";
        code << "        unit=" << unit << ",\n";
        code << "        angle=" << angle << ",\n";
        code << "        mirror_x=" << ( mirrorX ? "True" : "False" ) << ",\n";
        code << "        mirror_y=" << ( mirrorY ? "True" : "False" ) << "\n";
        code << "    )\n";

        // Handle top-level reference parameter
        if( !reference.empty() )
        {
            code << "    # Set reference from top-level parameter\n";
            code << "    if hasattr(sch.symbols, 'set_reference'):\n";
            code << "        sch.symbols.set_reference(symbol, '" << EscapePythonString( reference ) << "')\n";
            code << "    elif hasattr(symbol, 'reference'):\n";
            code << "        symbol.reference = '" << EscapePythonString( reference ) << "'\n";
        }

        // Handle properties object (for Value, Footprint, etc.)
        if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
        {
            code << "    props = " << aInput["properties"].dump() << "\n";
            code << "    if 'Reference' in props:\n";
            code << "        if hasattr(sch.symbols, 'set_reference'):\n";
            code << "            sch.symbols.set_reference(symbol, props['Reference'])\n";
            code << "        elif hasattr(symbol, 'reference'):\n";
            code << "            symbol.reference = props['Reference']\n";
            code << "    if 'Value' in props:\n";
            code << "        sch.symbols.set_value(symbol, props['Value'])\n";
            code << "    if 'Footprint' in props:\n";
            code << "        sch.symbols.set_footprint(symbol, props['Footprint'])\n";
        }

        // Extract pin positions for the response (eliminates need for follow-up sch_get_pins call)
        code << "    pins = []\n";
        code << "    if hasattr(symbol, 'pins'):\n";
        code << "        for pin in symbol.pins:\n";
        code << "            pin_info = {'number': pin.number, 'name': getattr(pin, 'name', '')}\n";
        code << "            # Get exact transformed position via IPC\n";
        code << "            if hasattr(sch.symbols, 'get_transformed_pin_position'):\n";
        code << "                try:\n";
        code << "                    result = sch.symbols.get_transformed_pin_position(symbol, pin.number)\n";
        code << "                    if result:\n";
        code << "                        p = result['position']\n";
        code << "                        pin_info['position'] = [p.x / 1_000_000, p.y / 1_000_000]\n";
        code << "                        pin_info['orientation'] = result.get('orientation', 0)\n";
        code << "                except:\n";
        code << "                    if hasattr(pin, 'position'):\n";
        code << "                        pin_info['position'] = [pin.position.x / 1_000_000, pin.position.y / 1_000_000]\n";
        code << "            elif hasattr(pin, 'position'):\n";
        code << "                pin_info['position'] = [pin.position.x / 1_000_000, pin.position.y / 1_000_000]\n";
        code << "            pins.append(pin_info)\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(symbol), 'reference': getattr(symbol, 'reference', ''), 'pins': pins}\n";
    }
    else if( elementType == "power" )
    {
        std::string libId = aInput.value( "lib_id", "" );
        std::string powerName = libId;
        size_t colonPos = libId.find( ':' );
        if( colonPos != std::string::npos )
            powerName = libId.substr( colonPos + 1 );

        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        double angle = aInput.value( "angle", 0.0 );

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    symbol = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pos, angle=" << angle << ")\n";
        // Power symbol pin is at the symbol position itself (no offset)
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(symbol), 'name': '" << EscapePythonString( powerName ) << "', 'pin_position': [" << posX << ", " << posY << "]}\n";
    }
    else if( elementType == "wire" )
    {
        // Mode 1: from_pin + to_pin (with optional waypoints) - pin-to-pin routing
        if( aInput.contains( "from_pin" ) && aInput.contains( "to_pin" ) )
        {
            auto fromPin = aInput["from_pin"];
            auto toPin = aInput["to_pin"];
            std::string fromRef = fromPin.value( "ref", "" );
            std::string fromPinNum = fromPin.value( "pin", "" );
            std::string toRef = toPin.value( "ref", "" );
            std::string toPinNum = toPin.value( "pin", "" );

            code << "    sym1 = sch.symbols.get_by_ref('" << EscapePythonString( fromRef ) << "')\n";
            code << "    sym2 = sch.symbols.get_by_ref('" << EscapePythonString( toRef ) << "')\n";
            code << "    if not sym1:\n";
            code << "        raise ValueError('Symbol not found: " << EscapePythonString( fromRef ) << "')\n";
            code << "    if not sym2:\n";
            code << "        raise ValueError('Symbol not found: " << EscapePythonString( toRef ) << "')\n";

            // Check for waypoints - if present, use wire_path for custom routing
            if( aInput.contains( "waypoints" ) && aInput["waypoints"].is_array() && aInput["waypoints"].size() > 0 )
            {
                auto waypoints = aInput["waypoints"];
                code << "    waypoints = [\n";
                for( size_t i = 0; i < waypoints.size(); ++i )
                {
                    if( waypoints[i].is_array() && waypoints[i].size() >= 2 )
                    {
                        double x = SnapToGrid( waypoints[i][0].get<double>() );
                        double y = SnapToGrid( waypoints[i][1].get<double>() );
                        code << "        (" << x << ", " << y << "),\n";
                    }
                }
                code << "    ]\n";
                code << "    wires = sch.wiring.wire_path((sym1, '" << EscapePythonString( fromPinNum ) << "'), waypoints, (sym2, '" << EscapePythonString( toPinNum ) << "'))\n";
                code << "    result = {'status': 'success', 'source': 'ipc', 'wire_count': len(wires)}\n";
            }
            else
            {
                // No waypoints - use auto_wire for L-shaped orthogonal routing
                code << "    wires = sch.wiring.auto_wire(sym1, '" << EscapePythonString( fromPinNum ) << "', sym2, '" << EscapePythonString( toPinNum ) << "')\n";
                code << "    result = {'status': 'success', 'source': 'ipc', 'wire_count': len(wires)}\n";
            }
        }
        // Mode 2: from_pin + points (no to_pin) - start from pin, extend through points
        else if( aInput.contains( "from_pin" ) && aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            auto fromPin = aInput["from_pin"];
            std::string fromRef = fromPin.value( "ref", "" );
            std::string fromPinNum = fromPin.value( "pin", "" );
            auto points = aInput["points"];

            code << "    sym = sch.symbols.get_by_ref('" << EscapePythonString( fromRef ) << "')\n";
            code << "    if not sym:\n";
            code << "        raise ValueError('Symbol not found: " << EscapePythonString( fromRef ) << "')\n";
            code << "    # Get exact pin position via IPC\n";
            code << "    pin_pos = sch.symbols.get_pin_position(sym, '" << EscapePythonString( fromPinNum ) << "')\n";
            code << "    if not pin_pos:\n";
            code << "        raise ValueError('Pin not found: " << EscapePythonString( fromPinNum ) << "')\n";
            code << "    # Build points list starting from pin position\n";
            code << "    all_points = [pin_pos]\n";

            // Add remaining points
            for( size_t i = 0; i < points.size(); ++i )
            {
                if( points[i].is_array() && points[i].size() >= 2 )
                {
                    double x = SnapToGrid( points[i][0].get<double>() );
                    double y = SnapToGrid( points[i][1].get<double>() );
                    code << "    all_points.append(Vector2.from_xy_mm(" << x << ", " << y << "))\n";
                }
            }

            code << "    wires = sch.wiring.add_wires(all_points)\n";
            code << "    result = {'status': 'success', 'source': 'ipc', 'wire_count': len(wires)}\n";
        }
        // Mode 3: points only - explicit coordinate-based wiring
        else if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            auto points = aInput["points"];
            if( points.size() >= 2 )
            {
                code << "    wires_created = 0\n";
                for( size_t i = 0; i < points.size() - 1; ++i )
                {
                    if( points[i].is_array() && points[i].size() >= 2 &&
                        points[i + 1].is_array() && points[i + 1].size() >= 2 )
                    {
                        double x1 = SnapToGrid( points[i][0].get<double>() );
                        double y1 = SnapToGrid( points[i][1].get<double>() );
                        double x2 = SnapToGrid( points[i + 1][0].get<double>() );
                        double y2 = SnapToGrid( points[i + 1][1].get<double>() );

                        code << "    sch.wiring.add_wire(Vector2.from_xy_mm(" << x1 << ", " << y1 << "), Vector2.from_xy_mm(" << x2 << ", " << y2 << "))\n";
                        code << "    wires_created += 1\n";
                    }
                }
                code << "    result = {'status': 'success', 'source': 'ipc', 'count': wires_created}\n";
            }
        }
        else
        {
            code << "    raise ValueError('Either from_pin/to_pin or points required for wire')\n";
        }
    }
    else if( elementType == "junction" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    junction = sch.wiring.add_junction(pos)\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(junction)}\n";
    }
    else if( elementType == "label" )
    {
        std::string text = aInput.value( "text", "" );
        std::string labelType = aInput.value( "label_type", "local" );

        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";

        if( labelType == "local" )
            code << "    label = sch.labels.add_local('" << EscapePythonString( text ) << "', pos)\n";
        else if( labelType == "global" )
            code << "    label = sch.labels.add_global('" << EscapePythonString( text ) << "', pos)\n";
        else if( labelType == "hierarchical" )
            code << "    label = sch.labels.add_hierarchical('" << EscapePythonString( text ) << "', pos)\n";
        else
            code << "    label = sch.labels.add_local('" << EscapePythonString( text ) << "', pos)\n";

        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(label)}\n";
    }
    else if( elementType == "no_connect" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    nc = sch.wiring.add_no_connect(pos)\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(nc)}\n";
    }
    else if( elementType == "bus_entry" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        // direction: "right_down", "right_up", "left_down", "left_up"
        std::string direction = aInput.value( "direction", "right_down" );

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    bus_entry = sch.buses.add_bus_entry(pos, direction='" << EscapePythonString( direction ) << "')\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(bus_entry)}\n";
    }
    else if( elementType == "sheet" )
    {
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

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    size = Vector2.from_xy_mm(" << sizeW << ", " << sizeH << ")\n";
        code << "    sheet = sch.sheets.create(\n";
        code << "        name='" << EscapePythonString( sheetName ) << "',\n";
        code << "        filename='" << EscapePythonString( sheetFile.empty() ? sheetName + ".kicad_sch" : sheetFile ) << "',\n";
        code << "        position=pos,\n";
        code << "        size=size\n";
        code << "    )\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(sheet), 'name': '" << EscapePythonString( sheetName ) << "'}\n";
    }
    else
    {
        code << "    raise ValueError('Unknown element_type: " << EscapePythonString( elementType ) << "')\n";
    }

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
    code << "elif not use_ipc and file_path:\n";
    code << "    try:\n";
    code << GenerateFileFallbackHeader();

    // Generate file-based add for each element type
    if( elementType == "wire" && aInput.contains( "points" ) && aInput["points"].is_array() )
    {
        auto points = aInput["points"];
        if( points.size() >= 2 )
        {
            code << "        wires_created = 0\n";
            for( size_t i = 0; i < points.size() - 1; ++i )
            {
                if( points[i].is_array() && points[i].size() >= 2 &&
                    points[i + 1].is_array() && points[i + 1].size() >= 2 )
                {
                    double x1 = SnapToGrid( points[i][0].get<double>() );
                    double y1 = SnapToGrid( points[i][1].get<double>() );
                    double x2 = SnapToGrid( points[i + 1][0].get<double>() );
                    double y2 = SnapToGrid( points[i + 1][1].get<double>() );

                    code << "        wire_uuid = generate_uuid()\n";
                    code << "        wire_sexpr = f'''(wire (pts (xy " << x1 << " " << y1 << ") (xy " << x2 << " " << y2 << "))\n";
                    code << "    (stroke (width 0) (type default))\n";
                    code << "    (uuid \"{wire_uuid}\")\n";
                    code << "  )'''\n";
                    code << "        add_element_to_file(file_path, 'wire', wire_sexpr)\n";
                    code << "        wires_created += 1\n";
                }
            }
            code << "        result = {'status': 'success', 'source': 'file', 'count': wires_created}\n";
        }
    }
    else if( elementType == "junction" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "        junc_uuid = generate_uuid()\n";
        code << "        junc_sexpr = f'''(junction (at " << posX << " " << posY << ")\n";
        code << "    (diameter 0) (color 0 0 0 0)\n";
        code << "    (uuid \"{junc_uuid}\")\n";
        code << "  )'''\n";
        code << "        add_element_to_file(file_path, 'junction', junc_sexpr)\n";
        code << "        result = {'status': 'success', 'source': 'file', 'id': junc_uuid}\n";
    }
    else if( elementType == "label" )
    {
        std::string text = aInput.value( "text", "" );
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "        label_uuid = generate_uuid()\n";
        code << "        label_sexpr = f'''(label \"" << EscapePythonString( text ) << "\" (at " << posX << " " << posY << " 0)\n";
        code << "    (effects (font (size 1.27 1.27)) (justify left bottom))\n";
        code << "    (uuid \"{label_uuid}\")\n";
        code << "  )'''\n";
        code << "        add_element_to_file(file_path, 'label', label_sexpr)\n";
        code << "        result = {'status': 'success', 'source': 'file', 'id': label_uuid}\n";
    }
    else if( elementType == "no_connect" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        code << "        nc_uuid = generate_uuid()\n";
        code << "        nc_sexpr = f'''(no_connect (at " << posX << " " << posY << ")\n";
        code << "    (uuid \"{nc_uuid}\")\n";
        code << "  )'''\n";
        code << "        add_element_to_file(file_path, 'no_connect', nc_sexpr)\n";
        code << "        result = {'status': 'success', 'source': 'file', 'id': nc_uuid}\n";
    }
    else if( elementType == "bus_entry" )
    {
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = SnapToGrid( aInput["position"][0].get<double>() );
            posY = SnapToGrid( aInput["position"][1].get<double>() );
        }

        std::string direction = aInput.value( "direction", "right_down" );

        // Calculate size based on direction (default 2.54mm diagonal)
        double sizeX = 2.54, sizeY = 2.54;
        if( direction == "left_down" || direction == "left_up" )
            sizeX = -2.54;
        if( direction == "right_up" || direction == "left_up" )
            sizeY = -2.54;

        code << "        entry_uuid = generate_uuid()\n";
        code << "        entry_sexpr = f'''(bus_entry (at " << posX << " " << posY << ")\n";
        code << "    (size " << sizeX << " " << sizeY << ")\n";
        code << "    (stroke (width 0) (type default))\n";
        code << "    (uuid \"{entry_uuid}\")\n";
        code << "  )'''\n";
        code << "        add_element_to_file(file_path, 'bus_entry', entry_sexpr)\n";
        code << "        result = {'status': 'success', 'source': 'file', 'id': entry_uuid}\n";
    }
    else
    {
        // Symbol and other complex types - file fallback not supported
        code << "        result = {'status': 'error', 'message': f'File fallback not supported for " << elementType << ". IPC error: {ipc_error_msg}'}\n";
    }

    code << "    except Exception as file_error:\n";
    code << "        result = {'status': 'error', 'message': f'Both IPC and file fallback failed. IPC: {ipc_error_msg}, File: {str(file_error)}'}\n";
    code << "\n";

    code << "# Output result\n";
    code << "if result:\n";
    code << "    print(json.dumps(result, indent=2))\n";
    code << "else:\n";
    code << "    print(json.dumps({'status': 'error', 'message': 'No result generated'}))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateUpdateCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    std::string filePath = aInput.value( "file_path", "" );

    if( target.empty() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'target is required'}))\n";
        return code.str();
    }

    code << "import json, re, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "use_ipc = True\n";
    code << "result = None\n";
    code << "\n";

    // IPC attempt
    code << "# Try IPC first\n";
    code << "try:\n";
    code << "    is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "    target_item = None\n";
    code << "    if is_uuid:\n";
    code << "        items = sch.crud.get_by_id([target])\n";
    code << "        if items:\n";
    code << "            target_item = items[0]\n";
    code << "    else:\n";
    code << "        # Try exact reference match first\n";
    code << "        target_item = sch.symbols.get_by_ref(target)\n";
    code << "        # If not found, search all symbols for partial match\n";
    code << "        if not target_item:\n";
    code << "            all_symbols = sch.symbols.get_all()\n";
    code << "            matches = []\n";
    code << "            for sym in all_symbols:\n";
    code << "                ref = getattr(sym, 'reference', '')\n";
    code << "                # Check exact match, or prefix match (e.g., 'R' matches 'R1', 'R2')\n";
    code << "                if ref == target or ref.startswith(target) or target.startswith(ref):\n";
    code << "                    sym_uuid = str(sym.id.value) if hasattr(sym, 'id') and hasattr(sym.id, 'value') else str(getattr(sym, 'id', getattr(sym, 'uuid', '')))\n";
    code << "                    matches.append({'ref': ref, 'uuid': sym_uuid, 'value': getattr(sym, 'value', '')})\n";
    code << "            if len(matches) == 1:\n";
    code << "                target_item = sch.symbols.get_by_ref(matches[0]['ref'])\n";
    code << "            elif len(matches) > 1:\n";
    code << "                raise ValueError(f'Multiple matches for {target}: {matches}. Use UUID for precise targeting.')\n";
    code << "\n";
    code << "    if not target_item:\n";
    code << "        # List available symbols for better error message\n";
    code << "        all_symbols = sch.symbols.get_all()\n";
    code << "        available = [{'ref': getattr(s, 'reference', '?'), 'uuid': str(s.id.value) if hasattr(s, 'id') and hasattr(s.id, 'value') else str(getattr(s, 'id', ''))} for s in all_symbols[:10]]\n";
    code << "        raise ValueError(f'Element not found: {target}. Available symbols: {available}')\n";
    code << "\n";
    code << "    updated = False\n";

    if( aInput.contains( "position" ) && aInput["position"].is_array() &&
        aInput["position"].size() >= 2 )
    {
        double posX = SnapToGrid( aInput["position"][0].get<double>() );
        double posY = SnapToGrid( aInput["position"][1].get<double>() );
        code << "    new_pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    target_item = sch.symbols.move(target_item, new_pos)\n";
        code << "    updated = True\n";
    }

    if( aInput.contains( "angle" ) )
    {
        double angle = aInput.value( "angle", 0.0 );
        // Compute delta from current angle and use rotate() (set_angle doesn't exist)
        code << "    current_angle = getattr(target_item, 'angle', 0) or 0\n";
        code << "    desired_angle = " << angle << "\n";
        code << "    delta = (desired_angle - current_angle) % 360\n";
        code << "    if delta != 0:\n";
        code << "        target_item = sch.symbols.rotate(target_item, delta)\n";
        code << "    updated = True\n";
    }

    if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
    {
        code << "    props_to_update = " << aInput["properties"].dump() << "\n";
        code << "    if 'Value' in props_to_update:\n";
        code << "        sch.symbols.set_value(target_item, props_to_update['Value'])\n";
        code << "        updated = True\n";
        code << "    if 'Footprint' in props_to_update:\n";
        code << "        sch.symbols.set_footprint(target_item, props_to_update['Footprint'])\n";
        code << "        updated = True\n";
    }

    // Reposition text fields (Reference, Value, etc.) relative to symbol center
    if( aInput.contains( "fields" ) && aInput["fields"].is_object() )
    {
        code << "    # Reposition text fields relative to symbol center\n";
        code << "    sym_pos = target_item.position\n";

        for( auto& [fieldName, fieldSpec] : aInput["fields"].items() )
        {
            if( !fieldSpec.is_object() )
                continue;

            bool hasOffset = fieldSpec.contains( "offset" ) &&
                             fieldSpec["offset"].is_array() && fieldSpec["offset"].size() >= 2;
            bool hasAngle = fieldSpec.contains( "angle" ) && fieldSpec["angle"].is_number();

            if( !hasOffset && !hasAngle )
                continue;

            code << "    for _f in target_item._proto.fields:\n";
            code << "        if _f.name == '" << EscapePythonString( fieldName ) << "':\n";

            if( hasOffset )
            {
                double dx = SnapToGrid( fieldSpec["offset"][0].get<double>() );
                double dy = SnapToGrid( fieldSpec["offset"][1].get<double>() );
                code << "            _f.position.x_nm = sym_pos.x + int("
                     << dx << " * 1_000_000)\n";
                code << "            _f.position.y_nm = sym_pos.y + int("
                     << dy << " * 1_000_000)\n";
            }

            if( hasAngle )
            {
                double angle = fieldSpec["angle"].get<double>();
                code << "            _f.attributes.angle.value_degrees = " << angle << "\n";
            }

            code << "            break\n";
        }

        code << "    target_item = sch.crud.update_items(target_item)\n";
        code << "    if target_item:\n";
        code << "        target_item = target_item[0]\n";
        code << "    updated = True\n";
    }

    // Return post-update state for verification
    code << "    # Build state info from updated item\n";
    code << "    state = {}\n";
    code << "    if hasattr(target_item, 'position'):\n";
    code << "        pos = target_item.position\n";
    code << "        state['position'] = [pos.x / 1_000_000, pos.y / 1_000_000]  # Convert nm to mm\n";
    code << "    if hasattr(target_item, 'angle'):\n";
    code << "        state['angle'] = target_item.angle\n";
    code << "    if hasattr(target_item, 'mirror_x'):\n";
    code << "        if target_item.mirror_x:\n";
    code << "            state['mirror'] = 'x'\n";
    code << "        elif hasattr(target_item, 'mirror_y') and target_item.mirror_y:\n";
    code << "            state['mirror'] = 'y'\n";
    code << "        else:\n";
    code << "            state['mirror'] = 'none'\n";
    code << "    if hasattr(target_item, 'reference'):\n";
    code << "        state['reference'] = target_item.reference\n";
    code << "    if hasattr(target_item, 'value'):\n";
    code << "        state['value'] = target_item.value\n";
    code << "    result = {'status': 'success', 'source': 'ipc', 'target': target, 'updated': updated, 'state': state}\n";
    code << "\n";
    code << "except Exception as ipc_error:\n";
    code << "    use_ipc = False\n";
    code << "    ipc_error_msg = str(ipc_error)\n";
    code << "    print(f'IPC failed: {ipc_error_msg}', file=sys.stderr)\n";
    code << "\n";

    // Check if editor is still open
    code << GenerateEditorOpenCheck();
    code << "\n";

    // File fallback for update is not supported - always requires IPC
    code << "# Update requires IPC - file fallback not supported\n";
    code << "if not use_ipc:\n";
    code << "    if editor_still_open:\n";
    code << "        result = {'status': 'error', 'message': f'IPC failed but schematic editor is still open. Updates require IPC. Investigate the error: {ipc_error_msg}'}\n";
    code << "    else:\n";
    code << "        result = {'status': 'error', 'message': f'IPC failed and editor is closed. Open the schematic editor to use IPC for updates: {ipc_error_msg}'}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

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
    code << "errors = []\n";
    code << "\n";
    code << "try:\n";

    // Process each update in the array
    for( size_t i = 0; i < updates.size(); ++i )
    {
        auto update = updates[i];
        std::string target = update.value( "target", "" );

        if( target.empty() )
        {
            code << "    errors.append({'index': " << i << ", 'error': 'target is required'})\n";
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
            code << "        new_pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        item_" << i << " = sch.symbols.move(item_" << i << ", new_pos_" << i << ")\n";
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
                    code << "                _f.position.x_nm = sym_pos_" << i << ".x + int("
                         << dx << " * 1_000_000)\n";
                    code << "                _f.position.y_nm = sym_pos_" << i << ".y + int("
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
        code << "        errors.append({'index': " << i << ", 'target': '" << EscapePythonString( target ) << "', 'error': str(e_" << i << ")})\n";
        code << "\n";
    }

    code << "\n";
    code << "    result = {\n";
    code << "        'status': 'success' if len(errors) == 0 else 'partial',\n";
    code << "        'source': 'ipc',\n";
    code << "        'total': " << updates.size() << ",\n";
    code << "        'succeeded': len(results),\n";
    code << "        'failed': len(errors),\n";
    code << "        'results': results\n";
    code << "    }\n";
    code << "    if errors:\n";
    code << "        result['errors'] = errors\n";
    code << "\n";
    code << "except Exception as batch_error:\n";
    code << "    result = {'status': 'error', 'message': str(batch_error), 'partial_results': results, 'errors': errors}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    std::string filePath = aInput.value( "file_path", "" );

    if( target.empty() )
    {
        code << "import json\n";
        code << "print(json.dumps({'status': 'error', 'message': 'target is required'}))\n";
        return code.str();
    }

    code << "import json, re, sys\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "use_ipc = True\n";
    code << "result = None\n";
    code << "target_uuid = None\n";
    code << "\n";

    // IPC attempt
    code << "# Try IPC first\n";
    code << "try:\n";
    code << "    is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "    target_item = None\n";
    code << "    if is_uuid:\n";
    code << "        target_uuid = target\n";
    code << "        items = sch.crud.get_by_id([target])\n";
    code << "        if items:\n";
    code << "            target_item = items[0]\n";
    code << "    else:\n";
    code << "        target_item = sch.symbols.get_by_ref(target)\n";
    code << "        if target_item:\n";
    code << "            target_uuid = str(target_item.id.value) if hasattr(target_item, 'id') and hasattr(target_item.id, 'value') else str(getattr(target_item, 'id', getattr(target_item, 'uuid', '')))\n";
    code << "\n";
    code << "    if target_item:\n";
    code << "        sch.crud.remove_items([target_item])\n";
    code << "        result = {'status': 'success', 'source': 'ipc', 'target': target}\n";
    code << "    else:\n";
    code << "        raise ValueError(f'Element not found: {target}')\n";
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
    code << "elif not use_ipc and file_path and target_uuid:\n";
    code << "    try:\n";
    code << GenerateFileFallbackHeader();
    code << "        if delete_element_from_file(file_path, target_uuid):\n";
    code << "            result = {'status': 'success', 'source': 'file', 'target': target}\n";
    code << "        else:\n";
    code << "            result = {'status': 'error', 'message': f'Element not found in file: {target}'}\n";
    code << "    except Exception as file_error:\n";
    code << "        result = {'status': 'error', 'message': f'File fallback failed (editor closed). File error: {str(file_error)}'}\n";
    code << "elif not use_ipc:\n";
    code << "    result = {'status': 'error', 'message': f'IPC failed and cannot determine UUID for file fallback: {ipc_error_msg}'}\n";
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

    code << "import json, re, sys\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "targets = [";
    for( size_t i = 0; i < targets.size(); ++i )
    {
        code << "'" << EscapePythonString( targets[i].get<std::string>() ) << "'";
        if( i < targets.size() - 1 )
            code << ", ";
    }
    code << "]\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "cleanup_wires = " << ( cleanupWires ? "True" : "False" ) << "\n";
    code << "use_ipc = True\n";
    code << "result = None\n";
    code << "target_uuids = []\n";
    code << "\n";

    // IPC attempt
    code << "# Try IPC first\n";
    code << "try:\n";
    code << "    items_to_delete = []\n";
    code << "    not_found = []\n";
    code << "    for target in targets:\n";
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
    code << "    # Recursively clean up orphaned wires and junctions\n";
    code << "    orphaned_wires = []\n";
    code << "    orphaned_junctions = []\n";
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
    code << "        except Exception as cleanup_err:\n";
    code << "            print(f'Orphan cleanup warning: {cleanup_err}', file=sys.stderr)\n";
    code << "\n";
    code << "    result = {'status': 'success', 'source': 'ipc', 'deleted': len(items_to_delete)}\n";
    code << "    if orphaned_wires:\n";
    code << "        result['orphaned_wires_removed'] = len(orphaned_wires)\n";
    code << "        result['orphaned_wires'] = orphaned_wires\n";
    code << "    if orphaned_junctions:\n";
    code << "        result['orphaned_junctions_removed'] = len(orphaned_junctions)\n";
    code << "    if not_found:\n";
    code << "        result['not_found'] = not_found\n";
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


std::string SCH_CRUD_HANDLER::GenerateOpenSheetCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    // Extract parameters - match tool definition names
    std::string sheetPath = aInput.value( "sheet_path", "" );
    std::string filePath = aInput.value( "file_path", "" );

    code << "import json, sys\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "sheet_path = " << nlohmann::json( sheetPath ).dump() << "\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
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
    code << "            print(f'[sch_open_sheet] Hierarchy tree has {len(hierarchy_nodes)} nodes', file=sys.stderr)\n";
    code << "        except Exception as he:\n";
    code << "            print(f'[sch_open_sheet] get_hierarchy failed: {he}', file=sys.stderr)\n";
    code << "    \n";
    code << "    # Also get sheet items for fallback\n";
    code << "    sheets = sch.crud.get_sheets()\n";
    code << "    print(f'[sch_open_sheet] Found {len(sheets)} sheet items', file=sys.stderr)\n";
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
    code << "    # Handle file_path - open a specific .kicad_sch file\n";
    code << "    if not target_sheet and not navigated and file_path:\n";
    code << "        import os\n";
    code << "        basename = os.path.basename(file_path)\n";
    code << "        if basename in sheet_by_file:\n";
    code << "            target_sheet, target_info = sheet_by_file[basename]\n";
    code << "        elif file_path in sheet_by_file:\n";
    code << "            target_sheet, target_info = sheet_by_file[file_path]\n";
    code << "    \n";
    code << "    # Navigate to target sheet if found\n";
    code << "    if target_sheet and not navigated:\n";
    code << "        print(f'[sch_open_sheet] Attempting to navigate to sheet', file=sys.stderr)\n";
    code << "        # First try navigate_to with the SheetPath (most reliable for hierarchy nodes)\n";
    code << "        if hasattr(sch.sheets, 'navigate_to') and hasattr(target_sheet, 'path') and target_sheet.path:\n";
    code << "            try:\n";
    code << "                print(f'[sch_open_sheet] Using navigate_to with path', file=sys.stderr)\n";
    code << "                sch.sheets.navigate_to(target_sheet.path)\n";
    code << "                navigated = True\n";
    code << "            except Exception as nav_err:\n";
    code << "                print(f'[sch_open_sheet] navigate_to failed: {nav_err}', file=sys.stderr)\n";
    code << "        \n";
    code << "        if not navigated and hasattr(sch.sheets, 'enter'):\n";
    code << "            try:\n";
    code << "                # enter might work with the node directly\n";
    code << "                print(f'[sch_open_sheet] Trying enter method', file=sys.stderr)\n";
    code << "                sch.sheets.enter(target_sheet)\n";
    code << "                navigated = True\n";
    code << "            except Exception as enter_err:\n";
    code << "                print(f'[sch_open_sheet] enter failed: {enter_err}', file=sys.stderr)\n";
    code << "        \n";
    code << "        if not navigated and hasattr(sch.sheets, 'open'):\n";
    code << "            try:\n";
    code << "                print(f'[sch_open_sheet] Trying open method', file=sys.stderr)\n";
    code << "                sch.sheets.open(target_sheet)\n";
    code << "                navigated = True\n";
    code << "            except Exception as open_err:\n";
    code << "                print(f'[sch_open_sheet] open failed: {open_err}', file=sys.stderr)\n";
    code << "    \n";
    code << "    # Build result\n";
    code << "    if navigated:\n";
    code << "        result = {\n";
    code << "            'status': 'success',\n";
    code << "            'action': 'navigated',\n";
    code << "            'target': target_info,\n";
    code << "            'available_sheets': hierarchy\n";
    code << "        }\n";
    code << "    elif not sheet_path and not file_path:\n";
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
    code << "            'message': f'Sheet not found: {sheet_path or file_path}',\n";
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


std::string SCH_CRUD_HANDLER::GenerateConnectToPowerCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string ref = aInput.value( "ref", "" );
    std::string pin = aInput.value( "pin", "" );
    std::string power = aInput.value( "power", "" );

    // Get offset - default [0, 0] means place directly at pin (no wire)
    double offsetX = 0, offsetY = 0;
    if( aInput.contains( "offset" ) && aInput["offset"].is_array() && aInput["offset"].size() >= 2 )
    {
        offsetX = aInput["offset"][0].get<double>();
        offsetY = aInput["offset"][1].get<double>();
    }

    // Get power angle - if not specified, auto-detect based on power type
    bool hasAngle = aInput.contains( "power_angle" );
    double powerAngle = aInput.value( "power_angle", 0.0 );

    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "ref = '" << EscapePythonString( ref ) << "'\n";
    code << "pin_id = '" << EscapePythonString( pin ) << "'\n";
    code << "power_name = '" << EscapePythonString( power ) << "'\n";
    code << "offset_x = " << offsetX << "\n";
    code << "offset_y = " << offsetY << "\n";
    code << "\n";
    code << "try:\n";
    code << "    # Get the symbol and pin position\n";
    code << "    sym = sch.symbols.get_by_ref(ref)\n";
    code << "    if not sym:\n";
    code << "        raise ValueError(f'Symbol not found: {ref}')\n";
    code << "\n";
    code << "    # Get exact pin position via IPC\n";
    code << "    pin_result = sch.symbols.get_transformed_pin_position(sym, pin_id)\n";
    code << "    if not pin_result:\n";
    code << "        # Fallback to cached position\n";
    code << "        pin_pos = sch.symbols.get_pin_position(sym, pin_id)\n";
    code << "        if not pin_pos:\n";
    code << "            raise ValueError(f'Pin not found: {pin_id} on {ref}')\n";
    code << "        pin_x = pin_pos.x / 1_000_000\n";
    code << "        pin_y = pin_pos.y / 1_000_000\n";
    code << "    else:\n";
    code << "        pin_x = pin_result['position'].x / 1_000_000\n";
    code << "        pin_y = pin_result['position'].y / 1_000_000\n";
    code << "\n";
    code << "    # Snap to 1.27mm grid\n";
    code << "    def snap_to_grid(val, grid=1.27):\n";
    code << "        return round(val / grid) * grid\n";
    code << "    # Calculate power symbol position (snapped to grid)\n";
    code << "    power_x = snap_to_grid(pin_x + offset_x)\n";
    code << "    power_y = snap_to_grid(pin_y + offset_y)\n";
    code << "\n";
    code << "    # Auto-detect angle based on power type if not specified\n";

    if( hasAngle )
    {
        code << "    power_angle = " << powerAngle << "\n";
    }
    else
    {
        code << "    # GND at 0° = bars down (standard). VCC at 0° = bar up (standard).\n";
        code << "    power_lower = power_name.lower()\n";
        code << "    if 'gnd' in power_lower or 'vss' in power_lower:\n";
        code << "        power_angle = 0  # GND at 0° = bars down (standard orientation)\n";
        code << "    else:\n";
        code << "        power_angle = 0  # VCC at 0° = bar up (standard orientation)\n";
    }

    code << "\n";
    code << "    # Place the power symbol\n";
    code << "    power_pos = Vector2.from_xy_mm(power_x, power_y)\n";
    code << "    power_sym = sch.labels.add_power(power_name, power_pos, angle=power_angle)\n";
    code << "\n";
    code << "    wire_count = 0\n";
    code << "    # If there's an offset, draw a wire from pin to power symbol\n";
    code << "    if abs(offset_x) > 0.01 or abs(offset_y) > 0.01:\n";
    code << "        pin_vec = Vector2.from_xy_mm(pin_x, pin_y)\n";
    code << "        # For L-shaped routes when both offsets are non-zero\n";
    code << "        if abs(offset_x) > 0.01 and abs(offset_y) > 0.01:\n";
    code << "            # Create corner point - horizontal first\n";
    code << "            corner = Vector2.from_xy_mm(power_x, pin_y)\n";
    code << "            sch.wiring.add_wire(pin_vec, corner)\n";
    code << "            sch.wiring.add_wire(corner, power_pos)\n";
    code << "            wire_count = 2\n";
    code << "        else:\n";
    code << "            # Direct wire\n";
    code << "            sch.wiring.add_wire(pin_vec, power_pos)\n";
    code << "            wire_count = 1\n";
    code << "\n";
    code << "    result = {\n";
    code << "        'status': 'success',\n";
    code << "        'source': 'ipc',\n";
    code << "        'ref': ref,\n";
    code << "        'pin': pin_id,\n";
    code << "        'power': power_name,\n";
    code << "        'power_position': [power_x, power_y],\n";
    code << "        'pin_position': [pin_x, pin_y],\n";
    code << "        'wire_count': wire_count\n";
    code << "    }\n";
    code << "\n";
    code << "except Exception as e:\n";
    code << "    result = {'status': 'error', 'message': str(e)}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

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
    std::string filePath = aInput.value( "file_path", "" );

    code << "import json, sys\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << GenerateRefreshPreamble();
    code << "\n";
    code << "# Helper to safely extract ID from various object types\n";
    code << "def get_id(obj):\n";
    code << "    if obj is None:\n";
    code << "        return ''\n";
    code << "    if hasattr(obj, 'id'):\n";
    code << "        id_obj = obj.id\n";
    code << "        if hasattr(id_obj, 'value'):\n";
    code << "            return str(id_obj.value)\n";
    code << "        return str(id_obj)\n";
    code << "    if hasattr(obj, 'uuid'):\n";
    code << "        return str(obj.uuid)\n";
    code << "    if isinstance(obj, str):\n";
    code << "        return obj\n";
    code << "    return str(obj)\n";
    code << "\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "results = []\n";
    code << "errors = []\n";
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
            std::string reference = elem.value( "reference", "" );

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        sym_" << i << " = sch.symbols.add(\n";
            code << "            lib_id='" << EscapePythonString( libId ) << "',\n";
            code << "            position=pos_" << i << ",\n";
            code << "            unit=" << unit << ",\n";
            code << "            angle=" << angle << ",\n";
            code << "            mirror_x=" << ( mirrorX ? "True" : "False" ) << ",\n";
            code << "            mirror_y=" << ( mirrorY ? "True" : "False" ) << "\n";
            code << "        )\n";

            if( !reference.empty() )
            {
                code << "        if hasattr(sch.symbols, 'set_reference'):\n";
                code << "            sch.symbols.set_reference(sym_" << i << ", '" << EscapePythonString( reference ) << "')\n";
            }

            // Handle properties
            if( elem.contains( "properties" ) && elem["properties"].is_object() )
            {
                code << "        props_" << i << " = " << elem["properties"].dump() << "\n";
                code << "        if 'Value' in props_" << i << ":\n";
                code << "            sch.symbols.set_value(sym_" << i << ", props_" << i << "['Value'])\n";
                code << "        if 'Footprint' in props_" << i << ":\n";
                code << "            sch.symbols.set_footprint(sym_" << i << ", props_" << i << "['Footprint'])\n";
            }

            // Get pin positions for response
            code << "        pins_" << i << " = []\n";
            code << "        if hasattr(sym_" << i << ", 'pins'):\n";
            code << "            for pin in sym_" << i << ".pins:\n";
            code << "                pin_info = {'number': pin.number, 'name': getattr(pin, 'name', '')}\n";
            code << "                if hasattr(sch.symbols, 'get_transformed_pin_position'):\n";
            code << "                    try:\n";
            code << "                        result = sch.symbols.get_transformed_pin_position(sym_" << i << ", pin.number)\n";
            code << "                        if result:\n";
            code << "                            p = result['position']\n";
            code << "                            pin_info['position'] = [p.x / 1_000_000, p.y / 1_000_000]\n";
            code << "                    except:\n";
            code << "                        pass\n";
            code << "                pins_" << i << ".append(pin_info)\n";
            code << "        results.append({'index': " << i << ", 'type': 'symbol', 'id': get_id(sym_" << i << "), 'reference': getattr(sym_" << i << ", 'reference', ''), 'pins': pins_" << i << "})\n";
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
            code << "        results.append({'index': " << i << ", 'type': 'power', 'id': get_id(pwr_" << i << "), 'name': '" << EscapePythonString( powerName ) << "', 'pin_position': [" << posX << ", " << posY << "]})\n";
        }
        else if( elementType == "wire" )
        {
            // Handle wire with from_pin/to_pin
            if( elem.contains( "from_pin" ) && elem.contains( "to_pin" ) )
            {
                auto fromPin = elem["from_pin"];
                auto toPin = elem["to_pin"];
                std::string fromRef = fromPin.value( "ref", "" );
                std::string fromPinNum = fromPin.value( "pin", "" );
                std::string toRef = toPin.value( "ref", "" );
                std::string toPinNum = toPin.value( "pin", "" );

                code << "        sym1_" << i << " = sch.symbols.get_by_ref('" << EscapePythonString( fromRef ) << "')\n";
                code << "        sym2_" << i << " = sch.symbols.get_by_ref('" << EscapePythonString( toRef ) << "')\n";
                code << "        if sym1_" << i << " and sym2_" << i << ":\n";

                // Check for waypoints - if present, use wire_path for custom routing
                if( elem.contains( "waypoints" ) && elem["waypoints"].is_array() && elem["waypoints"].size() > 0 )
                {
                    auto waypoints = elem["waypoints"];
                    code << "            waypoints_" << i << " = [\n";
                    for( size_t j = 0; j < waypoints.size(); ++j )
                    {
                        if( waypoints[j].is_array() && waypoints[j].size() >= 2 )
                        {
                            double x = SnapToGrid( waypoints[j][0].get<double>() );
                            double y = SnapToGrid( waypoints[j][1].get<double>() );
                            code << "                (" << x << ", " << y << "),\n";
                        }
                    }
                    code << "            ]\n";
                    code << "            wires_" << i << " = sch.wiring.wire_path((sym1_" << i << ", '" << EscapePythonString( fromPinNum ) << "'), waypoints_" << i << ", (sym2_" << i << ", '" << EscapePythonString( toPinNum ) << "'))\n";
                }
                else
                {
                    // No waypoints - use auto_wire for L-shaped orthogonal routing
                    code << "            wires_" << i << " = sch.wiring.auto_wire(sym1_" << i << ", '" << EscapePythonString( fromPinNum ) << "', sym2_" << i << ", '" << EscapePythonString( toPinNum ) << "')\n";
                }
                code << "            results.append({'index': " << i << ", 'type': 'wire', 'wire_count': len(wires_" << i << ")})\n";
                code << "        else:\n";
                code << "            errors.append({'index': " << i << ", 'error': 'Symbol not found'})\n";
            }
            // Handle wire with points
            else if( elem.contains( "points" ) && elem["points"].is_array() )
            {
                auto points = elem["points"];
                if( points.size() >= 2 )
                {
                    code << "        wc_" << i << " = 0\n";
                    for( size_t j = 0; j < points.size() - 1; ++j )
                    {
                        if( points[j].is_array() && points[j].size() >= 2 &&
                            points[j + 1].is_array() && points[j + 1].size() >= 2 )
                        {
                            double x1 = SnapToGrid( points[j][0].get<double>() );
                            double y1 = SnapToGrid( points[j][1].get<double>() );
                            double x2 = SnapToGrid( points[j + 1][0].get<double>() );
                            double y2 = SnapToGrid( points[j + 1][1].get<double>() );
                            code << "        sch.wiring.add_wire(Vector2.from_xy_mm(" << x1 << ", " << y1 << "), Vector2.from_xy_mm(" << x2 << ", " << y2 << "))\n";
                            code << "        wc_" << i << " += 1\n";
                        }
                    }
                    code << "        results.append({'index': " << i << ", 'type': 'wire', 'wire_count': wc_" << i << "})\n";
                }
            }
        }
        else if( elementType == "junction" )
        {
            double posX = 0, posY = 0;
            if( elem.contains( "position" ) && elem["position"].is_array() &&
                elem["position"].size() >= 2 )
            {
                posX = SnapToGrid( elem["position"][0].get<double>() );
                posY = SnapToGrid( elem["position"][1].get<double>() );
            }

            code << "        pos_" << i << " = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
            code << "        junc_" << i << " = sch.wiring.add_junction(pos_" << i << ")\n";
            code << "        results.append({'index': " << i << ", 'type': 'junction', 'id': get_id(junc_" << i << ")})\n";
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
            code << "        results.append({'index': " << i << ", 'type': 'label', 'id': get_id(lbl_" << i << "), 'text': '" << EscapePythonString( text ) << "'})\n";
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
            code << "        results.append({'index': " << i << ", 'type': 'no_connect', 'id': get_id(nc_" << i << ")})\n";
        }
        else
        {
            code << "        errors.append({'index': " << i << ", 'error': 'Unknown element_type: " << EscapePythonString( elementType ) << "'})\n";
        }

        code << "    except Exception as e_" << i << ":\n";
        code << "        errors.append({'index': " << i << ", 'error': str(e_" << i << ")})\n";
        code << "\n";
    }

    code << "\n";
    code << "    result = {\n";
    code << "        'status': 'success' if len(errors) == 0 else 'partial',\n";
    code << "        'source': 'ipc',\n";
    code << "        'total': " << elements.size() << ",\n";
    code << "        'succeeded': len(results),\n";
    code << "        'failed': len(errors),\n";
    code << "        'results': results\n";
    code << "    }\n";
    code << "    if errors:\n";
    code << "        result['errors'] = errors\n";
    code << "\n";
    code << "except Exception as batch_error:\n";
    code << "    result = {'status': 'error', 'message': str(batch_error), 'partial_results': results, 'errors': errors}\n";
    code << "\n";
    code << "print(json.dumps(result, indent=2))\n";

    return code.str();
}
