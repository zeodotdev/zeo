#include "agent_openai_client_temp.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <ki_exception.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <sstream>

// --- SECRET KEY PLACEHOLDER ---
// TODO: Replace with valid key or load from secure storage
// defined as a macro to be easily spotted
#define OPENAI_API_KEY_MOONSHINE                                                                                       \
    "sk-proj-cKXdi_Pad6DZcl3KUmIBACkpSRKY6tI9QuY6jPzcg9yEFvlwNEYo47i4Dg1vSDDoeWOnIsCPWXT3BlbkFJGTOB-J1E99c_o-DOKn1Lu_" \
    "Aqbe3uInSU9zkI0zM_eHfxj1LmUIDRvFwyb3fKQxqJe45enV1sIA"

#define OPENAI_API_KEY                                                                                                 \
    "sk-proj-t2JQs-qzIqBMcBypENk5dceWZWTFwfTtTMzNS0D-"                                                                 \
    "m2h9XFoe9LeQYbRnE2TVOpOTEQEhgbdrXIT3BlbkFJ77EH9uZhjvSVZC8MjGxmq16CWFfvKoqQZk839cWDIjwTAsHlf16nCyL5MzsKsoZSyipAdR" \
    "xXUA"

using json = nlohmann::json;

OPENAI_CLIENT_TEMP::OPENAI_CLIENT_TEMP()
{
}

OPENAI_CLIENT_TEMP::~OPENAI_CLIENT_TEMP()
{
}

std::string OPENAI_CLIENT_TEMP::Ask( const std::string& aPrompt, const std::string& aSystem,
                                     const std::string& aPayload )
{
    KICAD_CURL_EASY curl;

    // 1. Setup URL
    curl.SetURL( "https://api.openai.com/v1/chat/completions" );

    // 2. Setup Headers
    curl.SetHeader( "Content-Type", "application/json" );
    std::string authHeader = "Authorization: Bearer " + std::string( OPENAI_API_KEY );
    curl.SetHeader( "Authorization", "Bearer " + std::string( OPENAI_API_KEY ) );

    // 3. Construct JSON Body
    // Combine System + Payload into system message or separate context?
    // For now, let's append payload to system or user prompt.
    // Let's put payload in system context for better separation.

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
    {
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;
    }

    json requestBody;
    requestBody["model"] = "gpt-4o"; // or gpt-3.5-turbo
    requestBody["messages"] = json::array( { { { "role", "system" }, { "content", fullSystemPrompt } },
                                             { { "role", "user" }, { "content", aPrompt } } } );
    // requestBody["temperature"] = 0.7;

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    // 4. Perform Request
    wxLogMessage( "OPENAI_CLIENT_TEMP: Sending request to OpenAI..." );
    try
    {
        curl.Perform();

        // 5. Parse Response
        std::string responseData = curl.GetBuffer();
        if( responseData.empty() )
            return "Error: Empty response from OpenAI.";

        int code = curl.GetResponseStatusCode();
        if( code != 200 )
            return "Error: API returned status " + std::to_string( code ) + "\nResponse: " + responseData;

        // Parse JSON response
        auto jsonResponse = json::parse( responseData );
        if( jsonResponse.contains( "choices" ) && !jsonResponse["choices"].empty() )
        {
            return jsonResponse["choices"][0]["message"]["content"].get<std::string>();
        }
        else
        {
            return "Error: Unexpected JSON format.\n" + responseData;
        }
    }
    catch( const IO_ERROR& e )
    {
        return "Error: Network request failed: " + e.What().ToStdString();
    }
    catch( const std::exception& e )
    {
        return "Error: Exception: " + std::string( e.what() );
    }
}

// Context for stream callback
struct StreamContext
{
    std::function<void( const std::string& )> callback;
    std::string                               buffer;       // For accumulating SSE lines
    std::string                               fullResponse; // For error reporting
};

