/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sch_crud_handler.h"
#include <sstream>
#include <iomanip>


bool SCH_CRUD_HANDLER::CanHandle( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_batch_delete" ||
           aToolName == "sch_open_sheet";
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
        std::string elementType = aInput.value( "element_type", "element" );
        std::string libId = aInput.value( "lib_id", "" );

        if( elementType == "symbol" && !libId.empty() )
        {
            size_t colonPos = libId.find( ':' );
            std::string symbolName = ( colonPos != std::string::npos )
                                         ? libId.substr( colonPos + 1 )
                                         : libId;
            return "Adding " + symbolName;
        }
        else if( elementType == "wire" )
        {
            return "Adding wire connection";
        }
        else if( elementType == "label" )
        {
            std::string text = aInput.value( "text", "" );
            if( !text.empty() )
                return "Adding label: " + text;
            return "Adding label";
        }
        else if( elementType == "power" )
        {
            std::string libId2 = aInput.value( "lib_id", "" );
            if( !libId2.empty() )
            {
                size_t colonPos = libId2.find( ':' );
                std::string name = ( colonPos != std::string::npos )
                                       ? libId2.substr( colonPos + 1 )
                                       : libId2;
                return "Adding power: " + name;
            }
            return "Adding power symbol";
        }
        else if( elementType == "junction" )
        {
            return "Adding junction";
        }
        else if( elementType == "no_connect" )
        {
            return "Adding no-connect marker";
        }
        else if( elementType == "sheet" )
        {
            std::string sheetName = aInput.value( "sheet_name", "" );
            if( !sheetName.empty() )
                return "Adding sheet: " + sheetName;
            return "Adding hierarchical sheet";
        }

        return "Adding " + elementType;
    }
    else if( aToolName == "sch_update" )
    {
        std::string target = aInput.value( "target", "" );
        if( !target.empty() )
            return "Updating " + target;
        return "Updating element";
    }
    else if( aToolName == "sch_delete" )
    {
        std::string target = aInput.value( "target", "" );
        if( !target.empty() )
            return "Deleting " + target;
        return "Deleting element";
    }
    else if( aToolName == "sch_batch_delete" )
    {
        if( aInput.contains( "targets" ) && aInput["targets"].is_array() )
        {
            size_t count = aInput["targets"].size();
            return "Deleting " + std::to_string( count ) + " elements";
        }
        return "Batch deleting elements";
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

    return "Executing " + aToolName;
}


bool SCH_CRUD_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_batch_delete" ||
           aToolName == "sch_open_sheet";
}


