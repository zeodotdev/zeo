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

#include "sch_tool_handler.h"
#include "sch_parser.h"
#include "sch_validator.h"
#include "../kicad_file/file_writer.h"
#include "../kicad_file/uuid_util.h"
#include "../kicad_file/sexpr_util.h"
#include <wx/string.h>
#include <regex>
#include <sstream>

namespace
{

/**
 * Check if a UUID is missing, empty, or a placeholder.
 * Placeholders include all-zeros UUID and empty strings.
 *
 * @param aUuid The UUID to check.
 * @return true if the UUID needs to be replaced.
 */
bool NeedsUuidReplacement( const std::string& aUuid )
{
    if( aUuid.empty() )
        return true;

    // Check for all-zeros placeholder
    if( aUuid == "00000000-0000-0000-0000-000000000000" )
        return true;

    // Check if it's a valid UUID format
    if( !UuidUtil::IsValidUuid( aUuid ) )
        return true;

    return false;
}


/**
 * Insert or replace a UUID in an element's S-expression.
 * If the element has an existing (uuid ...) expression, it's replaced.
 * If not, a new (uuid "...") is appended before the closing parenthesis.
 *
 * @param aElement The element S-expression string.
 * @param aNewUuid The UUID to insert.
 * @return The modified element with the new UUID.
 */
std::string InsertOrReplaceUuid( const std::string& aElement, const std::string& aNewUuid )
{
    // Try to match and replace existing UUID (quoted or unquoted)
    std::regex existingUuid(
        R"(\(uuid\s+\"?[0-9a-fA-F-]*\"?\))"
    );

    std::string replacement = "(uuid \"" + aNewUuid + "\")";

    if( std::regex_search( aElement, existingUuid ) )
    {
        return std::regex_replace( aElement, existingUuid, replacement );
    }

    // No existing UUID - insert before the final closing parenthesis
    // Find the last ')' that closes the main element
    size_t lastParen = aElement.rfind( ')' );
    if( lastParen == std::string::npos )
        return aElement;  // Malformed, let validation catch it

    // Insert the UUID before the closing paren, with proper formatting
    std::string result = aElement.substr( 0, lastParen );

    // Check if we need to add whitespace/newline
    if( !result.empty() && result.back() != '\n' && result.back() != ' ' && result.back() != '\t' )
        result += "\n    ";

    result += replacement;
    result += "\n";
    result += aElement.substr( lastParen );

    return result;
}


/**
 * Ensure an element has a valid, unique UUID.
 * If the element is missing a UUID, has an empty UUID, or has a placeholder/duplicate UUID,
 * a new unique UUID is generated and inserted.
 *
 * @param aElement The element S-expression string.
 * @param aExistingUuids Set of UUIDs already in use in the schematic.
 * @return The element with a valid unique UUID.
 */
std::string EnsureElementHasUuid( const std::string& aElement,
                                   const std::set<std::string>& aExistingUuids )
{
    // Parse the element to check for existing UUID
    auto parsed = SexprUtil::Parse( aElement );
    if( !parsed )
        return aElement;  // Let validation catch syntax errors

    // Look for existing UUID
    auto uuidExpr = SexprUtil::FindFirstChild( parsed.get(), "uuid" );

    if( uuidExpr )
    {
        std::string uuid = SexprUtil::GetStringValue( uuidExpr );

        // If UUID is valid and unique, keep it unchanged
        if( !NeedsUuidReplacement( uuid ) &&
            UuidUtil::IsUuidUnique( uuid, aExistingUuids ) )
        {
            return aElement;
        }
    }

    // Generate a new unique UUID
    std::string newUuid = UuidUtil::GenerateUniqueUuid( aExistingUuids );

    // Insert or replace the UUID in the element
    return InsertOrReplaceUuid( aElement, newUuid );
}

} // anonymous namespace


// Static list of tool names this handler supports
static const char* SCH_TOOL_NAMES[] = {
    "sch_get_summary",
    "sch_file_summary",
    "sch_live_summary",
    "sch_read_section",
    // "sch_modify",  // DISABLED: Use sch_add/sch_update/sch_delete instead
    "sch_validate",
    "sch_export_spice_netlist",
    "sch_get_pins"  // Lightweight pin lookup for a single symbol
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
    if( aToolName == "sch_get_summary" || aToolName == "sch_file_summary" )
        return ExecuteGetSummary( aInput );
    else if( aToolName == "sch_live_summary" )
        // sch_live_summary requires IPC - if Execute is called, IPC failed
        return "Error: sch_live_summary requires IPC execution. Schematic editor must be open.";
    else if( aToolName == "sch_get_pins" )
        // sch_get_pins requires IPC - if Execute is called, IPC failed
        return "Error: sch_get_pins requires IPC execution. Schematic editor must be open.";
    else if( aToolName == "sch_read_section" )
        return ExecuteReadSection( aInput );
    // DISABLED: Use sch_add/sch_update/sch_delete instead
    // else if( aToolName == "sch_modify" )
    //     return ExecuteModify( aInput );
    else if( aToolName == "sch_validate" )
        return ExecuteValidate( aInput );
    else if( aToolName == "sch_export_spice_netlist" )
        return ExecuteExportSpiceNetlist( aInput );

    return "Error: Unknown schematic tool: " + aToolName;
}


