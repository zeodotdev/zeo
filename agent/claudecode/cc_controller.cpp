#include "cc_controller.h"
#include "cc_subprocess.h"
#include "cc_events.h"
#include "../core/chat_events.h"

#include "../tools/tool_registry.h"
#include <zeo/agent_auth.h>
#include <kicad_curl/kicad_curl_easy.h>

#include <wx/log.h>
#include <wx/base64.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <thread>

using json = nlohmann::json;


CC_CONTROLLER::CC_CONTROLLER( wxEvtHandler* aEventSink ) :
    m_eventSink( aEventSink )
{
    // Bind raw subprocess events to our handlers
    Bind( EVT_CC_LINE,  &CC_CONTROLLER::OnCCLine,  this );
    Bind( EVT_CC_EXIT,  &CC_CONTROLLER::OnCCExit,  this );
    Bind( EVT_CC_ERROR, &CC_CONTROLLER::OnCCError, this );
}


CC_CONTROLLER::~CC_CONTROLLER()
{
    Cancel();
}


void CC_CONTROLLER::Start( const std::string& aWorkingDir, const std::string& aPromptsDir,
                           const std::string& aApiSocketPath, const std::string& aPythonPath )
{
    m_workingDir = aWorkingDir;
    m_promptsDir = aPromptsDir;
    m_apiSocketPath = aApiSocketPath;
    m_pythonPath = aPythonPath;

    // Generate MCP config for Zeo tools
    m_mcpConfigPath = GenerateMcpConfig();

    // Load system prompt (core.md + addendum)
    std::string systemPrompt = LoadSystemPrompt();

    m_subprocess = std::make_unique<CC_SUBPROCESS>( this );

    if( !m_subprocess->Start( aWorkingDir, m_mcpConfigPath, "claude-opus-4-6", "", systemPrompt ) )
    {
        wxLogError( "CC_CONTROLLER: Failed to start Claude Code subprocess" );
        ChatErrorData errData( "Claude Code is not installed. Install it from "
                               "https://docs.anthropic.com/en/docs/claude-code "
                               "and try again." );
        PostChatEvent( m_eventSink, EVT_CHAT_ERROR, errData );
        m_subprocess.reset();
        return;
    }

    ResetTurnState();
    m_sessionId.clear();
    m_toolResultCounter = 0;
    m_ccTurnCount = 0;

    wxLogInfo( "CC_CONTROLLER: Started in %s (MCP config: %s, prompt: %zu bytes)",
               aWorkingDir.c_str(), m_mcpConfigPath.c_str(), systemPrompt.size() );
}


void CC_CONTROLLER::SendMessage( const std::string& aText )
{
    if( !m_subprocess || !m_subprocess->IsRunning() )
    {
        wxLogWarning( "CC_CONTROLLER: Cannot send message, subprocess not running" );
        return;
    }

    m_busy = true;
    ResetTurnState();

    // Record user message in history (always the raw text)
    m_chatHistory.push_back( { { "role", "user" }, { "content", aText } } );

    // If there are prior messages the CC subprocess hasn't seen (from a different
    // backend or a loaded conversation), prepend them as context so CC is aware
    // of the conversation history.
    std::string messageToSend = aText;

    if( m_ccTurnCount == 0 && m_chatHistory.size() > 1 )
    {
        std::string context = "<prior_conversation>\n";

        // All messages except the one we just appended
        for( size_t i = 0; i < m_chatHistory.size() - 1; i++ )
        {
            const auto& msg = m_chatHistory[i];
            std::string role = msg.value( "role", "" );

            context += "[" + role + "]: ";

            if( msg["content"].is_string() )
            {
                context += msg["content"].get<std::string>();
            }
            else if( msg["content"].is_array() )
            {
                for( const auto& block : msg["content"] )
                {
                    std::string type = block.value( "type", "" );

                    if( type == "text" )
                        context += block.value( "text", "" );
                    else if( type == "tool_use" )
                        context += "[tool_use: " + block.value( "name", "" ) + "]";
                    else if( type == "tool_result" )
                    {
                        std::string content = block.value( "content", "" );
                        if( content.size() > 500 )
                            content = content.substr( 0, 500 ) + "...";
                        context += "[tool_result: " + content + "]";
                    }
                }
            }

            context += "\n";
        }

        context += "</prior_conversation>\n\n";
        messageToSend = context + aText;

        wxLogInfo( "CC_CONTROLLER: Prepended %zu prior messages as context (%zu bytes)",
                   m_chatHistory.size() - 1, context.size() );
    }

    m_ccTurnCount++;
    m_subprocess->SendUserMessage( messageToSend );

    wxLogInfo( "CC_CONTROLLER: Sent user message (%zu chars)", aText.size() );
}


