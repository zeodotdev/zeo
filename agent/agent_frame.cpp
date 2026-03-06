#include "agent_frame.h"
#include "agent_chat_history.h"
#include <zeo/agent_auth.h>
#include "cloud/agent_cloud_sync.h"
#include "bridge/webview_bridge.h"
#include "view/agent_markdown.h"
#include "view/unified_html_template.h"
#include "tools/tool_schemas.h"
#include "core/chat_controller.h"
#include "core/chat_events.h"
#include "tools/tool_registry.h"
#include "tools/handlers/check_status_handler.h"
#include "tools/handlers/open_editor_handler.h"
#include <kiway_mail.h>
#include <mail_type.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <wx/log.h>
#include <kiway.h>
#include <paths.h>
#include <kiplatform/environment.h>
#include <frame_type.h>
#include <sstream>
#include <fstream>
#include <set>
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
#include <zeo/zeo_constants.h>
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
#include "macos_webview_bg.h"
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
        "<div "
        "class=\"tool-result-header p-3 px-3 flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\">%s</span>"
        "<span class=\"tool-status text-text-muted text-[12px] ml-auto\"><i>Running...</i></span>"
        "</div>"
        "<div class=\"tool-result-body p-3 pt-0 border-t border-border-dark\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
        "</div>"
        "</div>",
        aIndex, aDesc, aIndex );
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
        "<div class=\"tool-result-body p-3 pt-0 border-t border-border-dark\" "
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
        m_hasPcbChanges( false ),
        m_pendingOpenSch( false ),
        m_pendingOpenPcb( false )
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

    // Detect system theme and apply it to the HTML template
    wxString htmlContent = GetUnifiedHtmlTemplate();
#ifdef __APPLE__
    if( !IsSystemDarkMode() )
    {
        // Apply light theme by adding 'light' class to the html element
        htmlContent.Replace( wxS( "<html class=\"h-full\">" ),
                             wxS( "<html class=\"h-full light\">" ) );
    }
#endif

    m_webView->SetPage( htmlContent );
    mainSizer->Add( m_webView, 1, wxEXPAND );

#ifdef __APPLE__
    // WKWebView handles Cmd+key through the Cocoa responder chain, bypassing wxWidgets.
    // Install an NSEvent monitor to intercept shortcuts when the webview has focus.
    void* nativeHandle = (void*) m_webView->GetWebView()->GetHandle();

    if( nativeHandle )
    {
        // Disable WKWebView's default white background so parent dark panel shows through
        SetWebViewDarkBackground( nativeHandle );

        InstallKeyboardMonitor(
                nativeHandle,
                [this]( KEY_SHORTCUT aShortcut )
                {
                    switch( aShortcut )
                    {
                    case KEY_SHORTCUT::SELECT_ALL:
                        m_webView->RunScriptAsync(
                                wxS( "var ta = document.getElementById('input-textarea');"
                                     "if(ta) { ta.focus(); ta.select(); }" ) );
                        break;

                    case KEY_SHORTCUT::UNDO:
                        m_webView->RunScriptAsync( wxS( "App.Input.undo()" ) );
                        break;

                    case KEY_SHORTCUT::REDO:
                        m_webView->RunScriptAsync( wxS( "App.Input.redo()" ) );
                        break;

                    case KEY_SHORTCUT::FOCUS_INPUT:
                        m_webView->RunScriptAsync( wxS( "App.Input.focus()" ) );
                        break;

                    case KEY_SHORTCUT::STOP_GENERATING:
                        if( m_isGenerating )
                        {
                            m_webView->RunScriptAsync(
                                    wxS( "if(App.Search.isOpen()){App.Search.close()}"
                                         "else{App.Bridge.sendMsg('stop_click')}" ) );
                        }
                        else
                        {
                            m_webView->RunScriptAsync( wxS( "App.Search.close()" ) );
                        }
                        break;

                    case KEY_SHORTCUT::NEW_CHAT:
                        DoNewChat();
                        break;

                    case KEY_SHORTCUT::SEARCH_CHAT:
                        m_webView->RunScriptAsync( wxS( "App.Search.open()" ) );
                        break;
                    }
                } );
    }
#endif

    // ACCELERATOR TABLE for keyboard shortcuts
    wxAcceleratorEntry entries[5];
    entries[0].Set( wxACCEL_CTRL, (int) 'C', ID_CHAT_COPY );
    entries[1].Set( wxACCEL_CTRL, (int) 'N', ID_CHAT_NEW );
    entries[2].Set( wxACCEL_CTRL, (int) 'L', ID_CHAT_FOCUS );
    entries[3].Set( wxACCEL_CTRL, (int) 'F', ID_CHAT_SEARCH );
    entries[4].Set( wxACCEL_NORMAL, WXK_ESCAPE, ID_CHAT_ESCAPE );
    wxAcceleratorTable accel( 5, entries );
    SetAcceleratorTable( accel );

    SetSizer( mainSizer );
    Layout();
    SetSize( 500, 600 );

    // Bind menu & accelerator events
    Bind( wxEVT_MENU, &AGENT_FRAME::OnPopupClick, this, ID_CHAT_COPY );

    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { DoNewChat(); }, ID_CHAT_NEW );

    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) {
        m_webView->RunScriptAsync( wxS( "App.Input.focus()" ) );
    }, ID_CHAT_FOCUS );

    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) {
        m_webView->RunScriptAsync( wxS( "App.Search.open()" ) );
    }, ID_CHAT_SEARCH );

    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) {
        if( m_isGenerating )
        {
            m_webView->RunScriptAsync(
                    wxS( "if(App.Search.isOpen()){App.Search.close()}"
                         "else{App.Bridge.sendMsg('stop_click')}" ) );
        }
        else
        {
            m_webView->RunScriptAsync( wxS( "App.Search.close()" ) );
        }
    }, ID_CHAT_ESCAPE );

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
    m_thinkingHtmlDirty = false;
    m_currentThinkingIndex = -1;

    // Initialize tool result counters
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
    LoadAndSetSystemPrompt();
    InitializeTools();

    // Auth will be set via MAIL_AUTH_POINTER from the launcher (shared instance).
    // If the launcher doesn't provide one (rare fallback), EnsureAuth() creates a local one.
    m_auth = nullptr;
    m_ownsAuth = false;

    // Wire auth to LLM client (nullptr for now, updated when auth pointer arrives)
    m_llmClient->SetAuth( nullptr );

    // Cloud sync (configured when auth pointer arrives)
    m_cloudSync = std::make_unique<AGENT_CLOUD_SYNC>();

    // Create chat controller
    m_chatController = std::make_unique<CHAT_CONTROLLER>( this );
    m_chatController->SetLLMClient( m_llmClient.get() );
    m_chatController->SetChatHistoryDb( &m_chatHistoryDb );
    m_chatController->SetAuth( nullptr );
    m_chatController->SetCloudSync( m_cloudSync.get() );
    m_chatController->SetKiwayRequestFn(
        [this]( int aFrameType, const std::string& aPayload ) -> std::string {
            return SendRequest( aFrameType, aPayload );
        } );
    m_chatController->SetProjectPathFn(
        [this]() -> std::string {
            wxString projectPath = Kiway().Prj().GetProjectPath();
            if( projectPath.IsEmpty() )
                return "";

            // Sync KIWAY state to registry so BuildStatusJson() has fresh data
            auto& reg = TOOL_REGISTRY::Instance();
            reg.SetProjectPath( projectPath.ToStdString() );
            reg.SetProjectName( Kiway().Prj().GetProjectName().ToStdString() );

            KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
            KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );
            reg.SetSchematicEditorOpen( schEditor && schEditor->IsShown() );
            reg.SetPcbEditorOpen( pcbEditor && pcbEditor->IsShown() );

            std::vector<std::string> openFiles;
            for( const auto& f : GetOpenEditorFiles() )
                openFiles.push_back( f.ToStdString() );
            reg.SetOpenEditorFiles( std::move( openFiles ) );

            return CHECK_STATUS_HANDLER::BuildStatusJson();
        } );

    // Schematic summary callback for user edit detection between turns.
    // Returns a JSON snapshot of all symbols and labels for diffing.
    m_chatController->SetSchematicSummaryFn(
        [this]() -> std::string {
            // Build a lightweight Python script that dumps schematic state as JSON
            std::string pyCode =
                "import json\n"
                "r = {\"symbols\": {}, \"labels\": {}, \"wire_count\": 0}\n"
                "try:\n"
                "    if hasattr(sch, 'refresh_document'):\n"
                "        sch.refresh_document()\n"
                "    for sym in sch.symbols.get_all():\n"
                "        uid = str(sym.id.value) if hasattr(sym, 'id') else ''\n"
                "        pos = sym.position\n"
                "        r['symbols'][uid] = {\n"
                "            'ref': getattr(sym, 'reference', '?'),\n"
                "            'val': getattr(sym, 'value', ''),\n"
                "            'lib': str(getattr(sym, 'lib_id', '')),\n"
                "            'x': round(pos.x / 1e6, 2),\n"
                "            'y': round(pos.y / 1e6, 2),\n"
                "            'ang': getattr(sym, 'angle', 0)\n"
                "        }\n"
                "    for lbl in sch.labels.get_all():\n"
                "        uid = str(lbl.id.value) if hasattr(lbl, 'id') else ''\n"
                "        pos = lbl.position\n"
                "        r['labels'][uid] = {\n"
                "            'name': getattr(lbl, 'text', getattr(lbl, 'name', '')),\n"
                "            'x': round(pos.x / 1e6, 2),\n"
                "            'y': round(pos.y / 1e6, 2)\n"
                "        }\n"
                "    try:\n"
                "        r['wire_count'] = len(sch.crud.get_wires())\n"
                "    except:\n"
                "        pass\n"
                "except:\n"
                "    pass\n"
                "print(json.dumps(r))\n";

            std::string result = SendRequest( FRAME_TERMINAL, "run_shell sch " + pyCode );

            // Validate result is JSON (not an error message)
            if( result.empty() || result[0] != '{' )
                return "";

            return result;
        } );

    // Callback for symbol generator to reload a symbol library after writing to disk
    {
        auto& reg = TOOL_REGISTRY::Instance();
        reg.SetReloadSymbolLibFn(
            [this]( const std::string& aLibName ) {
                // Step 1: Register the library in the project sym-lib-table if it doesn't
                // exist yet (handles first-time generation of project.kicad_sym).
                // MAIL_ADD_LOCAL_LIB checks HasRow before inserting, so it's safe to send
                // unconditionally — it's a no-op for already-registered libraries except that
                // it also calls manager.LoadProjectTables and adapter->LoadOne for new entries.
                wxString    projectPath = Prj().GetProjectPath();
                wxFileName  libFile( projectPath,
                                     wxString::FromUTF8( aLibName ) + wxS( ".kicad_sym" ) );

                if( libFile.FileExists() )
                {
                    wxLogInfo( "TOOL_REGISTRY: Registering/reloading symbol library '%s' via "
                               "MAIL_ADD_LOCAL_LIB",
                               aLibName.c_str() );
                    // Payload format: srcProjDir\nfilePath\n  (same as import_proj.cpp)
                    std::string addPayload = projectPath.ToUTF8().data();
                    addPayload += '\n';
                    addPayload += libFile.GetFullPath().ToUTF8().data();
                    addPayload += '\n';
                    Kiway().ExpressMail( FRAME_SCH, MAIL_ADD_LOCAL_LIB, addPayload );
                }

                // Step 2: Refresh the plugin cache so newly-written symbols become visible.
                // MAIL_RELOAD_LIB now calls LoadProjectTables before LoadOne, ensuring the
                // library is found in the in-memory table even if Step 1 was a no-op.
                wxLogInfo( "TOOL_REGISTRY: Reloading symbol library '%s' via MAIL_RELOAD_LIB",
                           aLibName.c_str() );
                std::string reloadPayload = aLibName;
                Kiway().ExpressMail( FRAME_SCH, MAIL_RELOAD_LIB, reloadPayload );
            } );
    }

    // Callback for footprint generator to reload a footprint library after writing to disk
    {
        auto& reg = TOOL_REGISTRY::Instance();
        reg.SetReloadFootprintLibFn(
            [this]( const std::string& aLibName ) {
                wxLogInfo( "TOOL_REGISTRY: Reloading footprint library '%s' via MAIL_RELOAD_LIB",
                           aLibName.c_str() );
                std::string payload = aLibName;
                Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_RELOAD_LIB, payload );
            } );
    }

    // Sync editor + project state to TOOL_REGISTRY before each tool execution
    m_chatController->SetEditorStateSyncFn(
        [this]() {
            KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
            KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

            bool schOpen = schEditor && schEditor->IsShown();
            bool pcbOpen = pcbEditor && pcbEditor->IsShown();

            auto& reg = TOOL_REGISTRY::Instance();
            reg.SetSchematicEditorOpen( schOpen );
            reg.SetPcbEditorOpen( pcbOpen );
            reg.SetProjectPath( Kiway().Prj().GetProjectPath().ToStdString() );
            reg.SetProjectName( Kiway().Prj().GetProjectName().ToStdString() );

            std::vector<std::string> openFiles;
            for( const auto& f : GetOpenEditorFiles() )
                openFiles.push_back( f.ToStdString() );
            reg.SetOpenEditorFiles( std::move( openFiles ) );
        } );

    m_chatController->SetHasQueuedMessageFn(
        [this]() { return HasQueuedMessage(); } );

    // Load persisted model preference (default to Claude 4.6 Opus)
    m_currentModel = LoadModelPreference();
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
    Bind( EVT_CHAT_COMPACTION, &AGENT_FRAME::OnChatCompaction, this );

    // Bind async tool execution completion event (for background tools like autorouter)
    Bind( EVT_TOOL_EXECUTION_COMPLETE, &AGENT_FRAME::OnAsyncToolComplete, this );

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
        std::vector<std::string> models = { "Claude 4.6 Opus", "Gemini 3.1 Pro" };
        m_bridge->PushModelList( models, m_currentModel );

        // Push initial title
        m_bridge->PushChatTitle( "New Chat" );

        // Push plan mode state
        m_bridge->PushPlanMode( m_agentMode == AgentMode::PLAN );
    } );
}

