#ifndef CHAT_EVENTS_H
#define CHAT_EVENTS_H

#include <wx/event.h>
#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <string>

// ============================================================================
// Event declarations
// These events are emitted by CHAT_CONTROLLER and handled by AGENT_FRAME
// ============================================================================

wxDECLARE_EVENT( EVT_CHAT_TEXT_DELTA, wxThreadEvent );      ///< Text chunk arrived
wxDECLARE_EVENT( EVT_CHAT_THINKING_START, wxThreadEvent );  ///< Thinking block started
wxDECLARE_EVENT( EVT_CHAT_THINKING_DELTA, wxThreadEvent );  ///< Thinking chunk arrived
wxDECLARE_EVENT( EVT_CHAT_THINKING_DONE, wxThreadEvent );   ///< Thinking block ended
wxDECLARE_EVENT( EVT_CHAT_TOOL_GENERATING, wxThreadEvent ); ///< Tool call being generated (streaming)
wxDECLARE_EVENT( EVT_CHAT_TOOL_START, wxThreadEvent );      ///< Tool execution starting
wxDECLARE_EVENT( EVT_CHAT_TOOL_COMPLETE, wxThreadEvent );   ///< Tool finished
wxDECLARE_EVENT( EVT_CHAT_TURN_COMPLETE, wxThreadEvent );   ///< Ready for next input
wxDECLARE_EVENT( EVT_CHAT_ERROR, wxThreadEvent );           ///< Error occurred
wxDECLARE_EVENT( EVT_CHAT_STATE_CHANGED, wxThreadEvent );   ///< State machine changed
wxDECLARE_EVENT( EVT_CHAT_TITLE_DELTA, wxThreadEvent );     ///< Title chunk arrived (streaming)
wxDECLARE_EVENT( EVT_CHAT_TITLE_GENERATED, wxThreadEvent ); ///< Title complete
wxDECLARE_EVENT( EVT_CHAT_HISTORY_LOADED, wxThreadEvent );  ///< Chat loaded from history

// ============================================================================
// Event payload structures
// These are heap-allocated and passed via wxThreadEvent::SetPayload()
// The receiver is responsible for deleting them
// ============================================================================

/**
 * Payload for EVT_CHAT_TEXT_DELTA events.
 * Contains the accumulated response text so far.
 */
struct ChatTextDeltaData
{
    std::string fullResponse;     ///< Complete accumulated response
    std::string deltaText;        ///< Just the new text in this chunk

    ChatTextDeltaData() = default;
    ChatTextDeltaData( const std::string& aFull, const std::string& aDelta )
        : fullResponse( aFull ), deltaText( aDelta ) {}
};

/**
 * Payload for EVT_CHAT_THINKING_START events.
 * Signals that a thinking block has started.
 */
struct ChatThinkingStartData
{
    int thinkingIndex;            ///< Index for this thinking block (for toggle state)

    ChatThinkingStartData() : thinkingIndex( 0 ) {}
    explicit ChatThinkingStartData( int aIndex ) : thinkingIndex( aIndex ) {}
};

/**
 * Payload for EVT_CHAT_THINKING_DELTA events.
 * Contains the accumulated thinking text.
 */
struct ChatThinkingDeltaData
{
    wxString fullThinking;        ///< Complete accumulated thinking
    wxString deltaText;           ///< Just the new text in this chunk

    ChatThinkingDeltaData() = default;
    ChatThinkingDeltaData( const wxString& aFull, const wxString& aDelta )
        : fullThinking( aFull ), deltaText( aDelta ) {}
};

/**
 * Payload for EVT_CHAT_THINKING_DONE events.
 * Signals that a thinking block has ended.
 */
struct ChatThinkingDoneData
{
    wxString finalThinking;       ///< Final accumulated thinking text

    ChatThinkingDoneData() = default;
    explicit ChatThinkingDoneData( const wxString& aThinking ) : finalThinking( aThinking ) {}
};

/**
 * Payload for EVT_CHAT_TOOL_GENERATING events.
 * Signals that the LLM is generating a tool call (before execution).
 */
struct ChatToolGeneratingData
{
    std::string toolId;           ///< Unique tool use ID
    std::string toolName;         ///< Tool name (e.g., "sch_modify")

    ChatToolGeneratingData() = default;
    ChatToolGeneratingData( const std::string& aId, const std::string& aName )
        : toolId( aId ), toolName( aName ) {}
};

/**
 * Payload for EVT_CHAT_TOOL_START events.
 * Contains information about the tool being executed.
 */
struct ChatToolStartData
{
    std::string    toolId;        ///< Unique tool use ID
    std::string    toolName;      ///< Tool name (e.g., "run_shell")
    std::string    description;   ///< Human-readable description
    nlohmann::json input;         ///< Tool input parameters

