#ifndef AGENT_OPENAI_CLIENT_TEMP_H
#define AGENT_OPENAI_CLIENT_TEMP_H

#include <string>
#include <functional>

/**
 * Temporary OpenAI Client for KiCad Agent.
 * Used to route calls to OpenAI API.
 */
class OPENAI_CLIENT_TEMP
{
public:
    OPENAI_CLIENT_TEMP();
    ~OPENAI_CLIENT_TEMP();

    /**
     * Send a chat completion request to OpenAI.
     * @param aPrompt The user prompt.
     * @param aSystem The system prompt/instruction.
     * @param aPayload Additional context payload (e.g. selection data).
     * @return The response content from OpenAI.
     */
    std::string Ask( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload );

    /**
     * Send a streaming chat completion request to OpenAI.
     * @param aPrompt The user prompt.
     * @param aSystem The system prompt/instruction.
     * @param aPayload Additional context payload (e.g. selection data).
     * @param aCallback The callback function to invoke with partial content updates.
     * @return True if successful, false otherwise.
     */
    bool AskStream( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload,
                    std::function<void( const std::string& )> aCallback );
};

#endif // AGENT_OPENAI_CLIENT_TEMP_H