AGENT_FRAME::~AGENT_FRAME()
{
#ifdef __APPLE__
    RemoveKeyboardMonitor();
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

    if( m_ownsAuth )
        delete m_auth;
}

void AGENT_FRAME::ShowChangedLanguage()
{
    KIWAY_PLAYER::ShowChangedLanguage();
}

void AGENT_FRAME::AppendHtml( const wxString& aHtml )
{
    // Insert before queued bubble so it stays at the bottom
    if( !m_queuedBubbleHtml.IsEmpty() )
    {
        size_t pos = m_fullHtmlContent.Find( m_queuedBubbleHtml );
        if( pos != wxString::npos )
        {
            m_fullHtmlContent.insert( pos, aHtml );
            m_bridge->PushAppendChat( aHtml );
            return;
        }
    }

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
    wxString displayContent = escapedContent.IsEmpty() ? wxString( "<i>Thinking...</i>" ) : escapedContent;

    // Always render both toggle link and content (content hidden by CSS if collapsed)
    // JavaScript will toggle visibility without page reload
    wxString expandedClass = m_thinkingExpanded ? " expanded" : "";
    wxString displayStyle = m_thinkingExpanded ? "block" : "none";

    wxString thinkingText = "Thinking";

    m_thinkingHtml = wxString::Format(
        "<div class=\"mb-1\">"
        "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline thinking-link\">%s</a>"
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

    // Rebuild thinking HTML lazily if content changed since last build
    if( m_thinkingHtmlDirty )
    {
        RebuildThinkingHtml();
        m_thinkingHtmlDirty = false;
    }

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
        // Preserve queued bubble at the end
        if( !m_queuedBubbleHtml.IsEmpty() )
            fullHtml += m_queuedBubbleHtml;
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
    m_htmlUpdateNeeded = false;

    // Always push a final content update so the dots are cleared from the DOM
    wxString streamingContent = BuildStreamingContent();
    m_bridge->PushStreamingContent( streamingContent );

    // Update full HTML content for consistency
    wxString html = m_htmlBeforeAgentResponse;
    html.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
    html += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
    // Preserve queued bubble at the end
    if( !m_queuedBubbleHtml.IsEmpty() )
        html += m_queuedBubbleHtml;
    m_fullHtmlContent = html;

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

void AGENT_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_RESPONSE )
    {
        // All tool execution goes through the controller's synchronous SendRequest path,
        // which polls m_toolResponse. Store the KIWAY response for SendRequest() to pick up.
        m_toolResponse = aEvent.GetPayload();
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
    else if( aEvent.Command() == MAIL_AUTH_POINTER )
    {
        // Receive shared AGENT_AUTH pointer from the launcher's SESSION_MANAGER.
        // This eliminates dual auth instances and the refresh token rotation race.
        std::string payload = aEvent.GetPayload();

        if( !payload.empty() )
        {
            AGENT_AUTH* sharedAuth = reinterpret_cast<AGENT_AUTH*>(
                    std::stoull( payload ) );

            if( sharedAuth )
            {
                // If we previously created a fallback auth, clean it up
                if( m_ownsAuth && m_auth )
                    delete m_auth;

                m_auth = sharedAuth;
                m_ownsAuth = false;

                // Wire auth to tool registry (for datasheet extraction)
                TOOL_REGISTRY::Instance().SetAuth( m_auth );

                // Wire the shared auth to LLM client and controller
                m_llmClient->SetAuth( m_auth );

                if( m_chatController )
                    m_chatController->SetAuth( m_auth );

                // Wire auth to cloud sync and start initial sync
                ConfigureCloudSync();

                UpdateAuthUI();
                wxLogTrace( "Agent", "Using shared auth from launcher" );
            }
        }
    }
    else if( aEvent.Command() == MAIL_AUTH_STATE_CHANGED )
    {
        // Auth state changed in the shared instance (sign-in, sign-out, refresh).
        // Tokens are already updated — just refresh the UI.
        UpdateAuthUI();

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
    else if( aEvent.Command() == MAIL_MCP_EXECUTE_AGENT_TOOL )
    {
        // MCP tool execution for C++ handler tools (routed from api_handler_project).
        // Dispatches directly to TOOL_REGISTRY::Execute() — no chat/UI involvement.
        std::string payload = aEvent.GetPayload();

        wxLogMessage( "AGENT_FRAME: MAIL_MCP_EXECUTE_AGENT_TOOL received, payload_len=%zu",
                      payload.length() );

        // Sync Kiway state to TOOL_REGISTRY (same as SetEditorStateSyncFn in chat flow)
        {
            KIWAY_PLAYER* schEditor = Kiway().Player( FRAME_SCH, false );
            KIWAY_PLAYER* pcbEditor = Kiway().Player( FRAME_PCB_EDITOR, false );

            auto& reg = TOOL_REGISTRY::Instance();
            reg.SetSchematicEditorOpen( schEditor && schEditor->IsShown() );
            reg.SetPcbEditorOpen( pcbEditor && pcbEditor->IsShown() );
            reg.SetProjectPath( Kiway().Prj().GetProjectPath().ToStdString() );
            reg.SetProjectName( Kiway().Prj().GetProjectName().ToStdString() );

            std::vector<std::string> openFiles;
            for( const auto& f : GetOpenEditorFiles() )
                openFiles.push_back( f.ToStdString() );
            reg.SetOpenEditorFiles( std::move( openFiles ) );
        }

        try
        {
            auto payloadJson = nlohmann::json::parse( payload );
            std::string toolName = payloadJson.value( "tool_name", "" );
            std::string toolArgsJson = payloadJson.value( "tool_args_json", "" );

            wxLogMessage( "AGENT_FRAME: MCP executing agent tool '%s'", toolName );

            nlohmann::json toolInput;

            if( !toolArgsJson.empty() )
                toolInput = nlohmann::json::parse( toolArgsJson );
            else
                toolInput = nlohmann::json::object();

            std::string result = TOOL_REGISTRY::Instance().Execute( toolName, toolInput );

            wxLogMessage( "AGENT_FRAME: MCP agent tool '%s' completed, result_len=%zu",
                          toolName, result.length() );

            aEvent.SetPayload( result );
        }
        catch( const std::exception& e )
        {
            wxLogError( "AGENT_FRAME: MCP agent tool execution failed: %s", e.what() );

            nlohmann::json errorJson;
            errorJson["status"] = "error";
            errorJson["message"] = std::string( "Agent tool execution failed: " ) + e.what();
            aEvent.SetPayload( errorJson.dump() );
        }
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
        m_bridge->PushInputAppendText( "@{" + label + "} " );
    }
}


void AGENT_FRAME::OnSend( wxCommandEvent& aEvent )
{
    try
    {
    wxLogInfo( "AGENT_FRAME::OnSend called" );
    // NOTE: This method still uses legacy code because it handles KiCad-specific requirements
    // (authentication, pending editor state, system prompt with schematic/PCB context, KIWAY
    // target sheet reset) that the controller doesn't currently support.
    // System prompt is now handled server-side.

    // Hide plan approval button when user sends a new message
    m_bridge->PushRemovePlanApproval();

    // If already generating: queue, cancel+send, or stop depending on context
    if( m_isGenerating )
    {
        bool hasContent = !m_pendingInputText.IsEmpty() || !m_pendingAttachments.empty();

        if( !hasContent )
        {
            // Empty input → stop (also clears queue)
            ClearQueuedMessage();
            OnStop( aEvent );
            return;
        }

        // Streaming / tool execution: queue the message.
        QueueMessage();
        return;
    }

    // Auto-reject pending open/close editor request if user sends a new message.
    // This runs outside m_isGenerating because OnChatToolStart calls
    // StopGeneratingAnimation() which sets m_isGenerating = false.
    bool hasApproval = m_pendingOpenSch || m_pendingOpenPcb;

    if( hasApproval )
    {
        ClearQueuedMessage();
        DoCancelOperation( false );
        // m_pendingInputText still has the user's text — fall through to normal send.
    }

    // Check authentication first
    if( !CheckAuthentication() )
    {
        AppendHtml( "<p><i>Please sign in to continue.</i></p>" );
        return;
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
        "<div class=\"flex justify-end my-3\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap break-words\">%s</div></div>",
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
    m_thinkingHtmlDirty = false;
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
        UploadCurrentChat();
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
    catch( const std::exception& e )
    {
        wxLogError( "AGENT_FRAME::OnSend exception: %s", e.what() );
    }
    catch( ... )
    {
        wxLogError( "AGENT_FRAME::OnSend unknown exception" );
    }
}

void AGENT_FRAME::OnStop( wxCommandEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnStop called" );

    // Re-insert all queued message text back into the input box before clearing.
    // Messages are joined with newlines, prepended to any existing input.
    if( HasQueuedMessage() )
    {
        wxString combined;
        for( const auto& msg : m_queuedMessages )
        {
            if( !msg.text.IsEmpty() )
            {
                if( !combined.IsEmpty() )
                    combined += "\n";
                combined += msg.text;
            }
        }
        if( !combined.IsEmpty() )
        {
            wxLogInfo( "AGENT_FRAME::OnStop - re-inserting %zu queued messages into input",
                       m_queuedMessages.size() );
            m_bridge->PushInputPrependText( combined );
        }
    }

    ClearQueuedMessage();
    DoCancelOperation( true );
}


void AGENT_FRAME::DoCancelOperation( bool aShowStopped )
{
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
        UploadCurrentChat();
    }

    // Clear uncommitted tool calls (haven't been added to history yet)
    if( m_pendingToolCalls.is_array() && !m_pendingToolCalls.empty() )
    {
        m_pendingToolCalls = json::array();
    }

    // Replace active "Running..." tool with "Cancelled" in both DOM and internal HTML
    if( !m_activeRunningHtml.IsEmpty() && m_activeToolResultIdx >= 0 )
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

    // Safety net: cancel ALL remaining "Running..." tool statuses in the DOM.
    // Handles edge cases where queued tool events created "Running..." elements
    // that aren't tracked by m_activeToolResultIdx.
    m_bridge->PushCancelRunningTools();

    // Clear pending editor open/close request state (prevents stale approval button clicks)
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();

    // Take schematic snapshot so user edits before next message are detected
    if( m_chatController )
        m_chatController->TakeSchematicSnapshot();

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
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    if( aShowStopped )
    {
        // NOTE: Don't reset m_toolResultCounter - old tool-result-N IDs persist in
        // m_fullHtmlContent. Counter must stay monotonically increasing to avoid
        // duplicate DOM IDs that cause subsequent tool updates to target stale elements.
        AppendHtml( "<div class=\"text-text-muted mb-1\">Stopped</div>" );
        m_bridge->PushActionButtonState( "Send" );
    }
}

// ── Message Queueing ─────────────────────────────────────────────────────

bool AGENT_FRAME::HasQueuedMessage() const
{
    return !m_queuedMessages.empty();
}


void AGENT_FRAME::QueueMessage()
{
    wxLogInfo( "AGENT_FRAME::QueueMessage - queueing message during generation (queue size=%zu)",
               m_queuedMessages.size() );

    QueuedMessage msg;
    msg.text = m_pendingInputText;
    msg.attachments = std::move( m_pendingAttachments );
    m_pendingInputText.Clear();
    m_pendingAttachments.clear();
    m_queuedMessages.push_back( std::move( msg ) );

    // Rebuild the single combined queued bubble from all queued messages
    RebuildQueuedBubble();

    // Clear input (button stays "Stop" during generation)
    m_bridge->PushInputClear();
}


void AGENT_FRAME::RebuildQueuedBubble()
{
    // Build combined content from all queued messages
    wxString combinedContent;
    for( size_t i = 0; i < m_queuedMessages.size(); i++ )
    {
        const auto& msg = m_queuedMessages[i];

        // Add line break between messages
        if( i > 0 )
            combinedContent += "<br>";

        combinedContent += FileAttach::BuildAttachmentBubbleHtml( msg.attachments );

        wxString escapedText = msg.text;
        escapedText.Replace( "&", "&amp;" );
        escapedText.Replace( "<", "&lt;" );
        escapedText.Replace( ">", "&gt;" );
        escapedText.Replace( "\n", "<br>" );
        combinedContent += escapedText;
    }

    wxString newHtml = wxString::Format(
        "<div id=\"queued-msg\" class=\"queued-msg flex justify-end my-3\" style=\"opacity:0.5;\">"
        "<div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap break-words\">"
        "%s</div></div>",
        combinedContent );

    if( !m_queuedBubbleHtml.IsEmpty() )
    {
        // Replace existing combined bubble
        m_fullHtmlContent.Replace( m_queuedBubbleHtml, newHtml );
        m_bridge->PushReplaceQueuedBubble( newHtml );
    }
    else
    {
        // First queue: append to end
        m_fullHtmlContent += newHtml;
        m_bridge->PushAppendToEnd( newHtml );
    }
    m_queuedBubbleHtml = newHtml;
}


void AGENT_FRAME::SendQueuedMessage()
{
    if( !HasQueuedMessage() )
        return;

    wxLogInfo( "AGENT_FRAME::SendQueuedMessage - sending %zu queued messages as one",
               m_queuedMessages.size() );

    m_turnCompleteForQueue = false;

    // Merge all queued messages into one: join texts with newlines, collect all attachments
    wxString combinedText;
    std::vector<FILE_ATTACHMENT> combinedAttachments;

    for( auto& msg : m_queuedMessages )
    {
        if( !msg.text.IsEmpty() )
        {
            if( !combinedText.IsEmpty() )
                combinedText += "\n";
            combinedText += msg.text;
        }
        for( auto& att : msg.attachments )
            combinedAttachments.push_back( std::move( att ) );
    }
    m_queuedMessages.clear();

    // Finalize the combined queued bubble (remove queued styling, make permanent)
    if( !m_queuedBubbleHtml.IsEmpty() )
    {
        wxString permanentHtml = m_queuedBubbleHtml;
        permanentHtml.Replace( " class=\"queued-msg", " class=\"" );
        permanentHtml.Replace( " style=\"opacity:0.5;\"", "" );
        permanentHtml.Replace( " id=\"queued-msg\"", "" );
        m_fullHtmlContent.Replace( m_queuedBubbleHtml, permanentHtml );
        m_bridge->PushFinalizeFirstQueuedMessage();
        m_queuedBubbleHtml.Clear();
    }

    // Add streaming div
    wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
    m_fullHtmlContent += streamingDiv;
    m_htmlBeforeAgentResponse = m_fullHtmlContent;
    m_bridge->PushAppendChat( streamingDiv );

    // Update UI
    m_bridge->PushActionButtonState( "Stop" );
    m_userScrolledUp = false;

    // Sync/repair controller
    if( m_chatController )
    {
        m_chatController->SetHistory( m_chatHistory );
        m_chatController->RepairHistory();
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Reset streaming state
    m_currentResponse = "";
    m_toolCallHtml = "";
    m_thinkingHtml = "";
    m_thinkingContent = "";
    m_thinkingExpanded = false;
    m_thinkingHtmlDirty = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;
    m_stopRequested = false;
    m_userScrolledUp = false;
    m_htmlUpdatePending = false;

    // State machine
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    m_llmClient->SetModel( m_currentModel );

    // Reset target sheet
    std::string emptyPayload;
    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_RESET_TARGET_SHEET, emptyPayload );

    // Send merged message via controller
    if( m_chatController )
    {
        if( !combinedAttachments.empty() )
        {
            std::vector<CHAT_CONTROLLER::UserAttachment> atts;
            for( const auto& att : combinedAttachments )
                atts.push_back( { att.base64_data, att.media_type } );
            m_chatController->SendMessageWithAttachments( combinedText.ToStdString(), atts );
        }
        else
        {
            m_chatController->SendMessage( combinedText.ToStdString() );
        }
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
        m_chatHistoryDb.Save( m_chatHistory );
        UploadCurrentChat();
    }
}


void AGENT_FRAME::ClearQueuedMessage()
{
    if( !m_queuedBubbleHtml.IsEmpty() )
        m_fullHtmlContent.Replace( m_queuedBubbleHtml, "" );

    if( !m_queuedMessages.empty() )
        m_bridge->PushRemoveAllQueuedMessages();

    m_queuedMessages.clear();
    m_queuedBubbleHtml.Clear();
    m_turnCompleteForQueue = false;
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
    static const wxString IMAGE_EXTS[] = {
        "png", "jpg", "jpeg", "gif", "webp", "bmp"
    };

    wxFileDialog dlg( this, "Attach File", "", "",
                      "Supported files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.pdf)"
                      "|*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.pdf"
                      "|Image files (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp)"
                      "|*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp"
                      "|PDF files (*.pdf)|*.pdf"
                      "|All files (*.*)|*.*",
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE );

    if( dlg.ShowModal() == wxID_OK )
    {
        wxArrayString paths;
        dlg.GetPaths( paths );

        for( const auto& path : paths )
        {
            FILE_ATTACHMENT att;
            bool loaded = false;
            wxString error;

            wxFileName fn( path );
            wxString ext = fn.GetExt().Lower();

            bool isImage = false;
            for( const auto& imgExt : IMAGE_EXTS )
            {
                if( ext == imgExt )
                {
                    isImage = true;
                    break;
                }
            }

            if( ext == "pdf" )
                loaded = FileAttach::LoadFileFromDisk( path, att, &error );
            else if( isImage )
                loaded = FileAttach::LoadImageFromFile( path, att, &error );

            if( loaded )
            {
                m_bridge->PushAddAttachment( wxString::FromUTF8( att.base64_data ),
                                             wxString::FromUTF8( att.media_type ),
                                             wxString::FromUTF8( att.filename ) );
            }
            else if( !error.empty() )
            {
                m_bridge->PushShowToast( error );
            }
            else
            {
                // Incompatible file — insert path into chat so the LLM can read it
                m_bridge->PushInputAppendText( path );
            }
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
    else if( href == "agent:open_simulator" )
        OnOpenSimulator();
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
    // Input attachment previews send raw base64; chat bubble clicks send a data URI in "src"
    std::string b64 = aMsg.value( "base64", "" );

    if( !b64.empty() )
    {
        FileAttach::ShowPreviewDialog( this, wxString::FromUTF8( b64 ) );
        return;
    }

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
        m_thinkingHtmlDirty = false;
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
    // Only suppress streaming updates if user has scrolled away from the bottom.
    // The 'active' flag is always true (scroll events only fire during activity),
    // so basing m_userScrolledUp on it would suppress updates even when the user
    // is at the bottom (e.g., after a layout reflow from tool result rendering).
    m_userScrolledUp = !coupled;
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

    m_bridge->PushActiveChat( m_chatHistoryDb.GetConversationId() );
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

        SaveModelPreference( m_currentModel );
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

void AGENT_FRAME::DoPlanToggle()
{
    m_agentMode = ( m_agentMode == AgentMode::EXECUTE )
                  ? AgentMode::PLAN
                  : AgentMode::EXECUTE;

    if( m_chatController )
        m_chatController->SetAgentMode( m_agentMode );

    if( m_llmClient )
        m_llmClient->SetAgentMode( m_agentMode );

    m_bridge->PushPlanMode( m_agentMode == AgentMode::PLAN );
}

void AGENT_FRAME::DoPlanApprove()
{
    // Remove approval button from UI
    m_bridge->PushRemovePlanApproval();

    // Switch to execute mode
    m_agentMode = AgentMode::EXECUTE;
    if( m_chatController )
        m_chatController->SetAgentMode( AgentMode::EXECUTE );
    if( m_llmClient )
        m_llmClient->SetAgentMode( AgentMode::EXECUTE );
    m_bridge->PushPlanMode( false );

    // Send approval message to kick off execution
    m_pendingInputText = "Proceed with the plan above.";
    wxCommandEvent evt;
    OnSend( evt );
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

void AGENT_FRAME::DoPendingChangesAcceptSheet( const wxString& aPath, bool aIsPcb )
{
    wxLogInfo( "AGENT_FRAME::DoPendingChangesAcceptSheet: %s (isPcb=%d)", aPath, aIsPcb );

    if( aIsPcb )
    {
        KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_APPROVE, payload );
        }
        m_hasPcbChanges = false;
        m_pcbFilename.Clear();
    }
    else
    {
        KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            nlohmann::json j;
            j["sheet_path"] = aPath.ToStdString();
            std::string payload = j.dump();
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_APPROVE, payload );
        }
        m_pendingSchSheets.erase( aPath );
    }

    QueryPendingChanges();
}

void AGENT_FRAME::DoPendingChangesRejectSheet( const wxString& aPath, bool aIsPcb )
{
    wxLogInfo( "AGENT_FRAME::DoPendingChangesRejectSheet: %s (isPcb=%d)", aPath, aIsPcb );

    if( aIsPcb )
    {
        KIWAY_PLAYER* pcbPlayer = Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REJECT, payload );
        }
        m_hasPcbChanges = false;
        m_pcbFilename.Clear();
    }
    else
    {
        KIWAY_PLAYER* schPlayer = Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            nlohmann::json j;
            j["sheet_path"] = aPath.ToStdString();
            std::string payload = j.dump();
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REJECT, payload );
        }
        m_pendingSchSheets.erase( aPath );
    }

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
    EnsureAuth();

    if( m_auth )
        m_auth->StartOAuthFlow( "agent" );
}

