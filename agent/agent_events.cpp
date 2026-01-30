#include "agent_events.h"
#include <wx/event.h>

// Define the custom event types for tool execution
wxDEFINE_EVENT( EVT_TOOL_EXECUTION_COMPLETE, wxCommandEvent );
wxDEFINE_EVENT( EVT_TOOL_EXECUTION_ERROR, wxCommandEvent );
wxDEFINE_EVENT( EVT_TOOL_EXECUTION_PROGRESS, wxCommandEvent );
wxDEFINE_EVENT( EVT_TOOL_EXECUTION_TIMEOUT, wxCommandEvent );

// Define the custom event types for LLM streaming
wxDEFINE_EVENT( EVT_LLM_STREAM_CHUNK, wxThreadEvent );
wxDEFINE_EVENT( EVT_LLM_STREAM_COMPLETE, wxThreadEvent );
wxDEFINE_EVENT( EVT_LLM_STREAM_ERROR, wxThreadEvent );


void PostToolResult( wxEvtHandler* aHandler, const ToolExecutionResult& aResult )
{
    if( !aHandler )
        return;

    wxCommandEvent* event = new wxCommandEvent( EVT_TOOL_EXECUTION_COMPLETE );

    // Store a copy of the result in the event's client data
    // The event handler is responsible for deleting this
    event->SetClientData( new ToolExecutionResult( aResult ) );

    // Thread-safe event posting - the event will be processed on the main thread
    wxQueueEvent( aHandler, event );
}


void PostToolError( wxEvtHandler* aHandler, const std::string& aToolUseId,
                    const std::string& aErrorMessage )
{
    if( !aHandler )
        return;

    wxCommandEvent* event = new wxCommandEvent( EVT_TOOL_EXECUTION_ERROR );

    ToolExecutionResult* result = new ToolExecutionResult();
    result->tool_use_id = aToolUseId;
    result->success = false;
    result->error_message = aErrorMessage;

    event->SetClientData( result );
    wxQueueEvent( aHandler, event );
}


void PostToolProgress( wxEvtHandler* aHandler, const ToolExecutionProgress& aProgress )
{
    if( !aHandler )
        return;

    wxCommandEvent* event = new wxCommandEvent( EVT_TOOL_EXECUTION_PROGRESS );

    event->SetClientData( new ToolExecutionProgress( aProgress ) );
    wxQueueEvent( aHandler, event );
}


void PostToolTimeout( wxEvtHandler* aHandler, const std::string& aToolUseId )
{
    if( !aHandler )
        return;

    wxCommandEvent* event = new wxCommandEvent( EVT_TOOL_EXECUTION_TIMEOUT );

    ToolExecutionResult* result = new ToolExecutionResult();
    result->tool_use_id = aToolUseId;
    result->success = false;
    result->error_message = "Tool execution timed out";

    event->SetClientData( result );
    wxQueueEvent( aHandler, event );
}


// ============================================================================
// LLM Streaming Event Helpers
// ============================================================================

void PostLLMChunk( wxEvtHandler* aHandler, const LLMStreamChunk& aChunk )
{
    if( !aHandler )
        return;

    wxThreadEvent* event = new wxThreadEvent( EVT_LLM_STREAM_CHUNK );

    // Store a copy of the chunk data in the event's payload
    // Using SetPayload for thread-safe data transfer
    event->SetPayload( new LLMStreamChunk( aChunk ) );

    // Thread-safe event posting
    wxQueueEvent( aHandler, event );
}


void PostLLMComplete( wxEvtHandler* aHandler, const LLMStreamComplete& aComplete )
{
    if( !aHandler )
        return;

    wxThreadEvent* event = new wxThreadEvent( EVT_LLM_STREAM_COMPLETE );

    event->SetPayload( new LLMStreamComplete( aComplete ) );
    wxQueueEvent( aHandler, event );
}


void PostLLMError( wxEvtHandler* aHandler, const std::string& aErrorMessage )
{
    if( !aHandler )
        return;

    wxThreadEvent* event = new wxThreadEvent( EVT_LLM_STREAM_ERROR );

    LLMStreamComplete* complete = new LLMStreamComplete();
    complete->success = false;
    complete->error_message = aErrorMessage;

    event->SetPayload( complete );
    wxQueueEvent( aHandler, event );
}


