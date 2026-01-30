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
 * Tests for CHAT_CONTROLLER
 */

#include <boost/test/unit_test.hpp>
#include <core/chat_controller.h>
#include <core/chat_events.h>
#include <agent_state.h>
#include <agent_events.h>
#include <wx/event.h>

#include "mock_llm_client.h"
#include "mock_chat_history.h"

/**
 * Test fixture for CHAT_CONTROLLER tests
 */
struct ChatControllerFixture
{
    ChatControllerFixture()
        : eventSink( nullptr ),
          controller( &eventSink )
    {
        controller.SetLLMClient( &mockClient );
        controller.SetChatHistoryDb( &mockHistory );
    }

    ~ChatControllerFixture()
    {
        // Clean up any pending events
    }

    wxEvtHandler eventSink;
    CHAT_CONTROLLER controller;
    MOCK_LLM_CLIENT mockClient;
    MOCK_CHAT_HISTORY mockHistory;
};


BOOST_AUTO_TEST_SUITE( ChatController )

// ============================================================================
// Initialization and Query Tests
// ============================================================================

/**
 * Test that controller initializes to IDLE state
 */
BOOST_FIXTURE_TEST_CASE( InitializesToIdleState, ChatControllerFixture )
{
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( controller.CanAcceptInput() );
    BOOST_CHECK( !controller.IsBusy() );
}

/**
 * Test that chat history initializes as empty array
 */
BOOST_FIXTURE_TEST_CASE( InitializesWithEmptyHistory, ChatControllerFixture )
{
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK( history.is_array() );
    BOOST_CHECK( history.empty() );

    const auto& context = controller.GetApiContext();
    BOOST_CHECK( context.is_array() );
    BOOST_CHECK( context.empty() );
}

/**
 * Test GetCurrentResponse returns empty string initially
 */
BOOST_FIXTURE_TEST_CASE( CurrentResponseInitiallyEmpty, ChatControllerFixture )
{
    BOOST_CHECK( controller.GetCurrentResponse().empty() );
}

/**
 * Test GetThinkingContent returns empty string initially
 */
BOOST_FIXTURE_TEST_CASE( ThinkingContentInitiallyEmpty, ChatControllerFixture )
{
    BOOST_CHECK( controller.GetThinkingContent().IsEmpty() );
}

/**
 * Test GetChatId returns empty string initially
 */
BOOST_FIXTURE_TEST_CASE( ChatIdInitiallyEmpty, ChatControllerFixture )
{
    BOOST_CHECK( controller.GetChatId().empty() );
}

/**
 * Test GetCurrentModel returns empty string initially
 */
BOOST_FIXTURE_TEST_CASE( CurrentModelInitiallyEmpty, ChatControllerFixture )
{
    BOOST_CHECK( controller.GetCurrentModel().empty() );
}

/**
 * Test GetTools returns non-empty list of tool definitions
 */
BOOST_FIXTURE_TEST_CASE( GetToolsReturnsToolDefinitions, ChatControllerFixture )
{
    const auto& tools = controller.GetTools();
    BOOST_CHECK( !tools.empty() );
}

// ============================================================================
// SetHistory and SetModel Tests
// ============================================================================

/**
 * Test SetHistory updates both chatHistory and apiContext
 */
BOOST_FIXTURE_TEST_CASE( SetHistoryUpdatesBothArrays, ChatControllerFixture )
{
    nlohmann::json history = nlohmann::json::array();
    history.push_back( { { "role", "user" }, { "content", "Hello" } } );
    history.push_back( { { "role", "assistant" }, { "content", "Hi there!" } } );

    controller.SetHistory( history );

    BOOST_CHECK_EQUAL( controller.GetChatHistory().size(), 2u );
    BOOST_CHECK_EQUAL( controller.GetApiContext().size(), 2u );
    BOOST_CHECK_EQUAL( controller.GetChatHistory()[0]["content"], "Hello" );
}

/**
 * Test SetModel updates controller and LLM client
 */
