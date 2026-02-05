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
           aToolName == "sch_batch_delete";
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
            // Extract just the symbol name from lib_id
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

    return "Executing " + aToolName;
}


bool SCH_CRUD_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    return aToolName == "sch_add" ||
           aToolName == "sch_update" ||
           aToolName == "sch_delete" ||
           aToolName == "sch_batch_delete";
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
    // Convert mm to nanometers (1mm = 1,000,000nm)
    int64_t nm = static_cast<int64_t>( aMm * 1000000.0 );
    return std::to_string( nm );
}


std::string SCH_CRUD_HANDLER::GenerateAddCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string elementType = aInput.value( "element_type", "" );

    code << "import json\n";
    code << "from kipy.schematic import SchematicSymbol, SchematicWire, SchematicJunction, "
            "SchematicLabel, SchematicNoConnect, SchematicSheet\n";
    code << "from kipy.proto.schematic.schematic_types_pb2 import Symbol, Line, Junction, "
            "LocalLabel, GlobalLabel, HierarchicalLabel, NoConnect, Sheet\n";
    code << "from kipy.proto.common.types.base_types_pb2 import LibraryIdentifier\n";
    code << "\n";

    if( elementType == "symbol" || elementType == "power" )
    {
        std::string libId = aInput.value( "lib_id", "" );
        if( libId.empty() )
        {
            code << "print('Error: lib_id is required for symbol/power')\n";
            return code.str();
        }

        // Parse lib_id into library and name
        std::string library, name;
        size_t colonPos = libId.find( ':' );
        if( colonPos != std::string::npos )
        {
            library = libId.substr( 0, colonPos );
            name = libId.substr( colonPos + 1 );
        }
        else
        {
            name = libId;
        }

        // Get position
        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
        }

        // Get angle and mirror
        double angle = aInput.value( "angle", 0.0 );
        std::string mirror = aInput.value( "mirror", "none" );
        bool mirrorX = ( mirror == "x" );
        bool mirrorY = ( mirror == "y" );

        code << "# Create symbol\n";
        code << "sym = Symbol()\n";
        code << "sym.lib_id.library_nickname = '" << EscapePythonString( library ) << "'\n";
        code << "sym.lib_id.entry_name = '" << EscapePythonString( name ) << "'\n";
        code << "sym.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "sym.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "sym.angle.value_degrees = " << angle << "\n";
        code << "sym.mirror_x = " << ( mirrorX ? "True" : "False" ) << "\n";
        code << "sym.mirror_y = " << ( mirrorY ? "True" : "False" ) << "\n";

        // Set unit if specified
        if( aInput.contains( "unit" ) )
        {
            int unit = aInput.value( "unit", 1 );
            code << "sym.unit = " << unit << "\n";
        }

        // Create the symbol
        code << "\n";
        code << "# Add to schematic\n";
        code << "wrapper = SchematicSymbol(proto=sym)\n";
        code << "result = sch.create_items([wrapper])\n";
        code << "\n";

        // Handle properties if specified
        if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
        {
            code << "# Update field values\n";
            code << "if result and len(result) > 0:\n";
            code << "    created = result[0]\n";
            code << "    # Note: Field updates may need to be done through update_items\n";
            // Properties handling would need proper field ID resolution
        }

        code << "# Report result\n";
        code << "if result and len(result) > 0:\n";
        code << "    created = result[0]\n";
        code << "    print(f'Created symbol at (" << posX << ", " << posY << ")')\n";
        code << "    print(f'ID: {created.id.value}')\n";
        code << "else:\n";
        code << "    print('Error: Failed to create symbol')\n";
    }
    else if( elementType == "wire" )
    {
        code << "# Create wire(s)\n";
        code << "wires_to_create = []\n";

        // Check for pin-based wiring
        if( aInput.contains( "from_pin" ) && aInput.contains( "to_pin" ) )
        {
            auto fromPin = aInput["from_pin"];
            auto toPin = aInput["to_pin"];

            std::string fromRef = fromPin.value( "ref", "" );
            std::string fromPinNum = fromPin.value( "pin", "" );
            std::string toRef = toPin.value( "ref", "" );
            std::string toPinNum = toPin.value( "pin", "" );

            code << "# Find pin positions for wire routing\n";
            code << "from_ref = '" << EscapePythonString( fromRef ) << "'\n";
            code << "from_pin_num = '" << EscapePythonString( fromPinNum ) << "'\n";
            code << "to_ref = '" << EscapePythonString( toRef ) << "'\n";
            code << "to_pin_num = '" << EscapePythonString( toPinNum ) << "'\n";
            code << "\n";
            code << "# Get all symbols to find pin positions\n";
            code << "all_symbols = sch.get_items(types=['symbol'])\n";
            code << "\n";
            code << "from_pos = None\n";
            code << "to_pos = None\n";
            code << "\n";
            code << "for sym in all_symbols:\n";
            code << "    ref_field = next((f for f in sym.fields if f.name == 'Reference'), None)\n";
            code << "    if ref_field:\n";
            code << "        ref_val = ref_field.text\n";
            code << "        if ref_val == from_ref:\n";
            code << "            for pin in sym.pins:\n";
            code << "                if pin.number == from_pin_num:\n";
            code << "                    from_pos = (pin.position.x_nm, pin.position.y_nm)\n";
            code << "                    break\n";
            code << "        if ref_val == to_ref:\n";
            code << "            for pin in sym.pins:\n";
            code << "                if pin.number == to_pin_num:\n";
            code << "                    to_pos = (pin.position.x_nm, pin.position.y_nm)\n";
            code << "                    break\n";
            code << "\n";
            code << "if from_pos and to_pos:\n";
            code << "    wire = Line()\n";
            code << "    wire.start.x_nm = from_pos[0]\n";
            code << "    wire.start.y_nm = from_pos[1]\n";
            code << "    wire.end.x_nm = to_pos[0]\n";
            code << "    wire.end.y_nm = to_pos[1]\n";
            code << "    wires_to_create.append(SchematicWire(proto=wire))\n";
            code << "else:\n";
            code << "    print(f'Error: Could not find pins - from: {from_pos}, to: {to_pos}')\n";
        }
        else if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            // Point-based wiring
            auto points = aInput["points"];
            if( points.size() >= 2 )
            {
                code << "# Create wires from points\n";
                code << "points = [\n";
                for( size_t i = 0; i < points.size(); ++i )
                {
                    if( points[i].is_array() && points[i].size() >= 2 )
                    {
                        double x = points[i][0].get<double>();
                        double y = points[i][1].get<double>();
                        code << "    (" << MmToNm( x ) << ", " << MmToNm( y ) << "),\n";
                    }
                }
                code << "]\n";
                code << "\n";
                code << "for i in range(len(points) - 1):\n";
                code << "    wire = Line()\n";
                code << "    wire.start.x_nm = points[i][0]\n";
                code << "    wire.start.y_nm = points[i][1]\n";
                code << "    wire.end.x_nm = points[i + 1][0]\n";
                code << "    wire.end.y_nm = points[i + 1][1]\n";
                code << "    wires_to_create.append(SchematicWire(proto=wire))\n";
            }
        }

        code << "\n";
        code << "if wires_to_create:\n";
        code << "    result = sch.create_items(wires_to_create)\n";
        code << "    print(f'Created {len(result)} wire segment(s)')\n";
        code << "else:\n";
        code << "    print('Error: No wire segments to create')\n";
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

        code << "# Create junction\n";
        code << "junc = Junction()\n";
        code << "junc.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "junc.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "\n";
        code << "wrapper = SchematicJunction(proto=junc)\n";
        code << "result = sch.create_items([wrapper])\n";
        code << "if result:\n";
        code << "    print(f'Created junction at (" << posX << ", " << posY << ")')\n";
        code << "else:\n";
        code << "    print('Error: Failed to create junction')\n";
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

        code << "# Create " << labelType << " label\n";

        if( labelType == "local" )
        {
            code << "label = LocalLabel()\n";
        }
        else if( labelType == "global" )
        {
            code << "label = GlobalLabel()\n";
        }
        else if( labelType == "hierarchical" )
        {
            code << "label = HierarchicalLabel()\n";
        }
        else
        {
            code << "label = LocalLabel()  # Default to local\n";
        }

        code << "label.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "label.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "label.text.text = '" << EscapePythonString( text ) << "'\n";
        code << "\n";
        code << "wrapper = SchematicLabel(proto=label)\n";
        code << "result = sch.create_items([wrapper])\n";
        code << "if result:\n";
        code << "    print(f'Created " << labelType << " label: " << EscapePythonString( text )
             << "')\n";
        code << "else:\n";
        code << "    print('Error: Failed to create label')\n";
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

        code << "# Create no-connect marker\n";
        code << "nc = NoConnect()\n";
        code << "nc.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "nc.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "\n";
        code << "wrapper = SchematicNoConnect(proto=nc)\n";
        code << "result = sch.create_items([wrapper])\n";
        code << "if result:\n";
        code << "    print(f'Created no-connect at (" << posX << ", " << posY << ")')\n";
        code << "else:\n";
        code << "    print('Error: Failed to create no-connect')\n";
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

        code << "# Create hierarchical sheet\n";
        code << "sheet = Sheet()\n";
        code << "sheet.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "sheet.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "sheet.size.x_nm = " << MmToNm( sizeW ) << "\n";
        code << "sheet.size.y_nm = " << MmToNm( sizeH ) << "\n";
        code << "sheet.name = '" << EscapePythonString( sheetName ) << "'\n";
        if( !sheetFile.empty() )
        {
            code << "sheet.filename = '" << EscapePythonString( sheetFile ) << "'\n";
        }
        code << "\n";
        code << "wrapper = SchematicSheet(proto=sheet)\n";
        code << "result = sch.create_items([wrapper])\n";
        code << "if result:\n";
        code << "    print(f'Created sheet: " << EscapePythonString( sheetName ) << "')\n";
        code << "else:\n";
        code << "    print('Error: Failed to create sheet')\n";
    }
    else
    {
        code << "print('Error: Unknown element_type: " << EscapePythonString( elementType )
             << "')\n";
    }

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateUpdateCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    if( target.empty() )
    {
        code << "print('Error: target is required')\n";
        return code.str();
    }

    code << "import json\n";
    code << "\n";
    code << "# Find target element\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "target_item = None\n";
    code << "\n";

    // Check if target looks like a UUID
    code << "# Check if target is a UUID or reference\n";
    code << "import re\n";
    code << "is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "\n";
    code << "if is_uuid:\n";
    code << "    # Get by UUID\n";
    code << "    from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "    kiid = KIID(value=target)\n";
    code << "    items = sch.get_items_by_id([kiid])\n";
    code << "    if items:\n";
    code << "        target_item = items[0]\n";
    code << "else:\n";
    code << "    # Search by reference\n";
    code << "    all_symbols = sch.get_items(types=['symbol'])\n";
    code << "    for sym in all_symbols:\n";
    code << "        ref_field = next((f for f in sym.fields if f.name == 'Reference'), None)\n";
    code << "        if ref_field and ref_field.text == target:\n";
    code << "            target_item = sym\n";
    code << "            break\n";
    code << "\n";
    code << "if not target_item:\n";
    code << "    print(f'Error: Could not find element: {target}')\n";
    code << "else:\n";
    code << "    # Apply updates\n";
    code << "    updated = False\n";

    // Position update
    if( aInput.contains( "position" ) && aInput["position"].is_array() &&
        aInput["position"].size() >= 2 )
    {
        double posX = aInput["position"][0].get<double>();
        double posY = aInput["position"][1].get<double>();
        code << "    target_item.position.x_nm = " << MmToNm( posX ) << "\n";
        code << "    target_item.position.y_nm = " << MmToNm( posY ) << "\n";
        code << "    updated = True\n";
    }

    // Angle update
    if( aInput.contains( "angle" ) )
    {
        double angle = aInput.value( "angle", 0.0 );
        code << "    if hasattr(target_item, 'angle'):\n";
        code << "        target_item.angle.value_degrees = " << angle << "\n";
        code << "        updated = True\n";
    }

    // Mirror update
    if( aInput.contains( "mirror" ) )
    {
        std::string mirror = aInput.value( "mirror", "none" );
        code << "    if hasattr(target_item, 'mirror_x'):\n";
        code << "        target_item.mirror_x = " << ( mirror == "x" ? "True" : "False" ) << "\n";
        code << "        target_item.mirror_y = " << ( mirror == "y" ? "True" : "False" ) << "\n";
        code << "        updated = True\n";
    }

    // Unit update
    if( aInput.contains( "unit" ) )
    {
        int unit = aInput.value( "unit", 1 );
        code << "    if hasattr(target_item, 'unit'):\n";
        code << "        target_item.unit = " << unit << "\n";
        code << "        updated = True\n";
    }

    // DNP and exclusion flags
    if( aInput.contains( "dnp" ) )
    {
        bool dnp = aInput.value( "dnp", false );
        code << "    if hasattr(target_item, 'dnp'):\n";
        code << "        target_item.dnp = " << ( dnp ? "True" : "False" ) << "\n";
        code << "        updated = True\n";
    }
    if( aInput.contains( "exclude_from_bom" ) )
    {
        bool flag = aInput.value( "exclude_from_bom", false );
        code << "    if hasattr(target_item, 'exclude_from_bom'):\n";
        code << "        target_item.exclude_from_bom = " << ( flag ? "True" : "False" ) << "\n";
        code << "        updated = True\n";
    }
    if( aInput.contains( "exclude_from_board" ) )
    {
        bool flag = aInput.value( "exclude_from_board", false );
        code << "    if hasattr(target_item, 'exclude_from_board'):\n";
        code << "        target_item.exclude_from_board = " << ( flag ? "True" : "False" ) << "\n";
        code << "        updated = True\n";
    }

    // Properties/fields update
    if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
    {
        code << "    # Update field values\n";
        code << "    if hasattr(target_item, 'fields'):\n";
        code << "        props_to_update = " << aInput["properties"].dump() << "\n";
        code << "        for field in target_item.fields:\n";
        code << "            if field.name in props_to_update:\n";
        code << "                field.text = props_to_update[field.name]\n";
        code << "                updated = True\n";
    }

    code << "\n";
    code << "    if updated:\n";
    code << "        result = sch.update_items([target_item])\n";
    code << "        print(f'Updated {target}')\n";
    code << "    else:\n";
    code << "        print('No updates specified')\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    if( target.empty() )
    {
        code << "print('Error: target is required')\n";
        return code.str();
    }

    code << "import re\n";
    code << "from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "target_id = None\n";
    code << "\n";
    code << "# Check if target is a UUID\n";
    code << "is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "\n";
    code << "if is_uuid:\n";
    code << "    target_id = KIID(value=target)\n";
    code << "else:\n";
    code << "    # Search by reference\n";
    code << "    all_symbols = sch.get_items(types=['symbol'])\n";
    code << "    for sym in all_symbols:\n";
    code << "        ref_field = next((f for f in sym.fields if f.name == 'Reference'), None)\n";
    code << "        if ref_field and ref_field.text == target:\n";
    code << "            target_id = sym.id\n";
    code << "            break\n";
    code << "\n";
    code << "if target_id:\n";
    code << "    result = sch.delete_items([target_id])\n";
    code << "    print(f'Deleted {target}')\n";
    code << "else:\n";
    code << "    print(f'Error: Could not find element: {target}')\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateBatchDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "targets" ) || !aInput["targets"].is_array() )
    {
        code << "print('Error: targets array is required')\n";
        return code.str();
    }

    auto targets = aInput["targets"];

    code << "import re\n";
    code << "from kipy.proto.common.types.base_types_pb2 import KIID\n";
    code << "\n";
    code << "targets = [";
    for( size_t i = 0; i < targets.size(); ++i )
    {
        code << "'" << EscapePythonString( targets[i].get<std::string>() ) << "'";
        if( i < targets.size() - 1 )
            code << ", ";
    }
    code << "]\n";
    code << "\n";
    code << "ids_to_delete = []\n";
    code << "not_found = []\n";
    code << "\n";
    code << "# Pre-fetch all symbols for reference lookup\n";
    code << "all_symbols = sch.get_items(types=['symbol'])\n";
    code << "ref_to_id = {}\n";
    code << "for sym in all_symbols:\n";
    code << "    ref_field = next((f for f in sym.fields if f.name == 'Reference'), None)\n";
    code << "    if ref_field:\n";
    code << "        ref_to_id[ref_field.text] = sym.id\n";
    code << "\n";
    code << "for target in targets:\n";
    code << "    is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "    if is_uuid:\n";
    code << "        ids_to_delete.append(KIID(value=target))\n";
    code << "    elif target in ref_to_id:\n";
    code << "        ids_to_delete.append(ref_to_id[target])\n";
    code << "    else:\n";
    code << "        not_found.append(target)\n";
    code << "\n";
    code << "if ids_to_delete:\n";
    code << "    result = sch.delete_items(ids_to_delete)\n";
    code << "    print(f'Deleted {len(ids_to_delete)} element(s)')\n";
    code << "\n";
    code << "if not_found:\n";
    code << "    print(f'Warning: Could not find: {not_found}')\n";

    return code.str();
}
