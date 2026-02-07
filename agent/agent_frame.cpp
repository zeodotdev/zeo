#include "agent_frame.h"
#include "agent_chat_history.h"
#include "auth/agent_auth.h"
#include "bridge/webview_bridge.h"
#include "view/agent_markdown.h"
#include "view/unified_html_template.h"
#include "tools/agent_tools.h"
#include "core/chat_controller.h"
#include "core/chat_events.h"
#include "tools/tool_registry.h"
#include "tools/kicad_file/file_writer.h"
#include "tools/schematic/sch_parser.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/log.h>
#include <kiway.h>
#include <paths.h>
#include <kiplatform/environment.h>
#include <frame_type.h>
#include <sstream>
#include <fstream>
#include <thread>
#include <set>
#include <algorithm>
#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dir.h>
#include <wx/stattext.h>
#include <bitmaps.h>
#include <id.h>
#include <nlohmann/json.hpp>
#include <wx/settings.h>
#include <wx/base64.h>
#include <wx/clipbrd.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/mstream.h>
#include <wx/toolbar.h>
#include <wx/bitmap.h>
#include <wx/statbmp.h>
#include <wx/icon.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <kiid.h>

#ifdef __APPLE__
#include "macos_key_monitor.h"
#include <libproc.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

// Helper function to extract the error line from a Python traceback.
// Python tracebacks end with the actual error on the last non-empty line.
static wxString ExtractPythonErrorLine( const std::string& traceback )
{
    // Python tracebacks end with the error line, e.g.:
    // "AttributeError: 'Board' object has no attribute 'foo'"
    // Find the last non-empty line
    size_t pos = traceback.rfind( '\n' );
    while( pos != std::string::npos && pos > 0 )
    {
        size_t prevPos = traceback.rfind( '\n', pos - 1 );
        size_t lineStart = ( prevPos == std::string::npos ) ? 0 : prevPos + 1;
        std::string line = traceback.substr( lineStart, pos - lineStart );

        // Skip empty lines
        if( !line.empty() && line.find_first_not_of( " \t\r" ) != std::string::npos )
        {
            wxString result = wxString::FromUTF8( line );
            result.Replace( "&", "&amp;" );
            result.Replace( "<", "&lt;" );
            result.Replace( ">", "&gt;" );
            return result;
        }
        pos = prevPos;
    }
    return "Unknown error";
}


// Helper: Try to parse raw tool result as JSON and pretty-print it.
// Falls back to HTML-escaped raw string if not valid JSON.
static wxString FormatToolResult( const std::string& aRawResult )
{
    try
    {
        nlohmann::json parsed = nlohmann::json::parse( aRawResult );
        std::string pretty = parsed.dump( 2 );
        wxString result = wxString::FromUTF8( pretty );
        result.Replace( "&", "&amp;" );
        result.Replace( "<", "&lt;" );
        result.Replace( ">", "&gt;" );
        return "<code class=\"language-json\">" + result + "</code>";
    }
    catch( ... )
    {
        // Not valid JSON - HTML-escape and return as-is
        wxString result = wxString::FromUTF8( aRawResult );
        result.Replace( "&", "&amp;" );
        result.Replace( "<", "&lt;" );
        result.Replace( ">", "&gt;" );
        return result;
    }
}


// Helper: Generate a short preview string for a tool result (for collapsed view).
static wxString GetToolResultPreview( const std::string& aRawResult, int aMaxLen = 80 )
{
    try
    {
        nlohmann::json parsed = nlohmann::json::parse( aRawResult );

        if( parsed.is_object() )
        {
            std::string keys;
            int count = 0;

            for( auto it = parsed.begin(); it != parsed.end() && count < 3; ++it, ++count )
            {
                if( !keys.empty() )
                    keys += ", ";
                keys += it.key();
            }

            int total = static_cast<int>( parsed.size() );

            if( total > 3 )
                return wxString::Format( "{%s, ...} (%d keys)", keys, total );
            else
                return wxString::Format( "{%s}", keys );
        }
        else if( parsed.is_array() )
        {
            return wxString::Format( "[%d items]", static_cast<int>( parsed.size() ) );
        }
        else
        {
            wxString val = wxString::FromUTF8( parsed.dump() );

            if( static_cast<int>( val.length() ) > aMaxLen )
                val = val.Left( aMaxLen ) + "...";

            return val;
        }
    }
    catch( ... )
    {
        // Not JSON - take first line
        wxString raw = wxString::FromUTF8( aRawResult );
        wxString firstLine = raw.BeforeFirst( '\n' );

        if( static_cast<int>( firstLine.length() ) > aMaxLen )
            firstLine = firstLine.Left( aMaxLen ) + "...";

        return firstLine;
    }
}


// Helper: Build the full tool result component HTML in "Running..." state.
// Includes the collapsible body (initially empty/hidden) so the JS callback
// can populate it on completion without replacing the element.
static wxString BuildRunningToolHtml( int aIndex, const wxString& aDesc )
{
    return wxString::Format(
        "<div id=\"tool-result-%d\" class=\"bg-bg-secondary rounded-md my-2 max-w-full break-words\">"
        "<a href=\"toggle:toolresult:%d\" "
        "class=\"tool-result-header p-3 px-3 no-underline flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\">%s</span>"
        "<span class=\"tool-status text-text-muted text-[12px] ml-auto\"><i>Running...</i></span>"
        "</a>"
        "<div class=\"tool-result-body p-3 pt-0 border-t border-border-dark\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
        "</div>"
        "</div>",
        aIndex, aIndex, aDesc, aIndex );
}


// Helper: Build the collapsible HTML for a completed tool result block.
// Has a stable id="tool-result-N" matching the running box it replaces.
static wxString BuildToolResultHtml( int aIndex, const wxString& aDesc,
                                     const wxString& aStatusClass, const wxString& aStatusText,
                                     const wxString& aFullFormatted,
                                     const wxString& aImageHtml, bool aExpanded )
{
    wxString displayStyle = aExpanded ? "block" : "none";

    wxString html = wxString::Format(
        "<div id=\"tool-result-%d\" class=\"bg-bg-secondary rounded-md my-2 max-w-full break-words\">"
        // Clickable header: same layout as the Running box
        "<a href=\"toggle:toolresult:%d\" "
        "class=\"tool-result-header p-3 px-3 no-underline flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\">%s</span>"
        "<span class=\"%s text-[12px] ml-auto\"><strong>%s</strong></span>"
        "</a>"
        // Expanded content (hidden by default)
        "<div class=\"p-3 pt-0 border-t border-border-dark\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:%s;\">"
        "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap break-words m-0 mt-2\">%s</pre>"
        "%s"
        "</div>"
        "</div>",
        aIndex,
        aIndex,
        aDesc, aStatusClass, aStatusText,
        aIndex, displayStyle,
        aFullFormatted,
        aImageHtml );

    return html;
}

// Helper: Build the HTML for a tool approval prompt (e.g. "Open" / "Close" editor).
// Uses a non-clickable <div> header (no toggle) since the body is empty during approval.
// Preserves .tool-status and .tool-result-body so updateToolResult() can populate them
// on completion. After the next full re-render the element becomes expandable via
// m_fullHtmlContent which is updated to BuildToolResultHtml by OnChatToolComplete.
static wxString BuildToolApprovalHtml( int aIndex, const wxString& aDesc,
                                       const wxString& aButtonText,
                                       const wxString& aActionHref,
                                       const wxString& aBgColor,
                                       const wxString& aTextColor )
{
    return wxString::Format(
        "<div id=\"tool-result-%d\" class=\"bg-bg-secondary rounded-md my-2 max-w-full break-words\">"
        "<div class=\"p-3 px-3 flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\">%s</span>"
        "<span class=\"tool-status text-[12px] ml-auto\">"
        "<a href=\"%s\" style=\"background:%s; color:%s; padding:3px 14px; "
        "border-radius:6px; font-size:12px; font-weight:600; text-decoration:none; "
        "cursor:pointer;\">%s</a>"
        "</span>"
        "</div>"
        "<div class=\"tool-result-body p-3 pt-0 border-t border-border-dark\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
        "</div>"
        "</div>",
        aIndex, aDesc, aActionHref, aBgColor, aTextColor, aButtonText, aIndex );
}

BEGIN_EVENT_TABLE( AGENT_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, AGENT_FRAME::OnExit )

END_EVENT_TABLE()

