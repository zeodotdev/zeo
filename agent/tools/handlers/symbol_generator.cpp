#include "symbol_generator.h"
#include "tools/tool_registry.h"
#include <zeo/agent_auth.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/log.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <cmath>

using json = nlohmann::json;

/**
 * Escape a string for KiCad S-expression format.
 * Double quotes and backslashes must be escaped.
 */
static std::string EscapeSExpr( const std::string& s )
{
    std::string out;
    out.reserve( s.size() );

    for( char c : s )
    {
        if( c == '"' )       out += "\\\"";
        else if( c == '\\' ) out += "\\\\";
        else                 out += c;
    }

    return out;
}

/**
 * URL-encode a string using libcurl.
 */
static std::string UrlEncode( const std::string& s )
{
    KICAD_CURL_EASY curlHelper;
    char* encoded = curl_easy_escape( curlHelper.GetCurl(),
                                       s.c_str(),
                                       static_cast<int>( s.size() ) );
    std::string result = encoded ? encoded : s;

    if( encoded )
        curl_free( encoded );

    return result;
}

// KiCad grid spacing for pins (100 mil = 2.54 mm)
static constexpr double PIN_SPACING = 2.54;
// Pin length in mm
static constexpr double PIN_LENGTH = 2.54;
// Gap between pin groups on a side
static constexpr double GROUP_GAP = 2.54;
// Minimum body width/height
static constexpr double MIN_BODY_WIDTH = 10.16;  // 400 mil
static constexpr double MIN_BODY_HEIGHT = 5.08;  // 200 mil
// Font size for pin names and numbers
static constexpr double FONT_SIZE = 1.27;
// Property text offset from body
static constexpr double PROP_OFFSET = 1.27;


std::string SYMBOL_GENERATOR::Execute( const std::string& aToolName,
                                        const nlohmann::json& aInput )
{
    std::string partNumber = aInput.value( "part_number", "" );
    std::string manufacturer = aInput.value( "manufacturer", "" );
    std::string libraryName = aInput.value( "library_name", "" );

    if( partNumber.empty() )
        return R"({"error": "part_number is required"})";

    // Default library name to project-local library
    if( libraryName.empty() )
        libraryName = "project";

    wxLogInfo( "SYMBOL_GENERATOR: Generating symbol for %s by %s",
               partNumber.c_str(), manufacturer.c_str() );

    // Fetch component data from database
    ComponentData data;
    if( !FetchComponentData( partNumber, manufacturer, data ) )
    {
        return R"({"error": "Component not found or extraction not completed. Use extract_datasheet first."})";
    }

    if( data.pins.empty() )
    {
        return R"({"error": "No pin data available for this component. Cannot generate symbol."})";
    }

    // Determine output path
    const auto& reg = TOOL_REGISTRY::Instance();
    std::string projectPath = reg.GetProjectPath();

    if( projectPath.empty() )
        return R"({"error": "No project open. Open or create a project first."})";

    // Write to <project_dir>/<library_name>.kicad_sym
    wxFileName projDir( wxString::FromUTF8( projectPath ), "" );
    std::string libFileName = libraryName + ".kicad_sym";
    wxFileName libPath( projDir.GetPath(), wxString::FromUTF8( libFileName ) );
    std::string outputPath = libPath.GetFullPath().ToStdString();

    wxLogInfo( "SYMBOL_GENERATOR: Output path: %s", outputPath.c_str() );

    // Check if library file already exists — if so, we need to check for the
    // symbol name and either append or report conflict
    bool libExists = wxFileName::FileExists( libPath.GetFullPath() );

    // Generate the symbol S-expression content
    std::string symbolContent = GenerateSymbolContent( data, libraryName );

    if( libExists )
    {
        // Read existing file
        std::ifstream existingFile( outputPath );
        std::string existingContent( ( std::istreambuf_iterator<char>( existingFile ) ),
                                       std::istreambuf_iterator<char>() );
        existingFile.close();

        // Check if symbol already exists in library
        std::string symbolTag = "(symbol \"" + data.part_number + "\"";
        if( existingContent.find( symbolTag ) != std::string::npos )
        {
            json result;
            result["status"] = "already_exists";
            result["message"] = "Symbol '" + data.part_number + "' already exists in " + libFileName;
            result["lib_id"] = libraryName + ":" + data.part_number;
            result["file_path"] = outputPath;
            return result.dump( 2 );
        }

        // Append symbol before the closing parenthesis
        size_t lastParen = existingContent.rfind( ')' );
        if( lastParen != std::string::npos )
        {
            std::string newContent = existingContent.substr( 0, lastParen )
                                     + symbolContent + "\n)\n";

            std::ofstream outFile( outputPath );

            if( !outFile.is_open() )
            {
                json err;
                err["error"] = "Failed to open library file for writing: " + outputPath;
                return err.dump();
            }

            outFile << newContent;
            outFile.close();

            if( outFile.fail() )
            {
                json err;
                err["error"] = "Failed to write library file: " + outputPath;
                return err.dump();
            }
        }
    }
    else
    {
        // Create new library file
        std::ofstream outFile( outputPath );

        if( !outFile.is_open() )
        {
            json err;
            err["error"] = "Failed to create library file: " + outputPath;
            return err.dump();
        }

        outFile << "(kicad_symbol_lib\n"
                << "\t(version 20241209)\n"
                << "\t(generator \"zeo_agent\")\n"
                << "\t(generator_version \"1.0\")\n"
                << symbolContent << "\n)\n";
        outFile.close();

        if( outFile.fail() )
        {
            json err;
            err["error"] = "Failed to write library file: " + outputPath;
            return err.dump();
        }
    }

    wxLogInfo( "SYMBOL_GENERATOR: Symbol written for %s (%zu pins)",
               data.part_number.c_str(), data.pins.size() );

    json result;
    result["status"] = "created";
    result["message"] = "Symbol '" + data.part_number + "' generated with "
                        + std::to_string( data.pins.size() ) + " pins";
    result["lib_id"] = libraryName + ":" + data.part_number;
    result["file_path"] = outputPath;
    result["pin_count"] = data.pins.size();
    result["instructions"] = "Add the library to the project symbol table if not already present, "
                             "then use sch_add with lib_id '" + libraryName + ":"
                             + data.part_number + "' to place the symbol.";
    return result.dump( 2 );
}


