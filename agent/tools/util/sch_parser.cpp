#include "sch_parser.h"
#include "sexpr_util.h"
#include "file_writer.h"
#include "kicad_cli_util.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <sys/wait.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/log.h>

namespace SchParser
{

// Helper to match wildcard patterns like "R*" or "C?"
static bool MatchesPattern( const std::string& aValue, const std::string& aPattern )
{
    if( aPattern.empty() )
        return true;

    // Convert simple wildcards to regex
    std::string regexStr;
    for( char c : aPattern )
    {
        switch( c )
        {
        case '*': regexStr += ".*"; break;
        case '?': regexStr += "."; break;
        case '.': case '^': case '$': case '+': case '(': case ')':
        case '[': case ']': case '{': case '}': case '|': case '\\':
            regexStr += "\\";
            regexStr += c;
            break;
        default:
            regexStr += c;
        }
    }

    try
    {
        std::regex pattern( "^" + regexStr + "$", std::regex::icase );
        return std::regex_match( aValue, pattern );
    }
    catch( ... )
    {
        return aValue == aPattern;  // Fallback to exact match on regex error
    }
}


// Helper to extract reference designator from a symbol S-expression
static std::string ExtractReference( const SEXPR::SEXPR* aSymbol )
{
    auto props = SexprUtil::FindChildren( aSymbol, "property" );
    for( const auto& prop : props )
    {
        auto children = prop->GetChildren();
        if( children && children->size() >= 3 )
        {
            const SEXPR::SEXPR* nameExpr = children->at( 1 );
            if( nameExpr->IsString() && nameExpr->GetString() == "Reference" )
            {
                const SEXPR::SEXPR* valueExpr = children->at( 2 );
                if( valueExpr->IsString() )
                    return valueExpr->GetString();
            }
        }
    }
    return "";
}


// Helper to extract value from a symbol S-expression
static std::string ExtractValue( const SEXPR::SEXPR* aSymbol )
{
    auto props = SexprUtil::FindChildren( aSymbol, "property" );
    for( const auto& prop : props )
    {
        auto children = prop->GetChildren();
        if( children && children->size() >= 3 )
        {
            const SEXPR::SEXPR* nameExpr = children->at( 1 );
            if( nameExpr->IsString() && nameExpr->GetString() == "Value" )
            {
                const SEXPR::SEXPR* valueExpr = children->at( 2 );
                if( valueExpr->IsString() )
                    return valueExpr->GetString();
            }
        }
    }
    return "";
}


nlohmann::json PinInfo::ToJson() const
{
    return {
        { "number", number },
        { "name", name },
        { "pos", { x, y } }
    };
}


nlohmann::json SymbolInfo::ToJson() const
{
    nlohmann::json pinsJson = nlohmann::json::array();
    for( const auto& pin : pins )
        pinsJson.push_back( pin.ToJson() );

    return {
        { "uuid", uuid },
        { "ref", reference },
        { "value", value },
        { "lib_id", libId },
        { "pos", { x, y } },
        { "angle", angle },
        { "unit", unit },
        { "pins", pinsJson }
    };
}


nlohmann::json WireInfo::ToJson() const
{
    return {
        { "uuid", uuid },
        { "start", { startX, startY } },
        { "end", { endX, endY } }
    };
}


nlohmann::json JunctionInfo::ToJson() const
{
    return {
        { "uuid", uuid },
        { "pos", { x, y } }
    };
}


nlohmann::json LabelInfo::ToJson() const
{
    return {
        { "uuid", uuid },
        { "text", text },
        { "type", type },
        { "pos", { x, y } }
    };
}


nlohmann::json NoConnectInfo::ToJson() const
{
    return {
        { "uuid", uuid },
        { "pos", { x, y } }
    };
}


nlohmann::json SheetInfo::ToJson() const
{
    return {
        { "name", name },
        { "file", filename },
        { "uuid", uuid }
    };
}


nlohmann::json SummaryCounts::ToJson() const
{
    return {
        { "symbols", symbols },
        { "wires", wires },
        { "junctions", junctions },
        { "labels", labels },
        { "no_connects", noConnects },
        { "sheets", sheets }
    };
}


nlohmann::json SchematicSummary::ToJson() const
{
    nlohmann::json symbolsJson = nlohmann::json::array();
    for( const auto& sym : symbols )
        symbolsJson.push_back( sym.ToJson() );

    nlohmann::json wiresJson = nlohmann::json::array();
    for( const auto& wire : wires )
        wiresJson.push_back( wire.ToJson() );

    nlohmann::json junctionsJson = nlohmann::json::array();
    for( const auto& junc : junctions )
        junctionsJson.push_back( junc.ToJson() );

    nlohmann::json labelsJson = nlohmann::json::array();
    for( const auto& lbl : labels )
        labelsJson.push_back( lbl.ToJson() );

    nlohmann::json noConnectsJson = nlohmann::json::array();
    for( const auto& nc : noConnects )
        noConnectsJson.push_back( nc.ToJson() );

    nlohmann::json sheetsJson = nlohmann::json::array();
    for( const auto& sheet : sheets )
        sheetsJson.push_back( sheet.ToJson() );

    return {
        { "source", source },
        { "file", file },
        { "version", version },
        { "uuid", uuid },
        { "paper", paper },
        { "title", title },
        { "symbols", symbolsJson },
        { "wires", wiresJson },
        { "junctions", junctionsJson },
        { "labels", labelsJson },
        { "no_connects", noConnectsJson },
        { "sheets", sheetsJson },
        { "spice_directives", spice_directives },
        { "counts", counts.ToJson() }
    };
}


/**
 * Extract pins from a lib_symbol definition for a specific unit.
 * Returns vector of (number, name, rel_x, rel_y, pin_angle).
 */
static std::vector<std::tuple<std::string, std::string, double, double, double>>
ExtractLibSymbolPins( const SEXPR::SEXPR* aLibSymbol, int aUnit )
{
    std::vector<std::tuple<std::string, std::string, double, double, double>> pins;

    auto children = aLibSymbol->GetChildren();
    if( !children )
        return pins;

    // Find symbol_N_M blocks (unit N, convert M)
    // Unit 0 applies to all units; otherwise match specific unit
    for( const auto& child : *children )
    {
        if( !child->IsList() || SexprUtil::GetListType( child ) != "symbol" )
            continue;

        // Parse symbol name to get unit number: "LibName_N_M"
        auto symbolChildren = child->GetChildren();
        if( !symbolChildren || symbolChildren->size() < 2 )
            continue;

        const SEXPR::SEXPR* nameExpr = symbolChildren->at( 1 );
        if( !nameExpr->IsString() )
            continue;

        std::string symbolName = nameExpr->GetString();
        // Extract unit from name pattern "LibName_N_M"
        size_t lastUnderscore = symbolName.rfind( '_' );
        size_t secondLastUnderscore = symbolName.rfind( '_', lastUnderscore - 1 );
        if( lastUnderscore == std::string::npos || secondLastUnderscore == std::string::npos )
            continue;

        int unitNum = std::stoi( symbolName.substr( secondLastUnderscore + 1,
                                                     lastUnderscore - secondLastUnderscore - 1 ) );

        // Unit 0 applies to all, otherwise must match
        if( unitNum != 0 && unitNum != aUnit )
            continue;

        // Find pins in this symbol block
        auto pinExprs = SexprUtil::FindChildren( child, "pin" );
        for( const auto& pinExpr : pinExprs )
        {
            // Extract pin position: (at x y angle)
            auto atExpr = SexprUtil::FindFirstChild( pinExpr, "at" );
            double px = 0, py = 0, pangle = 0;
            if( atExpr )
                SexprUtil::GetCoordinates( atExpr, px, py, pangle );

            // Extract pin name
            auto nameChild = SexprUtil::FindFirstChild( pinExpr, "name" );
            std::string pinName;
            if( nameChild )
                pinName = SexprUtil::GetStringValue( nameChild );

            // Extract pin number
            auto numberChild = SexprUtil::FindFirstChild( pinExpr, "number" );
            std::string pinNumber;
            if( numberChild )
                pinNumber = SexprUtil::GetStringValue( numberChild );

            pins.emplace_back( pinNumber, pinName, px, py, pangle );
        }
    }

    return pins;
}


/**
 * Transform relative pin position to absolute position.
 * Applies symbol rotation and mirroring.
 */
static void TransformPinPosition( double symX, double symY, double symAngle,
                                   bool mirrorX, bool mirrorY,
                                   double pinRelX, double pinRelY,
                                   double& pinAbsX, double& pinAbsY )
{
    // Apply mirroring first (in symbol's local space)
    double x = mirrorY ? -pinRelX : pinRelX;
    double y = mirrorX ? -pinRelY : pinRelY;

    // Convert angle to radians
    double rad = symAngle * M_PI / 180.0;
    double cosA = std::cos( rad );
    double sinA = std::sin( rad );

    // Rotate and translate
    pinAbsX = symX + x * cosA - y * sinA;
    pinAbsY = symY + x * sinA + y * cosA;
}


std::string GenerateSpiceNetlist( const std::string& aSchematicPath )
{
    if( aSchematicPath.empty() )
    {
        wxLogWarning( "SPICE: Empty schematic path" );
        return std::string();
    }

    std::string cmdPrefix = KiCadCliUtil::GetKicadCliCommandPrefix();
    if( cmdPrefix.empty() )
        return std::string();

    std::string cmd = cmdPrefix
                      + " sch export netlist --format spice -o /dev/stdout \""
                      + aSchematicPath + "\" 2>/dev/null";

    wxLogInfo( "SPICE: Running command: %s", cmd.c_str() );

    FILE* pipe = popen( cmd.c_str(), "r" );
    if( !pipe )
    {
        wxLogWarning( "SPICE: popen() failed" );
        return std::string();
    }

    std::string result;
    char        buffer[4096];

    while( fgets( buffer, sizeof( buffer ), pipe ) )
        result += buffer;

    int status = pclose( pipe );
    if( status != 0 )
    {
        int exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
        int signal   = WIFSIGNALED( status ) ? WTERMSIG( status ) : 0;
        wxLogWarning( "SPICE: kicad-cli failed (exit=%d, signal=%d)",
                      exitCode, signal );
        return std::string();
    }

    // kicad-cli may print warnings to stdout before the actual netlist content.
    // Strip any lines before the first valid SPICE line (starting with . or *)
    size_t validStart = 0;
    size_t pos = 0;

    while( pos < result.size() )
    {
        size_t lineEnd = result.find( '\n', pos );
        if( lineEnd == std::string::npos )
            lineEnd = result.size();

        if( pos < lineEnd )
        {
            char firstChar = result[pos];
            if( firstChar == '.' || firstChar == '*' )
            {
                validStart = pos;
                break;
            }
        }

        pos = lineEnd + 1;
    }

    if( validStart > 0 )
    {
        wxLogInfo( "SPICE: Stripped %zu bytes of warnings from output", validStart );
        result = result.substr( validStart );
    }

    wxLogInfo( "SPICE: Generated netlist (%zu bytes)", result.size() );
    return result;
}


SchematicSummary GetSummary( const std::string& aFilePath )
{
    SchematicSummary summary;
    summary.source = "file";
    summary.file = FileWriter::GetFilename( aFilePath );
    summary.version = 0;
    summary.counts = { 0, 0, 0, 0, 0, 0 };

    auto root = SexprUtil::ParseFile( aFilePath );
    if( !root || !root->IsList() )
        return summary;

    // Verify it's a kicad_sch file
    if( SexprUtil::GetListType( root.get() ) != "kicad_sch" )
        return summary;

    // Extract version
    auto versionExpr = SexprUtil::FindFirstChild( root.get(), "version" );
    if( versionExpr )
        summary.version = SexprUtil::GetIntValue( versionExpr );

    // Extract UUID
    auto uuidExpr = SexprUtil::FindFirstChild( root.get(), "uuid" );
    if( uuidExpr )
        summary.uuid = SexprUtil::GetStringValue( uuidExpr );

    // Extract paper size
    auto paperExpr = SexprUtil::FindFirstChild( root.get(), "paper" );
    if( paperExpr )
        summary.paper = SexprUtil::GetStringValue( paperExpr );

    // Extract title from title_block
    auto titleBlock = SexprUtil::FindFirstChild( root.get(), "title_block" );
    if( titleBlock )
    {
        auto titleExpr = SexprUtil::FindFirstChild( titleBlock, "title" );
        if( titleExpr )
            summary.title = SexprUtil::GetStringValue( titleExpr );
    }

    // Extract symbols
    auto symbols = SexprUtil::FindChildren( root.get(), "symbol" );
    for( const auto& sym : symbols )
    {
        SymbolInfo info;

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( sym, "uuid" );
        if( uuidChild )
            info.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get lib_id
        auto libIdChild = SexprUtil::FindFirstChild( sym, "lib_id" );
        if( libIdChild )
            info.libId = SexprUtil::GetStringValue( libIdChild );

        // Get position
        auto atChild = SexprUtil::FindFirstChild( sym, "at" );
        if( atChild )
            SexprUtil::GetCoordinates( atChild, info.x, info.y, info.angle );

        // Get unit
        auto unitChild = SexprUtil::FindFirstChild( sym, "unit" );
        if( unitChild )
            info.unit = SexprUtil::GetIntValue( unitChild );

        // Get reference and value from properties
        info.reference = ExtractReference( sym );
        info.value = ExtractValue( sym );

        // Extract mirror flags
        info.mirrorX = false;
        info.mirrorY = false;
        auto mirrorExpr = SexprUtil::FindFirstChild( sym, "mirror" );
        if( mirrorExpr )
        {
            std::string mirrorVal = SexprUtil::GetStringValue( mirrorExpr );
            info.mirrorX = ( mirrorVal == "x" || mirrorVal == "xy" );
            info.mirrorY = ( mirrorVal == "y" || mirrorVal == "xy" );
        }

        summary.symbols.push_back( info );
    }

    // Look up lib_symbols to extract pins for each symbol
    auto libSymbols = SexprUtil::FindFirstChild( root.get(), "lib_symbols" );
    if( libSymbols )
    {
        auto libSymChildren = SexprUtil::FindChildren( libSymbols, "symbol" );

        for( auto& symInfo : summary.symbols )
        {
            // Find matching lib_symbol by lib_id
            for( const auto& libSym : libSymChildren )
            {
                auto libSymChildren2 = libSym->GetChildren();
                if( libSymChildren2 && libSymChildren2->size() >= 2 )
                {
                    const SEXPR::SEXPR* libNameExpr = libSymChildren2->at( 1 );
                    if( libNameExpr->IsString() && libNameExpr->GetString() == symInfo.libId )
                    {
                        // Extract pins for this unit
                        auto pinDefs = ExtractLibSymbolPins( libSym, symInfo.unit );
                        for( const auto& pinDef : pinDefs )
                        {
                            PinInfo pin;
                            pin.number = std::get<0>( pinDef );
                            pin.name = std::get<1>( pinDef );
                            double relX = std::get<2>( pinDef );
                            double relY = std::get<3>( pinDef );
                            TransformPinPosition( symInfo.x, symInfo.y, symInfo.angle,
                                                  symInfo.mirrorX, symInfo.mirrorY,
                                                  relX, relY, pin.x, pin.y );
                            symInfo.pins.push_back( pin );
                        }
                        break;
                    }
                }
            }
        }
    }

    // Extract wires with full details
    auto wireExprs = SexprUtil::FindChildren( root.get(), "wire" );
    for( const auto& wireExpr : wireExprs )
    {
        WireInfo wireInfo;

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( wireExpr, "uuid" );
        if( uuidChild )
            wireInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get points: (pts (xy x1 y1) (xy x2 y2))
        auto ptsChild = SexprUtil::FindFirstChild( wireExpr, "pts" );
        if( ptsChild )
        {
            auto xyChildren = SexprUtil::FindChildren( ptsChild, "xy" );
            if( xyChildren.size() >= 2 )
            {
                auto xy1 = xyChildren[0]->GetChildren();
                auto xy2 = xyChildren[1]->GetChildren();
                if( xy1 && xy1->size() >= 3 )
                {
                    wireInfo.startX = xy1->at( 1 )->IsDouble() ? xy1->at( 1 )->GetDouble() :
                                      xy1->at( 1 )->IsInteger() ? xy1->at( 1 )->GetInteger() : 0;
                    wireInfo.startY = xy1->at( 2 )->IsDouble() ? xy1->at( 2 )->GetDouble() :
                                      xy1->at( 2 )->IsInteger() ? xy1->at( 2 )->GetInteger() : 0;
                }
                if( xy2 && xy2->size() >= 3 )
                {
                    wireInfo.endX = xy2->at( 1 )->IsDouble() ? xy2->at( 1 )->GetDouble() :
                                    xy2->at( 1 )->IsInteger() ? xy2->at( 1 )->GetInteger() : 0;
                    wireInfo.endY = xy2->at( 2 )->IsDouble() ? xy2->at( 2 )->GetDouble() :
                                    xy2->at( 2 )->IsInteger() ? xy2->at( 2 )->GetInteger() : 0;
                }
            }
        }

        summary.wires.push_back( wireInfo );
    }

    // Extract junctions with full details
    auto junctionExprs = SexprUtil::FindChildren( root.get(), "junction" );
    for( const auto& juncExpr : junctionExprs )
    {
        JunctionInfo juncInfo;

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( juncExpr, "uuid" );
        if( uuidChild )
            juncInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get position
        auto atChild = SexprUtil::FindFirstChild( juncExpr, "at" );
        if( atChild )
        {
            double dummy;
            SexprUtil::GetCoordinates( atChild, juncInfo.x, juncInfo.y, dummy );
        }

        summary.junctions.push_back( juncInfo );
    }

    // Extract labels with full details (local labels)
    auto labelExprs = SexprUtil::FindChildren( root.get(), "label" );
    for( const auto& lblExpr : labelExprs )
    {
        LabelInfo lblInfo;
        lblInfo.type = "label";

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( lblExpr, "uuid" );
        if( uuidChild )
            lblInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get text (second element)
        auto children = lblExpr->GetChildren();
        if( children && children->size() >= 2 && children->at( 1 )->IsString() )
            lblInfo.text = children->at( 1 )->GetString();

        // Get position
        auto atChild = SexprUtil::FindFirstChild( lblExpr, "at" );
        if( atChild )
        {
            double dummy;
            SexprUtil::GetCoordinates( atChild, lblInfo.x, lblInfo.y, dummy );
        }

        summary.labels.push_back( lblInfo );
    }

    // Extract global labels
    auto globalLabelExprs = SexprUtil::FindChildren( root.get(), "global_label" );
    for( const auto& lblExpr : globalLabelExprs )
    {
        LabelInfo lblInfo;
        lblInfo.type = "global_label";

        auto uuidChild = SexprUtil::FindFirstChild( lblExpr, "uuid" );
        if( uuidChild )
            lblInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        auto children = lblExpr->GetChildren();
        if( children && children->size() >= 2 && children->at( 1 )->IsString() )
            lblInfo.text = children->at( 1 )->GetString();

        auto atChild = SexprUtil::FindFirstChild( lblExpr, "at" );
        if( atChild )
        {
            double dummy;
            SexprUtil::GetCoordinates( atChild, lblInfo.x, lblInfo.y, dummy );
        }

        summary.labels.push_back( lblInfo );
    }

    // Extract hierarchical labels
    auto hierLabelExprs = SexprUtil::FindChildren( root.get(), "hierarchical_label" );
    for( const auto& lblExpr : hierLabelExprs )
    {
        LabelInfo lblInfo;
        lblInfo.type = "hierarchical_label";

        auto uuidChild = SexprUtil::FindFirstChild( lblExpr, "uuid" );
        if( uuidChild )
            lblInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        auto children = lblExpr->GetChildren();
        if( children && children->size() >= 2 && children->at( 1 )->IsString() )
            lblInfo.text = children->at( 1 )->GetString();

        auto atChild = SexprUtil::FindFirstChild( lblExpr, "at" );
        if( atChild )
        {
            double dummy;
            SexprUtil::GetCoordinates( atChild, lblInfo.x, lblInfo.y, dummy );
        }

        summary.labels.push_back( lblInfo );
    }

    // Extract no_connects
    auto ncExprs = SexprUtil::FindChildren( root.get(), "no_connect" );
    for( const auto& ncExpr : ncExprs )
    {
        NoConnectInfo ncInfo;

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( ncExpr, "uuid" );
        if( uuidChild )
            ncInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get position
        auto atChild = SexprUtil::FindFirstChild( ncExpr, "at" );
        if( atChild )
        {
            double dummy;
            SexprUtil::GetCoordinates( atChild, ncInfo.x, ncInfo.y, dummy );
        }

        summary.noConnects.push_back( ncInfo );
    }

    // Extract SPICE directives from text items
    auto textItems = SexprUtil::FindChildren( root.get(), "text" );
    for( const auto& textExpr : textItems )
    {
        auto textChildren = textExpr->GetChildren();
        if( !textChildren || textChildren->size() < 2 )
            continue;

        const SEXPR::SEXPR* contentExpr = textChildren->at( 1 );
        if( !contentExpr->IsString() )
            continue;

        std::string content = contentExpr->GetString();

        // Check each line for SPICE directive prefixes
        std::istringstream stream( content );
        std::string line;
        bool hasDirective = false;

        while( std::getline( stream, line ) )
        {
            // Trim leading whitespace
            size_t start = line.find_first_not_of( " \t" );
            if( start == std::string::npos )
                continue;

            std::string trimmed = line.substr( start );

            // Convert to lowercase for matching
            std::string lower = trimmed;
            std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );

            // Match ngspice directive prefixes
            if( lower.substr( 0, 3 ) == ".ac" || lower.substr( 0, 5 ) == ".tran" ||
                lower.substr( 0, 3 ) == ".dc" || lower.substr( 0, 3 ) == ".op" ||
                lower.substr( 0, 6 ) == ".noise" || lower.substr( 0, 3 ) == ".pz" ||
                lower.substr( 0, 3 ) == ".sp" || lower.substr( 0, 5 ) == ".sens" ||
                lower.substr( 0, 3 ) == ".tf" || lower.substr( 0, 6 ) == ".disto" ||
                lower.substr( 0, 5 ) == ".meas" || lower.substr( 0, 4 ) == ".fft" )
            {
                hasDirective = true;
                break;
            }
        }

        if( hasDirective )
            summary.spice_directives.push_back( content );
    }