std::string SCH_TOOL_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string filePath = aInput.value( "file_path", "" );
    std::string fileName = filePath.empty() ? "schematic" : FileWriter::GetFilename( filePath );

    if( aToolName == "sch_get_summary" )
        return "Getting summary of " + fileName;
    else if( aToolName == "sch_file_summary" )
        return "Reading file summary of " + fileName;
    else if( aToolName == "sch_live_summary" )
        return "Getting live summary from editor";
    else if( aToolName == "sch_get_pins" )
    {
        std::string ref = aInput.value( "ref", "" );
        return "Getting pins for " + ( ref.empty() ? "symbol" : ref );
    }
    else if( aToolName == "sch_read_section" )
    {
        std::string section = aInput.value( "section", "all" );
        return "Reading " + section + " from " + fileName;
    }
    // DISABLED: Use sch_add/sch_update/sch_delete instead
    // else if( aToolName == "sch_modify" )
    // {
    //     std::string operation = aInput.value( "operation", "modify" );
    //     std::string elementType = aInput.value( "element_type", "element" );
    //     return operation + " " + elementType + " in " + fileName;
    // }
    else if( aToolName == "sch_validate" )
        return "Validating " + fileName;
    else if( aToolName == "sch_export_spice_netlist" )
        return "Exporting SPICE netlist from " + fileName;

    return "Executing " + aToolName;
}


std::string SCH_TOOL_HANDLER::ExecuteGetSummary( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    if( !FileWriter::FileExists( filePath ) )
        return "Error: File not found: " + filePath;

    SchParser::SchematicSummary summary = SchParser::GetSummary( filePath );
    return summary.ToJson().dump( 2 );
}


std::string SCH_TOOL_HANDLER::ExecuteReadSection( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    if( !FileWriter::FileExists( filePath ) )
        return "Error: File not found: " + filePath;

    std::string sectionName = aInput.value( "section", "all" );
    std::string filter = aInput.value( "filter", "" );

    SchParser::SectionType section = SchParser::SectionFromString( sectionName );
    return SchParser::ReadSection( filePath, section, filter );
}


