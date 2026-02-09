#include "chat_controller.h"
#include "chat_events.h"
#include "../tools/agent_tools.h"
#include "../tools/tool_registry.h"
#include "view/file_attach.h"
#include "agent_llm_client.h"
#include "agent_chat_history.h"
#include "auth/agent_auth.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>
#include <kiway.h>
#include <wx/log.h>
#include <kicad_curl/kicad_curl_easy.h>

// ============================================================================
// Event definitions
// ============================================================================

wxDEFINE_EVENT( EVT_CHAT_TEXT_DELTA, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_THINKING_START, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_THINKING_DELTA, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_THINKING_DONE, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TOOL_GENERATING, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TOOL_START, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TOOL_COMPLETE, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TURN_COMPLETE, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_ERROR, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_STATE_CHANGED, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TITLE_DELTA, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TITLE_GENERATED, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_HISTORY_LOADED, wxThreadEvent );


// ============================================================================
// Constructor / Destructor
// ============================================================================

CHAT_CONTROLLER::CHAT_CONTROLLER( wxEvtHandler* aEventSink )
    : m_eventSink( aEventSink ),
      m_llmClient( nullptr ),
      m_chatHistoryDb( nullptr ),
      m_auth( nullptr ),
      m_stopRequested( false ),
      m_continueAfterComplete( false ),
      m_agentMode( AgentMode::EXECUTE )
{
    // Initialize tool definitions
    m_tools = AgentTools::GetToolDefinitions();

    // Initialize chat history as empty array
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();
}


CHAT_CONTROLLER::~CHAT_CONTROLLER()
{
    // Services (m_llmClient, m_chatHistoryDb) are not owned, don't delete
}


// ============================================================================
// Commands (input)
// ============================================================================

void CHAT_CONTROLLER::SendMessage( const std::string& aText )
{
    wxLogInfo( "CHAT_CONTROLLER::SendMessage called with text: %s", aText.c_str() );
    DoSendMessage( aText, nlohmann::json() );
}


void CHAT_CONTROLLER::SendMessageWithAttachments(
        const std::string& aText,
        const std::vector<UserAttachment>& aAttachments )
{
    wxLogInfo( "CHAT_CONTROLLER::SendMessageWithAttachments called with %zu attachments",
               aAttachments.size() );

    // Build multi-content array (Anthropic format): attachments first, then text
    nlohmann::json content = nlohmann::json::array();

    for( const auto& att : aAttachments )
    {
        // Images use "image" type, everything else (e.g. PDF) uses "document" type
        std::string blockType = FileAttach::IsImageMediaType( att.media_type ) ? "image" : "document";

        content.push_back( {
            { "type", blockType },
            { "source", {
                { "type", "base64" },
                { "media_type", att.media_type },
                { "data", att.base64_data }
            } }
        } );
    }

    DoSendMessage( aText, content );
}


void CHAT_CONTROLLER::DoSendMessage( const std::string& aText, const nlohmann::json& aContent )
{
    if( !CanAcceptInput() )
    {
        wxLogWarning( "CHAT_CONTROLLER::DoSendMessage called while busy" );
        return;
    }

    // Store first user message for title generation (count user messages, not all messages)
    int userMessageCount = 0;
    for( const auto& msg : m_chatHistory )
    {
        if( msg.contains( "role" ) && msg["role"] == "user" )
            userMessageCount++;
    }

    if( userMessageCount == 0 )
    {
        m_firstUserMessage = aText.empty() ? "(File attachment)" : aText;
        GenerateTitle();
    }

    // Inject project context (including hierarchy) into first user message
    std::string messageText = aText;
    if( userMessageCount == 0 && m_getProjectPathFn )
    {
        std::string projectContext = m_getProjectPathFn();
        if( !projectContext.empty() )
        {
            messageText = "<project_context>\n" + projectContext +
                         "\n</project_context>\n\n" + aText;

            wxLogInfo( "CHAT: First message with context:\n%s", messageText.c_str() );
        }
    }

    // Build the user message for history
    nlohmann::json userMsg;

    if( aContent.is_array() && !aContent.empty() )
    {
        // Multi-content message (images + text)
        nlohmann::json content = aContent;

        if( !messageText.empty() )
        {
            content.push_back( {
                { "type", "text" },
                { "text", messageText }
            } );
        }

        userMsg = { { "role", "user" }, { "content", content } };
    }
    else
    {
        // Plain text message
        userMsg = { { "role", "user" }, { "content", messageText } };
    }

    AddToHistory( userMsg );

    // Reset streaming state
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_thinkingSignature.clear();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();

    // Transition to waiting for LLM
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                             static_cast<int>( m_ctx.GetState() ) ) );

    // Start the LLM request
    StartLLMRequest();
}


