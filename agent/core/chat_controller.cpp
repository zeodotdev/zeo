#include "chat_controller.h"
#include "chat_events.h"
#include "../tools/tool_schemas.h"
#include "../tools/tool_registry.h"
#include "../tools/handlers/datasheet_handler.h"
#include "view/file_attach.h"
#include "agent_llm_client.h"
#include "agent_chat_history.h"
#include <zeo/agent_auth.h>
#include "../cloud/agent_cloud_sync.h"
#include "agent_monitor_log.h"

#include <algorithm>
#include <chrono>
#include <map>
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
wxDEFINE_EVENT( EVT_CHAT_COMPACTION, wxThreadEvent );


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
    m_tools = ToolSchemas::GetToolDefinitions();

    // Initialize chat history as empty array
    m_chatHistory = nlohmann::json::array();
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

    // Inject user edit context for follow-up messages
    if( userMessageCount > 0 )
    {
        std::string editsSummary = GetUserEditsSummary();
        if( !editsSummary.empty() )
        {
            messageText = "<schematic_changes_by_user>\nThe user made the following changes to the schematic since your last response:\n"
                          + editsSummary
                          + "</schematic_changes_by_user>\n\n" + messageText;

            wxLogInfo( "CHAT: Injecting user edits context into message:\n%s",
                       messageText.c_str() );
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
    wxLogInfo( "CHAT_CONTROLLER::Cancel called, state=%d, responseLen=%zu, thinkingLen=%zu, chatId=%s",
               static_cast<int>( m_ctx.GetState() ), m_currentResponse.size(),
               static_cast<size_t>( m_thinkingContent.length() ), m_chatId.c_str() );
    m_stopRequested = true;
    m_continueAfterComplete = false;

    if( m_llmClient )
        m_llmClient->CancelRequest();

    // If we're IDLE with no partial content, there's nothing to cancel or preserve.
    // This avoids adding spurious "*(Stopped)*" markers when switching chats.
    if( m_ctx.GetState() == AgentConversationState::IDLE
        && m_currentResponse.empty()
        && m_thinkingContent.IsEmpty() )
    {
        wxLogInfo( "CHAT_CONTROLLER::Cancel - IDLE with no partial content, skipping" );
        return;
    }

    // Save any partial response that was being streamed.
    // Skip this if we're in a tool execution state — the assistant message (with tool_use blocks)
    // was already committed to history by AddAssistantToolUseToHistory(). The stale
    // m_currentResponse/m_thinkingContent from that turn would create a duplicate assistant message.
    bool toolStateActive = m_ctx.GetState() == AgentConversationState::TOOL_USE_DETECTED ||
                           m_ctx.GetState() == AgentConversationState::EXECUTING_TOOL ||
                           m_ctx.GetState() == AgentConversationState::PROCESSING_TOOL_RESULT;

    wxLogInfo( "CHAT_CONTROLLER::Cancel toolStateActive=%d, hasResponse=%d, hasThinking=%d",
               toolStateActive,
               !m_currentResponse.empty(), !m_thinkingContent.IsEmpty() );

    if( !toolStateActive )
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

        // If content is empty (e.g. cancelled during compaction before any response,
        // or cancelled during thinking before signature arrived), add a minimal text
        // block. This prevents consecutive user messages (e.g. compaction marker
        // followed by next user message) which the API rejects.
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
        wxLogInfo( "CHAT_CONTROLLER::Cancel - added partial response to history, historySize=%zu",
                   m_chatHistory.size() );

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
    if( m_ctx.GetState() != AgentConversationState::ERRORED )
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
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_chatId.clear();
    m_firstUserMessage.clear();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();
    m_schematicSnapshot.clear();
    m_vcsNotified = false;

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

    // Cancel any ongoing operation (adds partial response to m_chatHistory if streaming)
    size_t historyBefore = m_chatHistory.size();
    wxLogInfo( "CHAT_CONTROLLER::LoadChat - about to Cancel, oldChatId=%s, historySize=%zu",
               m_chatId.c_str(), historyBefore );
    Cancel();

    // Save old chat's history only if Cancel() actually added a partial response.
    // This avoids re-saving unmodified chats (which would update their timestamp).
    if( !m_chatId.empty() && m_chatHistory.size() > historyBefore )
    {
        wxLogInfo( "CHAT_CONTROLLER::LoadChat - Cancel added content, saving old chat=%s, "
                   "historySize=%zu", m_chatId.c_str(), m_chatHistory.size() );
        m_chatHistoryDb->Save( m_chatHistory );
    }

    // Clear streaming state that Cancel() preserved for frame finalization.
    // We're switching chats, not going through the normal OnStop() path.
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_thinkingSignature.clear();
    m_pendingToolCalls = nlohmann::json::array();
    m_serverToolBlocks = nlohmann::json::array();

    // Load from database - returns messages array, stores title internally
    wxLogInfo( "CHAT_CONTROLLER::LoadChat - loading new chat=%s", aChatId.c_str() );
    nlohmann::json messages = m_chatHistoryDb->Load( aChatId );
    if( messages.is_null() )
    {
        EmitEvent( EVT_CHAT_ERROR, ChatErrorData( "Failed to load chat", false ) );
        return;
    }

    m_chatId = aChatId;
    m_chatHistory = messages;

    // Repair any structural issues in loaded history (e.g., orphaned tool_use
    // blocks from previous errors). This fixes corrupted chats on load.
    RepairHistory();

    // Title is stored in database object after Load() call
    std::string title = m_chatHistoryDb->GetTitle();

    // Clear first user message to prevent title regeneration
    m_firstUserMessage.clear();

    // Emit loaded event
    EmitEvent( EVT_CHAT_HISTORY_LOADED, ChatHistoryLoadedData( aChatId, title ) );
}


void CHAT_CONTROLLER::SaveStreamingSnapshot()
{
    if( !m_chatHistoryDb || m_chatId.empty() )
        return;

    // Build a snapshot: committed history + any in-flight streaming content
    nlohmann::json snapshot = m_chatHistory;
    bool appendedStreaming = false;

    // Append current streaming response as a temporary assistant message
    if( m_ctx.GetState() == AgentConversationState::WAITING_FOR_LLM )
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

        if( !m_currentResponse.empty() )
        {
            content.push_back( {
                { "type", "text" },
                { "text", m_currentResponse }
            } );
        }

        if( !content.empty() )
        {
            snapshot.push_back( {
                { "role", "assistant" },
                { "content", content }
            } );
            appendedStreaming = true;
        }
    }

    wxLogInfo( "CHAT_CONTROLLER::SaveStreamingSnapshot - chatId=%s, historyMsgs=%zu, "
               "appendedStreaming=%d, responseLen=%zu",
               m_chatId.c_str(), m_chatHistory.size(), appendedStreaming,
               m_currentResponse.size() );

    m_chatHistoryDb->Save( snapshot );
    m_lastSnapshotTime = std::chrono::steady_clock::now();
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
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - THINKING_DONE (%zu chars):\n%s",
                   aChunk.thinking_text.size(), aChunk.thinking_text.c_str() );
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
    case LLMChunkType::COMPACTION_START:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - COMPACTION_START" );
        break;
    case LLMChunkType::COMPACTION:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - COMPACTION (context compacted)" );
        break;
    case LLMChunkType::REFUSAL:
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - REFUSAL" );
        break;
    case LLMChunkType::ERRORED:
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

        // Periodic snapshot for crash protection (every 5 seconds)
        {
            auto now = std::chrono::steady_clock::now();
            if( now - m_lastSnapshotTime > std::chrono::seconds( 5 ) )
                SaveStreamingSnapshot();
        }
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

    case LLMChunkType::ERRORED:
        HandleLLMError( aChunk.error_message, 0, aChunk.error_type );
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

        // Add "Please continue" user message to history
        nlohmann::json continueMsg = {
            { "role", "user" },
            { "content", "Please continue." }
        };
        AddToHistory( continueMsg );

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

    case LLMChunkType::COMPACTION_START:
    {
        // Compaction block started — notify UI immediately so it shows
        // "Compacting..." while the content streams in.
        EmitEvent( EVT_CHAT_COMPACTION );
        break;
    }

    case LLMChunkType::COMPACTION:
    {
        // Context was compacted by the API. Insert a marker message into
        // m_chatHistory so that BuildApiContext() knows to skip everything
        // before it. The marker doubles as the API's replacement context.
        wxLogInfo( "CHAT_CONTROLLER::HandleLLMChunk - COMPACTION received, "
                   "inserting marker at index %zu", m_chatHistory.size() );

        nlohmann::json compactionMsg = {
            { "role", "user" },
            { "content", nlohmann::json::array( {
                { { "type", "text" },
                  { "text", aChunk.compaction_content },
                  { "cache_control", { { "type", "ephemeral" } } } }
            } ) },
            { "_compaction", true }
        };
        m_chatHistory.push_back( compactionMsg );
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
        ExecuteAllTools();
        return;
    }

    // Streaming completed successfully - most handling is done in HandleLLMChunk for END_TURN
}


