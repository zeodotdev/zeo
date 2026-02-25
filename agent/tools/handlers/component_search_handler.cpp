#include "component_search_handler.h"
#include "../../agent_events.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <nlohmann/json.hpp>
#include <wx/app.h>
#include <wx/log.h>
#include <curl/curl.h>
#include <thread>

using json = nlohmann::json;

static const char* MCP_ENDPOINT = "https://pcbparts.dev/mcp";


std::string COMPONENT_SEARCH_HANDLER::Execute( const std::string& aToolName,
                                                const nlohmann::json& aInput )
{
    std::string action   = aInput.value( "action", "" );
    std::string supplier = aInput.value( "supplier", "jlcpcb" );
    std::string query    = aInput.value( "query", "" );

    if( action.empty() )
        return R"({"error": "action is required"})";

    std::string mcpTool = MapToMCPTool( action, supplier );

    if( mcpTool.empty() )
    {
        return R"({"error": "Unsupported action/supplier combination: )"
               + action + "/" + supplier + R"("})";
    }

    // Build the MCP arguments: start with params (pass-through), then merge query
    json args;

    if( aInput.contains( "params" ) && aInput["params"].is_object() )
        args = aInput["params"];

    // Map query to the correct parameter name for each MCP tool
    if( !query.empty() )
    {
        if( mcpTool == "mouser_search" )
            args["keyword"] = query;
        else if( mcpTool == "mouser_get_part" )
            args["part_number"] = query;
        else if( mcpTool == "digikey_search" )
            args["keywords"] = query;
        else if( mcpTool == "digikey_get_part" )
            args["product_number"] = query;
        else if( mcpTool == "jlc_get_part" )
        {
            // LCSC numbers start with 'C' followed by digits; everything else is MPN
            if( !query.empty() && query[0] == 'C'
                && query.find_first_not_of( "0123456789", 1 ) == std::string::npos )
                args["lcsc"] = query;
            else
                args["mpn"] = query;
        }
        else if( mcpTool == "jlc_find_alternatives" )
            args["lcsc"] = query;
        else if( mcpTool == "jlc_get_pinout" )
            args["lcsc"] = query;
        else
            args["query"] = query;
    }

    // Only inject limit for tools that support it (search/list tools)
    bool supportsLimit = ( mcpTool == "jlc_search" || mcpTool == "jlc_search_api"
                           || mcpTool == "jlc_find_alternatives"
                           || mcpTool == "mouser_search" || mcpTool == "digikey_search"
                           || mcpTool == "cse_search" );

    if( supportsLimit )
    {
        if( aInput.contains( "limit" ) && aInput["limit"].is_number_integer() )
            args["limit"] = aInput["limit"];
        else if( !args.contains( "limit" ) )
            args["limit"] = 5;
    }

    return CallMCP( mcpTool, args );
}


std::string COMPONENT_SEARCH_HANDLER::GetDescription( const std::string& aToolName,
                                                       const nlohmann::json& aInput ) const
{
    std::string action   = aInput.value( "action", "search" );
    std::string supplier = aInput.value( "supplier", "jlcpcb" );
    std::string query    = aInput.value( "query", "" );

    // Capitalize supplier for display
    std::string supplierLabel = supplier;

    if( supplier == "jlcpcb" )       supplierLabel = "JLCPCB";
    else if( supplier == "mouser" )  supplierLabel = "Mouser";
    else if( supplier == "digikey" ) supplierLabel = "DigiKey";

    if( action == "search" && !query.empty() )
        return "Searching " + supplierLabel + " for " + query;
    else if( action == "get_part" && !query.empty() )
        return "Getting part details for " + query;
    else if( action == "find_alternatives" )
        return "Finding alternatives on " + supplierLabel;
    else if( action == "validate_bom" )
        return "Validating BOM on " + supplierLabel;
    else if( action == "get_pinout" )
        return "Getting pinout data";
    else if( action == "get_kicad" )
        return "Getting KiCad symbol/footprint";
    else if( action == "list_categories" )
        return "Listing component categories";
    else if( action == "export_bom" )
        return "Exporting BOM";

    return "Component search: " + action;
}


