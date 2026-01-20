#include "agent_llm_client.h"
#include "agent_frame.h"
#include "agent_events.h"
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
    // 1. Environment variable pointing to custom location
    const char* devPath = std::getenv( "KICAD_AGENT_ENV_PATH" );
    if( devPath )
    {
        searchPaths.push_back( std::string( devPath ) );
    }

    // 2. Relative to executable - search parent directories for various patterns
    //    Primary location: .../code/kicad-agent/agent/.env
    wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
    wxString exeDir = exePath.GetPath();

    fprintf( stderr, "[ENV] Executable path: %s\n", exeDir.ToStdString().c_str() );

    wxFileName searchPath( exeDir, "" );
    for( int i = 0; i < 8; i++ )  // Search up to 8 levels up
    {
        // Pattern 1: agent/.env (if we're in kicad-agent directory)
        wxFileName agentEnvPath = searchPath;
        agentEnvPath.AppendDir( "agent" );
        agentEnvPath.SetFullName( ".env" );
        searchPaths.push_back( std::string( agentEnvPath.GetFullPath().mb_str() ) );

        // Pattern 2: kicad-agent/agent/.env
        wxFileName kicadAgentEnvPath = searchPath;
        kicadAgentEnvPath.AppendDir( "kicad-agent" );
        kicadAgentEnvPath.AppendDir( "agent" );
        kicadAgentEnvPath.SetFullName( ".env" );
        searchPaths.push_back( std::string( kicadAgentEnvPath.GetFullPath().mb_str() ) );

        // Pattern 3: code/kicad-agent/agent/.env (full path from workspace root)
        wxFileName codeKicadAgentEnvPath = searchPath;
        codeKicadAgentEnvPath.AppendDir( "code" );
        codeKicadAgentEnvPath.AppendDir( "kicad-agent" );
        codeKicadAgentEnvPath.AppendDir( "agent" );
        codeKicadAgentEnvPath.SetFullName( ".env" );
        searchPaths.push_back( std::string( codeKicadAgentEnvPath.GetFullPath().mb_str() ) );

        searchPath.RemoveLastDir();
    }

    // 3. Current working directory
    searchPaths.push_back( ".env" );

    // 4. KiCad config directory
    wxString configDir = wxStandardPaths::Get().GetUserConfigDir();
    searchPaths.push_back( std::string( configDir.mb_str() ) + "/kicad-agent/.env" );

    // 5. User's home directory .config/kicad-agent/.env
    wxString homeDir = wxGetHomeDir();
    searchPaths.push_back( std::string( homeDir.mb_str() ) + "/.config/kicad-agent/.env" );

    // Try each path
    fprintf( stderr, "[ENV] Searching for .env file in %zu locations...\n", searchPaths.size() );

    for( const auto& path : searchPaths )
    {
        std::ifstream file( path );
        if( !file.is_open() )
        {
            // Only log first few paths to avoid spam
            static int logCount = 0;
            if( logCount++ < 5 )
                fprintf( stderr, "[ENV]   Not found: %s\n", path.c_str() );
            continue;
        }

        fprintf( stderr, "[ENV]   FOUND: %s\n", path.c_str() );

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
            fprintf( stderr, "[ENV] API keys loaded successfully!\n" );
            fprintf( stderr, "[ENV]   Anthropic key: %s\n",
                     s_anthropicApiKey.empty() ? "(not set)" : "(set)" );
            fprintf( stderr, "[ENV]   OpenAI key: %s\n",
                     s_openaiApiKey.empty() ? "(not set)" : "(set)" );
            s_keysLoaded = true;
            return true;
        }
        else
        {
            fprintf( stderr, "[ENV]   File found but no valid keys parsed!\n" );
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
        fprintf( stderr, "[ENV] API keys loaded from environment variables\n" );
        s_keysLoaded = true;
        return true;
    }

    // No keys found - will fail when API calls are made
    fprintf( stderr, "[ENV] ERROR: No API keys found in any location!\n" );
    fprintf( stderr, "[ENV] Please create a .env file at: code/kicad-agent/agent/.env\n" );
    fprintf( stderr, "[ENV] Or set KICAD_AGENT_ENV_PATH environment variable\n" );
    return false;
}

AGENT_LLM_CLIENT::AGENT_LLM_CLIENT( AGENT_FRAME* aParent ) :
        m_parent( aParent ),
        m_modelName( "GPT-4o" ),
        m_requestInProgress( false ),
        m_cancelRequested( false )
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


// ============================================================================
// Async LLM Streaming Implementation
// ============================================================================

