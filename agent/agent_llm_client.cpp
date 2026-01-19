#include "agent_llm_client.h"
#include "agent_frame.h"
#include <id.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <ki_exception.h>
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <sstream>
#include <fstream>

using json = nlohmann::json;

// Static member definitions
std::string AGENT_LLM_CLIENT::s_openaiApiKey;
std::string AGENT_LLM_CLIENT::s_anthropicApiKey;
bool        AGENT_LLM_CLIENT::s_keysLoaded = false;

// Helper to trim whitespace from string
static std::string trimString( const std::string& str )
{
    size_t start = str.find_first_not_of( " \t\r\n" );
    if( start == std::string::npos )
        return "";
    size_t end = str.find_last_not_of( " \t\r\n" );
    return str.substr( start, end - start + 1 );
}

bool AGENT_LLM_CLIENT::LoadApiKeys( const std::string& aEnvFilePath )
{
    if( s_keysLoaded )
        return true;

    std::vector<std::string> searchPaths;

    // If explicit path provided, try it first
    if( !aEnvFilePath.empty() )
    {
        searchPaths.push_back( aEnvFilePath );
    }

    // Standard search locations for .env file
    // 1. Environment variable pointing to dev folder
    const char* devPath = std::getenv( "KICAD_AGENT_DEV_PATH" );
    if( devPath )
    {
        searchPaths.push_back( std::string( devPath ) + "/.env" );
    }

    // 2. User's home directory dev folder
    wxString homeDir = wxGetHomeDir();
    searchPaths.push_back( std::string( homeDir.mb_str() ) + "/workspaces/KiCAD_agentic/dev/.env" );

    // 3. Current working directory
    searchPaths.push_back( ".env" );

    // 4. KiCad config directory
    wxString configDir = wxStandardPaths::Get().GetUserConfigDir();
    searchPaths.push_back( std::string( configDir.mb_str() ) + "/kicad/.env" );

    // Try each path
    for( const auto& path : searchPaths )
    {
        std::ifstream file( path );
        if( !file.is_open() )
            continue;

        // Keys found at this path

        std::string line;
        while( std::getline( file, line ) )
        {
            // Skip empty lines and comments
            line = trimString( line );
            if( line.empty() || line[0] == '#' )
                continue;

            // Parse KEY=VALUE
            size_t eqPos = line.find( '=' );
            if( eqPos == std::string::npos )
                continue;

            std::string key = trimString( line.substr( 0, eqPos ) );
            std::string value = trimString( line.substr( eqPos + 1 ) );

            // Remove quotes if present
            if( value.length() >= 2 )
            {
                if( ( value.front() == '"' && value.back() == '"' ) ||
                    ( value.front() == '\'' && value.back() == '\'' ) )
                {
                    value = value.substr( 1, value.length() - 2 );
                }
            }

            if( key == "OPENAI_API_KEY" )
            {
                s_openaiApiKey = value;
            }
            else if( key == "ANTHROPIC_API_KEY" )
            {
                s_anthropicApiKey = value;
            }
        }

        file.close();

        // Check if we got at least one key
        if( !s_openaiApiKey.empty() || !s_anthropicApiKey.empty() )
        {
            s_keysLoaded = true;
            return true;
        }
    }

    // Also check environment variables as fallback
    const char* openaiEnv = std::getenv( "OPENAI_API_KEY" );
    const char* anthropicEnv = std::getenv( "ANTHROPIC_API_KEY" );

    if( openaiEnv )
        s_openaiApiKey = openaiEnv;
    if( anthropicEnv )
        s_anthropicApiKey = anthropicEnv;

    if( !s_openaiApiKey.empty() || !s_anthropicApiKey.empty() )
    {
        s_keysLoaded = true;
        return true;
    }

    // No keys found - will fail when API calls are made
    return false;
}

AGENT_LLM_CLIENT::AGENT_LLM_CLIENT( AGENT_FRAME* aParent ) :
        m_parent( aParent ),
        m_modelName( "GPT-4o" )
{
    // Ensure API keys are loaded
    LoadApiKeys();
}

AGENT_LLM_CLIENT::~AGENT_LLM_CLIENT()
{
}

void AGENT_LLM_CLIENT::SetModel( const std::string& aModelName )
{
    m_modelName = aModelName;
}

std::string AGENT_LLM_CLIENT::Ask( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload )
{
    return "Error: Synchronous Ask not fully implemented for multi-model. Use AskStream.";
}

// Context for stream callback
struct StreamContext
{
    std::function<void( const std::string& )> callback;
    std::string                               buffer;       // For accumulating data
    std::string                               fullResponse; // For error reporting
    bool                                      isAnthropic;  // Flag for parser
};

