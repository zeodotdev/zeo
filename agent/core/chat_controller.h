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

#ifndef CHAT_CONTROLLER_H
#define CHAT_CONTROLLER_H

#include <wx/event.h>
#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

#include "agent_state.h"
#include "agent_events.h"
#include "chat_events.h"

class AGENT_LLM_CLIENT;
class AGENT_CHAT_HISTORY;
struct LLM_TOOL;

/**
 * Central controller for chat state and logic.
 *
 * This class owns all chat-related state and emits events for UI updates.
 * The frame subscribes to these events and updates the display accordingly.
 *
 * Architecture:
 * - Controller owns state (messages, response, tools, state machine)
 * - Controller receives LLM events and tool results
 * - Controller emits high-level events for UI updates
 * - Frame is a thin subscriber that renders state to HTML
 */
class CHAT_CONTROLLER : public wxEvtHandler
{
public:
    /**
     * Construct a chat controller.
     * @param aEventSink Where to post events (typically AGENT_FRAME)
     */
    CHAT_CONTROLLER( wxEvtHandler* aEventSink );
    ~CHAT_CONTROLLER();

    // =========================================================================
    // Commands (input) - These modify state and may trigger async operations
    // =========================================================================

    /**
     * Send a user message and start LLM request.
     * @param aText The user's message text
     */
    void SendMessage( const std::string& aText );

    /**
     * Cancel the current operation (streaming or tool execution).
     */
    void Cancel();

    /**
     * Retry the last failed request.
     */
    void Retry();

    /**
     * Start a new chat, clearing all state.
     */
    void NewChat();

    /**
     * Load a chat from history.
     * @param aChatId The chat ID to load
     */
    void LoadChat( const std::string& aChatId );

    /**
     * Set the model to use for requests.
     * @param aModel The model name
     */
    void SetModel( const std::string& aModel );

    /**
     * Repair history to fix orphaned tool_use/tool_result blocks.
     *
     * The Anthropic API requires:
     * 1. Every tool_use must have a corresponding tool_result in the NEXT message
     * 2. Every tool_result must reference a tool_use in the PREVIOUS message
     *
     * This method performs two passes:
     * - Pass 1: Remove orphaned tool_results (no matching tool_use in prev message)
     * - Pass 2: Add fake tool_results for orphaned tool_uses
     *
     * Should be called before SendMessage() when loading existing conversations.
     */
    void RepairHistory();

    // =========================================================================
    // Queries (read-only) - These return state without side effects
    // =========================================================================

    AgentConversationState GetState() const;
    bool CanAcceptInput() const;
    bool IsBusy() const;

    const std::string& GetCurrentResponse() const { return m_currentResponse; }
    const wxString& GetThinkingContent() const { return m_thinkingContent; }
    const nlohmann::json& GetChatHistory() const { return m_chatHistory; }
    const nlohmann::json& GetApiContext() const { return m_apiContext; }

    /**
     * Set the chat history (for syncing from frame or loading from disk).
     * Also syncs the API context.
     * @param aHistory The chat history to set
     */
    void SetHistory( const nlohmann::json& aHistory )
    {
        m_chatHistory = aHistory;
        m_apiContext = aHistory;
    }

    /**
     * Clear streaming state for a new request.
     * Called by frame before starting a new LLM request.
     */
    void ClearStreamingState()
    {
        m_currentResponse.clear();
        m_thinkingContent.clear();
        m_thinkingSignature.clear();
    }

    /**
     * Add a user message to history (for syncing frame's user message to controller).
     * @param aText The user's message text
     */
    void AddUserMessage( const std::string& aText )
    {
        nlohmann::json userMsg = { { "role", "user" }, { "content", aText } };
        m_chatHistory.push_back( userMsg );
        m_apiContext.push_back( userMsg );
    }
    const std::string& GetCurrentModel() const { return m_currentModel; }
    const std::string& GetChatId() const { return m_chatId; }
    const std::vector<LLM_TOOL>& GetTools() const { return m_tools; }

    // =========================================================================
    // Service injection - These set dependencies
    // =========================================================================

    void SetLLMClient( AGENT_LLM_CLIENT* aClient ) { m_llmClient = aClient; }
    void SetChatHistoryDb( AGENT_CHAT_HISTORY* aDb ) { m_chatHistoryDb = aDb; }
    void SetAuth( class AGENT_AUTH* aAuth ) { m_auth = aAuth; }