AGENT_FRAME::AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_AGENT, "Agent", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE,
                      "agent_frame_name", schIUScale ),
        m_isTrackingAgent( false ),
        m_hasPcbChanges( false ),
        m_pendingOpenSch( false ),
        m_pendingOpenPcb( false ),
        m_pendingCloseSch( false ),
        m_pendingClosePcb( false ),
        m_pendingCloseSaveFirst( true )
{
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

    // --- Single Unified WebView ---
    // The entire UI (top bar, chat area, input, controls, pending changes, auth overlay)
    // is rendered in a single HTML/CSS/JS webview.

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    m_webView = new WEBVIEW_PANEL( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
    m_webView->BindLoadedEvent();

    // Create bridge BEFORE registering the message handler
    m_bridge = std::make_unique<WEBVIEW_BRIDGE>( this, m_webView );

    // All JS→C++ messages route through the bridge
    m_webView->AddMessageHandler( wxS( "agent" ),
        [this]( const wxString& msg ) { m_bridge->OnMessage( msg ); } );

    // Load the unified HTML template (contains top bar, chat, input, controls, overlays)
    m_fullHtmlContent = "";
    m_webView->SetPage( GetUnifiedHtmlTemplate() );
    mainSizer->Add( m_webView, 1, wxEXPAND );

#ifdef __APPLE__
    // WKWebView handles Cmd+A through the Cocoa responder chain, bypassing wxWidgets.
    // Install an NSEvent monitor to intercept Cmd+A when the webview has focus.
    void* nativeHandle = (void*) m_webView->GetWebView()->GetHandle();

    if( nativeHandle )
    {
        InstallSelectAllMonitor(
                nativeHandle,
                [this]()
                {
                    m_webView->RunScriptAsync(
                            wxS( "var ta = document.getElementById('input-textarea');"
                                 "if(ta) { ta.focus(); ta.select(); }" ) );
                } );
    }
#endif

    // ACCELERATOR TABLE for Cmd+C
    wxAcceleratorEntry entries[1];
    entries[0].Set( wxACCEL_CTRL, (int) 'C', ID_CHAT_COPY );
    wxAcceleratorTable accel( 1, entries );
    SetAcceleratorTable( accel );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 600 );

    // Bind menu & accelerator events
    Bind( wxEVT_MENU, &AGENT_FRAME::OnPopupClick, this, ID_CHAT_COPY );

    // Bind Async LLM Streaming Events
    Bind( EVT_LLM_STREAM_CHUNK, &AGENT_FRAME::OnLLMStreamChunk, this );
    Bind( EVT_LLM_STREAM_COMPLETE, &AGENT_FRAME::OnLLMStreamComplete, this );
    Bind( EVT_LLM_STREAM_ERROR, &AGENT_FRAME::OnLLMStreamError, this );

    // Initialize generating animation
    m_generatingDots = 0;
    m_isGenerating = false;
    m_isCompacting = false;
    m_userScrolledUp = false;
    m_lastScrollActivityMs = 0;
    m_htmlUpdatePending = false;
    m_htmlUpdateNeeded = false;
    m_generatingTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnGeneratingTimer, this );
    m_htmlUpdateTimer.Bind( wxEVT_TIMER, &AGENT_FRAME::OnHtmlUpdateTimer, this );

    // Initialize thinking state
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_isStreamingMarkdown = false;
    m_currentThinkingIndex = -1;

    // Initialize tool result toggle state
    m_toolResultCounter = 0;
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    // Bind Size Event
    Bind( wxEVT_SIZE, &AGENT_FRAME::OnSize, this );

    // Initialize History
    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();
    m_pendingToolCalls = nlohmann::json::array();

    // Initialize chat history persistence with timestamp conversation ID
    wxDateTime now = wxDateTime::Now();
    std::string conversationId = now.Format( "%Y-%m-%d_%H-%M-%S" ).ToStdString();
    m_chatHistoryDb.SetConversationId( conversationId );

    // Initialize LLM client and tools
    m_llmClient = std::make_unique<AGENT_LLM_CLIENT>( this );
    InitializeTools();

    // Initialize authentication
    m_auth = new AGENT_AUTH();

    // Load Supabase configuration from JSON file
    std::string supabaseUrl, supabaseKey;

    // Try loading from supabase_config.json in source directory
    wxFileName configPath( __FILE__ );
    configPath.SetFullName( "supabase_config.json" );

    if( wxFileExists( configPath.GetFullPath() ) )
    {
        std::ifstream configFile( configPath.GetFullPath().ToStdString() );

        if( configFile.is_open() )
        {
            try
            {
                json config = json::parse( configFile );
                supabaseUrl = config.value( "project_url", "" );
                supabaseKey = config.value( "publishable_key", "" );

                wxLogTrace( "Agent", "Loaded Supabase config from %s", configPath.GetFullPath() );
            }
            catch( const std::exception& e )
            {
                wxLogWarning( "Failed to parse supabase_config.json: %s", e.what() );
            }
            configFile.close();
        }
    }

    if( !supabaseUrl.empty() && !supabaseKey.empty() )
    {
        m_auth->Configure( supabaseUrl, supabaseKey );
        wxLogTrace( "Agent", "Supabase authentication configured" );
    }
    else
    {
        wxLogWarning( "Supabase configuration not found. Authentication features will be disabled." );
        wxLogWarning( "Create supabase_config.json or set KICAD_AGENT_SUPABASE_URL/KEY environment variables." );
    }

    // Wire auth to LLM client for proxy authentication
    m_llmClient->SetAuth( m_auth );

    // Create chat controller
    m_chatController = std::make_unique<CHAT_CONTROLLER>( this );
    m_chatController->SetLLMClient( m_llmClient.get() );
    m_chatController->SetChatHistoryDb( &m_chatHistoryDb );
    m_chatController->SetAuth( m_auth );
    m_chatController->SetKiwayRequestFn(
        [this]( int aFrameType, const std::string& aPayload ) -> std::string {
            return SendRequest( aFrameType, aPayload );
        } );
    m_chatController->SetProjectPathFn(
        [this]() -> std::string {
            wxString projectPath = Kiway().Prj().GetProjectPath();
            if( projectPath.IsEmpty() )
                return "";

            // Build JSON with project path, PCB file, and schematic hierarchy
            nlohmann::json projectContext;
            projectContext["path"] = projectPath.ToStdString();

            // Get project name for conventional file detection
            wxFileName projDir( projectPath, "" );
            wxString projName = projDir.GetDirs().IsEmpty() ? wxString() : projDir.GetDirs().Last();

            // Find PCB file - try projectName.kicad_pcb first, then scan directory
            wxDir dir( projectPath );
            if( !projName.IsEmpty() )
            {
                wxString expectedPcb = projectPath + projName + ".kicad_pcb";
                if( wxFileExists( expectedPcb ) )
                {
                    projectContext["pcb_file"] = ( projName + ".kicad_pcb" ).ToStdString();
                }
            }

            // Fallback: scan for all PCB files if expected one not found
            if( !projectContext.contains( "pcb_file" ) && dir.IsOpened() )
            {
                nlohmann::json pcbFiles = nlohmann::json::array();
                wxString filename;
                bool cont = dir.GetFirst( &filename, "*.kicad_pcb", wxDIR_FILES );
                while( cont )
                {
                    pcbFiles.push_back( filename.ToStdString() );
                    cont = dir.GetNext( &filename );
                }
                if( !pcbFiles.empty() )
                    projectContext["pcb_files"] = pcbFiles;
            }

            // Build schematic hierarchy from root sheet(s)
            // Define recursive hierarchy builder
            std::function<nlohmann::json( const std::string&, std::set<std::string>& )> buildHierarchy;
            buildHierarchy = [&]( const std::string& schPath,
                                  std::set<std::string>& visited ) -> nlohmann::json {
                nlohmann::json node;

                // Avoid infinite loops from circular references
                if( visited.count( schPath ) )
                    return node;
                visited.insert( schPath );

                auto summary = SchParser::GetSummary( schPath );
                node["file"] = summary.file;
                node["uuid"] = summary.uuid;

                // Recursively process child sheets
                if( !summary.sheets.empty() )
                {
                    nlohmann::json children = nlohmann::json::array();
                    for( const auto& sheet : summary.sheets )
                    {
                        // Resolve child sheet path relative to parent
                        wxFileName childPath( schPath );
                        childPath.SetFullName( sheet.filename );
                        std::string childFullPath = childPath.GetFullPath().ToStdString();

                        nlohmann::json childNode = buildHierarchy( childFullPath, visited );
                        if( !childNode.empty() )
                        {
                            childNode["name"] = sheet.name;  // Display name from parent
                            children.push_back( childNode );
                        }
                    }
                    if( !children.empty() )
                        node["children"] = children;
                }

                return node;
            };

            // Find root schematic(s) - check .kicad_pro for top_level_sheets
            std::vector<std::string> rootSchFiles;

            // Try to read top-level sheets from project file
            if( !projName.IsEmpty() )
            {
                wxString proFile = projectPath + projName + ".kicad_pro";
                if( wxFileExists( proFile ) )
                {
                    std::ifstream ifs( proFile.ToStdString() );
                    if( ifs.good() )
                    {
                        try
                        {
                            nlohmann::json projJson = nlohmann::json::parse( ifs );
                            if( projJson.contains( "schematic" ) &&
                                projJson["schematic"].contains( "top_level_sheets" ) )
                            {
                                for( const auto& sheet : projJson["schematic"]["top_level_sheets"] )
                                {
                                    if( sheet.contains( "filename" ) )
                                    {
                                        wxString schFile = projectPath +
                                            wxString::FromUTF8( sheet["filename"].get<std::string>() );
                                        if( wxFileExists( schFile ) )
                                            rootSchFiles.push_back( schFile.ToStdString() );
                                    }
                                }
                            }
                        }
                        catch( ... )
                        {
                            // JSON parse error - fall back to heuristics
                        }
                    }
                }
            }

            // Fall back to project-name.kicad_sch if no top-level sheets defined
            if( rootSchFiles.empty() && !projName.IsEmpty() )
            {
                wxString rootCandidate = projectPath + projName + ".kicad_sch";
                if( wxFileExists( rootCandidate ) )
                    rootSchFiles.push_back( rootCandidate.ToStdString() );
            }

            // Build hierarchy for each root
            if( !rootSchFiles.empty() )
            {
                nlohmann::json hierarchyArray = nlohmann::json::array();
                std::set<std::string> visited;

                for( const auto& rootFile : rootSchFiles )
                {
                    nlohmann::json rootNode = buildHierarchy( rootFile, visited );
                    if( !rootNode.empty() )
                        hierarchyArray.push_back( rootNode );
                }

                projectContext["hierarchy"] = hierarchyArray;
            }

            // Add files currently open in editors
            auto openFiles = GetOpenEditorFiles();
            if( !openFiles.empty() )
            {
                nlohmann::json arr = nlohmann::json::array();
                for( const auto& f : openFiles )
                    arr.push_back( f.ToStdString() );
                projectContext["open_editor_files"] = arr;
                wxLogInfo( "AGENT: Injecting %zu open editor file(s) into project context",
                           openFiles.size() );
            }

            return projectContext.dump( 2 );
        } );

    // Set model on controller
    m_currentModel = "Claude 4.6 Opus";
    m_chatController->SetModel( m_currentModel );

    // Bind Controller Events
    Bind( EVT_CHAT_TEXT_DELTA, &AGENT_FRAME::OnChatTextDelta, this );
    Bind( EVT_CHAT_THINKING_START, &AGENT_FRAME::OnChatThinkingStart, this );
    Bind( EVT_CHAT_THINKING_DELTA, &AGENT_FRAME::OnChatThinkingDelta, this );
    Bind( EVT_CHAT_THINKING_DONE, &AGENT_FRAME::OnChatThinkingDone, this );
    Bind( EVT_CHAT_TOOL_GENERATING, &AGENT_FRAME::OnChatToolGenerating, this );
    Bind( EVT_CHAT_TOOL_START, &AGENT_FRAME::OnChatToolStart, this );
    Bind( EVT_CHAT_TOOL_COMPLETE, &AGENT_FRAME::OnChatToolComplete, this );
    Bind( EVT_CHAT_TURN_COMPLETE, &AGENT_FRAME::OnChatTurnComplete, this );
    Bind( EVT_CHAT_ERROR, &AGENT_FRAME::OnChatError, this );
    Bind( EVT_CHAT_STATE_CHANGED, &AGENT_FRAME::OnChatStateChanged, this );
    Bind( EVT_CHAT_TITLE_DELTA, &AGENT_FRAME::OnChatTitleDelta, this );
    Bind( EVT_CHAT_TITLE_GENERATED, &AGENT_FRAME::OnChatTitleGenerated, this );
    Bind( EVT_CHAT_HISTORY_LOADED, &AGENT_FRAME::OnChatHistoryLoaded, this );

    // Create menu bar
    wxMenuBar* menuBar = new wxMenuBar();
    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append( wxID_EXIT, "E&xit\tAlt-X", "Exit application" );

    menuBar->Append( fileMenu, "&File" );
    SetMenuBar( menuBar );

    // Push initial UI state to webview after it loads
    // (Auth state, model list, chat title will be pushed once the page finishes loading)
    CallAfter( [this]()
    {
        UpdateAuthUI();

        // Push model list
        std::vector<std::string> models = { "Claude 4.6 Opus" };
        m_bridge->PushModelList( models, m_currentModel );

        // Push initial title
        m_bridge->PushChatTitle( "New Chat" );

        // Push tracking state
        m_bridge->PushTrackingState( m_isTrackingAgent );
    } );
}

AGENT_FRAME::~AGENT_FRAME()
{
#ifdef __APPLE__
    RemoveSelectAllMonitor();
#endif

    // Stop the generating animation timer to prevent timer events
    m_generatingTimer.Stop();

    // Stop tool timeout timer
    m_toolTimeoutTimer.Stop();

    // Cancel any in-progress LLM request and wait for it to finish
    // This prevents the background thread from posting events to a destroyed handler
    if( m_llmClient )
    {
        m_llmClient->CancelRequest();

        // Wait for the request to finish (with timeout to prevent hang)
        int waitCount = 0;
        const int maxWaitMs = 5000;  // 5 second timeout
        const int sleepMs = 10;

        while( m_llmClient->IsRequestInProgress() && waitCount * sleepMs < maxWaitMs )
        {
            wxMilliSleep( sleepMs );
            waitCount++;
        }

        if( m_llmClient->IsRequestInProgress() )
        {
            wxLogWarning( "LLM request did not finish within timeout during AGENT_FRAME destruction" );
        }

        // Add a small safety delay to ensure the thread has fully finished
        // after m_requestInProgress is set to false
        wxMilliSleep( 50 );
    }

    delete m_auth;
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::AppendHtml( const wxString& aHtml )
{
    m_fullHtmlContent += aHtml;
    m_bridge->PushAppendChat( aHtml );
}

void AGENT_FRAME::RebuildThinkingHtml()
{
    // Rebuild thinking HTML based on current state
    // Shows "Thinking" as a clickable link that expands/collapses content

    if( m_thinkingContent.IsEmpty() && !m_isThinking )
    {
        m_thinkingHtml = "";
        return;
    }

    // Escape HTML in thinking content
    wxString escapedContent = m_thinkingContent;
    escapedContent.Replace( "&", "&amp;" );
    escapedContent.Replace( "<", "&lt;" );
    escapedContent.Replace( ">", "&gt;" );
    escapedContent.Replace( "\n", "<br>" );

    // Use placeholder if content is empty (during initial THINKING_START)
    // This ensures the content div exists immediately for user clicks
    wxString displayContent = escapedContent.IsEmpty() ? "<i>Thinking...</i>" : escapedContent;

    // Always render both toggle link and content (content hidden by CSS if collapsed)
    // JavaScript will toggle visibility without page reload
    wxString expandedClass = m_thinkingExpanded ? " expanded" : "";
    wxString displayStyle = m_thinkingExpanded ? "block" : "none";

    wxString thinkingText = "Thinking";

    m_thinkingHtml = wxString::Format(
        "<div class=\"mb-1\">"
        "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline hover:underline\">%s</a>"
        "<div class=\"thinking-content text-[#606060] mt-1 mb-0 pl-3 border-l-2 border-[#404040] whitespace-pre-wrap%s\" data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\">%s</div>"
        "</div>",
        m_currentThinkingIndex, thinkingText, expandedClass, m_currentThinkingIndex, displayStyle, displayContent );
}

void AGENT_FRAME::UpdateAgentResponse()
{
    // Mark that an HTML update is needed and start the throttling timer if not running.
    // This batches multiple rapid calls (on every character during streaming) into
    // ~20 updates/sec max to prevent WebKit segfaults from excessive SetPage() calls.
    m_htmlUpdateNeeded = true;

    if( !m_htmlUpdateTimer.IsRunning() )
    {
        // Start timer with 50ms interval (20 updates per second max)
        m_htmlUpdateTimer.Start( 50, wxTIMER_CONTINUOUS );
    }
}

void AGENT_FRAME::OnGeneratingTimer( wxTimerEvent& aEvent )
{
    // Cycle through 1, 2, 3 dots
    m_generatingDots = ( m_generatingDots % 3 ) + 1;
    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse
}

wxString AGENT_FRAME::BuildStreamingContent()
{
    // Build the streaming content HTML from current state
    wxString streamingContent;

    // Include thinking HTML if available (streamed directly in updateStreamingContent)
    if( !m_thinkingHtml.IsEmpty() )
        streamingContent += m_thinkingHtml;

    // Get current response from controller and append with markdown
    std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";

    // Strip leading newlines to avoid blank line gap after thinking block
    size_t start = currentResponse.find_first_not_of( "\n\r" );

    if( start != std::string::npos && start > 0 )
        currentResponse = currentResponse.substr( start );
    else if( start == std::string::npos )
        currentResponse.clear();

    streamingContent += AgentMarkdown::ToHtml( currentResponse );

    // Include any tool call HTML
    if( !m_toolCallHtml.IsEmpty() )
        streamingContent += m_toolCallHtml;

    // Add animated dots when compacting or generating
    if( m_isCompacting )
    {
        wxString dots;
        for( int i = 0; i < m_generatingDots; i++ )
            dots += ".";
        streamingContent += "<font color='#FFA500'>Compacting" + dots + "</font>";
    }
    else if( m_isGenerating && !m_isStreamingMarkdown )
    {
        wxString dots;
        for( int i = 0; i < m_generatingDots; i++ )
            dots += ".";

        // Show tool call box with "Running..." when a tool name is known
        if( !m_generatingToolName.IsEmpty() )
        {
            streamingContent += wxString::Format(
                "<div class=\"bg-bg-secondary rounded-md my-2 max-w-full break-words\">"
                "<div class=\"p-3 px-3 flex items-center gap-2\">"
                "<span class=\"text-text-secondary text-[12px]\">%s</span>"
                "<span class=\"text-text-muted text-[12px] ml-auto\"><i>Running%s</i></span>"
                "</div></div>",
                m_generatingToolName, dots );
        }
        else
            streamingContent += "<font color='#888888'>" + dots + "</font>";
    }

    return streamingContent;
}

void AGENT_FRAME::FlushStreamingContentUpdate( bool aForce )
{
    // Skip update if user scrolled up, unless forced (e.g., user-initiated toggle)
    if( m_userScrolledUp && !aForce )
        return;

    wxString streamingContent = BuildStreamingContent();
    m_bridge->PushStreamingContent( streamingContent );

    // Clear the pending update flag since we just executed it
    m_htmlUpdateNeeded = false;
}

void AGENT_FRAME::OnHtmlUpdateTimer( wxTimerEvent& aEvent )
{
    // Throttled HTML update - only update if needed AND user hasn't scrolled up
    if( m_htmlUpdateNeeded && !m_userScrolledUp )
    {
        m_htmlUpdateNeeded = false;

        wxString streamingContent = BuildStreamingContent();
        m_bridge->PushStreamingContent( streamingContent );

        // Update full HTML content for when we need to do a full render later
        wxString fullHtml = m_htmlBeforeAgentResponse;
        fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
        fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
        m_fullHtmlContent = fullHtml;
    }
    // If user scrolled up, leave m_htmlUpdateNeeded=true so update happens when they scroll back
}

void AGENT_FRAME::StartGeneratingAnimation()
{
    m_isGenerating = true;
    m_isStreamingMarkdown = false;
    m_generatingDots = 1;
    m_generatingTimer.Start( 400 ); // Update every 400ms
    m_bridge->PushActionButtonState( "Stop" );
}

void AGENT_FRAME::StopGeneratingAnimation()
{
    m_isGenerating = false;
    m_isStreamingMarkdown = false;
    m_generatingTimer.Stop();
    m_htmlUpdateTimer.Stop();
    m_generatingDots = 0;
    m_generatingToolName.Clear();

    // Perform one final HTML update if there was a pending update
    if( m_htmlUpdateNeeded )
    {
        m_htmlUpdateNeeded = false;

        wxString streamingContent = BuildStreamingContent();
        m_bridge->PushStreamingContent( streamingContent );

        // Update full HTML content for consistency
        wxString html = m_htmlBeforeAgentResponse;
        html.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
        html += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
        m_fullHtmlContent = html;
    }

    // Note: Don't set button to "Send" here - let the caller decide
    // (tool execution keeps "Stop", only IDLE sets "Send")
}

void AGENT_FRAME::SetHtml( const wxString& aHtml )
{
    m_fullHtmlContent = aHtml;
    m_bridge->PushFullChatContent( aHtml );
}

void AGENT_FRAME::AutoScrollToBottom()
{
    // Auto-scroll now handled by CSS flex-direction: column-reverse
    // No-op kept for compatibility with existing code
}

void AGENT_FRAME::OnChatScroll( wxScrollWinEvent& aEvent )
{
    // Scroll tracking now handled by CSS flex-direction: column-reverse
    // No manual scroll management needed
    aEvent.Skip();
}

void AGENT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        std::string payload = aEvent.GetPayload();

        // Check if we're in async tool execution mode (frame's context has an executing tool)
        // NOTE: The controller also executes tools via the synchronous SendRequest path,
        // which expects m_toolResponse to be set. Only use async path if the FRAME
        // actually has a tool marked as executing.
        PendingToolCall* executing = m_conversationCtx.GetExecutingToolCall();

        if( executing )
        {
            // Frame has an executing tool - use async path
            // Post tool completion event
            ToolExecutionResult* result = new ToolExecutionResult();
            result->tool_use_id = executing->tool_use_id;
            result->tool_name = executing->tool_name;
            result->result = payload;
            result->success = !payload.empty() && payload.find( "Error:" ) != 0;
            result->execution_time_ms = ( wxGetLocalTimeMillis() - executing->start_time ).GetValue();

            PostToolResult( this, *result );
            delete result;  // PostToolResult copies the data
        }
        else
        {
            // Sync mode (controller path) - store response for SendRequest() to pick up
            m_toolResponse = payload;
        }
    }
    else if( aEvent.Command() == MAIL_SELECTION )
    {
        std::string payload = aEvent.GetPayload();

        if( payload.find( "JSON_PAYLOAD" ) == 0 )
        {
            std::stringstream ss( payload );
            std::string       prefix, source, jsonStr;
            std::getline( ss, prefix ); // JSON_PAYLOAD
            std::getline( ss, source ); // SCH or PCB

            // Remainder is JSON
            // We can determine start pos by prefix.length() + source.length() + 2 (newlines)
            // Or just read the rest of the stream
            std::string line;
            while( std::getline( ss, line ) )
            {
                jsonStr += line + "\n";
            }

            // Update JSON Store
            if( source == "SCH" )
                m_schJson = jsonStr;
            else if( source == "PCB" )
                m_pcbJson = jsonStr;

            // Update Selection Pill
            try
            {
                auto j = nlohmann::json::parse( jsonStr );
                if( j.contains( "selection" ) && !j["selection"].empty() )
                {
                    nlohmann::json& firstItem = j["selection"][0];
                    std::string     label = "Add: Selection"; // Default

                    if( firstItem.contains( "ref" ) )
                    {
                        label = "Add: " + firstItem["ref"].get<std::string>();
                    }
                    else if( firstItem.contains( "type" ) )
                    {
                        std::string type = firstItem["type"].get<std::string>();
                        if( type == "pin" || type == "pad" )
                        {
                            std::string num =
                                    firstItem.contains( "number" ) ? firstItem["number"].get<std::string>() : "?";
                            label = "Add: " + type + " " + num;
                        }
                        else if( type == "wire" || type == "track" )
                        {
                            label = "Add: " + type;
                            if( firstItem.contains( "net_name" ) )
                                label += " (" + firstItem["net_name"].get<std::string>() + ")";
                        }
                        else
                        {
                            label = "Add: " + type;
                        }
                    }

                    size_t count = j["selection"].size();
                    if( count > 1 )
                        label += " +" + std::to_string( count - 1 );

                    m_currentSelectionLabel = label;
                    m_bridge->PushSelectionPill( label, true );
                }
                else
                {
                    m_currentSelectionLabel.Clear();
                    m_bridge->PushSelectionPill( wxEmptyString, false );
                }
            }
            catch( ... )
            {
                // Parse error
            }
        }
        else if( payload == "SELECTION\nSCH\nCLEARED" )
        {
            // Clear SCH selection specifically?
            // Actually, new tools send JSON with empty selection array instead of this?
            // I updated tools to send JSON *if !empty()*.
            // Wait, I updated tools: "if( m_selection.Empty() ) { ... SELECTION\nSCH\nCLEARED ... }"
            // So I still need to handle legacy clear messages?
            // Yes.
            if( payload.find( "SCH" ) != std::string::npos
                || payload.find( "PCB" ) != std::string::npos )
            {
                m_currentSelectionLabel.Clear();
                m_bridge->PushSelectionPill( wxEmptyString, false );
            }
        }
    }
    else if( aEvent.Command() == MAIL_AUTH_STATE_CHANGED )
    {
        // Reload session from keychain (tokens were saved by launcher's SESSION_MANAGER)
        if( m_auth )
        {
            m_auth->LoadSession();
            UpdateAuthUI();
        }

        // If sign-in was from agent frame, bring it to front (after UI is updated)
        std::string payload = aEvent.GetPayload();
        if( payload.find( "source=agent" ) != std::string::npos )
        {
            if( IsIconized() )
                Iconize( false );
            Show( true );
            Raise();
            RequestUserAttention();
        }
    }
    else if( aEvent.Command() == MAIL_AGENT_DIFF_CLEARED ||
             aEvent.Command() == MAIL_AGENT_CHECK_CHANGES )
    {
        wxLogInfo( "AGENT_FRAME: Received diff/check changes notification, refreshing panel" );
        QueryPendingChanges();

        m_currentSelectionLabel.Clear();
        m_bridge->PushSelectionPill( wxEmptyString, false );
    }
    else if( aEvent.Command() == MAIL_AGENT_TRACKING_BROKEN )
    {
        m_isTrackingAgent = false;
        m_bridge->PushTrackingState( false );
    }
}

