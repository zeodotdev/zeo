#include "agent_llm_client.h"
#include <zeo/agent_auth.h>
#include "agent_frame.h"
#include "agent_events.h"
#include <id.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <thread>

using json = nlohmann::json;

static const std::string ZEO_API_URL = ZEO_BASE_URL + "/api/llm/messages";

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
    // If a previous request was cancelled but the thread hasn't finished yet, wait for it.
    // During SSE streaming (especially compaction), the server may pause between events,
    // leaving curl in a blocking recv() where progress callbacks don't fire.
    if( m_requestInProgress.load() && m_cancelRequested.load() )
    {
        const int maxWaitMs = 3000;
        const int sleepMs = 10;
        int waited = 0;

        while( m_requestInProgress.load() && waited < maxWaitMs )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
            waited += sleepMs;
        }

        if( m_requestInProgress.load() )
        {
            // The old thread is stuck (curl blocked in recv with no progress callbacks).
            // Force-clear the flag so the new request can proceed. The stale thread will
            // eventually finish and its cleanup is harmless: it won't post events
            // (wasCancelled is true) and its redundant store(false) is a no-op.
            wxLogWarning( "Cancelled LLM request did not finish within %dms — force-clearing",
                          maxWaitMs );
            m_requestInProgress.store( false );
        }
    }

    // Check if a request is already in progress (non-cancelled)
    if( m_requestInProgress.load() )
    {
        PostLLMError( aHandler, "Another LLM request is already in progress" );
        return false;
    }

    // Reset cancel flag
    m_cancelRequested.store( false );
    m_requestInProgress.store( true );

    // Create and start the background thread
    // Compose full system prompt: base + plan addendum when in plan mode
    std::string fullPrompt = m_systemPrompt;

    if( m_agentMode == AgentMode::PLAN && !m_planAddendum.empty() )
        fullPrompt += "\n" + m_planAddendum;

    LLM_REQUEST_THREAD* thread = new LLM_REQUEST_THREAD(
        this, aHandler, m_modelName, aMessages, aTools, m_agentMode,
        m_chatId, m_chatTitle, m_chatStoragePath, m_logStoragePath,
        fullPrompt, m_toolDurations );
    m_toolDurations.clear();

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
                                         const std::vector<LLM_TOOL>& aTools,
                                         AgentMode aAgentMode,
                                         const std::string& aChatId,
                                         const std::string& aChatTitle,
                                         const std::string& aChatStoragePath,
                                         const std::string& aLogStoragePath,
                                         const std::string& aSystemPrompt,
                                         const std::map<std::string, int>& aToolDurations ) :
        wxThread( wxTHREAD_DETACHED ),
        m_client( aClient ),
        m_handler( aHandler ),
        m_model( aModel ),
        m_messages( aMessages ),
        m_tools( aTools ),
        m_agentMode( aAgentMode ),
        m_systemPrompt( aSystemPrompt ),
        m_cancelFlag( nullptr ),
        m_chatId( aChatId ),
        m_chatTitle( aChatTitle ),
        m_chatStoragePath( aChatStoragePath ),
        m_logStoragePath( aLogStoragePath ),
        m_toolDurations( aToolDurations )
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
        if( m_client )
            m_client->m_requestInProgress.store( false );
        return nullptr;
    }

    // Map display name to API model ID
    std::string apiModel;
    if( m_model == "Gemini 3.1 Pro" )
        apiModel = "gemini-3.1-pro-preview-customtools";
    else if( m_model == "Claude 4.6 Opus (1M)" )
        apiModel = "claude-opus-4-6-1m";
    else
        apiModel = "claude-opus-4-6";

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["messages"] = m_messages;
    requestBody["stream"] = true;

    // Set max_tokens for Anthropic models (Gemini doesn't need it — proxy handles)
    if( apiModel.find( "gemini" ) == std::string::npos )
    {
        if( apiModel.find( "claude-opus-4-6" ) == 0 )
            requestBody["max_tokens"] = 128000;
        else
            requestBody["max_tokens"] = 131072;
    }

    // Signal agent mode and conversation metadata to server
    // (proxy strips these before forwarding to LLM provider)
    json metadataObj = {
        { "agent_mode", ( m_agentMode == AgentMode::PLAN ) ? "plan" : "execute" }
    };

    if( !m_chatId.empty() )
        metadataObj["conversation_id"] = m_chatId;

    if( !m_chatTitle.empty() )
        metadataObj["conversation_title"] = m_chatTitle;

    if( !m_chatStoragePath.empty() )
        metadataObj["chat_storage_path"] = m_chatStoragePath;

    if( !m_logStoragePath.empty() )
        metadataObj["log_storage_path"] = m_logStoragePath;

    if( !m_systemPrompt.empty() )
        metadataObj["system_prompt"] = m_systemPrompt;

    if( !m_toolDurations.empty() )
    {
        json durations = json::object();
        for( const auto& [id, ms] : m_toolDurations )
            durations[id] = ms;
        metadataObj["tool_durations"] = durations;
        m_toolDurations.clear();
    }

    requestBody["metadata"] = metadataObj;

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

            if( tool.defer_loading )
                toolObj["defer_loading"] = true;

            requestBody["tools"].push_back( toolObj );
        }
    }

    std::string requestBodyStr = requestBody.dump();

    wxLogInfo( "LLM_REQUEST: model=%s, messages=%zu, tools=%zu, body_size=%zu bytes, url=%s",
               apiModel.c_str(), m_messages.size(), m_tools.size(), requestBodyStr.size(),
               ZEO_API_URL.c_str() );

    // Get access token from auth manager
    std::string accessToken;
    if( m_client && m_client->m_auth )
    {
        accessToken = m_client->m_auth->GetAccessToken();
    }

    if( accessToken.empty() )
    {
        PostLLMError( m_handler, "Not authenticated. Please sign in." );
        if( m_client )
            m_client->m_requestInProgress.store( false );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    // Set up curl options - use zeo proxy
    curl_easy_setopt( curl, CURLOPT_URL, ZEO_API_URL.c_str() );
    curl_easy_setopt( curl, CURLOPT_POST, 1L );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, requestBodyStr.c_str() );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, requestBodyStr.size() );

    // Connection timeouts
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 30L );

    // Headers - use Bearer auth for proxy
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append( headers, "Content-Type: application/json" );
    headers = curl_slist_append( headers, ( "Authorization: Bearer " + accessToken ).c_str() );
    headers = curl_slist_append( headers,
        ( std::string( "X-Agent-Mode: " )
          + ( ( m_agentMode == AgentMode::PLAN ) ? "plan" : "execute" ) ).c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

    // Set up progress callback for fast cancellation
    // The progress callback is called frequently and can abort immediately
    // NOTE: Must use an explicit function pointer cast — passing a lambda directly
    // to curl_easy_setopt (a C variadic function) doesn't trigger the lambda-to-
    // function-pointer conversion on MSVC, causing a crash in pgrs_update.
    curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );
    curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION,
        static_cast<int(*)( void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t )>(
        []( void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t ) -> int {
            std::atomic<bool>* cancelFlag = static_cast<std::atomic<bool>*>( clientp );
            if( cancelFlag && cancelFlag->load() )
                return 1;  // Non-zero aborts the transfer
            return 0;
        }));
    curl_easy_setopt( curl, CURLOPT_XFERINFODATA, m_cancelFlag );

    // Retry loop for transient connection errors
    static constexpr int    MAX_RETRIES       = 3;
    static constexpr int    BASE_DELAY_MS     = 1000;
    CURLcode                res               = CURLE_OK;
    long                    http_code         = 0;
    bool                    wasCancelled      = false;
    StreamContext           ctx;

    for( int attempt = 0; attempt <= MAX_RETRIES; attempt++ )
    {
        // Reset streaming context for each attempt
        ctx = {};
        ctx.handler = m_handler;
        ctx.cancelFlag = m_cancelFlag;

        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, &ctx );

        res = curl_easy_perform( curl );

        wasCancelled = m_cancelFlag && m_cancelFlag->load();
        if( wasCancelled )
            break;

        // Check for transient errors worth retrying
        bool isTransient = ( res == CURLE_SSL_CONNECT_ERROR
                             || res == CURLE_COULDNT_CONNECT
                             || res == CURLE_OPERATION_TIMEDOUT
                             || res == CURLE_GOT_NOTHING
                             || res == CURLE_SEND_ERROR
                             || res == CURLE_RECV_ERROR );

        if( res == CURLE_OK )
        {
            curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );

            // Retry on 502/503/529 (server transient errors)
            isTransient = ( http_code == 502 || http_code == 503 || http_code == 529 );

            if( !isTransient )
                break;  // Success or non-transient HTTP error
        }

        if( !isTransient || attempt == MAX_RETRIES )
            break;

        // Exponential backoff: 1s, 2s, 4s
        int delayMs = BASE_DELAY_MS * ( 1 << attempt );
        std::string reason = res != CURLE_OK
                ? curl_easy_strerror( res )
                : "HTTP " + std::to_string( http_code );
        wxLogInfo( "LLM request failed (attempt %d/%d): %s — retrying in %dms",
                   attempt + 1, MAX_RETRIES + 1, reason.c_str(), delayMs );

        std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );

        // Re-check cancellation after sleep
        if( m_cancelFlag && m_cancelFlag->load() )
        {
            wasCancelled = true;
            break;
        }

        // Reset curl handle state for retry (reuse the connection cache)
        curl_easy_setopt( curl, CURLOPT_FRESH_CONNECT, 1L );
    }

    // Clean up headers
    curl_slist_free_all( headers );

    // Check result
    if( res != CURLE_OK )
    {
        std::string errorMsg = "Curl error: " + std::string( curl_easy_strerror( res ) );
        if( !wasCancelled )
            PostLLMError( m_handler, errorMsg );
        // Clear in-progress flag before returning so the next request isn't blocked.
        // The destructor also clears this, but detached-thread destruction timing is
        // non-deterministic and can race with the next AskStreamWithToolsAsync call.
        if( m_client )
            m_client->m_requestInProgress.store( false );
        curl_easy_cleanup( curl );
        return nullptr;
    }

    if( http_code != 200 )
    {
        // Pass raw response body and HTTP code as structured data
        std::string responseBody = ctx.buffer.substr( 0, 500 ); // Limit to 500 chars
        if( !wasCancelled )
            PostLLMError( m_handler, responseBody, http_code );
        if( m_client )
            m_client->m_requestInProgress.store( false );
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
    // Use direct file I/O for crash diagnostics (wxLog buffers from bg threads)
    static FILE* dbgFile = nullptr;
#ifdef _WIN32
    if( !dbgFile )
    {
        dbgFile = _wfopen( L"C:\\Users\\jared\\AppData\\Roaming\\kicad\\logs\\stream_debug.log", L"w" );
    }
#endif

    size_t realsize = size * nmemb;
    StreamContext* ctx = static_cast<StreamContext*>( userp );

    if( dbgFile ) { fprintf( dbgFile, "CB: entry realsize=%zu ctx=%p\n", realsize, (void*)ctx ); fflush( dbgFile ); }

    if( !ctx )
        return 0;

    if( dbgFile ) { fprintf( dbgFile, "CB: handler=%p cancelFlag=%p\n", (void*)ctx->handler, (void*)ctx->cancelFlag ); fflush( dbgFile ); }

    if( !ctx->handler )
        return 0;

    // Check for cancellation
    if( ctx->cancelFlag && ctx->cancelFlag->load() )
    {
        return 0; // Returning 0 tells curl to abort
    }

    // Append to buffer
    ctx->buffer.append( static_cast<char*>( contents ), realsize );
    if( dbgFile ) { fprintf( dbgFile, "CB: buffer appended, size=%zu\n", ctx->buffer.size() ); fflush( dbgFile ); }

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

        if( dbgFile ) { fprintf( dbgFile, "CB: SSE event len=%zu first80='%.80s'\n", event.size(), event.c_str() ); fflush( dbgFile ); }

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

        if( dbgFile ) { fprintf( dbgFile, "CB: parsing JSON len=%zu\n", data.size() ); fflush( dbgFile ); }

        try
        {
            json j = json::parse( data );

            // Handle different event types
            std::string eventType = j.value( "type", "" );
            if( dbgFile ) { fprintf( dbgFile, "CB: eventType='%s' handler=%p\n", eventType.c_str(), (void*)ctx->handler ); fflush( dbgFile ); }

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

                    // Post TOOL_USE_START to show tool name while generating input
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::TOOL_USE_START;
                    chunk.tool_use_id = ctx->currentToolId;
                    chunk.tool_name = ctx->currentToolName;
                    PostLLMChunk( ctx->handler, chunk );
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
                else if( blockType == "compaction" )
                {
                    // Start accumulating compaction content
                    ctx->currentCompaction = "";
                    ctx->inCompaction = true;

                    // Post COMPACTION_START so UI shows "Compacting..." immediately
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::COMPACTION_START;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( blockType == "server_tool_use" )
                {
                    // Server-side tool invoked (e.g., web_search)
                    // Input is streamed via input_json_delta, so track state
                    // and assemble the complete block at content_block_stop
                    ctx->serverToolId = contentBlock.value( "id", "" );
                    ctx->serverToolName = contentBlock.value( "name", "" );
                    ctx->serverToolInput = "";
                    ctx->inServerTool = true;

                    // Post event immediately for logging/UI
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::SERVER_TOOL_USE;
                    chunk.tool_name = ctx->serverToolName;
                    PostLLMChunk( ctx->handler, chunk );
                }
                else if( blockType == "web_search_tool_result" )
                {
                    // Web search results returned from server
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::SERVER_TOOL_RESULT;
                    chunk.content_block_json = contentBlock.dump();
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

                        if( dbgFile ) { fprintf( dbgFile, "CB: posting TEXT chunk, handler=%p\n", (void*)ctx->handler ); fflush( dbgFile ); }
                        LLMStreamChunk chunk;
                        chunk.type = LLMChunkType::TEXT;
                        chunk.text = text;
                        PostLLMChunk( ctx->handler, chunk );
                        if( dbgFile ) { fprintf( dbgFile, "CB: TEXT chunk posted OK\n" ); fflush( dbgFile ); }
                    }
                }
                else if( deltaType == "input_json_delta" )
                {
                    std::string partial = delta.value( "partial_json", "" );

                    if( ctx->inServerTool )
                        ctx->serverToolInput += partial;
                    else
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
                else if( deltaType == "compaction_delta" )
                {
                    // Accumulate compaction content
                    std::string compaction = delta.value( "content", "" );
                    ctx->currentCompaction += compaction;
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
                else if( ctx->inServerTool )
                {
                    // Server tool block complete - assemble full block with accumulated input
                    nlohmann::json serverToolBlock = {
                        { "type", "server_tool_use" },
                        { "id", ctx->serverToolId },
                        { "name", ctx->serverToolName }
                    };

                    if( !ctx->serverToolInput.empty() )
                    {
                        try
                        {
                            serverToolBlock["input"] = json::parse( ctx->serverToolInput );
                        }
                        catch( ... )
                        {
                            serverToolBlock["input"] = json::object();
                        }
                    }

                    // Re-post SERVER_TOOL_USE with complete content block JSON
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::SERVER_TOOL_USE;
                    chunk.tool_name = ctx->serverToolName;
                    chunk.content_block_json = serverToolBlock.dump();
                    PostLLMChunk( ctx->handler, chunk );

                    ctx->inServerTool = false;
                    ctx->serverToolId.clear();
                    ctx->serverToolName.clear();
                    ctx->serverToolInput.clear();
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
                else if( ctx->inCompaction )
                {
                    // Compaction block complete - post with full content
                    LLMStreamChunk chunk;
                    chunk.type = LLMChunkType::COMPACTION;
                    chunk.compaction_content = ctx->currentCompaction;
                    PostLLMChunk( ctx->handler, chunk );

                    ctx->inCompaction = false;
                    ctx->currentCompaction.clear();
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
                    // Server tool executing - stream continues
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
            }
            else if( eventType == "error" )
            {
                // Check for cancellation before posting event
                if( ctx->cancelFlag && ctx->cancelFlag->load() )
                    return 0;

                auto error = j.value( "error", json::object() );

                LLMStreamChunk chunk;
                chunk.type = LLMChunkType::ERRORED;
                chunk.error_type = error.value( "type", "" );
                chunk.error_message = error.value( "message", "Unknown error" );
                PostLLMChunk( ctx->handler, chunk );
            }
        }
        catch( const json::exception& e )
        {
            if( dbgFile ) { fprintf( dbgFile, "CB: JSON parse error: %s\n", e.what() ); fflush( dbgFile ); }
        }
    }

    if( dbgFile ) { fprintf( dbgFile, "CB: returning realsize=%zu\n", realsize ); fflush( dbgFile ); }
    return realsize;
}


void LLM_REQUEST_THREAD::ParseAndPostEvents( StreamContext& ctx, const std::string& data )
{
    // This method is not used - parsing is done inline in StreamWriteCallback
    // Kept for potential future use
}