void CC_CONTROLLER::Cancel()
{
    if( m_subprocess )
    {
        m_intentionalStop = true;
        m_subprocess->Stop();
        m_subprocess.reset();
    }

    m_busy = false;
}


void CC_CONTROLLER::NewSession()
{
    Cancel();

    std::string systemPrompt = LoadSystemPrompt();

    m_subprocess = std::make_unique<CC_SUBPROCESS>( this );

    if( !m_subprocess->Start( m_workingDir, m_mcpConfigPath, "claude-opus-4-6", "", systemPrompt ) )
    {
        wxLogError( "CC_CONTROLLER: Failed to start Claude Code subprocess (new session)" );
        ChatErrorData errData( "Claude Code is not installed. Install it from "
                               "https://docs.anthropic.com/en/docs/claude-code "
                               "and try again." );
        PostChatEvent( m_eventSink, EVT_CHAT_ERROR, errData );
        m_subprocess.reset();
        return;
    }

    ResetTurnState();
    m_sessionId.clear();
    m_toolResultCounter = 0;
    m_thinkingIndex = 0;
    m_ccTurnCount = 0;
    m_chatHistory = json::array();

    wxLogInfo( "CC_CONTROLLER: New session started" );
}


void CC_CONTROLLER::ResumeSession( const std::string& aSessionId )
{
    Cancel();

    std::string systemPrompt = LoadSystemPrompt();

    m_subprocess = std::make_unique<CC_SUBPROCESS>( this );

    if( !m_subprocess->Start( m_workingDir, m_mcpConfigPath, "claude-opus-4-6", aSessionId, systemPrompt ) )
    {
        wxLogError( "CC_CONTROLLER: Failed to start Claude Code subprocess (resume)" );
        ChatErrorData errData( "Claude Code is not installed. Install it from "
                               "https://docs.anthropic.com/en/docs/claude-code "
                               "and try again." );
        PostChatEvent( m_eventSink, EVT_CHAT_ERROR, errData );
        m_subprocess.reset();
        return;
    }

    ResetTurnState();
    m_sessionId = aSessionId;

    wxLogInfo( "CC_CONTROLLER: Resuming session %s", aSessionId.c_str() );
}


bool CC_CONTROLLER::IsRunning() const
{
    return m_subprocess && m_subprocess->IsRunning();
}


void CC_CONTROLLER::ResetTurnState()
{
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_inThinking = false;
    m_activeBlocks.clear();
    m_pendingToolIds.clear();
    m_pendingToolNames.clear();
    m_lastStderrLine.clear();
    m_currentAssistantContent = json::array();
}


void CC_CONTROLLER::CommitAssistantMessage()
{
    if( m_currentAssistantContent.empty() )
        return;

    m_chatHistory.push_back( { { "role", "assistant" }, { "content", m_currentAssistantContent } } );
    m_currentAssistantContent = json::array();
}


// ═══════════════════════════════════════════════════════════════════════════
// Raw event handlers
// ═══════════════════════════════════════════════════════════════════════════

void CC_CONTROLLER::OnCCLine( wxThreadEvent& aEvent )
{
    std::string line = aEvent.GetString().ToStdString();
    ParseLine( line );
}


void CC_CONTROLLER::OnCCExit( wxThreadEvent& aEvent )
{
    int exitCode = aEvent.GetInt();
    wxLogInfo( "CC_CONTROLLER: Process exited with code %d (intentional=%d)",
               exitCode, m_intentionalStop );

    m_busy = false;

    // Don't show error for intentional stops (NewSession, Cancel, model switch)
    if( m_intentionalStop )
    {
        m_intentionalStop = false;
        return;
    }

    if( exitCode != 0 )
    {
        std::string errorMsg;

        if( exitCode == 127 )
        {
            errorMsg = "Claude Code is not installed. Install it from "
                       "https://docs.anthropic.com/en/docs/claude-code "
                       "and try again.";
        }
        else if( !m_lastStderrLine.empty() )
        {
            // Use the last stderr line for context (often contains auth or config errors)
            errorMsg = "Claude Code error: " + m_lastStderrLine;
        }
        else
        {
            errorMsg = "Claude Code process exited unexpectedly (code "
                       + std::to_string( exitCode ) + ")";
        }

        ChatErrorData errData( errorMsg, exitCode != 127 );
        PostChatEvent( m_eventSink, EVT_CHAT_ERROR, errData );
    }
    else
    {
        // Normal exit — emit turn complete
        ChatTurnCompleteData data( false, false );
        PostChatEvent( m_eventSink, EVT_CHAT_TURN_COMPLETE, data );
    }
}