    ChatToolStartData() = default;
    ChatToolStartData( const std::string& aId, const std::string& aName,
                       const std::string& aDesc, const nlohmann::json& aInput )
        : toolId( aId ), toolName( aName ), description( aDesc ), input( aInput ) {}
};

/**
 * Payload for EVT_CHAT_TOOL_COMPLETE events.
 * Contains the result of tool execution.
 */
struct ChatToolCompleteData
{
    std::string toolId;           ///< Unique tool use ID
    std::string toolName;         ///< Tool name
    std::string result;           ///< Tool execution result (text)
    bool        success;          ///< Whether tool succeeded
    bool        isPythonError;    ///< Whether result is a Python traceback
    bool        hasImage;         ///< Whether result contains an image
    std::string imageBase64;      ///< Base64-encoded image data (if hasImage)
    std::string imageMediaType;   ///< Image MIME type e.g. "image/png" (if hasImage)

    ChatToolCompleteData() : success( false ), isPythonError( false ), hasImage( false ) {}
    ChatToolCompleteData( const std::string& aId, const std::string& aName,
                          const std::string& aResult, bool aSuccess, bool aIsPythonError = false )
        : toolId( aId ), toolName( aName ), result( aResult ),
          success( aSuccess ), isPythonError( aIsPythonError ), hasImage( false ) {}
};

/**
 * Payload for EVT_CHAT_TURN_COMPLETE events.
 * Signals that the assistant's turn is complete.
 */
struct ChatTurnCompleteData
{
    bool hasToolCalls;            ///< Whether this turn included tool calls
    bool continuing;              ///< Whether continuation is coming (max_tokens hit)

    ChatTurnCompleteData() : hasToolCalls( false ), continuing( false ) {}
    explicit ChatTurnCompleteData( bool aHasToolCalls, bool aContinuing = false )
        : hasToolCalls( aHasToolCalls ), continuing( aContinuing ) {}
};

/**
 * Payload for EVT_CHAT_ERROR events.
 * Contains error information.
 */
struct ChatErrorData
{
    std::string message;          ///< Error message
    bool        canRetry;         ///< Whether retry is possible
    bool        isContextError;   ///< Whether this is a context length error

    ChatErrorData() : canRetry( false ), isContextError( false ) {}
    ChatErrorData( const std::string& aMsg, bool aCanRetry = false, bool aIsContextError = false )
        : message( aMsg ), canRetry( aCanRetry ), isContextError( aIsContextError ) {}
};

/**
 * Payload for EVT_CHAT_STATE_CHANGED events.
 * Contains the new state.
 */
struct ChatStateChangedData
{
    int oldState;                 ///< Previous state (AgentConversationState)
    int newState;                 ///< New state (AgentConversationState)

    ChatStateChangedData() : oldState( 0 ), newState( 0 ) {}
    ChatStateChangedData( int aOld, int aNew ) : oldState( aOld ), newState( aNew ) {}
};

/**
 * Payload for EVT_CHAT_TITLE_DELTA events.
 * Contains partial title during streaming animation.
 */
struct ChatTitleDeltaData
{
    std::string partialTitle;     ///< Title text revealed so far

    ChatTitleDeltaData() = default;
    explicit ChatTitleDeltaData( const std::string& aPartial ) : partialTitle( aPartial ) {}
};

/**
 * Payload for EVT_CHAT_TITLE_GENERATED events.
 * Contains the complete generated title.
 */
struct ChatTitleGeneratedData
{
    std::string title;            ///< Generated chat title

    ChatTitleGeneratedData() = default;
    explicit ChatTitleGeneratedData( const std::string& aTitle ) : title( aTitle ) {}
};

/**
 * Payload for EVT_CHAT_HISTORY_LOADED events.
 * Signals that a chat was loaded from history.
 */
struct ChatHistoryLoadedData
{
    std::string chatId;           ///< Loaded chat ID
    std::string title;            ///< Chat title

    ChatHistoryLoadedData() = default;
    ChatHistoryLoadedData( const std::string& aId, const std::string& aTitle )
        : chatId( aId ), title( aTitle ) {}
};

// ============================================================================
// Helper functions for posting events
// ============================================================================

/**
 * Post a chat event with payload to the given handler.
 * The payload is heap-allocated; the receiver must delete it.
 */
template<typename T>
void PostChatEvent( wxEvtHandler* aHandler, wxEventType aType, const T& aData )
{
    if( !aHandler )
        return;

    wxThreadEvent* event = new wxThreadEvent( aType );
    event->SetPayload( new T( aData ) );
    wxQueueEvent( aHandler, event );
}

/**
 * Post a chat event with no payload.
 */
inline void PostChatEvent( wxEvtHandler* aHandler, wxEventType aType )
{
    if( !aHandler )
        return;

    wxQueueEvent( aHandler, new wxThreadEvent( aType ) );
}

#endif // CHAT_EVENTS_H