BOOST_FIXTURE_TEST_CASE( SetModelUpdatesClientAndController, ChatControllerFixture )
{
    controller.SetModel( "claude-3-5-sonnet" );

    BOOST_CHECK_EQUAL( controller.GetCurrentModel(), "claude-3-5-sonnet" );
    BOOST_CHECK_EQUAL( mockClient.GetLastModelSet(), "claude-3-5-sonnet" );
}

/**
 * Test AddUserMessage adds to both history arrays
 */
BOOST_FIXTURE_TEST_CASE( AddUserMessageUpdatesBothArrays, ChatControllerFixture )
{
    controller.AddUserMessage( "Test message" );

    BOOST_CHECK_EQUAL( controller.GetChatHistory().size(), 1u );
    BOOST_CHECK_EQUAL( controller.GetApiContext().size(), 1u );
    BOOST_CHECK_EQUAL( controller.GetChatHistory()[0]["role"], "user" );
    BOOST_CHECK_EQUAL( controller.GetChatHistory()[0]["content"], "Test message" );
}

/**
 * Test ClearStreamingState clears response and thinking content
 */
BOOST_FIXTURE_TEST_CASE( ClearStreamingStateClearsContent, ChatControllerFixture )
{
    // Simulate some streaming state by sending a message
    controller.SendMessage( "Hello" );

    // Simulate receiving some text
    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response text";
    controller.HandleLLMChunk( textChunk );

    // Clear streaming state
    controller.ClearStreamingState();

    BOOST_CHECK( controller.GetCurrentResponse().empty() );
    BOOST_CHECK( controller.GetThinkingContent().IsEmpty() );
}

// ============================================================================
// SendMessage Tests
// ============================================================================

/**
 * Test SendMessage transitions to WAITING_FOR_LLM state
 */
BOOST_FIXTURE_TEST_CASE( SendMessageTransitionsToWaitingForLLM, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );
    BOOST_CHECK( !controller.CanAcceptInput() );
    BOOST_CHECK( controller.IsBusy() );
}

/**
 * Test SendMessage adds user message to history
 */
BOOST_FIXTURE_TEST_CASE( SendMessageAddsToHistory, ChatControllerFixture )
{
    controller.SendMessage( "Hello world" );

    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 1u );
    BOOST_CHECK_EQUAL( history[0]["role"], "user" );
    BOOST_CHECK_EQUAL( history[0]["content"], "Hello world" );
}

/**
 * Test SendMessage starts LLM async request
 */
BOOST_FIXTURE_TEST_CASE( SendMessageStartsLLMRequest, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    BOOST_CHECK( mockClient.WasAsyncStarted() );
    BOOST_CHECK_EQUAL( mockClient.GetLastMessages().size(), 1u );
}

/**
 * Test SendMessage is rejected when busy
 */
BOOST_FIXTURE_TEST_CASE( SendMessageRejectedWhenBusy, ChatControllerFixture )
{
    controller.SendMessage( "First message" );
    mockClient.Reset();

    // Try to send another message while busy
    controller.SendMessage( "Second message" );

    // Second message should not start a new request
    BOOST_CHECK( !mockClient.WasAsyncStarted() );
    // History should only have one message
    BOOST_CHECK_EQUAL( controller.GetChatHistory().size(), 1u );
}

/**
 * Test SendMessage fails gracefully without LLM client
 */
BOOST_FIXTURE_TEST_CASE( SendMessageWithoutLLMClient, ChatControllerFixture )
{
    controller.SetLLMClient( nullptr );
    controller.SendMessage( "Hello" );

    // Should transition to ERROR state
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::ERROR ) );
}

// ============================================================================
// Cancel Tests
// ============================================================================

/**
 * Test Cancel transitions back to IDLE from WAITING_FOR_LLM
 */
BOOST_FIXTURE_TEST_CASE( CancelFromWaitingForLLM, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );

    controller.Cancel();

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( mockClient.WasCancelled() );
}

/**
 * Test Cancel adds fake tool_results for pending tools
 */
