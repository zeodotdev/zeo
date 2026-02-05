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
    code << "from kipy.geometry import Vector2\n";
    code << "\n";

    if( elementType == "symbol" )
    {
        std::string libId = aInput.value( "lib_id", "" );
        if( libId.empty() )
        {
            code << "print('Error: lib_id is required for symbol')\n";
            return code.str();
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
        int unit = aInput.value( "unit", 1 );

        code << "# Add symbol using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    symbol = sch.symbols.add(\n";
        code << "        lib_id='" << EscapePythonString( libId ) << "',\n";
        code << "        position=pos,\n";
        code << "        unit=" << unit << ",\n";
        code << "        angle=" << angle << ",\n";
        code << "        mirror_x=" << ( mirrorX ? "True" : "False" ) << ",\n";
        code << "        mirror_y=" << ( mirrorY ? "True" : "False" ) << "\n";
        code << "    )\n";

        // Handle properties if specified
        if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
        {
            code << "    # Update field values\n";
            code << "    props = " << aInput["properties"].dump() << "\n";
            code << "    if 'Value' in props:\n";
            code << "        sch.symbols.set_value(symbol, props['Value'])\n";
            code << "    if 'Footprint' in props:\n";
            code << "        sch.symbols.set_footprint(symbol, props['Footprint'])\n";
        }

        code << "    print(f'Created symbol {symbol.reference} at (" << posX << ", " << posY << ")')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(symbol.id.value), 'reference': symbol.reference}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating symbol: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
    }
    else if( elementType == "power" )
    {
        std::string libId = aInput.value( "lib_id", "" );
        if( libId.empty() )
        {
            code << "print('Error: lib_id is required for power symbol')\n";
            return code.str();
        }

        // Extract power name from lib_id (e.g., "power:GND" -> "GND")
        std::string powerName = libId;
        size_t colonPos = libId.find( ':' );
        if( colonPos != std::string::npos )
        {
            powerName = libId.substr( colonPos + 1 );
        }

        double posX = 0, posY = 0;
        if( aInput.contains( "position" ) && aInput["position"].is_array() &&
            aInput["position"].size() >= 2 )
        {
            posX = aInput["position"][0].get<double>();
            posY = aInput["position"][1].get<double>();
        }

        double angle = aInput.value( "angle", 0.0 );

        code << "# Add power symbol using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    symbol = sch.labels.add_power('" << EscapePythonString( powerName ) << "', pos, angle=" << angle << ")\n";
        code << "    print(f'Created power symbol " << EscapePythonString( powerName ) << " at (" << posX << ", " << posY << ")')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(symbol.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating power symbol: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
    }
    else if( elementType == "wire" )
    {
        code << "# Create wire(s) using high-level API\n";
        code << "try:\n";

        // Check for pin-based wiring
        if( aInput.contains( "from_pin" ) && aInput.contains( "to_pin" ) )
        {
            auto fromPin = aInput["from_pin"];
            auto toPin = aInput["to_pin"];

            std::string fromRef = fromPin.value( "ref", "" );
            std::string fromPinNum = fromPin.value( "pin", "" );
            std::string toRef = toPin.value( "ref", "" );
            std::string toPinNum = toPin.value( "pin", "" );

            code << "    # Find symbols by reference\n";
            code << "    sym1 = sch.symbols.get_by_ref('" << EscapePythonString( fromRef ) << "')\n";
            code << "    sym2 = sch.symbols.get_by_ref('" << EscapePythonString( toRef ) << "')\n";
            code << "    if not sym1:\n";
            code << "        raise ValueError('Symbol not found: " << EscapePythonString( fromRef ) << "')\n";
            code << "    if not sym2:\n";
            code << "        raise ValueError('Symbol not found: " << EscapePythonString( toRef ) << "')\n";
            code << "    wire = sch.wiring.wire_pins(sym1, '" << EscapePythonString( fromPinNum ) << "', sym2, '" << EscapePythonString( toPinNum ) << "')\n";
            code << "    print('Created wire between pins')\n";
            code << "    print(json.dumps({'status': 'success'}))\n";
        }
        else if( aInput.contains( "points" ) && aInput["points"].is_array() )
        {
            // Point-based wiring
            auto points = aInput["points"];
            if( points.size() >= 2 )
            {
                code << "    # Create wires from points\n";
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

                        code << "    start = Vector2.from_xy_mm(" << x1 << ", " << y1 << ")\n";
                        code << "    end = Vector2.from_xy_mm(" << x2 << ", " << y2 << ")\n";
                        code << "    sch.wiring.add_wire(start, end)\n";
                        code << "    wires_created += 1\n";
                    }
                }

                code << "    print(f'Created {wires_created} wire segment(s)')\n";
                code << "    print(json.dumps({'status': 'success', 'count': wires_created}))\n";
            }
            else
            {
                code << "    print('Error: Need at least 2 points for wire')\n";
                code << "    print(json.dumps({'status': 'error', 'message': 'Need at least 2 points'}))\n";
            }
        }
        else
        {
            code << "    print('Error: Either from_pin/to_pin or points required for wire')\n";
            code << "    print(json.dumps({'status': 'error', 'message': 'Missing wire endpoints'}))\n";
        }

        code << "except Exception as e:\n";
        code << "    print(f'Error creating wire: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
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

        code << "# Create junction using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    junction = sch.wiring.add_junction(pos)\n";
        code << "    print(f'Created junction at (" << posX << ", " << posY << ")')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(junction.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating junction: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
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

        code << "# Create " << labelType << " label using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";

        if( labelType == "local" )
        {
            code << "    label = sch.labels.add_local('" << EscapePythonString( text ) << "', pos)\n";
        }
        else if( labelType == "global" )
        {
            code << "    label = sch.labels.add_global('" << EscapePythonString( text ) << "', pos)\n";
        }
        else if( labelType == "hierarchical" )
        {
            code << "    label = sch.labels.add_hierarchical('" << EscapePythonString( text ) << "', pos)\n";
        }
        else
        {
            code << "    label = sch.labels.add_local('" << EscapePythonString( text ) << "', pos)  # Default to local\n";
        }

        code << "    print(f'Created " << labelType << " label: " << EscapePythonString( text ) << "')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(label.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating label: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
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

        code << "# Create no-connect marker using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    nc = sch.wiring.add_no_connect(pos)\n";
        code << "    print(f'Created no-connect at (" << posX << ", " << posY << ")')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(nc.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating no-connect: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
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

        code << "# Create hierarchical sheet using high-level API\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    size = Vector2.from_xy_mm(" << sizeW << ", " << sizeH << ")\n";
        code << "    sheet = sch.sheets.create(\n";
        code << "        name='" << EscapePythonString( sheetName ) << "',\n";
        code << "        filename='" << EscapePythonString( sheetFile.empty() ? sheetName + ".kicad_sch" : sheetFile ) << "',\n";
        code << "        position=pos,\n";
        code << "        size=size\n";
        code << "    )\n";
        code << "    print(f'Created sheet: " << EscapePythonString( sheetName ) << "')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(sheet.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating sheet: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
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

        double angle = aInput.value( "angle", 0.0 );

        code << "# Create bus entry\n";
        code << "try:\n";
        code << "    pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "    entry = sch.buses.add_bus_entry(pos, angle=" << angle << ")\n";
        code << "    print(f'Created bus entry at (" << posX << ", " << posY << ")')\n";
        code << "    print(json.dumps({'status': 'success', 'id': str(entry.id.value)}))\n";
        code << "except Exception as e:\n";
        code << "    print(f'Error creating bus entry: {e}')\n";
        code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";
    }
    else
    {
        code << "print('Error: Unknown element_type: " << EscapePythonString( elementType ) << "')\n";
        code << "print(json.dumps({'status': 'error', 'message': 'Unknown element_type: " << EscapePythonString( elementType ) << "'}))\n";
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
        code << "print(json.dumps({'status': 'error', 'message': 'target is required'}))\n";
        return code.str();
    }

    code << "import json, re\n";
    code << "from kipy.geometry import Vector2\n";
    code << "\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "target_item = None\n";
    code << "\n";

    // Check if target looks like a UUID
    code << "# Check if target is a UUID or reference\n";
    code << "is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "\n";
    code << "try:\n";
    code << "    if is_uuid:\n";
    code << "        # Get by UUID using crud API\n";
    code << "        items = sch.crud.get_by_id([target])\n";
    code << "        if items:\n";
    code << "            target_item = items[0]\n";
    code << "    else:\n";
    code << "        # Search by reference\n";
    code << "        target_item = sch.symbols.get_by_ref(target)\n";
    code << "\n";
    code << "    if not target_item:\n";
    code << "        print(f'Error: Could not find element: {target}')\n";
    code << "        print(json.dumps({'status': 'error', 'message': f'Element not found: {target}'}))\n";
    code << "    else:\n";
    code << "        # Apply updates\n";
    code << "        updated = False\n";

    // Position update
    if( aInput.contains( "position" ) && aInput["position"].is_array() &&
        aInput["position"].size() >= 2 )
    {
        double posX = aInput["position"][0].get<double>();
        double posY = aInput["position"][1].get<double>();
        code << "        # Update position\n";
        code << "        new_pos = Vector2.from_xy_mm(" << posX << ", " << posY << ")\n";
        code << "        if hasattr(target_item, 'position'):\n";
        code << "            target_item = sch.symbols.move(target_item, new_pos)\n";
        code << "            updated = True\n";
    }

    // Angle update
    if( aInput.contains( "angle" ) )
    {
        double angle = aInput.value( "angle", 0.0 );
        code << "        # Update rotation\n";
        code << "        if hasattr(target_item, 'angle'):\n";
        code << "            target_item = sch.symbols.rotate(target_item, " << angle << ")\n";
        code << "            updated = True\n";
    }

    // Mirror update
    if( aInput.contains( "mirror" ) )
    {
        std::string mirror = aInput.value( "mirror", "none" );
        if( mirror == "x" || mirror == "y" )
        {
            code << "        # Update mirror\n";
            code << "        target_item = sch.symbols.mirror(target_item, '" << mirror << "')\n";
            code << "        updated = True\n";
        }
    }

    // Properties/fields update
    if( aInput.contains( "properties" ) && aInput["properties"].is_object() )
    {
        code << "        # Update field values\n";
        code << "        props_to_update = " << aInput["properties"].dump() << "\n";
        code << "        if 'Value' in props_to_update:\n";
        code << "            sch.symbols.set_value(target_item, props_to_update['Value'])\n";
        code << "            updated = True\n";
        code << "        if 'Footprint' in props_to_update:\n";
        code << "            sch.symbols.set_footprint(target_item, props_to_update['Footprint'])\n";
        code << "            updated = True\n";
    }

    code << "\n";
    code << "        if updated:\n";
    code << "            print(f'Updated {target}')\n";
    code << "            print(json.dumps({'status': 'success', 'target': target}))\n";
    code << "        else:\n";
    code << "            print('No updates specified')\n";
    code << "            print(json.dumps({'status': 'success', 'message': 'No changes made'}))\n";
    code << "except Exception as e:\n";
    code << "    print(f'Error updating element: {e}')\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    std::string target = aInput.value( "target", "" );
    if( target.empty() )
    {
        code << "print('Error: target is required')\n";
        code << "print(json.dumps({'status': 'error', 'message': 'target is required'}))\n";
        return code.str();
    }

    code << "import json, re\n";
    code << "\n";
    code << "target = '" << EscapePythonString( target ) << "'\n";
    code << "target_item = None\n";
    code << "\n";
    code << "# Check if target is a UUID\n";
    code << "is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "\n";
    code << "try:\n";
    code << "    if is_uuid:\n";
    code << "        # Delete by UUID using crud API\n";
    code << "        items = sch.crud.get_by_id([target])\n";
    code << "        if items:\n";
    code << "            target_item = items[0]\n";
    code << "    else:\n";
    code << "        # Search by reference\n";
    code << "        target_item = sch.symbols.get_by_ref(target)\n";
    code << "\n";
    code << "    if target_item:\n";
    code << "        sch.crud.remove_items([target_item])\n";
    code << "        print(f'Deleted {target}')\n";
    code << "        print(json.dumps({'status': 'success', 'target': target}))\n";
    code << "    else:\n";
    code << "        print(f'Error: Could not find element: {target}')\n";
    code << "        print(json.dumps({'status': 'error', 'message': f'Element not found: {target}'}))\n";
    code << "except Exception as e:\n";
    code << "    print(f'Error deleting element: {e}')\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

    return code.str();
}