void CHAT_CONTROLLER::HandleLLMError( const std::string& aError, long aHttpCode,
                                      const std::string& aErrorType )
{
    wxLogInfo( "CHAT_CONTROLLER::HandleLLMError: http=%ld type=%s msg=%s",
               aHttpCode, aErrorType.c_str(), aError.c_str() );
    AgentConversationState oldState = m_ctx.GetState();

    // Remove orphaned user message if no assistant response was generated yet.
    // This prevents consecutive user messages which violate the Anthropic API format.
    if( m_currentResponse.empty() && m_thinkingContent.IsEmpty() && !m_chatHistory.empty() )
    {
        auto& lastMsg = m_chatHistory.back();
        if( lastMsg.contains( "role" ) && lastMsg["role"] == "user" )
        {
            // Check if this user message contains tool_result blocks.
            // tool_result messages are structurally required — they must follow
            // the assistant's tool_use blocks. Removing them corrupts history.
            bool hasToolResult = false;

            if( lastMsg.contains( "content" ) && lastMsg["content"].is_array() )
            {
                for( const auto& block : lastMsg["content"] )
                {
                    if( block.contains( "type" ) && block["type"] == "tool_result" )
                    {
                        hasToolResult = true;
                        break;
                    }
                }
            }

            if( hasToolResult )
            {
                wxLogInfo( "CHAT_CONTROLLER::HandleLLMError - keeping tool_result user message "
                           "(structurally required)" );
            }
            else
            {
                wxLogInfo( "CHAT_CONTROLLER::HandleLLMError - removing orphaned user message" );
                m_chatHistory.erase( m_chatHistory.end() - 1 );

                // Persist the removal — the frame already saved to disk before the
                // API response arrived, so without this the orphaned message reappears
                // when the chat is reloaded.
                if( m_chatHistoryDb && !m_chatId.empty() )
                    m_chatHistoryDb->Save( m_chatHistory );
            }
        }
    }

    m_ctx.error_message = aError;

    bool canRetry = true;  // Most errors can be retried
    bool isContextError = aError.find( "context" ) != std::string::npos ||
                          aError.find( "token" ) != std::string::npos;

    EmitEvent( EVT_CHAT_ERROR, ChatErrorData( aError, canRetry, isContextError,
                                              aHttpCode, aErrorType ) );

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

void CHAT_CONTROLLER::EmitToolStart( const std::string& aToolId, const std::string& aToolName,
                                      const nlohmann::json& aInput )
{
    wxLogInfo( "CHAT_CONTROLLER::EmitToolStart - tool: %s (id=%s) input=%s",
               aToolName.c_str(), aToolId.c_str(), aInput.dump().c_str() );

    std::string desc = TOOL_REGISTRY::Instance().GetDescription( aToolName, aInput );
    EmitEvent( EVT_CHAT_TOOL_START, ChatToolStartData( aToolId, aToolName, desc, aInput ) );
    AGENT_MONITOR_LOG::Instance().LogToolStart( aToolId, aToolName, desc, aInput.dump() );
}


void CHAT_CONTROLLER::ExecuteAllTools()
{
    wxLogInfo( "CHAT_CONTROLLER::ExecuteAllTools called" );

    auto allTools = m_ctx.GetAllPendingToolCalls();
    if( allTools.empty() )
    {
        wxLogInfo( "CHAT_CONTROLLER::ExecuteAllTools - no tools, continuing chat" );
        ContinueChat();
        return;
    }

    // Sync editor state and IPC function once before launching all tools
    if( m_syncEditorStateFn )
        m_syncEditorStateFn();

    TOOL_REGISTRY::Instance().SetSendRequestFn( m_sendRequestFn );

    // Transition to EXECUTING_TOOL once for the batch
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::EXECUTING_TOOL );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );

    wxLogInfo( "CHAT_CONTROLLER::ExecuteAllTools - %zu tools", allTools.size() );

    // ── Classify and launch tools ──
    //
    // IPC tool data is copied by value into a local vector.  Pointers into
    // m_pendingTools become dangling when RemovePendingToolCall() erases
    // vector elements during wxYield() event processing.
    struct ToolSnapshot
    {
        std::string    id;
        std::string    name;
        nlohmann::json input;
    };

    std::vector<ToolSnapshot> ipcTools;
    auto& reg = TOOL_REGISTRY::Instance();

    for( PendingToolCall* tool : allTools )
    {
        // Frame-managed tools require user interaction (approval dialogs).
        // Left in m_pendingTools for ExecuteDeferredFrameTool() to pick up
        // after all parallel/IPC tools finish.
        if( tool->tool_name == "open_editor" || tool->tool_name == "sch_run_erc" )
        {
            wxLogInfo( "CHAT_CONTROLLER::ExecuteAllTools - deferring frame-managed tool: %s (id=%s)",
                       tool->tool_name.c_str(), tool->tool_use_id.c_str() );
            continue;
        }

        // IPC tools must run on the main thread (SendRequest uses wxYield).
        // Collect by value for the sequential pass below.
        if( reg.HasHandler( tool->tool_name ) && reg.RequiresIPC( tool->tool_name ) )
        {
            ipcTools.push_back( { tool->tool_use_id, tool->tool_name, tool->tool_input } );
            continue;
        }

        // Start threaded / async tools immediately
        tool->is_executing = true;
        tool->start_time = wxGetUTCTimeMillis();
        EmitToolStart( tool->tool_use_id, tool->tool_name, tool->tool_input );

        if( reg.HasHandler( tool->tool_name ) && reg.IsAsync( tool->tool_name ) )
        {
            wxLogInfo( "CHAT_CONTROLLER::ExecuteAllTools - async tool: %s", tool->tool_name.c_str() );
            reg.ExecuteAsync( tool->tool_name, tool->tool_input, tool->tool_use_id, m_eventSink );
            continue;
        }

        // Threaded: capture data by value, post result when done
        std::string toolId = tool->tool_use_id;
        std::string toolName = tool->tool_name;
        nlohmann::json toolInput = tool->tool_input;
        wxEvtHandler* sink = m_eventSink;

        std::thread( [toolId, toolName, toolInput, sink]()
        {
            std::string result;
            bool success = false;

            try
            {
                result = TOOL_REGISTRY::Instance().ExecuteToolSync( toolName, toolInput );
                success = !result.empty() && result.find( "Error:" ) != 0;
            }
            catch( const std::exception& e )
            {
                result = std::string( "Error: Tool execution failed: " ) + e.what();
            }
            catch( ... )
            {
                result = "Error: Tool execution failed with unknown exception";
            }

            ToolExecutionResult tr;
            tr.tool_use_id = toolId;
            tr.tool_name = toolName;
            tr.result = result;
            tr.success = success;
            PostToolResult( sink, tr );
        } ).detach();
    }

    // ── IPC tools: run sequentially on the main thread ──
    //
    // Pre-mark all IPC tools as executing so that ProcessToolResult —
    // which may fire re-entrantly during wxYield when threaded tool results
    // arrive — sees them via GetExecutingToolCall() and doesn't prematurely
    // start deferred frame-managed tools.
    for( const auto& t : ipcTools )
    {
        PendingToolCall* orig = m_ctx.FindPendingToolCall( t.id );
        if( orig )
        {
            orig->is_executing = true;
            orig->start_time = wxGetUTCTimeMillis();
        }
    }

    for( const auto& t : ipcTools )
    {
        EmitToolStart( t.id, t.name, t.input );

        // Flush UI events so the tool status dot appears before we block.
        // May process threaded tool results, but we use our local copy.
        wxYield();

        std::string result;
        bool success = false;

        try
        {
            result = reg.ExecuteToolSync( t.name, t.input );
            success = !result.empty() && result.find( "Error:" ) != 0;
        }
        catch( const std::exception& e )
        {
            result = std::string( "Error: Tool execution failed: " ) + e.what();
        }
        catch( ... )
        {
            result = "Error: Tool execution failed with unknown exception";
        }

        ProcessToolResult( t.id, result, success );
    }

    // Start first deferred frame-managed tool if nothing else is still running
    if( !m_ctx.GetExecutingToolCall() && m_ctx.HasPendingToolCalls() )
        ExecuteDeferredFrameTool();
}


