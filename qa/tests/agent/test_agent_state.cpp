/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * Tests for AgentConversationContext state machine
 */

#include <boost/test/unit_test.hpp>
#include <agent_state.h>

BOOST_AUTO_TEST_SUITE( AgentState )

/**
 * Test that context initializes to IDLE state
 */
BOOST_AUTO_TEST_CASE( ContextInitializesToIdle )
{
    AgentConversationContext ctx;

    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( ctx.CanAcceptUserInput() );
    BOOST_CHECK( !ctx.IsBusy() );
    BOOST_CHECK( !ctx.IsToolExecuting() );
    BOOST_CHECK( !ctx.HasPendingToolCalls() );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 0u );
}

/**
 * Test Reset() clears all state
 */
BOOST_AUTO_TEST_CASE( ResetClearsAllState )
{
    AgentConversationContext ctx;

    // Modify state
    ctx.SetState( AgentConversationState::ERROR );
    ctx.accumulated_response = "test response";
    ctx.error_message = "test error";
    ctx.last_tool_result = "test result";
    ctx.last_tool_use_id = "test_id";

    PendingToolCall tool( "tool_1", "test_tool", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool );

    AgentConversationContext::ToolResult result;
    result.tool_use_id = "tool_1";
    ctx.completed_tool_results.push_back( result );

    // Reset
    ctx.Reset();

    // Verify all state is cleared
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( ctx.accumulated_response.empty() );
    BOOST_CHECK( ctx.error_message.empty() );
    BOOST_CHECK( ctx.last_tool_result.empty() );
    BOOST_CHECK( ctx.last_tool_use_id.empty() );
    BOOST_CHECK( !ctx.HasPendingToolCalls() );
    BOOST_CHECK( ctx.completed_tool_results.empty() );
}

/**
 * Test CanAcceptUserInput() returns true only in IDLE state
 */
BOOST_AUTO_TEST_CASE( CanAcceptUserInputOnlyInIdle )
{
    AgentConversationContext ctx;

    // IDLE - can accept input
    ctx.SetState( AgentConversationState::IDLE );
    BOOST_CHECK( ctx.CanAcceptUserInput() );

    // WAITING_FOR_LLM - cannot accept input
    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );
    BOOST_CHECK( !ctx.CanAcceptUserInput() );

    // TOOL_USE_DETECTED - cannot accept input
    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );
    BOOST_CHECK( !ctx.CanAcceptUserInput() );

    // EXECUTING_TOOL - cannot accept input
    ctx.SetState( AgentConversationState::EXECUTING_TOOL );
    BOOST_CHECK( !ctx.CanAcceptUserInput() );

    // PROCESSING_TOOL_RESULT - cannot accept input
    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );
    BOOST_CHECK( !ctx.CanAcceptUserInput() );

    // ERROR - cannot accept input
    ctx.SetState( AgentConversationState::ERROR );
    BOOST_CHECK( !ctx.CanAcceptUserInput() );
}

/**
 * Test IsToolExecuting() returns true only in EXECUTING_TOOL state
 */
BOOST_AUTO_TEST_CASE( IsToolExecutingOnlyInExecutingTool )
{
    AgentConversationContext ctx;

    ctx.SetState( AgentConversationState::IDLE );
    BOOST_CHECK( !ctx.IsToolExecuting() );

    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );
    BOOST_CHECK( !ctx.IsToolExecuting() );

    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );
    BOOST_CHECK( !ctx.IsToolExecuting() );

    ctx.SetState( AgentConversationState::EXECUTING_TOOL );
    BOOST_CHECK( ctx.IsToolExecuting() );

    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );
    BOOST_CHECK( !ctx.IsToolExecuting() );

    ctx.SetState( AgentConversationState::ERROR );
    BOOST_CHECK( !ctx.IsToolExecuting() );
}

/**
 * Test IsBusy() returns true for all states except IDLE
 */