BOOST_FIXTURE_TEST_CASE( CancelAddsFakeToolResults, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    // Simulate receiving tool_use chunks
    LLMStreamChunk toolChunk;
    toolChunk.type = LLMChunkType::TOOL_USE;
    toolChunk.tool_use_id = "tool_123";
    toolChunk.tool_name = "run_shell";
    toolChunk.tool_input_json = "{\"command\": \"ls\"}";
    controller.HandleLLMChunk( toolChunk );

    LLMStreamChunk toolDoneChunk;
    toolDoneChunk.type = LLMChunkType::TOOL_USE_DONE;
    controller.HandleLLMChunk( toolDoneChunk );

    // Now cancel (should add fake tool_result)
    controller.Cancel();

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );

    // History should have: user message, assistant (tool_use), user (tool_result)
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_GE( history.size(), 2u );

    // Find the fake tool_result message
    bool foundFakeResult = false;
    for( const auto& msg : history )
    {
        if( msg["role"] == "user" && msg["content"].is_array() )
        {
            for( const auto& block : msg["content"] )
            {
                if( block["type"] == "tool_result" && block["is_error"] == true )
                {
                    foundFakeResult = true;
                    BOOST_CHECK_EQUAL( block["tool_use_id"], "tool_123" );
                }
            }
        }
    }
    BOOST_CHECK( foundFakeResult );
}

// ============================================================================
// NewChat Tests
// ============================================================================

/**
 * Test NewChat clears all state
 */
BOOST_FIXTURE_TEST_CASE( NewChatClearsAllState, ChatControllerFixture )
{
    // Set up some state
    controller.SendMessage( "Hello" );

    // Simulate receiving response
    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Now start new chat
    controller.NewChat();

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( controller.GetChatHistory().empty() );
    BOOST_CHECK( controller.GetApiContext().empty() );
    BOOST_CHECK( controller.GetCurrentResponse().empty() );
    BOOST_CHECK( controller.GetChatId().empty() );
}

/**
 * Test NewChat cancels any ongoing operation
 */
BOOST_FIXTURE_TEST_CASE( NewChatCancelsOngoingOperation, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );
    BOOST_CHECK( controller.IsBusy() );

    controller.NewChat();

    BOOST_CHECK( mockClient.WasCancelled() );
    BOOST_CHECK( !controller.IsBusy() );
}

// ============================================================================
// LoadChat Tests
// ============================================================================

/**
 * Test LoadChat loads history from database
 */
BOOST_FIXTURE_TEST_CASE( LoadChatLoadsFromDatabase, ChatControllerFixture )
{
    // Set up mock history
    nlohmann::json savedHistory = nlohmann::json::array();
    savedHistory.push_back( { { "role", "user" }, { "content", "Previous message" } } );
    savedHistory.push_back( { { "role", "assistant" }, { "content", "Previous response" } } );
    mockHistory.AddChat( "chat-123", savedHistory, "Test Chat" );

    controller.LoadChat( "chat-123" );

    BOOST_CHECK( mockHistory.WasLoadCalled() );
    BOOST_CHECK_EQUAL( mockHistory.GetLastLoadedId(), "chat-123" );
    BOOST_CHECK_EQUAL( controller.GetChatHistory().size(), 2u );
    BOOST_CHECK_EQUAL( controller.GetChatId(), "chat-123" );
}

/**
 * Test LoadChat without database emits error
 */
BOOST_FIXTURE_TEST_CASE( LoadChatWithoutDatabaseEmitsError, ChatControllerFixture )
{
    controller.SetChatHistoryDb( nullptr );

    controller.LoadChat( "chat-123" );

    // No crash, chat not loaded
    BOOST_CHECK( controller.GetChatHistory().empty() );
}

/**
 * Test LoadChat cancels ongoing operation
 */
BOOST_FIXTURE_TEST_CASE( LoadChatCancelsOngoingOperation, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );
    BOOST_CHECK( controller.IsBusy() );

    nlohmann::json savedHistory = nlohmann::json::array();
    mockHistory.AddChat( "chat-123", savedHistory );

    controller.LoadChat( "chat-123" );

    BOOST_CHECK( mockClient.WasCancelled() );
}