void CHAT_CONTROLLER::Cancel()
{
    wxLogInfo( "CHAT_CONTROLLER::Cancel called" );
    m_stopRequested = true;
        m_continueAfterComplete = false;

    if( m_llmClient )
        m_llmClient->CancelRequest();

    // Save any partial response that was being streamed.
    // Skip this if we're in a tool execution state — the assistant message (with tool_use blocks)
    // was already committed to history by AddAssistantToolUseToHistory(). The stale
    // m_currentResponse/m_thinkingContent from that turn would create a duplicate assistant message.
    bool toolStateActive = m_ctx.GetState() == AgentConversationState::TOOL_USE_DETECTED ||
                           m_ctx.GetState() == AgentConversationState::EXECUTING_TOOL ||
                           m_ctx.GetState() == AgentConversationState::PROCESSING_TOOL_RESULT;

    if( !toolStateActive && ( !m_currentResponse.empty() || !m_thinkingContent.IsEmpty() ) )
    {
        nlohmann::json content = nlohmann::json::array();

        // Add thinking block if present
        if( !m_thinkingContent.IsEmpty() && !m_thinkingSignature.empty() )
        {
            content.push_back( {
                { "type", "thinking" },
                { "thinking", m_thinkingContent.ToStdString() },
                { "signature", m_thinkingSignature }
            } );
        }

        // Add partial text response with (Stopped) indicator
        if( !m_currentResponse.empty() )
        {
            content.push_back( {
                { "type", "text" },
                { "text", m_currentResponse + "\n\n*(Stopped)*" }
            } );
        }

        // If content is empty (e.g. cancelled during thinking before signature arrived),
        // add a minimal text block to avoid sending an empty assistant message to the API.
        if( content.empty() )
        {
            content.push_back( {
                { "type", "text" },
                { "text", "*(Stopped)*" }
            } );
        }

        nlohmann::json assistantMsg = {
            { "role", "assistant" },
            { "content", content }
        };
        AddToHistory( assistantMsg );

        // Note: Don't clear m_currentResponse here - the UI needs it to finalize the display.
        // The frame's OnStop() will clear streaming state after finalizing.
    }

    // If tool_use blocks were already added to history (state >= TOOL_USE_DETECTED),
    // we need to add fake tool_results for any pending tools to satisfy the Anthropic API.
    // The API requires every tool_use to have a corresponding tool_result.
    if( m_ctx.GetState() == AgentConversationState::TOOL_USE_DETECTED ||
        m_ctx.GetState() == AgentConversationState::EXECUTING_TOOL ||
        m_ctx.GetState() == AgentConversationState::PROCESSING_TOOL_RESULT )
    {
        // Collect all tool IDs that need fake results
        std::vector<std::string> orphanedToolIds;

        // Get from pending tools (JSON array - these were already added to history)
        for( const auto& tool : m_pendingToolCalls )
        {
            if( tool.contains( "id" ) && tool["id"].is_string() )
            {
                orphanedToolIds.push_back( tool["id"].get<std::string>() );
            }
        }

        // Also check the state machine's pending tools
        for( size_t i = 0; i < m_ctx.GetPendingToolCallCount(); i++ )
        {
            PendingToolCall* pending = m_ctx.GetNextPendingToolCall();
            if( pending && !pending->tool_use_id.empty() )
            {
                // Only add if not already in the list
                if( std::find( orphanedToolIds.begin(), orphanedToolIds.end(),
                               pending->tool_use_id ) == orphanedToolIds.end() )
                {
                    orphanedToolIds.push_back( pending->tool_use_id );
                }
            }
        }

        // Add fake tool_results for orphaned tool_use blocks
        if( !orphanedToolIds.empty() )
        {
            nlohmann::json content = nlohmann::json::array();
            for( const auto& toolId : orphanedToolIds )
            {
                content.push_back( {
                    { "type", "tool_result" },
                    { "tool_use_id", toolId },
                    { "content", "Tool execution was stopped by the user." },
                    { "is_error", true }
                } );
            }

            nlohmann::json toolResultMsg = {
                { "role", "user" },
                { "content", content }
            };

            AddToHistory( toolResultMsg );
        }
    }

    // Clear pending tools
    m_ctx.ClearPendingToolCalls();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();

    // Transition back to idle
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::IDLE );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                             static_cast<int>( m_ctx.GetState() ) ) );
}


void CHAT_CONTROLLER::Retry()
{
    wxLogInfo( "CHAT_CONTROLLER::Retry called" );
    if( m_ctx.GetState() != AgentConversationState::ERROR )
        return;

    m_stopRequested = false;
    m_currentResponse.clear();
    m_thinkingContent.clear();

    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                             static_cast<int>( m_ctx.GetState() ) ) );

    StartLLMRequest();
}


void CHAT_CONTROLLER::NewChat()
{
    wxLogInfo( "CHAT_CONTROLLER::NewChat called" );
    Cancel();

    // Clear all chat state
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_compactionContent.clear();
    m_chatId.clear();
    m_firstUserMessage.clear();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();

    m_ctx.Reset();
}


void CHAT_CONTROLLER::LoadChat( const std::string& aChatId )
{
    wxLogInfo( "CHAT_CONTROLLER::LoadChat called with chatId: %s", aChatId.c_str() );
    if( !m_chatHistoryDb )
    {
        EmitEvent( EVT_CHAT_ERROR, ChatErrorData( "No history database configured", false ) );
        return;
    }

    // Cancel any ongoing operation
    Cancel();

    // Load from database - returns messages array, stores title internally
    nlohmann::json messages = m_chatHistoryDb->Load( aChatId );
    if( messages.is_null() )
    {
        EmitEvent( EVT_CHAT_ERROR, ChatErrorData( "Failed to load chat", false ) );
        return;
    }

    m_chatId = aChatId;
    m_chatHistory = messages;
    m_apiContext = m_chatHistory;  // For now, use same as chat history

    // Title is stored in database object after Load() call
    std::string title = m_chatHistoryDb->GetTitle();

    // Clear first user message to prevent title regeneration
    m_firstUserMessage.clear();

    // Emit loaded event
    EmitEvent( EVT_CHAT_HISTORY_LOADED, ChatHistoryLoadedData( aChatId, title ) );
}


void CHAT_CONTROLLER::SetModel( const std::string& aModel )
{
    wxLogInfo( "CHAT_CONTROLLER::SetModel called with model: %s", aModel.c_str() );
    m_currentModel = aModel;

    if( m_llmClient )
        m_llmClient->SetModel( aModel );
}


// ============================================================================
// Queries (read-only)
// ============================================================================

AgentConversationState CHAT_CONTROLLER::GetState() const
{
    return m_ctx.GetState();
}


bool CHAT_CONTROLLER::CanAcceptInput() const
{
    return m_ctx.CanAcceptUserInput();
}


bool CHAT_CONTROLLER::IsBusy() const
{
    return m_ctx.IsBusy();
}


// ============================================================================
// Event handlers (called by frame, forwarded from LLM/tool events)
// ============================================================================