void AGENT_FRAME::DoSelectionPillClick()
{
    wxLogInfo( "AGENT_FRAME::DoSelectionPillClick called" );

    if( !m_currentSelectionLabel.IsEmpty() )
    {
        wxString label = m_currentSelectionLabel;

        // Strip "Add: " prefix if present for the tag
        if( label.StartsWith( "Add: " ) )
            label = label.Mid( 5 );

        // Hide pill
        m_currentSelectionLabel.Clear();
        m_bridge->PushSelectionPill( wxEmptyString, false );

        // Append @{Label} to input via bridge
        // Build the text to append and let JS handle cursor placement
        m_bridge->PushInputSetText( "@{" + label + "} " );
    }
}


void AGENT_FRAME::OnSend( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnSend called" );
    // NOTE: This method still uses legacy code because it handles KiCad-specific requirements
    // (authentication, pending editor state, system prompt with schematic/PCB context, KIWAY
    // target sheet reset) that the controller doesn't currently support.
    // System prompt is now handled server-side.

    // If already processing, this button acts as Stop
    if( m_isGenerating )
    {
        OnStop( aEvent );
        return;
    }

    // Check authentication first
    if( !CheckAuthentication() )
    {
        AppendHtml( "<p><i>Please sign in to continue.</i></p>" );
        return;
    }

    // Auto-reject pending open/close editor request if user sends a new message.
    // Use Cancel() which handles orphaned tools and transitions to IDLE.
    // Don't use HandleToolResult() as it would call ContinueChat() and start an LLM request,
    // but OnSend will also start a request → we'd get duplicate requests.
    if( m_pendingOpenSch || m_pendingOpenPcb || m_pendingCloseSch || m_pendingClosePcb )
    {
        if( m_chatController )
            m_chatController->Cancel();

        // Replace approval box with "Cancelled" in the permanent DOM
        if( !m_activeRunningHtml.IsEmpty() && m_activeToolResultIdx >= 0 )
        {
            wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Tool execution" ) : m_lastToolDesc;
            wxString cancelledHtml = BuildToolResultHtml( m_activeToolResultIdx, desc,
                "text-text-muted", "Cancelled", "User cancelled", "", false );
            m_fullHtmlContent.Replace( m_activeRunningHtml, cancelledHtml );
            m_activeRunningHtml.Clear();
        }

        m_pendingOpenSch = false;
        m_pendingOpenPcb = false;
        m_pendingOpenToolId.clear();
        m_pendingCloseSch = false;
        m_pendingClosePcb = false;
        m_pendingCloseToolId.clear();
        m_toolCallHtml = "";
    }

    wxString text = m_pendingInputText;
    m_pendingInputText.Clear();
    if( text.IsEmpty() && m_pendingAttachments.empty() )
        return;

    // Reset scroll state for new user message - user sending indicates engagement at bottom
    m_userScrolledUp = false;

    // Build user message HTML
    wxString escapedText = text;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );

    wxString bubbleContent = FileAttach::BuildAttachmentBubbleHtml( m_pendingAttachments )
                             + escapedText;
    wxString msgHtml = wxString::Format(
        "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
        bubbleContent );

    // Add streaming content container for incremental updates
    wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );

    // Append to internal HTML state
    m_fullHtmlContent += msgHtml + streamingDiv;

    // Save HTML snapshot for markdown re-rendering during streaming
    m_htmlBeforeAgentResponse = m_fullHtmlContent;

    // Full page re-render
    SetHtml( m_fullHtmlContent );

    // Clear Input and Update UI
    m_bridge->PushInputClear();
    m_bridge->PushActionButtonState( "Stop" );

    // Configure controller for this request (system prompt now handled server-side)
    if( m_chatController )
    {
        // Sync frame's history to controller before repair
        // (controller may be out of sync if conversation was loaded from disk)
        m_chatController->SetHistory( m_chatHistory );

        // Repair orphaned tool_use/tool_result blocks
        m_chatController->RepairHistory();

        // Sync repaired history back to frame for rendering/persistence
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Reset frame streaming state
    m_currentResponse = "";
    m_toolCallHtml = "";
    m_thinkingHtml = "";
    m_thinkingContent = "";
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();
    // NOTE: Don't reset m_toolResultCounter here - old tool-result-N IDs persist in
    // m_fullHtmlContent after SetHtml(). Counter must be monotonically increasing to
    // avoid duplicate DOM IDs. Only reset in DoNewChat/OnChatHistoryLoaded (full re-render).
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;
    m_stopRequested = false;
    m_userScrolledUp = false;
    m_htmlUpdatePending = false;

    // Transition frame's state machine (legacy - controller also has state machine)
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Configure LLM client with selected model
    m_llmClient->SetModel( m_currentModel );

    // Reset target sheet for new conversation turn
    std::string emptyPayload;
    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_RESET_TARGET_SHEET, emptyPayload );

    // Send message via controller (handles history, starts LLM request)
    if( m_chatController )
    {
        if( !m_pendingAttachments.empty() )
        {
            std::vector<CHAT_CONTROLLER::UserAttachment> attachments;
            for( const auto& att : m_pendingAttachments )
                attachments.push_back( { att.base64_data, att.media_type } );

            m_chatController->SendMessageWithAttachments( text.ToStdString(), attachments );
        }
        else
        {
            m_chatController->SendMessage( text.ToStdString() );
        }

        // Sync controller's history back to frame for rendering and persistence
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
    }
    else
    {
        // Fallback: legacy path if no controller (shouldn't happen)
        nlohmann::json userMsg = { { "role", "user" }, { "content", text.ToStdString() } };
        m_chatHistory.push_back( userMsg );
        m_apiContext.push_back( userMsg );
        m_chatHistoryDb.Save( m_chatHistory );
        StartAsyncLLMRequest();
    }

    m_pendingAttachments.clear();
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnStop called" );
    using json = nlohmann::json;

    // Delegate cancel logic to controller
    if( m_chatController )
        m_chatController->Cancel();

    // Stop generating animation and compacting state
    StopGeneratingAnimation();
    m_isCompacting = false;

    // Cancel any in-progress async LLM request
    if( m_llmClient && m_llmClient->IsRequestInProgress() )
    {
        m_llmClient->CancelRequest();
    }

    // Signal to stop - affects tool execution loops and streaming callbacks (legacy)
    m_stopRequested = true;

    // Note: StopGeneratingAnimation() already did the final render without dots,
    // so we don't need to call UpdateAgentResponse() here (would restart timer with cleared state)

    // Sync frame's history from controller (controller handles orphaned tool_use blocks)
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
    }

    // Clear uncommitted tool calls (haven't been added to history yet)
    if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
    {
        m_pendingToolCalls = json::array();
    }

    // Replace approval box with "Cancelled" if an editor approval was pending
    if( ( m_pendingOpenSch || m_pendingOpenPcb || m_pendingCloseSch || m_pendingClosePcb )
        && !m_activeRunningHtml.IsEmpty() && m_activeToolResultIdx >= 0 )
    {
        wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Tool execution" ) : m_lastToolDesc;
        wxString cancelledHtml = BuildToolResultHtml( m_activeToolResultIdx, desc,
            "text-text-muted", "Cancelled", "User cancelled", "", false );
        m_fullHtmlContent.Replace( m_activeRunningHtml, cancelledHtml );
        m_activeRunningHtml.Clear();

        // Also update the DOM element directly
        m_bridge->PushToolResultUpdate( m_activeToolResultIdx, "text-text-muted", "Cancelled",
            "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap "
            "break-words m-0 mt-2\">User cancelled</pre>" );
    }

    // Clear pending editor open/close request state (prevents stale approval button clicks)
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();
    m_pendingCloseSch = false;
    m_pendingClosePcb = false;
    m_pendingCloseToolId.clear();

    // Transition state machine to IDLE
    m_conversationCtx.TransitionTo( AgentConversationState::IDLE );

    // Finalize the streaming content div so next response uses a fresh div
    m_bridge->PushFinalizeStreaming();
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // Preserve thinking for index tracking
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
    }

    // Clear streaming state
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_currentThinkingIndex = -1;
    m_toolResultCounter = 0;
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    AppendHtml( "<br><p><i>(Stopped)</i></p>" );
    m_bridge->PushActionButtonState( "Send" );
}

// ── Bridge callback methods (called by WEBVIEW_BRIDGE) ──────────────────

void AGENT_FRAME::OnBridgeSubmit( const nlohmann::json& aMsg )
{
    wxString text = wxString::FromUTF8( aMsg.value( "text", "" ) );
    m_pendingAttachments = FileAttach::ParseAttachmentsFromJson( aMsg );

    if( !text.IsEmpty() || !m_pendingAttachments.empty() )
    {
        m_pendingInputText = text;
        wxCommandEvent evt;
        OnSend( evt );
    }
}

void AGENT_FRAME::OnBridgeAttachClick()
{
    wxFileDialog dlg( this, "Attach File", "", "",
                      "Supported files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.pdf)"
                      "|*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.pdf"
                      "|Image files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp)"
                      "|*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp"
                      "|PDF files (*.pdf)|*.pdf",
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE );

    if( dlg.ShowModal() == wxID_OK )
    {
        wxArrayString paths;
        dlg.GetPaths( paths );

        for( const auto& path : paths )
        {
            FILE_ATTACHMENT att;
            bool loaded = false;

            wxFileName fn( path );
            wxString ext = fn.GetExt().Lower();

            if( ext == "pdf" )
                loaded = FileAttach::LoadFileFromDisk( path, att );
            else
                loaded = FileAttach::LoadImageFromFile( path, att );

            if( !loaded )
                continue;

            m_bridge->PushAddAttachment( wxString::FromUTF8( att.base64_data ),
                                         wxString::FromUTF8( att.media_type ),
                                         wxString::FromUTF8( att.filename ) );
        }
    }
}