void CC_CONTROLLER::OnCCError( wxThreadEvent& aEvent )
{
    std::string line = aEvent.GetString().ToStdString();
    wxLogInfo( "CC_CONTROLLER stderr: %s", line.c_str() );

    // Capture the last stderr line for use in exit error messages
    if( !line.empty() )
        m_lastStderrLine = line;
}


// ═══════════════════════════════════════════════════════════════════════════
// NDJSON parsing
// ═══════════════════════════════════════════════════════════════════════════

void CC_CONTROLLER::ParseLine( const std::string& aLine )
{
    if( aLine.empty() )
        return;

    json parsed;

    try
    {
        parsed = json::parse( aLine );
    }
    catch( const json::parse_error& e )
    {
        wxLogWarning( "CC_CONTROLLER: Failed to parse NDJSON: %s", e.what() );
        return;
    }

    std::string type = parsed.value( "type", "" );

    if( type == "stream_event" )
    {
        HandleStreamEvent( parsed );
    }
    else if( type == "assistant" )
    {
        HandleAssistantMessage( parsed );
    }
    else if( type == "result" )
    {
        HandleResultMessage( parsed );
    }
    else if( type == "system" )
    {
        // System init message — extract session_id
        if( parsed.contains( "session_id" ) )
            m_sessionId = parsed["session_id"].get<std::string>();

        wxLogInfo( "CC_CONTROLLER: System message, session=%s", m_sessionId.c_str() );
    }
    else if( type == "user" )
    {
        // User messages contain tool_result blocks after CC executes tools.
        // Extract results and match them to pending tool IDs.
        HandleUserMessage( parsed );
    }
    else
    {
        wxLogInfo( "CC_CONTROLLER: Unknown message type: %s", type.c_str() );
    }
}


void CC_CONTROLLER::HandleStreamEvent( const json& aMsg )
{
    if( !aMsg.contains( "event" ) )
        return;

    const json& event = aMsg["event"];
    std::string eventType = event.value( "type", "" );

    if( eventType == "content_block_start" )
        HandleContentBlockStart( event );
    else if( eventType == "content_block_delta" )
        HandleContentBlockDelta( event );
    else if( eventType == "content_block_stop" )
        HandleContentBlockStop( event );
    else if( eventType == "message_start" )
    {
        // New assistant message — complete any tools still pending (fallback if
        // tool_result wasn't captured, e.g. for hidden tools or edge cases)
        for( const auto& toolId : m_pendingToolIds )
        {
            ChatToolCompleteData data( toolId, "", "", true );
            PostChatEvent( m_eventSink, EVT_CHAT_TOOL_COMPLETE, data );
        }
        m_pendingToolIds.clear();
        m_pendingToolNames.clear();
    }
    else if( eventType == "message_stop" )
    {
        // History recording handled by HandleAssistantMessage (complete message)
    }
}


void CC_CONTROLLER::HandleContentBlockStart( const json& aEvent )
{
    int index = aEvent.value( "index", -1 );
    if( index < 0 )
        return;

    const json& block = aEvent.value( "content_block", json::object() );
    std::string blockType = block.value( "type", "" );

    ContentBlock cb;
    cb.index = index;

    if( blockType == "thinking" )
    {
        cb.type = BlockType::THINKING;
        m_inThinking = true;
        m_thinkingContent.Clear();
        m_thinkingIndex++;

        ChatThinkingStartData data( m_thinkingIndex );
        PostChatEvent( m_eventSink, EVT_CHAT_THINKING_START, data );
    }
    else if( blockType == "text" )
    {
        cb.type = BlockType::TEXT;
    }
    else if( blockType == "tool_use" )
    {
        cb.type = BlockType::TOOL_USE;
        cb.toolId = block.value( "id", "" );
        cb.toolName = block.value( "name", "" );

        // Hide internal CC tools from the UI (same as CC's own GUI behavior)
        static const std::set<std::string> hiddenTools = {
            "ToolSearch", "TodoWrite", "EnterPlanMode", "ExitPlanMode",
            "EnterWorktree", "AskUserQuestion", "Skill", "SendMessage",
            "TaskCreate", "TaskGet", "TaskList", "TaskOutput", "TaskStop",
            "TaskUpdate", "TeamCreate", "TeamDelete", "LSP"
        };

        if( hiddenTools.count( cb.toolName ) == 0 )
        {
            // Strip MCP prefixes for cleaner display (mcp__zeo__check_status → check_status)
            std::string displayName = cb.toolName;
            const std::string mcpPrefix = "mcp__zeo__";

            if( displayName.substr( 0, mcpPrefix.size() ) == mcpPrefix )
                displayName = displayName.substr( mcpPrefix.size() );

            cb.displayName = displayName;

            ChatToolGeneratingData genData( cb.toolId, displayName );
            PostChatEvent( m_eventSink, EVT_CHAT_TOOL_GENERATING, genData );
        }
    }

    m_activeBlocks[index] = cb;
}