void CHAT_CONTROLLER::HandleLLMChunk( const LLMStreamChunk& aChunk )
{
    if( m_stopRequested )
        return;

    // Only log significant state changes, not every text/thinking chunk
    switch( aChunk.type )
    {
    case LLMChunkType::THINKING_START:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - THINKING_START" );
        break;
    case LLMChunkType::THINKING_DONE:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - THINKING_DONE" );
        break;
    case LLMChunkType::TOOL_USE_START:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - TOOL_USE_START: %s", aChunk.tool_name.c_str() );
        break;
    case LLMChunkType::TOOL_USE:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - TOOL_USE: %s (id=%s)",
                aChunk.tool_name.c_str(), aChunk.tool_use_id.c_str() );
        break;
    case LLMChunkType::TOOL_USE_DONE:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - TOOL_USE_DONE" );
        break;
    case LLMChunkType::END_TURN:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - END_TURN, response: %s", m_currentResponse.c_str() );
        break;
    case LLMChunkType::MAX_TOKENS:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - MAX_TOKENS (will continue)" );
        break;
    case LLMChunkType::PAUSE_TURN:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - PAUSE_TURN (server tool executing)" );
        break;
    case LLMChunkType::SERVER_TOOL_USE:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - SERVER_TOOL_USE: %s", aChunk.tool_name.c_str() );
        break;
    case LLMChunkType::SERVER_TOOL_RESULT:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - SERVER_TOOL_RESULT" );
        break;
    case LLMChunkType::COMPACTION:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - COMPACTION (context compacted)" );
        break;
    case LLMChunkType::REFUSAL:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - REFUSAL" );
        break;
    case LLMChunkType::ERROR:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - ERROR: %s", aChunk.error_message.c_str() );
        break;
    default:
        break;
    }

    switch( aChunk.type )
    {
    case LLMChunkType::TEXT:
        m_currentResponse += aChunk.text;
        EmitEvent( EVT_CHAT_TEXT_DELTA, ChatTextDeltaData( m_currentResponse, aChunk.text ) );
        break;

    case LLMChunkType::THINKING_START:
        m_thinkingContent.clear();
        EmitEvent( EVT_CHAT_THINKING_START, ChatThinkingStartData( 0 ) );  // Index managed by frame
        break;

    case LLMChunkType::THINKING:
        m_thinkingContent += aChunk.thinking_text;
        EmitEvent( EVT_CHAT_THINKING_DELTA, ChatThinkingDeltaData( m_thinkingContent,
                                                                    wxString::FromUTF8( aChunk.thinking_text ) ) );
        break;

    case LLMChunkType::THINKING_DONE:
        // Store the signature for including in API context
        m_thinkingSignature = aChunk.thinking_signature;
        EmitEvent( EVT_CHAT_THINKING_DONE, ChatThinkingDoneData( m_thinkingContent ) );
        break;

    case LLMChunkType::TOOL_USE_START:
        // Notify UI that a tool is being generated (show tool name while streaming)
        EmitEvent( EVT_CHAT_TOOL_GENERATING,
                   ChatToolGeneratingData( aChunk.tool_use_id, aChunk.tool_name ) );
        break;

    case LLMChunkType::TOOL_USE:
    {
        // Parse tool input from JSON string
        nlohmann::json toolInput;
        try
        {
            toolInput = nlohmann::json::parse( aChunk.tool_input_json );
        }
        catch( ... )
        {
            toolInput = nlohmann::json::object();
        }

        // Queue the tool call
        PendingToolCall toolCall( aChunk.tool_use_id, aChunk.tool_name, toolInput );
        m_ctx.AddPendingToolCall( toolCall );

        // Also add to pending JSON array for history
        m_pendingToolCalls.push_back( {
            { "type", "tool_use" },
            { "id", aChunk.tool_use_id },
            { "name", aChunk.tool_name },
            { "input", toolInput }
        } );
        break;
    }

    case LLMChunkType::TOOL_USE_DONE:
    {
        // All tools parsed, transition to tool-detected state
        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );

        // Add assistant message with tool uses to history
        AddAssistantToolUseToHistory( m_pendingToolCalls );

        // NOTE: Don't clear streaming state here. The frame needs to read currentResponse
        // to render the text one final time before capturing m_htmlBeforeAgentResponse.
        // The frame will call ClearStreamingState() after capturing the HTML.

        // NOTE: Don't start tool execution here - wait for HandleLLMComplete to ensure
        // the curl stream is fully closed before we might need to start a new request.
        break;
    }

    case LLMChunkType::END_TURN:
    {
        // LLM finished without tool calls - add assistant message with response
        // Include thinking, server tool blocks, and text as needed
        if( !m_currentResponse.empty() || !m_thinkingContent.IsEmpty()
            || !m_serverToolBlocks.empty() )
        {
            nlohmann::json content = nlohmann::json::array();

            // Add thinking block first (if present) with signature
            if( !m_thinkingContent.IsEmpty() && !m_thinkingSignature.empty() )
            {
                content.push_back( {
                    { "type", "thinking" },
                    { "thinking", m_thinkingContent.ToStdString() },
                    { "signature", m_thinkingSignature }
                } );
            }

            // Add server tool blocks (server_tool_use, web_search_tool_result, etc.)
            for( const auto& block : m_serverToolBlocks )
            {
                content.push_back( block );
            }

            // Add text content (if present)
            if( !m_currentResponse.empty() )
            {
                content.push_back( {
                    { "type", "text" },
                    { "text", m_currentResponse }
                } );
            }

            nlohmann::json assistantMsg = {
                { "role", "assistant" },
                { "content", content }
            };
            AddToHistory( assistantMsg );
        }

        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::IDLE );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );
        EmitEvent( EVT_CHAT_TURN_COMPLETE, ChatTurnCompleteData( false ) );
        break;
    }

    case LLMChunkType::ERROR:
        HandleLLMError( aChunk.error_message );
        break;

    case LLMChunkType::MAX_TOKENS:
    {
        // Response truncated due to max_tokens limit
        // Save the partial response and continue generation automatically
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - MAX_TOKENS, continuing generation" );

        // Add partial assistant message to context (with thinking, server tool blocks, text)
        if( !m_currentResponse.empty() || !m_thinkingContent.IsEmpty()
            || !m_serverToolBlocks.empty() )
        {
            nlohmann::json content = nlohmann::json::array();

            if( !m_thinkingContent.IsEmpty() && !m_thinkingSignature.empty() )
            {
                content.push_back( {
                    { "type", "thinking" },
                    { "thinking", m_thinkingContent.ToStdString() },
                    { "signature", m_thinkingSignature }
                } );
            }

            // Add server tool blocks (server_tool_use, web_search_tool_result, etc.)
            for( const auto& block : m_serverToolBlocks )
            {
                content.push_back( block );
            }

            if( !m_currentResponse.empty() )
            {
                content.push_back( {
                    { "type", "text" },
                    { "text", m_currentResponse }
                } );
            }

            nlohmann::json assistantMsg = {
                { "role", "assistant" },
                { "content", content }
            };
            AddToHistory( assistantMsg );
        }

        // Add "Please continue" user message to API context only (not display history)
        nlohmann::json continueMsg = {
            { "role", "user" },
            { "content", "Please continue." }
        };
        m_apiContext.push_back( continueMsg );  // API only, not m_chatHistory

        // Emit turn complete with continuing=true so UI finalizes current content
        EmitEvent( EVT_CHAT_TURN_COMPLETE, ChatTurnCompleteData( false, true ) );

        // Set flag to continue after stream completes (can't start new request while current is active)
        m_continueAfterComplete = true;
        break;
    }

    case LLMChunkType::PAUSE_TURN:
    {
        // Server tool paused (e.g., web_search, code_execution)
        // Stream continues automatically after Anthropic executes the server tool
        // Do NOT transition to IDLE - stay in current state
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - PAUSE_TURN (server tool executing, stream will continue)" );
        break;
    }

    case LLMChunkType::SERVER_TOOL_USE:
    {
        // Server-side tool invoked (e.g., web_search) - accumulate for API context
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - SERVER_TOOL_USE: %s", aChunk.tool_name.c_str() );

        if( !aChunk.content_block_json.empty() )
        {
            try
            {
                m_serverToolBlocks.push_back( nlohmann::json::parse( aChunk.content_block_json ) );
            }
            catch( ... )
            {
                wxLogWarning( "CHAT_CONTROLLER - Failed to parse SERVER_TOOL_USE content block" );
            }
        }
        break;
    }

    case LLMChunkType::SERVER_TOOL_RESULT:
    {
        // Server-side tool completed - accumulate for API context
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - SERVER_TOOL_RESULT" );

        if( !aChunk.content_block_json.empty() )
        {
            try
            {
                m_serverToolBlocks.push_back( nlohmann::json::parse( aChunk.content_block_json ) );
            }
            catch( ... )
            {
                wxLogWarning( "CHAT_CONTROLLER - Failed to parse SERVER_TOOL_RESULT content block" );
            }
        }
        break;
    }

    case LLMChunkType::COMPACTION:
    {
        // Context was compacted by the API - store the compaction content
        // and replace old messages in API context
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - COMPACTION received" );

        m_compactionContent = aChunk.compaction_content;

        // Clear all previous messages from API context and replace with compaction
        // The compaction block summarizes the earlier conversation
        m_apiContext = nlohmann::json::array();
        m_apiContext.push_back( {
            { "role", "user" },
            { "content", {
                {
                    { "type", "compaction" },
                    { "content", m_compactionContent }
                }
            } }
        } );
        break;
    }

    case LLMChunkType::REFUSAL:
    {
        // Model refused the request - treat as end of turn with the refusal message
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - REFUSAL" );

        // Add assistant message with refusal content
        if( !m_currentResponse.empty() )
        {
            nlohmann::json content = nlohmann::json::array();
            content.push_back( {
                { "type", "text" },
                { "text", m_currentResponse }
            } );

            nlohmann::json assistantMsg = {
                { "role", "assistant" },
                { "content", content }
            };
            AddToHistory( assistantMsg );
        }

        // Transition to IDLE (same as END_TURN)
        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::IDLE );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );
        EmitEvent( EVT_CHAT_TURN_COMPLETE, ChatTurnCompleteData( false ) );
        break;
    }

    }
}