BOOST_AUTO_TEST_CASE( IsBusyForAllNonIdleStates )
{
    AgentConversationContext ctx;

    ctx.SetState( AgentConversationState::IDLE );
    BOOST_CHECK( !ctx.IsBusy() );

    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );
    BOOST_CHECK( ctx.IsBusy() );

    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );
    BOOST_CHECK( ctx.IsBusy() );

    ctx.SetState( AgentConversationState::EXECUTING_TOOL );
    BOOST_CHECK( ctx.IsBusy() );

    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );
    BOOST_CHECK( ctx.IsBusy() );

    ctx.SetState( AgentConversationState::ERROR );
    BOOST_CHECK( ctx.IsBusy() );
}

/**
 * Test valid state transitions from IDLE
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromIdle )
{
    AgentConversationContext ctx;

    // IDLE -> WAITING_FOR_LLM is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );

    // Reset for next test
    ctx.SetState( AgentConversationState::IDLE );

    // IDLE -> TOOL_USE_DETECTED is invalid
    BOOST_CHECK( !ctx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );

    // IDLE -> ERROR is invalid
    BOOST_CHECK( !ctx.TransitionTo( AgentConversationState::ERROR ) );
}

/**
 * Test valid state transitions from WAITING_FOR_LLM
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromWaitingForLLM )
{
    AgentConversationContext ctx;
    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );

    // WAITING_FOR_LLM -> TOOL_USE_DETECTED is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::TOOL_USE_DETECTED ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::TOOL_USE_DETECTED ) );

    // Reset
    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );

    // WAITING_FOR_LLM -> IDLE is valid (end_turn)
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::IDLE ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );

    // Reset
    ctx.SetState( AgentConversationState::WAITING_FOR_LLM );

    // WAITING_FOR_LLM -> ERROR is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::ERROR ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::ERROR ) );
}

/**
 * Test valid state transitions from TOOL_USE_DETECTED
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromToolUseDetected )
{
    AgentConversationContext ctx;
    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );

    // TOOL_USE_DETECTED -> EXECUTING_TOOL is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::EXECUTING_TOOL ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::EXECUTING_TOOL ) );

    // Reset
    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );

    // TOOL_USE_DETECTED -> ERROR is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::ERROR ) );

    // Reset
    ctx.SetState( AgentConversationState::TOOL_USE_DETECTED );

    // TOOL_USE_DETECTED -> IDLE is invalid
    BOOST_CHECK( !ctx.TransitionTo( AgentConversationState::IDLE ) );
}

/**
 * Test valid state transitions from EXECUTING_TOOL
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromExecutingTool )
{
    AgentConversationContext ctx;
    ctx.SetState( AgentConversationState::EXECUTING_TOOL );

    // EXECUTING_TOOL -> PROCESSING_TOOL_RESULT is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::PROCESSING_TOOL_RESULT ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::PROCESSING_TOOL_RESULT ) );

    // Reset
    ctx.SetState( AgentConversationState::EXECUTING_TOOL );

    // EXECUTING_TOOL -> ERROR is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::ERROR ) );

    // Reset
    ctx.SetState( AgentConversationState::EXECUTING_TOOL );

    // EXECUTING_TOOL -> IDLE is valid (user cancelled)
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::IDLE ) );
}

/**
 * Test valid state transitions from PROCESSING_TOOL_RESULT
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromProcessingToolResult )
{
    AgentConversationContext ctx;
    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );

    // PROCESSING_TOOL_RESULT -> WAITING_FOR_LLM is valid (continue chat)
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );

    // Reset
    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );

    // PROCESSING_TOOL_RESULT -> EXECUTING_TOOL is valid (more tools)
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::EXECUTING_TOOL ) );

    // Reset
    ctx.SetState( AgentConversationState::PROCESSING_TOOL_RESULT );

    // PROCESSING_TOOL_RESULT -> ERROR is valid
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::ERROR ) );
}

/**
 * Test valid state transitions from ERROR
 */