bool AGENT_FRAME::canCloseWindow( wxCloseEvent& aEvent )
{
    // When the user clicks the X button, hide instead of destroying. This preserves:
    //   - Shared auth pointer from the launcher (MAIL_AUTH_POINTER is only sent once at startup)
    //   - Loaded webview and chat state
    //   - Conversation history in memory
    // The window is re-shown by ShowPlayer/ShowAgent when the user reopens it.
    // Allow programmatic closes (NonUserClose from PlayersClose during app quit) through.
    if( !m_isNonUserClose && aEvent.CanVeto() )
    {
        wxLogTrace( "Agent", "Agent window hidden (close vetoed to preserve auth state)" );
        Hide();
        return false;
    }

    return true;
}

void AGENT_FRAME::OnExit( wxCommandEvent& event )
{
    Hide();
}

std::string AGENT_FRAME::SendRequest( int aDestFrame, const std::string& aPayload )
{
    // Check if the target frame exists.
    // For the terminal frame, create it on demand (needed for shell commands).
    // For editor frames, don't create — the editor must already be open.
    bool createIfMissing = ( static_cast<FRAME_T>( aDestFrame ) == FRAME_TERMINAL );
    KIWAY_PLAYER* targetPlayer = Kiway().Player( static_cast<FRAME_T>( aDestFrame ),
                                                  createIfMissing );
    if( !targetPlayer )
    {
        wxLogWarning( "AGENT: SendRequest - target frame %d is not open", aDestFrame );
        return "Error: Target frame is not open.";
    }

    wxLogInfo( "AGENT: SendRequest to frame %d, payload: %s",
               aDestFrame, wxString::FromUTF8( aPayload ) );

    // Use a sentinel value to distinguish "no response yet" from "empty response received"
    static const std::string NO_RESPONSE_SENTINEL = "\x01__NO_RESPONSE__\x01";
    m_toolResponse = NO_RESPONSE_SENTINEL;
    std::string payloadCopy = aPayload;

    Kiway().ExpressMail( static_cast<FRAME_T>( aDestFrame ), MAIL_AGENT_REQUEST, payloadCopy );

    // Wait for response (Sync)
    // We expect the target frame to reply via MAIL_AGENT_RESPONSE which sets m_toolResponse
    wxLongLong start = wxGetLocalTimeMillis();
    constexpr long TIMEOUT_MS = 120000;  // 2 minutes for bulk operations (e.g., labeling 100+ pins)
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

    wxLogInfo( "AGENT: SendRequest got response after %ld ms: %s",
               elapsed, wxString::FromUTF8( m_toolResponse ) );

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

void AGENT_FRAME::LoadAndSetSystemPrompt()
{
    // Resolve SharedSupport/agent/prompts/ in the app bundle
    std::string promptsDir;
    const char* envDir = std::getenv( "AGENT_PYTHON_DIR" );

    if( envDir && envDir[0] )
    {
        wxFileName dir( wxString::FromUTF8( envDir ), "" );
        dir.RemoveLastDir();  // python/ → agent/
        dir.AppendDir( "prompts" );
        promptsDir = dir.GetPath().ToStdString();
    }
    else
    {
        wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
        wxFileName dir( exePath.GetPath(), "" );
#ifdef __WXMSW__
        dir.AppendDir( "agent" );
        dir.AppendDir( "prompts" );
#else
        dir.RemoveLastDir();
        dir.AppendDir( "SharedSupport" );
        dir.AppendDir( "agent" );
        dir.AppendDir( "prompts" );
#endif
        promptsDir = dir.GetPath().ToStdString();
    }

    auto readFile = []( const std::string& path ) -> std::string
    {
        std::ifstream file( path );

        if( !file.is_open() )
        {
            wxLogWarning( "Could not open prompt file: %s", path );
            return "";
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    };

    std::string core = readFile( promptsDir + "/core.md" );
    std::string agent = readFile( promptsDir + "/agent.md" );
    std::string plan = readFile( promptsDir + "/plan.md" );

    if( !core.empty() )
    {
        std::string combined = core;

        if( !agent.empty() )
            combined += "\n" + agent;

        m_llmClient->SetSystemPrompt( combined );

        if( !plan.empty() )
            m_llmClient->SetPlanAddendum( plan );

        wxLogMessage( "Loaded system prompt from bundle (%zu bytes core + %zu bytes agent + %zu bytes plan)",
                      core.size(), agent.size(), plan.size() );
    }
    else
    {
        wxLogWarning( "System prompt not found in bundle — proxy will use its local copy" );
    }
}


void AGENT_FRAME::InitializeTools()
{
    m_tools = ToolSchemas::GetToolDefinitions();
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
    // Reset scroll guard so new streaming content (thinking, text) is visible.
    // Without this, m_userScrolledUp can remain true after a tool call if the user
    // scrolled during tool execution, causing FlushStreamingContentUpdate and the
    // timer to skip all DOM updates for the new response.
    m_userScrolledUp = false;

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
    m_thinkingHtmlDirty = false;
    m_isThinking = false;
    m_pendingToolCalls = nlohmann::json::array();

    // Ensure we're in the right state
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Start the async LLM request (uses m_apiContext which has been compacted)
    StartAsyncLLMRequest();
}


void AGENT_FRAME::OnLLMStreamChunk( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnLLMStreamChunk - entry" );

    // Get the chunk data from the event payload
    LLMStreamChunk* chunk = aEvent.GetPayload<LLMStreamChunk*>();
    if( !chunk )
    {
        wxLogError( "AGENT_FRAME::OnLLMStreamChunk - null chunk payload!" );
        return;
    }

    wxLogInfo( "AGENT_FRAME::OnLLMStreamChunk - type=%d, controller=%p",
               static_cast<int>( chunk->type ), static_cast<void*>( m_chatController.get() ) );

    // Forward to controller for processing
    // Controller emits EVT_CHAT_* events which are handled by OnChat* methods
    if( m_chatController )
    {
        m_chatController->HandleLLMChunk( *chunk );
    }

    wxLogInfo( "AGENT_FRAME::OnLLMStreamChunk - done" );

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

    // Auto-send queued message once BOTH conditions are met:
    //  1. OnChatTurnComplete(continuing=false) ran (cleanup done, m_turnCompleteForQueue set)
    //  2. OnLLMStreamComplete ran (background thread released m_requestInProgress)
    // Either event can fire first, so both check. Whichever fires second triggers the send.
    if( m_turnCompleteForQueue && HasQueuedMessage() )
    {
        m_turnCompleteForQueue = false;
        SendQueuedMessage();
    }
}


void AGENT_FRAME::OnLLMStreamError( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT: OnLLMStreamError called" );

    // Get error data
    LLMStreamComplete* complete = aEvent.GetPayload<LLMStreamComplete*>();
    std::string errorMessage = complete ? complete->error_message : "Unknown error";
    long httpCode = complete ? complete->http_status_code : 0;

    // Forward to controller - it will emit EVT_CHAT_ERROR
    // UI updates are handled by OnChatError
    if( m_chatController )
    {
        m_chatController->HandleLLMError( errorMessage, httpCode );
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

    // Sync conversation ID to controller so it can persist streaming snapshots
    if( m_chatController )
        m_chatController->SetChatId( m_chatHistoryDb.GetConversationId() );

    m_bridge->PushChatTitle( "New Chat" );

    // Clear historical thinking state
    m_historicalThinking.clear();
    m_historicalThinkingExpanded.clear();
    m_historicalToolResultExpanded.clear();
    m_currentThinkingIndex = -1;
    m_toolResultCounter = 0;
    m_queuedBubbleHtml.Clear();
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    // Focus the input textarea so the user can start typing immediately
    m_bridge->PushInputFocus();
}

void AGENT_FRAME::LoadConversation( const std::string& aConversationId )
{
    wxLogInfo( "AGENT_FRAME::LoadConversation called with id: %s, isGenerating=%d, "
               "llmInProgress=%d",
               aConversationId.c_str(), m_isGenerating,
               m_llmClient ? m_llmClient->IsRequestInProgress() : false );

    // Clean up any in-progress streaming UI state before switching chats
    if( m_isGenerating )
    {
        wxLogInfo( "AGENT_FRAME::LoadConversation - stopping generating animation" );
        StopGeneratingAnimation();
    }

    m_stopRequested = true;
    m_isCompacting = false;
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;
    m_pendingToolCalls = nlohmann::json::array();

    if( m_llmClient && m_llmClient->IsRequestInProgress() )
    {
        wxLogInfo( "AGENT_FRAME::LoadConversation - cancelling in-progress LLM request" );
        m_llmClient->CancelRequest();
    }

    // Delegate to controller - it will emit EVT_CHAT_HISTORY_LOADED
    // which triggers OnChatHistoryLoaded for UI updates
    if( m_chatController )
    {
        m_chatController->LoadChat( aConversationId );
    }
}


/**
 * Strip the *(Stopped)* marker from the end of a text string.
 * Returns true if the marker was found and stripped.
 */
static bool StripStoppedMarker( std::string& aText )
{
    static const std::string MARKER = "\n\n*(Stopped)*";
    static const std::string MARKER_ONLY = "*(Stopped)*";

    if( aText == MARKER_ONLY )
    {
        aText.clear();
        return true;
    }

    if( aText.size() >= MARKER.size() &&
        aText.compare( aText.size() - MARKER.size(), MARKER.size(), MARKER ) == 0 )
    {
        aText.erase( aText.size() - MARKER.size() );
        return true;
    }

    return false;
}


static std::string StripProjectContext( const std::string& aText )
{
    static const std::string PREFIX = "<project_context>\n";
    static const std::string SUFFIX = "\n</project_context>\n\n";

    if( aText.compare( 0, PREFIX.size(), PREFIX ) == 0 )
    {
        size_t endPos = aText.find( SUFFIX );
        if( endPos != std::string::npos )
            return aText.substr( endPos + SUFFIX.size() );
    }

    return aText;
}


static std::string StripSchematicChanges( const std::string& aText )
{
    static const std::string PREFIX = "<schematic_changes_by_user>\n";
    static const std::string SUFFIX = "</schematic_changes_by_user>\n\n";

    if( aText.compare( 0, PREFIX.size(), PREFIX ) == 0 )
    {
        size_t endPos = aText.find( SUFFIX );
        if( endPos != std::string::npos )
            return aText.substr( endPos + SUFFIX.size() );
    }

    return aText;
}


void AGENT_FRAME::RenderChatHistory()
{
    // Clear historical thinking storage
    m_historicalThinking.clear();

    // Reset counters - RenderChatHistory updates m_toolResultCounter so that
    // subsequent live tool calls get indices that don't collide with historical ones.
    m_toolResultCounter = 0;
    m_queuedBubbleHtml.Clear();
    m_toolDescByUseId.clear();
    m_toolNameByUseId.clear();

    // Build HTML from chat history (inner content only, no template wrapper)
    m_fullHtmlContent = "";

    for( const auto& msg : m_chatHistory )
    {
        if( !msg.contains( "role" ) || !msg.contains( "content" ) )
            continue;

        // Render a divider for compaction markers (not shown as a chat message)
        if( msg.contains( "_compaction" ) && msg["_compaction"] == true )
        {
            m_fullHtmlContent +=
                "<div class=\"flex items-center my-4\">"
                "<div class=\"flex-1 border-t border-[#404040]\"></div>"
                "<span class=\"mx-3 text-text-muted text-xs\">Context compacted</span>"
                "<div class=\"flex-1 border-t border-[#404040]\"></div>"
                "</div>";
            continue;
        }

        std::string role = msg["role"];

        // Content can be string or array (tool use)
        if( msg["content"].is_string() )
        {
            std::string content = msg["content"];

            if( role == "user" )
            {
                content = StripProjectContext( content );
                content = StripSchematicChanges( content );
            }

            // Strip leading newlines to match live streaming behavior
            size_t start = content.find_first_not_of( "\n\r" );
            if( start != std::string::npos && start > 0 )
                content = content.substr( start );

            wxString display = content;

            if( role == "user" )
            {
                // Right-aligned speech bubble style for user messages
                display.Replace( "&", "&amp;" );
                display.Replace( "<", "&lt;" );
                display.Replace( ">", "&gt;" );
                display.Replace( "\n", "<br>" );
                m_fullHtmlContent += wxString::Format(
                    "<div class=\"flex justify-end my-3\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap break-words\">%s</div></div>",
                    display );
            }
            else if( role == "assistant" )
            {
                // Strip *(Stopped)* marker and render as styled div to match live stop
                bool wasStopped = StripStoppedMarker( content );
                m_fullHtmlContent += AgentMarkdown::ToHtml( content );
                if( wasStopped )
                    m_fullHtmlContent += "<div class=\"text-text-muted mb-1\">Stopped</div>";
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

                    if( role == "user" )
                    {
                        text = StripProjectContext( text );
                        text = StripSchematicChanges( text );
                    }

                    // Strip leading newlines to match live streaming behavior
                    size_t start = text.find_first_not_of( "\n\r" );
                    if( start != std::string::npos && start > 0 )
                        text = text.substr( start );

                    wxString display = text;

                    if( role == "assistant" )
                    {
                        // Strip *(Stopped)* marker and render as styled div to match live stop
                        bool wasStopped = StripStoppedMarker( text );
                        m_fullHtmlContent += AgentMarkdown::ToHtml( text );
                        if( wasStopped )
                            m_fullHtmlContent += "<div class=\"text-text-muted mb-1\">Stopped</div>";
                    }
                    else if( role == "user" )
                    {
                        // Right-aligned speech bubble style for user messages
                        display.Replace( "&", "&amp;" );
                        display.Replace( "<", "&lt;" );
                        display.Replace( ">", "&gt;" );
                        display.Replace( "\n", "<br>" );
                        m_fullHtmlContent += wxString::Format(
                            "<div class=\"flex justify-end my-3\"><div class=\"bg-bg-tertiary py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap break-words\">%s</div></div>",
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
                            "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline thinking-link\">Thinking</a>"
                            "<div class=\"thinking-content text-[#606060] mt-1 mb-0 pl-3 border-l-2 border-[#404040] whitespace-pre-wrap%s\" data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\">%s</div>"
                            "</div>",
                            thinkingIndex, expandedClass, thinkingIndex, displayStyle, escapedText );
                    }
                }
                else if( blockType == "tool_use" )
                {
                    // Render tool_use block with human-readable description
                    std::string toolName = block.value( "name", "unknown" );
                    std::string toolId = block.value( "id", "" );
                    nlohmann::json toolInput = block.value( "input", nlohmann::json::object() );
                    wxString desc = TOOL_REGISTRY::Instance().GetDescription( toolName, toolInput );

                    // Store in map keyed by tool_use id for pairing with tool_result
                    if( !toolId.empty() )
                    {
                        m_toolDescByUseId[toolId] = desc;
                        m_toolNameByUseId[toolId] = toolName;
                    }

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

                    // Look up tool description by tool_use_id, fall back to
                    // m_lastToolDesc (single-tool case), then generic fallback
                    wxString desc;
                    std::string toolUseId = block.value( "tool_use_id", "" );

                    if( !toolUseId.empty() && m_toolDescByUseId.count( toolUseId ) )
                        desc = m_toolDescByUseId[toolUseId];
                    else if( !m_lastToolDesc.IsEmpty() )
                        desc = m_lastToolDesc;
                    else
                        desc = "Tool execution";

                    bool expanded = m_historicalToolResultExpanded.count( m_toolResultCounter ) > 0;

                    m_fullHtmlContent += BuildToolResultHtml( m_toolResultCounter, desc,
                                                             statusClass, statusText,
                                                             fullFormatted,
                                                             wxEmptyString, expanded );

                    // Render image AFTER the collapsible tool result so it is
                    // always visible in the chat stream.
                    if( !imageHtml.IsEmpty() )
                    {
                        m_fullHtmlContent += "<div class=\"my-2\">"
                                             + imageHtml
                                             + "</div>";
                    }

                    // Add "Open in Simulator" button for historical SPICE results
                    std::string histToolName;

                    if( !toolUseId.empty() && m_toolNameByUseId.count( toolUseId ) )
                        histToolName = m_toolNameByUseId[toolUseId];

                    if( histToolName == "sch_run_simulation" && !isError && !isPythonError )
                    {
                        m_fullHtmlContent +=
                            "<div class=\"my-1\"><a href=\"agent:open_simulator\" "
                            "style=\"background:#1a3d1a; color:#4ade80; padding:5px 14px; "
                            "border-radius:6px; font-size:12px; font-weight:600; "
                            "text-decoration:none; cursor:pointer; "
                            "display:inline-block;\">Open in Simulator</a></div>";
                    }

                    m_toolResultCounter++;
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
    EnsureAuth();

    if( m_auth )
        return m_auth->IsAuthenticated();

    return false;
}

void AGENT_FRAME::EnsureAuth()
{
    if( m_auth )
        return;

    // Fallback: create a local AGENT_AUTH if the launcher didn't provide one
    // (e.g., agent opened from sch/pcb editor when preload failed).
    wxLogTrace( "Agent", "No shared auth from launcher, creating fallback auth" );

    m_auth = new AGENT_AUTH();
    m_ownsAuth = true;

    // Load Supabase configuration from JSON file
    std::string supabaseUrl, supabaseKey;

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
            }
            catch( ... ) {}
            configFile.close();
        }
    }

    if( !supabaseUrl.empty() && !supabaseKey.empty() )
        m_auth->Configure( supabaseUrl, supabaseKey );
    else
        m_auth->LoadSession();

    // Wire auth and Supabase config to tool registry (for datasheet extraction)
    TOOL_REGISTRY::Instance().SetAuth( m_auth );

    if( !supabaseUrl.empty() )
    {
        TOOL_REGISTRY::Instance().SetSupabaseUrl( supabaseUrl );
        TOOL_REGISTRY::Instance().SetSupabaseAnonKey( supabaseKey );
    }

    // Wire to LLM client and controller
    m_llmClient->SetAuth( m_auth );

    if( m_chatController )
        m_chatController->SetAuth( m_auth );

    // Wire auth to cloud sync
    ConfigureCloudSync();

    UpdateAuthUI();
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

    // If diff views are disabled, hide the pending changes bar and skip querying
    COMMON_SETTINGS* settings = Pgm().GetSettingsManager().GetCommonSettings();

    if( settings && !settings->m_Agent.enable_diff_view )
    {
        m_bridge->PushPendingChangesShow( false );
        return;
    }

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


void AGENT_FRAME::OnOpenSimulator()
{
    wxLogInfo( "AGENT_FRAME::OnOpenSimulator called" );

    KIWAY_PLAYER* simFrame = Kiway().Player( FRAME_SIMULATOR, true );

    if( !simFrame )
    {
        wxLogWarning( "OnOpenSimulator: failed to create simulator frame" );
        return;
    }

    if( wxWindow* blocking = simFrame->Kiway().GetBlockingDialog() )
        blocking->Close( true );

    simFrame->Show( true );

    if( simFrame->IsIconized() )
        simFrame->Iconize( false );

    simFrame->Raise();
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
    wxLogInfo( "AGENT_FRAME::OnChatTextDelta - entry" );

    ChatTextDeltaData* data = aEvent.GetPayload<ChatTextDeltaData*>();
    if( !data )
        return;

    // Markdown text is now streaming - hide the waiting dots and compacting indicator
    m_isStreamingMarkdown = true;
    m_isCompacting = false;

    // Controller owns the response - UpdateAgentResponse reads from controller

    // Re-render full response with markdown
    UpdateAgentResponse();

    wxLogInfo( "AGENT_FRAME::OnChatTextDelta - done" );

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

    // Initialize thinking state for new block — compaction is done
    m_isThinking = true;
    m_isCompacting = false;
    m_thinkingContent = "";
    m_thinkingExpanded = false;

    // Set index for this thinking block using m_historicalThinking.size()
    // After the push above (if any), this gives us the correct next index
    m_currentThinkingIndex = static_cast<int>( m_historicalThinking.size() );

    // Rebuild thinking HTML and immediately flush to DOM
    // This bypasses the timer to minimize delay before thinking link is clickable
    RebuildThinkingHtml();
    m_thinkingHtmlDirty = false;
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

    // Mark thinking HTML as dirty - defer the expensive HTML escape + rebuild to the
    // next timer tick. Thinking deltas arrive hundreds of times per second but the timer
    // only fires every 50ms, so rebuilding on every delta wastes O(n) work per delta
    // (total O(n²) over the block).
    m_thinkingHtmlDirty = true;
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
    m_thinkingHtmlDirty = false;
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

    // Skip queued events that arrive after cancellation
    if( m_stopRequested )
    {
        delete data;
        return;
    }

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
        // Preserve queued bubble at the end
        if( !m_queuedBubbleHtml.IsEmpty() )
            fullHtml += m_queuedBubbleHtml;
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

        wxLogInfo( "AGENT_FRAME::OnChatToolStart - assigned idx=%d (counter now %d)",
                   idx, m_toolResultCounter );

        // Append running box to permanent DOM
        AppendHtml( m_activeRunningHtml );

        // Create new streaming div after the running box
        wxString streamingDiv = wxS( "<div id=\"streaming-content\"></div>" );
        AppendHtml( streamingDiv );
        m_htmlBeforeAgentResponse = m_fullHtmlContent;
    }

    // Handle open_editor — requires KIWAY access for player management
    if( data->toolName == "open_editor" )
    {
        OPEN_EDITOR_HANDLER handler;

        std::vector<std::string> allowedPaths;
        for( const auto& p : GetAllowedPaths() )
            allowedPaths.push_back( p.ToStdString() );

        std::string editorType = data->input.value( "editor_type", "" );
        FRAME_T frameType = ( editorType == "sch" ) ? FRAME_SCH : FRAME_PCB_EDITOR;

        KIWAY_PLAYER* existingPlayer = Kiway().Player( frameType, false );
        bool editorShown = existingPlayer && existingPlayer->IsShown();
        std::string currentFile = editorShown
                                      ? existingPlayer->GetCurrentFileName().ToStdString()
                                      : "";

        auto result = handler.Evaluate( data->input,
                                        Kiway().Prj().GetProjectPath().ToStdString(),
                                        Kiway().Prj().GetProjectName().ToStdString(),
                                        allowedPaths, editorShown, currentFile );

        m_pendingOpenFilePath = result.filePath;

        switch( result.action )
        {
        case OpenEditorResult::FOCUS_EXISTING:
            if( existingPlayer->IsIconized() )
                existingPlayer->Iconize( false );
            existingPlayer->Raise();

            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId, result.resultMessage, true );

            delete data;
            return;

        case OpenEditorResult::RELOAD_WITH_FILE:
            existingPlayer->Close( true );
            {
                KIWAY_PLAYER* newPlayer = Kiway().Player( frameType, true );
                if( newPlayer )
                {
                    std::vector<wxString> files;
                    files.push_back( result.filePath );
                    newPlayer->OpenProjectFiles( files );
                    newPlayer->Show( true );
                    newPlayer->Raise();

                    m_pendingOpenFilePath.Clear();

                    if( m_chatController )
                        m_chatController->HandleToolResult( data->toolId,
                                                            result.resultMessage, true );
                }
                else
                {
                    m_pendingOpenFilePath.Clear();

                    if( m_chatController )
                        m_chatController->HandleToolResult( data->toolId,
                            "Error: Failed to reopen " + result.editorLabel.ToStdString()
                                + " editor", false );
                }
            }
            delete data;
            return;

        case OpenEditorResult::NEEDS_APPROVAL:
            m_pendingOpenSch = result.isSch;
            m_pendingOpenPcb = !result.isSch;
            m_pendingOpenToolId = data->toolId;
            ShowOpenEditorApproval( result.editorLabel );
            delete data;
            return;

        case OpenEditorResult::ERRORED:
            if( m_chatController )
                m_chatController->HandleToolResult( data->toolId, result.errorMessage, false );
            delete data;
            return;
        }
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

    // Skip queued events that arrive after cancellation
    if( m_stopRequested )
    {
        delete data;
        return;
    }

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
            m_bridge->PushToolResultImageChunk( idx, chunk );
        }

        m_bridge->PushToolResultImageEnd( idx );

        size_t numChunks = ( b64.length() + chunkSize - 1 ) / chunkSize;
        wxLogInfo( "AGENT_FRAME::OnChatToolComplete - pushed %zu image chunks for idx=%d",
                   numChunks, idx );
    }

    // Add "Open in Simulator" button for successful SPICE simulation results
    if( data->success && data->toolName == "sch_run_simulation" )
    {
        m_bridge->PushToolResultActionButton( idx, "Open in Simulator",
                                               "agent:open_simulator" );
    }

    // Update internal HTML tracking (replace running HTML with full completed HTML).
    // Image is rendered OUTSIDE the collapsible tool result so it stays visible.
    wxString completedHtml = BuildToolResultHtml( idx, desc, statusClass, statusText,
                                                  fullFormatted, wxEmptyString, false );

    if( !imageHtml.IsEmpty() )
        completedHtml += "<div class=\"my-2\">" + imageHtml + "</div>";

    // Include "Open in Simulator" button in internal HTML for full re-renders
    if( data->success && data->toolName == "sch_run_simulation" )
    {
        completedHtml += "<div class=\"my-1\"><a href=\"agent:open_simulator\" "
            "style=\"background:#1a3d1a; color:#4ade80; padding:5px 14px; "
            "border-radius:6px; font-size:12px; font-weight:600; text-decoration:none; "
            "cursor:pointer; display:inline-block;\">Open in Simulator</a></div>";
    }

    if( !m_activeRunningHtml.IsEmpty() )
    {
        size_t prevLen = m_fullHtmlContent.length();
        m_fullHtmlContent.Replace( m_activeRunningHtml, completedHtml );
        bool replaced = ( m_fullHtmlContent.length() != prevLen );
        m_htmlBeforeAgentResponse.Replace( m_activeRunningHtml, completedHtml );

        if( !replaced )
            wxLogWarning( "AGENT_FRAME::OnChatToolComplete - Replace FAILED for idx=%d "
                          "(running HTML not found in m_fullHtmlContent)", idx );
    }
    m_activeRunningHtml.Clear();

    // Safety net: if this tool had an image, re-push the status update on the next
    // event loop iteration. The 50+ image chunk scripts can delay or disrupt the
    // original updateToolResult call; this idempotent re-push ensures the DOM reflects
    // the completed state.
    if( data->hasImage && !data->imageBase64.empty() )
    {
        wxString safetyStatusClass = statusClass;
        wxString safetyStatusText = statusText;
        wxString safetyBody = textBody;
        int safetyIdx = idx;

        CallAfter( [this, safetyIdx, safetyStatusClass, safetyStatusText, safetyBody]() {
            m_bridge->PushToolResultUpdate( safetyIdx, safetyStatusClass, safetyStatusText,
                                            safetyBody );
        } );
    }

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

    UpdateAgentResponse();
    // Auto-scroll handled by CSS flex-direction: column-reverse

    delete data;
}


void AGENT_FRAME::OnAsyncToolComplete( wxCommandEvent& aEvent )
{
    // Handle completion of async tools (like autorouter) that run in background threads
    ToolExecutionResult* result = static_cast<ToolExecutionResult*>( aEvent.GetClientData() );
    if( !result )
        return;

    wxLogInfo( "AGENT_FRAME::OnAsyncToolComplete - tool=%s, success=%s",
               result->tool_name.c_str(), result->success ? "true" : "false" );

    // Forward the result to the chat controller
    if( m_chatController )
    {
        m_chatController->HandleToolResult( result->tool_use_id, result->result, result->success );
    }

    // Clean up - event data is owned by the event
    delete result;
}


void AGENT_FRAME::CommonSettingsChanged( int aFlags )
{
    // AGENT_FRAME doesn't have toolbars or most of the infrastructure that
    // EDA_BASE_FRAME::CommonSettingsChanged expects.
    // Don't call base class to avoid RecreateToolbars() crash (m_toolbarSettings is null).
    // The agent frame is a simple webview-based chat interface that doesn't need
    // toolbar recreation or most common settings handling.
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
            UploadCurrentChat();
        }
    }

    // Take schematic snapshot for user edit detection on next message
    if( !continuing && m_chatController )
    {
        m_chatController->TakeSchematicSnapshot();
    }

    // Show plan approval button when plan mode turn completes
    if( !continuing && m_agentMode == AgentMode::PLAN )
    {
        m_bridge->PushPlanApproval();
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

    // Auto-send queued message.  Both OnChatTurnComplete and OnLLMStreamComplete must
    // have fired: TurnComplete ensures cleanup is done, StreamComplete ensures the
    // background thread released m_requestInProgress.  Either can fire first.
    if( !continuing && HasQueuedMessage() )
    {
        if( !m_llmClient->IsRequestInProgress() )
        {
            // StreamComplete already fired — safe to send now.
            SendQueuedMessage();
        }
        else
        {
            // StreamComplete hasn't fired yet — flag so it sends when it does.
            m_turnCompleteForQueue = true;
        }
    }
}