void CHAT_CONTROLLER::ExecuteDeferredFrameTool()
{
    PendingToolCall* tool = m_ctx.GetNextPendingToolCall();
    if( !tool )
    {
        ContinueChat();
        return;
    }

    wxLogInfo( "CHAT_CONTROLLER::ExecuteDeferredFrameTool - %s (id=%s)",
               tool->tool_name.c_str(), tool->tool_use_id.c_str() );

    tool->is_executing = true;
    tool->start_time = wxGetUTCTimeMillis();
    EmitToolStart( tool->tool_use_id, tool->tool_name, tool->tool_input );

    // Frame handles this tool and calls HandleToolResult() when done
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

            if( resultJson.contains( "image" ) )
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
                    // Low-res image for the LLM API context
                    AgentConversationContext::ToolResultContentBlock imgBlock;
                    imgBlock.type = AgentConversationContext::ToolResultContentBlock::Type::IMAGE;
                    imgBlock.media_type = resultJson["image"].value( "media_type", "image/png" );
                    imgBlock.base64_data = resultJson["image"].value( "base64", "" );
                    tr.content_blocks.push_back( imgBlock );
                    tr.has_image_content = true;

                    hasImage = true;
                    imageMediaType = imgBlock.media_type;

                    // Use high-res display_image for the chat UI if available,
                    // otherwise fall back to the API image
                    if( resultJson.contains( "display_image" ) )
                    {
                        imageBase64 = resultJson["display_image"].value( "base64", "" );
                        imageMediaType = resultJson["display_image"].value( "media_type",
                                                                           imageMediaType );
                    }
                    else
                    {
                        imageBase64 = imgBlock.base64_data;
                    }

                    wxLogInfo( "CHAT_CONTROLLER: Image content block added - "
                               "api_b64_len=%zu, display_b64_len=%zu, content_blocks=%zu",
                               imgBlock.base64_data.length(), imageBase64.length(),
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

    // Log tool end to monitor log
    long durationMs = static_cast<long>( ( wxGetUTCTimeMillis() - tool->start_time ).GetValue() );
    AGENT_MONITOR_LOG::Instance().LogToolEnd( aToolId, toolName, tr.result, aSuccess, durationMs );

    // Track tool duration for metrics
    if( m_llmClient )
    {
        m_llmClient->AddToolDuration( aToolId, static_cast<int>( durationMs ) );
    }

    // Notify VCS after a successful write tool so it can auto-init git if needed.
    // Only fires once per chat session to avoid repeated checks.
    if( aSuccess && !m_vcsNotified && m_vcsNotifyFn )
    {
        bool isWriteTool = true;
        for( const auto& t : m_tools )
        {
            if( t.name == toolName )
            {
                isWriteTool = !t.read_only;
                break;
            }
        }

        if( isWriteTool )
        {
            m_vcsNotified = true;
            m_vcsNotifyFn();
        }
    }

    // Background datasheet extraction for symbols with datasheet URLs
    if( aSuccess && ( toolName == "sch_add"
                      || toolName == "sch_get_summary"
                      || toolName == "sch_inspect" ) )
    {
        try
        {
            DATASHEET_HANDLER::MaybeTriggerExtraction( toolName, aResult );
        }
        catch( ... )
        {
            wxLogTrace( "Agent", "DATASHEET_HANDLER: Exception in MaybeTriggerExtraction" );
        }
    }

    // Remove from pending
    m_ctx.RemovePendingToolCall( aToolId );

    // NOTE: Don't add tool_result to history here. The Anthropic API requires
    // ALL tool_results to be in ONE user message immediately after the assistant
    // message with tool_uses. We collect results in m_ctx.completed_tool_results
    // and add them all at once when all tools are done.

    // If more tools are still pending, check whether they're running or deferred
    if( m_ctx.HasPendingToolCalls() )
    {
        // If there's at least one executing tool, wait for it
        if( m_ctx.GetExecutingToolCall() )
        {
            wxLogInfo( "CHAT_CONTROLLER::ProcessToolResult - %zu tools still pending, waiting",
                       m_ctx.GetPendingToolCallCount() );
            return;
        }

        // All executing tools done but deferred frame-managed tools remain — start next one
        wxLogInfo( "CHAT_CONTROLLER::ProcessToolResult - parallel tools done, "
                   "starting deferred frame-managed tool" );
        ExecuteDeferredFrameTool();
        return;
    }

    // All tools complete - add ALL tool results as ONE user message
    AddAllToolResultsToHistory();

    // If the frame has a queued user message, interrupt the auto-continue loop
    // so the queued message gets sent between tool rounds (like Claude Code).
    if( m_hasQueuedMessageFn && m_hasQueuedMessageFn() )
    {
        wxLogInfo( "CHAT_CONTROLLER::ProcessToolResult - queued message detected, "
                   "yielding to frame instead of auto-continuing" );
        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::IDLE );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );
        EmitEvent( EVT_CHAT_TURN_COMPLETE, ChatTurnCompleteData( false ) );
        return;
    }

    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::PROCESSING_TOOL_RESULT );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );

    ContinueChat();
}


