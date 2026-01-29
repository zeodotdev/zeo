#ifndef AGENT_EVENTS_H
#define AGENT_EVENTS_H

#include <wx/event.h>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

// Forward declaration
class wxEvtHandler;

// Custom event types for async tool execution
wxDECLARE_EVENT( EVT_TOOL_EXECUTION_COMPLETE, wxCommandEvent );
wxDECLARE_EVENT( EVT_TOOL_EXECUTION_ERROR, wxCommandEvent );
wxDECLARE_EVENT( EVT_TOOL_EXECUTION_PROGRESS, wxCommandEvent );
wxDECLARE_EVENT( EVT_TOOL_EXECUTION_TIMEOUT, wxCommandEvent );

// Custom event types for async LLM streaming
wxDECLARE_EVENT( EVT_LLM_STREAM_CHUNK, wxThreadEvent );
wxDECLARE_EVENT( EVT_LLM_STREAM_COMPLETE, wxThreadEvent );
wxDECLARE_EVENT( EVT_LLM_STREAM_ERROR, wxThreadEvent );

// Custom event type for title generation
wxDECLARE_EVENT( EVT_TITLE_GENERATED, wxThreadEvent );

/**
 * Result data from an async tool execution
 */
struct ToolExecutionResult
{
    std::string tool_use_id;      // Unique ID for this tool call
    std::string tool_name;        // Name of the tool that was executed
    std::string result;           // The output/result from the tool
    bool        success;          // Whether execution succeeded
    std::string error_message;    // Error message if success is false
    long        execution_time_ms; // How long the tool took to execute

    ToolExecutionResult() : success( false ), execution_time_ms( 0 ) {}
};

/**
 * Progress data for long-running tool executions
 */
struct ToolExecutionProgress
{
    std::string tool_use_id;      // Unique ID for this tool call
    std::string output_chunk;     // Incremental output from the tool
    int         progress_percent; // Optional progress percentage (-1 if unknown)

    ToolExecutionProgress() : progress_percent( -1 ) {}
};

/**
 * Helper function to post a tool execution result to a handler.
 * This is thread-safe and can be called from any thread.
 *
 * @param aHandler The event handler to receive the result (typically AGENT_FRAME)
 * @param aResult The result data to send
 */
void PostToolResult( wxEvtHandler* aHandler, const ToolExecutionResult& aResult );

/**
 * Helper function to post a tool execution error to a handler.
 * This is thread-safe and can be called from any thread.
 *
 * @param aHandler The event handler to receive the error
 * @param aToolUseId The tool use ID that failed
 * @param aErrorMessage Description of the error
 */
void PostToolError( wxEvtHandler* aHandler, const std::string& aToolUseId,
                    const std::string& aErrorMessage );

/**
 * Helper function to post progress updates for a tool execution.
 * This is thread-safe and can be called from any thread.
 *
 * @param aHandler The event handler to receive the progress
 * @param aProgress The progress data to send
 */
void PostToolProgress( wxEvtHandler* aHandler, const ToolExecutionProgress& aProgress );

/**
 * Helper function to post a timeout event for a tool execution.
 * This is thread-safe and can be called from any thread.
 *
 * @param aHandler The event handler to receive the timeout
 * @param aToolUseId The tool use ID that timed out
 */
void PostToolTimeout( wxEvtHandler* aHandler, const std::string& aToolUseId );


// ============================================================================
// LLM Streaming Events
// ============================================================================

/**
 * Event types for LLM streaming chunks.
 * These mirror LLM_EVENT_TYPE but are used for async event posting.
 */
enum class LLMChunkType
{
    TEXT,              // Text content delta
    THINKING_START,    // Thinking block started (show loading)
    THINKING,          // Thinking block content delta (streamed incrementally)
    THINKING_DONE,     // Thinking block complete
    TOOL_USE,          // Tool call with id, name, input
    TOOL_USE_DONE,     // All tool calls parsed, ready to execute
    END_TURN,          // Model finished (no more tool calls)
    ERROR,             // Error occurred
    CONTEXT_STATUS,    // Context usage status from server
    CONTEXT_COMPACTING,// Server is compacting context (show "Compacting...")
    CONTEXT_EXHAUSTED, // HTTP 400 context exhausted with recovery messages
    CONTEXT_TRUNCATED  // Mid-response truncation with recovery messages
};

/**
 * Data for an LLM streaming chunk event.
 * Posted from the background thread to the main thread.
 */
struct LLMStreamChunk
{
    LLMChunkType   type;
    std::string    text;              // For TEXT events
    std::string    thinking_text;     // For THINKING events (summarized thinking)
    std::string    thinking_signature; // For THINKING_DONE events (signature for API)
    std::string    tool_use_id;       // For TOOL_USE events
    std::string    tool_name;      // For TOOL_USE events
    std::string    tool_input_json; // For TOOL_USE events (serialized JSON)
    std::string    error_message;  // For ERROR events

    // Context status fields (for CONTEXT_STATUS events)
    int            context_percent_used;  // 0-100
    bool           context_compacted;     // true if context was compacted

    // Context recovery fields (for CONTEXT_EXHAUSTED/CONTEXT_TRUNCATED events)
    nlohmann::json summarized_messages;   // Compacted messages for retry

    LLMStreamChunk() : type( LLMChunkType::TEXT ),
                       context_percent_used( 0 ),
                       context_compacted( false ) {}
};

/**
 * Data for LLM stream completion event.
 */
struct LLMStreamComplete
{
    bool        success;
    long        http_status_code;
    std::string error_message;    // If success is false

    LLMStreamComplete() : success( true ), http_status_code( 200 ) {}
};

/**
 * Helper function to post an LLM stream chunk to the main thread.
 * This is thread-safe and should be called from the background thread.
 *
 * @param aHandler The event handler to receive the chunk (typically AGENT_FRAME)
 * @param aChunk The chunk data to send
 */
void PostLLMChunk( wxEvtHandler* aHandler, const LLMStreamChunk& aChunk );

/**
 * Helper function to post an LLM stream completion event.
 * This is thread-safe and should be called from the background thread.
 *
 * @param aHandler The event handler to receive the completion
 * @param aComplete The completion data
 */
void PostLLMComplete( wxEvtHandler* aHandler, const LLMStreamComplete& aComplete );

/**
 * Helper function to post an LLM stream error event.
 * This is thread-safe and should be called from the background thread.
 *
 * @param aHandler The event handler to receive the error
 * @param aErrorMessage Description of the error
 */
void PostLLMError( wxEvtHandler* aHandler, const std::string& aErrorMessage );

/**
 * Data for title generation event.
 */
struct TitleGeneratedData
{
    std::string title;
    std::string conversationId;

    TitleGeneratedData() {}
    TitleGeneratedData( const std::string& aTitle, const std::string& aConversationId )
        : title( aTitle ), conversationId( aConversationId ) {}
};

/**
 * Helper function to post a generated title to the main thread.
 * This is thread-safe and should be called from the background thread.
 *
 * @param aHandler The event handler to receive the title (typically AGENT_FRAME)
 * @param aTitle The generated title string
 * @param aConversationId The conversation ID this title belongs to
 */
void PostTitleGenerated( wxEvtHandler* aHandler, const std::string& aTitle, const std::string& aConversationId );

#endif // AGENT_EVENTS_H