std::string SYMBOL_GENERATOR::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string part = aInput.value( "part_number", "" );
    return "Generating KiCad symbol for " + part;
}


bool SYMBOL_GENERATOR::FetchComponentData( const std::string& aPartNumber,
                                            const std::string& aManufacturer,
                                            ComponentData& aData )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return false;

    std::string accessToken = auth->GetAccessToken();
    if( accessToken.empty() )
        return false;

    // Query component with packages (which contain pins) and footprints
    std::string url = supabaseUrl + "/rest/v1/components?select="
        "id,part_number,manufacturer,description,category,datasheet_url,"
        "extraction_status,"
        "component_packages(id,package_type,"
            "component_pins(pin_number,pin_name,pin_type,function_group),"
            "component_footprints(footprint_library,footprint_name,is_datasheet_recommended)"
        ")"
        "&part_number=eq." + UrlEncode( aPartNumber );

    if( !aManufacturer.empty() )
        url += "&manufacturer=eq." + UrlEncode( aManufacturer );

    url += "&limit=1";

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetHeader( "apikey", anonKey );
        curl.SetHeader( "Accept", "application/json" );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();
        if( httpCode < 200 || httpCode >= 300 )
        {
            wxLogTrace( "Agent", "SYMBOL_GENERATOR: Query failed HTTP %ld", httpCode );
            return false;
        }

        auto response = json::parse( curl.GetBuffer() );
        if( !response.is_array() || response.empty() )
            return false;

        auto& comp = response[0];

        if( comp.value( "extraction_status", "" ) != "completed" )
        {
            wxLogTrace( "Agent", "SYMBOL_GENERATOR: Extraction not completed for %s",
                        aPartNumber.c_str() );
            return false;
        }

        aData.part_number = comp.value( "part_number", aPartNumber );
        aData.manufacturer = comp.value( "manufacturer", aManufacturer );
        aData.description = comp.value( "description", "" );
        aData.category = comp.value( "category", "" );
        aData.datasheet_url = comp.value( "datasheet_url", "" );

        // Extract pins from packages
        if( comp.contains( "component_packages" ) )
        {
            for( const auto& pkg : comp["component_packages"] )
            {
                if( pkg.contains( "component_pins" ) )
                {
                    for( const auto& pin : pkg["component_pins"] )
                    {
                        PinData pd;
                        pd.number = pin.value( "pin_number", "" );
                        pd.name = pin.value( "pin_name", "" );
                        pd.type = pin.value( "pin_type", "passive" );
                        pd.function_group = pin.value( "function_group", "" );
                        aData.pins.push_back( pd );
                    }
                }

                // Get first recommended footprint
                if( aData.footprint.empty() && pkg.contains( "component_footprints" ) )
                {
                    for( const auto& fp : pkg["component_footprints"] )
                    {
                        std::string lib = fp.value( "footprint_library", "" );
                        std::string name = fp.value( "footprint_name", "" );
                        if( !name.empty() )
                        {
                            aData.footprint = lib.empty() ? name : ( lib + ":" + name );
                            break;
                        }
                    }
                }
            }
        }

        return !aData.pins.empty();
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "SYMBOL_GENERATOR: Fetch exception: %s", e.what() );
        return false;
    }
}