    // Extract sheet (child) references
    auto sheetExprs = SexprUtil::FindChildren( root.get(), "sheet" );
    for( const auto& sheetExpr : sheetExprs )
    {
        SheetInfo sheetInfo;

        // Get UUID
        auto uuidChild = SexprUtil::FindFirstChild( sheetExpr, "uuid" );
        if( uuidChild )
            sheetInfo.uuid = SexprUtil::GetStringValue( uuidChild );

        // Get Sheetname and Sheetfile from properties
        auto props = SexprUtil::FindChildren( sheetExpr, "property" );
        for( const auto& prop : props )
        {
            auto children = prop->GetChildren();
            if( children && children->size() >= 3 )
            {
                const SEXPR::SEXPR* nameExpr = children->at( 1 );
                const SEXPR::SEXPR* valueExpr = children->at( 2 );
                if( nameExpr->IsString() && valueExpr->IsString() )
                {
                    std::string propName = nameExpr->GetString();
                    if( propName == "Sheetname" )
                        sheetInfo.name = valueExpr->GetString();
                    else if( propName == "Sheetfile" )
                        sheetInfo.filename = valueExpr->GetString();
                }
            }
        }

        summary.sheets.push_back( sheetInfo );
    }

    // Update counts
    summary.counts.symbols = static_cast<int>( summary.symbols.size() );
    summary.counts.wires = static_cast<int>( summary.wires.size() );
    summary.counts.junctions = static_cast<int>( summary.junctions.size() );
    summary.counts.labels = static_cast<int>( summary.labels.size() );
    summary.counts.noConnects = static_cast<int>( summary.noConnects.size() );
    summary.counts.sheets = static_cast<int>( summary.sheets.size() );

