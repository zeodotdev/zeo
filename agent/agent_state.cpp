#include "agent_state.h"
#include <wx/log.h>


AgentConversationContext::AgentConversationContext() :
    m_state( AgentConversationState::IDLE )
{
}


void AgentConversationContext::Reset()
{
    m_state = AgentConversationState::IDLE;
    m_pendingTools.clear();
    accumulated_response.clear();
    error_message.clear();
    last_tool_result.clear();
    last_tool_use_id.clear();
    completed_tool_results.clear();
}


bool AgentConversationContext::CanAcceptUserInput() const
{
    return m_state == AgentConversationState::IDLE;
}


bool AgentConversationContext::IsToolExecuting() const
{
    return m_state == AgentConversationState::EXECUTING_TOOL;
}


bool AgentConversationContext::IsBusy() const
{
    return m_state != AgentConversationState::IDLE;
}


bool AgentConversationContext::IsValidTransition( AgentConversationState aFrom,
                                                   AgentConversationState aTo ) const
{
    // Define valid state transitions
    switch( aFrom )
    {
    case AgentConversationState::IDLE:
        // From IDLE, can only go to WAITING_FOR_LLM (user sent message)
        return aTo == AgentConversationState::WAITING_FOR_LLM;

    case AgentConversationState::WAITING_FOR_LLM:
        // From WAITING_FOR_LLM, can go to:
        // - TOOL_USE_DETECTED (LLM wants to use a tool)
        // - IDLE (LLM finished with end_turn)
        // - ERROR (something went wrong)
        return aTo == AgentConversationState::TOOL_USE_DETECTED ||
               aTo == AgentConversationState::IDLE ||
               aTo == AgentConversationState::ERROR;

    case AgentConversationState::TOOL_USE_DETECTED:
        // From TOOL_USE_DETECTED, can go to:
        // - EXECUTING_TOOL (started executing)
        // - ERROR (couldn't start execution)
        return aTo == AgentConversationState::EXECUTING_TOOL ||
               aTo == AgentConversationState::ERROR;

    case AgentConversationState::EXECUTING_TOOL:
        // From EXECUTING_TOOL, can go to:
        // - PROCESSING_TOOL_RESULT (tool finished)
        // - ERROR (tool failed or timed out)
        // - IDLE (user cancelled)
        return aTo == AgentConversationState::PROCESSING_TOOL_RESULT ||
               aTo == AgentConversationState::ERROR ||
               aTo == AgentConversationState::IDLE;

    case AgentConversationState::PROCESSING_TOOL_RESULT:
        // From PROCESSING_TOOL_RESULT, can go to:
        // - WAITING_FOR_LLM (continue conversation with result)
        // - EXECUTING_TOOL (more tools to execute)
        // - ERROR (processing failed)
        return aTo == AgentConversationState::WAITING_FOR_LLM ||
               aTo == AgentConversationState::EXECUTING_TOOL ||
               aTo == AgentConversationState::ERROR;

    case AgentConversationState::ERROR:
        // From ERROR, can only go to IDLE (reset)
        return aTo == AgentConversationState::IDLE;

    default:
        return false;
    }
}


bool AgentConversationContext::TransitionTo( AgentConversationState aNewState )
{
    if( aNewState == m_state )
    {
        // Already in this state
        return true;
    }

    if( !IsValidTransition( m_state, aNewState ) )
    {
        wxLogDebug( "AGENT STATE: Invalid transition from %s to %s",
                    AgentStateToString( m_state ),
                    AgentStateToString( aNewState ) );
        return false;
    }

    wxLogDebug( "AGENT STATE: Transitioning from %s to %s",
                AgentStateToString( m_state ),
                AgentStateToString( aNewState ) );

    m_state = aNewState;
    return true;
}


void AgentConversationContext::AddPendingToolCall( const PendingToolCall& aToolCall )
{
    m_pendingTools.push_back( aToolCall );
    wxLogDebug( "AGENT STATE: Added pending tool call '%s' (total: %zu)",
                aToolCall.tool_name.c_str(), m_pendingTools.size() );
}


PendingToolCall* AgentConversationContext::GetNextPendingToolCall()
{
    for( auto& tool : m_pendingTools )
    {
        if( !tool.is_executing )
        {
            return &tool;
        }
    }
    return nullptr;
}


PendingToolCall* AgentConversationContext::GetExecutingToolCall()
{
    for( auto& tool : m_pendingTools )
    {
        if( tool.is_executing )
        {
            return &tool;
        }
    }
    return nullptr;
}


PendingToolCall* AgentConversationContext::FindPendingToolCall( const std::string& aToolUseId )
{
    for( auto& tool : m_pendingTools )
    {
        if( tool.tool_use_id == aToolUseId )
        {
            return &tool;
        }
    }
    return nullptr;
}


bool AgentConversationContext::RemovePendingToolCall( const std::string& aToolUseId )
{
    for( auto it = m_pendingTools.begin(); it != m_pendingTools.end(); ++it )
    {
        if( it->tool_use_id == aToolUseId )
        {
            wxLogDebug( "AGENT STATE: Removed pending tool call '%s'", it->tool_name.c_str() );
            m_pendingTools.erase( it );
            return true;
        }
    }
    return false;
}


void AgentConversationContext::ClearPendingToolCalls()
{
    m_pendingTools.clear();
    wxLogDebug( "AGENT STATE: Cleared all pending tool calls" );
}
