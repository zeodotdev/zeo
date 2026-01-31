/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "chat_controller.h"
#include "chat_events.h"
#include "agent_tools.h"
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
wxDEFINE_EVENT( EVT_CHAT_TOOL_START, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TOOL_COMPLETE, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TURN_COMPLETE, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_ERROR, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_STATE_CHANGED, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TITLE_DELTA, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_TITLE_GENERATED, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_HISTORY_LOADED, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_CONTEXT_STATUS, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_CONTEXT_COMPACTING, wxThreadEvent );
wxDEFINE_EVENT( EVT_CHAT_CONTEXT_RECOVERED, wxThreadEvent );


// ============================================================================
// Constructor / Destructor
// ============================================================================

CHAT_CONTROLLER::CHAT_CONTROLLER( wxEvtHandler* aEventSink )
    : m_eventSink( aEventSink ),
      m_llmClient( nullptr ),
      m_chatHistoryDb( nullptr ),
      m_auth( nullptr ),
      m_stopRequested( false )
{
    // Initialize tool definitions
    m_tools = AgentTools::GetToolDefinitions();

    // Initialize chat history as empty array
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
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
    if( !CanAcceptInput() )
    {
        wxLogWarning( "CHAT_CONTROLLER::SendMessage called while busy" );
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
        m_firstUserMessage = aText;
        GenerateTitle();
    }

    // Add user message to history
    nlohmann::json userMsg = {
        { "role", "user" },
        { "content", aText }
    };
    AddToHistory( userMsg );

    // Reset streaming state
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_thinkingSignature.clear();
    m_pendingToolCalls = nlohmann::json::array();

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
    m_stopRequested = true;

    if( m_llmClient )
        m_llmClient->CancelRequest();

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

    // Transition back to idle
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.TransitionTo( AgentConversationState::IDLE );
    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                             static_cast<int>( m_ctx.GetState() ) ) );
}


void CHAT_CONTROLLER::Retry()
{
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
    Cancel();

    // Clear all chat state
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_chatId.clear();
    m_firstUserMessage.clear();
    m_pendingToolCalls = nlohmann::json::array();

    m_ctx.Reset();
}


void CHAT_CONTROLLER::LoadChat( const std::string& aChatId )
{
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
        // All tools parsed, transition to tool execution
        AgentConversationState oldState = m_ctx.GetState();
        m_ctx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED );
        EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                                  static_cast<int>( m_ctx.GetState() ) ) );

        // Add assistant message with tool uses to history
        AddAssistantToolUseToHistory( m_pendingToolCalls );

        // NOTE: Don't clear streaming state here. The frame needs to read currentResponse
        // to render the text one final time before capturing m_htmlBeforeAgentResponse.
        // The frame will call ClearStreamingState() after capturing the HTML.

        // Start executing tools (controller drives tool execution, frame just does UI)
        ExecuteNextTool();
        break;
    }

    case LLMChunkType::END_TURN:
    {
        // LLM finished without tool calls - add assistant message with response
        // Include thinking blocks with signatures if present
        if( !m_currentResponse.empty() || !m_thinkingContent.IsEmpty() )
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

    case LLMChunkType::CONTEXT_STATUS:
        if( aChunk.context_compacted )
        {
            EmitEvent( EVT_CHAT_CONTEXT_STATUS, ChatContextStatusData( true ) );
        }
        break;

    case LLMChunkType::CONTEXT_COMPACTING:
        EmitEvent( EVT_CHAT_CONTEXT_COMPACTING, ChatContextCompactingData() );
        break;

    case LLMChunkType::CONTEXT_EXHAUSTED:
    case LLMChunkType::CONTEXT_TRUNCATED:
        // Context recovery - replace API context with compacted messages
        if( !aChunk.summarized_messages.empty() && aChunk.summarized_messages.is_array() )
        {
            m_apiContext = aChunk.summarized_messages;
            EmitEvent( EVT_CHAT_CONTEXT_RECOVERED, ChatContextRecoveredData( aChunk.summarized_messages ) );
        }
        break;
    }
}


void CHAT_CONTROLLER::HandleLLMComplete()
{
    // Streaming completed successfully
    // Most handling is done in HandleLLMChunk for END_TURN
}