void AGENT_FRAME::OnBridgeLinkClick( const nlohmann::json& aMsg )
{
    wxString href = wxString::FromUTF8( aMsg.value( "href", "" ) );
    wxLogInfo( "AGENT_FRAME::OnBridgeLinkClick - href: %s", href.ToStdString().c_str() );

    if( href == "agent:approve_open" )
        OnApproveOpenEditor();
    else if( href == "agent:reject_open" )
        OnRejectOpenEditor();
    else if( href == "agent:approve_close" )
        OnApproveCloseEditor();
    else if( href.StartsWith( "http://" ) || href.StartsWith( "https://" ) )
        wxLaunchDefaultBrowser( href );
}

void AGENT_FRAME::OnBridgeCopy( const nlohmann::json& aMsg )
{
    wxString text = wxString::FromUTF8( aMsg.value( "text", "" ) );

    if( !text.IsEmpty() && wxTheClipboard->Open() )
    {
        wxTheClipboard->SetData( new wxTextDataObject( text ) );
        wxTheClipboard->Close();
    }
}

void AGENT_FRAME::OnBridgeCopyImage( const nlohmann::json& aMsg )
{
    wxString src = wxString::FromUTF8( aMsg.value( "src", "" ) );
    int commaPos = src.Find( ',' );

    if( commaPos != wxNOT_FOUND )
    {
        wxString b64 = src.Mid( commaPos + 1 );
        wxMemoryBuffer buf = wxBase64Decode( b64 );

        if( buf.GetDataLen() > 0 )
        {
            wxMemoryInputStream stream( buf.GetData(), buf.GetDataLen() );
            m_pendingCopyImage = wxImage( stream, wxBITMAP_TYPE_PNG );
        }
    }

    if( m_pendingCopyImage.IsOk() )
    {
        wxMenu menu;
        menu.Append( ID_COPY_IMAGE, "Copy Image" );

        menu.Bind( wxEVT_MENU, [this]( wxCommandEvent& )
        {
            if( m_pendingCopyImage.IsOk() && wxTheClipboard->Open() )
            {
                wxTheClipboard->SetData(
                        new wxBitmapDataObject( wxBitmap( m_pendingCopyImage ) ) );
                wxTheClipboard->Flush();
                wxTheClipboard->Close();
            }
            m_pendingCopyImage = wxImage();
        }, ID_COPY_IMAGE );

        PopupMenu( &menu, ScreenToClient( wxGetMousePosition() ) );
    }
}

void AGENT_FRAME::OnBridgePreviewImage( const nlohmann::json& aMsg )
{
    wxString src = wxString::FromUTF8( aMsg.value( "src", "" ) );
    int commaPos = src.Find( ',' );

    if( commaPos != wxNOT_FOUND )
        FileAttach::ShowPreviewDialog( this, src.Mid( commaPos + 1 ) );
}

void AGENT_FRAME::OnBridgePreviewFile( const nlohmann::json& aMsg )
{
    std::string b64 = aMsg.value( "base64", "" );
    std::string filename = aMsg.value( "filename", "document.pdf" );

    if( !b64.empty() )
        FileAttach::OpenFilePreview( wxString::FromUTF8( b64 ),
                                     wxString::FromUTF8( filename ) );
}

void AGENT_FRAME::OnBridgeThinkingToggled( const nlohmann::json& aMsg )
{
    int index = aMsg.value( "index", -1 );
    bool expanded = aMsg.value( "expanded", false );

    if( index == m_currentThinkingIndex && m_currentThinkingIndex >= 0 )
    {
        m_thinkingExpanded = expanded;
        RebuildThinkingHtml();
        FlushStreamingContentUpdate( true );
    }
    else if( index >= 0 && index < (int)m_historicalThinking.size() )
    {
        if( expanded )
            m_historicalThinkingExpanded.insert( index );
        else
            m_historicalThinkingExpanded.erase( index );

        wxString from = wxString::Format(
            "data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\"",
            index, expanded ? "none" : "block" );
        wxString to = wxString::Format(
            "data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\"",
            index, expanded ? "block" : "none" );
        m_fullHtmlContent.Replace( from, to );
        m_htmlBeforeAgentResponse.Replace( from, to );
    }
}

void AGENT_FRAME::OnBridgeToolResultToggled( const nlohmann::json& aMsg )
{
    int index = aMsg.value( "index", -1 );
    bool expanded = aMsg.value( "expanded", false );

    if( index >= 0 )
    {
        if( expanded )
            m_historicalToolResultExpanded.insert( index );
        else
            m_historicalToolResultExpanded.erase( index );

        wxString from = wxString::Format(
            "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:%s;\"",
            index, expanded ? "none" : "block" );
        wxString to = wxString::Format(
            "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:%s;\"",
            index, expanded ? "block" : "none" );
        m_fullHtmlContent.Replace( from, to );
        m_htmlBeforeAgentResponse.Replace( from, to );
    }
}

void AGENT_FRAME::OnBridgeScrollActivity( const nlohmann::json& aMsg )
{
    bool active = aMsg.value( "active", false );
    bool coupled = aMsg.value( "coupled", true );

    m_lastScrollActivityMs = wxGetLocalTimeMillis().GetValue();
    m_userScrolledUp = active || !coupled;
}

void AGENT_FRAME::OnBridgeHistoryOpen()
{
    auto historyList = m_chatHistoryDb.GetHistoryList();

    nlohmann::json entries = nlohmann::json::array();
    for( const auto& entry : historyList )
    {
        nlohmann::json e;
        e["id"] = entry.id;
        e["title"] = entry.title.empty() ? "Untitled Chat" : entry.title;
        e["timestamp"] = entry.lastUpdated;
        entries.push_back( e );
    }

    m_bridge->PushHistoryList( entries );
    m_bridge->PushHistoryShow( true );
}

void AGENT_FRAME::OnBridgeHistorySearch( const wxString& aQuery )
{
    auto historyList = m_chatHistoryDb.GetHistoryList();

    nlohmann::json entries = nlohmann::json::array();
    wxString lowerQuery = aQuery.Lower();

    for( const auto& entry : historyList )
    {
        wxString title = wxString::FromUTF8( entry.title );
        if( aQuery.IsEmpty() || title.Lower().Contains( lowerQuery ) )
        {
            nlohmann::json e;
            e["id"] = entry.id;
            e["title"] = entry.title.empty() ? "Untitled Chat" : entry.title;
            e["timestamp"] = entry.lastUpdated;
            entries.push_back( e );
        }
    }

    m_bridge->PushHistoryList( entries );
}

// ── Bridge-triggered actions ────────────────────────────────────────────

void AGENT_FRAME::DoModelChange( const std::string& aModel )
{
    wxLogInfo( "AGENT_FRAME::DoModelChange - model: %s", aModel.c_str() );

    if( aModel != m_currentModel )
    {
        m_currentModel = aModel;

        if( m_chatController )
            m_chatController->SetModel( m_currentModel );
    }
}

void AGENT_FRAME::DoSendClick()
{
    wxCommandEvent evt;
    OnSend( evt );
}

void AGENT_FRAME::DoStopClick()
{
    wxCommandEvent evt;
    OnStop( evt );
}

void AGENT_FRAME::DoTrackToggle()
{
    m_isTrackingAgent = !m_isTrackingAgent;

    nlohmann::json payload;
    payload["tracking"] = m_isTrackingAgent;
    std::string payloadStr = payload.dump();

    m_bridge->PushTrackingState( m_isTrackingAgent );

    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_TRACKING_MODE, payloadStr );
    Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_TRACKING_MODE, payloadStr );
}

void AGENT_FRAME::DoPendingChangesToggle()
{
    // Re-query and push updated pending changes (update auto-shows/hides)
    QueryPendingChanges();
}

void AGENT_FRAME::DoPendingChangesAcceptAll()
{
    wxLogInfo( "AGENT_FRAME::DoPendingChangesAcceptAll" );

    // Accept schematic changes
    if( !m_pendingSchSheets.empty() )
    {
        KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_APPROVE, payload );
        }
    }

    // Accept PCB changes
    if( m_hasPcbChanges )
    {
        KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_APPROVE, payload );
        }
    }

    // Clear state and update UI
    m_pendingSchSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.Clear();

    QueryPendingChanges();
}

void AGENT_FRAME::DoPendingChangesRejectAll()
{
    wxLogInfo( "AGENT_FRAME::DoPendingChangesRejectAll" );

    // Reject schematic changes
    if( !m_pendingSchSheets.empty() )
    {
        KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REJECT, payload );
        }
    }

    // Reject PCB changes
    if( m_hasPcbChanges )
    {
        KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REJECT, payload );
        }
    }

    // Clear state and update UI
    m_pendingSchSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.Clear();

    QueryPendingChanges();
}

void AGENT_FRAME::DoPendingChangesView( const wxString& aPath, bool aIsPcb )
{
    wxLogInfo( "AGENT_FRAME::DoPendingChangesView: %s (isPcb=%d)", aPath, aIsPcb );

    if( aIsPcb )
    {
        Kiway().Player( FRAME_PCB_EDITOR, true );
        std::string payload;
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_VIEW_CHANGES, payload );
    }
    else
    {
        Kiway().Player( FRAME_SCH, true );
        nlohmann::json j;
        j["sheet_path"] = aPath.ToStdString();
        std::string payload = j.dump();
        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_VIEW_CHANGES, payload );
    }
}

void AGENT_FRAME::DoSignIn()
{
    wxLogInfo( "AGENT_FRAME::DoSignIn called" );
    if( m_auth )
        m_auth->StartOAuthFlow( "agent" );
}

void AGENT_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

std::string AGENT_FRAME::SendRequest( int aDestFrame, const std::string& aPayload )
{
    // Ensure the target frame exists before sending the message
    // This is necessary because Kiway silently drops messages to non-existent frames
    KIWAY_PLAYER* targetPlayer = Kiway().Player( static_cast<FRAME_T>( aDestFrame ), true );
    if( !targetPlayer )
    {
        wxLogError( "AGENT: SendRequest failed - could not create target frame %d", aDestFrame );
        return "Error: Failed to create target frame for tool execution.";
    }

    // Log the request (truncate payload if too long)
    wxString payloadPreview = wxString::FromUTF8( aPayload.substr( 0, 200 ) );
    if( aPayload.length() > 200 )
        payloadPreview += "...";
    wxLogInfo( "AGENT: SendRequest to frame %d, payload: %s", aDestFrame, payloadPreview );

    // Use a sentinel value to distinguish "no response yet" from "empty response received"
    static const std::string NO_RESPONSE_SENTINEL = "\x01__NO_RESPONSE__\x01";
    m_toolResponse = NO_RESPONSE_SENTINEL;
    std::string payloadCopy = aPayload;

    Kiway().ExpressMail( static_cast<FRAME_T>( aDestFrame ), MAIL_AGENT_REQUEST, payloadCopy );

    // Wait for response (Sync)
    // We expect the target frame to reply via MAIL_AGENT_RESPONSE which sets m_toolResponse
    wxLongLong start = wxGetLocalTimeMillis();
    constexpr long TIMEOUT_MS = 10000;
    m_stopRequested = false;  // Reset stop flag
    while( m_toolResponse == NO_RESPONSE_SENTINEL && ( wxGetLocalTimeMillis() - start < TIMEOUT_MS ) )
    {
        wxYield(); // Process events (including the MailIn event and Stop button)
        if( m_stopRequested )
        {
            wxLogInfo( "AGENT: SendRequest cancelled by user after %lld ms",
                       ( wxGetLocalTimeMillis() - start ).GetValue() );
            return "Error: Tool execution cancelled by user.";
        }
        wxMilliSleep( 10 );
    }

    long elapsed = ( wxGetLocalTimeMillis() - start ).GetValue();
    bool timedOut = ( m_toolResponse == NO_RESPONSE_SENTINEL );
    bool emptyResponse = ( m_toolResponse.empty() );

    if( timedOut )
    {
        wxLogError( "AGENT: SendRequest TIMEOUT after %ld ms waiting for frame %d (no response received)",
                    elapsed, aDestFrame );
        return wxString::Format(
            "Error: Tool execution timed out after %ld ms (no response received).",
            elapsed ).ToStdString();
    }

    if( emptyResponse )
    {
        wxLogWarning( "AGENT: SendRequest got EMPTY response after %ld ms from frame %d",
                      elapsed, aDestFrame );
        return wxString::Format(
            "Error: Tool returned empty response after %ld ms.",
            elapsed ).ToStdString();
    }

    // Log successful response (truncate if too long)
    wxString responsePreview = wxString::FromUTF8( m_toolResponse.substr( 0, 200 ) );
    if( m_toolResponse.length() > 200 )
        responsePreview += "...";
    wxLogInfo( "AGENT: SendRequest got response after %ld ms: %s", elapsed, responsePreview );

    return m_toolResponse;
}

void AGENT_FRAME::OnChatRightClick( wxMouseEvent& aEvent )
{
    // Right-click now handled by JavaScript contextmenu event listener
    // See agent_html_template.cpp for implementation
    aEvent.Skip();
}

void AGENT_FRAME::OnPopupClick( wxCommandEvent& aEvent )
{
    // Copy now handled by JavaScript message passing
    // See OnBridgeCopy() for "copy" action handling
    aEvent.Skip();
}

// ============================================================================
// Native Tool Calling Implementation
// ============================================================================

void AGENT_FRAME::InitializeTools()
{
    m_tools = AgentTools::GetToolDefinitions();
}

// NOTE: ExecuteTool was removed - tools are now executed via CHAT_CONTROLLER::ExecuteNextTool()
// which sets the project path via the m_getProjectPathFn callback.

// NOTE: HandleLLMEvent and ContinueConversation were removed in Phase 5.3
// They were dead code - only used in the synchronous path which is no longer called.
// All LLM streaming now uses the async path: StartAsyncLLMRequest -> OnLLMStream* events

// ============================================================================
// Async LLM Streaming Event Handlers
// ============================================================================

void AGENT_FRAME::StartAsyncLLMRequest()
{
    // Start the generating animation
    StartGeneratingAnimation();

    m_llmClient->SetModel( m_currentModel );

    // Filter out thinking blocks from API context before sending to API
    // (System prompt now handled server-side)
    // (Anthropic requires signatures for thinking blocks, which we don't store)
    // Note: m_apiContext may be compacted after context recovery
    nlohmann::json filteredHistory = nlohmann::json::array();
    for( const auto& msg : m_apiContext )
    {
        if( msg.contains( "content" ) && msg["content"].is_array() )
        {
            // Filter content array to remove thinking blocks
            nlohmann::json filteredContent = nlohmann::json::array();
            for( const auto& block : msg["content"] )
            {
                if( !block.contains( "type" ) || block["type"] != "thinking" )
                {
                    filteredContent.push_back( block );
                }
            }

            // Only add message if it has non-empty content after filtering
            if( !filteredContent.empty() )
            {
                nlohmann::json filteredMsg = msg;
                filteredMsg["content"] = filteredContent;
                filteredHistory.push_back( filteredMsg );
            }
        }
        else
        {
            // String content or other format - pass through as-is
            filteredHistory.push_back( msg );
        }
    }

    // Start async request - returns immediately
    if( !m_llmClient->AskStreamWithToolsAsync( filteredHistory, m_tools, this ) )
    {
        wxLogInfo( "AGENT: Failed to start async LLM request" );
        StopGeneratingAnimation();
        AppendHtml( "<p><font color='red'>Error: Failed to start LLM request</font></p>" );
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        m_bridge->PushActionButtonState( "Send" );
    }
}