void CHAT_CONTROLLER::AddToHistory( const nlohmann::json& aMessage )
{
    wxLogInfo( "CHAT_CONTROLLER::AddToHistory [%zu] %s",
               m_chatHistory.size(), aMessage.dump().c_str() );
    m_chatHistory.push_back( aMessage );
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


bool CHAT_CONTROLLER::RepairMessageArray( nlohmann::json& messages )
{
    // Repair a message array to fix orphaned tool_use/tool_result blocks.
    // The Anthropic API requires:
    // 1. Every tool_use must have a corresponding tool_result in the NEXT message
    // 2. Every tool_result must reference a tool_use in the PREVIOUS message
    // 3. Server-side tools (web_search, etc.) must have both server_tool_use AND
    //    their result (web_search_tool_result) in the SAME assistant message

    if( messages.empty() )
        return false;

    using json = nlohmann::json;
    bool modified = false;

    // PASS 0: Remove orphaned server_tool_use blocks
    // Server-side tools (e.g., web_search) require both the server_tool_use and its
    // result (e.g., web_search_tool_result) in the same assistant message.
    // If a stream was interrupted, we might have server_tool_use without its result.
    for( auto& msg : messages )
    {
        if( !msg.contains( "role" ) || msg["role"] != "assistant" )
            continue;
        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        // Collect server_tool_use IDs and their expected result types
        // Format: { id -> expected_result_type }
        // e.g., "web_search" server_tool_use expects "web_search_tool_result"
        std::map<std::string, std::string> serverToolUseIds;
        for( const auto& block : msg["content"] )
        {
            if( block.contains( "type" ) && block["type"] == "server_tool_use" &&
                block.contains( "id" ) && block.contains( "name" ) )
            {
                std::string id = block["id"].get<std::string>();
                std::string name = block["name"].get<std::string>();
                // Expected result type is "<name>_tool_result"
                serverToolUseIds[id] = name + "_tool_result";
            }
        }

        if( serverToolUseIds.empty() )
            continue;

        // Check which server_tool_use IDs have their corresponding results
        std::set<std::string> foundResultIds;
        for( const auto& block : msg["content"] )
        {
            if( !block.contains( "type" ) )
                continue;

            std::string blockType = block["type"].get<std::string>();

            // Check if this is a server tool result (ends with "_tool_result")
            if( blockType.length() > 12 &&
                blockType.substr( blockType.length() - 12 ) == "_tool_result" &&
                block.contains( "tool_use_id" ) )
            {
                foundResultIds.insert( block["tool_use_id"].get<std::string>() );
            }
        }

        // Filter out orphaned server_tool_use blocks
        json newContent = json::array();
        size_t removedCount = 0;
        for( const auto& block : msg["content"] )
        {
            bool keep = true;
            if( block.contains( "type" ) && block["type"] == "server_tool_use" &&
                block.contains( "id" ) )
            {
                std::string id = block["id"].get<std::string>();
                if( foundResultIds.find( id ) == foundResultIds.end() )
                {
                    // This server_tool_use has no result - remove it
                    keep = false;
                    removedCount++;
                    wxLogInfo( "CHAT_CONTROLLER::RepairMessageArray - removing orphaned "
                               "server_tool_use id=%s", id.c_str() );
                }
            }
            if( keep )
            {
                newContent.push_back( block );
            }
        }

        if( removedCount > 0 )
        {
            msg["content"] = newContent;
            modified = true;
        }
    }

    // PASS 1: Remove orphaned tool_result blocks
    // These are tool_results that don't have a matching tool_use in the previous message
    for( size_t i = 1; i < messages.size(); i++ )
    {
        auto& msg = messages[i];

        // Only check user messages with array content
        if( !msg.contains( "role" ) || msg["role"] != "user" )
            continue;
        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        // Get valid tool_use IDs from the previous message (if it's an assistant message)
        std::set<std::string> validToolUseIds;
        const auto& prevMsg = messages[i - 1];
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
            modified = true;
        }
    }

    // Remove messages marked for removal
    messages.erase(
        std::remove_if( messages.begin(), messages.end(),
            []( const json& msg ) {
                return msg.contains( "_remove" ) && msg["_remove"] == true;
            }),
        messages.end() );

    // PASS 2: Add missing tool_result blocks for orphaned tool_uses
    for( size_t i = 0; i < messages.size(); i++ )
    {
        const auto& msg = messages[i];

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

        if( nextMsgIdx < messages.size() )
        {
            const auto& nextMsg = messages[nextMsgIdx];
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
        if( nextMsgIsToolResultUser && nextMsgIdx < messages.size() )
        {
            for( const auto& toolId : missingIds )
            {
                messages[nextMsgIdx]["content"].push_back( {
                    { "type", "tool_result" },
                    { "tool_use_id", toolId },
                    { "content", "Tool execution was interrupted. No result available." },
                    { "is_error", true }
                });
            }
            modified = true;
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
            messages.insert( messages.begin() + i + 1, toolResultMsg );
            modified = true;

            i++; // Skip the message we just inserted
        }
    }

    // PASS 3: Merge consecutive user messages.
    // The Anthropic API requires strict user/assistant alternation.
    // Consecutive user messages can accumulate when HandleLLMError removes orphaned
    // messages but doesn't persist the removal, or when the app crashes mid-conversation.
    // We merge them into one message by combining their content arrays.
    for( size_t i = 1; i < messages.size(); /* no increment */ )
    {
        auto& prev = messages[i - 1];
        auto& curr = messages[i];

        if( !prev.contains( "role" ) || !curr.contains( "role" ) ||
            prev["role"] != "user" || curr["role"] != "user" )
        {
            i++;
            continue;
        }

        // Merge curr's content into prev, combining content arrays.
        json prevContent;

        if( prev.contains( "content" ) && prev["content"].is_array() )
            prevContent = prev["content"];
        else if( prev.contains( "content" ) && prev["content"].is_string() )
            prevContent = json::array( { { { "type", "text" }, { "text", prev["content"].get<std::string>() } } } );
        else
            prevContent = json::array();

        if( curr.contains( "content" ) && curr["content"].is_array() )
        {
            for( const auto& block : curr["content"] )
                prevContent.push_back( block );
        }
        else if( curr.contains( "content" ) && curr["content"].is_string() )
        {
            prevContent.push_back( { { "type", "text" }, { "text", curr["content"].get<std::string>() } } );
        }

        prev["content"] = prevContent;

        // Preserve _compaction flag if curr is a compaction marker.
        // BuildApiContext() relies on this flag to slice the history.
        // When merging into a compaction marker, drop pre-compaction content
        // (e.g. tool_result blocks whose matching tool_use was compacted away).
        // The compaction summary already encompasses everything before it.
        if( curr.contains( "_compaction" ) && curr["_compaction"] == true )
        {
            prev["_compaction"] = true;

            // Keep only the compaction marker's own content blocks.
            // Pre-compaction tool_result blocks would reference tool_use IDs
            // that no longer exist in the post-compaction API context.
            if( curr.contains( "content" ) && curr["content"].is_array() )
                prev["content"] = curr["content"];
        }

        messages.erase( messages.begin() + i );
        modified = true;
        wxLogInfo( "CHAT_CONTROLLER::RepairMessageArray - merged consecutive user messages at index %zu", i );
        // Don't increment — check the new element at position i
    }

    return modified;
}


void CHAT_CONTROLLER::RepairHistory()
{
    bool modified = RepairMessageArray( m_chatHistory );

    if( modified )
    {
        wxLogInfo( "CHAT_CONTROLLER::RepairHistory - chat history was repaired" );

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


void CHAT_CONTROLLER::SanitizeMessages( nlohmann::json& aMessages )
{
    // Sanitize message array to ensure valid format for the Anthropic API.
    // This fixes issues like consecutive user messages that can occur after errors.
    if( aMessages.empty() )
        return;

    nlohmann::json sanitized = nlohmann::json::array();
    std::string lastRole;

    for( const auto& msg : aMessages )
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
                    // Merge current message's content into the tool_result message
                    // to maintain role alternation while preserving the tool_result.
                    wxLogInfo( "CHAT_CONTROLLER::SanitizeMessages - merging into tool_result "
                               "user message" );
                    auto& prevSanitized = sanitized.back();

                    if( !prevSanitized.contains( "content" ) || !prevSanitized["content"].is_array() )
                    {
                        if( prevSanitized.contains( "content" ) && prevSanitized["content"].is_string() )
                        {
                            prevSanitized["content"] = nlohmann::json::array(
                                { { { "type", "text" },
                                    { "text", prevSanitized["content"].get<std::string>() } } } );
                        }
                        else
                        {
                            prevSanitized["content"] = nlohmann::json::array();
                        }
                    }

                    // Append current message's content blocks
                    if( msg.contains( "content" ) && msg["content"].is_array() )
                    {
                        for( const auto& block : msg["content"] )
                            prevSanitized["content"].push_back( block );
                    }
                    else if( msg.contains( "content" ) && msg["content"].is_string() )
                    {
                        prevSanitized["content"].push_back(
                            { { "type", "text" },
                              { "text", msg["content"].get<std::string>() } } );
                    }

                    continue;  // Skip push_back — content was merged into previous
                }
                else
                {
                    // For consecutive user messages, replace the previous one
                    wxLogInfo( "CHAT_CONTROLLER::SanitizeMessages - removing duplicate user message" );
                    if( !sanitized.empty() )
                        sanitized.erase( sanitized.end() - 1 );
                }
            }
            else if( role == "assistant" )
            {
                // For consecutive assistant messages, keep both (could be continuation)
                // but this shouldn't normally happen
                wxLogWarning( "CHAT_CONTROLLER::SanitizeMessages - consecutive assistant messages" );
            }
        }

        // Filter out __stripped__ image/document blocks from legacy history.
        // These are placeholders from when we used to strip base64 data on save.
        nlohmann::json cleanMsg = msg;

        if( cleanMsg.contains( "content" ) && cleanMsg["content"].is_array() )
        {
            nlohmann::json cleanContent = nlohmann::json::array();

            for( const auto& block : cleanMsg["content"] )
            {
                // Check for image/document blocks with __stripped__ data
                bool isStripped = false;

                if( block.contains( "source" ) && block["source"].contains( "data" )
                    && block["source"]["data"] == "__stripped__" )
                {
                    isStripped = true;
                }

                // Also check inside tool_result content arrays
                if( block.contains( "content" ) && block["content"].is_array() )
                {
                    nlohmann::json cleanInner = nlohmann::json::array();

                    for( const auto& inner : block["content"] )
                    {
                        if( inner.contains( "source" ) && inner["source"].contains( "data" )
                            && inner["source"]["data"] == "__stripped__" )
                        {
                            continue;  // Drop stripped image from tool result
                        }

                        cleanInner.push_back( inner );
                    }

                    nlohmann::json cleanBlock = block;
                    cleanBlock["content"] = cleanInner;
                    cleanContent.push_back( cleanBlock );
                    continue;
                }

                if( !isStripped )
                    cleanContent.push_back( block );
            }

            cleanMsg["content"] = cleanContent;
        }

        sanitized.push_back( cleanMsg );
        lastRole = role;
    }

    // Ensure conversation doesn't end with orphaned assistant tool_use without tool_result
    // (This is handled by RepairHistory, but double-check here)

    if( sanitized != aMessages )
    {
        wxLogInfo( "CHAT_CONTROLLER::SanitizeMessages - context was sanitized" );
        aMessages = sanitized;
    }
}