BOOST_AUTO_TEST_CASE( ValidTransitionsFromError )
{
    AgentConversationContext ctx;
    ctx.SetState( AgentConversationState::ERROR );

    // ERROR -> IDLE is valid (reset)
    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::IDLE ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );

    // Reset
    ctx.SetState( AgentConversationState::ERROR );

    // ERROR -> WAITING_FOR_LLM is invalid (must go through IDLE)
    BOOST_CHECK( !ctx.TransitionTo( AgentConversationState::WAITING_FOR_LLM ) );
}

/**
 * Test transitioning to same state returns true
 */
BOOST_AUTO_TEST_CASE( TransitionToSameStateSucceeds )
{
    AgentConversationContext ctx;

    BOOST_CHECK( ctx.TransitionTo( AgentConversationState::IDLE ) );
    BOOST_CHECK_EQUAL( static_cast<int>( ctx.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
}

/**
 * Test adding pending tool calls
 */
BOOST_AUTO_TEST_CASE( AddPendingToolCall )
{
    AgentConversationContext ctx;

    BOOST_CHECK( !ctx.HasPendingToolCalls() );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 0u );

    nlohmann::json input = { { "command", "ls -la" } };
    PendingToolCall tool1( "tool_1", "run_shell", input );
    ctx.AddPendingToolCall( tool1 );

    BOOST_CHECK( ctx.HasPendingToolCalls() );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 1u );

    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool2 );

    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 2u );
}

/**
 * Test GetNextPendingToolCall returns first non-executing tool
 */
BOOST_AUTO_TEST_CASE( GetNextPendingToolCall )
{
    AgentConversationContext ctx;

    // No tools - returns nullptr
    BOOST_CHECK( ctx.GetNextPendingToolCall() == nullptr );

    PendingToolCall tool1( "tool_1", "run_shell", nlohmann::json::object() );
    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool1 );
    ctx.AddPendingToolCall( tool2 );

    // First call returns tool_1
    PendingToolCall* next = ctx.GetNextPendingToolCall();
    BOOST_REQUIRE( next != nullptr );
    BOOST_CHECK_EQUAL( next->tool_use_id, "tool_1" );
    BOOST_CHECK( !next->is_executing );

    // Mark tool_1 as executing
    next->is_executing = true;

    // Now GetNextPendingToolCall returns tool_2
    next = ctx.GetNextPendingToolCall();
    BOOST_REQUIRE( next != nullptr );
    BOOST_CHECK_EQUAL( next->tool_use_id, "tool_2" );
}

/**
 * Test GetExecutingToolCall returns currently executing tool
 */
BOOST_AUTO_TEST_CASE( GetExecutingToolCall )
{
    AgentConversationContext ctx;

    // No tools - returns nullptr
    BOOST_CHECK( ctx.GetExecutingToolCall() == nullptr );

    PendingToolCall tool1( "tool_1", "run_shell", nlohmann::json::object() );
    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool1 );
    ctx.AddPendingToolCall( tool2 );

    // No tool executing yet
    BOOST_CHECK( ctx.GetExecutingToolCall() == nullptr );

    // Mark tool_1 as executing
    PendingToolCall* next = ctx.GetNextPendingToolCall();
    next->is_executing = true;

    // Now GetExecutingToolCall returns tool_1
    PendingToolCall* executing = ctx.GetExecutingToolCall();
    BOOST_REQUIRE( executing != nullptr );
    BOOST_CHECK_EQUAL( executing->tool_use_id, "tool_1" );
}

/**
 * Test FindPendingToolCall finds tool by ID
 */
BOOST_AUTO_TEST_CASE( FindPendingToolCallById )
{
    AgentConversationContext ctx;

    PendingToolCall tool1( "tool_1", "run_shell", nlohmann::json::object() );
    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool1 );
    ctx.AddPendingToolCall( tool2 );

    // Find tool_2
    PendingToolCall* found = ctx.FindPendingToolCall( "tool_2" );
    BOOST_REQUIRE( found != nullptr );
    BOOST_CHECK_EQUAL( found->tool_name, "read_file" );

    // Find non-existent tool
    BOOST_CHECK( ctx.FindPendingToolCall( "tool_999" ) == nullptr );
}