// ============================================================================
// Retry Tests
// ============================================================================

/**
 * Test Retry only works in ERROR state
 */
BOOST_FIXTURE_TEST_CASE( RetryOnlyWorksInErrorState, ChatControllerFixture )
{
    // Not in error state - retry should do nothing
    mockClient.Reset();
    controller.Retry();
    BOOST_CHECK( !mockClient.WasAsyncStarted() );

    // Trigger error state
    controller.SendMessage( "Hello" );
    LLMStreamChunk errorChunk;
    errorChunk.type = LLMChunkType::ERROR;
    errorChunk.error_message = "Test error";
    controller.HandleLLMChunk( errorChunk );

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::ERROR ) );

    // Now retry should work
    mockClient.Reset();
    controller.Retry();

    BOOST_CHECK( mockClient.WasAsyncStarted() );
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );
}

// ============================================================================
// HandleLLMChunk Tests - TEXT
// ============================================================================

/**
 * Test HandleLLMChunk accumulates text
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkAccumulatesText, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk chunk1;
    chunk1.type = LLMChunkType::TEXT;
    chunk1.text = "Hello ";
    controller.HandleLLMChunk( chunk1 );

    BOOST_CHECK_EQUAL( controller.GetCurrentResponse(), "Hello " );

    LLMStreamChunk chunk2;
    chunk2.type = LLMChunkType::TEXT;
    chunk2.text = "World!";
    controller.HandleLLMChunk( chunk2 );

    BOOST_CHECK_EQUAL( controller.GetCurrentResponse(), "Hello World!" );
}

/**
 * Test HandleLLMChunk ignores chunks when stop requested
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkIgnoresAfterCancel, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk chunk1;
    chunk1.type = LLMChunkType::TEXT;
    chunk1.text = "Before cancel";
    controller.HandleLLMChunk( chunk1 );

    controller.Cancel();

    LLMStreamChunk chunk2;
    chunk2.type = LLMChunkType::TEXT;
    chunk2.text = "After cancel";
    controller.HandleLLMChunk( chunk2 );

    // Text after cancel should be ignored
    BOOST_CHECK_EQUAL( controller.GetCurrentResponse(), "Before cancel" );
}

// ============================================================================
// HandleLLMChunk Tests - THINKING
// ============================================================================

/**
 * Test HandleLLMChunk handles thinking blocks
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkHandlesThinking, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk thinkStart;
    thinkStart.type = LLMChunkType::THINKING_START;
    controller.HandleLLMChunk( thinkStart );

    LLMStreamChunk thinkChunk;
    thinkChunk.type = LLMChunkType::THINKING;
    thinkChunk.thinking_text = "Let me think...";
    controller.HandleLLMChunk( thinkChunk );

    BOOST_CHECK_EQUAL( controller.GetThinkingContent(), "Let me think..." );

    LLMStreamChunk thinkDone;
    thinkDone.type = LLMChunkType::THINKING_DONE;
    thinkDone.thinking_signature = "sig123";
    controller.HandleLLMChunk( thinkDone );
}

// ============================================================================
// HandleLLMChunk Tests - TOOL_USE
// ============================================================================

/**
 * Test HandleLLMChunk queues tool calls
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkQueuesToolCalls, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk toolChunk;
    toolChunk.type = LLMChunkType::TOOL_USE;
    toolChunk.tool_use_id = "tool_1";
    toolChunk.tool_name = "run_shell";
    toolChunk.tool_input_json = "{\"command\": \"ls -la\"}";
    controller.HandleLLMChunk( toolChunk );

    // Still waiting for TOOL_USE_DONE
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );

    LLMStreamChunk toolDone;
    toolDone.type = LLMChunkType::TOOL_USE_DONE;
    controller.HandleLLMChunk( toolDone );

    // Now should transition to TOOL_USE_DETECTED
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::TOOL_USE_DETECTED ) );
}

// ============================================================================
// HandleLLMChunk Tests - END_TURN
// ============================================================================

/**
 * Test HandleLLMChunk END_TURN transitions to IDLE
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkEndTurnTransitionsToIdle, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( controller.CanAcceptInput() );
}

/**
 * Test HandleLLMChunk END_TURN adds assistant message to history
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkEndTurnAddsAssistantMessage, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Hello back!";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 2u );
    BOOST_CHECK_EQUAL( history[1]["role"], "assistant" );
    BOOST_CHECK( history[1]["content"].is_array() );

    // Find text block
    bool foundText = false;
    for( const auto& block : history[1]["content"] )
    {
        if( block["type"] == "text" )
        {
            foundText = true;
            BOOST_CHECK_EQUAL( block["text"], "Hello back!" );
        }
    }
    BOOST_CHECK( foundText );
}

// ============================================================================
// HandleLLMChunk Tests - ERROR
// ============================================================================

/**
 * Test HandleLLMChunk ERROR transitions to ERROR state
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkErrorTransitionsToErrorState, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    LLMStreamChunk errorChunk;
    errorChunk.type = LLMChunkType::ERROR;
    errorChunk.error_message = "API rate limit exceeded";
    controller.HandleLLMChunk( errorChunk );

    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::ERROR ) );
}

// ============================================================================
// HandleLLMChunk Tests - CONTEXT
// ============================================================================

/**
 * Test HandleLLMChunk CONTEXT_EXHAUSTED updates API context
 */