void CHAT_CONTROLLER::HandleLLMComplete()
{
    // Check if we need to continue generation (from max_tokens)
    if( m_continueAfterComplete )
    {
        m_continueAfterComplete = false;
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMComplete - continuing after max_tokens" );
        ContinueChat();
        return;
    }

    // If tools were detected, now start executing them (stream is safely closed)
    if( m_ctx.GetState() == AgentConversationState::TOOL_USE_DETECTED )
    {
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMComplete - stream complete, starting tool execution" );
        ExecuteNextTool();
        return;
    }

    // Streaming completed successfully - most handling is done in HandleLLMChunk for END_TURN
}


void CHAT_CONTROLLER::HandleLLMError( const std::string& aError )
{
    wxLogInfo( "CHAT_CONTROLLER::HandleLLMError: %s", aError.c_str() );
    AgentConversationState oldState = m_ctx.GetState();

    // Remove orphaned user message if no assistant response was generated yet.
    // This prevents consecutive user messages which violate the Anthropic API format.
    if( m_currentResponse.empty() && m_thinkingContent.IsEmpty() && !m_chatHistory.empty() )
    {
        auto& lastMsg = m_chatHistory.back();
        if( lastMsg.contains( "role" ) && lastMsg["role"] == "user" )
        {
            wxLogInfo( "CHAT_CONTROLLER::HandleLLMError - removing orphaned user message" );
            m_chatHistory.erase( m_chatHistory.end() - 1 );

            // Also remove from API context
            if( !m_apiContext.empty() )
            {
                auto& lastApiMsg = m_apiContext.back();
                if( lastApiMsg.contains( "role" ) && lastApiMsg["role"] == "user" )
                {
                    m_apiContext.erase( m_apiContext.end() - 1 );
                }
            }
        }
    }

    m_ctx.error_message = aError;

    bool canRetry = true;  // Most errors can be retried
    bool isContextError = aError.find( "context" ) != std::string::npos ||
                          aError.find( "token" ) != std::string::npos;

    EmitEvent( EVT_CHAT_ERROR, ChatErrorData( aError, canRetry, isContextError ) );

    // Auto-recover to IDLE state so user can retry immediately.
    // This prevents the chat from becoming stuck in ERROR state.
    m_ctx.SetState( AgentConversationState::IDLE );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );
}


void CHAT_CONTROLLER::HandleToolResult( const std::string& aToolId,
                                         const std::string& aResult,
                                         bool aSuccess )
{
    ProcessToolResult( aToolId, aResult, aSuccess );
}


// ============================================================================
// Internal methods
// ============================================================================

void CHAT_CONTROLLER::ExecuteNextTool()
{
    wxLogInfo( "CHAT_CONTROLLER::ExecuteNextTool called" );
    PendingToolCall* tool = m_ctx.GetNextPendingToolCall();
    if( !tool )
    {
        wxLogInfo( "CHAT_CONTROLLER::ExecuteNextTool - no more tools, continuing chat" );
        ContinueChat();
        return;
    }

    wxLogInfo( "CHAT_CONTROLLER::ExecuteNextTool - executing tool: %s (id=%s)",
               tool->tool_name.c_str(), tool->tool_use_id.c_str() );

    // Mark as executing
    tool->is_executing = true;
    tool->start_time = wxGetUTCTimeMillis();

    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::EXECUTING_TOOL );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );

    // Get tool description
    wxString desc = AgentTools::GetToolDescription( tool->tool_name, tool->tool_input );

    // Emit tool start event
    EmitEvent( EVT_CHAT_TOOL_START, ChatToolStartData( tool->tool_use_id, tool->tool_name,
                                                        desc.ToStdString(), tool->tool_input ) );

    // Handle frame-managed tools specially - they are processed by AGENT_FRAME in OnChatToolStart
    // and call HandleToolResult() when done
    if( tool->tool_name == "open_editor" ||
        tool->tool_name == "close_editor" ||
        tool->tool_name == "check_status" ||
        tool->tool_name == "save" ||
        tool->tool_name == "create_project" )
    {
        // Don't execute synchronously - wait for frame to handle via HandleToolResult
        return;
    }

    // Set project path on tool registry for path validation
    if( m_getProjectPathFn )
    {
        TOOL_REGISTRY::Instance().SetProjectPath( m_getProjectPathFn() );
    }

    // Log project path being used for debugging
    if( m_getProjectPathFn )
    {
        wxLogInfo( "CHAT_CONTROLLER::ExecuteNextTool - project path: %s", m_getProjectPathFn().c_str() );
    }

    // Execute tool with exception handling to prevent stuck state
    std::string result;
    bool success = false;

    try
    {
        // Execute tool - route through TOOL_REGISTRY for file tools, KIWAY for terminal tools
        result = AgentTools::ExecuteToolSync( tool->tool_name, tool->tool_input, m_sendRequestFn );
        success = !result.empty() && result.find( "Error:" ) != 0;
    }
    catch( const std::exception& e )
    {
        wxLogError( "CHAT_CONTROLLER::ExecuteNextTool - exception during tool execution: %s", e.what() );
        result = std::string( "Error: Tool execution failed with exception: " ) + e.what();
        success = false;
    }
    catch( ... )
    {
        wxLogError( "CHAT_CONTROLLER::ExecuteNextTool - unknown exception during tool execution" );
        result = "Error: Tool execution failed with unknown exception";
        success = false;
    }

    // Process the result (logging happens in ProcessToolResult)
    ProcessToolResult( tool->tool_use_id, result, success );
}


