#include "cc_controller.h"
#include "cc_subprocess.h"
#include "cc_events.h"
#include "../core/chat_events.h"

#include <wx/log.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

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
    m_subprocess->Start( aWorkingDir, m_mcpConfigPath, "claude-opus-4-6", "", systemPrompt );

    ResetTurnState();
    m_sessionId.clear();
    m_toolResultCounter = 0;

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

    m_subprocess->SendUserMessage( aText );

    wxLogInfo( "CC_CONTROLLER: Sent user message (%zu chars)", aText.size() );
}


void CC_CONTROLLER::Cancel()
{
    if( m_subprocess )
    {
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
    m_subprocess->Start( m_workingDir, m_mcpConfigPath, "claude-opus-4-6", "", systemPrompt );

    ResetTurnState();
    m_sessionId.clear();
    m_toolResultCounter = 0;
    m_thinkingIndex = 0;

    wxLogInfo( "CC_CONTROLLER: New session started" );
}


void CC_CONTROLLER::ResumeSession( const std::string& aSessionId )
{
    Cancel();

    std::string systemPrompt = LoadSystemPrompt();

    m_subprocess = std::make_unique<CC_SUBPROCESS>( this );
    m_subprocess->Start( m_workingDir, m_mcpConfigPath, "claude-opus-4-6", aSessionId, systemPrompt );

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
    wxLogInfo( "CC_CONTROLLER: Process exited with code %d", exitCode );

    m_busy = false;

    if( exitCode != 0 )
    {
        ChatErrorData errData( "Claude Code process exited unexpectedly (code "
                               + std::to_string( exitCode ) + ")", true );
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
    wxLogInfo( "CC_CONTROLLER stderr: %s", aEvent.GetString() );
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
        // New assistant message — complete any pending tools from previous turn
        for( const auto& toolId : m_pendingToolIds )
        {
            ChatToolCompleteData data( toolId, "", "(completed)", true );
            PostChatEvent( m_eventSink, EVT_CHAT_TOOL_COMPLETE, data );
        }
        m_pendingToolIds.clear();
    }
    else if( eventType == "message_stop" )
    {
        // Message complete — if there were tool calls, they're now executing
        // Turn complete will come from "result" message or next message_start
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

        // Emit tool generating event
        ChatToolGeneratingData genData( cb.toolId, cb.toolName );
        PostChatEvent( m_eventSink, EVT_CHAT_TOOL_GENERATING, genData );
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

    if( it == m_activeBlocks.end() )
        return;

    ContentBlock& block = it->second;

    if( block.type == BlockType::THINKING )
    {
        m_inThinking = false;

        ChatThinkingDoneData data( m_thinkingContent );
        PostChatEvent( m_eventSink, EVT_CHAT_THINKING_DONE, data );
    }
    else if( block.type == BlockType::TOOL_USE )
    {
        // Tool call is complete — Claude Code will now execute it internally
        // Parse tool input for display
        json toolInput;
        try
        {
            if( !block.toolInput.empty() )
                toolInput = json::parse( block.toolInput );
        }
        catch( ... )
        {
            toolInput = block.toolInput;
        }

        // Build a description from tool name + key input fields
        std::string desc = block.toolName;

        if( toolInput.is_object() )
        {
            if( toolInput.contains( "command" ) )
                desc = toolInput["command"].get<std::string>();
            else if( toolInput.contains( "file_path" ) )
                desc = block.toolName + ": " + toolInput["file_path"].get<std::string>();
            else if( toolInput.contains( "pattern" ) )
                desc = block.toolName + ": " + toolInput["pattern"].get<std::string>();
            else if( toolInput.contains( "prompt" ) )
                desc = block.toolName + ": " + toolInput["prompt"].get<std::string>();
        }

        m_toolResultCounter++;

        ChatToolStartData startData( block.toolId, block.toolName, desc, toolInput );
        PostChatEvent( m_eventSink, EVT_CHAT_TOOL_START, startData );

        // Track this tool as pending (result comes when CC finishes executing it)
        m_pendingToolIds.push_back( block.toolId );
    }

    m_activeBlocks.erase( it );
}


void CC_CONTROLLER::HandleAssistantMessage( const json& aMsg )
{
    // Complete assistant message (after streaming). We already handled
    // content via stream events, so this is mostly informational.
    wxLogInfo( "CC_CONTROLLER: Complete assistant message received" );
}


// ═══════════════════════════════════════════════════════════════════════════
// MCP config and system prompt helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string CC_CONTROLLER::GenerateMcpConfig()
{
    if( m_pythonPath.empty() || m_apiSocketPath.empty() )
    {
        wxLogInfo( "CC_CONTROLLER: Skipping MCP config (python=%s, socket=%s)",
                   m_pythonPath.c_str(), m_apiSocketPath.c_str() );
        return "";
    }

    // Build MCP config JSON pointing to kipy.mcp as an stdio server
    json config;
    config["mcpServers"]["zeo"]["command"] = m_pythonPath;
    config["mcpServers"]["zeo"]["args"] = json::array( { "-m", "kipy.mcp" } );
    config["mcpServers"]["zeo"]["env"]["KICAD_API_SOCKET"] = m_apiSocketPath;

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

    // Try to load MCP addendum from the kipy package (sibling to python path)
    // The addendum.md is in the kipy/mcp/ package directory within site-packages
    if( !m_pythonPath.empty() )
    {
        // Python path is .../Python.framework/Versions/Current/bin/python3
        // Site-packages is .../Python.framework/Versions/Current/lib/python3.X/site-packages/
        wxFileName pyPath( m_pythonPath );
        wxFileName siteDir( pyPath.GetPath(), "" );
        siteDir.RemoveLastDir();  // bin/ -> Versions/Current/
        siteDir.AppendDir( "lib" );

        // Find the python3.X directory
        wxString libPath = siteDir.GetPath();
        wxDir dir( libPath );

        if( dir.IsOpened() )
        {
            wxString subdir;
            bool found = dir.GetFirst( &subdir, "python3*", wxDIR_DIRS );

            if( found )
            {
                siteDir.AppendDir( subdir );
                siteDir.AppendDir( "site-packages" );
                siteDir.AppendDir( "kipy" );
                siteDir.AppendDir( "mcp" );

                std::string addendum = readFile( siteDir.GetPath().ToStdString() + "/addendum.md" );

                if( !addendum.empty() )
                {
                    prompt += "\n" + addendum;
                    wxLogInfo( "CC_CONTROLLER: Appended MCP addendum (%zu bytes)", addendum.size() );
                }
            }
        }
    }

    wxLogInfo( "CC_CONTROLLER: Loaded system prompt (%zu bytes)", prompt.size() );
    return prompt;
}


void CC_CONTROLLER::HandleResultMessage( const json& aMsg )
{
    // Final result — conversation turn is complete
    wxLogInfo( "CC_CONTROLLER: Result message received" );

    // Extract session_id if present
    if( aMsg.contains( "session_id" ) )
        m_sessionId = aMsg["session_id"].get<std::string>();

    // Complete any remaining pending tools
    for( const auto& toolId : m_pendingToolIds )
    {
        ChatToolCompleteData data( toolId, "", "(completed)", true );
        PostChatEvent( m_eventSink, EVT_CHAT_TOOL_COMPLETE, data );
    }
    m_pendingToolIds.clear();

    m_busy = false;

    ChatTurnCompleteData data( m_toolResultCounter > 0, false );
    PostChatEvent( m_eventSink, EVT_CHAT_TURN_COMPLETE, data );
}
