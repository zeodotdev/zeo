#ifndef AGENT_LLM_CLIENT_H
#define AGENT_LLM_CLIENT_H

#include <string>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>
#include <wx/thread.h>

class AGENT_AUTH;
class AGENT_FRAME;
class wxEvtHandler;

/**
 * Tool definition for native tool calling.
 */
struct LLM_TOOL
{
    std::string    name;
    std::string    description;
    nlohmann::json input_schema;
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
    ERROR           // Error occurred
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
     * Set the model to use (e.g., "Claude 4.5 Sonnet").
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

private:
    AGENT_FRAME* m_parent;
    AGENT_AUTH*  m_auth = nullptr;
    std::string  m_modelName;

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
                        const std::vector<LLM_TOOL>& aTools );

    virtual ~LLM_REQUEST_THREAD();

protected:
    virtual void* Entry() override;

private:
    AGENT_LLM_CLIENT*     m_client;
    wxEvtHandler*         m_handler;
    std::string           m_model;
    nlohmann::json        m_messages;
    std::vector<LLM_TOOL> m_tools;

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
    };

    // Parse SSE events and post to main thread
    void ParseAndPostEvents( StreamContext& ctx, const std::string& data );
};

#endif // AGENT_LLM_CLIENT_H