std::string SCH_CRUD_HANDLER::GenerateBatchDeleteCode( const nlohmann::json& aInput ) const
{
    std::ostringstream code;

    if( !aInput.contains( "targets" ) || !aInput["targets"].is_array() )
    {
        code << "print('Error: targets array is required')\n";
        code << "print(json.dumps({'status': 'error', 'message': 'targets array is required'}))\n";
        return code.str();
    }

    auto targets = aInput["targets"];

    code << "import json, re\n";
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
    code << "items_to_delete = []\n";
    code << "not_found = []\n";
    code << "\n";
    code << "try:\n";
    code << "    for target in targets:\n";
    code << "        is_uuid = bool(re.match(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
            "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$', target))\n";
    code << "        if is_uuid:\n";
    code << "            items = sch.crud.get_by_id([target])\n";
    code << "            if items:\n";
    code << "                items_to_delete.append(items[0])\n";
    code << "            else:\n";
    code << "                not_found.append(target)\n";
    code << "        else:\n";
    code << "            item = sch.symbols.get_by_ref(target)\n";
    code << "            if item:\n";
    code << "                items_to_delete.append(item)\n";
    code << "            else:\n";
    code << "                not_found.append(target)\n";
    code << "\n";
    code << "    if items_to_delete:\n";
    code << "        sch.crud.remove_items(items_to_delete)\n";
    code << "        print(f'Deleted {len(items_to_delete)} element(s)')\n";
    code << "\n";
    code << "    result = {'status': 'success', 'deleted': len(items_to_delete)}\n";
    code << "    if not_found:\n";
    code << "        result['not_found'] = not_found\n";
    code << "        print(f'Warning: Could not find: {not_found}')\n";
    code << "    print(json.dumps(result))\n";
    code << "except Exception as e:\n";
    code << "    print(f'Error during batch delete: {e}')\n";
    code << "    print(json.dumps({'status': 'error', 'message': str(e)}))\n";

    return code.str();
}
