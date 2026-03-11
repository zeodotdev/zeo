#include "cc_controller.h"
#include "cc_subprocess.h"
#include "cc_events.h"
#include "../core/chat_events.h"

#include "../tools/tool_registry.h"

#include <wx/log.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>

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
    m_pendingToolNames.clear();
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
    // Complete assistant message (after streaming). We already handled
    // content via stream events, so this is mostly informational.
    wxLogInfo( "CC_CONTROLLER: Complete assistant message received" );
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
    wxLogInfo( "CC_CONTROLLER: Result message received" );

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

    m_busy = false;

    ChatTurnCompleteData data( m_toolResultCounter > 0, false );
    PostChatEvent( m_eventSink, EVT_CHAT_TURN_COMPLETE, data );
}