// Context for tool-aware streaming callback
struct ToolStreamContext
{
    std::function<void( const LLM_EVENT& )> callback;
    std::string                             buffer;           // For accumulating SSE data
    std::string                             fullResponse;     // For error reporting

    // Current tool use being accumulated
    bool                                    inToolUse = false;
    std::string                             currentToolId;
    std::string                             currentToolName;
    std::string                             currentToolInputJson;  // Partial JSON accumulator

    // Track all tool calls in this response
    std::vector<LLM_EVENT>                  pendingToolCalls;
};

static size_t StreamWriteCallback( void* aContents, size_t aSize, size_t aNmemb, void* aUserp )
{
    size_t         realSize = aSize * aNmemb;
    StreamContext* ctx = static_cast<StreamContext*>( aUserp );

    // Append new data
    ctx->buffer.append( static_cast<const char*>( aContents ), realSize );
    ctx->fullResponse.append( static_cast<const char*>( aContents ), realSize );

    // Process complete lines
    size_t lastNewline = ctx->buffer.find_last_of( "\n" );
    if( lastNewline == std::string::npos )
        return realSize;

    std::string processChunk = ctx->buffer.substr( 0, lastNewline + 1 );
    ctx->buffer = ctx->buffer.substr( lastNewline + 1 );

    std::stringstream lineStream( processChunk );
    std::string       line;
    while( std::getline( lineStream, line ) )
    {
        if( !line.empty() && line.back() == '\r' )
            line.pop_back();

        if( ctx->isAnthropic )
        {
            // Anthropic SSE Format: event: ..., data: JSON
            if( line.rfind( "data: ", 0 ) == 0 )
            {
                std::string payload = line.substr( 6 );
                try
                {
                    auto j = json::parse( payload );
                    if( j.contains( "type" ) )
                    {
                        std::string type = j["type"];
                        if( type == "content_block_delta" && j.contains( "delta" ) && j["delta"].contains( "text" ) )
                        {
                            std::string content = j["delta"]["text"];
                            if( ctx->callback )
                                ctx->callback( content );
                        }
                    }
                }
                catch( ... )
                {
                }
            }
        }
        else // OpenAI
        {
            if( line.rfind( "data: ", 0 ) == 0 )
            {
                std::string payload = line.substr( 6 );
                if( payload == "[DONE]" )
                    continue;
                try
                {
                    auto j = json::parse( payload );
                    if( j.contains( "choices" ) && !j["choices"].empty() && j["choices"][0].contains( "delta" )
                        && j["choices"][0]["delta"].contains( "content" ) )
                    {
                        std::string content = j["choices"][0]["delta"]["content"].get<std::string>();
                        if( ctx->callback )
                            ctx->callback( content );
                    }
                }
                catch( ... )
                {
                }
            }
        }
    }

    return realSize;
}