void AGENT_FRAME::RetryLastRequest()
{
    // Reset streaming state for retry
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_thinkingExpanded = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();
    m_pendingCloseSch = false;
    m_pendingClosePcb = false;
    m_pendingCloseToolId.clear();

    // Ensure we're in the right state
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Start the async LLM request (uses m_apiContext which has been compacted)
    StartAsyncLLMRequest();
}


void AGENT_FRAME::OnLLMStreamChunk( wxThreadEvent& aEvent )
{
    // Get the chunk data from the event payload
    LLMStreamChunk* chunk = aEvent.GetPayload<LLMStreamChunk*>();
    if( !chunk )
        return;

    // Forward to controller for processing
    // Controller emits EVT_CHAT_* events which are handled by OnChat* methods
    if( m_chatController )
    {
        m_chatController->HandleLLMChunk( *chunk );
    }

    // Clean up
    delete chunk;
}

// NOTE: HandleLLMChunk was removed in Phase 5.3c
// All LLM chunk processing now goes through the controller which emits EVT_CHAT_* events


void AGENT_FRAME::OnLLMStreamComplete( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT: OnLLMStreamComplete called" );

    // Forward to controller - it will emit appropriate EVT_CHAT_* events
    // UI updates are handled by OnChatTurnComplete, OnChatError, etc.
    if( m_chatController )
    {
        m_chatController->HandleLLMComplete();
    }

    // Clean up payload
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    if( complete )
    {
        delete complete;
    }
}


void AGENT_FRAME::OnLLMStreamError( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT: OnLLMStreamError called" );

    // Get error data
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    std::string errorMessage = complete ? complete->error_message : "Unknown error";

    // Forward to controller - it will emit EVT_CHAT_ERROR
    // UI updates are handled by OnChatError
    if( m_chatController )
    {
        m_chatController->HandleLLMError( errorMessage );
    }

    // Clean up payload
    if( complete )
    {
        delete complete;
    }
}

void AGENT_FRAME::DoNewChat()
{
    wxLogInfo( "AGENT_FRAME::DoNewChat called" );

    bool isBusy = m_chatController ? m_chatController->IsBusy()
                                   : ( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE );
    if( isBusy )
    {
        wxMessageBox( _( "Please wait for the current response to complete before starting a new chat." ),
                      _( "Chat in Progress" ), wxOK | wxICON_INFORMATION );
        return;
    }

    if( m_chatController )
        m_chatController->NewChat();

    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();

    // UI reset - m_fullHtmlContent is now just chat area inner HTML (no template wrapper)
    m_fullHtmlContent = "";
    SetHtml( m_fullHtmlContent );
    m_chatHistoryDb.StartNewConversation();
    m_bridge->PushChatTitle( "New Chat" );

    // Clear historical thinking state
    m_historicalThinking.clear();
    m_historicalThinkingExpanded.clear();
    m_historicalToolResultExpanded.clear();
    m_currentThinkingIndex = -1;
    m_toolResultCounter = 0;
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;
}

void AGENT_FRAME::LoadConversation( const std::string& aConversationId )
{
    wxLogInfo( "AGENT_FRAME::LoadConversation called with id: %s", aConversationId.c_str() );
    // Delegate to controller - it will emit EVT_CHAT_HISTORY_LOADED
    // which triggers OnChatHistoryLoaded for UI updates
    if( m_chatController )
    {
        m_chatController->LoadChat( aConversationId );
    }
}


void AGENT_FRAME::RenderChatHistory()
{
    // Clear historical thinking storage
    m_historicalThinking.clear();

    // Counter for tool result toggle indices
    int toolResultIndex = 0;

    // Build HTML from chat history (inner content only, no template wrapper)
    m_fullHtmlContent = "";

    for( const auto& msg : m_chatHistory )
    {
        if( !msg.contains( "role" ) || !msg.contains( "content" ) )
            continue;

        std::string role = msg["role"];

        // Content can be string or array (tool use)
        if( msg["content"].is_string() )
        {
            std::string content = msg["content"];
            wxString display = content;

            if( role == "user" )
            {
                // Right-aligned speech bubble style for user messages
                display.Replace( "&", "&amp;" );
                display.Replace( "<", "&lt;" );
                display.Replace( ">", "&gt;" );
                display.Replace( "\n", "<br>" );
                m_fullHtmlContent += wxString::Format(
                    "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
                    display );
            }
            else if( role == "assistant" )
            {
                // Left-aligned markdown formatted response
                m_fullHtmlContent += AgentMarkdown::ToHtml( content );
                m_fullHtmlContent += "<br>";
            }
        }
        else if( msg["content"].is_array() )
        {
            // Check for user messages with file attachments - render as one combined bubble
            if( role == "user" )
            {
                bool hasAttachments = false;
                for( const auto& block : msg["content"] )
                {
                    std::string blockType = block.value( "type", "" );
                    if( blockType == "image" || blockType == "document" )
                    {
                        hasAttachments = true;
                        break;
                    }
                }

                if( hasAttachments )
                {
                    wxString bubble = FileAttach::BuildHistoryBubbleHtml(
                            msg["content"] );
                    if( !bubble.IsEmpty() )
                        m_fullHtmlContent += bubble;

                    continue;  // Skip block-level iteration for this message
                }
            }

            // Iterate through content blocks and render each one
            for( const auto& block : msg["content"] )
            {
                if( !block.contains( "type" ) )
                    continue;

                std::string blockType = block["type"];

                if( blockType == "text" )
                {
                    // Render text block
                    std::string text = block.value( "text", "" );
                    wxString display = text;

                    if( role == "assistant" )
                    {
                        // Left-aligned markdown formatted response
                        m_fullHtmlContent += AgentMarkdown::ToHtml( display );
                        m_fullHtmlContent += "<br>";
                    }
                    else if( role == "user" )
                    {
                        // Right-aligned speech bubble style for user messages
                        display.Replace( "&", "&amp;" );
                        display.Replace( "<", "&lt;" );
                        display.Replace( ">", "&gt;" );
                        display.Replace( "\n", "<br>" );
                        m_fullHtmlContent += wxString::Format(
                            "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s</div></div>",
                            display );
                    }
                }
                else if( blockType == "thinking" )
                {
                    // Render thinking block with toggle support
                    std::string thinking = block.value( "thinking", "" );
                    if( !thinking.empty() )
                    {
                        int thinkingIndex = m_historicalThinking.size();
                        wxString thinkingText = wxString::FromUTF8( thinking );

                        // Store the raw thinking content for later toggle
                        m_historicalThinking.push_back( thinkingText );

                        // Check if this thinking block is expanded
                        bool expanded = m_historicalThinkingExpanded.count( thinkingIndex ) > 0;

                        // Escape HTML for display
                        wxString escapedText = thinkingText;
                        escapedText.Replace( "&", "&amp;" );
                        escapedText.Replace( "<", "&lt;" );
                        escapedText.Replace( ">", "&gt;" );
                        escapedText.Replace( "\n", "<br>" );

                        // Always render both toggle link and content (content hidden by CSS)
                        // JavaScript will toggle visibility without page reload
                        wxString expandedClass = expanded ? " expanded" : "";
                        wxString displayStyle = expanded ? "block" : "none";

                        m_fullHtmlContent += wxString::Format(
                            "<div class=\"mb-1\">"
                            "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline hover:underline\">Thinking</a>"
                            "<div class=\"thinking-content text-[#606060] mt-1 mb-0 pl-3 border-l-2 border-[#404040] whitespace-pre-wrap%s\" data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\">%s</div>"
                            "</div>",
                            thinkingIndex, expandedClass, thinkingIndex, displayStyle, escapedText );
                    }
                }
                else if( blockType == "tool_use" )
                {
                    // Render tool_use block with human-readable description
                    std::string toolName = block.value( "name", "unknown" );
                    nlohmann::json toolInput = block.value( "input", nlohmann::json::object() );
                    wxString desc = AgentTools::GetToolDescription( toolName, toolInput );

                    // Store for pairing with result (next block)
                    m_lastToolDesc = desc;
                }
                else if( blockType == "tool_result" )
                {
                    // Render collapsible tool call + result block
                    bool isError = block.value( "is_error", false );

                    wxString statusClass;
                    wxString statusText;

                    // Extract text content - handle both string and array formats
                    std::string textContent;
                    wxString imageHtml;

                    if( block.contains( "content" ) && block["content"].is_array() )
                    {
                        for( const auto& inner : block["content"] )
                        {
                            std::string innerType = inner.value( "type", "" );

                            if( innerType == "text" )
                            {
                                textContent += inner.value( "text", "" );
                            }
                            else if( innerType == "image" && inner.contains( "source" ) )
                            {
                                std::string imgData = inner["source"].value( "data", "" );
                                std::string mediaType = inner["source"].value(
                                        "media_type", "image/png" );

                                if( imgData != "__stripped__" && !imgData.empty() )
                                {
                                    imageHtml += "<br><img src=\"data:"
                                        + wxString::FromUTF8( mediaType )
                                        + ";base64,"
                                        + wxString::FromUTF8( imgData )
                                        + "\" style=\"max-width:100%; border-radius:6px; "
                                          "margin:8px 0;\" />";
                                }
                                else
                                {
                                    imageHtml += "<br><i>(Screenshot from previous "
                                                 "session)</i>";
                                }
                            }
                        }
                    }
                    else
                    {
                        textContent = block.value( "content", "" );
                    }

                    // Determine status and preview
                    bool isPythonError = ( textContent.find( "Traceback" )
                                           != std::string::npos );

                    if( isPythonError )
                    {
                        statusClass = "text-accent-red";
                        statusText = "Error";
                    }
                    else if( isError )
                    {
                        statusClass = "text-accent-red";
                        statusText = "Failed";
                    }
                    else
                    {
                        statusClass = "text-accent-green";
                        statusText = "Completed";
                    }

                    // Format full result for expanded view
                    wxString fullFormatted = FormatToolResult( textContent );

                    wxString desc = m_lastToolDesc.IsEmpty() ? "Tool execution" : m_lastToolDesc;
                    bool expanded = m_historicalToolResultExpanded.count( toolResultIndex ) > 0;

                    m_fullHtmlContent += BuildToolResultHtml( toolResultIndex, desc,
                                                             statusClass, statusText,
                                                             fullFormatted,
                                                             imageHtml, expanded );
                    toolResultIndex++;
                    m_lastToolDesc = "";
                }
            }
        }
    }

    // Include any pending tool call UI (e.g., open editor approval)
    if( !m_toolCallHtml.IsEmpty() )
    {
        m_fullHtmlContent += m_toolCallHtml;
    }

    SetHtml( m_fullHtmlContent );
}

// ============================================================================
// Authentication Methods
// ============================================================================

void AGENT_FRAME::UpdateAuthUI()
{
    bool authenticated = m_auth && m_auth->IsAuthenticated();
    m_bridge->PushAuthState( authenticated );
}

bool AGENT_FRAME::CheckAuthentication()
{
    if( m_auth )
    {
        return m_auth->IsAuthenticated();
    }
    return true; // No auth configured, allow access
}

void AGENT_FRAME::OnSize( wxSizeEvent& aEvent )
{
    aEvent.Skip();
}


// ============================================================================
// Agent Change Approval Methods
// ============================================================================

void AGENT_FRAME::QueryPendingChanges()
{
    wxLogInfo( "AGENT_FRAME::QueryPendingChanges" );

    m_pendingSchSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.Clear();

    // Query schematic editor for pending changes
    KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
    if( schPlayer )
    {
        std::string response;
        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_HAS_CHANGES, response );

        try
        {
            nlohmann::json j = nlohmann::json::parse( response );
            bool hasChanges = j.value( "has_changes", false );

            if( hasChanges && j.contains( "affected_sheets" ) && j["affected_sheets"].is_array() )
            {
                for( const auto& sheet : j["affected_sheets"] )
                {
                    if( sheet.is_string() )
                    {
                        wxString sheetPath = wxString::FromUTF8( sheet.get<std::string>() );
                        if( !sheetPath.IsEmpty() )
                            m_pendingSchSheets.insert( sheetPath );
                    }
                }
            }
        }
        catch( const std::exception& e )
        {
            wxLogInfo( "AGENT_FRAME: Failed to parse schematic response: %s", e.what() );
        }
    }

    // Query PCB editor for pending changes
    KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
    if( pcbPlayer )
    {
        std::string response;
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_HAS_CHANGES, response );
        m_hasPcbChanges = ( response == "true" );

        if( m_hasPcbChanges )
            m_pcbFilename = "PCB";
    }

    // Build JSON data for the webview pending changes panel
    bool hasAnyChanges = !m_pendingSchSheets.empty() || m_hasPcbChanges;

    nlohmann::json data;
    nlohmann::json sheets = nlohmann::json::array();
    for( const auto& sheet : m_pendingSchSheets )
        sheets.push_back( sheet.ToStdString() );
    data["sheets"] = sheets;
    data["has_pcb"] = m_hasPcbChanges;

    m_bridge->PushPendingChanges( data );
}


void AGENT_FRAME::OnSchematicChangeHandled( bool aAccepted )
{
    AppendHtml( aAccepted ? "<p><i>Schematic changes accepted.</i></p>"
                          : "<p><i>Schematic changes rejected.</i></p>" );
    QueryPendingChanges();
}


void AGENT_FRAME::OnPcbChangeHandled( bool aAccepted )
{
    AppendHtml( aAccepted ? "<p><i>PCB changes accepted.</i></p>"
                          : "<p><i>PCB changes rejected.</i></p>" );
    QueryPendingChanges();
}


//=============================================================================
// Concurrent Editing Support
//=============================================================================

void AGENT_FRAME::SetAgentTargetSheet( const KIID& aSheetId, const wxString& aSheetName )
{
    m_agentWorkspace.SetTargetSheet( aSheetId );
}


void AGENT_FRAME::BeginAgentTransaction()
{
    if( m_agentWorkspace.BeginTransaction() )
    {
        wxLogInfo( "Agent transaction started" );

        // Set up conflict callback
        m_agentWorkspace.SetConflictCallback(
            [this]( const KIID& aItemId, const CONFLICT_INFO& aInfo )
            {
                OnConflictDetected( aItemId, aInfo );
            } );
    }
}