void CHAT_CONTROLLER::ProcessToolResult( const std::string& aToolId,
                                          const std::string& aResult,
                                          bool aSuccess )
{
    // Log full result for ALL tool calls
    if( aSuccess )
    {
        wxLogInfo( "CHAT_CONTROLLER::ProcessToolResult - toolId=%s SUCCESS, result_len=%zu",
                   aToolId.c_str(), aResult.length() );
    }
    else
    {
        wxLogError( "CHAT_CONTROLLER::ProcessToolResult - toolId=%s FAILED: %s",
                    aToolId.c_str(), aResult.c_str() );
    }

    // Validate toolId is not empty
    if( aToolId.empty() )
    {
        wxLogWarning( "ProcessToolResult: empty toolId - recovering to IDLE state" );
        // Recover from stuck state
        if( m_ctx.GetState() == AgentConversationState::EXECUTING_TOOL )
        {
            m_ctx.SetState( AgentConversationState::IDLE );
            EmitEvent( EVT_CHAT_STATE_CHANGED,
                       ChatStateChangedData( static_cast<int>( AgentConversationState::EXECUTING_TOOL ),
                                             static_cast<int>( AgentConversationState::IDLE ) ) );
            EmitEvent( EVT_CHAT_ERROR, ChatErrorData( "Tool execution failed: invalid tool ID", true ) );
        }
        return;
    }

    // Find the tool - if not found, this is a stale request after cancellation
    PendingToolCall* tool = m_ctx.FindPendingToolCall( aToolId );
    if( !tool )
    {
        wxLogWarning( "ProcessToolResult: tool %s not found - ignoring stale request",
                      aToolId.c_str() );
        // If we're stuck in EXECUTING_TOOL state with no pending tools, recover
        if( m_ctx.GetState() == AgentConversationState::EXECUTING_TOOL &&
            !m_ctx.HasPendingToolCalls() )
        {
            wxLogWarning( "ProcessToolResult: recovering from stuck EXECUTING_TOOL state" );
            m_ctx.SetState( AgentConversationState::IDLE );
            EmitEvent( EVT_CHAT_STATE_CHANGED,
                       ChatStateChangedData( static_cast<int>( AgentConversationState::EXECUTING_TOOL ),
                                             static_cast<int>( AgentConversationState::IDLE ) ) );
        }
        return;
    }

    std::string toolName = tool->tool_name;

    // Check if result contains Python traceback
    bool isPythonError = aResult.find( "Traceback" ) != std::string::npos;

    // Store result
    AgentConversationContext::ToolResult tr;
    tr.tool_use_id = aToolId;
    tr.tool_name = toolName;
    tr.result = aResult;
    tr.success = aSuccess;
    tr.is_python_error = isPythonError;

    // Check if the result contains image content (from screenshot tool)
    bool hasImage = false;
    std::string imageBase64;
    std::string imageMediaType;

    // Only attempt JSON parse if the result looks like a JSON object
    if( !aResult.empty() && aResult.front() == '{' )
    {
        try
        {
            auto resultJson = nlohmann::json::parse( aResult );

            if( resultJson.value( "__has_image", false ) )
            {
                wxLogInfo( "CHAT_CONTROLLER: Detected image content in tool result for %s",
                           aToolId.c_str() );

                // Build rich content blocks
                AgentConversationContext::ToolResultContentBlock textBlock;
                textBlock.type = AgentConversationContext::ToolResultContentBlock::Type::TEXT;
                textBlock.text = resultJson.value( "text", "" );
                tr.content_blocks.push_back( textBlock );

                if( resultJson.contains( "image" ) )
                {
                    AgentConversationContext::ToolResultContentBlock imgBlock;
                    imgBlock.type = AgentConversationContext::ToolResultContentBlock::Type::IMAGE;
                    imgBlock.media_type = resultJson["image"].value( "media_type", "image/png" );
                    imgBlock.base64_data = resultJson["image"].value( "base64", "" );
                    tr.content_blocks.push_back( imgBlock );
                    tr.has_image_content = true;

                    hasImage = true;
                    imageBase64 = imgBlock.base64_data;
                    imageMediaType = imgBlock.media_type;

                    wxLogInfo( "CHAT_CONTROLLER: Image content block added - media_type=%s, "
                               "base64_len=%zu, content_blocks=%zu",
                               imgBlock.media_type.c_str(), imgBlock.base64_data.length(),
                               tr.content_blocks.size() );
                }

                // Override plain text result for display purposes
                tr.result = resultJson.value( "text", aResult );
            }
        }
        catch( const nlohmann::json::exception& e )
        {
            // Not JSON or no image -- use as-is (backward compatible)
            wxLogInfo( "CHAT_CONTROLLER: Tool result is not image JSON (normal): %s", e.what() );
        }
    }

    m_ctx.completed_tool_results.push_back( tr );

    // Emit tool complete event (with image data if present)
    ChatToolCompleteData completeData( aToolId, toolName, tr.result, aSuccess, isPythonError );
    completeData.hasImage = hasImage;
    completeData.imageBase64 = imageBase64;
    completeData.imageMediaType = imageMediaType;
    EmitEvent( EVT_CHAT_TOOL_COMPLETE, completeData );

    // Remove from pending
    m_ctx.RemovePendingToolCall( aToolId );

    // NOTE: Don't add tool_result to history here. The Anthropic API requires
    // ALL tool_results to be in ONE user message immediately after the assistant
    // message with tool_uses. We collect results in m_ctx.completed_tool_results
    // and add them all at once when all tools are done.

    // Execute next tool or continue chat
    if( m_ctx.HasPendingToolCalls() )
    {
        ExecuteNextTool();
    }
    else
    {
        // All tools complete - add ALL tool results as ONE user message
        AddAllToolResultsToHistory();

        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::PROCESSING_TOOL_RESULT );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );

        ContinueChat();
    }
}