bool AGENT_LLM_CLIENT::AskStreamWithToolsAsync( const nlohmann::json& aMessages,
                                                 const std::string& aSystem,
                                                 const std::vector<LLM_TOOL>& aTools,
                                                 wxEvtHandler* aHandler )
{
    // Check if a request is already in progress
    if( m_requestInProgress.load() )
    {
        PostLLMError( aHandler, "Another LLM request is already in progress" );
        return false;
    }

    // Reset cancel flag
    m_cancelRequested.store( false );
    m_requestInProgress.store( true );

    // Create and start the background thread
    LLM_REQUEST_THREAD* thread = new LLM_REQUEST_THREAD(
        this, aHandler, m_modelName, aMessages, aSystem, aTools );

    // wxThread requires Create() before Run()
    if( thread->Create() != wxTHREAD_NO_ERROR )
    {
        delete thread;
        m_requestInProgress.store( false );
        PostLLMError( aHandler, "Failed to create LLM request thread" );
        return false;
    }

    if( thread->Run() != wxTHREAD_NO_ERROR )
    {
        delete thread;
        m_requestInProgress.store( false );
        PostLLMError( aHandler, "Failed to start LLM request thread" );
        return false;
    }

    // Thread is running - it will post events and clean up when done
    return true;
}


// ============================================================================
// LLM_REQUEST_THREAD Implementation
// ============================================================================

LLM_REQUEST_THREAD::LLM_REQUEST_THREAD( AGENT_LLM_CLIENT* aClient,
                                         wxEvtHandler* aHandler,
                                         const std::string& aModel,
                                         const nlohmann::json& aMessages,
                                         const std::string& aSystem,
                                         const std::vector<LLM_TOOL>& aTools ) :
        wxThread( wxTHREAD_DETACHED ),
        m_client( aClient ),
        m_handler( aHandler ),
        m_model( aModel ),
        m_messages( aMessages ),
        m_system( aSystem ),
        m_tools( aTools ),
        m_cancelFlag( nullptr )
{
}


LLM_REQUEST_THREAD::~LLM_REQUEST_THREAD()
{
    // Mark request as no longer in progress
    if( m_client )
    {
        m_client->m_requestInProgress.store( false );
    }
}


void* LLM_REQUEST_THREAD::Entry()
{
    // Get the cancel flag from the client
    m_cancelFlag = &m_client->m_cancelRequested;

    // Initialize curl
    CURL* curl = curl_easy_init();
    if( !curl )
    {
        PostLLMError( m_handler, "Failed to initialize curl" );
        return nullptr;
    }

    // Build the request
    std::string apiModel = "claude-sonnet-4-20250514";
    if( m_model == "Claude 4.5 Opus" )
        apiModel = "claude-opus-4-5-20251101";
    else if( m_model == "Claude 4 Sonnet" )
        apiModel = "claude-sonnet-4-20250514";

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["system"] = m_system;
    requestBody["messages"] = m_messages;
    requestBody["max_tokens"] = 4096;
    requestBody["stream"] = true;

    // Add tools
    if( !m_tools.empty() )
    {
        requestBody["tools"] = json::array();
        for( const auto& tool : m_tools )
        {
            json toolObj;
            toolObj["name"] = tool.name;
            toolObj["description"] = tool.description;
            toolObj["input_schema"] = tool.input_schema;
            requestBody["tools"].push_back( toolObj );
        }
    }

    std::string requestBodyStr = requestBody.dump();

    // Set up curl options
    curl_easy_setopt( curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages" );
    curl_easy_setopt( curl, CURLOPT_POST, 1L );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, requestBodyStr.c_str() );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, requestBodyStr.size() );

    // Headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append( headers, "Content-Type: application/json" );

    std::string apiKey = AGENT_LLM_CLIENT::GetAnthropicKey();
    headers = curl_slist_append( headers, ( "x-api-key: " + apiKey ).c_str() );
    headers = curl_slist_append( headers, "anthropic-version: 2023-06-01" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

    // Set up streaming callback
    StreamContext ctx;
    ctx.handler = m_handler;
    ctx.cancelFlag = m_cancelFlag;
    ctx.inToolInput = false;

    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &ctx );

    // Perform the request (this blocks until complete or cancelled)
    CURLcode res = curl_easy_perform( curl );

    // Clean up headers
    curl_slist_free_all( headers );

    // Check result
    if( res != CURLE_OK )
    {
        std::string errorMsg = "Curl error: " + std::string( curl_easy_strerror( res ) );
        PostLLMError( m_handler, errorMsg );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );

    if( http_code != 200 )
    {
        std::string errorMsg = "Anthropic API error: HTTP " + std::to_string( http_code );
        PostLLMError( m_handler, errorMsg );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Post completion event
    LLMStreamComplete complete;
    complete.success = true;
    complete.http_status_code = http_code;
    PostLLMComplete( m_handler, complete );

    curl_easy_cleanup( curl );
    return nullptr;
}


