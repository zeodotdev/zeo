#include "agent_llm_client.h"
#include "agent_frame.h"
#include <id.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <ki_exception.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <sstream>

// --- SECRET KEY PLACEHOLDERS ---
// TODO: Load from secure storage or settings
#define OPENAI_API_KEY                                                                                                 \
    "sk-proj-t2JQs-qzIqBMcBypENk5dceWZWTFwfTtTMzNS0D-"                                                                 \
    "m2h9XFoe9LeQYbRnE2TVOpOTEQEhgbdrXIT3BlbkFJ77EH9uZhjvSVZC8MjGxmq16CWFfvKoqQZk839cWDIjwTAsHlf16nCyL5MzsKsoZSyipAdR" \
    "xXUA"

#define ANTHROPIC_API_KEY                                                                                              \
    "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"

using json = nlohmann::json;

AGENT_LLM_CLIENT::AGENT_LLM_CLIENT( AGENT_FRAME* aParent ) :
        m_parent( aParent ),
        m_modelName( "GPT-4o" )
{
}

AGENT_LLM_CLIENT::~AGENT_LLM_CLIENT()
{
}

void AGENT_LLM_CLIENT::SetModel( const std::string& aModelName )
{
    m_modelName = aModelName;
}

std::string AGENT_LLM_CLIENT::Ask( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload )
{
    return "Error: Synchronous Ask not fully implemented for multi-model. Use AskStream.";
}

// Context for stream callback
struct StreamContext
{
    std::function<void( const std::string& )> callback;
    std::string                               buffer;       // For accumulating data
    std::string                               fullResponse; // For error reporting
    bool                                      isAnthropic;  // Flag for parser
};

static size_t StreamWriteCallback( void* aContents, size_t aSize, size_t aNmemb, void* aUserp )
{
    size_t         realSize = aSize * aNmemb;
    StreamContext* ctx = static_cast<StreamContext*>( aUserp );

    // Append new data
    ctx->buffer.append( static_cast<const char*>( aContents ), realSize );
    ctx->fullResponse.append( static_cast<const char*>( aContents ), realSize );

    // Process complete lines
    size_t lastNewline = ctx->buffer.find_last_of( "\n" );
    if( lastNewline == std::string::npos )
        return realSize;

    std::string processChunk = ctx->buffer.substr( 0, lastNewline + 1 );
    ctx->buffer = ctx->buffer.substr( lastNewline + 1 );

    std::stringstream lineStream( processChunk );
    std::string       line;
    while( std::getline( lineStream, line ) )
    {
        if( !line.empty() && line.back() == '\r' )
            line.pop_back();

        if( ctx->isAnthropic )
        {
            // Anthropic SSE Format: event: ..., data: JSON
            if( line.rfind( "data: ", 0 ) == 0 )
            {
                std::string payload = line.substr( 6 );
                try
                {
                    auto j = json::parse( payload );
                    if( j.contains( "type" ) )
                    {
                        std::string type = j["type"];
                        if( type == "content_block_delta" && j.contains( "delta" ) && j["delta"].contains( "text" ) )
                        {
                            std::string content = j["delta"]["text"];
                            if( ctx->callback )
                                ctx->callback( content );
                        }
                    }
                }
                catch( ... )
                {
                }
            }
        }
        else // OpenAI
        {
            if( line.rfind( "data: ", 0 ) == 0 )
            {
                std::string payload = line.substr( 6 );
                if( payload == "[DONE]" )
                    continue;
                try
                {
                    auto j = json::parse( payload );
                    if( j.contains( "choices" ) && !j["choices"].empty() && j["choices"][0].contains( "delta" )
                        && j["choices"][0]["delta"].contains( "content" ) )
                    {
                        std::string content = j["choices"][0]["delta"]["content"].get<std::string>();
                        if( ctx->callback )
                            ctx->callback( content );
                    }
                }
                catch( ... )
                {
                }
            }
        }
    }

    return realSize;
}

bool AGENT_LLM_CLIENT::AskStream( const nlohmann::json& aMessages, const std::string& aSystem,
                                  const std::string& aPayload, std::function<void( const std::string& )> aCallback )
{
    // Route based on model name
    if( m_modelName.find( "Claude" ) != std::string::npos )
    {
        return AskStreamAnthropic( aMessages, aSystem, aPayload, aCallback );
    }
    else
    {
        return AskStreamOpenAI( aMessages, aSystem, aPayload, aCallback );
    }
}

bool AGENT_LLM_CLIENT::AskStreamOpenAI( const nlohmann::json& aMessages, const std::string& aSystem,
                                        const std::string&                        aPayload,
                                        std::function<void( const std::string& )> aCallback )
{
    KICAD_CURL_EASY curl;
    curl.SetURL( "https://api.openai.com/v1/chat/completions" );
    curl.SetHeader( "Content-Type", "application/json" );
    curl.SetHeader( "Authorization", "Bearer " + std::string( OPENAI_API_KEY ) );

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;

    json requestBody;
    requestBody["model"] = "gpt-4o"; // Hardcoded default for OpenAI path

    // Prepend system message to history
    json fullMessages = json::array();
    fullMessages.push_back( { { "role", "system" }, { "content", fullSystemPrompt } } );
    for( const auto& msg : aMessages )
    {
        fullMessages.push_back( msg );
    }

    requestBody["messages"] = fullMessages;
    requestBody["stream"] = true;

    // TODO: Add Tools if maintaining OpenAI tool support

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    StreamContext ctx;
    ctx.callback = aCallback;
    ctx.isAnthropic = false;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        curl.Perform();
        long http_code = curl.GetResponseStatusCode();
        if( http_code != 200 )
        {
            if( aCallback )
                aCallback( "Error: OpenAI API " + std::to_string( http_code ) + "\n" + ctx.fullResponse );
            return false;
        }
        return true;
    }
    catch( const std::exception& e )
    {
        if( aCallback )
            aCallback( "Error: " + std::string( e.what() ) );
        return false;
    }
}

bool AGENT_LLM_CLIENT::AskStreamAnthropic( const nlohmann::json& aMessages, const std::string& aSystem,
                                           const std::string&                        aPayload,
                                           std::function<void( const std::string& )> aCallback )
{
    KICAD_CURL_EASY curl;
    curl.SetURL( "https://api.anthropic.com/v1/messages" );
    curl.SetHeader( "content-type", "application/json" );
    curl.SetHeader( "x-api-key", ANTHROPIC_API_KEY );
    curl.SetHeader( "anthropic-version", "2023-06-01" );

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;

    // Map UI names to API names
    std::string apiModel = "claude-3-5-sonnet-20240620";
    if( m_modelName == "Claude 3 Opus" )
        apiModel = "claude-3-opus-20240229";

    // wxLogMessage( "Anthropic Request: Model=%s", apiModel.c_str() ); // Removed to prevent pop-ups

    json requestBody;
    requestBody["model"] = apiModel;
    requestBody["system"] = fullSystemPrompt;
    requestBody["messages"] = aMessages;
    requestBody["max_tokens"] = 4096;
    requestBody["stream"] = true;

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    StreamContext ctx;
    ctx.callback = aCallback;
    ctx.isAnthropic = true;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        curl.Perform();
        long http_code = curl.GetResponseStatusCode();
        if( http_code != 200 )
        {
            if( aCallback )
                aCallback( "Error: Anthropic API " + std::to_string( http_code ) + "\n" + ctx.fullResponse );
            return false;
        }
        return true;
    }
    catch( const std::exception& e )
    {
        if( aCallback )
            aCallback( "Error: " + std::string( e.what() ) );
        return false;
    }
}