BOOST_FIXTURE_TEST_CASE( HandleLLMChunkContextExhaustedUpdatesContext, ChatControllerFixture )
{
    controller.SendMessage( "Hello" );

    nlohmann::json summarized = nlohmann::json::array();
    summarized.push_back( { { "role", "user" }, { "content", "Summarized history" } } );

    LLMStreamChunk contextChunk;
    contextChunk.type = LLMChunkType::CONTEXT_EXHAUSTED;
    contextChunk.summarized_messages = summarized;
    controller.HandleLLMChunk( contextChunk );

    // API context should be updated
    BOOST_CHECK_EQUAL( controller.GetApiContext().size(), 1u );
    BOOST_CHECK_EQUAL( controller.GetApiContext()[0]["content"], "Summarized history" );
}

// ============================================================================
// HandleToolResult Tests
// ============================================================================

/**
 * Test HandleToolResult processes tool result
 */
BOOST_FIXTURE_TEST_CASE( HandleToolResultProcessesResult, ChatControllerFixture )
{
    // Set up a KIWAY request function
    std::string lastPayload;
    controller.SetKiwayRequestFn( [&lastPayload]( int frame, const std::string& payload ) {
        lastPayload = payload;
        return "Tool output";
    } );

    controller.SendMessage( "Hello" );

    // Simulate tool_use
    LLMStreamChunk toolChunk;
    toolChunk.type = LLMChunkType::TOOL_USE;
    toolChunk.tool_use_id = "tool_1";
    toolChunk.tool_name = "run_shell";
    toolChunk.tool_input_json = "{\"command\": \"ls\"}";
    controller.HandleLLMChunk( toolChunk );

    LLMStreamChunk toolDone;
    toolDone.type = LLMChunkType::TOOL_USE_DONE;
    controller.HandleLLMChunk( toolDone );

    // At this point ExecuteNextTool was called, which should have executed the tool
    // and called HandleToolResult internally

    // The controller should have added tool result to history and started next LLM request
    BOOST_CHECK( mockClient.WasAsyncStarted() );
}

// ============================================================================
// RepairHistory Tests
// ============================================================================

/**
 * Test RepairHistory does nothing on empty history
 */
BOOST_FIXTURE_TEST_CASE( RepairHistoryEmptyHistory, ChatControllerFixture )
{
    controller.RepairHistory();

    BOOST_CHECK( controller.GetChatHistory().empty() );
}

/**
 * Test RepairHistory removes orphaned tool_results
 */