    return summary;
}


SectionType SectionFromString( const std::string& aName )
{
    if( aName == "header" )       return SectionType::HEADER;
    if( aName == "lib_symbols" )  return SectionType::LIB_SYMBOLS;
    if( aName == "symbols" )      return SectionType::SYMBOLS;
    if( aName == "wires" )        return SectionType::WIRES;
    if( aName == "junctions" )    return SectionType::JUNCTIONS;
    if( aName == "labels" )       return SectionType::LABELS;
    if( aName == "text" )         return SectionType::TEXT;
    if( aName == "sheets" )       return SectionType::SHEETS;
    if( aName == "sheet_instances" ) return SectionType::SHEET_INSTANCES;
    return SectionType::ALL;
}


std::string ReadSection( const std::string& aFilePath, SectionType aSection,
                         const std::string& aFilter )
{
    std::string content;
    if( !FileWriter::ReadFile( aFilePath, content ) )
        return "Error: Could not read file: " + aFilePath;

    if( aSection == SectionType::ALL )
        return content;

    auto root = SexprUtil::Parse( content );
    if( !root || !root->IsList() )
        return "Error: Failed to parse schematic file";

    std::stringstream result;

    auto appendElements = [&]( const std::string& elementType )
    {
        auto elements = SexprUtil::FindChildren( root.get(), elementType );
        for( const auto& elem : elements )
        {
            // Apply filter if specified
            if( !aFilter.empty() )
            {
                // Check for UUID match
                auto uuidChild = SexprUtil::FindFirstChild( elem, "uuid" );
                if( uuidChild )
                {
                    std::string uuid = SexprUtil::GetStringValue( uuidChild );
                    if( uuid == aFilter )
                    {
                        result << SexprUtil::ToString( elem ) << "\n";
                        continue;
                    }
                }

                // Check for reference match (symbols only)
                if( elementType == "symbol" )
                {
                    std::string ref = ExtractReference( elem );
                    if( !MatchesPattern( ref, aFilter ) )
                        continue;
                }
            }
            result << SexprUtil::ToString( elem ) << "\n";
        }
    };

    switch( aSection )
    {
    case SectionType::HEADER:
        // Return version, uuid, paper, title_block
        {
            auto ver = SexprUtil::FindFirstChild( root.get(), "version" );
            auto gen = SexprUtil::FindFirstChild( root.get(), "generator" );
            auto genVer = SexprUtil::FindFirstChild( root.get(), "generator_version" );
            auto uuid = SexprUtil::FindFirstChild( root.get(), "uuid" );
            auto paper = SexprUtil::FindFirstChild( root.get(), "paper" );
            auto title = SexprUtil::FindFirstChild( root.get(), "title_block" );

            if( ver ) result << SexprUtil::ToString( ver ) << "\n";
            if( gen ) result << SexprUtil::ToString( gen ) << "\n";
            if( genVer ) result << SexprUtil::ToString( genVer ) << "\n";
            if( uuid ) result << SexprUtil::ToString( uuid ) << "\n";
            if( paper ) result << SexprUtil::ToString( paper ) << "\n";
            if( title ) result << SexprUtil::ToString( title ) << "\n";
        }
        break;

    case SectionType::LIB_SYMBOLS:
        {
            auto libSymbols = SexprUtil::FindFirstChild( root.get(), "lib_symbols" );
            if( libSymbols )
                result << SexprUtil::ToString( libSymbols );
        }
        break;

    case SectionType::SYMBOLS:
        appendElements( "symbol" );
        break;

    case SectionType::WIRES:
        appendElements( "wire" );
        break;

    case SectionType::JUNCTIONS:
        appendElements( "junction" );
        break;

    case SectionType::LABELS:
        appendElements( "label" );
        break;

    case SectionType::TEXT:
        appendElements( "text" );
        break;

    case SectionType::SHEETS:
        appendElements( "sheet" );
        break;

    case SectionType::SHEET_INSTANCES:
        {
            auto sheetInst = SexprUtil::FindFirstChild( root.get(), "sheet_instances" );
            if( sheetInst )
                result << SexprUtil::ToString( sheetInst );
        }
        break;

    default:
        return content;
    }

    return result.str();
}


