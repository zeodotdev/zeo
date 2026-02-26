#include "component_search_handler.h"
#include "../../agent_llm_client.h"
#include "../../agent_events.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <nlohmann/json.hpp>
#include <wx/app.h>
#include <wx/log.h>
#include <curl/curl.h>
#include <set>
#include <thread>

using json = nlohmann::json;

static const char* MCP_ENDPOINT = "https://pcbparts.dev/mcp";


COMPONENT_SEARCH_HANDLER::COMPONENT_SEARCH_HANDLER()
{
    // Fire background fetch of tool schemas from MCP server
    std::thread( [this]() { FetchMcpSchemas(); } ).detach();
}


void COMPONENT_SEARCH_HANDLER::FetchMcpSchemas()
{
    wxLogInfo( "COMPONENT_SEARCH: Fetching tool schemas from %s", MCP_ENDPOINT );

    try
    {
        json request;
        request["jsonrpc"] = "2.0";
        request["method"]  = "tools/list";
        request["id"]      = 1;

        KICAD_CURL_EASY curl;
        curl.SetURL( MCP_ENDPOINT );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetHeader( "Accept", "application/json, text/event-stream" );
        curl.SetPostFields( request.dump() );
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 15L );

        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode < 200 || httpCode >= 300 )
        {
            wxLogTrace( "Agent", "COMPONENT_SEARCH: tools/list HTTP %ld", httpCode );
            return;
        }

        // Parse SSE or plain JSON response
        std::string body = curl.GetBuffer();
        std::string jsonStr;
        size_t dataPos = body.rfind( "data: " );

        if( dataPos != std::string::npos )
        {
            jsonStr = body.substr( dataPos + 6 );

            while( !jsonStr.empty()
                   && ( jsonStr.back() == '\n' || jsonStr.back() == '\r'
                        || jsonStr.back() == ' ' ) )
            {
                jsonStr.pop_back();
            }
        }
        else
        {
            jsonStr = body;
        }

        auto response = json::parse( jsonStr );

        if( !response.contains( "result" ) || !response["result"].contains( "tools" ) )
        {
            wxLogTrace( "Agent", "COMPONENT_SEARCH: tools/list missing result.tools" );
            return;
        }

        // Build set of tool names we handle (for filtering)
        auto handledNames = GetToolNames();
        std::set<std::string> handled( handledNames.begin(), handledNames.end() );

        std::vector<LLM_TOOL> tools;

        for( const auto& mcpTool : response["result"]["tools"] )
        {
            std::string name = mcpTool.value( "name", "" );

            if( !handled.count( name ) )
                continue;

            LLM_TOOL tool;
            tool.name = name;
            tool.description = mcpTool.value( "description", "" );
            tool.defer_loading = true;

            // MCP uses "inputSchema" (camelCase), Anthropic API expects "input_schema"
            if( mcpTool.contains( "inputSchema" ) )
                tool.input_schema = mcpTool["inputSchema"];
            else
                tool.input_schema = { { "type", "object" }, { "properties", json::object() } };

            tools.push_back( std::move( tool ) );
        }

        {
            std::lock_guard<std::mutex> lock( m_mcpMutex );
            m_mcpTools = std::move( tools );
            m_mcpFetched = true;
        }

        wxLogInfo( "COMPONENT_SEARCH: Fetched %zu tool schemas from MCP",
                   m_mcpTools.size() );
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "COMPONENT_SEARCH: tools/list failed: %s", e.what() );
    }
}


std::vector<LLM_TOOL> COMPONENT_SEARCH_HANDLER::GetDynamicTools() const
{
    std::lock_guard<std::mutex> lock( m_mcpMutex );
    return m_mcpTools;
}


std::string COMPONENT_SEARCH_HANDLER::Execute( const std::string& aToolName,
                                                const nlohmann::json& aInput )
{
    json args = aInput;

    // Inject default limit for search/list tools if not provided
    static const std::set<std::string> limitTools = {
        "jlc_search", "jlc_find_alternatives",
        "mouser_search", "digikey_search", "cse_search"
    };

    if( limitTools.count( aToolName ) && !args.contains( "limit" ) )
        args["limit"] = 5;

    return CallMCP( aToolName, args );
}


std::string COMPONENT_SEARCH_HANDLER::GetDescription( const std::string& aToolName,
                                                       const nlohmann::json& aInput ) const
{
    // Extract a human-readable label from the tool name
    std::string label = aToolName;

    // Map tool prefixes to supplier labels
    if( aToolName.rfind( "jlc_", 0 ) == 0 )
        label = "JLCPCB";
    else if( aToolName.rfind( "mouser_", 0 ) == 0 )
        label = "Mouser";
    else if( aToolName.rfind( "digikey_", 0 ) == 0 )
        label = "DigiKey";
    else if( aToolName.rfind( "cse_", 0 ) == 0 )
        label = "PCBParts";

    // Try to extract a query/keyword for context
    std::string query;

    for( const auto& key : { "query", "keyword", "keywords", "lcsc", "mpn",
                              "part_number", "product_number" } )
    {
        if( aInput.contains( key ) && aInput[key].is_string() )
        {
            query = aInput[key].get<std::string>();
            break;
        }
    }

    if( aToolName.find( "search" ) != std::string::npos && !query.empty() )
        return "Searching " + label + " for " + query;
    else if( aToolName.find( "get_part" ) != std::string::npos && !query.empty() )
        return "Getting " + label + " part: " + query;
    else if( aToolName.find( "alternatives" ) != std::string::npos )
        return "Finding alternatives on " + label;
    else if( aToolName.find( "validate_bom" ) != std::string::npos )
        return "Validating BOM on " + label;
    else if( aToolName.find( "pinout" ) != std::string::npos )
        return "Getting pinout data";
    else if( aToolName.find( "get_kicad" ) != std::string::npos )
        return "Getting KiCad symbol/footprint";
    else if( aToolName.find( "list_attributes" ) != std::string::npos )
        return "Listing component attributes";
    else if( aToolName.find( "export_bom" ) != std::string::npos )
        return "Exporting BOM";

    return "Component search: " + aToolName;
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