void CHAT_CONTROLLER::HandleLLMError( const std::string& aError )
{
    AgentConversationState oldState = m_ctx.GetState();
    m_ctx.SetState( AgentConversationState::ERROR );
    m_ctx.error_message = aError;

    EmitEvent( EVT_CHAT_STATE_CHANGED, ChatStateChangedData( static_cast<int>( oldState ),
                                                              static_cast<int>( m_ctx.GetState() ) ) );

    bool canRetry = true;  // Most errors can be retried
    bool isContextError = aError.find( "context" ) != std::string::npos ||
                          aError.find( "token" ) != std::string::npos;

    EmitEvent( EVT_CHAT_ERROR, ChatErrorData( aError, canRetry, isContextError ) );
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
    PendingToolCall* tool = m_ctx.GetNextPendingToolCall();
    if( !tool )
    {
        // No more tools, continue chat
        ContinueChat();
        return;
    }

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

    // Handle open_editor specially - requires user approval in the UI
    // The frame will show an approval dialog and call HandleToolResult() when done
    if( tool->tool_name == "open_editor" )
    {
        // Don't execute synchronously - wait for frame to handle via HandleToolResult
        return;
    }

    // Execute tool via KIWAY
    if( m_sendRequestFn )
    {
        std::string payload = AgentTools::BuildToolPayload( tool->tool_name, tool->tool_input );
        std::string result = m_sendRequestFn( FRAME_TERMINAL, payload );

        // Process the result
        ProcessToolResult( tool->tool_use_id, result, !result.empty() && result.find( "Error:" ) != 0 );
    }
    else
    {
        ProcessToolResult( tool->tool_use_id, "Error: No KIWAY request function configured", false );
    }
}


void CHAT_CONTROLLER::ProcessToolResult( const std::string& aToolId,
                                          const std::string& aResult,
                                          bool aSuccess )
{
    // Find the tool
    PendingToolCall* tool = m_ctx.FindPendingToolCall( aToolId );
    std::string toolName = tool ? tool->tool_name : "unknown";

    // Check if result contains Python traceback
    bool isPythonError = aResult.find( "Traceback" ) != std::string::npos;

    // Store result
    AgentConversationContext::ToolResult tr;
    tr.tool_use_id = aToolId;
    tr.tool_name = toolName;
    tr.result = aResult;
    tr.success = aSuccess;
    tr.is_python_error = isPythonError;
    m_ctx.completed_tool_results.push_back( tr );

    // Emit tool complete event
    EmitEvent( EVT_CHAT_TOOL_COMPLETE, ChatToolCompleteData( aToolId, toolName, aResult,
                                                              aSuccess, isPythonError ) );

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
        content.push_back( {
            { "type", "tool_result" },
            { "tool_use_id", result.tool_use_id },
            { "content", result.result }
        } );
    }

    nlohmann::json toolResultMsg = {
        { "role", "user" },
        { "content", content }
    };

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


void CHAT_CONTROLLER::StartLLMRequest()
{
    if( !m_llmClient )
    {
        HandleLLMError( "No LLM client configured" );
        return;
    }

    m_stopRequested = false;

    // System prompt now handled server-side
    // Start async request - events will be forwarded to HandleLLMChunk
    bool started = m_llmClient->AskStreamWithToolsAsync( m_apiContext, m_tools, m_eventSink );
    if( !started )
    {
        HandleLLMError( "Failed to start LLM request" );
    }
}


void CHAT_CONTROLLER::ContinueChat()
{
    // Reset for next turn
    m_currentResponse.clear();
    m_thinkingContent.clear();
    m_thinkingSignature.clear();
    m_pendingToolCalls = nlohmann::json::array();
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
    if( m_firstUserMessage.empty() )
        return;

    if( !m_auth )
        return;

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
            curl.SetURL( "https://www.zener.so/api/llm/title" );
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
template void CHAT_CONTROLLER::EmitEvent<ChatToolStartData>( wxEventType, const ChatToolStartData& );
template void CHAT_CONTROLLER::EmitEvent<ChatToolCompleteData>( wxEventType, const ChatToolCompleteData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTurnCompleteData>( wxEventType, const ChatTurnCompleteData& );
template void CHAT_CONTROLLER::EmitEvent<ChatErrorData>( wxEventType, const ChatErrorData& );
template void CHAT_CONTROLLER::EmitEvent<ChatStateChangedData>( wxEventType, const ChatStateChangedData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTitleDeltaData>( wxEventType, const ChatTitleDeltaData& );
template void CHAT_CONTROLLER::EmitEvent<ChatTitleGeneratedData>( wxEventType, const ChatTitleGeneratedData& );
template void CHAT_CONTROLLER::EmitEvent<ChatHistoryLoadedData>( wxEventType, const ChatHistoryLoadedData& );
