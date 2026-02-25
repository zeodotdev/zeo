#include "datasheet_handler.h"
#include "tools/tool_registry.h"
#include "../../agent_events.h"
#include <zeo/agent_auth.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/app.h>
#include <wx/log.h>
#include <thread>
#include <mutex>
#include <set>

using json = nlohmann::json;

// Tracks parts already triggered for background extraction this session.
// Prevents duplicate batch requests when sch_get_summary is called multiple
// times before a prior extraction finishes.
static std::mutex          s_triggeredMutex;
static std::set<std::string> s_triggeredParts;

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

// Generic KiCad library names that aren't manufacturers
static const std::set<std::string> GENERIC_LIB_NAMES = {
    "Device", "Connector", "Power", "Simulation", "Mechanical", "Sensor",
    "Timer", "Memory", "Interface", "Logic", "Analog", "RF", "Relay",
    "Switch", "Transistor", "Diode", "Regulator", "LED", "Motor",
    "Display", "Audio", "MCU_Module", "Oscillator", "Amplifier",
    "Comparator", "Converter_DAC", "Converter_ADC", "Reference_Voltage",
    "pspice", "power"
};


std::string DATASHEET_HANDLER::Execute( const std::string& aToolName,
                                         const nlohmann::json& aInput )
{
    if( aToolName == "datasheet_query" )
    {
        std::string partNumber = aInput.value( "part_number", "" );
        std::string manufacturer = aInput.value( "manufacturer", "" );

        if( partNumber.empty() )
            return R"({"error": "part_number is required"})";

        return QueryExtractionData( partNumber, manufacturer );
    }

    // extract_datasheet is async-only, shouldn't reach here
    return R"({"error": "extract_datasheet must be called asynchronously"})";
}


std::string DATASHEET_HANDLER::GetDescription( const std::string& aToolName,
                                                const nlohmann::json& aInput ) const
{
    if( aToolName == "extract_datasheet" )
    {
        std::string part = aInput.value( "part_number", "" );
        return "Extracting datasheet data for " + part;
    }

    std::string part = aInput.value( "part_number", "" );
    return "Query datasheet data for " + part;
}