void CC_CONTROLLER::HandleContentBlockDelta( const json& aEvent )
{
    int index = aEvent.value( "index", -1 );
    auto it = m_activeBlocks.find( index );

    if( it == m_activeBlocks.end() )
        return;

    ContentBlock& block = it->second;
    const json& delta = aEvent.value( "delta", json::object() );
    std::string deltaType = delta.value( "type", "" );

    wxLogInfo( "CC_CONTROLLER::HandleContentBlockDelta - index=%d, type=%s, blockType=%d",
               index, deltaType.c_str(), static_cast<int>( block.type ) );

    if( block.type == BlockType::TEXT && deltaType == "text_delta" )
    {
        std::string text = delta.value( "text", "" );
        m_currentResponse += text;

        ChatTextDeltaData data( m_currentResponse, text );
        PostChatEvent( m_eventSink, EVT_CHAT_TEXT_DELTA, data );
    }
    else if( block.type == BlockType::THINKING && deltaType == "thinking_delta" )
    {
        std::string thinking = delta.value( "thinking", "" );
        m_thinkingContent += wxString::FromUTF8( thinking );

        ChatThinkingDeltaData data( m_thinkingContent, wxString::FromUTF8( thinking ) );
        PostChatEvent( m_eventSink, EVT_CHAT_THINKING_DELTA, data );
    }
    else if( block.type == BlockType::TOOL_USE && deltaType == "input_json_delta" )
    {
        block.toolInput += delta.value( "partial_json", "" );
    }
}


void CC_CONTROLLER::HandleContentBlockStop( const json& aEvent )
{
    int index = aEvent.value( "index", -1 );
    auto it = m_activeBlocks.find( index );

    wxLogInfo( "CC_CONTROLLER::HandleContentBlockStop - index=%d, found=%d", index, it != m_activeBlocks.end() );

    if( it == m_activeBlocks.end() )
        return;

    ContentBlock& block = it->second;

    if( block.type == BlockType::TEXT )
    {
        // History recording handled by HandleAssistantMessage (complete message)
    }
    else if( block.type == BlockType::THINKING )
    {
        m_inThinking = false;

        ChatThinkingDoneData data( m_thinkingContent );
        PostChatEvent( m_eventSink, EVT_CHAT_THINKING_DONE, data );
    }
    else if( block.type == BlockType::TOOL_USE )
    {
        // Tool call is complete — Claude Code will now execute it internally
        // Parse tool input for display (default to empty object so .value() calls work)
        json toolInput = json::object();
        try
        {
            if( !block.toolInput.empty() )
                toolInput = json::parse( block.toolInput );
        }
        catch( ... )
        {
            // If parse fails, keep empty object (not a raw string)
        }

        // Use display name (with MCP prefix stripped) for description
        std::string name = block.displayName.empty() ? block.toolName : block.displayName;
        std::string desc;

        wxLogInfo( "CC_CONTROLLER::HandleContentBlockStop - tool_use: name=%s, inputLen=%zu",
                   name.c_str(), block.toolInput.size() );

        // For MCP tools (mcp__zeo__*), use TOOL_REGISTRY to get the same
        // human-readable descriptions as the Zeo agent (e.g. "Running ERC check",
        // "Adding NE555P", "Getting schematic summary").
        const std::string mcpPrefix2 = "mcp__zeo__";
        if( block.toolName.substr( 0, mcpPrefix2.size() ) == mcpPrefix2 )
        {
            desc = TOOL_REGISTRY::Instance().GetDescription( name, toolInput );
        }

        // For non-MCP tools (Read, Edit, Bash, Grep, etc.), derive from input
        if( desc.empty() || desc == "Executing " + name )
        {
            desc = name;

            if( toolInput.is_object() )
            {
                if( toolInput.contains( "command" ) )
                    desc = toolInput["command"].get<std::string>();
                else if( toolInput.contains( "file_path" ) )
                    desc = name + ": " + toolInput["file_path"].get<std::string>();
                else if( toolInput.contains( "pattern" ) )
                    desc = name + ": " + toolInput["pattern"].get<std::string>();
                else if( toolInput.contains( "query" ) )
                    desc = name + ": " + toolInput["query"].get<std::string>();
            }
        }

        // Skip hidden internal CC tools
        static const std::set<std::string> hiddenTools = {
            "ToolSearch", "TodoWrite", "EnterPlanMode", "ExitPlanMode",
            "EnterWorktree", "AskUserQuestion", "Skill", "SendMessage",
            "TaskCreate", "TaskGet", "TaskList", "TaskOutput", "TaskStop",
            "TaskUpdate", "TeamCreate", "TeamDelete", "LSP"
        };

        // History recording handled by HandleAssistantMessage (complete message)

        if( hiddenTools.count( block.toolName ) == 0 )
        {
            m_toolResultCounter++;

            wxLogInfo( "CC_CONTROLLER::HandleContentBlockStop - posting EVT_CHAT_TOOL_START for %s (id=%s)",
                       name.c_str(), block.toolId.c_str() );

            ChatToolStartData startData( block.toolId, name, desc, toolInput );
            PostChatEvent( m_eventSink, EVT_CHAT_TOOL_START, startData );

            // Track this tool as pending (result comes in user message with tool_result)
            m_pendingToolIds.push_back( block.toolId );
            m_pendingToolNames[block.toolId] = name;
        }
    }

    m_activeBlocks.erase( it );
}