void CHAT_CONTROLLER::AddToHistory( const nlohmann::json& aMessage )
{
    m_chatHistory.push_back( aMessage );
    m_apiContext.push_back( aMessage );
}


void CHAT_CONTROLLER::AddToolResultToHistory( const std::string& aToolUseId,
                                               const std::string& aResult )
{
    // Tool results are added as user messages with tool_result content block
    // NOTE: This method adds a SINGLE tool_result. For multiple tools, use
    // AddAllToolResultsToHistory() which batches all results into one message.
    nlohmann::json toolResultMsg = {
        { "role", "user" },
        { "content", nlohmann::json::array( {
            {
                { "type", "tool_result" },
                { "tool_use_id", aToolUseId },
                { "content", aResult }
            }
        } ) }
    };

    AddToHistory( toolResultMsg );
}


void CHAT_CONTROLLER::AddAllToolResultsToHistory()
{
    // Add ALL completed tool results as ONE user message.
    // The Anthropic API requires all tool_results to be in a single message
    // immediately following the assistant message with tool_uses.
    if( m_ctx.completed_tool_results.empty() )
        return;

    nlohmann::json content = nlohmann::json::array();

    for( const auto& result : m_ctx.completed_tool_results )
    {
        if( result.HasImageContent() )
        {
            // Build array content with text + image blocks for the Anthropic API
            nlohmann::json contentBlocks = nlohmann::json::array();

            for( const auto& block : result.content_blocks )
            {
                if( block.type == AgentConversationContext::ToolResultContentBlock::Type::TEXT )
                {
                    contentBlocks.push_back( {
                        { "type", "text" },
                        { "text", block.text }
                    } );
                }
                else if( block.type == AgentConversationContext::ToolResultContentBlock::Type::IMAGE )
                {
                    contentBlocks.push_back( {
                        { "type", "image" },
                        { "source", {
                            { "type", "base64" },
                            { "media_type", block.media_type },
                            { "data", block.base64_data }
                        } }
                    } );
                }
            }

            content.push_back( {
                { "type", "tool_result" },
                { "tool_use_id", result.tool_use_id },
                { "content", contentBlocks }
            } );

            wxLogInfo( "CHAT_CONTROLLER: Added image tool result to history - "
                       "tool_use_id=%s, content_blocks=%zu",
                       result.tool_use_id.c_str(), contentBlocks.size() );
        }
        else
        {
            // Backward compatible: plain string content
            content.push_back( {
                { "type", "tool_result" },
                { "tool_use_id", result.tool_use_id },
                { "content", result.result }
            } );
        }
    }

    nlohmann::json toolResultMsg = {
        { "role", "user" },
        { "content", content }
    };

    wxLogInfo( "CHAT_CONTROLLER: AddAllToolResultsToHistory - %zu results, "
               "serialized message size: %zu",
               m_ctx.completed_tool_results.size(), toolResultMsg.dump().length() );

    AddToHistory( toolResultMsg );
}


void CHAT_CONTROLLER::RepairHistory()
{
    // Repair history to fix orphaned tool_use/tool_result blocks.
    // The Anthropic API requires:
    // 1. Every tool_use must have a corresponding tool_result in the NEXT message
    // 2. Every tool_result must reference a tool_use in the PREVIOUS message

    if( m_chatHistory.empty() )
        return;

    using json = nlohmann::json;
    bool historyModified = false;

    // PASS 1: Remove orphaned tool_result blocks
    // These are tool_results that don't have a matching tool_use in the previous message
    for( size_t i = 1; i < m_chatHistory.size(); i++ )
    {
        auto& msg = m_chatHistory[i];

        // Only check user messages with array content
        if( !msg.contains( "role" ) || msg["role"] != "user" )
            continue;
        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        // Get valid tool_use IDs from the previous message (if it's an assistant message)
        std::set<std::string> validToolUseIds;
        const auto& prevMsg = m_chatHistory[i - 1];
        if( prevMsg.contains( "role" ) && prevMsg["role"] == "assistant" &&
            prevMsg.contains( "content" ) && prevMsg["content"].is_array() )
        {
            for( const auto& block : prevMsg["content"] )
            {
                if( block.contains( "type" ) && block["type"] == "tool_use" &&
                    block.contains( "id" ) )
                {
                    validToolUseIds.insert( block["id"].get<std::string>() );
                }
            }
        }

        // Filter out orphaned tool_results from this message
        json newContent = json::array();
        size_t removedCount = 0;
        for( const auto& block : msg["content"] )
        {
            bool keep = true;
            if( block.contains( "type" ) && block["type"] == "tool_result" &&
                block.contains( "tool_use_id" ) )
            {
                std::string toolUseId = block["tool_use_id"].get<std::string>();
                if( validToolUseIds.find( toolUseId ) == validToolUseIds.end() )
                {
                    // This tool_result references a tool_use not in the previous message
                    keep = false;
                    removedCount++;
                }
            }
            if( keep )
            {
                newContent.push_back( block );
            }
        }

        if( removedCount > 0 )
        {
            if( newContent.empty() )
            {
                // Message is now empty, mark for removal
                msg["_remove"] = true;
            }
            else
            {
                msg["content"] = newContent;
            }
            historyModified = true;
        }
    }

    // Remove messages marked for removal
    m_chatHistory.erase(
        std::remove_if( m_chatHistory.begin(), m_chatHistory.end(),
            []( const json& msg ) {
                return msg.contains( "_remove" ) && msg["_remove"] == true;
            }),
        m_chatHistory.end() );

    // PASS 2: Add missing tool_result blocks for orphaned tool_uses
    for( size_t i = 0; i < m_chatHistory.size(); i++ )
    {
        const auto& msg = m_chatHistory[i];

        // Only check assistant messages
        if( !msg.contains( "role" ) || msg["role"] != "assistant" )
            continue;
        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        // Collect tool_use IDs from this assistant message
        std::set<std::string> toolUseIds;
        for( const auto& block : msg["content"] )
        {
            if( block.contains( "type" ) && block["type"] == "tool_use" &&
                block.contains( "id" ) )
            {
                toolUseIds.insert( block["id"].get<std::string>() );
            }
        }

        if( toolUseIds.empty() )
            continue;

        // Check if the next message has tool_results for these IDs
        std::set<std::string> foundResultIds;
        bool nextMsgIsToolResultUser = false;
        size_t nextMsgIdx = i + 1;

        if( nextMsgIdx < m_chatHistory.size() )
        {
            const auto& nextMsg = m_chatHistory[nextMsgIdx];
            if( nextMsg.contains( "role" ) && nextMsg["role"] == "user" &&
                nextMsg.contains( "content" ) && nextMsg["content"].is_array() )
            {
                for( const auto& block : nextMsg["content"] )
                {
                    if( block.contains( "type" ) && block["type"] == "tool_result" &&
                        block.contains( "tool_use_id" ) )
                    {
                        foundResultIds.insert( block["tool_use_id"].get<std::string>() );
                        nextMsgIsToolResultUser = true;
                    }
                }
            }
        }

        // Find which tool_use IDs are missing tool_results
        std::vector<std::string> missingIds;
        for( const auto& toolId : toolUseIds )
        {
            if( foundResultIds.find( toolId ) == foundResultIds.end() )
            {
                missingIds.push_back( toolId );
            }
        }

        if( missingIds.empty() )
            continue;

        // If the next message already has tool_results, add the missing ones to it
        // Otherwise, insert a new message
        if( nextMsgIsToolResultUser && nextMsgIdx < m_chatHistory.size() )
        {
            for( const auto& toolId : missingIds )
            {
                m_chatHistory[nextMsgIdx]["content"].push_back( {
                    { "type", "tool_result" },
                    { "tool_use_id", toolId },
                    { "content", "Tool execution was interrupted. No result available." },
                    { "is_error", true }
                });
            }
            historyModified = true;
        }
        else
        {
            json toolResultMsg;
            toolResultMsg["role"] = "user";
            json content = json::array();

            for( const auto& toolId : missingIds )
            {
                content.push_back( {
                    { "type", "tool_result" },
                    { "tool_use_id", toolId },
                    { "content", "Tool execution was interrupted. No result available." },
                    { "is_error", true }
                });
            }

            toolResultMsg["content"] = content;
            m_chatHistory.insert( m_chatHistory.begin() + i + 1, toolResultMsg );
            historyModified = true;

            i++; // Skip the message we just inserted
        }
    }

    if( historyModified )
    {
        // Sync API context after fix
        m_apiContext = m_chatHistory;

        // Save repaired history (chat history DB maintains its own conversation ID)
        if( m_chatHistoryDb )
        {
            m_chatHistoryDb->Save( m_chatHistory );
        }
    }
}


