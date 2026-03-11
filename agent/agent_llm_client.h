#ifndef AGENT_LLM_CLIENT_H
#define AGENT_LLM_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <nlohmann/json.hpp>
#include <wx/thread.h>

#include "agent_state.h"
#include <zeo/zeo_constants.h>

class AGENT_AUTH;
class AGENT_FRAME;
class wxEvtHandler;

/**
 * Tool group for editor-state filtering.
 */
enum class ToolGroup
{
    GENERAL,    ///< Always included regardless of editor state
    SCHEMATIC,  ///< Included when schematic editor is open
    PCB         ///< Included when PCB editor is open
};

/**
 * Tool definition for native tool calling.
 */
struct LLM_TOOL
{
    std::string    name;
    std::string    description;
    nlohmann::json input_schema;
    bool           read_only = false;      ///< true = safe for plan mode (no modifications)
    ToolGroup      group = ToolGroup::GENERAL;
    bool           defer_loading = false;  ///< true = excluded from prompt, discoverable via tool search
};

/**
 * Event types for streaming with tools.
 */
enum class LLM_EVENT_TYPE
{
    TEXT,           // Text content delta
    TOOL_USE,       // Tool call with id, name, input
    TOOL_USE_DONE,  // All tool calls parsed, ready to execute
    END_TURN,       // Model finished (no more tool calls)
    ERRORED         // Error occurred
};

/**
 * Event data for streaming callbacks.
 */
struct LLM_EVENT
{
    LLM_EVENT_TYPE type;
    std::string    text;           // For TEXT events
    std::string    tool_use_id;    // For TOOL_USE events
    std::string    tool_name;      // For TOOL_USE events
    nlohmann::json tool_input;     // For TOOL_USE events
    std::string    error_message;  // For ERROR events
};

/**
 * LLM Client for KiCad Agent.
 * Uses authenticated proxy API for all LLM requests.
 */
class AGENT_LLM_CLIENT
{
    friend class LLM_REQUEST_THREAD;  // Allow thread to access private members

public:
    AGENT_LLM_CLIENT( AGENT_FRAME* aParent );
    ~AGENT_LLM_CLIENT();

    /**
     * Set the model to use (e.g., "Claude 4.6 Opus").
     * @param aModelName The model name.
     */
    void SetModel( const std::string& aModelName );

    /**
     * Send a streaming chat completion request with native tool calling (ASYNC/NON-BLOCKING).
     * This method returns immediately. Results are delivered via wx events to the handler.
     *
     * Events posted:
     * - EVT_LLM_STREAM_CHUNK: For each streaming chunk (text, tool_use, etc.)
     * - EVT_LLM_STREAM_COMPLETE: When streaming finishes successfully
     * - EVT_LLM_STREAM_ERROR: If an error occurs
     *
     * @param aMessages The full chat history as a JSON array.
     * @param aTools Vector of tool definitions.
     * @param aHandler The event handler to receive streaming events (typically AGENT_FRAME).
     * @return True if the request was started, false if it couldn't be started.
     */
    bool AskStreamWithToolsAsync( const nlohmann::json& aMessages,
                                   const std::vector<LLM_TOOL>& aTools,
                                   wxEvtHandler* aHandler );

    /**
     * Check if an async LLM request is currently in progress.
     */
    bool IsRequestInProgress() const { return m_requestInProgress.load(); }

    /**
     * Cancel any in-progress async request.
     * The request may not stop immediately but will be cancelled as soon as possible.
     */
    void CancelRequest() { m_cancelRequested.store( true ); }

    /**
     * Set the authentication manager for proxy API requests.
     * @param aAuth Pointer to the authentication manager (owned by caller)
     */
    void SetAuth( AGENT_AUTH* aAuth ) { m_auth = aAuth; }

    /**
     * Set the system prompt (core + agent addendum) to include in API requests.
     * The web proxy uses this instead of its own local copy.
     */
    void SetSystemPrompt( const std::string& aPrompt ) { m_systemPrompt = aPrompt; }
    void SetPlanAddendum( const std::string& aAddendum ) { m_planAddendum = aAddendum; }

    /**
     * Set the agent mode (EXECUTE or PLAN) for the next request.
     */
    void SetAgentMode( AgentMode aMode ) { m_agentMode = aMode; }