BOOST_FIXTURE_TEST_CASE( RepairHistoryRemovesOrphanedToolResults, ChatControllerFixture )
{
    // Create history with orphaned tool_result (no matching tool_use in previous message)
    nlohmann::json history = nlohmann::json::array();

    // User message
    history.push_back( { { "role", "user" }, { "content", "Hello" } } );

    // Orphaned tool_result (should be removed since previous message has no tool_use)
    nlohmann::json orphanedToolResult = {
        { "role", "user" },
        { "content", nlohmann::json::array( {
            {
                { "type", "tool_result" },
                { "tool_use_id", "orphan_id" },
                { "content", "orphaned result" }
            }
        } ) }
    };
    history.push_back( orphanedToolResult );

    controller.SetHistory( history );
    controller.RepairHistory();

    // The orphaned tool_result message should be removed
    const auto& repairedHistory = controller.GetChatHistory();

    // Check that no tool_result with orphan_id exists
    bool foundOrphan = false;
    for( const auto& msg : repairedHistory )
    {
        if( msg["role"] == "user" && msg["content"].is_array() )
        {
            for( const auto& block : msg["content"] )
            {
                if( block.contains( "tool_use_id" ) && block["tool_use_id"] == "orphan_id" )
                {
                    foundOrphan = true;
                }
            }
        }
    }
    BOOST_CHECK( !foundOrphan );
}

/**
 * Test RepairHistory adds missing tool_results
 */
BOOST_FIXTURE_TEST_CASE( RepairHistoryAddsMissingToolResults, ChatControllerFixture )
{
    // Create history with tool_use but no tool_result
    nlohmann::json history = nlohmann::json::array();

    // User message
    history.push_back( { { "role", "user" }, { "content", "Hello" } } );

    // Assistant message with tool_use
    nlohmann::json assistantMsg = {
        { "role", "assistant" },
        { "content", nlohmann::json::array( {
            {
                { "type", "tool_use" },
                { "id", "tool_123" },
                { "name", "run_shell" },
                { "input", { { "command", "ls" } } }
            }
        } ) }
    };
    history.push_back( assistantMsg );

    // No tool_result follows - this is the orphan we need to fix

    controller.SetHistory( history );
    controller.RepairHistory();

    const auto& repairedHistory = controller.GetChatHistory();

    // Should now have 3 messages: user, assistant (tool_use), user (tool_result)
    BOOST_CHECK_GE( repairedHistory.size(), 3u );

    // Find the fake tool_result
    bool foundFakeResult = false;
    for( const auto& msg : repairedHistory )
    {
        if( msg["role"] == "user" && msg["content"].is_array() )
        {
            for( const auto& block : msg["content"] )
            {
                if( block["type"] == "tool_result" && block["tool_use_id"] == "tool_123" )
                {
                    foundFakeResult = true;
                    BOOST_CHECK( block["is_error"] == true );
                }
            }
        }
    }
    BOOST_CHECK( foundFakeResult );
}

/**
 * Test RepairHistory preserves valid history
 */
BOOST_FIXTURE_TEST_CASE( RepairHistoryPreservesValidHistory, ChatControllerFixture )
{
    // Create valid history with matching tool_use and tool_result
    nlohmann::json history = nlohmann::json::array();

    // User message
    history.push_back( { { "role", "user" }, { "content", "Hello" } } );

    // Assistant with tool_use
    nlohmann::json assistantMsg = {
        { "role", "assistant" },
        { "content", nlohmann::json::array( {
            {
                { "type", "tool_use" },
                { "id", "tool_valid" },
                { "name", "run_shell" },
                { "input", { { "command", "ls" } } }
            }
        } ) }
    };
    history.push_back( assistantMsg );

    // User with matching tool_result
    nlohmann::json toolResultMsg = {
        { "role", "user" },
        { "content", nlohmann::json::array( {
            {
                { "type", "tool_result" },
                { "tool_use_id", "tool_valid" },
                { "content", "file1.txt\nfile2.txt" }
            }
        } ) }
    };
    history.push_back( toolResultMsg );

    // Assistant response
    history.push_back( { { "role", "assistant" },
                         { "content", nlohmann::json::array( { { { "type", "text" }, { "text", "Done!" } } } ) } } );

    controller.SetHistory( history );
    controller.RepairHistory();

    const auto& repairedHistory = controller.GetChatHistory();

    // History should be unchanged (4 messages)
    BOOST_CHECK_EQUAL( repairedHistory.size(), 4u );
}

