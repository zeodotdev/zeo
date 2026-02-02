#include "agent_llm_client.h"
#include "auth/agent_auth.h"
#include "agent_frame.h"
#include "agent_events.h"
#include <id.h>
#include <curl/curl.h>
#include <ki_exception.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;


AGENT_LLM_CLIENT::AGENT_LLM_CLIENT( AGENT_FRAME* aParent ) :
        m_parent( aParent ),
        m_modelName( "GPT-4o" ),
        m_requestInProgress( false ),
        m_cancelRequested( false )
{
}

AGENT_LLM_CLIENT::~AGENT_LLM_CLIENT()
{
}

void AGENT_LLM_CLIENT::SetModel( const std::string& aModelName )
{
    m_modelName = aModelName;
}

// ============================================================================
// Async LLM Streaming Implementation
// ============================================================================

bool AGENT_LLM_CLIENT::AskStreamWithToolsAsync( const nlohmann::json& aMessages,
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
        this, aHandler, m_modelName, aMessages, aTools );

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
                                         const std::vector<LLM_TOOL>& aTools ) :
        wxThread( wxTHREAD_DETACHED ),
        m_client( aClient ),
        m_handler( aHandler ),
        m_model( aModel ),
        m_messages( aMessages ),
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
    std::string apiModel = "claude-sonnet-4-5-20250929";
    if( m_model == "Claude 4.5 Opus" )
        apiModel = "claude-opus-4-5-20251101";
    else if( m_model == "Claude 4.5 Sonnet" )
        apiModel = "claude-sonnet-4-5-20250929";

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["messages"] = m_messages;
    requestBody["max_tokens"] = 2000;  // TODO: restore to 4096 after testing
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

    // Get access token from auth manager
    std::string accessToken;
    if( m_client && m_client->m_auth )
    {
        accessToken = m_client->m_auth->GetAccessToken();
    }

    if( accessToken.empty() )
    {
        PostLLMError( m_handler, "Not authenticated. Please sign in." );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Set up curl options - use zener.so proxy (or override via ZENER_API_URL)
    const char* envUrl = std::getenv( "ZENER_API_URL" );
    std::string apiUrl = envUrl ? envUrl : "https://www.zener.so/api/llm/messages";
    curl_easy_setopt( curl, CURLOPT_URL, apiUrl.c_str() );
    curl_easy_setopt( curl, CURLOPT_POST, 1L );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, requestBodyStr.c_str() );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, requestBodyStr.size() );

    // Headers - use Bearer auth for proxy
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append( headers, "Content-Type: application/json" );
    headers = curl_slist_append( headers, ( "Authorization: Bearer " + accessToken ).c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

    // Set up streaming callback
    StreamContext ctx;
    ctx.handler = m_handler;
    ctx.cancelFlag = m_cancelFlag;
    ctx.inToolInput = false;
    ctx.inThinking = false;

    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &ctx );

    // Set up progress callback for fast cancellation
    // The progress callback is called frequently and can abort immediately
    curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );
    curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION,
        []( void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t ) -> int {
            std::atomic<bool>* cancelFlag = static_cast<std::atomic<bool>*>( clientp );
            if( cancelFlag && cancelFlag->load() )
                return 1;  // Non-zero aborts the transfer
            return 0;
        });
    curl_easy_setopt( curl, CURLOPT_XFERINFODATA, m_cancelFlag );

    // Perform the request (this blocks until complete or cancelled)
    CURLcode res = curl_easy_perform( curl );

    // Clean up headers
    curl_slist_free_all( headers );

    // Check if we were cancelled - don't post events to potentially destroyed handler
    bool wasCancelled = m_cancelFlag && m_cancelFlag->load();

    // Check result
    if( res != CURLE_OK )
    {
        std::string errorMsg = "Curl error: " + std::string( curl_easy_strerror( res ) );
        if( !wasCancelled )
            PostLLMError( m_handler, errorMsg );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );

    if( http_code != 200 )
    {
        // Check for context_exhausted error (HTTP 400)
        // The server no longer sends summarizedMessages - client must call /api/llm/summarize
        if( http_code == 400 )
        {
            try
            {
                json errorJson = json::parse( ctx.buffer );
                if( errorJson.value( "error", "" ) == "context_exhausted" )
                {
                    // Send context exhausted event (no messages - controller will call summarize endpoint)
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::CONTEXT_EXHAUSTED;
                    PostLLMChunk( m_handler, chunk );

                    // Mark request as complete BEFORE posting completion event
                    if( m_client )
                    {
                        m_client->m_requestInProgress.store( false );
                    }

                    // Send completion with failure flag - controller needs to handle recovery
                    LLMStreamComplete complete;
                    complete.success = false;
                    complete.http_status_code = 400;
                    complete.error_message = "context_exhausted";
                    PostLLMComplete( m_handler, complete );
                    curl_easy_cleanup( curl );
                    return nullptr;
                }
            }
            catch( ... )
            {
                // JSON parse error - fall through to normal error handling
            }
        }

        std::string errorMsg = "LLM API error: HTTP " + std::to_string( http_code );
        // Include the response body which contains error details
        if( !ctx.buffer.empty() )
        {
            errorMsg += "\nResponse: " + ctx.buffer.substr( 0, 500 ); // Limit to 500 chars
        }
        if( !wasCancelled )
            PostLLMError( m_handler, errorMsg );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Mark request as complete BEFORE posting completion event
    // This ensures the flag is clear when the handler processes the event
    if( m_client )
    {
        m_client->m_requestInProgress.store( false );
    }

    // Post completion event (only if not cancelled)
    if( !wasCancelled )
    {
        LLMStreamComplete complete;
        complete.success = true;
        complete.http_status_code = http_code;
        PostLLMComplete( m_handler, complete );
    }

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
        // Check for cancellation before processing each event
        if( ctx->cancelFlag && ctx->cancelFlag->load() )
        {
            return 0; // Abort - handler may be destroyed
        }

        std::string event = ctx->buffer.substr( 0, pos );
        ctx->buffer.erase( 0, pos + 2 );

        // Parse SSE event - extract event type and data line
        // SSE format can be "event: xxx\ndata: yyy" or just "data: yyy"
        std::string sseEventType;
        size_t eventPos = event.find( "event: " );
        if( eventPos != std::string::npos )
        {
            size_t eventStart = eventPos + 7;
            size_t lineEnd = event.find( '\n', eventStart );
            sseEventType = ( lineEnd == std::string::npos )
                ? event.substr( eventStart )
                : event.substr( eventStart, lineEnd - eventStart );
        }

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

        // Handle context_status event from server
        if( sseEventType == "context_status" )
        {
            try
            {
                json j = json::parse( data );
                LLMStreamChunk chunk;
                chunk.type = LLMChunkType::CONTEXT_STATUS;
                chunk.context_percent_used = j.value( "percent_used", 0 );
                chunk.context_compacted = j.value( "compacted", false );
                PostLLMChunk( ctx->handler, chunk );
            }
            catch( const json::exception& )
            {
                // JSON parse error - skip this event
            }
            continue;
        }

        // Handle context_compacting event (Scenario B: server is compacting)
        if( sseEventType == "context_compacting" )
        {
            LLMStreamChunk chunk;
            chunk.type = LLMChunkType::CONTEXT_COMPACTING;
            PostLLMChunk( ctx->handler, chunk );
            continue;
        }

        // Handle error SSE event (including context_exhausted mid-stream)
        if( sseEventType == "error" )
        {
            try
            {
                json j = json::parse( data );
                if( j.value( "error", "" ) == "context_exhausted" )
                {
                    // Context exhausted mid-stream - controller will handle recovery
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::CONTEXT_EXHAUSTED;
                    PostLLMChunk( ctx->handler, chunk );
                    return 0;  // Abort stream - controller will handle recovery
                }
            }
            catch( const json::exception& )
            {
                // JSON parse error - skip this event
            }
            continue;
        }

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
                else if( blockType == "thinking" )
                {
                    ctx->currentThinking = "";
                    ctx->inThinking = true;

                    // Post THINKING_START to show loading animation
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::THINKING_START;
                    PostLLMChunk( ctx->handler, chunk );
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
                        // Check for cancellation before posting event
                        if( ctx->cancelFlag && ctx->cancelFlag->load() )
                            return 0;

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
                else if( deltaType == "thinking_delta" )
                {
                    // Stream thinking text incrementally (like text_delta)
                    std::string thinking = delta.value( "thinking", "" );

                    if( !thinking.empty() )
                    {
                        ctx->currentThinking += thinking;  // Accumulate for API

                        LLMStreamChunk chunk;
                        chunk.type = LLMChunkType::THINKING;
                        chunk.thinking_text = thinking;
                        PostLLMChunk( ctx->handler, chunk );
                    }
                }
                else if( deltaType == "signature_delta" )
                {
                    // Accumulate signature for sending back to API
                    std::string signature = delta.value( "signature", "" );
                    ctx->currentSignature += signature;
                }
            }
            else if( eventType == "content_block_stop" )
            {
                if( ctx->inToolInput )
                {
                    // Check for cancellation before posting event
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

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
                else if( ctx->inThinking )
                {
                    // Thinking block complete - post THINKING_DONE with accumulated content and signature
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::THINKING_DONE;
                    chunk.thinking_text = ctx->currentThinking;      // Full thinking content
                    chunk.thinking_signature = ctx->currentSignature; // Signature for API
                    PostLLMChunk( ctx->handler, chunk );

                    ctx->inThinking = false;
                    ctx->currentThinking.clear();
                    ctx->currentSignature.clear();
                }
            }
            else if( eventType == "message_delta" )
            {
                auto delta = j.value( "delta", json::object() );
                std::string stopReason = delta.value( "stop_reason", "" );

                if( stopReason == "tool_use" )
                {
                    // Check for cancellation before posting event
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    // Signal that tool use is complete, ready to execute
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::TOOL_USE_DONE;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "end_turn" )
                {
                    // Check for cancellation before posting event
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::END_TURN;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "max_tokens" )
                {
                    // Response truncated due to max_tokens limit - needs continuation
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::MAX_TOKENS;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "pause_turn" )
                {
                    // Server tool paused - needs retry
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::PAUSE_TURN;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "refusal" )
                {
                    // Model refused the request
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::REFUSAL;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( stopReason == "model_context_window_exceeded" )
                {
                    // Context exhausted mid-stream - handled via error event from server
                    // This case shouldn't happen as server sends error SSE event,
                    // but handle defensively
                    if( ctx->cancelFlag && ctx->cancelFlag->load() )
                        return 0;

                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::CONTEXT_EXHAUSTED;
                    PostLLMChunk( ctx->handler, chunk );
                }
            }
            else if( eventType == "error" )
            {
                // Check for cancellation before posting event
                if( ctx->cancelFlag && ctx->cancelFlag->load() )
                    return 0;

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


// ============================================================================
// Summarize Endpoint Implementation
// ============================================================================

SummarizeResult AGENT_LLM_CLIENT::CallSummarizeEndpoint( const nlohmann::json& aMessages,
                                                          int aKeepCount )
{
    SummarizeResult result;

    // Get access token
    std::string accessToken;
    if( m_auth )
    {
        accessToken = m_auth->GetAccessToken();
    }

    if( accessToken.empty() )
    {
        result.error_message = "Not authenticated";
        return result;
    }

    // Initialize curl
    CURL* curl = curl_easy_init();
    if( !curl )
    {
        result.error_message = "Failed to initialize curl";
        return result;
    }

    // Build request body
    json requestBody;
    requestBody["messages"] = aMessages;
    requestBody["keep_count"] = aKeepCount;
    std::string requestBodyStr = requestBody.dump();

    // Set up curl options
    curl_easy_setopt( curl, CURLOPT_URL, "https://www.zener.so/api/llm/summarize" );
    curl_easy_setopt( curl, CURLOPT_POST, 1L );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, requestBodyStr.c_str() );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, requestBodyStr.size() );

    // Headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append( headers, "Content-Type: application/json" );
    headers = curl_slist_append( headers, ( "Authorization: Bearer " + accessToken ).c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

    // Response buffer
    std::string responseBuffer;
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION,
        []( void* contents, size_t size, size_t nmemb, void* userp ) -> size_t {
            std::string* buffer = static_cast<std::string*>( userp );
            buffer->append( static_cast<char*>( contents ), size * nmemb );
            return size * nmemb;
        });
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &responseBuffer );

    // Perform request
    CURLcode res = curl_easy_perform( curl );

    // Clean up headers
    curl_slist_free_all( headers );

    if( res != CURLE_OK )
    {
        result.error_message = "Curl error: " + std::string( curl_easy_strerror( res ) );
        curl_easy_cleanup( curl );
        return result;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );
    curl_easy_cleanup( curl );

    if( http_code != 200 )
    {
        // Try to parse error from response
        try
        {
            json errorJson = json::parse( responseBuffer );
            result.error_message = errorJson.value( "error", "HTTP " + std::to_string( http_code ) );
            if( errorJson.contains( "message" ) )
            {
                result.error_message += ": " + errorJson["message"].get<std::string>();
            }
        }
        catch( ... )
        {
            result.error_message = "HTTP " + std::to_string( http_code );
        }
        return result;
    }

    // Parse successful response
    try
    {
        json responseJson = json::parse( responseBuffer );
        if( responseJson.contains( "messages" ) && responseJson["messages"].is_array() )
        {
            result.success = true;
            result.messages = responseJson["messages"];
        }
        else
        {
            result.error_message = "Invalid response: missing messages array";
        }
    }
    catch( const json::exception& e )
    {
        result.error_message = "JSON parse error: " + std::string( e.what() );
    }

    return result;
}