void CC_CONTROLLER::HandleAssistantMessage( const json& aMsg )
{
    // Complete assistant message — this contains the full content blocks.
    // Stream events (content_block_stop, message_stop) may not be emitted by
    // Claude Code's stream-json format, so we build history from this message.

    if( !aMsg.contains( "message" ) )
    {
        wxLogInfo( "CC_CONTROLLER: Assistant message with no 'message' field" );
        return;
    }

    const json& message = aMsg["message"];

    if( !message.contains( "content" ) || !message["content"].is_array() )
    {
        wxLogInfo( "CC_CONTROLLER: Assistant message with no content array" );
        return;
    }

    // Build history content from the complete message
    json historyContent = json::array();

    for( const auto& block : message["content"] )
    {
        std::string blockType = block.value( "type", "" );

        if( blockType == "text" )
        {
            historyContent.push_back( { { "type", "text" }, { "text", block.value( "text", "" ) } } );
        }
        else if( blockType == "tool_use" )
        {
            json toolBlock = {
                { "type", "tool_use" },
                { "id", block.value( "id", "" ) },
                { "name", block.value( "name", "" ) },
                { "input", block.value( "input", json::object() ) }
            };
            historyContent.push_back( toolBlock );
        }
        // Skip thinking/signature blocks — not needed for history
    }

    if( !historyContent.empty() )
    {
        m_chatHistory.push_back( { { "role", "assistant" }, { "content", historyContent } } );
        wxLogInfo( "CC_CONTROLLER: Recorded assistant message with %zu content blocks in history",
                   historyContent.size() );
    }

    // Clear streaming accumulation state since we got the complete message
    m_currentAssistantContent = json::array();
}