std::string FindByUuid( const std::string& aContent, const std::string& aUuid )
{
    // Use regex to find the element containing this UUID
    // Pattern matches any element that contains (uuid "target-uuid")
    std::string pattern = R"(\([a-z_]+\s[^)]*\(uuid\s+\"?)" + aUuid + R"(\"?\)[^)]*\))";

    // This is a simplification - real implementation would need proper S-expr balancing
    // For now, let's parse and search
    auto root = SexprUtil::Parse( aContent );
    if( !root || !root->IsList() )
        return "";

    auto children = root->GetChildren();
    if( !children )
        return "";

    for( const auto& child : *children )
    {
        if( !child->IsList() )
            continue;

        auto uuidExpr = SexprUtil::FindFirstChild( child, "uuid" );
        if( uuidExpr && SexprUtil::GetStringValue( uuidExpr ) == aUuid )
            return SexprUtil::ToString( child );
    }

    return "";
}


std::vector<std::string> FindSymbolsByReference( const std::string& aContent,
                                                  const std::string& aPattern )
{
    std::vector<std::string> results;

    auto root = SexprUtil::Parse( aContent );
    if( !root || !root->IsList() )
        return results;

    auto symbols = SexprUtil::FindChildren( root.get(), "symbol" );
    for( const auto& sym : symbols )
    {
        std::string ref = ExtractReference( sym );
        if( MatchesPattern( ref, aPattern ) )
            results.push_back( SexprUtil::ToString( sym ) );
    }

    return results;
}