void DATASHEET_HANDLER::ExecuteAsync( const std::string& aToolName,
                                       const nlohmann::json& aInput,
                                       const std::string& aToolUseId,
                                       wxEvtHandler* aEventHandler )
{
    if( aToolName != "extract_datasheet" )
        return;

    std::string partNumber = aInput.value( "part_number", "" );
    std::string manufacturer = aInput.value( "manufacturer", "" );
    std::string datasheetUrl = aInput.value( "datasheet_url", "" );

    if( partNumber.empty() || datasheetUrl.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "part_number and datasheet_url are required"})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "DATASHEET_HANDLER: Starting sync extraction for %s", partNumber.c_str() );

    // Spawn background thread for the synchronous HTTP call (30-60s)
    std::thread( [=, this]()
    {
        std::string extractResult = ExtractDatasheetSync( partNumber, manufacturer, datasheetUrl );

        wxLogInfo( "DATASHEET_HANDLER: Sync extraction complete for %s", partNumber.c_str() );

        // Parse the result to determine success
        bool success = false;
        try
        {
            auto resultJson = json::parse( extractResult );
            std::string status = resultJson.value( "extraction_status", "" );
            success = ( status == "completed" );
        }
        catch( ... )
        {
            // Parse failed, treat as error
        }

        // Post result back to main thread
        wxTheApp->CallAfter( [=]()
        {
            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name = aToolName;
            result.result = extractResult;
            result.success = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


std::string DATASHEET_HANDLER::ExtractDatasheetSync( const std::string& aPartNumber,
                                                      const std::string& aManufacturer,
                                                      const std::string& aDatasheetUrl )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return R"({"error": "Not configured"})";

    std::string accessToken = auth->GetAccessToken();

    if( accessToken.empty() )
        return R"({"error": "Not authenticated"})";

    // Build request body with mode=sync
    json body;
    body["part_number"] = aPartNumber;
    body["datasheet_url"] = aDatasheetUrl;
    body["mode"] = "sync";

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

        // Sync extraction can take 30-120s for large datasheets
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 180L );

        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();
        std::string response = curl.GetBuffer();

        wxLogInfo( "DATASHEET_HANDLER: Sync extraction HTTP %ld for %s",
                    httpCode, aPartNumber.c_str() );

        if( httpCode >= 200 && httpCode < 300 )
        {
            // Parse the response and check status
            auto resultJson = json::parse( response );
            std::string status = resultJson.value( "extraction_status", "unknown" );

            if( status == "completed" )
            {
                // Extraction done — now query the full data
                wxLogInfo( "DATASHEET_HANDLER: Sync extraction completed, querying data" );
                std::string queryResult = QueryExtractionData( aPartNumber, aManufacturer );

                try
                {
                    auto queryJson = json::parse( queryResult );

                    // If the query itself returned an error, propagate it
                    if( queryJson.contains( "error" ) )
                        return queryResult;

                    // Wrap with extraction metadata
                    queryJson["extraction_message"] = "Datasheet extracted successfully";
                    queryJson["extraction_input_tokens"] = resultJson.value( "input_tokens", 0 );
                    queryJson["extraction_output_tokens"] = resultJson.value( "output_tokens", 0 );
                    return queryJson.dump( 2 );
                }
                catch( const json::parse_error& e )
                {
                    wxLogTrace( "Agent", "DATASHEET_HANDLER: Failed to parse query result: %s",
                                e.what() );
                    return queryResult;
                }
            }

            return response;
        }
        else
        {
            wxLogTrace( "Agent", "DATASHEET_HANDLER: Sync extraction failed HTTP %ld: %s",
                        httpCode, response.c_str() );

            json errorResult;
            errorResult["error"] = "Extraction failed with HTTP " + std::to_string( httpCode );
            errorResult["detail"] = response;
            return errorResult.dump();
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "DATASHEET_HANDLER: Sync extraction exception: %s", e.what() );

        json errorResult;
        errorResult["error"] = "Extraction request failed";
        errorResult["detail"] = e.what();
        return errorResult.dump();
    }
}


std::string DATASHEET_HANDLER::QueryExtractionData( const std::string& aPartNumber,
                                                     const std::string& aManufacturer )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return R"({"error": "Not configured"})";

    std::string accessToken = auth->GetAccessToken();

    if( accessToken.empty() )
        return R"({"error": "Not authenticated"})";

    // Build the REST API query URL
    std::string url = supabaseUrl + "/rest/v1/components?select="
        "id,part_number,manufacturer,description,category,subcategory,lifecycle,"
        "extraction_status,extracted_at,"
        "component_packages(*),"
        "component_electrical(*),"
        "component_voltage_rails(*),"
        "component_placement(*),"
        "component_design_guidelines(*),"
        "component_decoupling_requirements(*),"
        "component_external_parts(*)"
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

        if( httpCode >= 200 && httpCode < 300 )
        {
            auto response = json::parse( curl.GetBuffer() );

            if( response.is_array() && !response.empty() )
            {
                return response[0].dump( 2 );
            }

            return R"({"extraction_status": "not_found", "message": "No extraction data found for this component"})";
        }
        else
        {
            wxLogTrace( "Agent", "DATASHEET_HANDLER: Query failed HTTP %ld: %s",
                        httpCode, curl.GetBuffer().c_str() );
            return R"({"error": "Query failed"})";
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "DATASHEET_HANDLER: Query exception: %s", e.what() );
        return R"({"error": "Query failed"})";
    }
}


void DATASHEET_HANDLER::ParseLibId( const std::string& aLibId,
                                     std::string& aPartNumber,
                                     std::string& aManufacturer )
{
    auto colonPos = aLibId.find( ':' );

    if( colonPos != std::string::npos )
    {
        aPartNumber = aLibId.substr( colonPos + 1 );
        std::string libName = aLibId.substr( 0, colonPos );

        if( GENERIC_LIB_NAMES.find( libName ) == GENERIC_LIB_NAMES.end() )
            aManufacturer = libName;
        else
            aManufacturer.clear();
    }
    else
    {
        aPartNumber = aLibId;
        aManufacturer.clear();
    }
}