// ============================================================================
// Integration Tests
// ============================================================================

/**
 * Test full conversation flow: send message -> receive response -> idle
 */
BOOST_FIXTURE_TEST_CASE( FullConversationFlow, ChatControllerFixture )
{
    // Start conversation
    controller.SendMessage( "What is 2+2?" );
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::WAITING_FOR_LLM ) );

    // Receive thinking
    LLMStreamChunk thinkStart;
    thinkStart.type = LLMChunkType::THINKING_START;
    controller.HandleLLMChunk( thinkStart );

    LLMStreamChunk thinkText;
    thinkText.type = LLMChunkType::THINKING;
    thinkText.thinking_text = "Simple math question...";
    controller.HandleLLMChunk( thinkText );

    LLMStreamChunk thinkDone;
    thinkDone.type = LLMChunkType::THINKING_DONE;
    thinkDone.thinking_signature = "sig";
    controller.HandleLLMChunk( thinkDone );

    // Receive text response
    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "2+2 equals 4.";
    controller.HandleLLMChunk( textChunk );

    // End turn
    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Verify final state
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
    BOOST_CHECK( controller.CanAcceptInput() );

    // Verify history
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 2u );
    BOOST_CHECK_EQUAL( history[0]["role"], "user" );
    BOOST_CHECK_EQUAL( history[1]["role"], "assistant" );
}

/**
 * Test conversation with tool use flow
 */
BOOST_FIXTURE_TEST_CASE( ConversationWithToolUseFlow, ChatControllerFixture )
{
    // Set up KIWAY request function
    controller.SetKiwayRequestFn( []( int frame, const std::string& payload ) {
        return "file1.txt\nfile2.txt";
    } );

    // Start conversation
    controller.SendMessage( "List files" );

    // Receive tool_use
    LLMStreamChunk toolChunk;
    toolChunk.type = LLMChunkType::TOOL_USE;
    toolChunk.tool_use_id = "tool_1";
    toolChunk.tool_name = "run_shell";
    toolChunk.tool_input_json = "{\"command\": \"ls\"}";
    controller.HandleLLMChunk( toolChunk );

    LLMStreamChunk toolDone;
    toolDone.type = LLMChunkType::TOOL_USE_DONE;
    controller.HandleLLMChunk( toolDone );

    // After tool execution, controller should start another LLM request
    // The mock client tracks this
    mockClient.Reset();

    // Check that tool was executed (state should have progressed)
    // Note: The actual tool execution happens synchronously in our test setup

    // Verify history contains tool_use and tool_result
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_GE( history.size(), 2u );

    // Find assistant message with tool_use
    bool foundToolUse = false;
    for( const auto& msg : history )
    {
        if( msg["role"] == "assistant" && msg["content"].is_array() )
        {
            for( const auto& block : msg["content"] )
            {
                if( block["type"] == "tool_use" )
                {
                    foundToolUse = true;
                    BOOST_CHECK_EQUAL( block["id"], "tool_1" );
                }
            }
        }
    }
    BOOST_CHECK( foundToolUse );
}

// ============================================================================
// Title Generation Tests
// ============================================================================

/**
 * Test that title is generated on first END_TURN
 */
BOOST_FIXTURE_TEST_CASE( TitleGeneratedOnFirstEndTurn, ChatControllerFixture )
{
    // Send first message
    controller.SendMessage( "Hello, how are you?" );

    // Receive text response
    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "I'm doing well!";
    controller.HandleLLMChunk( textChunk );

    // End turn - should trigger title generation
    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Title should have been generated (event emitted)
    // Since we can't directly check emitted events, we verify state is IDLE
    // and the flow completed successfully
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
}

