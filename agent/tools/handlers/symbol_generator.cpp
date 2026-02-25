#include "symbol_generator.h"
#include "tools/tool_registry.h"
#include <zeo/agent_auth.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/log.h>
#include <wx/app.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <cmath>
#include <thread>

using json = nlohmann::json;

/**
 * Escape a string for KiCad S-expression format.
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

/**
 * Null-safe JSON string accessor.
 * json::value() throws when the key exists but has a null value.
 */
static std::string SafeStr( const json& j, const char* key, const std::string& def )
{
    auto it = j.find( key );
    if( it == j.end() || it->is_null() )
        return def;
    return it->get<std::string>();
}


// KiCad grid spacing for pins (100 mil = 2.54 mm)
static constexpr double PIN_SPACING = 2.54;
static constexpr double PIN_LENGTH = 2.54;
static constexpr double GROUP_GAP = 2.54;
static constexpr double MIN_BODY_WIDTH = 10.16;   // 400 mil
static constexpr double MIN_BODY_HEIGHT = 5.08;    // 200 mil
static constexpr double FONT_SIZE = 1.27;
static constexpr double PROP_OFFSET = 1.27;


// ---------------------------------------------------------------------------
// Execute (sync stub — not used since IsAsync returns true)
// ---------------------------------------------------------------------------
std::string SYMBOL_GENERATOR::Execute( const std::string& aToolName,
                                        const nlohmann::json& aInput )
{
    return R"({"error": "generate_symbol must be called asynchronously"})";
}


std::string SYMBOL_GENERATOR::GetDescription( const std::string& aToolName,
                                               const nlohmann::json& aInput ) const
{
    std::string part = aInput.value( "part_number", "" );
    return "Generating KiCad symbol for " + part;
}