void CC_CONTROLLER::HandleUserMessage( const json& aMsg )
{
    // User messages in CC stream contain tool_result blocks after tool execution.
    // Format: { "type": "user", "message": { "content": [ { "type": "tool_result",
    //           "tool_use_id": "...", "content": "..." } ] } }

    if( !aMsg.contains( "message" ) )
        return;

    const json& message = aMsg["message"];

    if( !message.contains( "content" ) || !message["content"].is_array() )
        return;

    // Record tool_result user message in history
    {
        json toolResultContent = json::array();

        for( const auto& block : message["content"] )
        {
            if( block.value( "type", "" ) == "tool_result" )
            {
                json histBlock = {
                    { "type", "tool_result" },
                    { "tool_use_id", block.value( "tool_use_id", "" ) }
                };

                // Store content as string (truncated for history size)
                if( block.contains( "content" ) )
                {
                    if( block["content"].is_string() )
                    {
                        std::string content = block["content"].get<std::string>();
                        if( content.size() > 4000 )
                            content = content.substr( 0, 4000 ) + "\n... (truncated)";
                        histBlock["content"] = content;
                    }
                    else
                    {
                        // Array content — store text parts only (skip images for size)
                        std::string content;
                        for( const auto& part : block["content"] )
                        {
                            if( part.value( "type", "" ) == "text" )
                            {
                                if( !content.empty() )
                                    content += "\n";
                                content += part.value( "text", "" );
                            }
                        }
                        if( content.size() > 4000 )
                            content = content.substr( 0, 4000 ) + "\n... (truncated)";
                        histBlock["content"] = content;
                    }
                }

                if( block.value( "is_error", false ) )
                    histBlock["is_error"] = true;

                toolResultContent.push_back( histBlock );
            }
        }

        if( !toolResultContent.empty() )
            m_chatHistory.push_back( { { "role", "user" }, { "content", toolResultContent } } );
    }

    for( const auto& block : message["content"] )
    {
        if( block.value( "type", "" ) != "tool_result" )
            continue;

        std::string toolUseId = block.value( "tool_use_id", "" );
        if( toolUseId.empty() )
            continue;

        // Extract the result text and any image data
        std::string resultText;
        std::string imageBase64;
        std::string imageMimeType;

        if( block.contains( "content" ) )
        {
            if( block["content"].is_string() )
            {
                resultText = block["content"].get<std::string>();
            }
            else if( block["content"].is_array() )
            {
                // Content can be an array of text and image blocks
                for( const auto& part : block["content"] )
                {
                    std::string partType = part.value( "type", "" );

                    if( partType == "text" )
                    {
                        if( !resultText.empty() )
                            resultText += "\n";
                        resultText += part.value( "text", "" );
                    }
                    else if( partType == "image" )
                    {
                        // Image block: { "type": "image", "source": { "type": "base64",
                        //   "media_type": "image/png", "data": "<base64>" } }
                        if( part.contains( "source" ) && part["source"].is_object() )
                        {
                            imageBase64 = part["source"].value( "data", "" );
                            imageMimeType = part["source"].value( "media_type", "image/png" );
                        }
                    }
                }
            }
        }

        bool isError = block.value( "is_error", false );

        // Truncate very long results for display
        std::string displayResult = resultText;
        if( displayResult.size() > 2000 )
            displayResult = displayResult.substr( 0, 2000 ) + "\n... (truncated)";

        // Look up the tool name from our pending map
        std::string toolName;
        auto nameIt = m_pendingToolNames.find( toolUseId );
        if( nameIt != m_pendingToolNames.end() )
            toolName = nameIt->second;

        // Match to pending tool and emit completion
        auto it = std::find( m_pendingToolIds.begin(), m_pendingToolIds.end(), toolUseId );
        if( it != m_pendingToolIds.end() )
        {
            ChatToolCompleteData data( toolUseId, toolName, displayResult, !isError );

            if( !imageBase64.empty() )
            {
                data.hasImage = true;
                data.imageBase64 = imageBase64;
                data.imageMediaType = imageMimeType;
            }

            PostChatEvent( m_eventSink, EVT_CHAT_TOOL_COMPLETE, data );

            m_pendingToolIds.erase( it );
            m_pendingToolNames.erase( toolUseId );
        }

        wxLogInfo( "CC_CONTROLLER: Tool result for %s (%zu bytes, error=%d, hasImage=%d)",
                   toolUseId.c_str(), resultText.size(), isError, !imageBase64.empty() );
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// MCP config and system prompt helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string CC_CONTROLLER::GenerateMcpConfig()
{
    if( m_apiSocketPath.empty() )
    {
        wxLogInfo( "CC_CONTROLLER: Skipping MCP config (no API socket)" );
        return "";
    }

    if( m_pythonPath.empty() )
    {
        wxLogWarning( "CC_CONTROLLER: Skipping MCP config (no Python3 found)" );
        return "";
    }

    // Build MCP config JSON pointing to kipy.mcp as an stdio server.
    // Uses system Python (not bundled 3.9) because the mcp SDK requires >=3.10.
    // kipy is installed as editable (`pip install -e`) so it uses the source tree.
    json config;
    config["mcpServers"]["zeo"]["command"] = m_pythonPath;
    config["mcpServers"]["zeo"]["args"] = json::array( { "-m", "kipy.mcp" } );
    config["mcpServers"]["zeo"]["env"]["KICAD_API_SOCKET"] = m_apiSocketPath;

    // Pass Supabase credentials for tool usage tracking in MCP server
    const auto& supabaseUrl = TOOL_REGISTRY::Instance().GetSupabaseUrl();
    const auto& supabaseKey = TOOL_REGISTRY::Instance().GetSupabaseAnonKey();

    if( !supabaseUrl.empty() && !supabaseKey.empty() )
    {
        config["mcpServers"]["zeo"]["env"]["ZEO_SUPABASE_URL"] = supabaseUrl;
        config["mcpServers"]["zeo"]["env"]["ZEO_SUPABASE_ANON_KEY"] = supabaseKey;
    }

#ifdef __WXMSW__
    // On Windows, Python is the system interpreter — set PYTHONPATH to the
    // bundled site-packages so it finds the correct kipy version.
    wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
    wxFileName siteDir( exePath.GetPath(), "" );
    siteDir.AppendDir( "Lib" );
    siteDir.AppendDir( "site-packages" );

    if( wxDir::Exists( siteDir.GetPath() ) )
    {
        config["mcpServers"]["zeo"]["env"]["PYTHONPATH"] = siteDir.GetPath().ToStdString();
        wxLogInfo( "CC_CONTROLLER: Set PYTHONPATH=%s", siteDir.GetPath() );
    }
#endif

    // Write to temp file
    wxString tempPath = wxFileName::GetTempDir() + "/zeo_cc_mcp.json";
    std::string path = tempPath.ToStdString();

    std::ofstream file( path );
    if( !file.is_open() )
    {
        wxLogError( "CC_CONTROLLER: Failed to write MCP config to %s", path.c_str() );
        return "";
    }

    file << config.dump( 2 );
    file.close();

    wxLogInfo( "CC_CONTROLLER: Generated MCP config at %s", path.c_str() );
    return path;
}


std::string CC_CONTROLLER::LoadSystemPrompt()
{
    if( m_promptsDir.empty() )
        return "";

    auto readFile = []( const std::string& path ) -> std::string
    {
        std::ifstream file( path );
        if( !file.is_open() )
            return "";

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    };

    // Load core.md from the prompts directory
    std::string prompt = readFile( m_promptsDir + "/core.md" );

    if( prompt.empty() )
    {
        wxLogWarning( "CC_CONTROLLER: Could not load core.md from %s", m_promptsDir.c_str() );
        return "";
    }

    // Load MCP addendum from kipy package
    if( !m_pythonPath.empty() )
    {
        std::string addendum;

#ifdef __WXMSW__
        // Windows: bin/Lib/site-packages/kipy/mcp/addendum.md
        {
            wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
            wxFileName siteDir( exePath.GetPath(), "" );
            siteDir.AppendDir( "Lib" );
            siteDir.AppendDir( "site-packages" );
            siteDir.AppendDir( "kipy" );
            siteDir.AppendDir( "mcp" );
            wxString addendumPath = siteDir.GetPath() + wxFileName::GetPathSeparator() + "addendum.md";
            addendum = readFile( addendumPath.ToStdString() );
        }
#else
        // macOS: Use Python to resolve the path — handles both editable and regular installs.
        // Must unset PYTHONHOME since the app bundle sets it for bundled Python 3.9.
        {
            std::string cmd = "unset PYTHONHOME PYTHONPATH; " + m_pythonPath
                              + " -c \"import kipy.mcp, os; print(os.path.join("
                                "os.path.dirname(kipy.mcp.__file__), 'addendum.md'))\" 2>/dev/null";

            FILE* pipe = popen( cmd.c_str(), "r" );
            if( pipe )
            {
                char buf[1024];
                std::string addendumPath;

                if( fgets( buf, sizeof( buf ), pipe ) )
                {
                    addendumPath = buf;
                    while( !addendumPath.empty()
                           && ( addendumPath.back() == '\n' || addendumPath.back() == '\r' ) )
                        addendumPath.pop_back();
                }

                pclose( pipe );

                if( !addendumPath.empty() )
                    addendum = readFile( addendumPath );
            }
        }
#endif

        if( !addendum.empty() )
        {
            prompt += "\n" + addendum;
            wxLogInfo( "CC_CONTROLLER: Appended MCP addendum (%zu bytes)", addendum.size() );
        }
        else
        {
            wxLogWarning( "CC_CONTROLLER: Could not find kipy/mcp/addendum.md" );
        }
    }

    wxLogInfo( "CC_CONTROLLER: Loaded system prompt (%zu bytes)", prompt.size() );
    return prompt;
}


void CC_CONTROLLER::HandleResultMessage( const json& aMsg )
{
    // Final result — conversation turn is complete
    wxLogInfo( "CC_CONTROLLER: Result message received (historySize=%zu)", m_chatHistory.size() );

    // Extract session_id if present
    if( aMsg.contains( "session_id" ) )
        m_sessionId = aMsg["session_id"].get<std::string>();

    // If there's a result text (e.g. from slash commands like /cost), emit it
    if( aMsg.contains( "result" ) && aMsg["result"].is_string() )
    {
        std::string resultText = aMsg["result"].get<std::string>();

        if( !resultText.empty() && m_currentResponse.empty() )
        {
            // No streamed text yet — this is a slash command result
            m_currentResponse = resultText;

            ChatTextDeltaData textData( m_currentResponse, resultText );
            PostChatEvent( m_eventSink, EVT_CHAT_TEXT_DELTA, textData );
        }
    }

    // Complete any remaining pending tools
    for( const auto& toolId : m_pendingToolIds )
    {
        ChatToolCompleteData data( toolId, "", "(completed)", true );
        PostChatEvent( m_eventSink, EVT_CHAT_TOOL_COMPLETE, data );
    }
    m_pendingToolIds.clear();

    // Record API usage to Supabase (fire-and-forget background thread)
    if( aMsg.contains( "usage" ) && aMsg["usage"].is_object() )
    {
        const auto& usage = aMsg["usage"];
        int inputTokens = usage.value( "input_tokens", 0 );
        int outputTokens = usage.value( "output_tokens", 0 );
        int cacheCreation = usage.value( "cache_creation_input_tokens", 0 );
        int cacheRead = usage.value( "cache_read_input_tokens", 0 );

        // Get model from modelUsage keys (first entry)
        std::string model = "claude-code";
        if( aMsg.contains( "modelUsage" ) && aMsg["modelUsage"].is_object() )
        {
            for( const auto& [key, val] : aMsg["modelUsage"].items() )
            {
                model = key;
                break;
            }
        }

        auto& registry = TOOL_REGISTRY::Instance();
        std::string supabaseUrl = registry.GetSupabaseUrl();
        std::string supabaseKey = registry.GetSupabaseAnonKey();
        AGENT_AUTH* auth = registry.GetAuth();

        if( !supabaseUrl.empty() && auth && auth->IsAuthenticated() )
        {
            std::string accessToken = auth->GetAccessToken();
            std::string userId = ""; // Extracted from JWT below

            // Decode user_id from JWT sub claim
            if( !accessToken.empty() )
            {
                // Find second dot to extract payload
                size_t dot1 = accessToken.find( '.' );
                size_t dot2 = ( dot1 != std::string::npos ) ?
                              accessToken.find( '.', dot1 + 1 ) : std::string::npos;

                if( dot1 != std::string::npos && dot2 != std::string::npos )
                {
                    // wxBase64Decode handles URL-safe base64
                    std::string payload = accessToken.substr( dot1 + 1, dot2 - dot1 - 1 );

                    // Add padding
                    while( payload.size() % 4 != 0 )
                        payload += '=';

                    // Replace URL-safe chars
                    std::replace( payload.begin(), payload.end(), '-', '+' );
                    std::replace( payload.begin(), payload.end(), '_', '/' );

                    wxMemoryBuffer decoded = wxBase64Decode( payload );

                    if( decoded.GetDataLen() > 0 )
                    {
                        try
                        {
                            std::string jsonStr( (const char*) decoded.GetData(),
                                                 decoded.GetDataLen() );
                            auto jwt = json::parse( jsonStr );
                            userId = jwt.value( "sub", "" );
                        }
                        catch( ... ) {}
                    }
                }
            }

            if( !userId.empty() )
            {
                // Fire-and-forget POST to api_usage
                json usageRow;
                usageRow["user_id"] = userId;
                usageRow["model"] = model;
                usageRow["input_tokens"] = inputTokens;
                usageRow["output_tokens"] = outputTokens;
                usageRow["cache_creation_tokens"] = cacheCreation;
                usageRow["cache_read_tokens"] = cacheRead;
                usageRow["cost_cents"] = 0;
                usageRow["web_search_count"] = 0;
                usageRow["web_fetch_count"] = 0;

                std::string body = usageRow.dump();

                std::thread( [supabaseUrl, supabaseKey, accessToken, body]()
                {
                    try
                    {
                        KICAD_CURL_EASY curl;
                        curl.SetURL( supabaseUrl + "/rest/v1/api_usage" );
                        curl.SetHeader( "Content-Type", "application/json" );
                        curl.SetHeader( "apikey", supabaseKey );
                        curl.SetHeader( "Authorization", "Bearer " + accessToken );
                        curl.SetHeader( "Prefer", "return=minimal" );
                        curl.SetPostFields( body );
                        curl.Perform();

                        int status = curl.GetResponseStatusCode();
                        if( status >= 200 && status < 300 )
                            wxLogInfo( "CC_CONTROLLER: Recorded API usage" );
                        else
                            wxLogWarning( "CC_CONTROLLER: api_usage insert returned %d", status );
                    }
                    catch( ... )
                    {
                        wxLogWarning( "CC_CONTROLLER: Failed to record API usage" );
                    }
                }).detach();
            }
        }
    }

    m_busy = false;

    ChatTurnCompleteData data( m_toolResultCounter > 0, false );
    PostChatEvent( m_eventSink, EVT_CHAT_TURN_COMPLETE, data );
}
