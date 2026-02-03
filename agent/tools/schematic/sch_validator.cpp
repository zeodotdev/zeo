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

#include "sch_validator.h"
#include "../kicad_file/sexpr_util.h"
#include "../kicad_file/file_writer.h"
#include "../kicad_file/uuid_util.h"
#include <set>
#include <regex>

namespace SchValidator
{

namespace
{

/**
 * Check for balanced parentheses in S-expression content.
 * The SEXPR parser is lenient and doesn't validate this, so we do it here.
 *
 * @param aContent The content to check.
 * @param aError Output error message if unbalanced.
 * @return true if parentheses are balanced.
 */
bool CheckBalancedParentheses( const std::string& aContent, std::string& aError )
{
    int parenDepth = 0;
    bool inString = false;
    int lineNumber = 1;

    for( size_t i = 0; i < aContent.size(); ++i )
    {
        char c = aContent[i];

        if( c == '\n' )
            lineNumber++;

        if( inString )
        {
            if( c == '\\' && i + 1 < aContent.size() )
            {
                ++i;  // Skip escaped character
                continue;
            }
            if( c == '"' )
                inString = false;
            continue;
        }

        if( c == '"' )
        {
            inString = true;
        }
        else if( c == '(' )
        {
            parenDepth++;
        }
        else if( c == ')' )
        {
            parenDepth--;
            if( parenDepth < 0 )
            {
                aError = "Syntax error at line " + std::to_string( lineNumber ) +
                         ": unexpected closing parenthesis";
                return false;
            }
        }
    }

    if( inString )
    {
        aError = "Syntax error: unterminated string";
        return false;
    }

    if( parenDepth > 0 )
    {
        aError = "Syntax error: missing " + std::to_string( parenDepth ) +
                 " closing parenthesis(es)";
        return false;
    }

    return true;
}


/**
 * Extract the base name from a lib_id.
 * For example, "Device:C" returns "C", "Simulation_SPICE:DIODE" returns "DIODE".
 * @param aLibId The full library ID (e.g., "Device:C")
 * @return The base symbol name after the colon, or the full string if no colon.
 */
std::string ExtractBaseName( const std::string& aLibId )
{
    size_t colonPos = aLibId.find( ':' );
    if( colonPos != std::string::npos && colonPos + 1 < aLibId.length() )
        return aLibId.substr( colonPos + 1 );
    return aLibId;
}


/**
 * Validate a sub-symbol name against the expected naming convention.
 * Sub-symbols must follow the format: <BaseName>_<unit>_<bodyStyle>
 * where unit is a non-negative integer and bodyStyle is 1 or 2.
 *
 * @param aParentName The parent symbol name (e.g., "Device:C")
 * @param aSubName The sub-symbol name to validate (e.g., "C_0_1")
 * @param aErrors Vector to append error messages to.
 * @return true if the sub-symbol name is valid.
 */
bool ValidateSubSymbolName( const std::string& aParentName, const std::string& aSubName,
                            std::vector<std::string>& aErrors )
{
    std::string baseName = ExtractBaseName( aParentName );

    // Pattern: <BaseName>_<unit>_<bodyStyle>
    // Unit: non-negative integer (0 for shared graphics, 1+ for unit-specific)
    // Body style: 1 (normal) or 2 (DeMorgan/alternate)
    std::regex pattern( "^(.+)_(\\d+)_(\\d+)$" );
    std::smatch match;

    if( !std::regex_match( aSubName, match, pattern ) )
    {
        aErrors.push_back( "Invalid sub-symbol name format '" + aSubName +
                           "': expected '<BaseName>_<unit>_<style>'" );
        return false;
    }

    std::string subBaseName = match[1].str();
    std::string unitStr = match[2].str();
    std::string styleStr = match[3].str();

    // Check that the base name matches
    if( subBaseName != baseName )
    {
        aErrors.push_back( "Sub-symbol name prefix '" + subBaseName +
                           "' doesn't match parent symbol base name '" + baseName + "'" );
        return false;
    }

    // Validate unit number (must be non-negative integer - already matched by regex)
    // Unit 0 is for shared graphics, 1+ for unit-specific content

    // Validate body style (must be 1 or 2)
    int bodyStyle = std::stoi( styleStr );
    if( bodyStyle != 1 && bodyStyle != 2 )
    {
        aErrors.push_back( "Invalid body style '" + styleStr +
                           "' in sub-symbol name '" + aSubName + "' (expected 1 or 2)" );
        return false;
    }

    return true;
}


/**
 * Validate a single library symbol definition.
 * Checks sub-symbol naming conventions.
 *
 * @param aLibSym The library symbol S-expression.
 * @param aResult ValidationResult to append errors/warnings to.
 */
void ValidateLibSymbol( const SEXPR::SEXPR* aLibSym, ValidationResult& aResult )
{
    // Get the symbol name (second element in the list)
    auto children = aLibSym->GetChildren();
    if( !children || children->size() < 2 )
        return;

    const SEXPR::SEXPR* nameExpr = children->at( 1 );
    if( !nameExpr->IsString() )
        return;

    std::string parentName = nameExpr->GetString();

    // Find all nested symbol children (sub-symbols like "C_0_1")
    auto subSymbols = SexprUtil::FindChildren( aLibSym, "symbol" );

    for( const auto& subSym : subSymbols )
    {
        auto subChildren = subSym->GetChildren();
        if( !subChildren || subChildren->size() < 2 )
            continue;

        const SEXPR::SEXPR* subNameExpr = subChildren->at( 1 );
        if( !subNameExpr->IsString() )
            continue;

        std::string subName = subNameExpr->GetString();
        std::vector<std::string> errors;

        if( !ValidateSubSymbolName( parentName, subName, errors ) )
        {
            for( const auto& error : errors )
            {
                aResult.AddError( "In lib_symbols '" + parentName + "': " + error );
            }
        }
    }
}


/**
 * Validate the lib_symbols section of a schematic.
 * Checks all library symbol definitions for proper structure and naming.
 *
 * @param aLibSymbols The lib_symbols S-expression.
 * @param aResult ValidationResult to append errors/warnings to.
 */
void ValidateLibSymbols( const SEXPR::SEXPR* aLibSymbols, ValidationResult& aResult )
{
    auto libSymDefs = SexprUtil::FindChildren( aLibSymbols, "symbol" );

    for( const auto& libSym : libSymDefs )
    {
        ValidateLibSymbol( libSym, aResult );
    }
}

} // anonymous namespace


nlohmann::json ValidationResult::ToJson() const
{
    return {
        { "valid", valid },
        { "syntax_ok", syntaxOk },
        { "structure_ok", structureOk },
        { "warnings", warnings },
        { "errors", errors }
    };
}


ValidationResult ValidateFile( const std::string& aFilePath )
{
    std::string content;
    if( !FileWriter::ReadFile( aFilePath, content ) )
    {
        ValidationResult result;
        result.AddError( "Could not read file: " + aFilePath );
        return result;
    }

    return ValidateContent( content );
}


ValidationResult ValidateContent( const std::string& aContent )
{
    ValidationResult result;

    // Level 1: Syntax validation
    // First check balanced parentheses (SEXPR parser doesn't validate this)
    std::string parenError;
    if( !CheckBalancedParentheses( aContent, parenError ) )
    {
        result.syntaxOk = false;
        result.AddError( parenError );
        return result;
    }

    // Then use the SEXPR parser for tokenization validation
    auto syntaxResult = SexprUtil::ValidateSyntax( aContent );
    if( !syntaxResult.valid )
    {
        result.syntaxOk = false;
        result.AddError( "Syntax error at line " + std::to_string( syntaxResult.errorLine ) +
                         ": " + syntaxResult.error );
        return result;  // Can't proceed with structural validation if syntax is invalid
    }

    // Parse the content
    auto root = SexprUtil::Parse( aContent );
    if( !root || !root->IsList() )
    {
        result.AddError( "Failed to parse content as S-expression" );
        return result;
    }

    // Verify it's a kicad_sch file
    if( SexprUtil::GetListType( root.get() ) != "kicad_sch" )
    {
        result.AddError( "Not a KiCad schematic file (expected 'kicad_sch', got '" +
                         SexprUtil::GetListType( root.get() ) + "')" );
        return result;
    }

    // Level 2: Structural validation

    // Check for required fields
    auto versionExpr = SexprUtil::FindFirstChild( root.get(), "version" );
    if( !versionExpr )
    {
        result.structureOk = false;
        result.AddError( "Missing required 'version' field" );
    }

    auto uuidExpr = SexprUtil::FindFirstChild( root.get(), "uuid" );
    if( !uuidExpr )
    {
        result.structureOk = false;
        result.AddError( "Missing required 'uuid' field" );
    }

    // Check UUID uniqueness
    std::set<std::string> uuids = UuidUtil::ExtractUuids( aContent );
    std::set<std::string> seenUuids;
    for( const auto& uuid : uuids )
    {
        if( !UuidUtil::IsValidUuid( uuid ) )
        {
            result.AddWarning( "Invalid UUID format: " + uuid );
        }
        if( seenUuids.find( uuid ) != seenUuids.end() )
        {
            result.structureOk = false;
            result.AddError( "Duplicate UUID: " + uuid );
        }
        seenUuids.insert( uuid );
    }

    // Validate lib_symbols section (embedded library symbols)
    auto libSymbols = SexprUtil::FindFirstChild( root.get(), "lib_symbols" );
    if( libSymbols )
    {
        ValidateLibSymbols( libSymbols, result );
    }

    // Validate placed symbols (instances on the schematic)
    auto symbols = SexprUtil::FindChildren( root.get(), "symbol" );
    std::set<std::string> references;

    for( const auto& sym : symbols )
    {
        // Check for lib_id
        auto libId = SexprUtil::FindFirstChild( sym, "lib_id" );
        if( !libId )
        {
            result.AddWarning( "Symbol missing lib_id" );
        }

        // Check for uuid
        auto symUuid = SexprUtil::FindFirstChild( sym, "uuid" );
        if( !symUuid )
        {
            result.structureOk = false;
            result.AddError( "Symbol missing uuid" );
        }

        // Check for position
        auto at = SexprUtil::FindFirstChild( sym, "at" );
        if( !at )
        {
            result.structureOk = false;
            result.AddError( "Symbol missing 'at' position" );
        }

        // Check for Reference property and detect duplicates
        auto props = SexprUtil::FindChildren( sym, "property" );
        bool hasReference = false;
        std::string refValue;

        for( const auto& prop : props )
        {
            auto children = prop->GetChildren();
            if( children && children->size() >= 3 )
            {
                const SEXPR::SEXPR* nameExpr = children->at( 1 );
                if( nameExpr->IsString() && nameExpr->GetString() == "Reference" )
                {
                    hasReference = true;
                    const SEXPR::SEXPR* valExpr = children->at( 2 );
                    if( valExpr->IsString() )
                    {
                        refValue = valExpr->GetString();
                        // Skip power symbols (start with #)
                        if( !refValue.empty() && refValue[0] != '#' )
                        {
                            if( references.find( refValue ) != references.end() )
                            {
                                result.AddWarning( "Duplicate reference designator: " + refValue );
                            }
                            references.insert( refValue );
                        }
                    }
                }
                else if( nameExpr->IsString() && nameExpr->GetString() == "Footprint" )
                {
                    const SEXPR::SEXPR* valExpr = children->at( 2 );
                    if( valExpr->IsString() && valExpr->GetString().empty() )
                    {
                        // Don't warn for power symbols
                        if( refValue.empty() || refValue[0] != '#' )
                        {
                            // result.AddWarning( "Symbol " + refValue + " has no footprint assigned" );
                        }
                    }
                }
            }
        }

        if( !hasReference )
        {
            result.AddWarning( "Symbol missing Reference property" );
        }
    }

    // Validate wires
    auto wires = SexprUtil::FindChildren( root.get(), "wire" );
    for( const auto& wire : wires )
    {
        auto pts = SexprUtil::FindFirstChild( wire, "pts" );
        if( !pts )
        {
            result.structureOk = false;
            result.AddError( "Wire missing 'pts' (points)" );
        }
    }

    // Validate junctions
    auto junctions = SexprUtil::FindChildren( root.get(), "junction" );
    for( const auto& junc : junctions )
    {
        auto at = SexprUtil::FindFirstChild( junc, "at" );
        if( !at )
        {
            result.structureOk = false;
            result.AddError( "Junction missing 'at' position" );
        }
    }

    // Validate labels
    auto labels = SexprUtil::FindChildren( root.get(), "label" );
    for( const auto& lbl : labels )
    {
        auto children = lbl->GetChildren();
        if( !children || children->size() < 2 )
        {
            result.structureOk = false;
            result.AddError( "Label missing name" );
        }

        auto at = SexprUtil::FindFirstChild( lbl, "at" );
        if( !at )
        {
            result.structureOk = false;
            result.AddError( "Label missing 'at' position" );
        }
    }

    return result;
}


ValidationResult ValidateElement( const std::string& aElementType, const std::string& aElement )
{
    if( aElementType == "symbol" )
        return ValidateSymbol( aElement );
    else if( aElementType == "wire" )
        return ValidateWire( aElement );
    else if( aElementType == "junction" )
        return ValidateJunction( aElement );
    else if( aElementType == "label" )
        return ValidateLabel( aElement );
    else if( aElementType == "text" )
    {
        // Basic validation for text
        auto syntaxResult = SexprUtil::ValidateSyntax( aElement );
        ValidationResult result;
        if( !syntaxResult.valid )
        {
            result.syntaxOk = false;
            result.AddError( "Invalid S-expression syntax: " + syntaxResult.error );
        }
        return result;
    }

    ValidationResult result;
    result.AddError( "Unknown element type: " + aElementType );
    return result;
}


ValidationResult ValidateSymbol( const std::string& aContent )
{
    ValidationResult result;

    auto syntaxResult = SexprUtil::ValidateSyntax( aContent );
    if( !syntaxResult.valid )
    {
        result.syntaxOk = false;
        result.AddError( "Invalid S-expression syntax: " + syntaxResult.error );
        return result;
    }

    auto parsed = SexprUtil::Parse( aContent );
    if( !parsed || !parsed->IsList() )
    {
        result.AddError( "Failed to parse symbol content" );
        return result;
    }

    if( SexprUtil::GetListType( parsed.get() ) != "symbol" )
    {
        result.AddError( "Not a symbol element (expected 'symbol', got '" +
                         SexprUtil::GetListType( parsed.get() ) + "')" );
        return result;
    }

    // Check required fields
    if( !SexprUtil::FindFirstChild( parsed.get(), "lib_id" ) )
    {
        result.structureOk = false;
        result.AddError( "Symbol missing 'lib_id'" );
    }

    if( !SexprUtil::FindFirstChild( parsed.get(), "at" ) )
    {
        result.structureOk = false;
        result.AddError( "Symbol missing 'at' position" );
    }

    if( !SexprUtil::FindFirstChild( parsed.get(), "uuid" ) )
    {
        result.structureOk = false;
        result.AddError( "Symbol missing 'uuid'" );
    }

    return result;
}


ValidationResult ValidateWire( const std::string& aContent )
{
    ValidationResult result;

    auto syntaxResult = SexprUtil::ValidateSyntax( aContent );
    if( !syntaxResult.valid )
    {
        result.syntaxOk = false;
        result.AddError( "Invalid S-expression syntax: " + syntaxResult.error );
        return result;
    }

    auto parsed = SexprUtil::Parse( aContent );
    if( !parsed || !parsed->IsList() )
    {
        result.AddError( "Failed to parse wire content" );
        return result;
    }

    if( SexprUtil::GetListType( parsed.get() ) != "wire" )
    {
        result.AddError( "Not a wire element" );
        return result;
    }

    if( !SexprUtil::FindFirstChild( parsed.get(), "pts" ) )
    {
        result.structureOk = false;
        result.AddError( "Wire missing 'pts' (points)" );
    }

    return result;
}


ValidationResult ValidateJunction( const std::string& aContent )
{
    ValidationResult result;

    auto syntaxResult = SexprUtil::ValidateSyntax( aContent );
    if( !syntaxResult.valid )
    {
        result.syntaxOk = false;
        result.AddError( "Invalid S-expression syntax: " + syntaxResult.error );
        return result;
    }

    auto parsed = SexprUtil::Parse( aContent );
    if( !parsed || !parsed->IsList() )
    {
        result.AddError( "Failed to parse junction content" );
        return result;
    }

    if( SexprUtil::GetListType( parsed.get() ) != "junction" )
    {
        result.AddError( "Not a junction element" );
        return result;
    }

    if( !SexprUtil::FindFirstChild( parsed.get(), "at" ) )
    {
        result.structureOk = false;
        result.AddError( "Junction missing 'at' position" );
    }

    return result;
}


ValidationResult ValidateLabel( const std::string& aContent )
{
    ValidationResult result;

    auto syntaxResult = SexprUtil::ValidateSyntax( aContent );
    if( !syntaxResult.valid )
    {
        result.syntaxOk = false;
        result.AddError( "Invalid S-expression syntax: " + syntaxResult.error );
        return result;
    }

    auto parsed = SexprUtil::Parse( aContent );
    if( !parsed || !parsed->IsList() )
    {
        result.AddError( "Failed to parse label content" );
        return result;
    }

    if( SexprUtil::GetListType( parsed.get() ) != "label" )
    {
        result.AddError( "Not a label element" );
        return result;
    }

    auto children = parsed->GetChildren();
    if( !children || children->size() < 2 )
    {
        result.structureOk = false;
        result.AddError( "Label missing name" );
    }

    if( !SexprUtil::FindFirstChild( parsed.get(), "at" ) )
    {
        result.structureOk = false;
        result.AddError( "Label missing 'at' position" );
    }

    return result;
}

} // namespace SchValidator