// Write callback for streaming
static size_t StreamWriteCallback( void* aContents, size_t aSize, size_t aNmemb, void* aUserp )
{
    size_t         realSize = aSize * aNmemb;
    StreamContext* ctx = static_cast<StreamContext*>( aUserp );

    // Append new data to buffer
    ctx->buffer.append( static_cast<const char*>( aContents ), realSize );
    ctx->fullResponse.append( static_cast<const char*>( aContents ), realSize );

    // Process complete lines
    std::stringstream ss( ctx->buffer );
    std::string       line;
    std::string       remainder;

    // We need to handle the case where the last line is incomplete.
    // std::getline reads until delimiter, but doesn't tell us if it hit EOF or delimiter if delimiter is at end.
    // A robust way: find last newline in buffer.
    size_t lastNewline = ctx->buffer.find_last_of( "\n" );
    if( lastNewline == std::string::npos )
    {
        // No newline yet, keep buffering
        return realSize;
    }

    // Process up to last newline
    std::string processChunk = ctx->buffer.substr( 0, lastNewline + 1 );
    ctx->buffer = ctx->buffer.substr( lastNewline + 1 );

    std::stringstream lineStream( processChunk );
    while( std::getline( lineStream, line ) )
    {
        // Trim CR if present (Windows/HTTP standard)
        if( !line.empty() && line.back() == '\r' )
            line.pop_back();

        if( line.rfind( "data: ", 0 ) == 0 ) // Starts with "data: "
        {
            std::string payload = line.substr( 6 ); // Skip "data: "
            if( payload == "[DONE]" )
                continue;

            try
            {
                auto jsonPayload = json::parse( payload );
                if( jsonPayload.contains( "choices" ) && !jsonPayload["choices"].empty()
                    && jsonPayload["choices"][0].contains( "delta" )
                    && jsonPayload["choices"][0]["delta"].contains( "content" ) )
                {
                    std::string content = jsonPayload["choices"][0]["delta"]["content"].get<std::string>();
                    if( ctx->callback )
                    {
                        ctx->callback( content );
                    }
                }
            }
            catch( ... )
            {
                // Ignore parse errors for partial/malformed lines
            }
        }
        else if( line.find( "\"error\":" ) != std::string::npos )
        {
            // heuristic check for error in the stream
            printf( "Stream Error Line: %s\n", line.c_str() );
        }
    }

    return realSize;
}

bool OPENAI_CLIENT_TEMP::AskStream( const std::string& aPrompt, const std::string& aSystem, const std::string& aPayload,
                                    std::function<void( const std::string& )> aCallback )
{
    KICAD_CURL_EASY curl;

    // Enable verbose for debugging
    // curl_easy_setopt( curl.GetCurl(), CURLOPT_VERBOSE, 1L );

    curl.SetURL( "https://api.openai.com/v1/chat/completions" );
    curl.SetHeader( "Content-Type", "application/json" );
    curl.SetHeader( "Authorization", "Bearer " + std::string( OPENAI_API_KEY ) );

    std::string fullSystemPrompt = aSystem;
    if( !aPayload.empty() )
        fullSystemPrompt += "\n\nCONTEXT:\n" + aPayload;

    json requestBody;
    requestBody["model"] = "gpt-4o";
    requestBody["messages"] = json::array( { { { "role", "system" }, { "content", fullSystemPrompt } },
                                             { { "role", "user" }, { "content", aPrompt } } } );
    requestBody["stream"] = true;

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    StreamContext ctx;
    ctx.callback = aCallback;

    CURL* rawCurl = curl.GetCurl();
    curl_easy_setopt( rawCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback );
    curl_easy_setopt( rawCurl, CURLOPT_WRITEDATA, &ctx );

    try
    {
        // Debug print
        printf( "Sending OpenAI Request...\n" );

        curl.Perform();

        long http_code = 0;
        curl_easy_getinfo( rawCurl, CURLINFO_RESPONSE_CODE, &http_code );

        printf( "OpenAI Response Code: %ld\n", http_code );

        if( http_code != 200 )
        {
            wxLogMessage( "OpenAI Error %ld: %s", http_code, ctx.fullResponse.c_str() );
            // Pass error to user if possible, or just fail
            if( aCallback )
                aCallback( "Error: API " + std::to_string( http_code ) + "\n" + ctx.fullResponse );
            return false;
        }

        return true;
    }
    catch( const std::exception& e )
    {
        wxLogMessage( "Stream Error: %s", e.what() );
        printf( "Stream Exception: %s\n", e.what() );
        return false;
    }
}