    /**
     * Set the function used to send KIWAY requests for tool execution.
     * @param aFn Function taking (frame_type, payload) and returning result
     */
    void SetKiwayRequestFn( std::function<std::string( int, const std::string& )> aFn )
    {
        m_sendRequestFn = aFn;
    }

    /**
     * Set the function used to get the current project path.
     * @param aFn Function returning the project path string
     */
    void SetProjectPathFn( std::function<std::string()> aFn )
    {
        m_getProjectPathFn = aFn;
    }

    // =========================================================================
    // Event handlers - Called by frame's event table, forwarded to controller
    // =========================================================================

    /**
     * Handle an LLM streaming chunk event.
     * @param aChunk The streaming chunk data
     */
    void HandleLLMChunk( const LLMStreamChunk& aChunk );

    /**
     * Handle LLM stream completion.
     */
    void HandleLLMComplete();

    /**
     * Handle LLM stream error.
     * @param aError The error message
     */
    void HandleLLMError( const std::string& aError );

    /**
     * Handle tool execution completion.
     * @param aToolId The tool use ID
     * @param aResult The tool result
     * @param aSuccess Whether the tool succeeded
     */
    void HandleToolResult( const std::string& aToolId, const std::string& aResult, bool aSuccess );

private:
    wxEvtHandler* m_eventSink;  ///< Where to post events (AGENT_FRAME)

    // -------------------------------------------------------------------------
    // Chat state
    // -------------------------------------------------------------------------
    nlohmann::json m_chatHistory;      ///< Full message history for display/persistence
    nlohmann::json m_apiContext;       ///< Context sent to API (may be compacted)
    std::string    m_currentResponse;  ///< Accumulated streaming text
    wxString       m_thinkingContent;  ///< Accumulated thinking block text
    std::string    m_thinkingSignature; ///< Thinking block signature for API
    std::string    m_currentModel;     ///< Selected model name
    std::string    m_chatId;           ///< Current chat ID

    // -------------------------------------------------------------------------
    // State machine
    // -------------------------------------------------------------------------
    AgentConversationContext m_ctx;           ///< Chat state machine
    nlohmann::json           m_pendingToolCalls;  ///< Tools awaiting execution
    bool                     m_stopRequested;     ///< Cancel flag
    bool                     m_continueAfterComplete; ///< Continue generation after stream completes (for max_tokens)

    // -------------------------------------------------------------------------
    // Services (injected, not owned)
    // -------------------------------------------------------------------------
    AGENT_LLM_CLIENT*  m_llmClient;      ///< LLM API client
    AGENT_CHAT_HISTORY* m_chatHistoryDb;  ///< Persistence layer
    class AGENT_AUTH*   m_auth;          ///< Authentication manager
    std::function<std::string( int, const std::string& )> m_sendRequestFn;
    std::function<std::string()> m_getProjectPathFn;  ///< Get project path for tool validation

    // -------------------------------------------------------------------------
    // Tool definitions
    // -------------------------------------------------------------------------
    std::vector<LLM_TOOL> m_tools;  ///< Available tool definitions

    // -------------------------------------------------------------------------
    // Title generation state
    // -------------------------------------------------------------------------
    std::string m_firstUserMessage;  ///< First user message for title generation

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    /**
     * Execute the next pending tool in the queue.
     */
    void ExecuteNextTool();

    /**
     * Process a tool result and continue the chat.
     */
    void ProcessToolResult( const std::string& aToolId, const std::string& aResult, bool aSuccess );

    /**
     * Add a message to history arrays.
     */
    void AddToHistory( const nlohmann::json& aMessage );

    /**
     * Add tool result as a user message to history.
     */
    void AddToolResultToHistory( const std::string& aToolUseId, const std::string& aResult );

    /**
     * Add all completed tool results as a single user message.
     * Required by Anthropic API - all tool_results must be in one message.
     */
    void AddAllToolResultsToHistory();

    /**
     * Add assistant message with tool use blocks to history.
     */
    void AddAssistantToolUseToHistory( const nlohmann::json& aToolUseBlocks );

    /**
     * Start an async LLM request with current context.
     */
    void StartLLMRequest();

    /**
     * Continue the chat after tool results.
     */
    void ContinueChat();

    /**
     * Generate a title for the chat.
     */
    void GenerateTitle();

    /**
     * Emit an event to the event sink.
     */
    template<typename T>
    void EmitEvent( wxEventType aType, const T& aData );

    /**
     * Emit a simple event with no payload.
     */
    void EmitEvent( wxEventType aType );
};

#endif // CHAT_CONTROLLER_H