void DATASHEET_HANDLER::MaybeTriggerExtraction( const std::string& aToolName,
                                                 const std::string& aToolResult )
{
    json resultJson;

    try
    {
        resultJson = json::parse( aToolResult );
    }
    catch( ... )
    {
        return;
    }

    // Collect symbols from either sch_add or sch_get_summary format
    std::vector<const json*> symbolEntries;

    if( aToolName == "sch_add" && resultJson.contains( "results" ) )
    {
        for( const auto& r : resultJson["results"] )
        {
            if( r.value( "element_type", "" ) == "symbol" )
                symbolEntries.push_back( &r );
        }
    }
    else if( ( aToolName == "sch_get_summary" || aToolName == "sch_read_section" )
             && resultJson.contains( "symbols" ) )
    {
        for( const auto& s : resultJson["symbols"] )
            symbolEntries.push_back( &s );
    }

    if( symbolEntries.empty() )
        return;

    // Deduplicate by lib_id (don't trigger twice for same component)
    std::set<std::string> seen;
    int triggered = 0;

    for( const auto* entry : symbolEntries )
    {
        std::string datasheetUrl = entry->value( "datasheet_url", "" );

        if( datasheetUrl.empty() )
            continue;

        std::string libId = entry->value( "lib_id", "" );

        if( libId.empty() || seen.count( libId ) )
            continue;

        seen.insert( libId );

        std::string partNumber, manufacturer;
        ParseLibId( libId, partNumber, manufacturer );

        if( partNumber.empty() )
            continue;

        // Skip generic passives (R, C, L) — no useful datasheet to extract
        if( partNumber == "R" || partNumber == "C" || partNumber == "L"
            || partNumber == "R_Small" || partNumber == "C_Small"
            || partNumber == "C_Polarized" || partNumber == "L_Small" )
            continue;

        wxLogInfo( "DATASHEET_HANDLER: Triggering extraction for %s (%s)",
                   partNumber.c_str(), datasheetUrl.c_str() );

        TriggerExtractionAsync( partNumber, manufacturer, datasheetUrl );
        triggered++;
    }

    if( triggered > 0 )
    {
        wxLogInfo( "DATASHEET_HANDLER: Triggered %d background extractions from %s",
                   triggered, aToolName.c_str() );
    }
}


void DATASHEET_HANDLER::TriggerExtractionAsync( const std::string& aPartNumber,
                                                 const std::string& aManufacturer,
                                                 const std::string& aDatasheetUrl )
{
    // Deduplicate across calls: skip if already triggered this session
    {
        std::lock_guard<std::mutex> lock( s_triggeredMutex );

        if( s_triggeredParts.count( aPartNumber ) )
        {
            wxLogInfo( "DATASHEET_HANDLER: Skipping duplicate extraction trigger for %s",
                       aPartNumber.c_str() );
            return;
        }

        s_triggeredParts.insert( aPartNumber );
    }

    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return;

    std::string accessToken = auth->GetAccessToken();

    if( accessToken.empty() )
        return;

    // Capture values for the background thread
    std::string url = supabaseUrl + "/functions/v1/extract-datasheet";
    std::string token = accessToken;
    std::string apiKey = anonKey;
    std::string partNumber = aPartNumber;
    std::string manufacturer = aManufacturer;
    std::string datasheetUrl = aDatasheetUrl;

    std::thread( [url, token, apiKey, partNumber, manufacturer, datasheetUrl]()
    {
        json body;
        body["part_number"] = partNumber;
        body["datasheet_url"] = datasheetUrl;

        if( !manufacturer.empty() )
            body["manufacturer"] = manufacturer;

        try
        {
            KICAD_CURL_EASY curl;
            curl.SetURL( url );
            curl.SetFollowRedirects( true );
            curl.SetHeader( "Authorization", "Bearer " + token );
            curl.SetHeader( "apikey", apiKey );
            curl.SetHeader( "Content-Type", "application/json" );
            curl.SetPostFields( body.dump() );
            curl.Perform();

            long httpCode = curl.GetResponseStatusCode();
            wxLogTrace( "Agent", "DATASHEET_HANDLER: Extraction trigger HTTP %ld for %s: %s",
                        httpCode, partNumber.c_str(), curl.GetBuffer().c_str() );
        }
        catch( const std::exception& e )
        {
            wxLogTrace( "Agent", "DATASHEET_HANDLER: Extraction trigger failed for %s: %s",
                        partNumber.c_str(), e.what() );
        }
    } ).detach();
}