// ---------------------------------------------------------------------------
// ExecuteAsync — spawns background thread for the full workflow
// ---------------------------------------------------------------------------
void SYMBOL_GENERATOR::ExecuteAsync( const std::string& aToolName,
                                      const nlohmann::json& aInput,
                                      const std::string& aToolUseId,
                                      wxEvtHandler* aEventHandler )
{
    std::string partNumber = aInput.value( "part_number", "" );
    std::string manufacturer = aInput.value( "manufacturer", "" );
    std::string datasheetUrl = aInput.value( "datasheet_url", "" );
    std::string componentId = aInput.value( "component_id", "" );
    std::string libraryName = aInput.value( "library_name", "" );

    if( datasheetUrl.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "datasheet_url is required"})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    if( libraryName.empty() )
        libraryName = "project";

    // Get project path on main thread
    const auto& reg = TOOL_REGISTRY::Instance();
    std::string projectPath = reg.GetProjectPath();

    if( projectPath.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "No project open. Open or create a project first."})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "SYMBOL_GENERATOR: Starting for %s (datasheet=%s, component_id=%s)",
               partNumber.c_str(), datasheetUrl.c_str(), componentId.c_str() );

    std::thread( [=, this]()
    {
        std::string resultStr = DoGenerate( partNumber, manufacturer, datasheetUrl,
                                             componentId, libraryName, projectPath );

        bool success = false;
        try
        {
            auto resultJson = json::parse( resultStr );
            success = !resultJson.contains( "error" );
        }
        catch( ... ) {}

        wxTheApp->CallAfter( [=]()
        {
            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name = "generate_symbol";
            result.result = resultStr;
            result.success = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


// ---------------------------------------------------------------------------
// DoGenerate — full workflow (runs on background thread)
// ---------------------------------------------------------------------------
std::string SYMBOL_GENERATOR::DoGenerate( const std::string& aPartNumber,
                                           const std::string& aManufacturer,
                                           const std::string& aDatasheetUrl,
                                           const std::string& aComponentId,
                                           const std::string& aLibraryName,
                                           const std::string& aProjectPath )
{
    // Step 1: Check local library for existing symbol with matching datasheet URL
    if( !aDatasheetUrl.empty() )
    {
        std::string existingLibId = FindLocalSymbolByDatasheet( aDatasheetUrl, aProjectPath );
        if( !existingLibId.empty() )
        {
            wxLogInfo( "SYMBOL_GENERATOR: Found existing local symbol: %s", existingLibId.c_str() );
            json result;
            result["status"] = "already_exists";
            result["message"] = "Symbol already exists in local library";
            result["lib_id"] = existingLibId;
            return result.dump( 2 );
        }
    }

    // Step 2: Try to fetch component data from DB
    ComponentData data;
    bool haveData = FetchComponentData( aPartNumber, aManufacturer, data,
                                         aComponentId, aDatasheetUrl );

    // Step 3: If no extraction data, auto-trigger extraction
    if( !haveData && !aDatasheetUrl.empty() )
    {
        wxLogInfo( "SYMBOL_GENERATOR: No extraction data found, triggering extraction" );
        std::string newComponentId = TriggerExtractionSync( aPartNumber, aManufacturer,
                                                             aDatasheetUrl );

        if( newComponentId.empty() )
        {
            json err;
            err["error"] = "Datasheet extraction failed. Check the URL is accessible.";
            return err.dump();
        }

        // Re-fetch with the component ID we got back
        haveData = FetchComponentData( aPartNumber, aManufacturer, data, newComponentId );
    }

    if( !haveData )
    {
        json err;
        err["error"] = "Component not found. Provide a datasheet_url to extract data automatically.";
        return err.dump();
    }

    if( data.pins.empty() )
    {
        json err;
        err["error"] = "No pin data available for this component. The datasheet may not have "
                        "contained parseable pin information.";
        return err.dump();
    }

    // Step 4: Generate and write the symbol
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    std::string libFileName = aLibraryName + ".kicad_sym";
    wxFileName libPath( projDir.GetPath(), wxString::FromUTF8( libFileName ) );
    std::string outputPath = libPath.GetFullPath().ToStdString();

    wxLogInfo( "SYMBOL_GENERATOR: Output path: %s", outputPath.c_str() );

    std::string symbolContent = GenerateSymbolContent( data, aLibraryName );
    bool libExists = wxFileName::FileExists( libPath.GetFullPath() );

    if( libExists )
    {
        std::ifstream existingFile( outputPath );
        std::string existingContent( ( std::istreambuf_iterator<char>( existingFile ) ),
                                       std::istreambuf_iterator<char>() );
        existingFile.close();

        // Check if symbol already exists by name
        std::string symbolTag = "(symbol \"" + data.part_number + "\"";
        if( existingContent.find( symbolTag ) != std::string::npos )
        {
            json result;
            result["status"] = "already_exists";
            result["message"] = "Symbol '" + data.part_number + "' already exists in " + libFileName;
            result["lib_id"] = aLibraryName + ":" + data.part_number;
            result["file_path"] = outputPath;
            return result.dump( 2 );
        }

        // Append symbol before closing paren
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
        }
    }
    else
    {
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
    }

    wxLogInfo( "SYMBOL_GENERATOR: Symbol written for %s (%zu pins)",
               data.part_number.c_str(), data.pins.size() );

    json result;
    result["status"] = "created";
    result["message"] = "Symbol '" + data.part_number + "' generated with "
                        + std::to_string( data.pins.size() ) + " pins";
    result["lib_id"] = aLibraryName + ":" + data.part_number;
    result["file_path"] = outputPath;
    result["pin_count"] = data.pins.size();
    result["instructions"] = "Add the library to the project symbol table if not already present, "
                             "then use sch_add with lib_id '" + aLibraryName + ":"
                             + data.part_number + "' to place the symbol.";
    return result.dump( 2 );
}


// ---------------------------------------------------------------------------
// FindLocalSymbolByDatasheet — scan .kicad_sym files for matching Datasheet URL
// ---------------------------------------------------------------------------
std::string SYMBOL_GENERATOR::FindLocalSymbolByDatasheet( const std::string& aDatasheetUrl,
                                                           const std::string& aProjectPath )
{
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    wxString dirPath = projDir.GetPath();

    wxArrayString files;
    wxDir::GetAllFiles( dirPath, &files, "*.kicad_sym", wxDIR_FILES );

    // Search pattern: (property "Datasheet" "URL"
    std::string searchStr = "(property \"Datasheet\" \"" + EscapeSExpr( aDatasheetUrl ) + "\"";

    for( const auto& filePath : files )
    {
        std::ifstream file( filePath.ToStdString() );
        if( !file.is_open() )
            continue;

        std::string content( ( std::istreambuf_iterator<char>( file ) ),
                               std::istreambuf_iterator<char>() );
        file.close();

        size_t datasheetPos = content.find( searchStr );
        if( datasheetPos == std::string::npos )
            continue;

        // Found! Now find the enclosing symbol name.
        // Walk backwards from datasheetPos to find (symbol "NAME"
        size_t searchStart = content.rfind( "(symbol \"", datasheetPos );
        if( searchStart == std::string::npos )
            continue;

        size_t nameStart = searchStart + 9; // length of '(symbol "'
        size_t nameEnd = content.find( '"', nameStart );
        if( nameEnd == std::string::npos )
            continue;

        std::string symbolName = content.substr( nameStart, nameEnd - nameStart );

        // Skip sub-symbol entries like "LT8711HE_0_1" or "LT8711HE_1_1"
        // These contain underscores followed by digits — the real symbol doesn't
        if( symbolName.find( '_' ) != std::string::npos )
        {
            // Check if it ends with _N_N pattern
            size_t lastUnderscore = symbolName.rfind( '_' );
            if( lastUnderscore > 0 )
            {
                size_t secondLastUnderscore = symbolName.rfind( '_', lastUnderscore - 1 );
                if( secondLastUnderscore != std::string::npos )
                {
                    // Re-search from further back to find the parent symbol
                    searchStart = content.rfind( "(symbol \"", searchStart - 1 );
                    if( searchStart != std::string::npos )
                    {
                        nameStart = searchStart + 9;
                        nameEnd = content.find( '"', nameStart );
                        if( nameEnd != std::string::npos )
                            symbolName = content.substr( nameStart, nameEnd - nameStart );
                    }
                }
            }
        }

        // Derive library name from filename
        wxFileName fn( filePath );
        std::string libName = fn.GetName().ToStdString();

        return libName + ":" + symbolName;
    }

    return "";
}


// ---------------------------------------------------------------------------
// FetchComponentData — query Supabase by datasheet URL, component ID, or part number
// ---------------------------------------------------------------------------
bool SYMBOL_GENERATOR::FetchComponentData( const std::string& aPartNumber,
                                            const std::string& aManufacturer,
                                            ComponentData& aData,
                                            const std::string& aComponentId,
                                            const std::string& aDatasheetUrl )
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

    std::string url = supabaseUrl + "/rest/v1/components?select="
        "id,part_number,manufacturer,description,category,datasheet_url,"
        "extraction_status,"
        "component_packages(id,package_type,"
            "component_pins(pin_number,pin_name,pin_type,function_group),"
            "component_footprints(footprint_library,footprint_name,is_datasheet_recommended)"
        ")";

    // Priority: datasheet_url > component_id > part_number
    if( !aDatasheetUrl.empty() )
    {
        url += "&datasheet_url=eq." + UrlEncode( aDatasheetUrl );
    }
    else if( !aComponentId.empty() )
    {
        url += "&id=eq." + UrlEncode( aComponentId );
    }
    else
    {
        url += "&part_number=eq." + UrlEncode( aPartNumber );

        if( !aManufacturer.empty() )
            url += "&manufacturer=eq." + UrlEncode( aManufacturer );
    }

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

        if( SafeStr( comp, "extraction_status", "" ) != "completed" )
        {
            wxLogTrace( "Agent", "SYMBOL_GENERATOR: Extraction not completed" );
            return false;
        }

        aData.part_number = SafeStr( comp, "part_number", aPartNumber );
        aData.manufacturer = SafeStr( comp, "manufacturer", aManufacturer );
        aData.description = SafeStr( comp, "description", "" );
        aData.category = SafeStr( comp, "category", "" );
        aData.datasheet_url = SafeStr( comp, "datasheet_url", "" );

        if( comp.contains( "component_packages" ) )
        {
            for( const auto& pkg : comp["component_packages"] )
            {
                if( pkg.contains( "component_pins" ) )
                {
                    for( const auto& pin : pkg["component_pins"] )
                    {
                        PinData pd;
                        pd.number = SafeStr( pin, "pin_number", "" );
                        pd.name = SafeStr( pin, "pin_name", "" );
                        pd.type = SafeStr( pin, "pin_type", "passive" );
                        pd.function_group = SafeStr( pin, "function_group", "" );
                        aData.pins.push_back( pd );
                    }
                }

                if( aData.footprint.empty() && pkg.contains( "component_footprints" ) )
                {
                    for( const auto& fp : pkg["component_footprints"] )
                    {
                        std::string lib = SafeStr( fp, "footprint_library", "" );
                        std::string name = SafeStr( fp, "footprint_name", "" );
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


// ---------------------------------------------------------------------------
// TriggerExtractionSync — call extract-datasheet edge function, return component_id
// ---------------------------------------------------------------------------
std::string SYMBOL_GENERATOR::TriggerExtractionSync( const std::string& aPartNumber,
                                                      const std::string& aManufacturer,
                                                      const std::string& aDatasheetUrl )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return "";

    std::string accessToken = auth->GetAccessToken();
    if( accessToken.empty() )
        return "";

    json body;
    body["datasheet_url"] = aDatasheetUrl;
    body["mode"] = "sync";

    if( !aPartNumber.empty() )
        body["part_number"] = aPartNumber;
    if( !aManufacturer.empty() )
        body["manufacturer"] = aManufacturer;

    std::string url = supabaseUrl + "/functions/v1/extract-datasheet";

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetHeader( "apikey", anonKey );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetPostFields( body.dump() );

        // Sync extraction can take 30-120s
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 180L );

        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();
        std::string response = curl.GetBuffer();

        wxLogInfo( "SYMBOL_GENERATOR: Extraction HTTP %ld for %s",
                    httpCode, aDatasheetUrl.c_str() );

        if( httpCode >= 200 && httpCode < 300 )
        {
            auto resultJson = json::parse( response );
            std::string status = SafeStr( resultJson, "extraction_status", "" );

            if( status == "completed" )
                return SafeStr( resultJson, "component_id", "" );
        }

        wxLogTrace( "Agent", "SYMBOL_GENERATOR: Extraction failed HTTP %ld: %s",
                    httpCode, response.c_str() );
        return "";
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "SYMBOL_GENERATOR: Extraction exception: %s", e.what() );
        return "";
    }
}


// ---------------------------------------------------------------------------
// Pin layout and symbol generation (unchanged logic)
// ---------------------------------------------------------------------------
SYMBOL_GENERATOR::PinLayout SYMBOL_GENERATOR::AssignPinSides(
    const std::vector<PinData>& aPins )
{
    PinLayout layout;

    std::map<std::string, std::vector<const PinData*>> groups;
    for( const auto& pin : aPins )
    {
        std::string group = pin.function_group.empty() ? "Other" : pin.function_group;
        groups[group].push_back( &pin );
    }

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
                layout.left.push_back( pin );
        }
    }

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
    if( aType == "open_drain" )     return "open_collector";
    if( aType == "open_collector" ) return "open_collector";
    if( aType == "passive" )        return "passive";
    if( aType == "no_connect" )     return "no_connect";
    if( aType == "ground" )         return "power_in";
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

    auto countWithGaps = []( const std::vector<const PinData*>& pins ) -> int
    {
        if( pins.empty() )
            return 0;

        int count = static_cast<int>( pins.size() );
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

    int verticalSlots = std::max( leftSlots, rightSlots );
    int horizontalSlots = std::max( topSlots, bottomSlots );

    double bodyHeight = std::max( MIN_BODY_HEIGHT, verticalSlots * PIN_SPACING );
    double bodyWidth = std::max( MIN_BODY_WIDTH, horizontalSlots * PIN_SPACING );

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

    bodyWidth = std::ceil( bodyWidth / PIN_SPACING ) * PIN_SPACING;
    bodyHeight = std::ceil( bodyHeight / PIN_SPACING ) * PIN_SPACING;

    double halfW = bodyWidth / 2.0;
    double halfH = bodyHeight / 2.0;

    std::string ref = CategoryToReference( aData.category );

    std::ostringstream ss;
    ss << std::fixed;
    ss.precision( 2 );

    std::string escPart = EscapeSExpr( aData.part_number );
    std::string escFootprint = EscapeSExpr( aData.footprint );
    std::string escDatasheet = EscapeSExpr( aData.datasheet_url );
    std::string escDescription = EscapeSExpr( aData.description );
    std::string escManufacturer = EscapeSExpr( aData.manufacturer );

    ss << "\t(symbol \"" << escPart << "\"\n";
    ss << "\t\t(pin_names\n";
    ss << "\t\t\t(offset 1.016)\n";
    ss << "\t\t)\n";
    ss << "\t\t(exclude_from_sim no)\n";
    ss << "\t\t(in_bom yes)\n";
    ss << "\t\t(on_board yes)\n";

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

    ss << "\t\t(symbol \"" << escPart << "_0_1\"\n";
    ss << "\t\t\t(rectangle\n";
    ss << "\t\t\t\t(start " << -halfW << " " << halfH << ")\n";
    ss << "\t\t\t\t(end " << halfW << " " << -halfH << ")\n";
    ss << "\t\t\t\t(stroke (width 0.254) (type default))\n";
    ss << "\t\t\t\t(fill (type background))\n";
    ss << "\t\t\t)\n";
    ss << "\t\t)\n";

    ss << "\t\t(symbol \"" << escPart << "_1_1\"\n";

    auto emitPin = [&]( const PinData* pin, double x, double y, int orientation )
    {
        std::string kiType = PinTypeToKiCad( pin->type );
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

    auto emitSidePins = [&]( const std::vector<const PinData*>& pins,
                              double startX, double startY,
                              double dx, double dy, int orientation )
    {
        std::string lastGroup;
        int slot = 0;

        for( const auto* pin : pins )
        {
            if( !lastGroup.empty() && pin->function_group != lastGroup )
                slot++;

            double x = startX + slot * dx;
            double y = startY + slot * dy;
            emitPin( pin, x, y, orientation );

            lastGroup = pin->function_group;
            slot++;
        }
    };

    if( !layout.left.empty() )
    {
        double startY = ( ( leftSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.left, -( halfW + PIN_LENGTH ), startY, 0, -PIN_SPACING, 0 );
    }

    if( !layout.right.empty() )
    {
        double startY = ( ( rightSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.right, halfW + PIN_LENGTH, startY, 0, -PIN_SPACING, 180 );
    }

    if( !layout.top.empty() )
    {
        double startX = -( ( topSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.top, startX, halfH + PIN_LENGTH, PIN_SPACING, 0, 270 );
    }

    if( !layout.bottom.empty() )
    {
        double startX = -( ( bottomSlots - 1 ) * PIN_SPACING ) / 2.0;
        emitSidePins( layout.bottom, startX, -( halfH + PIN_LENGTH ), PIN_SPACING, 0, 90 );
    }

    ss << "\t\t)\n";
    ss << "\t\t(embedded_fonts no)\n";
    ss << "\t)\n";

    return ss.str();
}