void AGENT_FRAME::EndAgentTransaction( bool aCommit )
{
    if( m_agentWorkspace.EndTransaction( aCommit ) )
    {
        if( aCommit )
        {
            wxLogInfo( "Agent transaction committed - pending approval" );
            // Changes are now staged and waiting for user approval
            // The pending changes panel will show them
        }
        else
        {
            wxLogInfo( "Agent transaction reverted" );
        }
    }
}


void AGENT_FRAME::OnConflictDetected( const KIID& aItemId, const CONFLICT_INFO& aInfo )
{
    wxLogInfo( "Conflict detected for item %s: %s",
                aItemId.AsString(), aInfo.m_propertyName );

    // Update the conflict display in the pending changes panel
    UpdateConflictDisplay();

    // Optionally show a notification to the user
    AppendHtml( wxString::Format(
        "<p style='color: #FFA500;'><b>Conflict:</b> You modified item %s which the agent was also editing.</p>",
        aItemId.AsString() ) );
}


void AGENT_FRAME::OnConflictResolved( const KIID& aItemId, CONFLICT_RESOLUTION aResolution )
{
    m_agentWorkspace.ResolveConflict( aItemId, aResolution );
    UpdateConflictDisplay();

    wxString resolutionStr;
    switch( aResolution )
    {
    case CONFLICT_RESOLUTION::KEEP_USER:   resolutionStr = "kept your version"; break;
    case CONFLICT_RESOLUTION::KEEP_AGENT:  resolutionStr = "kept agent's version"; break;
    case CONFLICT_RESOLUTION::AUTO_MERGE:  resolutionStr = "merged both changes"; break;
    default:                               resolutionStr = "resolved manually"; break;
    }

    AppendHtml( wxString::Format(
        "<p><i>Conflict for %s: %s.</i></p>",
        aItemId.AsString(), resolutionStr ) );
}


void AGENT_FRAME::UpdateConflictDisplay()
{
    // Conflicts are now handled via the diff overlay in each editor
    // The pending changes panel just lists sheets with changes
}


//=============================================================================
// Editor Open Approval
//=============================================================================

void AGENT_FRAME::ShowOpenEditorApproval( const wxString& aEditorType )
{
    int idx = m_activeToolResultIdx;
    if( idx < 0 )
        return;

    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Open editor" ) : m_lastToolDesc;
    wxString approvalHtml = BuildToolApprovalHtml( idx, desc,
                                                    "Open", "agent:approve_open",
                                                    "#1a3d1a", "#4ade80" );

    // Replace the running tool box with the approval box in internal HTML
    m_fullHtmlContent.Replace( m_activeRunningHtml, approvalHtml );
    m_htmlBeforeAgentResponse.Replace( m_activeRunningHtml, approvalHtml );
    m_activeRunningHtml = approvalHtml;

    SetHtml( m_fullHtmlContent );
}


void AGENT_FRAME::ShowCloseEditorApproval( const wxString& aEditorType )
{
    int idx = m_activeToolResultIdx;
    if( idx < 0 )
        return;

    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Close editor" ) : m_lastToolDesc;
    wxString approvalHtml = BuildToolApprovalHtml( idx, desc,
                                                    "Close", "agent:approve_close",
                                                    "#3d1a1a", "#f87171" );

    m_fullHtmlContent.Replace( m_activeRunningHtml, approvalHtml );
    m_htmlBeforeAgentResponse.Replace( m_activeRunningHtml, approvalHtml );
    m_activeRunningHtml = approvalHtml;

    SetHtml( m_fullHtmlContent );
}


void AGENT_FRAME::OnApproveOpenEditor()
{
    wxLogInfo( "AGENT_FRAME::OnApproveOpenEditor called" );

    // Validate that we still have a pending request with a valid tool ID
    if( m_pendingOpenToolId.empty() )
    {
        wxLogWarning( "OnApproveOpenEditor: empty tool ID - ignoring stale click" );
        return;
    }

    if( !m_pendingOpenSch && !m_pendingOpenPcb )
    {
        wxLogWarning( "OnApproveOpenEditor: no pending editor type - ignoring stale click" );
        m_pendingOpenToolId.clear();
        return;
    }

    // Validate that the controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( m_pendingOpenToolId ) )
    {
        wxLogWarning( "OnApproveOpenEditor: tool %s no longer pending - ignoring stale click",
                      m_pendingOpenToolId.c_str() );
        m_pendingOpenSch = false;
        m_pendingOpenPcb = false;
        m_pendingOpenToolId.clear();
        m_pendingOpenFilePath.Clear();
        return;
    }

    // --- Original logic ---
    bool success = false;
    wxString editorName;

    if( m_pendingOpenSch )
    {
        editorName = "Schematic";
        success = DoOpenEditor( FRAME_SCH );
    }
    else if( m_pendingOpenPcb )
    {
        editorName = "PCB";
        success = DoOpenEditor( FRAME_PCB_EDITOR );
    }

    std::string result = success
        ? editorName.ToStdString() + " editor opened successfully"
        : "Failed to open " + editorName.ToStdString() + " editor";

    std::string toolId = m_pendingOpenToolId;
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();

    if( m_chatController )
        m_chatController->HandleToolResult( toolId, result, success );
}


void AGENT_FRAME::OnRejectOpenEditor()
{
    wxLogInfo( "AGENT_FRAME::OnRejectOpenEditor called" );

    // Validate that we still have a pending request
    if( m_pendingOpenToolId.empty() )
    {
        wxLogWarning( "OnRejectOpenEditor: empty tool ID - ignoring stale click" );
        return;
    }

    wxString editorName = m_pendingOpenSch ? "Schematic" : "PCB";
    std::string toolId = m_pendingOpenToolId;

    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();

    // Validate controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( toolId ) )
    {
        wxLogWarning( "OnRejectOpenEditor: tool %s no longer pending", toolId.c_str() );
        return;
    }

    if( m_chatController )
        m_chatController->HandleToolResult( toolId,
            "User declined to open " + editorName.ToStdString() + " editor", false );
}


void AGENT_FRAME::OnApproveCloseEditor()
{
    wxLogInfo( "AGENT_FRAME::OnApproveCloseEditor called" );

    if( m_pendingCloseToolId.empty() )
    {
        wxLogWarning( "OnApproveCloseEditor: empty tool ID - ignoring stale click" );
        return;
    }

    if( !m_pendingCloseSch && !m_pendingClosePcb )
    {
        wxLogWarning( "OnApproveCloseEditor: no pending editor type" );
        m_pendingCloseToolId.clear();
        return;
    }

    if( m_chatController && !m_chatController->HasPendingTool( m_pendingCloseToolId ) )
    {
        wxLogWarning( "OnApproveCloseEditor: tool %s no longer pending",
                      m_pendingCloseToolId.c_str() );
        m_pendingCloseSch = false;
        m_pendingClosePcb = false;
        m_pendingCloseToolId.clear();
        return;
    }

    FRAME_T frameType = m_pendingCloseSch ? FRAME_SCH : FRAME_PCB_EDITOR;
    wxString editorLabel = m_pendingCloseSch ? "Schematic" : "PCB";
    bool saveFirst = m_pendingCloseSaveFirst;

    KIWAY_PLAYER* player = Kiway().Player( frameType, false );
    if( player && player->IsShown() )
    {
        player->Close( !saveFirst );

        if( frameType == FRAME_SCH )
            TOOL_REGISTRY::Instance().SetSchematicEditorOpen( false );
        else if( frameType == FRAME_PCB_EDITOR )
            TOOL_REGISTRY::Instance().SetPcbEditorOpen( false );
    }

    std::string result = editorLabel.ToStdString() + " editor closed"
                         + ( saveFirst ? " (saved)" : "" );
    std::string toolId = m_pendingCloseToolId;

    m_pendingCloseSch = false;
    m_pendingClosePcb = false;
    m_pendingCloseToolId.clear();

    if( m_chatController )
        m_chatController->HandleToolResult( toolId, result, true );
}


bool AGENT_FRAME::DoOpenEditor( FRAME_T aFrameType )
{
    wxString editorName = ( aFrameType == FRAME_SCH ) ? "Schematic" : "PCB";

    wxLogInfo( "DoOpenEditor: Opening %s editor, pendingFilePath='%s'",
               editorName, m_pendingOpenFilePath );

    KIWAY_PLAYER* player = Kiway().Player( aFrameType, true );
    if( !player )
    {
        wxLogError( "DoOpenEditor: Failed to create %s player", editorName );
        return false;
    }

    // Open specific file if path was provided
    if( !m_pendingOpenFilePath.IsEmpty() )
    {
        wxLogInfo( "DoOpenEditor: Loading file '%s'", m_pendingOpenFilePath );

        std::vector<wxString> files;
        files.push_back( m_pendingOpenFilePath );
        bool loadResult = player->OpenProjectFiles( files );

        if( !loadResult )
        {
            wxLogWarning( "DoOpenEditor: OpenProjectFiles returned false for '%s'",
                          m_pendingOpenFilePath );
        }
        else
        {
            wxLogInfo( "DoOpenEditor: Successfully loaded '%s'", m_pendingOpenFilePath );
        }

        m_pendingOpenFilePath.Clear();
    }
    else
    {
        wxLogWarning( "DoOpenEditor: No file path specified - editor will open with blank/default document" );
    }

    player->Show( true );
    if( player->IsIconized() )
        player->Iconize( false );
    player->Raise();

    // Notify tool registry that editor is now open
    // This blocks direct file modifications to prevent IPC/file conflicts
    if( aFrameType == FRAME_SCH )
        TOOL_REGISTRY::Instance().SetSchematicEditorOpen( true );
    else if( aFrameType == FRAME_PCB_EDITOR )
        TOOL_REGISTRY::Instance().SetPcbEditorOpen( true );

    return true;
}


// ============================================================================
// Controller Event Handlers
// These handle events emitted by CHAT_CONTROLLER
// ============================================================================

void AGENT_FRAME::OnChatTextDelta( wxThreadEvent& aEvent )
{
    ChatTextDeltaData* data = aEvent.GetPayload<ChatTextDeltaData*>();
    if( !data )
        return;

    // Markdown text is now streaming - hide the waiting dots
    m_isStreamingMarkdown = true;

    // Controller owns the response - UpdateAgentResponse reads from controller

    // Re-render full response with markdown
    UpdateAgentResponse();

    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatThinkingStart( wxThreadEvent& aEvent )
{
    ChatThinkingStartData* data = aEvent.GetPayload<ChatThinkingStartData*>();

    // If there's existing thinking content from a previous block (e.g., before a tool call),
    // preserve it in history before starting the new block. This handles race conditions
    // where THINKING_START arrives before WAITING_FOR_LLM state change is processed.
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
        if( m_thinkingExpanded )
            m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
    }

    // Initialize thinking state for new block
    m_isThinking = true;
    m_thinkingContent = "";
    m_thinkingExpanded = false;

    // Set index for this thinking block using m_historicalThinking.size()
    // After the push above (if any), this gives us the correct next index
    m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );

    // Rebuild thinking HTML and immediately flush to DOM
    // This bypasses the timer to minimize delay before thinking link is clickable
    RebuildThinkingHtml();
    FlushStreamingContentUpdate();  // Immediate flush, don't wait for timer

    if( data )
        delete data;
}