std::string SCH_TOOL_HANDLER::ExecuteModify( const nlohmann::json& aInput )
{
    // Block file-based modifications when schematic editor is open
    // This prevents data conflicts between IPC (in-memory) and direct file operations
    if( IsSchematicEditorOpen() )
    {
        nlohmann::json errorJson = {
            { "success", false },
            { "error", "File modification blocked: Schematic editor is open" },
            { "message", "Direct file modifications are blocked while the schematic editor is open. "
                         "Use IPC-based tools (sch_add, sch_update, sch_delete) instead, or close "
                         "the editor first with close_editor." }
        };
        return errorJson.dump( 2 );
    }

    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    if( !FileWriter::FileExists( filePath ) )
        return "Error: File not found: " + filePath;

    std::string operation = aInput.value( "operation", "" );
    if( operation.empty() )
        return "Error: 'operation' parameter is required (add, update, or delete)";

    std::string elementType = aInput.value( "element_type", "" );
    if( elementType.empty() )
        return "Error: 'element_type' parameter is required (symbol, wire, junction, label, text)";

    // Read current file content
    std::string content;
    if( !FileWriter::ReadFile( filePath, content ) )
        return "Error: Failed to read file: " + filePath;

    std::string newContent;

    if( operation == "add" )
    {
        std::string data = aInput.value( "data", "" );
        if( data.empty() )
            return "Error: 'data' parameter is required for add operation";

        // Auto-generate UUID if missing, empty, or invalid
        std::set<std::string> existingUuids = UuidUtil::ExtractUuids( content );
        data = EnsureElementHasUuid( data, existingUuids );

        // Validate the element before adding
        auto validation = SchValidator::ValidateElement( elementType, data );
        if( !validation.valid )
        {
            nlohmann::json errorJson = {
                { "success", false },
                { "error", "Invalid element" },
                { "validation", validation.ToJson() }
            };
            return errorJson.dump( 2 );
        }

        newContent = SchParser::AddElement( content, elementType, data );
    }
    else if( operation == "update" )
    {
        std::string target = aInput.value( "target", "" );
        std::string data = aInput.value( "data", "" );

        if( target.empty() )
            return "Error: 'target' parameter is required for update operation (UUID or reference)";
        if( data.empty() )
            return "Error: 'data' parameter is required for update operation";

        // Validate the new element
        auto validation = SchValidator::ValidateElement( elementType, data );
        if( !validation.valid )
        {
            nlohmann::json errorJson = {
                { "success", false },
                { "error", "Invalid element" },
                { "validation", validation.ToJson() }
            };
            return errorJson.dump( 2 );
        }

        // If target looks like a UUID, use it directly
        if( UuidUtil::IsValidUuid( target ) )
        {
            newContent = SchParser::UpdateElement( content, target, data );
        }
        else
        {
            // Target is a reference - find the UUID first
            auto matches = SchParser::FindSymbolsByReference( content, target );
            if( matches.empty() )
                return "Error: No symbol found with reference: " + target;
            if( matches.size() > 1 )
                return "Error: Multiple symbols match reference pattern: " + target;

            // Extract UUID from the matched symbol
            auto parsed = SexprUtil::Parse( matches[0] );
            if( !parsed )
                return "Error: Failed to parse matched symbol";

            auto uuidExpr = SexprUtil::FindFirstChild( parsed.get(), "uuid" );
            if( !uuidExpr )
                return "Error: Matched symbol has no UUID";

            std::string uuid = SexprUtil::GetStringValue( uuidExpr );
            newContent = SchParser::UpdateElement( content, uuid, data );
        }

        if( newContent == content )
            return "Error: Target element not found for update";
    }
    else if( operation == "delete" )
    {
        std::string target = aInput.value( "target", "" );
        if( target.empty() )
            return "Error: 'target' parameter is required for delete operation (UUID or reference)";

        // If target looks like a UUID, use it directly
        if( UuidUtil::IsValidUuid( target ) )
        {
            newContent = SchParser::DeleteByUuid( content, target );
        }
        else
        {
            // Target is a reference - find the UUID first
            auto matches = SchParser::FindSymbolsByReference( content, target );
            if( matches.empty() )
                return "Error: No symbol found with reference: " + target;
            if( matches.size() > 1 )
                return "Error: Multiple symbols match reference pattern: " + target;

            // Extract UUID from the matched symbol
            auto parsed = SexprUtil::Parse( matches[0] );
            if( !parsed )
                return "Error: Failed to parse matched symbol";

            auto uuidExpr = SexprUtil::FindFirstChild( parsed.get(), "uuid" );
            if( !uuidExpr )
                return "Error: Matched symbol has no UUID";

            std::string uuid = SexprUtil::GetStringValue( uuidExpr );
            newContent = SchParser::DeleteByUuid( content, uuid );
        }

        if( newContent == content )
            return "Error: Target element not found for deletion";
    }
    else
    {
        return "Error: Unknown operation: " + operation + " (expected: add, update, delete)";
    }

    // Validate the modified content before writing
    auto validation = SchValidator::ValidateContent( newContent );
    if( !validation.valid )
    {
        nlohmann::json errorJson = {
            { "success", false },
            { "error", "Modified content failed validation" },
            { "validation", validation.ToJson() }
        };
        return errorJson.dump( 2 );
    }

    // Write the modified content
    auto writeResult = FileWriter::WriteFileSafe( filePath, newContent, true );
    if( !writeResult.success )
        return "Error: Failed to write file: " + writeResult.error;

    nlohmann::json result = {
        { "success", true },
        { "operation", operation },
        { "element_type", elementType },
        { "file", filePath }
    };

    if( !writeResult.backupPath.empty() )
        result["backup"] = writeResult.backupPath;

    if( !validation.warnings.empty() )
        result["warnings"] = validation.warnings;

    return result.dump( 2 );
}


std::string SCH_TOOL_HANDLER::ExecuteValidate( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    if( !FileWriter::FileExists( filePath ) )
        return "Error: File not found: " + filePath;

    auto result = SchValidator::ValidateFile( filePath );
    return result.ToJson().dump( 2 );
}


std::string SCH_TOOL_HANDLER::ExecuteExportSpiceNetlist( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    if( !FileWriter::FileExists( filePath ) )
        return "Error: File not found: " + filePath;

    std::string netlist = SchParser::GenerateSpiceNetlist( filePath );
    if( netlist.empty() )
        return "Error: Failed to generate SPICE netlist. Ensure the schematic is annotated "
               "and kicad-cli is available.";

    return netlist;
}


bool SCH_TOOL_HANDLER::RequiresIPC( const std::string& aToolName ) const
{
    // sch_get_summary: prefers IPC, falls back to file
    // sch_live_summary: requires IPC (no fallback)
    // sch_get_pins: requires IPC (no fallback)
    // sch_file_summary: never uses IPC (file only)
    return aToolName == "sch_get_summary" || aToolName == "sch_live_summary" || aToolName == "sch_get_pins";
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

    if( aToolName != "sch_get_summary" && aToolName != "sch_live_summary" )
        return "";

    std::string filePath = aInput.value( "file_path", "" );
    bool isLiveSummary = ( aToolName == "sch_live_summary" );

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

    if( isLiveSummary )
    {
        // sch_live_summary: fail hard if IPC fails (no file fallback)
        code << "    print(json.dumps({'status': 'error', 'message': f'IPC failed: {e}. Schematic editor must be open.'}))\n";
    }
    else
    {
        // sch_get_summary: signal for file-based fallback
        code << "    # IPC failed - signal for file-based fallback\n"
             << "    print(f'IPC_FALLBACK_REQUIRED: {e}', file=sys.stderr)\n"
             << "    print(json.dumps({'status': 'ipc_fallback', 'error': str(e)}))\n";
    }

    return "run_shell sch " + code.str();
}