void CHAT_CONTROLLER::AddAssistantToolUseToHistory( const nlohmann::json& aToolUseBlocks )
{
    // Build content array with thinking (if any), text (if any), and tool use blocks
    nlohmann::json content = nlohmann::json::array();

    // Add thinking block first (if present) with signature
    if( !m_thinkingContent.IsEmpty() && !m_thinkingSignature.empty() )
    {
        content.push_back( {
            { "type", "thinking" },
            { "thinking", m_thinkingContent.ToStdString() },
            { "signature", m_thinkingSignature }
        } );
    }

    // Add server tool blocks (server_tool_use, web_search_tool_result, etc.)
    for( const auto& block : m_serverToolBlocks )
    {
        content.push_back( block );
    }

    // Add text block if we have accumulated response
    if( !m_currentResponse.empty() )
    {
        content.push_back( {
            { "type", "text" },
            { "text", m_currentResponse }
        } );
    }

    // Add tool use blocks
    for( const auto& toolUse : aToolUseBlocks )
    {
        content.push_back( toolUse );
    }

    nlohmann::json assistantMsg = {
        { "role", "assistant" },
        { "content", content }
    };

    AddToHistory( assistantMsg );
}


void CHAT_CONTROLLER::SanitizeApiContext()
{
    // Sanitize API context to ensure valid message format for the Anthropic API.
    // This fixes issues like consecutive user messages that can occur after errors.
    if( m_apiContext.empty() )
        return;

    nlohmann::json sanitized = nlohmann::json::array();
    std::string lastRole;

    for( const auto& msg : m_apiContext )
    {
        if( !msg.contains( "role" ) )
            continue;

        std::string role = msg["role"].get<std::string>();

        // Skip consecutive messages with the same role (merge by keeping the later one)
        if( role == lastRole )
        {
            if( role == "user" )
            {
                // Check if the previous user message contains tool_result blocks.
                // tool_result messages MUST stay — they're structurally required to follow
                // the assistant's tool_use blocks. Removing them orphans the tool_use.
                bool prevHasToolResult = false;
                if( !sanitized.empty() )
                {
                    const auto& prevMsg = sanitized.back();
                    if( prevMsg.contains( "content" ) && prevMsg["content"].is_array() )
                    {
                        for( const auto& block : prevMsg["content"] )
                        {
                            if( block.contains( "type" ) && block["type"] == "tool_result" )
                            {
                                prevHasToolResult = true;
                                break;
                            }
                        }
                    }
                }

                if( prevHasToolResult )
                {
                    wxLogInfo( "CHAT_CONTROLLER::SanitizeApiContext - keeping tool_result user message" );
                }
                else
                {
                    // For consecutive user messages, replace the previous one
                    wxLogInfo( "CHAT_CONTROLLER::SanitizeApiContext - removing duplicate user message" );
                    if( !sanitized.empty() )
                        sanitized.erase( sanitized.end() - 1 );
                }
            }
            else if( role == "assistant" )
            {
                // For consecutive assistant messages, keep both (could be continuation)
                // but this shouldn't normally happen
                wxLogWarning( "CHAT_CONTROLLER::SanitizeApiContext - consecutive assistant messages" );
            }
        }

        // Strip __stripped__ attachment blocks from user messages (loaded from history)
        nlohmann::json cleanedMsg = msg;

        if( role == "user" && cleanedMsg.contains( "content" )
            && cleanedMsg["content"].is_array() )
        {
            nlohmann::json cleanedContent = nlohmann::json::array();

            for( const auto& block : cleanedMsg["content"] )
            {
                std::string blockType = block.value( "type", "" );

                if( ( blockType == "image" || blockType == "document" )
                    && block.contains( "source" )
                    && block["source"].value( "data", "" ) == "__stripped__" )
                {
                    continue;  // Drop stripped attachment blocks from API context
                }

                // Handle tool_result blocks with nested stripped images (e.g. screenshots)
                if( blockType == "tool_result"
                    && block.contains( "content" ) && block["content"].is_array() )
                {
                    nlohmann::json cleanedInner = nlohmann::json::array();

                    for( const auto& inner : block["content"] )
                    {
                        std::string innerType = inner.value( "type", "" );

                        if( ( innerType == "image" || innerType == "document" )
                            && inner.contains( "source" )
                            && inner["source"].value( "data", "" ) == "__stripped__" )
                        {
                            continue;  // Drop stripped image blocks from tool results
                        }

                        cleanedInner.push_back( inner );
                    }

                    nlohmann::json cleanedBlock = block;
                    cleanedBlock["content"] = cleanedInner;
                    cleanedContent.push_back( cleanedBlock );
                    continue;
                }

                cleanedContent.push_back( block );
            }

            if( cleanedContent.empty() )
                continue;  // Skip entirely empty user messages

            cleanedMsg["content"] = cleanedContent;

            // If only a single text block remains, simplify to string content
            if( cleanedContent.size() == 1 && cleanedContent[0].value( "type", "" ) == "text" )
                cleanedMsg["content"] = cleanedContent[0].value( "text", "" );
        }

        sanitized.push_back( cleanedMsg );
        lastRole = role;
    }

    // Ensure conversation doesn't end with orphaned assistant tool_use without tool_result
    // (This is handled by RepairHistory, but double-check here)

    if( sanitized != m_apiContext )
    {
        wxLogInfo( "CHAT_CONTROLLER::SanitizeApiContext - context was sanitized" );
        m_apiContext = sanitized;
    }
}