// Streaming callback for tool-aware Anthropic responses
static size_t ToolStreamWriteCallback( void* aContents, size_t aSize, size_t aNmemb, void* aUserp )
{
    size_t            realSize = aSize * aNmemb;
    ToolStreamContext* ctx = static_cast<ToolStreamContext*>( aUserp );

    // Append new data
    ctx->buffer.append( static_cast<const char*>( aContents ), realSize );
    ctx->fullResponse.append( static_cast<const char*>( aContents ), realSize );

    // Process complete lines
    size_t lastNewline = ctx->buffer.find_last_of( "\n" );
    if( lastNewline == std::string::npos )
        return realSize;

    std::string processChunk = ctx->buffer.substr( 0, lastNewline + 1 );
    ctx->buffer = ctx->buffer.substr( lastNewline + 1 );

    std::stringstream lineStream( processChunk );
    std::string       line;
    while( std::getline( lineStream, line ) )
    {
        if( !line.empty() && line.back() == '\r' )
            line.pop_back();

        // Anthropic SSE Format: event: ..., data: JSON
        if( line.rfind( "data: ", 0 ) != 0 )
            continue;

        std::string payload = line.substr( 6 );
        try
        {
            auto j = json::parse( payload );
            if( !j.contains( "type" ) )
                continue;

            std::string type = j["type"];

            // Handle text content deltas
            if( type == "content_block_delta" && j.contains( "delta" ) )
            {
                auto& delta = j["delta"];

                // Text delta
                if( delta.contains( "type" ) && delta["type"] == "text_delta" && delta.contains( "text" ) )
                {
                    LLM_EVENT event;
                    event.type = LLM_EVENT_TYPE::TEXT;
                    event.text = delta["text"];
                    if( ctx->callback )
                        ctx->callback( event );
                }
                // Tool input JSON delta (partial JSON accumulation)
                else if( delta.contains( "type" ) && delta["type"] == "input_json_delta" && delta.contains( "partial_json" ) )
                {
                    ctx->currentToolInputJson += delta["partial_json"].get<std::string>();
                }
            }
            // Content block start - check for tool_use
            else if( type == "content_block_start" && j.contains( "content_block" ) )
            {
                auto& block = j["content_block"];
                if( block.contains( "type" ) && block["type"] == "tool_use" )
                {
                    ctx->inToolUse = true;
                    ctx->currentToolId = block.value( "id", "" );
                    ctx->currentToolName = block.value( "name", "" );
                    ctx->currentToolInputJson.clear();
                }
            }
            // Content block stop - finalize tool call if we were in one
            else if( type == "content_block_stop" )
            {
                if( ctx->inToolUse )
                {
                    // Parse the accumulated JSON input
                    LLM_EVENT toolEvent;
                    toolEvent.type = LLM_EVENT_TYPE::TOOL_USE;
                    toolEvent.tool_use_id = ctx->currentToolId;
                    toolEvent.tool_name = ctx->currentToolName;

                    try
                    {
                        if( !ctx->currentToolInputJson.empty() )
                            toolEvent.tool_input = json::parse( ctx->currentToolInputJson );
                        else
                            toolEvent.tool_input = json::object();
                    }
                    catch( ... )
                    {
                        toolEvent.tool_input = json::object();
                    }

                    // Emit the tool use event
                    if( ctx->callback )
                        ctx->callback( toolEvent );

                    // Add to pending calls
                    ctx->pendingToolCalls.push_back( toolEvent );

                    // Reset state
                    ctx->inToolUse = false;
                    ctx->currentToolId.clear();
                    ctx->currentToolName.clear();
                    ctx->currentToolInputJson.clear();
                }
            }
            // Message delta - check for stop reason
            else if( type == "message_delta" && j.contains( "delta" ) )
            {
                auto& delta = j["delta"];
                if( delta.contains( "stop_reason" ) )
                {
                    std::string stopReason = delta["stop_reason"];
                    if( stopReason == "tool_use" )
                    {
                        // Signal that tool calls are ready to execute
                        LLM_EVENT doneEvent;
                        doneEvent.type = LLM_EVENT_TYPE::TOOL_USE_DONE;
                        if( ctx->callback )
                            ctx->callback( doneEvent );
                    }
                    else if( stopReason == "end_turn" )
                    {
                        // Model finished, no more tool calls
                        LLM_EVENT endEvent;
                        endEvent.type = LLM_EVENT_TYPE::END_TURN;
                        if( ctx->callback )
                            ctx->callback( endEvent );
                    }
                }
            }
            // Message stop - final signal
            else if( type == "message_stop" )
            {
                // If we haven't sent END_TURN yet, send it now
                // (This handles cases where stop_reason wasn't in message_delta)
            }
        }
        catch( ... )
        {
            // JSON parse error - skip this line
        }
    }

    return realSize;
}

bool AGENT_LLM_CLIENT::AskStream( const nlohmann::json& aMessages, const std::string& aSystem,
                                  const std::string& aPayload, std::function<void( const std::string& )> aCallback )
{
    // Route based on model name
    if( m_modelName.find( "Claude" ) != std::string::npos )
    {
        return AskStreamAnthropic( aMessages, aSystem, aPayload, aCallback );
    }
    else
    {
        return AskStreamOpenAI( aMessages, aSystem, aPayload, aCallback );
    }
}

bool AGENT_LLM_CLIENT::AskStreamOpenAI( const nlohmann::json& aMessages, const std::string& aSystem,
                                        const std::string&                        aPayload,
                                        std::function<void( const std::string& )> aCallback )
{
    KICAD_CURL_EASY curl;
    curl.SetURL( "https://api.openai.com/v1/chat/completions" );
    curl.SetHeader( "Content-Type", "application/json" );
    curl.SetHeader( "Authorization", "Bearer " + s_openaiApiKey );

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;

    json requestBody;
    requestBody["model"] = "gpt-4o"; // Hardcoded default for OpenAI path

    // Prepend system message to history
    json fullMessages = json::array();
    fullMessages.push_back( { { "role", "system" }, { "content", fullSystemPrompt } } );
    for( const auto& msg : aMessages )
    {
        fullMessages.push_back( msg );
    }

    requestBody["messages"] = fullMessages;
    requestBody["stream"] = true;

    // TODO: Add Tools if maintaining OpenAI tool support

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    StreamContext ctx;
    ctx.callback = aCallback;
    ctx.isAnthropic = false;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        curl.Perform();
        long http_code = curl.GetResponseStatusCode();
        if( http_code != 200 )
        {
            if( aCallback )
                aCallback( "Error: OpenAI API " + std::to_string( http_code ) + "\n" + ctx.fullResponse );
            return false;
        }
        return true;
    }
    catch( const std::exception& e )
    {
        if( aCallback )
            aCallback( "Error: " + std::string( e.what() ) );
        return false;
    }
}