void AGENT_FRAME::OnChatThinkingDelta( wxThreadEvent& aEvent )
{
    ChatThinkingDeltaData* data = aEvent.GetPayload<ChatThinkingDeltaData*>();
    if( !data )
        return;

    // Update thinking state (m_isThinking should already be true from THINKING_START)
    m_isThinking = true;
    m_thinkingContent = data->fullThinking;

    // Rebuild thinking HTML and trigger update via timer
    // The thinking content is included directly in BuildStreamingContent()
    // and will be updated on the next timer tick (max 50ms delay)
    RebuildThinkingHtml();
    UpdateAgentResponse();

    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatThinkingDone( wxThreadEvent& aEvent )
{
    ChatThinkingDoneData* data = aEvent.GetPayload<ChatThinkingDoneData*>();

    // Finalize thinking state
    m_isThinking = false;

    // Update content from event if available
    if( data )
    {
        m_thinkingContent = data->finalThinking;
        delete data;
    }

    // Rebuild thinking display (removes loading animation)
    RebuildThinkingHtml();
    UpdateAgentResponse();
}


void AGENT_FRAME::OnChatToolGenerating( wxThreadEvent& aEvent )
{
    ChatToolGeneratingData* data = aEvent.GetPayload<ChatToolGeneratingData*>();
    if( !data )
        return;

    // Reset streaming markdown flag so tool name shows (not hidden by preceding text)
    m_isStreamingMarkdown = false;

    // Store tool name for display in generating animation
    m_generatingToolName = wxString::FromUTF8( data->toolName );

    wxLogInfo( "AGENT_FRAME::OnChatToolGenerating - tool: %s", data->toolName.c_str() );

    // Trigger UI update to show tool name
    UpdateAgentResponse();

    delete data;
}


void AGENT_FRAME::OnChatToolStart( wxThreadEvent& aEvent )
{
    ChatToolStartData* data = aEvent.GetPayload<ChatToolStartData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatToolStart - tool: %s (id=%s)",
            data->toolName.c_str(), data->toolId.c_str() );

    // Clear generating tool name (tool is now executing, not generating)
    m_generatingToolName.Clear();

    // Stop animation and finalize thinking
    StopGeneratingAnimation();
    m_isThinking = false;

    // Flush streaming content to remove the generating box from the DOM
    // before finalization bakes the current content permanently
    FlushStreamingContentUpdate( true );

    // Sync m_fullHtmlContent with clean streaming state.
    {
        wxString streamingContent = BuildStreamingContent();
        wxString fullHtml = m_htmlBeforeAgentResponse;
        fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
        fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
        m_fullHtmlContent = fullHtml;
    }

    // Finalize the current streaming div so the agent's response text stays in place
    m_bridge->PushFinalizeStreaming();

    // Update m_fullHtmlContent to reflect finalized state (no more streaming-content ID)
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // Now clear streaming state in controller (text is baked above)
    if( m_chatController )
        m_chatController->ClearStreamingState();

    // Preserve thinking content to history before clearing (needed for correct index tracking)
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
        if( m_thinkingExpanded )
            m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
    }

    // Clear frame's thinking HTML since it's now part of the base HTML (prevents duplication)
    m_thinkingHtml.Clear();
    m_thinkingContent.Clear();

    // Store tool description for result display
    m_lastToolDesc = wxString::FromUTF8( data->description );

    // Place the tool result component in the permanent DOM for ALL tools.
    // OnChatToolComplete will update the status and populate the body via JS callback.
    {
        int idx = m_toolResultCounter++;
        wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Tool execution" ) : m_lastToolDesc;
        m_activeRunningHtml = BuildRunningToolHtml( idx, desc );
        m_activeToolResultIdx = idx;

        // Append running box to permanent DOM
        AppendHtml( m_activeRunningHtml );

        // Create new streaming div after the running box
        wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
        AppendHtml( streamingDiv );
        m_htmlBeforeAgentResponse = m_fullHtmlContent;
    }

    // Handle open_editor specially - requires user approval only if not already open
    if( data->toolName == "open_editor" )
    {
        std::string editorType = data->input.value( "editor_type", "" );
        FRAME_T frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;
        wxString editorLabel = ( editorType == "sch" ) ? "Schematic" : "PCB";

        // Capture optional file path
        std::string filePath = data->input.value( "file_path", "" );
        m_pendingOpenFilePath.Clear();

        // Validate file path if provided, or auto-detect from project
        wxString projectPath = Kiway().Prj().GetProjectPath();
        wxLogInfo( "open_editor: editor_type='%s', file_path='%s', projectPath='%s'",
                   wxString::FromUTF8( editorType ), wxString::FromUTF8( filePath ), projectPath );

        if( !filePath.empty() )
        {
            auto allowedPaths = GetAllowedPaths();
            bool pathValid = false;
            FileWriter::PathValidationResult pathResult;

            if( allowedPaths.empty() )
            {
                wxLogWarning( "open_editor: No allowed paths available" );
                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        "Error: no project or editor is open", false );
                delete data;
                return;
            }

            for( const auto& allowed : allowedPaths )
            {
                pathResult = FileWriter::ValidatePathInProject( filePath,
                                                                 allowed.ToStdString() );
                if( pathResult.valid )
                {
                    pathValid = true;
                    break;
                }
            }

            if( !pathValid )
            {
                wxLogWarning( "open_editor: Path validation failed: %s", pathResult.error );
                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        "Error: " + pathResult.error, false );
                delete data;
                return;
            }

            m_pendingOpenFilePath = wxString::FromUTF8( pathResult.resolvedPath );
            wxLogInfo( "open_editor: Validated file_path -> '%s'", m_pendingOpenFilePath );
        }
        else
        {
            // No file path provided - auto-detect project's default file
            wxString projectName = Kiway().Prj().GetProjectName();
            wxLogInfo( "open_editor: Auto-detect - projectPath='%s', projectName='%s'",
                       projectPath, projectName );

            if( !projectName.IsEmpty() && !projectPath.IsEmpty() )
            {
                wxString defaultFile;
                if( editorType == "sch" )
                    defaultFile = projectPath + projectName + ".kicad_sch";
                else
                    defaultFile = projectPath + projectName + ".kicad_pcb";

                wxLogInfo( "open_editor: Checking for default file: %s", defaultFile );

                if( wxFileExists( defaultFile ) )
                {
                    m_pendingOpenFilePath = defaultFile;
                    wxLogInfo( "open_editor: Auto-detected project file: %s", defaultFile );
                }
                else
                {
                    wxLogWarning( "open_editor: Default file does not exist: %s", defaultFile );
                }
            }
            else
            {
                wxLogWarning( "open_editor: Cannot auto-detect - project info not available" );
            }
        }

        // Check if editor is already open (false = don't create if not existing)
        KIWAY_PLAYER* existingPlayer = Kiway().Player( frameType, false );
        if( existingPlayer && existingPlayer->IsShown() )
        {
            // Check if we need to load a different file
            if( !m_pendingOpenFilePath.IsEmpty() )
            {
                // Get current file from the editor
                wxString currentFile = existingPlayer->GetCurrentFileName();
                wxFileName currentFn( currentFile );
                wxFileName requestedFn( m_pendingOpenFilePath );

                // Normalize paths for comparison
                currentFn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG );
                requestedFn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG );

                wxLogInfo( "open_editor: Current file='%s', Requested file='%s'",
                           currentFn.GetFullPath(), requestedFn.GetFullPath() );

                // Force close and reload when:
                // 1. Editor has untitled document (currentFile.IsEmpty()) - need to load the requested file
                // 2. Editor has different file open (paths don't match)
                if( currentFile.IsEmpty() || currentFn.GetFullPath() != requestedFn.GetFullPath() )
                {
                    wxLogInfo( "open_editor: %s - closing editor to load '%s'",
                               currentFile.IsEmpty() ? "Editor has untitled document" : "Different file open",
                               requestedFn.GetFullPath() );

                    // Close the editor to force a fresh load
                    // Note: This will discard any unsaved changes in the old file
                    // TODO: Consider adding a save-before-close mechanism
                    existingPlayer->Close( true );

                    // Now reopen the editor with the correct file
                    // Don't show approval dialog since user already approved opening the editor
                    KIWAY_PLAYER* newPlayer = Kiway().Player( frameType, true );
                    if( newPlayer )
                    {
                        std::vector<wxString> files;
                        files.push_back( m_pendingOpenFilePath );
                        newPlayer->OpenProjectFiles( files );
                        newPlayer->Show( true );
                        newPlayer->Raise();

                        wxString openedFile = m_pendingOpenFilePath;
                        m_pendingOpenFilePath.Clear();

                        if( m_chatController )
                            m_chatController->HandleToolResult( data->toolId,
                                editorLabel.ToStdString() + " editor reloaded with file: " + openedFile.ToStdString(), true );
                    }
                    else
                    {
                        m_pendingOpenFilePath.Clear();
                        if( m_chatController )
                            m_chatController->HandleToolResult( data->toolId,
                                "Error: Failed to reopen " + editorLabel.ToStdString() + " editor", false );
                    }

                    delete data;
                    return;
                }
                else
                {
                    // Same file already open - just focus the editor
                    wxLogInfo( "open_editor: File '%s' already open, focusing editor",
                               currentFn.GetFullPath() );

                    if( existingPlayer->IsIconized() )
                        existingPlayer->Iconize( false );
                    existingPlayer->Raise();

                    wxString openedFile = m_pendingOpenFilePath;
                    m_pendingOpenFilePath.Clear();

                    if( m_chatController )
                        m_chatController->HandleToolResult( data->toolId,
                            editorLabel.ToStdString() + " editor already has file open: " + openedFile.ToStdString(), true );

                    delete data;
                    return;
                }
            }
            else
            {
                // No file path - just focus existing editor
                if( existingPlayer->IsIconized() )
                    existingPlayer->Iconize( false );
                existingPlayer->Raise();

                if( m_chatController )
                    m_chatController->HandleToolResult( data->toolId,
                        editorLabel.ToStdString() + " editor is already open", true );

                delete data;
                return;
            }
        }

        // Editor not open - store pending request and show approval dialog
        m_pendingOpenSch = ( editorType == "sch" );
        m_pendingOpenPcb = ( editorType == "pcb" );
        m_pendingOpenToolId = data->toolId;

        ShowOpenEditorApproval( editorLabel );

        delete data;
        return;
    }

    // Handle check_status - returns project and editor state
    if( data->toolName == "check_status" )
    {
        nlohmann::json status;

        // Project info
        wxString projectPath = Kiway().Prj().GetProjectPath();
        status["project_path"] = projectPath.ToStdString();

        // Check which editors are open
        KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
        KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

        status["schematic_editor_open"] = ( schEditor && schEditor->IsShown() );
        status["pcb_editor_open"] = ( pcbEditor && pcbEditor->IsShown() );

        // Add project file paths
        wxString prjPath = Kiway().Prj().GetProjectPath();
        if( !prjPath.empty() )
        {
            wxString prjName = Kiway().Prj().GetProjectName();
            status["schematic_file"] = ( prjPath + prjName + ".kicad_sch" ).ToStdString();
            status["pcb_file"] = ( prjPath + prjName + ".kicad_pcb" ).ToStdString();
        }

        // Add files currently open in editors
        auto openFiles = GetOpenEditorFiles();
        if( !openFiles.empty() )
        {
            nlohmann::json arr = nlohmann::json::array();
            for( const auto& f : openFiles )
                arr.push_back( f.ToStdString() );
            status["open_editor_files"] = arr;
        }

        // Get current sheet if schematic is open (via IPC would be more accurate, but this is fast)
        status["current_sheet"] = ""; // Would need IPC call for sch.sheets.get_current()

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, status.dump( 2 ), true );

        delete data;
        return;
    }

    // Handle close_editor - requires user approval before closing
    if( data->toolName == "close_editor" )
    {
        std::string editorType = data->input.value( "editor_type", "" );
        bool saveFirst = data->input.value( "save_first", true );
        FRAME_T frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;
        wxString editorLabel = ( editorType == "sch" ) ? "Schematic" : "PCB";

        KIWAY_PLAYER* player = Kiway().Player( frameType, false );
        if( !player || !player->IsShown() )
        {
            // Editor not open - return success immediately (no popup needed)
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    editorLabel.ToStdString() + " editor is not open", true );
            delete data;
            return;
        }

        // Store pending request and show approval dialog
        m_pendingCloseSch = ( editorType == "sch" );
        m_pendingClosePcb = ( editorType == "pcb" );
        m_pendingCloseToolId = data->toolId;
        m_pendingCloseSaveFirst = saveFirst;

        ShowCloseEditorApproval( editorLabel );

        delete data;
        return;
    }

    // Handle save - save current documents
    // Note: Saving is better done via IPC tools which have direct access to the editor APIs.
    // This is a simplified version that just reports status.
    if( data->toolName == "save" )
    {
        std::string editorType = data->input.value( "editor_type", "all" );
        nlohmann::json result;
        result["status"] = "info";
        result["message"] = "Use IPC tools for saving. Schematic: sch.save(), PCB: pcb.save()";

        std::vector<std::string> openEditors;
        KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
        KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

        if( schEditor && schEditor->IsShown() )
            openEditors.push_back( "schematic" );
        if( pcbEditor && pcbEditor->IsShown() )
            openEditors.push_back( "pcb" );

        result["open_editors"] = openEditors;

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, result.dump( 2 ), true );

        delete data;
        return;
    }

    // Handle create_project - create new KiCad project
    if( data->toolName == "create_project" )
    {
        std::string projectName = data->input.value( "project_name", "" );
        std::string directory = data->input.value( "directory", "" );

        if( projectName.empty() || directory.empty() )
        {
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    "Error: project_name and directory are required", false );
            delete data;
            return;
        }

        // Create project directory
        wxString projDir = wxString::FromUTF8( directory ) + wxFileName::GetPathSeparator() +
                           wxString::FromUTF8( projectName );

        if( !wxDir::Make( projDir, wxS_DIR_DEFAULT ) && !wxDir::Exists( projDir ) )
        {
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId,
                    "Error: Could not create project directory: " + projDir.ToStdString(), false );
            delete data;
            return;
        }

        wxString basePath = projDir + wxFileName::GetPathSeparator() + wxString::FromUTF8( projectName );

        // Create minimal .kicad_pro file
        wxString proFile = basePath + ".kicad_pro";
        {
            wxFile f( proFile, wxFile::write );
            if( f.IsOpened() )
            {
                nlohmann::json proJson = {
                    { "meta", { { "filename", projectName + ".kicad_pro" }, { "version", 1 } } },
                    { "schematic", { { "legacy_lib_dir", "" }, { "legacy_lib_list", nlohmann::json::array() } } }
                };
                f.Write( wxString::FromUTF8( proJson.dump( 2 ) ) );
            }
        }

        // Create minimal .kicad_sch file
        wxString schFile = basePath + ".kicad_sch";
        {
            wxFile f( schFile, wxFile::write );
            if( f.IsOpened() )
            {
                f.Write(
                    "(kicad_sch\n"
                    "  (version 20250114)\n"
                    "  (generator \"zener_agent\")\n"
                    "  (generator_version \"1.0\")\n"
                    "  (uuid \"" + KIID().AsStdString() + "\")\n"
                    "  (paper \"A4\")\n"
                    "  (lib_symbols)\n"
                    "  (sheet_instances\n"
                    "    (path \"/\" (page \"\"))\n"
                    "  )\n"
                    ")\n"
                );
            }
        }

        // Create minimal .kicad_pcb file
        wxString pcbFile = basePath + ".kicad_pcb";
        {
            wxFile f( pcbFile, wxFile::write );
            if( f.IsOpened() )
            {
                f.Write(
                    "(kicad_pcb\n"
                    "  (version 20250114)\n"
                    "  (generator \"zener_agent\")\n"
                    "  (generator_version \"1.0\")\n"
                    "  (general\n"
                    "    (thickness 1.6)\n"
                    "    (legacy_teardrops no)\n"
                    "  )\n"
                    "  (paper \"A4\")\n"
                    "  (layers\n"
                    "    (0 \"F.Cu\" signal)\n"
                    "    (31 \"B.Cu\" signal)\n"
                    "    (32 \"B.Adhes\" user \"B.Adhesive\")\n"
                    "    (33 \"F.Adhes\" user \"F.Adhesive\")\n"
                    "    (34 \"B.Paste\" user)\n"
                    "    (35 \"F.Paste\" user)\n"
                    "    (36 \"B.SilkS\" user \"B.Silkscreen\")\n"
                    "    (37 \"F.SilkS\" user \"F.Silkscreen\")\n"
                    "    (38 \"B.Mask\" user)\n"
                    "    (39 \"F.Mask\" user)\n"
                    "    (40 \"Dwgs.User\" user \"User.Drawings\")\n"
                    "    (44 \"Edge.Cuts\" user)\n"
                    "  )\n"
                    "  (setup\n"
                    "    (pad_to_mask_clearance 0)\n"
                    "  )\n"
                    ")\n"
                );
            }
        }

        nlohmann::json result = {
            { "status", "success" },
            { "project_path", projDir.ToStdString() },
            { "files_created", {
                projectName + ".kicad_pro",
                projectName + ".kicad_sch",
                projectName + ".kicad_pcb"
            }}
        };

        if( m_chatController )
            m_chatController->HandleToolResult( data->toolId, result.dump( 2 ), true );

        delete data;
        return;
    }

    // Tool result lives outside streaming div - keep m_toolCallHtml clear
    m_toolCallHtml.Clear();

    delete data;
}


