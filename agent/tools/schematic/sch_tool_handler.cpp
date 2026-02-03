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

// Static list of tool names this handler supports
static const char* SCH_TOOL_NAMES[] = {
    "sch_get_summary",
    "sch_read_section",
    "sch_modify",
    "sch_validate",
    "sch_write"
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
        return ExecuteGetSummary( aInput );
    else if( aToolName == "sch_read_section" )
        return ExecuteReadSection( aInput );
    else if( aToolName == "sch_modify" )
        return ExecuteModify( aInput );
    else if( aToolName == "sch_validate" )
        return ExecuteValidate( aInput );
    else if( aToolName == "sch_write" )
        return ExecuteWrite( aInput );

    return "Error: Unknown schematic tool: " + aToolName;
}


std::string SCH_TOOL_HANDLER::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string filePath = aInput.value( "file_path", "" );
    std::string fileName = filePath.empty() ? "schematic" : FileWriter::GetFilename( filePath );

    if( aToolName == "sch_get_summary" )
        return "Getting summary of " + fileName;
    else if( aToolName == "sch_read_section" )
    {
        std::string section = aInput.value( "section", "all" );
        return "Reading " + section + " from " + fileName;
    }
    else if( aToolName == "sch_modify" )
    {
        std::string operation = aInput.value( "operation", "modify" );
        std::string elementType = aInput.value( "element_type", "element" );
        return operation + " " + elementType + " in " + fileName;
    }
    else if( aToolName == "sch_validate" )
        return "Validating " + fileName;
    else if( aToolName == "sch_write" )
        return "Writing " + fileName;

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


std::string SCH_TOOL_HANDLER::ExecuteWrite( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    if( filePath.empty() )
        return "Error: 'file_path' parameter is required";

    std::string content = aInput.value( "content", "" );
    if( content.empty() )
        return "Error: 'content' parameter is required";

    bool createBackup = aInput.value( "backup", true );

    // Validate the content before writing
    auto validation = SchValidator::ValidateContent( content );
    if( !validation.valid )
    {
        nlohmann::json errorJson = {
            { "success", false },
            { "error", "Content validation failed" },
            { "validation", validation.ToJson() }
        };
        return errorJson.dump( 2 );
    }

    // Write the file
    auto writeResult = FileWriter::WriteFileSafe( filePath, content, createBackup );
    if( !writeResult.success )
        return "Error: Failed to write file: " + writeResult.error;

    nlohmann::json result = {
        { "success", true },
        { "file", filePath }
    };

    if( !writeResult.backupPath.empty() )
        result["backup"] = writeResult.backupPath;

    if( !validation.warnings.empty() )
        result["warnings"] = validation.warnings;

    return result.dump( 2 );
}
