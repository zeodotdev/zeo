#ifndef CHAT_CONTROLLER_H
#define CHAT_CONTROLLER_H

#include <wx/event.h>
#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "agent_state.h"
#include "agent_events.h"
#include "chat_events.h"

class AGENT_LLM_CLIENT;
class AGENT_CHAT_HISTORY;
class AGENT_CLOUD_SYNC;
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
     * User-attached image for sending with a message.
     */
    struct UserAttachment
    {
        std::string base64_data;
        std::string media_type;
    };

    /**
     * Send a user message with image attachments.
     * Builds multi-content array format for the Anthropic API.
     * @param aText The user's message text (may be empty for image-only messages)
     * @param aAttachments Vector of base64-encoded images
     */
    void SendMessageWithAttachments( const std::string& aText,
                                     const std::vector<UserAttachment>& aAttachments );

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
     * Save a snapshot of the current chat including any in-flight streaming response.
     * Used for crash protection (periodic saves during streaming) and before chat switches.
     * Does not modify m_chatHistory — builds a temporary snapshot for persistence only.
     */
    void SaveStreamingSnapshot();

    /**
     * Set the model to use for requests.
     * @param aModel The model name
     */
    void SetModel( const std::string& aModel );

    /**
     * Set the agent mode (EXECUTE or PLAN).
     * In PLAN mode, only read-only tools are sent to the LLM.
     */
    void SetAgentMode( AgentMode aMode ) { m_agentMode = aMode; }
    AgentMode GetAgentMode() const { return m_agentMode; }

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

    /**
     * Repair structural issues in a message array (static helper).
     * Returns true if the array was modified.
     */
    static bool RepairMessageArray( nlohmann::json& aMessages );

    // =========================================================================
    // Queries (read-only) - These return state without side effects
    // =========================================================================

    AgentConversationState GetState() const;
    bool CanAcceptInput() const;
    bool IsBusy() const;

    /**
     * Check if a specific tool is still pending.
     */
    bool HasPendingTool( const std::string& aToolId ) const
    {
        return m_ctx.HasPendingToolCall( aToolId );
    }

    const std::string& GetCurrentResponse() const { return m_currentResponse; }
    const wxString& GetThinkingContent() const { return m_thinkingContent; }
    const nlohmann::json& GetChatHistory() const { return m_chatHistory; }
    nlohmann::json GetApiContext() const { return BuildApiContext(); }

    /**
     * Set the chat history (for syncing from frame).
     * @param aHistory The chat history to set
     */
    void SetHistory( const nlohmann::json& aHistory )
    {
        m_chatHistory = aHistory;
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
    }
    const std::string& GetCurrentModel() const { return m_currentModel; }
    const std::string& GetChatId() const { return m_chatId; }
    void SetChatId( const std::string& aId ) { m_chatId = aId; }
    const std::vector<LLM_TOOL>& GetTools() const { return m_tools; }

    // =========================================================================
    // Service injection - These set dependencies
    // =========================================================================

    void SetLLMClient( AGENT_LLM_CLIENT* aClient ) { m_llmClient = aClient; }
    void SetChatHistoryDb( AGENT_CHAT_HISTORY* aDb ) { m_chatHistoryDb = aDb; }
    void SetAuth( class AGENT_AUTH* aAuth ) { m_auth = aAuth; }
    void SetCloudSync( AGENT_CLOUD_SYNC* aCloudSync ) { m_cloudSync = aCloudSync; }

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

    /**
     * Set the function used to sync editor state to TOOL_REGISTRY.
     * Called before tool execution to ensure handlers have accurate editor state.
     * @param aFn Function that syncs editor open/closed state
     */
    void SetEditorStateSyncFn( std::function<void()> aFn )
    {
        m_syncEditorStateFn = aFn;
    }

    /**
     * Set a callback that returns true when the frame has a queued user message.
     * Used to interrupt the tool→ContinueChat loop so the queued message
     * can be sent between tool rounds instead of waiting for the full turn.
     */
    void SetHasQueuedMessageFn( std::function<bool()> aFn )
    {
        m_hasQueuedMessageFn = aFn;
    }

    /**
     * Set a callback to notify VCS when a write tool modifies project files.
     * Called once per chat session after the first successful write tool.
     * @param aFn Function that triggers VCS auto-init and refresh
     */
    void SetVcsNotifyFn( std::function<void()> aFn )
    {
        m_vcsNotifyFn = aFn;
    }

    /**
     * Set the function used to get a JSON snapshot of the current schematic state.
     * Used for detecting user edits between agent turns.
     * @param aFn Function returning a JSON string summary of all schematic items
     */
    void SetSchematicSummaryFn( std::function<std::string()> aFn )
    {
        m_getSchematicSummaryFn = aFn;
    }

    /**
     * Take a snapshot of the current schematic state.
     * Called by the frame after the agent's turn completes so that
     * user edits can be detected when the next message is sent.
     */
    void TakeSchematicSnapshot();

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
     * @param aError The error message (raw response body for HTTP errors)
     * @param aHttpCode HTTP status code (0 for non-HTTP errors)
     * @param aErrorType Error type string (e.g. "overloaded_error" for SSE errors)
     */
    void HandleLLMError( const std::string& aError, long aHttpCode = 0,
                         const std::string& aErrorType = "" );

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
    nlohmann::json           m_serverToolBlocks;  ///< Server tool blocks for API context
    bool                     m_stopRequested;     ///< Cancel flag
    bool                     m_continueAfterComplete; ///< Continue generation after stream completes (for max_tokens)

    // -------------------------------------------------------------------------
    // Services (injected, not owned)
    // -------------------------------------------------------------------------
    AGENT_LLM_CLIENT*  m_llmClient;      ///< LLM API client
    AGENT_CHAT_HISTORY* m_chatHistoryDb;  ///< Persistence layer
    class AGENT_AUTH*   m_auth;          ///< Authentication manager
    AGENT_CLOUD_SYNC*  m_cloudSync = nullptr;  ///< Cloud sync (for log filename access)
    std::function<std::string( int, const std::string& )> m_sendRequestFn;
    std::function<std::string()> m_getProjectPathFn;  ///< Get project path for context injection
    std::function<std::string()> m_getSchematicSummaryFn;  ///< Get schematic snapshot for edit detection
    std::function<void()> m_syncEditorStateFn;  ///< Sync editor state to TOOL_REGISTRY before tool execution
    std::function<bool()> m_hasQueuedMessageFn; ///< Returns true when frame has a queued user message
    std::function<void()> m_vcsNotifyFn;         ///< Notify VCS of file changes (auto-init + refresh)
    bool                  m_vcsNotified = false;  ///< True once VCS has been notified this session

    // -------------------------------------------------------------------------
    // User edit detection (schematic diff between turns)
    // -------------------------------------------------------------------------
    std::string m_schematicSnapshot;  ///< JSON snapshot taken after agent turn completes

    // -------------------------------------------------------------------------
    // Streaming snapshot (crash protection)
    // -------------------------------------------------------------------------
    std::chrono::steady_clock::time_point m_lastSnapshotTime;  ///< Throttle periodic saves

    // -------------------------------------------------------------------------
    // Tool definitions
    // -------------------------------------------------------------------------
    std::vector<LLM_TOOL> m_tools;      ///< Available tool definitions
    bool                  m_dynamicToolsMerged = false;  ///< True once MCP schemas are merged
    AgentMode             m_agentMode;  ///< Current agent mode (EXECUTE or PLAN)

    // -------------------------------------------------------------------------
    // Title generation state
    // -------------------------------------------------------------------------
    std::string m_firstUserMessage;  ///< First user message for title generation

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    /**
     * Merge dynamic tool schemas from handlers (e.g. MCP-fetched) into m_tools.
     * Called once when schemas become available; no-op after that.
     */
    void MergeDynamicTools();

    /**
     * Get tools filtered by current agent mode.
     * In PLAN mode, returns only read-only tools.
     */
    std::vector<LLM_TOOL> GetFilteredTools() const;

    /**
     * Execute all pending tools in parallel.
     * Spawns a thread for each tool; results arrive via EVT_TOOL_EXECUTION_COMPLETE.
     * Frame-managed tools (open_editor, sch_run_erc) are deferred until parallel tools complete.
     */
    void ExecuteAllTools();

    /**
     * Execute the next deferred frame-managed tool.
     * Called after all parallel tools complete. Frame-managed tools require user
     * interaction (approval dialogs) and must run sequentially.
     */
    void ExecuteDeferredFrameTool();

    /**
     * Process a tool result and continue the chat.
     */
    void ProcessToolResult( const std::string& aToolId, const std::string& aResult, bool aSuccess );

    /**
     * Add a message to chat history.
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
     * Common implementation for sending a user message.
     * Handles title generation, project context injection, history push,
     * streaming state reset, state transition, and LLM request start.
     *
     * @param aText The user's message text
     * @param aContent Pre-built content JSON (array for multi-content, null to use plain text)
     */
    void DoSendMessage( const std::string& aText, const nlohmann::json& aContent );

    /**
     * Build the API context from m_chatHistory.
     * Finds the last _compaction marker message and returns everything from
     * that point onward. If no compaction has occurred, returns full history.
     */
    nlohmann::json BuildApiContext() const;

    /**
     * Sanitize a message array before sending to ensure valid message format.
     * Fixes issues like consecutive user messages that can occur after errors.
     */
    static void SanitizeMessages( nlohmann::json& aMessages );

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

public:
    /**
     * Request title generation for a given message.
     * Used by CC_CONTROLLER to trigger title generation via the Zeo API
     * without going through the normal chat flow.
     */
    void RequestTitle( const std::string& aMessage );

private:

    /**
     * Emit an event to the event sink.
     */
    template<typename T>
    void EmitEvent( wxEventType aType, const T& aData );

    /**
     * Emit a simple event with no payload.
     */
    void EmitEvent( wxEventType aType );

    /**
     * Compute a human-readable summary of user edits by diffing the stored
     * schematic snapshot against the current state. Returns empty string if
     * no changes detected or no snapshot available.
     */
    std::string GetUserEditsSummary();
};

#endif // CHAT_CONTROLLER_H
