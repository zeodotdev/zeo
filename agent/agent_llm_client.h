#ifndef AGENT_LLM_CLIENT_H
#define AGENT_LLM_CLIENT_H

#include <string>
#include <functional>
#include <vector>
#include <map>
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
 * Generic LLM Client for KiCad Agent.
 * Supports OpenAI and Anthropic APIs.
 */
class AGENT_LLM_CLIENT
{
    friend class LLM_REQUEST_THREAD;  // Allow thread to access private members

public:
    AGENT_LLM_CLIENT( AGENT_FRAME* aParent );
    ~AGENT_LLM_CLIENT();

    /**
     * Set the model to use (e.g., "GPT-4o", "Claude 3.5 Sonnet").
     * @param aModelName The model name.
     */
    void SetModel( const std::string& aModelName );

    /**
     * Send a chat completion request.
     * @param aPrompt The user prompt.
     * @param aSystem The system prompt/instruction.
     * @param aPayload Additional context payload (e.g. selection data).
     * @return The response content.
     */
    std::string Ask( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload );

    /**
     * Send a streaming chat completion request.
     * @param aMessages The full chat history as a JSON array.
     * @param aSystem The system prompt/instruction.
     * @param aPayload Additional context payload (e.g. selection data).
     * @param aCallback The callback function to invoke with partial content updates.
     * @return True if successful, false otherwise.
     */
    bool AskStream( const nlohmann::json& aMessages, const std::string& aSystem, const std::string& aPayload,
                    std::function<void( const std::string& )> aCallback );

    /**
     * Send a streaming chat completion request with native tool calling (BLOCKING).
     * @param aMessages The full chat history as a JSON array.
     * @param aSystem The system prompt/instruction.
     * @param aTools Vector of tool definitions.
     * @param aCallback The callback function to invoke with LLM_EVENT updates.
     * @return True if successful, false otherwise.
     */
    bool AskStreamWithTools( const nlohmann::json& aMessages, const std::string& aSystem,
                             const std::vector<LLM_TOOL>& aTools,
                             std::function<void( const LLM_EVENT& )> aCallback );

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
     * @param aSystem The system prompt/instruction.
     * @param aTools Vector of tool definitions.
     * @param aHandler The event handler to receive streaming events (typically AGENT_FRAME).
     * @return True if the request was started, false if it couldn't be started.
     */
    bool AskStreamWithToolsAsync( const nlohmann::json& aMessages, const std::string& aSystem,
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
     * Load API keys from environment file.
     * Searches for .env file in standard locations.
     * @param aEnvFilePath Optional explicit path to .env file.
     * @return True if keys were loaded successfully.
     */
    static bool LoadApiKeys( const std::string& aEnvFilePath = "" );

    /**
     * Get the OpenAI API key.
     */
    static const std::string& GetOpenAIKey() { return s_openaiApiKey; }

    /**
     * Get the Anthropic API key.
     */
    static const std::string& GetAnthropicKey() { return s_anthropicApiKey; }

private:
    AGENT_FRAME* m_parent;
    AGENT_AUTH*  m_auth = nullptr;
    std::string  m_modelName;

    // Async request state
    std::atomic<bool> m_requestInProgress;
    std::atomic<bool> m_cancelRequested;

    // API keys loaded from .env file
    static std::string s_openaiApiKey;
    static std::string s_anthropicApiKey;
    static bool        s_keysLoaded;

    // Helper to request via OpenAI API
    bool AskStreamOpenAI( const nlohmann::json& aMessages, const std::string& aSystem, const std::string& aPayload,
                          std::function<void( const std::string& )> aCallback );

    // Helper to request via Anthropic API
    bool AskStreamAnthropic( const nlohmann::json& aMessages, const std::string& aSystem, const std::string& aPayload,
                             std::function<void( const std::string& )> aCallback );

    // Helper to request via Anthropic API with native tools (blocking)
    bool AskStreamAnthropicWithTools( const nlohmann::json& aMessages, const std::string& aSystem,
                                      const std::vector<LLM_TOOL>& aTools,
                                      std::function<void( const LLM_EVENT& )> aCallback );
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
                        const std::string& aSystem,
                        const std::vector<LLM_TOOL>& aTools );

    virtual ~LLM_REQUEST_THREAD();

protected:
    virtual void* Entry() override;

private:
    AGENT_LLM_CLIENT*     m_client;
    wxEvtHandler*         m_handler;
    std::string           m_model;
    nlohmann::json        m_messages;
    std::string           m_system;
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
        bool              inThinking;       // Currently accumulating thinking block
    };

    // Parse SSE events and post to main thread
    void ParseAndPostEvents( StreamContext& ctx, const std::string& data );
};

#endif // AGENT_LLM_CLIENT_H
