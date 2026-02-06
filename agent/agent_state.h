#ifndef AGENT_STATE_H
#define AGENT_STATE_H

#include <string>
#include <vector>
#include <wx/longlong.h>
#include <nlohmann/json.hpp>

/**
 * States for the agent conversation state machine.
 *
 * The agent progresses through these states during a conversation:
 * - IDLE: Ready for user input
 * - WAITING_FOR_LLM: Streaming response from LLM
 * - TOOL_USE_DETECTED: LLM has requested a tool call
 * - EXECUTING_TOOL: Tool is running asynchronously
 * - PROCESSING_TOOL_RESULT: Processing the tool's output
 * - ERROR: An error occurred
 */
enum class AgentConversationState
{
    IDLE,                    // Ready for user input
    WAITING_FOR_LLM,         // Streaming LLM response
    TOOL_USE_DETECTED,       // LLM requested tool use
    EXECUTING_TOOL,          // Tool is running async
    PROCESSING_TOOL_RESULT,  // Processing tool output
    ERROR                    // Error state
};

/**
 * Convert state enum to string for debugging/logging
 */
inline const char* AgentStateToString( AgentConversationState aState )
{
    switch( aState )
    {
    case AgentConversationState::IDLE:                   return "IDLE";
    case AgentConversationState::WAITING_FOR_LLM:        return "WAITING_FOR_LLM";
    case AgentConversationState::TOOL_USE_DETECTED:      return "TOOL_USE_DETECTED";
    case AgentConversationState::EXECUTING_TOOL:         return "EXECUTING_TOOL";
    case AgentConversationState::PROCESSING_TOOL_RESULT: return "PROCESSING_TOOL_RESULT";
    case AgentConversationState::ERROR:                  return "ERROR";
    default:                                             return "UNKNOWN";
    }
}

/**
 * Information about a pending tool call
 */
struct PendingToolCall
{
    std::string    tool_use_id;   // Unique ID from the LLM for this tool call
    std::string    tool_name;     // Name of the tool to execute
    nlohmann::json tool_input;    // Input parameters for the tool
    wxLongLong     start_time;    // When the tool execution started
    bool           is_executing;  // Whether execution has started

    PendingToolCall() : start_time( 0 ), is_executing( false ) {}

    PendingToolCall( const std::string& aId, const std::string& aName,
                     const nlohmann::json& aInput ) :
        tool_use_id( aId ),
        tool_name( aName ),
        tool_input( aInput ),
        start_time( 0 ),
        is_executing( false )
    {
    }
};

/**
 * Context for managing the agent conversation state.
 *
 * This class tracks:
 * - Current conversation state
 * - Pending tool calls
 * - Accumulated LLM response
 * - Error information
 */
class AgentConversationContext
{
public:
    AgentConversationContext();

    /**
     * Reset the context to initial state.
     * Call this when starting a new conversation or after an error.
     */
    void Reset();

    /**
     * Check if the agent can accept new user input.
     * @return true if in IDLE state
     */
    bool CanAcceptUserInput() const;

    /**
     * Check if a tool is currently executing.
     * @return true if in EXECUTING_TOOL state
     */
    bool IsToolExecuting() const;

    /**
     * Check if the agent is busy (not idle).
     * @return true if not in IDLE state
     */
    bool IsBusy() const;

    /**
     * Get the current state.
     */
    AgentConversationState GetState() const { return m_state; }

    /**
     * Set the current state.
     * Use TransitionTo() for validated transitions.
     */
    void SetState( AgentConversationState aState ) { m_state = aState; }

    /**
     * Attempt to transition to a new state.
     * @param aNewState The state to transition to
     * @return true if the transition is valid, false otherwise
     */
    bool TransitionTo( AgentConversationState aNewState );

    /**
     * Add a pending tool call to the queue.
     * @param aToolCall The tool call to add
     */
    void AddPendingToolCall( const PendingToolCall& aToolCall );

    /**
     * Get the next pending tool call (if any).
     * Returns the first tool where is_executing == false.
     * @return Pointer to the next pending tool call, or nullptr if none
     */
    PendingToolCall* GetNextPendingToolCall();

    /**
     * Get the currently executing tool call (if any).
     * Returns the first tool where is_executing == true.
     * @return Pointer to the executing tool call, or nullptr if none
     */
    PendingToolCall* GetExecutingToolCall();

    /**
     * Find a pending tool call by its ID.
     * @param aToolUseId The tool use ID to find
     * @return Pointer to the tool call, or nullptr if not found
     */
    PendingToolCall* FindPendingToolCall( const std::string& aToolUseId );

    /**
     * Remove a pending tool call by its ID.
     * @param aToolUseId The tool use ID to remove
     * @return true if the tool call was found and removed
     */
    bool RemovePendingToolCall( const std::string& aToolUseId );

    /**
     * Clear all pending tool calls.
     */
    void ClearPendingToolCalls();

    /**
     * Get the number of pending tool calls.
     */
    size_t GetPendingToolCallCount() const { return m_pendingTools.size(); }

    /**
     * Check if there are any pending tool calls.
     */
    bool HasPendingToolCalls() const { return !m_pendingTools.empty(); }

    /**
     * Check if a specific tool call is pending by its ID.
     */
    bool HasPendingToolCall( const std::string& aToolUseId ) const
    {
        for( const auto& tool : m_pendingTools )
        {
            if( tool.tool_use_id == aToolUseId )
                return true;
        }
        return false;
    }

    // Accumulated response from the LLM (for streaming)
    std::string accumulated_response;

    // Error information (when in ERROR state)
    std::string error_message;

    // Last tool result (for continuing conversation)
    std::string last_tool_result;
    std::string last_tool_use_id;

    // Rich content block for tool results (text or image)
    struct ToolResultContentBlock
    {
        enum class Type { TEXT, IMAGE };

        Type        type = Type::TEXT;
        std::string text;           // For TEXT type
        std::string media_type;     // For IMAGE type (e.g., "image/png")
        std::string base64_data;    // For IMAGE type (raw base64 string)
    };

    // Collected tool results (for batch adding to history and UI display)
    struct ToolResult
    {
        std::string tool_use_id;
        std::string tool_name;
        std::string tool_description;  // Human-readable description for display
        std::string result;
        bool        success;
        bool        is_python_error;   // Whether the result contains a Python traceback

        // Rich content blocks (text + images). When non-empty, takes precedence
        // over `result` for API serialization.
        std::vector<ToolResultContentBlock> content_blocks;
        bool has_image_content = false;   // Cached flag, set when IMAGE blocks are added

        bool HasImageContent() const { return has_image_content; }

        ToolResult() : success( true ), is_python_error( false ), has_image_content( false ) {}
    };
    std::vector<ToolResult> completed_tool_results;

private:
    AgentConversationState       m_state;
    std::vector<PendingToolCall> m_pendingTools;

    /**
     * Check if a state transition is valid.
     */
    bool IsValidTransition( AgentConversationState aFrom, AgentConversationState aTo ) const;
};

#endif // AGENT_STATE_H
