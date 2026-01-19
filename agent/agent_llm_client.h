#ifndef AGENT_LLM_CLIENT_H
#define AGENT_LLM_CLIENT_H

#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

class AGENT_FRAME;

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
     * Send a streaming chat completion request with native tool calling.
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
    std::string  m_modelName;

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

    // Helper to request via Anthropic API with native tools
    bool AskStreamAnthropicWithTools( const nlohmann::json& aMessages, const std::string& aSystem,
                                      const std::vector<LLM_TOOL>& aTools,
                                      std::function<void( const LLM_EVENT& )> aCallback );
};

#endif // AGENT_LLM_CLIENT_H