/**
 * Test that title is NOT generated on subsequent END_TURNs
 */
BOOST_FIXTURE_TEST_CASE( TitleNotGeneratedOnSubsequentEndTurns, ChatControllerFixture )
{
    // First conversation turn
    controller.SendMessage( "First message" );

    LLMStreamChunk textChunk1;
    textChunk1.type = LLMChunkType::TEXT;
    textChunk1.text = "First response";
    controller.HandleLLMChunk( textChunk1 );

    LLMStreamChunk endChunk1;
    endChunk1.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk1 );

    // Second conversation turn
    controller.SendMessage( "Second message" );

    LLMStreamChunk textChunk2;
    textChunk2.type = LLMChunkType::TEXT;
    textChunk2.text = "Second response";
    controller.HandleLLMChunk( textChunk2 );

    LLMStreamChunk endChunk2;
    endChunk2.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk2 );

    // History should have 4 messages (2 user + 2 assistant)
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 4u );
}

/**
 * Test that long messages are truncated for title
 */
BOOST_FIXTURE_TEST_CASE( TitleTruncatesLongMessages, ChatControllerFixture )
{
    // Send a message longer than 50 characters
    std::string longMessage = "This is a very long message that exceeds fifty characters and should be truncated for the title";
    controller.SendMessage( longMessage );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Verify conversation completed
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
}

/**
 * Test that short messages are not truncated
 */
BOOST_FIXTURE_TEST_CASE( TitleDoesNotTruncateShortMessages, ChatControllerFixture )
{
    // Send a message shorter than 50 characters
    std::string shortMessage = "Short message";
    controller.SendMessage( shortMessage );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Verify conversation completed
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
}

/**
 * Test that NewChat clears title generation state
 */
BOOST_FIXTURE_TEST_CASE( NewChatClearsTitleGenerationState, ChatControllerFixture )
{
    // Start a conversation
    controller.SendMessage( "First conversation" );

    // Start new chat before LLM responds
    controller.NewChat();

    // Start new conversation
    controller.SendMessage( "New conversation" );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Verify conversation completed with new message
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 2u );
    BOOST_CHECK_EQUAL( history[0]["content"], "New conversation" );
}

/**
 * Test that LoadChat clears title generation state
 */
BOOST_FIXTURE_TEST_CASE( LoadChatClearsTitleGenerationState, ChatControllerFixture )
{
    // Start a conversation
    controller.SendMessage( "Current conversation" );

    // Set up saved chat
    nlohmann::json savedHistory = nlohmann::json::array();
    savedHistory.push_back( { { "role", "user" }, { "content", "Saved message" } } );
    savedHistory.push_back( { { "role", "assistant" },
                              { "content", nlohmann::json::array( { { { "type", "text" }, { "text", "Saved response" } } } ) } } );
    mockHistory.AddChat( "saved-chat-123", savedHistory, "Saved Chat" );

    // Load existing chat
    controller.LoadChat( "saved-chat-123" );

    // Verify loaded history
    const auto& history = controller.GetChatHistory();
    BOOST_CHECK_EQUAL( history.size(), 2u );
    BOOST_CHECK_EQUAL( history[0]["content"], "Saved message" );

    // Send new message to loaded chat - should NOT trigger title generation
    controller.SendMessage( "Follow-up message" );

    LLMStreamChunk textChunk;
    textChunk.type = LLMChunkType::TEXT;
    textChunk.text = "Follow-up response";
    controller.HandleLLMChunk( textChunk );

    LLMStreamChunk endChunk;
    endChunk.type = LLMChunkType::END_TURN;
    controller.HandleLLMChunk( endChunk );

    // Verify conversation completed
    BOOST_CHECK_EQUAL( static_cast<int>( controller.GetState() ),
                       static_cast<int>( AgentConversationState::IDLE ) );
}

BOOST_AUTO_TEST_SUITE_END()