static wxString HumanizeErrorMessage( const std::string& aMessage, long aHttpCode,
                                      const std::string& aErrorType )
{
    // Try to extract a human-readable message from JSON response body
    auto extractApiMessage = []( const std::string& aBody ) -> std::string
    {
        try
        {
            auto j = nlohmann::json::parse( aBody );

            if( j.contains( "error" ) )
            {
                if( j["error"].is_string() )
                    return j["error"].get<std::string>();
                if( j["error"].is_object() && j["error"].contains( "message" ) )
                    return j["error"]["message"].get<std::string>();
            }
        }
        catch( ... )
        {
        }
        return "";
    };

    // --- HTTP errors (from non-200 API responses) ---
    if( aHttpCode > 0 )
    {
        std::string apiMessage = extractApiMessage( aMessage );

        switch( aHttpCode )
        {
        case 401:
            return wxString::Format( "Your session has expired. Please sign in again. (%ld)",
                                     aHttpCode );

        case 429:
        {
            wxString msg = !apiMessage.empty()
                    ? wxString::FromUTF8( apiMessage )
                    : wxString( "You've reached your usage limit." );
            return wxString::Format(
                    "<a href=\"%s/dashboard/billing\">%s</a> (%ld)",
                    ZEO_BASE_URL, msg, aHttpCode );
        }

        case 400:
            if( !apiMessage.empty() )
                return wxString::Format( "%s (%ld)", apiMessage, aHttpCode );
            return wxString::Format( "The request was invalid. (%ld)", aHttpCode );

        case 500:
            return wxString::Format( "The server is temporarily unavailable. (%ld)", aHttpCode );

        case 502:
        case 503:
        case 529:
            return wxString::Format( "Anthropic's API is temporarily unavailable. (%ld)",
                                     aHttpCode );

        default:
            if( !apiMessage.empty() )
                return wxString::Format( "%s (%ld)", apiMessage, aHttpCode );
            return wxString::Format( "An unexpected error occurred. (%ld)", aHttpCode );
        }
    }

    // --- SSE streaming errors (error type from Anthropic's event stream) ---
    if( !aErrorType.empty() )
    {
        if( aErrorType == "overloaded_error" )
            return wxString::Format( "Anthropic's API is currently overloaded. (%s)", aErrorType );
        if( aErrorType == "api_error" )
            return wxString::Format( "Anthropic's API returned an internal error. (%s)", aErrorType );
        if( aErrorType == "rate_limit_error" )
            return wxString::Format( "Anthropic's API rate limit reached. (%s)", aErrorType );
        if( aErrorType == "authentication_error" )
            return wxString::Format( "Your session has expired. Please sign in again. (%s)",
                                     aErrorType );
        if( aErrorType == "invalid_request_error" )
        {
            if( !aMessage.empty() )
                return wxString::Format( "%s (%s)", aMessage, aErrorType );
            return wxString::Format( "The request was invalid. (%s)", aErrorType );
        }

        if( !aMessage.empty() )
            return wxString::Format( "%s (%s)", aMessage, aErrorType );
        return wxString::Format( "An unexpected error occurred. (%s)", aErrorType );
    }

    // --- Curl / network errors (message starts with "Curl error:") ---
    if( aMessage.find( "Curl error:" ) == 0 )
    {
        std::string lower = aMessage;
        std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );

        if( lower.find( "resolve" ) != std::string::npos
            || lower.find( "connect" ) != std::string::npos )
            return "Unable to connect — check your internet connection.";
        if( lower.find( "timed out" ) != std::string::npos
            || lower.find( "timeout" ) != std::string::npos )
            return "The request timed out.";
        if( lower.find( "ssl" ) != std::string::npos )
            return "A secure connection couldn't be established.";
        return "A connection error occurred.";
    }

    // --- Plain messages (controller errors, etc.) — pass through ---
    return wxString::FromUTF8( aMessage );
}