/**
 * Test RemovePendingToolCall removes tool by ID
 */
BOOST_AUTO_TEST_CASE( RemovePendingToolCallById )
{
    AgentConversationContext ctx;

    PendingToolCall tool1( "tool_1", "run_shell", nlohmann::json::object() );
    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool1 );
    ctx.AddPendingToolCall( tool2 );

    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 2u );

    // Remove tool_1
    BOOST_CHECK( ctx.RemovePendingToolCall( "tool_1" ) );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 1u );
    BOOST_CHECK( ctx.FindPendingToolCall( "tool_1" ) == nullptr );

    // Remove non-existent tool returns false
    BOOST_CHECK( !ctx.RemovePendingToolCall( "tool_999" ) );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 1u );

    // Remove tool_2
    BOOST_CHECK( ctx.RemovePendingToolCall( "tool_2" ) );
    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 0u );
    BOOST_CHECK( !ctx.HasPendingToolCalls() );
}

/**
 * Test ClearPendingToolCalls removes all tools
 */
BOOST_AUTO_TEST_CASE( ClearPendingToolCallsRemovesAll )
{
    AgentConversationContext ctx;

    PendingToolCall tool1( "tool_1", "run_shell", nlohmann::json::object() );
    PendingToolCall tool2( "tool_2", "read_file", nlohmann::json::object() );
    PendingToolCall tool3( "tool_3", "write_file", nlohmann::json::object() );
    ctx.AddPendingToolCall( tool1 );
    ctx.AddPendingToolCall( tool2 );
    ctx.AddPendingToolCall( tool3 );

    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 3u );

    ctx.ClearPendingToolCalls();

    BOOST_CHECK_EQUAL( ctx.GetPendingToolCallCount(), 0u );
    BOOST_CHECK( !ctx.HasPendingToolCalls() );
}

/**
 * Test PendingToolCall default constructor
 */
BOOST_AUTO_TEST_CASE( PendingToolCallDefaultConstructor )
{
    PendingToolCall tool;

    BOOST_CHECK( tool.tool_use_id.empty() );
    BOOST_CHECK( tool.tool_name.empty() );
    BOOST_CHECK( tool.tool_input.is_null() );
    BOOST_CHECK_EQUAL( tool.start_time.GetValue(), 0 );
    BOOST_CHECK( !tool.is_executing );
}

/**
 * Test PendingToolCall parameterized constructor
 */
BOOST_AUTO_TEST_CASE( PendingToolCallParameterizedConstructor )
{
    nlohmann::json input = { { "path", "/test/file.txt" } };
    PendingToolCall tool( "my_tool_id", "read_file", input );

    BOOST_CHECK_EQUAL( tool.tool_use_id, "my_tool_id" );
    BOOST_CHECK_EQUAL( tool.tool_name, "read_file" );
    BOOST_CHECK_EQUAL( tool.tool_input["path"], "/test/file.txt" );
    BOOST_CHECK_EQUAL( tool.start_time.GetValue(), 0 );
    BOOST_CHECK( !tool.is_executing );
}

/**
 * Test AgentStateToString returns correct strings
 */
BOOST_AUTO_TEST_CASE( AgentStateToStringReturnsCorrectStrings )
{
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::IDLE ), "IDLE" );
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::WAITING_FOR_LLM ), "WAITING_FOR_LLM" );
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::TOOL_USE_DETECTED ), "TOOL_USE_DETECTED" );
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::EXECUTING_TOOL ), "EXECUTING_TOOL" );
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::PROCESSING_TOOL_RESULT ), "PROCESSING_TOOL_RESULT" );
    BOOST_CHECK_EQUAL( AgentStateToString( AgentConversationState::ERROR ), "ERROR" );
}

BOOST_AUTO_TEST_SUITE_END()