std::vector<LLM_TOOL> CHAT_CONTROLLER::GetFilteredTools() const
{
    if( m_agentMode == AgentMode::EXECUTE )
        return m_tools;

    // Plan mode: return only read-only tools
    std::vector<LLM_TOOL> filtered;
    std::copy_if( m_tools.begin(), m_tools.end(), std::back_inserter( filtered ),
                  []( const LLM_TOOL& t ) { return t.read_only; } );
    return filtered;
}


void CHAT_CONTROLLER::StartLLMRequest()
{
    wxLogInfo( "CHAT_CONTROLLER::StartLLMRequest called" );
    if( !m_llmClient )
    {
        HandleLLMError( "No LLM client configured" );
        return;
    }

    m_stopRequested = false;

    // Sanitize context before sending to ensure valid message format
    SanitizeApiContext();

    // System prompt now handled server-side
    // Start async request - events will be forwarded to HandleLLMChunk
    // In plan mode, only read-only tools are sent to the LLM
    bool started = m_llmClient->AskStreamWithToolsAsync( m_apiContext, GetFilteredTools(), m_eventSink );
    if( !started )
    {
        HandleLLMError( "Failed to start LLM request" );
    }
}


void CHAT_CONTROLLER::ContinueChat()
{
    wxLogInfo( "CHAT_CONTROLLER::ContinueChat called - starting next LLM turn" );
    // Reset for next turn
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_thinkingSignature.clear();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();
    m_ctx.completed_tool_results.clear();

    // Transition to waiting for LLM
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );

    // Start another LLM request to continue
    StartLLMRequest();
}


void CHAT_CONTROLLER::GenerateTitle()
{
    wxLogInfo( "CHAT_CONTROLLER::GenerateTitle called" );
    if( m_firstUserMessage.empty() )
        return;

    if( !m_auth )
        return;

    wxLogInfo( "CHAT_CONTROLLER::GenerateTitle - generating title for: %s", m_firstUserMessage.c_str() );
    // Capture first user message for thread
    std::string message = m_firstUserMessage;

    // Use background thread to avoid blocking
    std::thread( [this, message]() {
        try
        {
            // Check authentication
            std::string accessToken = m_auth->GetAccessToken();
            if( accessToken.empty() )
                return;

            // Setup HTTP request to title endpoint
            KICAD_CURL_EASY curl;
            curl.SetURL( ZENER_BASE_URL + "/api/llm/title" );
            curl.SetHeader( "Content-Type", "application/json" );
            curl.SetHeader( "Authorization", "Bearer " + accessToken );

            // Build request body
            nlohmann::json requestBody;
            requestBody["message"] = message;
            std::string jsonStr = requestBody.dump();
            curl.SetPostFields( jsonStr );

            // Perform request
            curl.Perform();
            long httpCode = curl.GetResponseStatusCode();

            if( httpCode == 200 )
            {
                // Parse response
                auto response = nlohmann::json::parse( curl.GetBuffer() );
                std::string title = response.value( "title", "" );

                // Stream title character by character for animation effect
                if( !title.empty() )
                {
                    std::string partial;
                    for( size_t i = 0; i < title.size(); i++ )
                    {
                        partial += title[i];
                        EmitEvent( EVT_CHAT_TITLE_DELTA, ChatTitleDeltaData( partial ) );
                        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
                    }

                    // Emit final title for persistence
                    EmitEvent( EVT_CHAT_TITLE_GENERATED, ChatTitleGeneratedData( title ) );
                }
            }
        }
        catch( ... )
        {
            // Silently ignore title generation errors
        }
    }).detach();
}


// ============================================================================
// Event emission helpers
// ============================================================================

template<typename T>
void CHAT_CONTROLLER::EmitEvent( wxEventType aType, const T& aData )
{
    PostChatEvent( m_eventSink, aType, aData );
}


void CHAT_CONTROLLER::EmitEvent( wxEventType aType )
{
    PostChatEvent( m_eventSink, aType );
}

// Explicit template instantiations
template void CHAT_CONTROLLER::EmitEvent<ChatTextDeltaData>( wxEventType, const ChatTextDeltaData& );
template void CHAT_CONTROLLER::EmitEvent<ChatThinkingDeltaData>( wxEventType, const ChatThinkingDeltaData& );
template void CHAT_CONTROLLER::EmitEvent<ChatThinkingStartData>( wxEventType, const ChatThinkingStartData& );
template void CHAT_CONTROLLER::EmitEvent<ChatThinkingDoneData>( wxEventType, const ChatThinkingDoneData& );
template void CHAT_CONTROLLER::EmitEvent<ChatToolStartData>( wxEventType, const ChatToolStartData& );
template void CHAT_CONTROLLER::EmitEvent<ChatToolCompleteData>( wxEventType, const ChatToolCompleteData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTurnCompleteData>( wxEventType, const ChatTurnCompleteData& );
template void CHAT_CONTROLLER::EmitEvent<ChatErrorData>( wxEventType, const ChatErrorData& );
template void CHAT_CONTROLLER::EmitEvent<ChatStateChangedData>( wxEventType, const ChatStateChangedData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTitleDeltaData>( wxEventType, const ChatTitleDeltaData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTitleGeneratedData>( wxEventType, const ChatTitleGeneratedData& );
template void CHAT_CONTROLLER::EmitEvent<ChatHistoryLoadedData>( wxEventType, const ChatHistoryLoadedData& );