std::string SCH_CRUD_HANDLER::GetIPCCommand( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string code;

    if( aToolName == "sch_add" )
        code = GenerateAddCode( aInput );
    else if( aToolName == "sch_update" )
        code = GenerateUpdateCode( aInput );
    else if( aToolName == "sch_delete" )
        code = GenerateDeleteCode( aInput );
    else if( aToolName == "sch_batch_delete" )
        code = GenerateBatchDeleteCode( aInput );
    else if( aToolName == "sch_open_sheet" )
        code = GenerateOpenSheetCode( aInput );

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


std::string SCH_CRUD_HANDLER::MmToNm( double aMm ) const
{
    int64_t nm = static_cast<int64_t>( aMm * 1000000.0 );
    return std::to_string( nm );
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
            code << "    sys.exit(1)\n";
            return code.str();
        }

        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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

        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(symbol), 'reference': getattr(symbol, 'reference', '')}\n";
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
        }

        double angle = aInput.value( "angle", 0.0 );

        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    symbol = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pos, angle=" << angle << ")\n";
        code << "    result = {'status': 'success', 'source': 'ipc', 'id': get_id(symbol)}\n";
    }
    else if( elementType == "wire" )
    {
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
            code << "    wire = sch.wiring.wire_pins(sym1, '" << EscapePythonString( fromPinNum ) << "', sym2, '" << EscapePythonString( toPinNum ) << "')\n";
            code << "    result = {'status': 'success', 'source': 'ipc'}\n";
        }
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
                        double x1 = points[i][0].get<double>();
                        double y1 = points[i][1].get<double>();
                        double x2 = points[i + 1][0].get<double>();
                        double y2 = points[i + 1][1].get<double>();

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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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

    // File fallback
    code << "# File-based fallback if IPC failed\n";
    code << "if not use_ipc and file_path:\n";
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
                    double x1 = points[i][0].get<double>();
                    double y1 = points[i][1].get<double>();
                    double x2 = points[i + 1][0].get<double>();
                    double y2 = points[i + 1][1].get<double>();

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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
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
        double posX = aInput["position"][0].get<double>();
        double posY = aInput["position"][1].get<double>();
        code << "    new_pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    target_item = sch.symbols.move(target_item, new_pos)\n";
        code << "    updated = True\n";
    }

    if( aInput.contains( "angle" ) )
    {
        double angle = aInput.value( "angle", 0.0 );
        code << "    target_item = sch.symbols.rotate(target_item, " << angle << ")\n";
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

    code << "    result = {'status': 'success', 'source': 'ipc', 'target': target, 'updated': updated}\n";
    code << "\n";
    code << "except Exception as ipc_error:\n";
    code << "    use_ipc = False\n";
    code << "    ipc_error_msg = str(ipc_error)\n";
    code << "    print(f'IPC failed: {ipc_error_msg}', file=sys.stderr)\n";
    code << "\n";

    // File fallback for update is complex - skip for now
    code << "# File-based update fallback not yet implemented\n";
    code << "if not use_ipc:\n";
    code << "    result = {'status': 'error', 'message': f'IPC failed and file fallback not available for update: {ipc_error_msg}'}\n";
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

    // File fallback
    code << "# File-based fallback if IPC failed\n";
    code << "if not use_ipc and file_path and target_uuid:\n";
    code << "    try:\n";
    code << GenerateFileFallbackHeader();
    code << "        if delete_element_from_file(file_path, target_uuid):\n";
    code << "            result = {'status': 'success', 'source': 'file', 'target': target}\n";
    code << "        else:\n";
    code << "            result = {'status': 'error', 'message': f'Element not found in file: {target}'}\n";
    code << "    except Exception as file_error:\n";
    code << "        result = {'status': 'error', 'message': f'Both IPC and file fallback failed. IPC: {ipc_error_msg}, File: {str(file_error)}'}\n";
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

    code << "import json, re, sys\n";
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
    code << "    if items_to_delete:\n";
    code << "        sch.crud.remove_items(items_to_delete)\n";
    code << "    result = {'status': 'success', 'source': 'ipc', 'deleted': len(items_to_delete)}\n";
    code << "    if not_found:\n";
    code << "        result['not_found'] = not_found\n";
    code << "\n";
    code << "except Exception as ipc_error:\n";
    code << "    use_ipc = False\n";
    code << "    ipc_error_msg = str(ipc_error)\n";
    code << "    print(f'IPC failed: {ipc_error_msg}', file=sys.stderr)\n";
    code << "\n";

    // File fallback
    code << "# File-based fallback if IPC failed\n";
    code << "if not use_ipc and file_path and target_uuids:\n";
    code << "    try:\n";
    code << GenerateFileFallbackHeader();
    code << "        deleted_count = 0\n";
    code << "        for uuid in target_uuids:\n";
    code << "            if delete_element_from_file(file_path, uuid):\n";
    code << "                deleted_count += 1\n";
    code << "        result = {'status': 'success', 'source': 'file', 'deleted': deleted_count}\n";
    code << "    except Exception as file_error:\n";
    code << "        result = {'status': 'error', 'message': f'Both IPC and file fallback failed. IPC: {ipc_error_msg}, File: {str(file_error)}'}\n";
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
    code << "sheet_path = " << nlohmann::json( sheetPath ).dump() << "\n";
    code << "file_path = " << nlohmann::json( filePath ).dump() << "\n";
    code << "result = None\n";
    code << "\n";
    code << "try:\n";
    code << "    # Get all sheets in the design\n";
    code << "    sheets = sch.crud.get_sheets()\n";
    code << "    \n";
    code << "    # Build hierarchy info with UUIDs\n";
    code << "    hierarchy = []\n";
    code << "    sheet_by_name = {}\n";
    code << "    sheet_by_file = {}\n";
    code << "    sheet_by_uuid = {}\n";
    code << "    \n";
    code << "    for sheet in sheets:\n";
    code << "        name = getattr(sheet, 'name', '')\n";
    code << "        filename = getattr(sheet, 'filename', '')\n";
    code << "        uuid = str(sheet.id.value) if hasattr(sheet, 'id') and hasattr(sheet.id, 'value') else str(getattr(sheet, 'id', getattr(sheet, 'uuid', '')))\n";
    code << "        info = {'name': name, 'file': filename, 'uuid': uuid}\n";
    code << "        hierarchy.append(info)\n";
    code << "        if name:\n";
    code << "            sheet_by_name[name] = (sheet, info)\n";
    code << "        if filename:\n";
    code << "            sheet_by_file[filename] = (sheet, info)\n";
    code << "        if uuid:\n";
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
    code << "            # Path format like '/uuid1/uuid2' - navigate to last UUID\n";
    code << "            parts = [p for p in sheet_path.split('/') if p]\n";
    code << "            if parts:\n";
    code << "                last_uuid = parts[-1]\n";
    code << "                if last_uuid in sheet_by_uuid:\n";
    code << "                    target_sheet, target_info = sheet_by_uuid[last_uuid]\n";
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
    code << "        # First try navigate_to with the SheetPath (most reliable)\n";
    code << "        if hasattr(sch.sheets, 'navigate_to') and hasattr(target_sheet, 'path'):\n";
    code << "            sch.sheets.navigate_to(target_sheet.path)\n";
    code << "            navigated = True\n";
    code << "        elif hasattr(sch.sheets, 'enter'):\n";
    code << "            # enter might work with the node directly\n";
    code << "            sch.sheets.enter(target_sheet)\n";
    code << "            navigated = True\n";
    code << "        elif hasattr(sch.sheets, 'open'):\n";
    code << "            sch.sheets.open(target_sheet)\n";
    code << "            navigated = True\n";
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
