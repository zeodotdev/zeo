#ifndef AGENT_LLM_CLIENT_H
#define AGENT_LLM_CLIENT_H

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

class AGENT_FRAME;

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
};

#endif // AGENT_LLM_CLIENT_H