void AGENT_FRAME::OnChatError( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnChatError - error received" );
    ChatErrorData* data = aEvent.GetPayload<ChatErrorData*>();
    if( !data )
        return;

    // Stop all animations before showing the error so the "..." dots are cleared
    StopGeneratingAnimation();
    m_isCompacting = false;

    // Finalize the streaming div so the next response gets a fresh one
    m_bridge->PushFinalizeStreaming();
    m_fullHtmlContent.Replace( "<div id=\"streaming-content\">", "<div>" );

    // Clear streaming state
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_currentThinkingIndex = -1;

    // Display human-readable error message (raw error is already in the log from HandleLLMError)
    wxString friendly = HumanizeErrorMessage( data->message, data->httpCode, data->errorType );
    wxString errorHtml = wxString::Format(
        "<div class=\"bg-bg-secondary rounded-md p-3 my-2\">"
        "<p class=\"text-accent-red text-[13px] m-0\"><b>Error:</b> %s</p>"
        "</div>",
        friendly );
    AppendHtml( errorHtml );

    // Re-insert all queued messages into input on error (don't lose the user's text)
    if( HasQueuedMessage() )
    {
        wxString combined;
        for( const auto& msg : m_queuedMessages )
        {
            if( !msg.text.IsEmpty() )
            {
                if( !combined.IsEmpty() )
                    combined += "\n";
                combined += msg.text;
            }
        }
        if( !combined.IsEmpty() )
            m_bridge->PushInputPrependText( combined );
    }

    ClearQueuedMessage();
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
    case AgentConversationState::ERRORED:
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
        m_chatHistory = m_chatController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        UploadCurrentChat();
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
    m_activeRunningHtml.Clear();
    m_activeToolResultIdx = -1;

    // Render the loaded chat history (also advances m_toolResultCounter past
    // historical tool-result indices so new live tools get non-colliding DOM IDs)
    RenderChatHistory();

    // Update DB ID so new messages go to this history
    m_chatHistoryDb.SetConversationId( data->chatId );

    // Auto-scroll to bottom handled by CSS flex-direction: column-reverse
    m_userScrolledUp = false;

    delete data;
}