size_t LLM_REQUEST_THREAD::StreamWriteCallback( void* contents, size_t size, size_t nmemb, void* userp )
{
    size_t realsize = size * nmemb;
    StreamContext* ctx = static_cast<StreamContext*>( userp );

    // Check for cancellation
    if( ctx->cancelFlag && ctx->cancelFlag->load() )
    {
        return 0; // Returning 0 tells curl to abort
    }

    // Append to buffer
    ctx->buffer.append( static_cast<char*>( contents ), realsize );

    // Process complete SSE events (lines ending with \n\n)
    size_t pos;
    while( ( pos = ctx->buffer.find( "\n\n" ) ) != std::string::npos )
    {
        std::string event = ctx->buffer.substr( 0, pos );
        ctx->buffer.erase( 0, pos + 2 );

        // Parse SSE event - find the data line
        // SSE format can be "event: xxx\ndata: yyy" or just "data: yyy"
        std::string data;
        size_t dataPos = event.find( "data: " );
        if( dataPos != std::string::npos )
        {
            // Extract from "data: " to the end of that line
            size_t dataStart = dataPos + 6;
            size_t lineEnd = event.find( '\n', dataStart );
            if( lineEnd == std::string::npos )
                data = event.substr( dataStart );
            else
                data = event.substr( dataStart, lineEnd - dataStart );
        }

        if( data.empty() )
            continue;

        // Skip [DONE] marker
        if( data == "[DONE]" )
            continue;

        try
        {
            json j = json::parse( data );

            // Handle different event types
            std::string eventType = j.value( "type", "" );

            if( eventType == "content_block_start" )
            {
                auto contentBlock = j.value( "content_block", json::object() );
                std::string blockType = contentBlock.value( "type", "" );

                if( blockType == "tool_use" )
                {
                    ctx->currentToolId = contentBlock.value( "id", "" );
                    ctx->currentToolName = contentBlock.value( "name", "" );
                    ctx->currentToolInput = "";
                    ctx->inToolInput = true;
                }
            }
            else if( eventType == "content_block_delta" )
            {
                auto delta = j.value( "delta", json::object() );
                std::string deltaType = delta.value( "type", "" );

                if( deltaType == "text_delta" )
                {
                    std::string text = delta.value( "text", "" );

                    if( !text.empty() )
                    {
                        LLMStreamChunk chunk;
                        chunk.type = LLMChunkType::TEXT;
                        chunk.text = text;
                        PostLLMChunk( ctx->handler, chunk );
                    }
                }
                else if( deltaType == "input_json_delta" )
                {
                    std::string partial = delta.value( "partial_json", "" );
                    ctx->currentToolInput += partial;
                }
            }
            else if( eventType == "content_block_stop" )
            {
                if( ctx->inToolInput )
                {
                    // Tool use block complete
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::TOOL_USE;
                    chunk.tool_use_id = ctx->currentToolId;
                    chunk.tool_name = ctx->currentToolName;
                    chunk.tool_input_json = ctx->currentToolInput;
                    PostLLMChunk( ctx->handler, chunk );

                    ctx->inToolInput = false;
                    ctx->currentToolId.clear();
                    ctx->currentToolName.clear();
                    ctx->currentToolInput.clear();
                }
            }
            else if( eventType == "message_delta" )
            {
                auto delta = j.value( "delta", json::object() );
                std::string stopReason = delta.value( "stop_reason", "" );

                if( stopReason == "tool_use" )
                {
                    // Signal that tool use is complete, ready to execute
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::TOOL_USE_DONE;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "end_turn" )
                {
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::END_TURN;
                    PostLLMChunk( ctx->handler, chunk );
                }
            }
            else if( eventType == "error" )
            {
                auto error = j.value( "error", json::object() );
                std::string errorMsg = error.value( "message", "Unknown error" );

                LLMStreamChunk chunk;
                chunk.type = LLMChunkType::ERROR;
                chunk.error_message = errorMsg;
                PostLLMChunk( ctx->handler, chunk );
            }
        }
        catch( const json::exception& )
        {
            // JSON parse error - skip this event and continue
        }
    }

    return realsize;
}


void LLM_REQUEST_THREAD::ParseAndPostEvents( StreamContext& ctx, const std::string& data )
{
    // This method is not used - parsing is done inline in StreamWriteCallback
    // Kept for potential future use
}