/**
 * Manage image content in API context to prevent 413 errors.
 * - Replaces "seen" images (before last assistant response) with text placeholders
 * - Limits recent images to aMaxRecentImages, replacing excess with placeholders
 *
 * @param aMessages The API context JSON array to modify in-place
 * @param aMaxRecentImages Maximum number of recent unseen images to keep (default 3)
 */
static void ManageImageContext( nlohmann::json& aMessages, int aMaxRecentImages = 3 )
{
    if( !aMessages.is_array() || aMessages.empty() )
        return;

    // Step 1: Find last assistant message index (images before this are "seen")
    int lastAssistantIdx = -1;
    for( int i = static_cast<int>( aMessages.size() ) - 1; i >= 0; --i )
    {
        if( aMessages[i].value( "role", "" ) == "assistant" )
        {
            lastAssistantIdx = i;
            break;
        }
    }

    // Step 2: Collect all image locations
    struct ImageLoc {
        size_t msgIdx;
        size_t contentIdx;
        size_t innerIdx;      // SIZE_MAX if not inside tool_result
        bool   inToolResult;
        bool   seen;
        std::string description;
    };
    std::vector<ImageLoc> imageLocs;

    for( size_t i = 0; i < aMessages.size(); ++i )
    {
        const auto& msg = aMessages[i];
        if( msg.value( "role", "" ) != "user" )
            continue;

        if( !msg.contains( "content" ) || !msg["content"].is_array() )
            continue;

        const auto& content = msg["content"];
        for( size_t j = 0; j < content.size(); ++j )
        {
            const auto& block = content[j];
            std::string blockType = block.value( "type", "" );

            // Direct image in user message
            if( blockType == "image" )
            {
                imageLocs.push_back( { i, j, SIZE_MAX, false,
                                       static_cast<int>( i ) < lastAssistantIdx,
                                       "user-attached image" } );
            }
            // Image inside tool_result
            else if( blockType == "tool_result" && block.contains( "content" )
                     && block["content"].is_array() )
            {
                const auto& inner = block["content"];
                std::string desc = "screenshot";

                // Try to get description from accompanying text block
                for( const auto& ib : inner )
                {
                    if( ib.value( "type", "" ) == "text" )
                    {
                        desc = ib.value( "text", "screenshot" );
                        break;
                    }
                }

                for( size_t k = 0; k < inner.size(); ++k )
                {
                    if( inner[k].value( "type", "" ) == "image" )
                    {
                        imageLocs.push_back( { i, j, k, true,
                                               static_cast<int>( i ) < lastAssistantIdx,
                                               desc } );
                    }
                }
            }
        }
    }

    if( imageLocs.empty() )
        return;

    // Step 3: Determine which images to replace
    // Process from newest to oldest; keep up to aMaxRecentImages unseen images
    int recentCount = 0;
    std::vector<bool> shouldReplace( imageLocs.size(), false );

    for( int idx = static_cast<int>( imageLocs.size() ) - 1; idx >= 0; --idx )
    {
        const auto& loc = imageLocs[idx];
        if( loc.seen )
        {
            shouldReplace[idx] = true;  // Already analyzed by LLM
        }
        else
        {
            recentCount++;
            if( recentCount > aMaxRecentImages )
                shouldReplace[idx] = true;  // Exceeds limit
        }
    }

    // Step 4: Replace images with text placeholders
    // Process in reverse order to maintain valid indices
    for( int idx = static_cast<int>( imageLocs.size() ) - 1; idx >= 0; --idx )
    {
        if( !shouldReplace[idx] )
            continue;

        const auto& loc = imageLocs[idx];
        std::string placeholder = "[Image: " + loc.description;
        placeholder += loc.seen ? " - already analyzed]" : " - removed to reduce context size]";

        nlohmann::json textBlock = {
            { "type", "text" },
            { "text", placeholder }
        };

        if( loc.inToolResult )
        {
            aMessages[loc.msgIdx]["content"][loc.contentIdx]["content"][loc.innerIdx] = textBlock;
        }
        else
        {
            aMessages[loc.msgIdx]["content"][loc.contentIdx] = textBlock;
        }
    }

    int replaced = std::count( shouldReplace.begin(), shouldReplace.end(), true );
    wxLogInfo( "ManageImageContext: %zu images found, %d replaced (max recent: %d)",
               imageLocs.size(), replaced, aMaxRecentImages );
}