void AGENT_FRAME::OnChatCompaction( wxThreadEvent& aEvent )
{
    wxLogInfo( "AGENT_FRAME::OnChatCompaction - context compacted, showing indicator" );
    m_isCompacting = true;

    // Start the generating animation timer if not already running,
    // so the "Compacting..." dots animate
    if( !m_generatingTimer.IsRunning() )
    {
        m_generatingDots = 1;
        m_generatingTimer.Start( 400 );
    }

    UpdateAgentResponse();
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


wxString AGENT_FRAME::GetPreferencesPath()
{
    wxString appSupport = wxStandardPaths::Get().GetUserDataDir();
    wxFileName dir( appSupport, wxEmptyString );
    dir.RemoveLastDir();
    dir.AppendDir( "kicad" );
    return wxFileName( dir.GetPath(), "agent_preferences", "json" ).GetFullPath();
}


void AGENT_FRAME::SaveModelPreference( const std::string& aModel )
{
    wxString path = GetPreferencesPath();

    // Ensure parent directory exists
    wxFileName fn( path );
    if( !wxFileName::DirExists( fn.GetPath() ) )
        wxFileName::Mkdir( fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    // Load existing prefs (if any) so we don't clobber other future settings
    nlohmann::json prefs;
    std::ifstream inFile( path.ToStdString() );
    if( inFile.is_open() )
    {
        try { inFile >> prefs; }
        catch( ... ) { prefs = nlohmann::json::object(); }
        inFile.close();
    }

    prefs["model"] = aModel;

    std::ofstream outFile( path.ToStdString() );
    if( outFile.is_open() )
    {
        outFile << prefs.dump( 2 );
        outFile.close();
    }
}


std::string AGENT_FRAME::LoadModelPreference()
{
    wxString path = GetPreferencesPath();

    std::ifstream file( path.ToStdString() );
    if( file.is_open() )
    {
        try
        {
            nlohmann::json prefs;
            file >> prefs;

            if( prefs.contains( "model" ) && prefs["model"].is_string() )
                return prefs["model"].get<std::string>();
        }
        catch( ... ) {}
    }

    return "Claude 4.6 Opus";
}


// ============================================================================
// Cloud Sync
// ============================================================================

void AGENT_FRAME::ConfigureCloudSync()
{
    if( !m_cloudSync || !m_auth )
        return;

    // Load Supabase configuration
    std::string supabaseUrl, supabaseKey;

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
            }
            catch( ... ) {}
            configFile.close();
        }
    }

    if( supabaseUrl.empty() || supabaseKey.empty() )
        return;

    m_cloudSync->SetAuth( m_auth );
    m_cloudSync->Configure( supabaseUrl, supabaseKey );

    // Also configure tool registry with Supabase credentials (for datasheet extraction)
    TOOL_REGISTRY::Instance().SetSupabaseUrl( supabaseUrl );
    TOOL_REGISTRY::Instance().SetSupabaseAnonKey( supabaseKey );

    // Run initial sync to upload any missed chats/logs from previous sessions
    m_cloudSync->SyncAll();
}


void AGENT_FRAME::UploadCurrentChat()
{
    if( !m_cloudSync )
        return;

    std::string convId = m_chatHistoryDb.GetConversationId();

    if( convId.empty() )
        return;

    // Build the JSON content (same format as what Save() writes to disk)
    json wrapper;
    wrapper["id"] = convId;
    wrapper["title"] = m_chatHistoryDb.GetTitle();
    wrapper["created_at"] = m_chatHistoryDb.GetCreatedAt();
    wrapper["last_updated"] = m_chatHistoryDb.GetLastUpdated();
    wrapper["messages"] = m_chatHistory;

    m_cloudSync->UploadChat( convId, wrapper.dump() );
}