std::string DeleteByUuid( const std::string& aContent, const std::string& aUuid )
{
    // Find and remove the element with matching UUID
    // We need to find the start and end of the element

    // Pattern to find uuid in content
    std::string uuidPattern = "(uuid \"" + aUuid + "\")";
    size_t uuidPos = aContent.find( uuidPattern );
    if( uuidPos == std::string::npos )
    {
        // Try without quotes
        uuidPattern = "(uuid " + aUuid + ")";
        uuidPos = aContent.find( uuidPattern );
        if( uuidPos == std::string::npos )
            return aContent;  // Not found
    }

    // Find the start of the containing element (go backwards to find opening paren)
    int parenDepth = 0;
    size_t elementStart = uuidPos;

    // First, count how deep we are
    while( elementStart > 0 )
    {
        elementStart--;
        if( aContent[elementStart] == ')' )
            parenDepth++;
        else if( aContent[elementStart] == '(' )
        {
            if( parenDepth == 0 )
            {
                // Found the start of our element
                break;
            }
            parenDepth--;
        }
    }

    // Now find the end of the element
    size_t elementEnd = uuidPos;
    parenDepth = 1;  // We're inside the element

    // Find where element starts and count from there
    elementEnd = elementStart + 1;
    parenDepth = 1;

    while( elementEnd < aContent.length() && parenDepth > 0 )
    {
        if( aContent[elementEnd] == '(' )
            parenDepth++;
        else if( aContent[elementEnd] == ')' )
            parenDepth--;
        elementEnd++;
    }

    // Remove the element and any trailing whitespace/newline
    while( elementEnd < aContent.length() &&
           ( aContent[elementEnd] == '\n' || aContent[elementEnd] == '\t' ||
             aContent[elementEnd] == ' ' ) )
    {
        elementEnd++;
    }

    std::string result = aContent.substr( 0, elementStart );
    result += aContent.substr( elementEnd );

    return result;
}