SYMBOL_GENERATOR::PinLayout SYMBOL_GENERATOR::AssignPinSides(
    const std::vector<PinData>& aPins )
{
    PinLayout layout;

    // Group pins by function_group for ordered placement
    std::map<std::string, std::vector<const PinData*>> groups;

    for( const auto& pin : aPins )
    {
        std::string group = pin.function_group.empty() ? "Other" : pin.function_group;
        groups[group].push_back( &pin );
    }

    // Assign each pin to a side based on type
    for( const auto& [groupName, pins] : groups )
    {
        for( const auto* pin : pins )
        {
            if( pin->type == "power_in" )
                layout.top.push_back( pin );
            else if( pin->type == "ground" )
                layout.bottom.push_back( pin );
            else if( pin->type == "output" || pin->type == "power_out"
                     || pin->type == "open_drain" || pin->type == "open_collector" )
                layout.right.push_back( pin );
            else if( pin->type == "no_connect" )
                layout.right.push_back( pin );
            else
                layout.left.push_back( pin );  // input, bidirectional, passive, etc.
        }
    }

    // Sort pins within each side by function_group then by pin name/number
    auto sortPins = []( std::vector<const PinData*>& pins )
    {
        std::sort( pins.begin(), pins.end(),
            []( const PinData* a, const PinData* b )
            {
                if( a->function_group != b->function_group )
                    return a->function_group < b->function_group;
                return a->name < b->name;
            } );
    };

    sortPins( layout.top );
    sortPins( layout.bottom );
    sortPins( layout.left );
    sortPins( layout.right );

    return layout;
}


std::string SYMBOL_GENERATOR::PinTypeToKiCad( const std::string& aType )
{
    if( aType == "power_in" )       return "power_in";
    if( aType == "power_out" )      return "power_out";
    if( aType == "input" )          return "input";
    if( aType == "output" )         return "output";
    if( aType == "bidirectional" )   return "bidirectional";
    if( aType == "open_drain" )     return "open_collector";  // KiCad uses open_collector for both
    if( aType == "open_collector" ) return "open_collector";
    if( aType == "passive" )        return "passive";
    if( aType == "no_connect" )     return "no_connect";
    if( aType == "ground" )         return "power_in";  // Ground is power_in in KiCad
    return "unspecified";
}


std::string SYMBOL_GENERATOR::CategoryToReference( const std::string& aCategory )
{
    if( aCategory == "ic" || aCategory == "module" )    return "U";
    if( aCategory == "connector" )                      return "J";
    if( aCategory == "passive" )                        return "R";
    if( aCategory == "discrete" )                       return "Q";
    if( aCategory == "sensor" )                         return "U";
    if( aCategory == "optoelectronic" )                 return "D";
    if( aCategory == "rf" )                             return "U";
    if( aCategory == "power" )                          return "U";
    if( aCategory == "electromechanical" )               return "K";
    return "U";
}