nlohmann::json CHAT_CONTROLLER::BuildApiContext() const
{
    // Find the last compaction marker in m_chatHistory.
    // Everything from that marker onward is the API context.
    int compactionIdx = -1;

    for( int i = static_cast<int>( m_chatHistory.size() ) - 1; i >= 0; --i )
    {
        if( m_chatHistory[i].contains( "_compaction" ) && m_chatHistory[i]["_compaction"] == true )
        {
            compactionIdx = i;
            break;
        }
    }

    if( compactionIdx < 0 )
        return m_chatHistory;  // No compaction — send full history

    // Return [compaction_marker] + all messages after it
    nlohmann::json context = nlohmann::json::array();

    for( size_t i = static_cast<size_t>( compactionIdx ); i < m_chatHistory.size(); ++i )
    {
        // Strip the internal _compaction flag before sending to API
        nlohmann::json msg = m_chatHistory[i];
        msg.erase( "_compaction" );
        context.push_back( msg );
    }

    wxLogInfo( "CHAT_CONTROLLER::BuildApiContext - compaction at index %d, "
               "sending %zu of %zu messages",
               compactionIdx, context.size(), m_chatHistory.size() );

    return context;
}


void CHAT_CONTROLLER::MergeDynamicTools()
{
    if( m_dynamicToolsMerged )
        return;

    auto dynamicTools = TOOL_REGISTRY::Instance().GetDynamicTools();

    if( dynamicTools.empty() )
        return;

    m_tools.insert( m_tools.end(), dynamicTools.begin(), dynamicTools.end() );
    m_dynamicToolsMerged = true;

    wxLogInfo( "CHAT_CONTROLLER: Merged %zu dynamic tool schemas", dynamicTools.size() );
}