std::string AddElement( const std::string& aContent, const std::string& aElementType,
                        const std::string& aElement )
{
    // Find the appropriate place to insert the element
    // Elements should be added before sheet_instances and embedded_fonts
    // The order is typically: symbols, wires, junctions, labels, text, sheets, sheet_instances

    std::string result = aContent;

    // Find the position to insert (before closing paren of root, or before sheet_instances)
    size_t insertPos = result.rfind( "\n)" );
    if( insertPos == std::string::npos )
        insertPos = result.rfind( ")" );

    // Try to find a better position based on element type
    static const std::vector<std::string> insertOrder = {
        "sheet_instances", "embedded_fonts"
    };

    for( const auto& marker : insertOrder )
    {
        std::string searchStr = "\t(" + marker;
        size_t markerPos = result.find( searchStr );
        if( markerPos != std::string::npos )
        {
            insertPos = markerPos;
            break;
        }
    }

    // Insert the element
    std::string toInsert = "\t" + aElement + "\n";
    result.insert( insertPos, toInsert );

    return result;
}


std::string UpdateElement( const std::string& aContent, const std::string& aUuid,
                           const std::string& aNewElement )
{
    // Delete old element and add new one
    std::string result = DeleteByUuid( aContent, aUuid );
    if( result == aContent )
        return aContent;  // Element not found

    // Determine element type from the new element
    auto parsed = SexprUtil::Parse( aNewElement );
    if( !parsed )
        return aContent;

    std::string elementType = SexprUtil::GetListType( parsed.get() );

    return AddElement( result, elementType, aNewElement );
}

} // namespace SchParser