std::string SYMBOL_GENERATOR::GenerateSymbolContent( const ComponentData& aData,
                                                      const std::string& aLibName )
{
    PinLayout layout = AssignPinSides( aData.pins );

    // Calculate how many pins on each side, accounting for group gaps
    auto countWithGaps = []( const std::vector<const PinData*>& pins ) -> int
    {
        if( pins.empty() )
            return 0;

        int count = static_cast<int>( pins.size() );
        // Count group transitions (add gap for each new group)
        std::string lastGroup;
        for( const auto* pin : pins )
        {
            if( !lastGroup.empty() && pin->function_group != lastGroup )
                count++;
            lastGroup = pin->function_group;
        }
        return count;
    };

    int leftSlots = countWithGaps( layout.left );
    int rightSlots = countWithGaps( layout.right );
    int topSlots = countWithGaps( layout.top );
    int bottomSlots = countWithGaps( layout.bottom );

    // Body dimensions
    int verticalSlots = std::max( leftSlots, rightSlots );
    int horizontalSlots = std::max( topSlots, bottomSlots );

    double bodyHeight = std::max( MIN_BODY_HEIGHT, verticalSlots * PIN_SPACING );
    double bodyWidth = std::max( MIN_BODY_WIDTH, horizontalSlots * PIN_SPACING );

    // Also ensure body is wide enough for the longest pin name on left/right
    auto maxNameLen = []( const std::vector<const PinData*>& pins ) -> size_t
    {
        size_t maxLen = 0;
        for( const auto* pin : pins )
            maxLen = std::max( maxLen, pin->name.length() );
        return maxLen;
    };

    size_t maxLeftName = maxNameLen( layout.left );
    size_t maxRightName = maxNameLen( layout.right );
    double nameWidth = std::max( maxLeftName, maxRightName ) * FONT_SIZE * 0.7;
    bodyWidth = std::max( bodyWidth, nameWidth * 2 + 2.0 );

    // Snap body dimensions to grid
    bodyWidth = std::ceil( bodyWidth / PIN_SPACING ) * PIN_SPACING;
    bodyHeight = std::ceil( bodyHeight / PIN_SPACING ) * PIN_SPACING;

    double halfW = bodyWidth / 2.0;
    double halfH = bodyHeight / 2.0;

    std::string ref = CategoryToReference( aData.category );

    std::ostringstream ss;
    ss << std::fixed;
    ss.precision( 2 );

    // Escape strings for S-expression format
    std::string escPart = EscapeSExpr( aData.part_number );
    std::string escFootprint = EscapeSExpr( aData.footprint );
    std::string escDatasheet = EscapeSExpr( aData.datasheet_url );
    std::string escDescription = EscapeSExpr( aData.description );
    std::string escManufacturer = EscapeSExpr( aData.manufacturer );

    // Symbol definition
    ss << "\t(symbol \"" << escPart << "\"\n";
    ss << "\t\t(pin_names\n";
    ss << "\t\t\t(offset 1.016)\n";
    ss << "\t\t)\n";
    ss << "\t\t(exclude_from_sim no)\n";
    ss << "\t\t(in_bom yes)\n";
    ss << "\t\t(on_board yes)\n";

    // Properties
    ss << "\t\t(property \"Reference\" \"" << ref << "\"\n";
    ss << "\t\t\t(at 0 " << ( halfH + PROP_OFFSET + 2.54 ) << " 0)\n";
    ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")))\n";
    ss << "\t\t)\n";

    ss << "\t\t(property \"Value\" \"" << escPart << "\"\n";
    ss << "\t\t\t(at 0 " << ( halfH + PROP_OFFSET ) << " 0)\n";
    ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")))\n";
    ss << "\t\t)\n";

    ss << "\t\t(property \"Footprint\" \"" << escFootprint << "\"\n";
    ss << "\t\t\t(at 0 " << -( halfH + PROP_OFFSET ) << " 0)\n";
    ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")) (hide yes))\n";
    ss << "\t\t)\n";

    ss << "\t\t(property \"Datasheet\" \"" << escDatasheet << "\"\n";
    ss << "\t\t\t(at 0 " << -( halfH + PROP_OFFSET + 2.54 ) << " 0)\n";
    ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")) (hide yes))\n";
    ss << "\t\t)\n";

    ss << "\t\t(property \"Description\" \"" << escDescription << "\"\n";
    ss << "\t\t\t(at 0 0 0)\n";
    ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")) (hide yes))\n";
    ss << "\t\t)\n";

    if( !aData.manufacturer.empty() )
    {
        ss << "\t\t(property \"Manufacturer\" \"" << escManufacturer << "\"\n";
        ss << "\t\t\t(at 0 0 0)\n";
        ss << "\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")) (hide yes))\n";
        ss << "\t\t)\n";
    }

    // Body rectangle (unit 0 = common graphics)
    ss << "\t\t(symbol \"" << escPart << "_0_1\"\n";
    ss << "\t\t\t(rectangle\n";
    ss << "\t\t\t\t(start " << -halfW << " " << halfH << ")\n";
    ss << "\t\t\t\t(end " << halfW << " " << -halfH << ")\n";
    ss << "\t\t\t\t(stroke (width 0.254) (type default))\n";
    ss << "\t\t\t\t(fill (type background))\n";
    ss << "\t\t\t)\n";
    ss << "\t\t)\n";

    // Pins (unit 1 = all pins in single unit)
    ss << "\t\t(symbol \"" << escPart << "_1_1\"\n";

    // Helper lambda to emit a pin
    auto emitPin = [&]( const PinData* pin, double x, double y, int orientation )
    {
        std::string kiType = PinTypeToKiCad( pin->type );

        // Orientation: 0=right, 90=up, 180=left, 270=down
        ss << "\t\t\t(pin " << kiType << " line\n";
        ss << "\t\t\t\t(at " << x << " " << y << " " << orientation << ")\n";
        ss << "\t\t\t\t(length " << PIN_LENGTH << ")\n";
        ss << "\t\t\t\t(name \"" << EscapeSExpr( pin->name ) << "\"\n";
        ss << "\t\t\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")))\n";
        ss << "\t\t\t\t)\n";
        ss << "\t\t\t\t(number \"" << EscapeSExpr( pin->number ) << "\"\n";
        ss << "\t\t\t\t\t(effects (font (size " << FONT_SIZE << " " << FONT_SIZE << ")))\n";
        ss << "\t\t\t\t)\n";
        ss << "\t\t\t)\n";
    };

    // Emit pins with group gaps
    auto emitSidePins = [&]( const std::vector<const PinData*>& pins,
                              double startX, double startY,
                              double dx, double dy, int orientation )
    {
        std::string lastGroup;
        int slot = 0;

        for( const auto* pin : pins )
        {
            // Add gap between groups
            if( !lastGroup.empty() && pin->function_group != lastGroup )
                slot++;

            double x = startX + slot * dx;
            double y = startY + slot * dy;

            emitPin( pin, x, y, orientation );

            lastGroup = pin->function_group;
            slot++;
        }
    };

    // Left side: connection at left, pin extends right toward body (orientation 0)
    if( !layout.left.empty() )
    {
        double startY = ( ( leftSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.left,
                      -( halfW + PIN_LENGTH ), startY,
                      0, -PIN_SPACING, 0 );
    }

    // Right side: connection at right, pin extends left toward body (orientation 180)
    if( !layout.right.empty() )
    {
        double startY = ( ( rightSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.right,
                      halfW + PIN_LENGTH, startY,
                      0, -PIN_SPACING, 180 );
    }

    // Top side: connection at top, pin extends down toward body (orientation 270)
    if( !layout.top.empty() )
    {
        double startX = -( ( topSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.top,
                      startX, halfH + PIN_LENGTH,
                      PIN_SPACING, 0, 270 );
    }

    // Bottom side: connection at bottom, pin extends up toward body (orientation 90)
    if( !layout.bottom.empty() )
    {
        double startX = -( ( bottomSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.bottom,
                      startX, -( halfH + PIN_LENGTH ),
                      PIN_SPACING, 0, 90 );
    }

    ss << "\t\t)\n";  // close pin unit symbol
    ss << "\t\t(embedded_fonts no)\n";
    ss << "\t)\n";     // close symbol

    return ss.str();
}