std::vector<LLM_TOOL> CHAT_CONTROLLER::GetFilteredTools() const
{
    // Plan mode: return only read-only tools
    if( m_agentMode == AgentMode::PLAN )
    {
        std::vector<LLM_TOOL> filtered;
        std::copy_if( m_tools.begin(), m_tools.end(), std::back_inserter( filtered ),
                      []( const LLM_TOOL& t ) { return t.read_only; } );
        return filtered;
    }

    // Execute mode: all core tools + editor-filtered deferred tools.
    // Core (non-deferred) tools are always included to keep the cached prefix stable.
    // Deferred tools are filtered by editor state to reduce tool search noise.
    bool schOpen = TOOL_REGISTRY::Instance().IsSchematicEditorOpen();
    bool pcbOpen = TOOL_REGISTRY::Instance().IsPcbEditorOpen();

    // Both or neither editor open → include everything
    if( schOpen == pcbOpen )
        return m_tools;

    std::vector<LLM_TOOL> result;

    for( const auto& t : m_tools )
    {
        if( !t.defer_loading )
        {
            result.push_back( t );
        }
        else if( t.group == ToolGroup::GENERAL )
        {
            result.push_back( t );
        }
        else if( t.group == ToolGroup::SCHEMATIC && schOpen )
        {
            result.push_back( t );
        }
        else if( t.group == ToolGroup::PCB && pcbOpen )
        {
            result.push_back( t );
        }
    }

    return result;
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

    // Repair structural issues (orphaned tool_use/tool_result) before sanitizing.
    RepairHistory();

    // Set conversation metadata on LLM client for usage tracking
    if( !m_chatId.empty() )
    {
        std::string title;
        std::string chatStoragePath;
        std::string logStoragePath;

        if( m_chatHistoryDb )
            title = m_chatHistoryDb->GetTitle();

        if( m_cloudSync )
        {
            std::string email = m_cloudSync->GetUserEmail();

            if( !email.empty() )
            {
                chatStoragePath = email + "/chats/" + m_chatId + ".json";

                std::string logFilename = m_cloudSync->GetCurrentLogFilename();

                if( !logFilename.empty() )
                    logStoragePath = email + "/logs/" + logFilename;
            }
        }

        m_llmClient->SetConversationMetadata( m_chatId, title, chatStoragePath, logStoragePath );
    }

    // Build the API context (handles compaction — only sends post-compaction messages)
    nlohmann::json apiContext = BuildApiContext();

    // Repair the sliced context. After compaction, the sliced context may have orphaned
    // tool_results (where the corresponding tool_use was before the compaction marker).
    // RepairMessageArray will remove these orphaned blocks.
    RepairMessageArray( apiContext );

    // Sanitize context before sending to ensure valid message format
    SanitizeMessages( apiContext );

    // Manage image context to prevent 413 errors from accumulated screenshots
    ManageImageContext( apiContext, 3 );  // Keep max 3 recent unseen images

    // Sync editor state so GetFilteredTools() has current open/closed state
    if( m_syncEditorStateFn )
        m_syncEditorStateFn();

    // Merge MCP tool schemas if they've arrived from background fetch
    MergeDynamicTools();

    // Start async request - events will be forwarded to HandleLLMChunk
    bool started = m_llmClient->AskStreamWithToolsAsync( apiContext, GetFilteredTools(), m_eventSink );
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
            curl.SetURL( ZEO_BASE_URL + "/api/llm/title" );
            curl.SetHeader( "Content-Type", "application/json" );
            curl.SetHeader( "Authorization", "Bearer " + accessToken );

            // Build request body
            nlohmann::json requestBody;
            requestBody["message"] = message;
            if( !m_chatId.empty() )
                requestBody["conversation_id"] = m_chatId;
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


void CHAT_CONTROLLER::RequestTitle( const std::string& aMessage )
{
    if( aMessage.empty() )
        return;

    m_firstUserMessage = aMessage;
    GenerateTitle();
}


// ============================================================================
// Schematic edit detection (diff between turns)
// ============================================================================

void CHAT_CONTROLLER::TakeSchematicSnapshot()
{
    if( m_getSchematicSummaryFn )
    {
        m_schematicSnapshot = m_getSchematicSummaryFn();
        wxLogInfo( "CHAT_CONTROLLER::TakeSchematicSnapshot - captured %zu bytes",
                   m_schematicSnapshot.size() );
    }
}


static std::string FormatDouble( double v )
{
    char buf[32];
    snprintf( buf, sizeof( buf ), "%.2f", v );
    return buf;
}


std::string CHAT_CONTROLLER::GetUserEditsSummary()
{
    if( !m_getSchematicSummaryFn || m_schematicSnapshot.empty() )
        return "";

    std::string currentSummary = m_getSchematicSummaryFn();
    if( currentSummary.empty() || currentSummary[0] != '{' )
        return "";

    // Parse both snapshots
    nlohmann::json before, after;
    try
    {
        before = nlohmann::json::parse( m_schematicSnapshot );
        after = nlohmann::json::parse( currentSummary );
    }
    catch( ... )
    {
        return "";
    }

    // Guard against false "cleared schematic" detection.
    // If the current read returns a completely empty state but the previous snapshot
    // had content, this likely means the API couldn't read the schematic (e.g. a
    // modal dialog like schematic settings was open). Skip injection and keep the
    // old snapshot so the next successful read can diff against it.
    bool beforeHasContent = !before.value( "symbols", nlohmann::json::object() ).empty()
                            || !before.value( "labels", nlohmann::json::object() ).empty()
                            || before.value( "wire_count", 0 ) > 0;
    bool afterIsEmpty = after.value( "symbols", nlohmann::json::object() ).empty()
                        && after.value( "labels", nlohmann::json::object() ).empty()
                        && after.value( "wire_count", 0 ) == 0;

    if( beforeHasContent && afterIsEmpty )
    {
        wxLogWarning( "CHAT_CONTROLLER::GetUserEditsSummary - current snapshot is empty but "
                      "previous had content; likely API read failure (dialog open?). "
                      "Skipping edit injection and keeping old snapshot." );
        return "";
    }

    std::vector<std::string> changes;

    // Compare symbols
    auto beforeSyms = before.value( "symbols", nlohmann::json::object() );
    auto afterSyms = after.value( "symbols", nlohmann::json::object() );

    for( auto& [uuid, sym] : afterSyms.items() )
    {
        if( !beforeSyms.contains( uuid ) )
        {
            changes.push_back( "Added " + sym.value( "ref", std::string( "?" ) )
                               + " (" + sym.value( "lib", std::string() ) + ") at ("
                               + FormatDouble( sym.value( "x", 0.0 ) ) + ", "
                               + FormatDouble( sym.value( "y", 0.0 ) ) + ")" );
        }
        else
        {
            auto& prev = beforeSyms[uuid];
            std::vector<std::string> mods;

            if( sym.value( "x", 0.0 ) != prev.value( "x", 0.0 )
                || sym.value( "y", 0.0 ) != prev.value( "y", 0.0 ) )
            {
                mods.push_back( "moved to (" + FormatDouble( sym.value( "x", 0.0 ) )
                                + ", " + FormatDouble( sym.value( "y", 0.0 ) ) + ")" );
            }

            if( sym.value( "ang", 0 ) != prev.value( "ang", 0 ) )
            {
                mods.push_back( "rotated to "
                                + std::to_string( sym.value( "ang", 0 ) ) + "\xC2\xB0" );
            }

            if( sym.value( "val", std::string() ) != prev.value( "val", std::string() ) )
            {
                mods.push_back( "value changed to \""
                                + sym.value( "val", std::string() ) + "\"" );
            }

            if( sym.value( "ref", std::string() ) != prev.value( "ref", std::string() ) )
            {
                mods.push_back( "reference changed from "
                                + prev.value( "ref", std::string( "?" ) )
                                + " to " + sym.value( "ref", std::string( "?" ) ) );
            }

            if( !mods.empty() )
            {
                std::string detail = sym.value( "ref", std::string( "?" ) ) + ": ";
                for( size_t i = 0; i < mods.size(); i++ )
                {
                    if( i > 0 )
                        detail += ", ";
                    detail += mods[i];
                }
                changes.push_back( detail );
            }
        }
    }

    for( auto& [uuid, sym] : beforeSyms.items() )
    {
        if( !afterSyms.contains( uuid ) )
        {
            changes.push_back( "Removed " + sym.value( "ref", std::string( "?" ) )
                               + " (" + sym.value( "lib", std::string() ) + ")" );
        }
    }

    // Compare labels
    auto beforeLabels = before.value( "labels", nlohmann::json::object() );
    auto afterLabels = after.value( "labels", nlohmann::json::object() );

    for( auto& [uuid, lbl] : afterLabels.items() )
    {
        if( !beforeLabels.contains( uuid ) )
        {
            changes.push_back( "Added label \"" + lbl.value( "name", std::string() )
                               + "\" at (" + FormatDouble( lbl.value( "x", 0.0 ) )
                               + ", " + FormatDouble( lbl.value( "y", 0.0 ) ) + ")" );
        }
        else
        {
            auto& prev = beforeLabels[uuid];
            if( lbl.value( "name", std::string() ) != prev.value( "name", std::string() ) )
            {
                changes.push_back( "Label renamed from \""
                                   + prev.value( "name", std::string() ) + "\" to \""
                                   + lbl.value( "name", std::string() ) + "\"" );
            }
            else if( lbl.value( "x", 0.0 ) != prev.value( "x", 0.0 )
                     || lbl.value( "y", 0.0 ) != prev.value( "y", 0.0 ) )
            {
                changes.push_back( "Label \"" + lbl.value( "name", std::string() )
                                   + "\" moved to ("
                                   + FormatDouble( lbl.value( "x", 0.0 ) ) + ", "
                                   + FormatDouble( lbl.value( "y", 0.0 ) ) + ")" );
            }
        }
    }

    for( auto& [uuid, lbl] : beforeLabels.items() )
    {
        if( !afterLabels.contains( uuid ) )
        {
            changes.push_back( "Removed label \""
                               + lbl.value( "name", std::string() ) + "\"" );
        }
    }

    // Compare wire count
    int beforeWires = before.value( "wire_count", 0 );
    int afterWires = after.value( "wire_count", 0 );

    if( afterWires != beforeWires )
    {
        int delta = afterWires - beforeWires;
        if( delta > 0 )
            changes.push_back( std::to_string( delta ) + " wire(s) added" );
        else
            changes.push_back( std::to_string( -delta ) + " wire(s) removed" );
    }

    if( changes.empty() )
        return "";

    // Update snapshot to current state
    m_schematicSnapshot = currentSummary;

    std::string result;
    for( const auto& change : changes )
        result += "- " + change + "\n";

    wxLogInfo( "CHAT_CONTROLLER::GetUserEditsSummary - detected %zu changes", changes.size() );
    return result;
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