    /**
     * Set conversation metadata for usage tracking.
     * These are sent in the request metadata and used by the proxy to link api_usage
     * rows to a conversation record.
     */
    void SetConversationMetadata( const std::string& aChatId,
                                   const std::string& aTitle,
                                   const std::string& aChatStoragePath,
                                   const std::string& aLogStoragePath )
    {
        m_chatId = aChatId;
        m_chatTitle = aTitle;
        m_chatStoragePath = aChatStoragePath;
        m_logStoragePath = aLogStoragePath;
    }

    /**
     * Record a tool execution duration for the next request's metadata.
     * Keyed by tool_use_id so the proxy can match it to the tool_result.
     */
    void AddToolDuration( const std::string& aToolUseId, int aDurationMs )
    {
        m_toolDurations[aToolUseId] = aDurationMs;
    }

private:
    AGENT_FRAME* m_parent;
    AGENT_AUTH*  m_auth = nullptr;
    std::string  m_modelName;
    AgentMode    m_agentMode = AgentMode::EXECUTE;

    // System prompt (core + agent addendum), sent to proxy in metadata
    std::string  m_systemPrompt;
    std::string  m_planAddendum;  // Appended to system prompt in plan mode

    // Conversation metadata for usage tracking
    std::string  m_chatId;
    std::string  m_chatTitle;
    std::string  m_chatStoragePath;
    std::string  m_logStoragePath;

    // Tool execution durations (tool_use_id → ms), sent in metadata and cleared per request
    std::map<std::string, int> m_toolDurations;

    // Async request state
    std::atomic<bool> m_requestInProgress;
    std::atomic<bool> m_cancelRequested;
};


/**
 * Background thread for async LLM streaming requests.
 * Runs curl.Perform() in a background thread and posts events to the main thread.
 */
class LLM_REQUEST_THREAD : public wxThread
{
public:
    LLM_REQUEST_THREAD( AGENT_LLM_CLIENT* aClient,
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
                        const std::map<std::string, int>& aToolDurations = {} );

    virtual ~LLM_REQUEST_THREAD();

protected:
    virtual void* Entry() override;

private:
    AGENT_LLM_CLIENT*     m_client;
    wxEvtHandler*         m_handler;
    std::string           m_model;
    nlohmann::json        m_messages;
    std::vector<LLM_TOOL> m_tools;
    AgentMode             m_agentMode;

    // System prompt sent to proxy
    std::string           m_systemPrompt;

    // Conversation metadata for usage tracking
    std::string           m_chatId;
    std::string           m_chatTitle;
    std::string           m_chatStoragePath;
    std::string           m_logStoragePath;

    // Tool execution durations from the client (tool_use_id → ms)
    std::map<std::string, int> m_toolDurations;

    // Flag to check if cancellation was requested
    std::atomic<bool>* m_cancelFlag;

    // Curl write callback that posts events
    static size_t StreamWriteCallback( void* contents, size_t size, size_t nmemb, void* userp );

    // Context for the streaming callback
    struct StreamContext
    {
        wxEvtHandler*     handler;
        std::atomic<bool>* cancelFlag;
        std::string       buffer;           // Buffer for incomplete SSE data
        std::string       currentToolId;    // Current tool_use block ID
        std::string       currentToolName;  // Current tool name
        std::string       currentToolInput; // Accumulated tool input JSON
        bool              inToolInput;      // Currently accumulating tool input
        std::string       currentThinking;  // Accumulated thinking text
        std::string       currentSignature; // Accumulated thinking signature (for API)
        bool              inThinking;       // Currently accumulating thinking block
        std::string       currentCompaction; // Accumulated compaction content
        bool              inCompaction;     // Currently accumulating compaction block
        bool              inServerTool;     // Currently accumulating server tool input
        std::string       serverToolId;     // Current server_tool_use block ID
        std::string       serverToolName;   // Current server_tool_use name
        std::string       serverToolInput;  // Accumulated server tool input JSON
    };

    // Parse SSE events and post to main thread
    void ParseAndPostEvents( StreamContext& ctx, const std::string& data );
};

#endif // AGENT_LLM_CLIENT_H