void AGENT_FRAME::OnChatToolComplete( wxThreadEvent& aEvent )
{
    ChatToolCompleteData* data = aEvent.GetPayload<ChatToolCompleteData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatToolComplete - tool: %s, success: %s",
            data->toolName.c_str(), data->success ? "true" : "false" );

    // Determine status display
    wxString statusClass;
    wxString statusText;

    if( data->isPythonError )
    {
        statusClass = "text-accent-red";
        statusText = "Error";
    }
    else if( !data->success )
    {
        statusClass = "text-accent-red";
        statusText = "Failed";
    }
    else
    {
        statusClass = "text-accent-green";
        statusText = "Completed";
    }

    // Format full result for expanded view
    wxString fullFormatted = FormatToolResult( data->result );

    // Build image HTML if present
    wxString imageHtml;

    if( data->hasImage && !data->imageBase64.empty() )
    {
        imageHtml = "<br><img src=\"data:"
            + wxString::FromUTF8( data->imageMediaType )
            + ";base64,"
            + wxString::FromUTF8( data->imageBase64 )
            + "\" style=\"max-width:100%; border-radius:6px; margin:8px 0;\" />";
    }

    // Update the existing tool result component via JS callback
    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Tool execution" ) : m_lastToolDesc;
    int idx = m_activeToolResultIdx;

    // Build the text-only body content (no image - image is appended separately to avoid
    // passing megabytes of base64 data in a single JS string literal)
    wxString textBody = wxString::Format(
        "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap "
        "break-words m-0 mt-2\">%s</pre>",
        fullFormatted );

    // Update status and text body in the existing DOM element via bridge
    m_bridge->PushToolResultUpdate( idx, statusClass, statusText, textBody );

    // Append image via chunked data URI (avoids multi-MB JS string literals)
    if( data->hasImage && !data->imageBase64.empty() )
    {
        wxString prefix = "data:" + wxString::FromUTF8( data->imageMediaType )
            + ";base64,";
        m_bridge->PushToolResultImageBegin( idx, prefix );

        // Send base64 data in ~100KB chunks
        const wxString b64 = wxString::FromUTF8( data->imageBase64 );
        const size_t chunkSize = 100000;

        for( size_t i = 0; i < b64.length(); i += chunkSize )
        {
            wxString chunk = b64.Mid( i, chunkSize );
            m_bridge->PushToolResultImageChunk( chunk );
        }

        m_bridge->PushToolResultImageEnd( idx );
    }

    // Update internal HTML tracking (replace running HTML with full completed HTML)
    wxString completedHtml = BuildToolResultHtml( idx, desc, statusClass, statusText,
                                                  fullFormatted, imageHtml, false );

    if( !m_activeRunningHtml.IsEmpty() )
    {
        m_fullHtmlContent.Replace( m_activeRunningHtml, completedHtml );
        m_htmlBeforeAgentResponse.Replace( m_activeRunningHtml, completedHtml );
    }
    m_activeRunningHtml.Clear();

    // After schematic tools complete successfully, trigger editor refresh for live UI feedback
    if( data->success && data->toolName == "sch_modify" )
    {
        try
        {
            nlohmann::json resultJson = nlohmann::json::parse( data->result );
            if( resultJson.value( "success", false ) )
            {
                std::string filePath = resultJson.value( "file", "" );
                if( !filePath.empty() )
                {
                    // Tell schematic editor to reload this file and refresh display
                    Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_REFRESH, filePath );
                }
            }
        }
        catch( ... )
        {
            // JSON parse failed - tool result may not be in expected format, skip refresh
        }
    }

    // Check for pending approval
    QueryPendingChanges();

    // Auto-follow on tool complete if tracking is active
    if( m_isTrackingAgent && data->success )
    {
        // Send to both editors - each will check if it has changes to show
        std::string emptyPayload;
        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_VIEW_CHANGES, emptyPayload );
        Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_VIEW_CHANGES, emptyPayload );
    }

    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnChatTurnComplete( wxThreadEvent& aEvent )
{
    ChatTurnCompleteData* data = aEvent.GetPayload<ChatTurnCompleteData*>();
    if( !data )
        return;

    bool continuing = data->continuing;
    wxLogInfo( "AGENT_FRAME::OnChatTurnComplete - turn completed (continuing=%d)", continuing );

    // If not continuing, stop animation and update button
    if( !continuing )
    {
        StopGeneratingAnimation();
        m_bridge->PushActionButtonState( "Send" );
    }

    // Finalize thinking state
    m_isThinking = false;

    // Sync history from controller (controller added the assistant message)
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        if( !continuing )
        {
            m_chatHistoryDb.Save( m_chatHistory );
        }
    }

    // Preserve thinking content for index tracking (so next message gets index+1)
    if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
    {
        m_historicalThinking.push_back( m_thinkingContent );
    }

    // Preserve thinking expansion state
    if( m_thinkingExpanded && m_currentThinkingIndex >= 0 )
        m_historicalThinkingExpanded.insert( m_currentThinkingIndex );

    // Finalize the streaming content div - remove its ID so future streams use a fresh div
    m_bridge->PushFinalizeStreaming();
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // If continuing, add a new streaming div for the next response
    if( continuing )
    {
        wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
        AppendHtml( streamingDiv );
    }

    // Clear streaming UI state (content is already in DOM via streaming updates)
    m_currentResponse.clear();
    m_thinkingContent.Clear();
    m_toolCallHtml.Clear();
    m_thinkingExpanded = false;
    m_currentThinkingIndex = -1;
    // NOTE: Don't reset m_toolResultCounter here - old tool-result-N IDs persist in the
    // DOM (no re-render). Counter must be monotonically increasing to avoid duplicate IDs.
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    // NOTE: Don't call RenderChatHistory() here - content is already in DOM from streaming.
    // RenderChatHistory() is only for loading saved conversations from disk.
    // Calling it here would cause a full SetPage() reload which jerks scroll position.

    delete data;
}


void AGENT_FRAME::OnChatError( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnChatError - error received" );
    ChatErrorData* data = aEvent.GetPayload<ChatErrorData*>();
    if( !data )
        return;

    // Display error message
    wxString errorHtml = wxString::Format(
        "<p><font color='red'><b>Error:</b> %s</font></p>",
        wxString::FromUTF8( data->message ) );
    AppendHtml( errorHtml );

    // Stop all animations and reset button
    StopGeneratingAnimation();
    m_isCompacting = false;
    m_bridge->PushActionButtonState( "Send" );

    // Clear streaming state
    m_isThinking = false;

    // Sync history from controller - it may have removed orphaned user messages
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Sync frame's state machine with controller (now IDLE after error recovery)
    m_conversationCtx.SetState( AgentConversationState::IDLE );

    delete data;
}


void AGENT_FRAME::OnChatStateChanged( wxThreadEvent& aEvent )
{
    ChatStateChangedData* data = aEvent.GetPayload<ChatStateChangedData*>();
    if( !data )
        return;

    // Update button based on new state
    AgentConversationState newState = static_cast<AgentConversationState>( data->newState );

    switch( newState )
    {
    case AgentConversationState::IDLE:
    case AgentConversationState::ERROR:
        m_bridge->PushActionButtonState( "Send" );
        break;

    case AgentConversationState::WAITING_FOR_LLM:
        m_bridge->PushActionButtonState( "Stop" );

        // If continuing after tool completion, bake in tool result before starting new stream
        // This ensures the tool call result appears BEFORE any new response text
        if( static_cast<AgentConversationState>( data->oldState ) ==
            AgentConversationState::PROCESSING_TOOL_RESULT )
        {
            // IMMEDIATE render before clearing (don't wait for timer)
            // Build the streaming content that should be baked in
            wxString streamingContent;
            if( !m_thinkingHtml.IsEmpty() )
                streamingContent += m_thinkingHtml;
            std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";
            streamingContent += AgentMarkdown::ToHtml( currentResponse );
            if( !m_toolCallHtml.IsEmpty() )
                streamingContent += m_toolCallHtml;

            // Send immediate DOM update
            if( !streamingContent.IsEmpty() )
                m_bridge->PushStreamingContent( streamingContent );

            // Update m_fullHtmlContent to match the DOM state
            wxString fullHtml = m_htmlBeforeAgentResponse;
            fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
            fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
            m_fullHtmlContent = fullHtml;

            // Finalize this streaming div (tool result now "baked in")
            m_bridge->PushFinalizeStreaming();
            m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

            // Add fresh streaming div for next content
            wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
            AppendHtml( streamingDiv );
            m_htmlBeforeAgentResponse = m_fullHtmlContent;

            // Preserve thinking content in history before clearing
            // This ensures the next thinking block gets a new index
            if( !m_thinkingContent.IsEmpty() && m_currentThinkingIndex >= 0 )
            {
                m_historicalThinking.push_back( m_thinkingContent );
                // Preserve expansion state too
                if( m_thinkingExpanded )
                    m_historicalThinkingExpanded.insert( m_currentThinkingIndex );
            }

            // NOW safe to clear - content is baked into DOM
            m_toolCallHtml.Clear();
            m_thinkingHtml.Clear();
            m_thinkingContent.Clear();
            m_thinkingExpanded = false;
            if( m_chatController )
                m_chatController->ClearStreamingState();
            m_htmlUpdateNeeded = false;

            // Update thinking index so next thinking block gets a new index
            // Now m_historicalThinking.size() reflects the pushed content
            m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );

            // Tool result counter continues across turns within a conversation
            // (indices are baked into the DOM, counter just needs to be monotonically increasing)
        }

        StartGeneratingAnimation();
        break;

    case AgentConversationState::TOOL_USE_DETECTED:
    case AgentConversationState::EXECUTING_TOOL:
    case AgentConversationState::PROCESSING_TOOL_RESULT:
        // Keep current button state during tool execution
        break;
    }

    delete data;
}


void AGENT_FRAME::OnChatTitleDelta( wxThreadEvent& aEvent )
{
    ChatTitleDeltaData* data = aEvent.GetPayload<ChatTitleDeltaData*>();
    if( !data )
        return;

    // Update title display with partial text (streaming animation)
    m_bridge->PushChatTitle( wxString::FromUTF8( data->partialTitle ) );

    delete data;
}


void AGENT_FRAME::OnChatTitleGenerated( wxThreadEvent& aEvent )
{
    ChatTitleGeneratedData* data = aEvent.GetPayload<ChatTitleGeneratedData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatTitleGenerated - title: %s", data->title.c_str() );
    m_bridge->PushChatTitle( wxString::FromUTF8( data->title ) );

    // Update persistence with the new title
    m_chatHistoryDb.SetTitle( data->title );

    if( m_chatController )
    {
        m_chatHistoryDb.Save( m_chatController->GetChatHistory() );
    }

    delete data;
}


void AGENT_FRAME::OnChatHistoryLoaded( wxThreadEvent& aEvent )
{
    ChatHistoryLoadedData* data = aEvent.GetPayload<ChatHistoryLoadedData*>();
    if( !data )
        return;

    wxLogInfo( "AGENT_FRAME::OnChatHistoryLoaded - chatId: %s, title: %s",
            data->chatId.c_str(), data->title.c_str() );

    // Hide history dropdown
    m_bridge->PushHistoryShow( false );

    // Update chat title
    std::string title = data->title;
    if( title.empty() )
        title = "Untitled Chat";
    m_bridge->PushChatTitle( wxString::FromUTF8( title ) );

    // Sync history from controller
    if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Clear historical thinking toggle state for new history
    m_historicalThinkingExpanded.clear();
    m_historicalToolResultExpanded.clear();
    m_currentThinkingIndex = -1;
    m_toolResultCounter = 0;
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    // Render the loaded chat history
    RenderChatHistory();

    // Update DB ID so new messages go to this history
    m_chatHistoryDb.SetConversationId( data->chatId );

    // Auto-scroll to bottom handled by CSS flex-direction: column-reverse
    m_userScrolledUp = false;

    delete data;
}


#ifdef __APPLE__
static std::vector<wxString> GetExternalEditorFiles()
{
    std::vector<wxString> files;
    std::set<std::string> seen;
    pid_t myPid = getpid();

    int bufSize = proc_listpids( PROC_ALL_PIDS, 0, NULL, 0 );

    if( bufSize <= 0 )
        return files;

    int maxPids = bufSize / sizeof( pid_t );
    std::vector<pid_t> pids( maxPids );
    bufSize = proc_listpids( PROC_ALL_PIDS, 0, pids.data(), bufSize );
    int nPids = bufSize / sizeof( pid_t );

    for( int i = 0; i < nPids; i++ )
    {
        if( pids[i] == 0 || pids[i] == myPid )
            continue;

        char pathbuf[PROC_PIDPATHINFO_MAXSIZE];

        if( proc_pidpath( pids[i], pathbuf, sizeof( pathbuf ) ) <= 0 )
            continue;

        std::string procPath( pathbuf );

        bool isSch = ( procPath.find( "eeschema" ) != std::string::npos );
        bool isPcb = ( procPath.find( "pcbnew" ) != std::string::npos );

        if( !isSch && !isPcb )
            continue;

        wxLogInfo( "AGENT: Found external editor process: PID %d (%s)", pids[i], procPath );

        // Get the process's current working directory — eeschema/pcbnew set cwd
        // to the directory of the file being edited
        struct proc_vnodepathinfo vnodePathInfo;
        int sz = proc_pidinfo( pids[i], PROC_PIDVNODEPATHINFO, 0,
                               &vnodePathInfo, sizeof( vnodePathInfo ) );

        if( sz <= 0 )
            continue;

        wxString cwd = wxString::FromUTF8( vnodePathInfo.pvi_cdir.vip_path );

        if( cwd.IsEmpty() )
            continue;

        wxLogInfo( "AGENT: External editor cwd: %s", cwd );

        // Scan the cwd for KiCad files matching this editor type
        wxString ext = isSch ? "*.kicad_sch" : "*.kicad_pcb";
        wxDir dir( cwd );

        if( dir.IsOpened() )
        {
            wxString filename;
            bool cont = dir.GetFirst( &filename, ext, wxDIR_FILES );

            while( cont )
            {
                wxString fullPath = cwd + wxFileName::GetPathSeparator() + filename;
                std::string fp = fullPath.ToStdString();

                if( seen.insert( fp ).second )
                {
                    wxLogInfo( "AGENT: External editor file: %s", fp );
                    files.push_back( fullPath );
                }

                cont = dir.GetNext( &filename );
            }
        }
    }

    return files;
}
#endif


std::vector<wxString> AGENT_FRAME::GetOpenEditorFiles()
{
    std::vector<wxString> files;

    // Editors within our KIWAY (same process)
    for( FRAME_T ft : { FRAME_SCH, FRAME_PCB_EDITOR } )
    {
        KIWAY_PLAYER* player = Kiway().Player( ft, false );

        if( player && player->IsShown() )
        {
            wxString f = player->GetCurrentFileName();

            if( !f.IsEmpty() )
                files.push_back( f );
        }
    }

    // Standalone editor processes (eeschema/pcbnew launched outside this KIWAY)
#ifdef __APPLE__
    for( const auto& f : GetExternalEditorFiles() )
        files.push_back( f );
#endif

    for( const auto& f : files )
        wxLogInfo( "AGENT: Open editor file: %s", f );

    return files;
}


std::vector<wxString> AGENT_FRAME::GetAllowedPaths()
{
    std::vector<wxString> paths;

    // KiCad documents root
    wxString docsPath;

    if( wxGetEnv( wxT( "KICAD_DOCUMENTS_HOME" ), &docsPath ) )
    {
        paths.push_back( docsPath );
    }
    else
    {
        wxFileName kicadDocs;
        kicadDocs.AssignDir( KIPLATFORM::ENV::GetDocumentsPath() );
        kicadDocs.AppendDir( KICAD_PATH_STR );
        paths.push_back( kicadDocs.GetPath() );
    }

    // Active project directory
    wxString projPath = Kiway().Prj().GetProjectPath();

    if( !projPath.IsEmpty() )
        paths.push_back( projPath );

    // Directories of files open in editors
    for( const auto& f : GetOpenEditorFiles() )
    {
        wxFileName fn( f );
        paths.push_back( fn.GetPath() );
    }

    wxString pathList;
    for( const auto& p : paths )
    {
        if( !pathList.IsEmpty() )
            pathList += ", ";
        pathList += p;
    }
    wxLogInfo( "AGENT: GetAllowedPaths: [%s]", pathList );

    return paths;
}