bool AGENT_LLM_CLIENT::AskStreamAnthropic( const nlohmann::json& aMessages, const std::string& aSystem,
                                           const std::string&                        aPayload,
                                           std::function<void( const std::string& )> aCallback )
{
    KICAD_CURL_EASY curl;
    curl.SetURL( "https://api.anthropic.com/v1/messages" );
    curl.SetHeader( "content-type", "application/json" );
    curl.SetHeader( "x-api-key", s_anthropicApiKey );
    curl.SetHeader( "anthropic-version", "2023-06-01" );

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;

    // Map UI names to API names
    std::string apiModel = "claude-3-5-sonnet-20240620";
    if( m_modelName == "Claude 4.5 Opus" )
        apiModel = "claude-opus-4-5-20251101";

    // wxLogMessage( "Anthropic Request: Model=%s", apiModel.c_str() ); // Removed to prevent pop-ups

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["system"] = fullSystemPrompt;
    requestBody["messages"] = aMessages;
    requestBody["max_tokens"] = 4096;
    requestBody["stream"] = true;

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    StreamContext ctx;
    ctx.callback = aCallback;
    ctx.isAnthropic = true;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        curl.Perform();
        long http_code = curl.GetResponseStatusCode();
        if( http_code != 200 )
        {
            if( aCallback )
                aCallback( "Error: Anthropic API " + std::to_string( http_code ) + "\n" + ctx.fullResponse );
            return false;
        }
        return true;
    }
    catch( const std::exception& e )
    {
        if( aCallback )
            aCallback( "Error: " + std::string( e.what() ) );
        return false;
    }
}

bool AGENT_LLM_CLIENT::AskStreamWithTools( const nlohmann::json& aMessages, const std::string& aSystem,
                                           const std::vector<LLM_TOOL>& aTools,
                                           std::function<void( const LLM_EVENT& )> aCallback )
{
    // Currently only implemented for Anthropic
    if( m_modelName.find( "Claude" ) != std::string::npos )
    {
        return AskStreamAnthropicWithTools( aMessages, aSystem, aTools, aCallback );
    }
    else
    {
        // OpenAI tool calling not yet implemented
        LLM_EVENT errorEvent;
        errorEvent.type = LLM_EVENT_TYPE::ERROR;
        errorEvent.error_message = "Tool calling not yet implemented for OpenAI models";
        if( aCallback )
            aCallback( errorEvent );
        return false;
    }
}

bool AGENT_LLM_CLIENT::AskStreamAnthropicWithTools( const nlohmann::json& aMessages, const std::string& aSystem,
                                                    const std::vector<LLM_TOOL>& aTools,
                                                    std::function<void( const LLM_EVENT& )> aCallback )
{
    KICAD_CURL_EASY curl;
    curl.SetURL( "https://api.anthropic.com/v1/messages" );
    curl.SetHeader( "content-type", "application/json" );
    curl.SetHeader( "x-api-key", s_anthropicApiKey );
    curl.SetHeader( "anthropic-version", "2023-06-01" );

    // Map UI names to API names
    std::string apiModel = "claude-3-5-sonnet-20240620";
    if( m_modelName == "Claude 4.5 Opus" )
        apiModel = "claude-opus-4-5-20251101";

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["system"] = aSystem;
    requestBody["messages"] = aMessages;
    requestBody["max_tokens"] = 4096;
    requestBody["stream"] = true;

    // Add tools array
    if( !aTools.empty() )
    {
        requestBody["tools"] = json::array();
        for( const auto& tool : aTools )
        {
            json toolObj;
            toolObj["name"] = tool.name;
            toolObj["description"] = tool.description;
            toolObj["input_schema"] = tool.input_schema;
            requestBody["tools"].push_back( toolObj );
        }
    }

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    ToolStreamContext ctx;
    ctx.callback = aCallback;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, ToolStreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        curl.Perform();
        long http_code = curl.GetResponseStatusCode();
        if( http_code != 200 )
        {
            LLM_EVENT errorEvent;
            errorEvent.type = LLM_EVENT_TYPE::ERROR;
            errorEvent.error_message = "Anthropic API error " + std::to_string( http_code ) + ": " + ctx.fullResponse;
            if( aCallback )
                aCallback( errorEvent );
            return false;
        }
        return true;
    }
    catch( const std::exception& e )
    {
        LLM_EVENT errorEvent;
        errorEvent.type = LLM_EVENT_TYPE::ERROR;
        errorEvent.error_message = std::string( e.what() );
        if( aCallback )
            aCallback( errorEvent );
        return false;
    }
}