void COMPONENT_SEARCH_HANDLER::ExecuteAsync( const std::string& aToolName,
                                              const nlohmann::json& aInput,
                                              const std::string& aToolUseId,
                                              wxEvtHandler* aEventHandler )
{
    // Capture inputs by value for the background thread
    nlohmann::json input = aInput;
    std::string toolName = aToolName;
    std::string toolUseId = aToolUseId;

    std::thread( [=, this]()
    {
        std::string execResult = Execute( toolName, input );

        bool success = true;

        try
        {
            auto resultJson = json::parse( execResult );
            if( resultJson.contains( "error" ) )
                success = false;
        }
        catch( ... )
        {
            // Not JSON or parse failed — treat as success (raw text result)
        }

        wxTheApp->CallAfter( [=]()
        {
            ToolExecutionResult result;
            result.tool_use_id = toolUseId;
            result.tool_name = toolName;
            result.result = execResult;
            result.success = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


std::string COMPONENT_SEARCH_HANDLER::MapToMCPTool( const std::string& aAction,
                                                     const std::string& aSupplier ) const
{
    if( aAction == "search" )
    {
        if( aSupplier == "jlcpcb" )  return "jlc_search";
        if( aSupplier == "mouser" )  return "mouser_search";
        if( aSupplier == "digikey" ) return "digikey_search";
    }
    else if( aAction == "get_part" )
    {
        if( aSupplier == "jlcpcb" )  return "jlc_get_part";
        if( aSupplier == "mouser" )  return "mouser_get_part";
        if( aSupplier == "digikey" ) return "digikey_get_part";
    }
    else if( aAction == "find_alternatives" && aSupplier == "jlcpcb" )
    {
        return "jlc_find_alternatives";
    }
    else if( aAction == "validate_bom" && aSupplier == "jlcpcb" )
    {
        return "jlc_validate_bom";
    }
    else if( aAction == "get_pinout" && aSupplier == "jlcpcb" )
    {
        return "jlc_get_pinout";
    }
    else if( aAction == "get_kicad" )
    {
        return "cse_get_kicad";
    }
    else if( aAction == "list_categories" && aSupplier == "jlcpcb" )
    {
        return "jlc_list_categories";
    }
    else if( aAction == "export_bom" && aSupplier == "jlcpcb" )
    {
        return "jlc_export_bom";
    }

    return "";
}


std::string COMPONENT_SEARCH_HANDLER::CallMCP( const std::string& aMcpTool,
                                                const nlohmann::json& aArgs )
{
    // Build JSON-RPC 2.0 request
    json request;
    request["jsonrpc"] = "2.0";
    request["method"]  = "tools/call";
    request["params"]  = { { "name", aMcpTool }, { "arguments", aArgs } };
    request["id"]      = 1;

    wxLogInfo( "COMPONENT_SEARCH: POST %s tool=%s", MCP_ENDPOINT, aMcpTool.c_str() );

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( MCP_ENDPOINT );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetHeader( "Accept", "application/json, text/event-stream" );
        curl.SetPostFields( request.dump() );

        // Set 30s total timeout via raw CURL handle
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 30L );

        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode < 200 || httpCode >= 300 )
        {
            wxLogTrace( "Agent", "COMPONENT_SEARCH: HTTP %ld: %s",
                         httpCode, curl.GetBuffer().c_str() );
            return R"({"error": "HTTP )" + std::to_string( httpCode )
                   + R"(", "details": "MCP server returned an error"})";
        }

        // Parse SSE response: strip "event: message\ndata: " prefix
        std::string body = curl.GetBuffer();
        std::string jsonStr;

        // Find the last "data: " line (SSE may have multiple events)
        size_t dataPos = body.rfind( "data: " );

        if( dataPos != std::string::npos )
        {
            jsonStr = body.substr( dataPos + 6 );

            // Trim trailing whitespace/newlines
            while( !jsonStr.empty()
                   && ( jsonStr.back() == '\n' || jsonStr.back() == '\r'
                        || jsonStr.back() == ' ' ) )
            {
                jsonStr.pop_back();
            }
        }
        else
        {
            // Response might be plain JSON (not SSE)
            jsonStr = body;
        }

        auto response = json::parse( jsonStr );

        // Check for MCP-level error
        if( response.contains( "error" ) )
        {
            wxLogTrace( "Agent", "COMPONENT_SEARCH: MCP error: %s",
                         response["error"].dump().c_str() );
            return response["error"].dump( 2 );
        }

        // Extract result.content[0].text
        if( response.contains( "result" ) )
        {
            const auto& result = response["result"];

            if( result.value( "isError", false ) )
            {
                wxLogTrace( "Agent", "COMPONENT_SEARCH: MCP tool error" );

                if( result.contains( "content" ) && !result["content"].empty() )
                    return R"({"error": )" + result["content"][0].value( "text", "MCP tool error" ) + "}";

                return R"({"error": "MCP tool returned an error"})";
            }

            if( result.contains( "content" ) && !result["content"].empty() )
            {
                std::string text = result["content"][0].value( "text", "" );

                // The text field is often a JSON string itself — try to parse it
                // so the LLM gets structured data
                try
                {
                    auto parsed = json::parse( text );
                    return parsed.dump( 2 );
                }
                catch( ... )
                {
                    return text;
                }
            }
        }

        // Fallback: return the whole response
        return response.dump( 2 );
    }
    catch( const json::parse_error& e )
    {
        wxLogTrace( "Agent", "COMPONENT_SEARCH: JSON parse error: %s", e.what() );
        return R"({"error": "Failed to parse MCP response"})";
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "COMPONENT_SEARCH: Request failed: %s", e.what() );
        return R"({"error": "Request to PCBParts MCP server failed"})";
    }
}
