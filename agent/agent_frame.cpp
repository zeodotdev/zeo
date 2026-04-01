#include "agent_frame.h"
#include "agent_chat_history.h"
#include <zeo/agent_auth.h>
#include <app_monitor.h>
#ifndef __WXMAC__
#include <zeo/auth_callback_server.h>
#endif
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
#include "claudecode/cc_controller.h"
#include <kiway_mail.h>
#include <mail_type.h>
#include <pgm_base.h>
#include <api/api_server.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <wx/log.h>
#include <kiway.h>
#include <paths.h>
#include <kiplatform/environment.h>
#include <kiplatform/ui.h>
#include <frame_type.h>
#include <algorithm>
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
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <bitmaps.h>
#include <id.h>
#include <nlohmann/json.hpp>
#include <zeo/zeo_constants.h>
#include <wx/settings.h>
#include <wx/uri.h>
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


// Helper: Escape a string for safe embedding in HTML content.
static wxString EscapeHtml( const wxString& aStr )
{
    wxString result = aStr;
    result.Replace( "&", "&amp;" );
    result.Replace( "<", "&lt;" );
    result.Replace( ">", "&gt;" );
    result.Replace( "\"", "&quot;" );
    result.Replace( "'", "&#39;" );
    return result;
}


// Helper: Try to parse raw tool result as JSON and pretty-print it.
// Falls back to HTML-escaped raw string if not valid JSON.
static wxString FormatToolResult( const std::string& aRawResult )
{
    try
    {
        nlohmann::json parsed = nlohmann::json::parse( aRawResult );
        std::string pretty = parsed.dump( 2 );
        return "<code class=\"language-json\">" + EscapeHtml( wxString::FromUTF8( pretty ) ) + "</code>";
    }
    catch( const nlohmann::json::exception& )
    {
        // Not valid JSON - HTML-escape and return as-is
        return EscapeHtml( wxString::FromUTF8( aRawResult ) );
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
    catch( const nlohmann::json::exception& )
    {
        // Not JSON - take first line
        wxString raw = wxString::FromUTF8( aRawResult );
        wxString firstLine = raw.BeforeFirst( '\n' );

        if( static_cast<int>( firstLine.length() ) > aMaxLen )
            firstLine = firstLine.Left( aMaxLen ) + "...";

        return firstLine;
    }
}


static wxString ChevronClass( bool aExpanded )
{
    return aExpanded ? "toggle-chevron expanded" : "toggle-chevron";
}


// Helper: Build the full tool result component HTML in "Running..." state.
// Includes the collapsible body (initially empty/hidden) so the JS callback
// can populate it on completion without replacing the element.
static wxString BuildRunningToolHtml( int aIndex, const wxString& aDesc )
{
    return wxString::Format(
        "<div id=\"tool-result-%d\" class=\"tool-result-card rounded-lg my-2 max-w-full break-words\">"
        "<div "
        "class=\"tool-result-header py-2.5 px-3 flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\" title=\"%s\">%s</span>"
        "<span class=\"tool-status text-text-muted text-[12px] ml-auto flex items-center gap-2\">"
        "<span class=\"tool-elapsed\" data-start=\"%lld\"></span>"
        "<a href=\"agent:cancel_tool\" style=\"color:var(--text-muted); font-size:11px; "
        "text-decoration:none; opacity:0.7; cursor:pointer;\" "
        "onmouseover=\"this.style.opacity='1';this.style.color='var(--accent-red)'\" "
        "onmouseout=\"this.style.opacity='0.7';this.style.color='var(--text-muted)'\">Cancel</a>"
        "<span class=\"tool-spinner\"></span>"
        "</span>"
        "</div>"
        "<div class=\"tool-result-body p-3 pt-0\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
        "</div>"
        "</div>",
        aIndex, EscapeHtml( aDesc ), EscapeHtml( aDesc ), (long long) wxGetUTCTimeMillis().GetValue(), aIndex );
}


// Helper: Build the collapsible HTML for a completed tool result block.
// Has a stable id="tool-result-N" matching the running box it replaces.
static wxString BuildToolResultHtml( int aIndex, const wxString& aDesc,
                                     const wxString& aStatusColor,
                                     const wxString& aFullFormatted,
                                     const wxString& aImageHtml, bool aExpanded )
{
    // If no meaningful content, render non-clickable header (no toggle)
    bool hasContent = !aFullFormatted.IsEmpty() || !aImageHtml.IsEmpty();

    if( !hasContent )
    {
        return wxString::Format(
            "<div id=\"tool-result-%d\" class=\"tool-result-card rounded-lg my-2 max-w-full break-words\">"
            "<div "
            "class=\"tool-result-header py-2.5 px-3 flex items-center gap-2\">"
            "<span class=\"text-text-secondary text-[12px]\" title=\"%s\">%s</span>"
            "<span class=\"tool-status-dot ml-auto\" style=\"background:%s;\"></span>"
            "</div>"
            "<div class=\"tool-result-body p-3 pt-0\" "
            "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
            "</div>"
            "</div>",
            aIndex, EscapeHtml( aDesc ), EscapeHtml( aDesc ), aStatusColor, aIndex );
    }

    wxString displayStyle = aExpanded ? "block" : "none";
    wxString chevronClass = ChevronClass( aExpanded );

    wxString html = wxString::Format(
        "<div id=\"tool-result-%d\" class=\"tool-result-card rounded-lg my-2 max-w-full break-words\">"
        // Clickable header: same layout as the Running box
        "<a href=\"toggle:toolresult:%d\" "
        "class=\"tool-result-header py-2.5 px-3 no-underline flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\" title=\"%s\">%s</span>"
        "<span class=\"%s\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><polyline points=\"9 6 15 12 9 18\"/></svg></span>"
        "<span class=\"tool-status-dot ml-auto\" style=\"background:%s;\"></span>"
        "</a>"
        // Expanded content (hidden by default)
        "<div class=\"tool-result-body p-3 pt-0\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:%s;\">"
        "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap break-words m-0 mt-2\">%s</pre>"
        "%s"
        "</div>"
        "</div>",
        aIndex,
        aIndex,
        EscapeHtml( aDesc ), EscapeHtml( aDesc ),
        chevronClass, aStatusColor,
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
        "<div id=\"tool-result-%d\" class=\"tool-result-card rounded-lg my-2 max-w-full break-words\">"
        "<div class=\"py-2.5 px-3 flex items-center gap-2\">"
        "<span class=\"text-text-secondary text-[12px]\">%s</span>"
        "<span class=\"toggle-chevron\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><polyline points=\"9 6 15 12 9 18\"/></svg></span>"
        "<span class=\"tool-status text-[12px] ml-auto\">"
        "<a href=\"%s\" style=\"background:%s; color:%s; padding:3px 14px; "
        "border-radius:8px; font-size:12px; font-weight:500; text-decoration:none; "
        "cursor:pointer;\">%s</a>"
        "</span>"
        "</div>"
        "<div class=\"tool-result-body p-3 pt-0\" "
        "data-toggle-type=\"toolresult\" data-toggle-index=\"%d\" style=\"display:none;\">"
        "</div>"
        "</div>",
        aIndex, EscapeHtml( aDesc ), aActionHref, aBgColor, aTextColor, EscapeHtml( aButtonText ), aIndex );
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

    if( !KIPLATFORM::UI::IsDarkTheme() )
    {
        // Apply light theme by adding 'light' class to the html element
        htmlContent.Replace( wxS( "<html class=\"h-full\">" ),
                             wxS( "<html class=\"h-full light\">" ) );
    }

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
    m_runningHtmlByIdx.clear();
    m_activeToolResultIdx = -1;
    m_showingOnboarding = false;

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

    // Initialize first conversation so it gets an ID for usage tracking
    m_chatHistoryDb.StartNewConversation();
    m_chatHistoryDb.SetProjectPath( Kiway().Prj().GetProjectPath().ToStdString() );

    // Create chat controller
    m_chatController = std::make_unique<CHAT_CONTROLLER>( this );
    m_chatController->SetLLMClient( m_llmClient.get() );
    m_chatController->SetChatHistoryDb( &m_chatHistoryDb );
    m_chatController->SetChatId( m_chatHistoryDb.GetConversationId() );
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

    // VCS notification: auto-init git repo and refresh VCS UI after agent writes.
    // Creates the VCS frame if needed (without showing it) so the mail is delivered.
    m_chatController->SetVcsNotifyFn(
        [this]() {
            try
            {
                // Ensure VCS frame exists (create=true) so it can receive the mail.
                // This does not show the window — only Player + Show() makes it visible.
                Kiway().Player( FRAME_VCS, true );

                std::string payload;
                Kiway().ExpressMail( FRAME_VCS, MAIL_VCS_REFRESH, payload );
            }
            catch( ... )
            {
                // VCS kiface load failure — safe to ignore
            }
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

            // Provide IPC functions so handlers (pcb_autoroute, generate_net_classes)
            // can communicate with editor frames.
            reg.SetSendRequestFn(
                [this]( int aFrameType, const std::string& aPayload ) -> std::string {
                    return SendRequest( aFrameType, aPayload );
                } );
            reg.SetSendFireAndForgetFn(
                [this]( int aFrameType, const std::string& aPayload ) {
                    std::string payloadCopy = aPayload;
                    Kiway().ExpressMail( static_cast<FRAME_T>( aFrameType ),
                                         MAIL_AGENT_REQUEST, payloadCopy );
                } );
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

    // Create menu bar (on macOS the menu bar lives in the system bar and is required;
    // on Windows/Linux it adds an unwanted "File" strip at the top of the agent window)
#ifdef __APPLE__
    wxMenuBar* menuBar = new wxMenuBar();
    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append( wxID_EXIT, "E&xit\tAlt-X", "Exit application" );

    menuBar->Append( fileMenu, "&File" );
    SetMenuBar( menuBar );
#endif

    // Set window icon (multiple sizes for title bar + taskbar)
    {
        wxIcon icon;
        wxIconBundle icon_bundle;

        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_agent, 16 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_agent, 32 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_agent, 48 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_agent, 128 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_agent, 256 ) );
        icon_bundle.AddIcon( icon );

        SetIcons( icon_bundle );
    }

    // Push initial UI state to webview after it loads
    // (Auth state, model list, chat title will be pushed once the page finishes loading)
    CallAfter( [this]()
    {
        UpdateAuthUI();

        // Push model list — include Claude Code if available
        std::vector<std::string> models = { "Claude 4.6 Opus", "Claude 4.6 Sonnet" };

        // Check if claude CLI is installed
        {
            bool found = false;
#ifdef __WXMSW__
            char pathBuf[MAX_PATH];
            found = SearchPathA( NULL, "claude.exe", NULL, MAX_PATH, pathBuf, NULL ) > 0
                    || SearchPathA( NULL, "claude.cmd", NULL, MAX_PATH, pathBuf, NULL ) > 0;

            if( !found )
            {
                // Check common install locations
                wxString home = wxGetHomeDir();
                found = wxFileName::FileExists( home + "\\.local\\bin\\claude.exe" );

                if( !found )
                {
                    const char* appData = getenv( "APPDATA" );
                    if( appData )
                        found = wxFileName::FileExists( wxString( appData ) + "\\npm\\claude.cmd" );
                }

                if( !found )
                {
                    const char* localAppData = getenv( "LOCALAPPDATA" );
                    if( localAppData )
                        found = wxFileName::FileExists(
                            wxString( localAppData ) + "\\Programs\\claude\\claude.exe" );
                }
            }
#else
            found = wxFileName::FileExists( "/usr/local/bin/claude" )
                    || wxFileName::FileExists( "/opt/homebrew/bin/claude" )
                    || wxFileName::FileExists( wxString( wxGetHomeDir() + "/.local/bin/claude" ) );
#endif
            m_ccPromoClaudeCodeAvailable = found;

            if( found )
                models.push_back( "Claude Code (Opus)" );
        }

        m_bridge->PushModelList( models, m_currentModel );

        // Push initial title
        m_bridge->PushChatTitle( "New Chat" );

        // Pre-fetch history list so first dropdown open is instant
        m_bridge->PushActiveChat( m_chatHistoryDb.GetConversationId() );
        m_bridge->PushHistoryList( BuildHistoryListJson() );

        // Push plan mode state
        m_bridge->PushPlanMode( m_agentMode == AgentMode::PLAN );

        // If the persisted model is Claude Code, initialize the CC backend now
        // (deferred to CallAfter so Pgm().GetApiServer() is available)
        if( m_currentModel == "Claude Code (Opus)" )
        {
            std::string savedModel = m_currentModel;
            m_currentModel = "";  // Force DoModelChange to not early-return
            DoModelChange( savedModel );
        }

        // Show onboarding empty state for new chat
        m_fullHtmlContent = BuildOnboardingHtml();
        m_showingOnboarding = true;
        SetHtml( m_fullHtmlContent );

        // Show Claude Code promo (detection is now complete)
        MaybeShowCcPromo();
    } );
}

AGENT_FRAME::~AGENT_FRAME()
{
#ifdef __APPLE__
    RemoveKeyboardMonitor();
#endif

    // Save CC chat history before destruction — the CC controller accumulates
    // messages in memory and they are only synced at turn boundaries. If the frame
    // is destroyed mid-conversation (e.g. project switch), we must persist now.
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        nlohmann::json ccHistory = m_ccController->GetChatHistory();

        if( !ccHistory.empty() )
        {
            m_chatHistory = ccHistory;
            m_chatHistoryDb.Save( m_chatHistory );
            wxLogInfo( "AGENT_FRAME::~AGENT_FRAME - saved CC history (%zu messages) before destruction",
                       m_chatHistory.size() );
        }
    }

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
        "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline thinking-link text-[12px]\">%s<span class=\"%s\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><polyline points=\"9 6 15 12 9 18\"/></svg></span></a>"
        "<div class=\"thinking-content text-text-muted text-[12px] mt-1 mb-0 pl-3 border-l border-border-dark whitespace-pre-wrap%s\" data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\">%s</div>"
        "</div>",
        m_currentThinkingIndex, thinkingText, ChevronClass( m_thinkingExpanded ), expandedClass, m_currentThinkingIndex, displayStyle, displayContent );
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

wxString AGENT_FRAME::BuildOnboardingHtml()
{
    // Detect project state to show contextual suggestions
    wxString projPath;

    try
    {
        projPath = Kiway().Prj().GetProjectPath();
    }
    catch( ... )
    {
    }

    bool hasProject = !projPath.IsEmpty();

    // Check for existing schematic/PCB files
    bool hasSch = false;
    bool hasPcb = false;

    if( hasProject )
    {
        wxDir dir( projPath );

        if( dir.IsOpened() )
        {
            wxString filename;

            if( dir.GetFirst( &filename, wxS( "*.kicad_sch" ), wxDIR_FILES ) )
                hasSch = true;

            if( dir.GetFirst( &filename, wxS( "*.kicad_pcb" ), wxDIR_FILES ) )
                hasPcb = true;
        }
    }

    // Check if user has previous conversations (returning user vs first time)
    bool hasHistory = false;

    if( hasProject )
    {
        auto historyList = m_chatHistoryDb.GetHistoryList( projPath.ToStdString() );
        hasHistory = historyList.size() > 1; // >1 because current empty chat counts as one
    }

    // Build the onboarding HTML
    wxString html;
    html += wxS( "<div class=\"onboard\">" );

    if( !hasProject )
    {
        // No project open
        html += wxS( "<div class=\"onboard-title\">Welcome to Zeo</div>" );
        html += wxS( "<div class=\"onboard-sub\">Open a KiCad project to get started, "
                     "or ask me anything about electronics design.</div>" );
        // fall through to shared chips
    }
    else if( !hasSch && !hasPcb )
    {
        // Empty project — no schematic or PCB files yet
        html += wxS( "<div class=\"onboard-title\">New Project</div>" );
        html += wxS( "<div class=\"onboard-sub\">This project is empty. "
                     "I can help you design a schematic, place components, or plan your circuit.</div>" );
        // fall through to shared chips
    }
    else if( hasSch && !hasPcb )
    {
        // Has schematic but no PCB
        html += wxS( "<div class=\"onboard-title\">What's next?</div>" );
        html += wxS( "<div class=\"onboard-sub\">"
                     "Your schematic is ready. I can run checks, update it, or help with the PCB layout.</div>" );
        // fall through to shared chips
    }
    else
    {
        // Has both schematic and PCB — existing project
        wxString greeting = hasHistory ? wxS( "New Chat" ) : wxS( "Welcome" );
        wxString subtitle = hasHistory
            ? wxS( "Start a new conversation about your design." )
            : wxS( "I can help you edit your schematic, route your PCB, run checks, and more." );

        html += wxString::Format( wxS( "<div class=\"onboard-title\">%s</div>" ), greeting );
        html += wxString::Format( wxS( "<div class=\"onboard-sub\">%s</div>" ), subtitle );
        // fall through to shared chips
    }

    // Same four suggestion chips for all states
    html += wxS( "<div class=\"onboard-grid\">" );
    html += wxS( "<a class=\"onboard-chip\" href=\"agent:suggest:Review%20my%20schematic\">"
                 "Review my schematic</a>" );
    html += wxS( "<a class=\"onboard-chip\" href=\"agent:suggest:Review%20my%20board\">"
                 "Review my board</a>" );
    html += wxS( "<a class=\"onboard-chip\" href=\"agent:suggest:Help%20me%20create%20something%20new\">"
                 "Help me create something new</a>" );
    html += wxS( "<a class=\"onboard-chip\" href=\"agent:suggest:Help%20me%20get%20ready%20for%20fabrication\">"
                 "Help me get ready for fabrication</a>" );
    html += wxS( "</div>" );

    // Keyboard shortcut hint
    html += wxS( "<div class=\"onboard-keys\">"
                 "<kbd>&#8984;</kbd> + <kbd>N</kbd> new chat"
                 "</div>" );

    html += wxS( "</div>" );

    return html;
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

    // Get current response from the active controller
    std::string currentResponse;

    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
        currentResponse = m_ccController->GetCurrentResponse();
    else if( m_chatController )
        currentResponse = m_chatController->GetCurrentResponse();

    // Strip leading newlines
    size_t start = currentResponse.find_first_not_of( "\n\r" );

    if( start != std::string::npos && start > 0 )
        currentResponse = currentResponse.substr( start );
    else if( start == std::string::npos )
        currentResponse.clear();

    // Thinking toggle appears above the response text
    if( !m_thinkingHtml.IsEmpty() )
        streamingContent += m_thinkingHtml;

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
        streamingContent += "<span style=\"color:#FFA500\">Compacting" + dots + "</span>";
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
                "<div class=\"tool-result-card rounded-lg my-2 max-w-full break-words\">"
                "<div class=\"py-2.5 px-3 flex items-center gap-2\">"
                "<span class=\"text-text-secondary text-[12px]\">%s</span>"
                "<span class=\"toggle-chevron\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><polyline points=\"9 6 15 12 9 18\"/></svg></span>"
                "<span class=\"text-text-muted text-[12px] ml-auto flex items-center gap-2\">"
                "<a href=\"agent:cancel_tool\" style=\"color:var(--text-muted); font-size:11px; "
                "text-decoration:none; opacity:0.7; cursor:pointer;\" "
                "onmouseover=\"this.style.opacity='1';this.style.color='var(--accent-red)'\" "
                "onmouseout=\"this.style.opacity='0.7';this.style.color='var(--text-muted)'\">Cancel</a>"
                "<span class=\"tool-spinner\"></span>"
                "</span>"
                "</div></div>",
                EscapeHtml( m_generatingToolName ) );
        }
        else
            streamingContent += "<span style=\"color:#888888\">" + dots + "</span>";
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
    if( !m_htmlUpdateNeeded )
        return;

    wxString streamingContent = BuildStreamingContent();

    // Always keep m_fullHtmlContent current so it's ready when user scrolls back
    wxString fullHtml = m_htmlBeforeAgentResponse;
    fullHtml.Replace( wxS( "<div id=\"streaming-content\"></div>" ), wxS( "" ) );
    fullHtml += wxS( "<div id=\"streaming-content\">" ) + streamingContent + wxS( "</div>" );
    if( !m_queuedBubbleHtml.IsEmpty() )
        fullHtml += m_queuedBubbleHtml;
    m_fullHtmlContent = fullHtml;

    // Only push to DOM if user is at the bottom
    if( !m_userScrolledUp )
    {
        m_htmlUpdateNeeded = false;
        m_bridge->PushStreamingContent( streamingContent );
    }
    // If user scrolled up, leave m_htmlUpdateNeeded=true so DOM updates when they scroll back
}

void AGENT_FRAME::StartGeneratingAnimation()
{
    m_isGenerating = true;
    m_isStreamingMarkdown = false;
    m_generatingDots = 1;
    m_generatingTimer.Start( 400 ); // Update every 400ms
    m_bridge->PushActionButtonState( "Stop" );

    // Immediately show the first frame of dots so there's no visual gap
    // between sending and the first timer tick
    FlushStreamingContentUpdate( true );
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

            // Provide send request function so handlers like pcb_autoroute and
            // generate_net_classes can communicate with editor frames via IPC.
            reg.SetSendRequestFn(
                [this]( int aFrameType, const std::string& aPayload ) -> std::string {
                    return SendRequest( aFrameType, aPayload );
                } );

            // Fire-and-forget IPC for commands that don't send MAIL_AGENT_RESPONSE
            // (take_snapshot, detect_changes).  Uses ExpressMail directly.
            reg.SetSendFireAndForgetFn(
                [this]( int aFrameType, const std::string& aPayload ) {
                    std::string payloadCopy = aPayload;
                    Kiway().ExpressMail( static_cast<FRAME_T>( aFrameType ),
                                         MAIL_AGENT_REQUEST, payloadCopy );
                } );
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

            auto& reg = TOOL_REGISTRY::Instance();

            if( reg.IsAsync( toolName ) )
            {
                // Async tool (e.g. pcb_autoroute): run via ExecuteAsync() and pump
                // the event loop so the UI stays responsive while Freerouting runs
                // in a background thread.  The result arrives via
                // EVT_TOOL_EXECUTION_COMPLETE → OnAsyncToolComplete, which sets
                // m_mcpAsyncResult when m_mcpAsyncPending is true.
                wxLogMessage( "AGENT_FRAME: MCP async execution for '%s'", toolName );

                m_mcpAsyncPending = true;
                m_mcpAsyncResult.clear();

                reg.ExecuteAsync( toolName, toolInput, /*toolUseId=*/"mcp", this );

                // Pump events until the async handler signals completion.
                // wxYield() processes pending events (including CallAfter callbacks
                // from the background thread) without blocking.
                while( m_mcpAsyncPending )
                {
                    wxYield();
                    wxMilliSleep( 10 );
                }

                wxLogMessage( "AGENT_FRAME: MCP async tool '%s' completed, result_len=%zu",
                              toolName, m_mcpAsyncResult.length() );

                aEvent.SetPayload( m_mcpAsyncResult );
            }
            else
            {
                std::string result = reg.Execute( toolName, toolInput );

                wxLogMessage( "AGENT_FRAME: MCP agent tool '%s' completed, result_len=%zu",
                              toolName, result.length() );

                aEvent.SetPayload( result );
            }
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

    // Save for retry on error
    m_lastSentText = text;

    // Reset scroll state for new user message - user sending indicates engagement at bottom
    m_userScrolledUp = false;

    // Clear onboarding content when user sends first message
    if( m_showingOnboarding )
    {
        m_fullHtmlContent.Clear();
        m_showingOnboarding = false;
    }

    // Build user message HTML
    wxString escapedText = text;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );

    wxString bubbleContent = FileAttach::BuildAttachmentBubbleHtml( m_pendingAttachments )
                             + escapedText;
    wxString msgHtml = wxString::Format(
        "<div class=\"user-msg-row my-3\"><div class=\"user-msg py-2 px-3.5 whitespace-pre-wrap break-words\">%s</div></div>",
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
    m_bridge->PushInputFocus();
    m_bridge->PushActionButtonState( "Stop" );

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
    m_runningHtmlByIdx.clear();
    m_activeToolResultIdx = -1;
    m_stopRequested = false;
    m_userScrolledUp = false;
    m_htmlUpdatePending = false;

    // ── Claude Code backend ──────────────────────────────────────────────
    if( m_backend == AgentBackend::CLAUDE_CODE )
    {
        if( m_ccController )
        {
            // Clear stale response before UI renders — otherwise the html update timer
            // can flash the previous turn's text in the new streaming div.
            m_ccController->ClearCurrentResponse();

            // Sync frame history into CC controller so it includes prior messages
            // (from either backend) before appending the new user message
            m_ccController->SetChatHistory( m_chatHistory );

            StartGeneratingAnimation();

            if( !m_ccController->SendMessage( text.ToStdString() ) )
            {
                wxLogWarning( "AGENT_FRAME: CC subprocess not running, restarting" );
                StopGeneratingAnimation();

                // Restart the subprocess — resume if we have a session ID, else new
                std::string sessionId = m_ccController->GetSessionId();

                if( !sessionId.empty() )
                    m_ccController->ResumeSession( sessionId );
                else
                    m_ccController->NewSession();

                m_ccController->SetChatHistory( m_chatHistory );
                StartGeneratingAnimation();

                if( !m_ccController->SendMessage( text.ToStdString() ) )
                {
                    wxLogError( "AGENT_FRAME: CC subprocess restart failed" );
                    StopGeneratingAnimation();
                    m_pendingAttachments.clear();
                    return;
                }
            }

            // Sync and save after user message is recorded
            m_chatHistory = m_ccController->GetChatHistory();
            m_chatHistoryDb.Save( m_chatHistory );

            // Generate title on first message (same as Zeo agent path)
            if( m_chatHistoryDb.GetTitle().empty() && m_chatController )
                m_chatController->RequestTitle( text.ToStdString() );
        }

        m_pendingAttachments.clear();
        return;
    }

    // ── Zeo Agent backend ────────────────────────────────────────────────

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

    // Transition frame's state machine (legacy - controller also has state machine)
    m_conversationCtx.Reset();
    m_conversationCtx.TransitionTo( AgentConversationState::WAITING_FOR_LLM );

    // Configure LLM client with selected model
    m_llmClient->SetModel( m_currentModel );

    // Sync agent target sheet with the user's current sheet on first message,
    // reset on subsequent messages so the agent can navigate freely
    {
        bool isFirstMessage = m_chatHistory.empty()
                              || std::none_of( m_chatHistory.begin(), m_chatHistory.end(),
                                               []( const nlohmann::json& msg )
                                               {
                                                   return msg.value( "role", "" ) == "user";
                                               } );

        if( isFirstMessage )
        {
            // Query the user's current sheet from the schematic editor and set it as target
            try
            {
                std::string resp = SendRequest( FRAME_SCH, "get_sch_sheets" );
                if( !resp.empty() )
                {
                    auto respJson = nlohmann::json::parse( resp, nullptr, false );
                    if( !respJson.is_discarded() && respJson.contains( "current_sheet_uuid" ) )
                    {
                        nlohmann::json payload;
                        payload["sheet_uuid"] = respJson["current_sheet_uuid"];
                        std::string payloadStr = payload.dump();

                        Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_TARGET_SHEET, payloadStr );

                        wxLogInfo( "AGENT_FRAME: Synced target sheet to user's current sheet: %s",
                                   respJson.value( "current_sheet", "unknown" ) );
                    }
                }
            }
            catch( const std::exception& e )
            {
                wxLogInfo( "AGENT_FRAME: Failed to sync target sheet on first msg: %s", e.what() );
            }
        }
        else
        {
            std::string emptyPayload;
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_RESET_TARGET_SHEET, emptyPayload );
        }
    }

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

    // Cancel any in-flight Python tool scripts and child processes (e.g., Freerouting)
    {
        TOOL_REGISTRY::Instance().CancelAll();
        std::string emptyPayload;
        Kiway().ExpressMail( FRAME_TERMINAL, MAIL_CANCEL_TOOL_EXECUTION, emptyPayload );
        wxLogInfo( "AGENT: Sent MAIL_CANCEL_TOOL_EXECUTION to terminal" );
    }

    // Signal to stop - affects tool execution loops and streaming callbacks (legacy)
    m_stopRequested = true;

    // Note: StopGeneratingAnimation() already did the final render without dots,
    // so we don't need to call UpdateAgentResponse() here (would restart timer with cleared state)

    // Sync frame's history from controller (controller handles orphaned tool_use blocks)
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        m_chatHistory = m_ccController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        UploadCurrentChat();
    }
    else if( m_chatController )
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

    // Replace ALL running tools with "Cancelled" in both DOM and internal HTML
    for( auto& [idx, runningHtml] : m_runningHtmlByIdx )
    {
        wxString desc = wxString( "Tool execution" );

        // Try to find description from m_toolDescByUseId via reverse lookup
        for( const auto& [useId, storedIdx] : m_toolIdxByUseId )
        {
            if( storedIdx == idx )
            {
                auto descIt = m_toolDescByUseId.find( useId );
                if( descIt != m_toolDescByUseId.end() )
                    desc = descIt->second;
                break;
            }
        }

        wxString cancelledHtml = BuildToolResultHtml( idx, desc,
            "var(--text-muted)", "User cancelled", "", false );
        m_fullHtmlContent.Replace( runningHtml, cancelledHtml );
        m_htmlBeforeAgentResponse.Replace( runningHtml, cancelledHtml );

        m_bridge->PushToolResultUpdate( idx, "text-text-muted", "Cancelled",
            "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap "
            "break-words m-0 mt-2\">User cancelled</pre>" );
    }
    m_runningHtmlByIdx.clear();

    // Safety net: cancel ALL remaining "Running..." tool statuses in the DOM.
    // Handles edge cases where queued tool events created "Running..." elements
    // that aren't tracked by m_activeToolResultIdx.
    m_bridge->PushCancelRunningTools();

    // Clear pending editor open/close request state (prevents stale approval button clicks)
    m_pendingOpenSch = false;
    m_pendingOpenPcb = false;
    m_pendingOpenToolId.clear();
    m_pendingOpenFilePath.Clear();

    // Clear pending ERC request state
    m_pendingERCToolId.clear();
    m_pendingERCInput.clear();

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
    m_runningHtmlByIdx.clear();
    m_activeToolResultIdx = -1;

    if( aShowStopped )
    {
        // NOTE: Don't reset m_toolResultCounter - old tool-result-N IDs persist in
        // m_fullHtmlContent. Counter must stay monotonically increasing to avoid
        // duplicate DOM IDs that cause subsequent tool updates to target stale elements.
        AppendHtml( "<div class=\"text-text-muted text-[12px] mb-1\">Stopped</div>" );
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

    // Clear input and refocus (button stays "Stop" during generation)
    m_bridge->PushInputClear();
    m_bridge->PushInputFocus();

    // Update JS-side queue count so up-arrow can prioritize editing queued messages
    m_bridge->PushQueueCount( static_cast<int>( m_queuedMessages.size() ) );
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
        "<div id=\"queued-msg\" class=\"queued-msg user-msg-row my-3\" style=\"opacity:0.5;\">"
        "<div class=\"user-msg py-2 px-3.5 whitespace-pre-wrap break-words\">%s</div></div>",
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
        m_bridge->PushQueueCount( 0 );
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
    m_runningHtmlByIdx.clear();
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

    m_bridge->PushQueueCount( 0 );
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

void AGENT_FRAME::OnBridgeEditQueued()
{
    if( m_queuedMessages.empty() )
        return;

    wxLogInfo( "AGENT_FRAME::OnBridgeEditQueued - popping last queued message for editing (queue size=%zu)",
               m_queuedMessages.size() );

    // Pop the last queued message and put its text back in the input
    QueuedMessage msg = std::move( m_queuedMessages.back() );
    m_queuedMessages.pop_back();

    // Set input text to the popped message
    m_bridge->PushInputSetText( msg.text );

    // Re-add any attachments from the popped message
    for( const auto& att : msg.attachments )
        m_bridge->PushAddAttachment( att.base64_data, att.media_type, att.filename );

    // Rebuild or remove the queued bubble
    if( m_queuedMessages.empty() )
    {
        // No more queued messages — remove the bubble entirely
        if( !m_queuedBubbleHtml.IsEmpty() )
            m_fullHtmlContent.Replace( m_queuedBubbleHtml, "" );

        m_bridge->PushRemoveAllQueuedMessages();
        m_queuedBubbleHtml.Clear();
    }
    else
    {
        // Still have queued messages — rebuild the combined bubble
        RebuildQueuedBubble();
    }

    m_bridge->PushQueueCount( static_cast<int>( m_queuedMessages.size() ) );
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
                m_bridge->PushShowToast( error, "error" );
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
    else if( href == "agent:approve_erc" )
        OnApproveRunERC();
    else if( href == "agent:reject_erc" )
        OnRejectRunERC();
    else if( href == "agent:open_simulator" )
        OnOpenSimulator();
    else if( href == "agent:retry" )
        OnRetryLastMessage();
    else if( href == "agent:cancel_tool" )
        DoCancelOperation( true );
    else if( href.StartsWith( "agent:suggest:" ) )
    {
        // Suggestion chip clicked — populate input and auto-send
        wxString text = href.Mid( 14 );  // skip "agent:suggest:"
        text = wxURI::Unescape( text );
        m_pendingInputText = text;
        wxCommandEvent evt;
        OnSend( evt );
    }
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
    bool wasScrolledUp = m_userScrolledUp;
    m_userScrolledUp = !coupled;

    // When user scrolls back to bottom, immediately flush any pending DOM updates
    if( wasScrolledUp && coupled && m_htmlUpdateNeeded )
        FlushStreamingContentUpdate( true );
}

nlohmann::json AGENT_FRAME::BuildHistoryListJson( const wxString& aFilter )
{
    std::string projectPath = Kiway().Prj().GetProjectPath().ToStdString();
    auto historyList = m_chatHistoryDb.GetHistoryList( projectPath );

    wxString lowerFilter = aFilter.Lower();
    nlohmann::json entries = nlohmann::json::array();

    for( const auto& entry : historyList )
    {
        if( !aFilter.IsEmpty() )
        {
            wxString title = wxString::FromUTF8( entry.title );
            if( !title.Lower().Contains( lowerFilter ) )
                continue;
        }

        nlohmann::json e;
        e["id"] = entry.id;
        e["title"] = entry.title.empty() ? "Untitled Chat" : entry.title;
        e["timestamp"] = entry.lastUpdated;
        entries.push_back( e );
    }

    return entries;
}

void AGENT_FRAME::OnBridgeHistoryOpen()
{
    m_bridge->PushActiveChat( m_chatHistoryDb.GetConversationId() );
    m_bridge->PushHistoryList( BuildHistoryListJson() );
}

void AGENT_FRAME::OnBridgeHistorySearch( const wxString& aQuery )
{
    m_bridge->PushHistoryList( BuildHistoryListJson( aQuery ) );
}

void AGENT_FRAME::DoHistoryDelete( const std::string& aConversationId )
{
    wxLogInfo( "AGENT_FRAME::DoHistoryDelete - id: %s", aConversationId.c_str() );

    bool deleted = m_chatHistoryDb.DeleteConversation( aConversationId );

    if( deleted )
    {
        // If we just deleted the active chat, start a new one
        if( aConversationId == m_chatHistoryDb.GetConversationId() )
        {
            DoNewChat();
        }

        // Refresh the history list
        m_bridge->PushHistoryList( BuildHistoryListJson() );
    }
}

// ── Bridge-triggered actions ────────────────────────────────────────────

void AGENT_FRAME::DoModelChange( const std::string& aModel )
{
    wxLogInfo( "AGENT_FRAME::DoModelChange - model: %s", aModel.c_str() );

    if( aModel == m_currentModel )
        return;

    m_currentModel = aModel;

    if( aModel == "Claude Code (Opus)" )
    {
        m_backend = AgentBackend::CLAUDE_CODE;

        if( !m_ccController )
            m_ccController = std::make_unique<CC_CONTROLLER>( this );

        // Determine working directory (project dir or home)
        wxString workDir = wxGetHomeDir();

        try
        {
            wxString projPath = Prj().GetProjectPath();
            if( !projPath.IsEmpty() )
                workDir = projPath;
        }
        catch( ... ) {}

        // Get API socket path for MCP config
        std::string apiSocketPath;
        try
        {
            apiSocketPath = Pgm().GetApiServer().SocketPath();
        }
        catch( ... )
        {
            wxLogWarning( "AGENT_FRAME: Could not get API socket path for CC MCP config" );
        }

        // Resolve Python3 path for MCP server.
        // Prefer the bundled Python 3.10 (has kipy + mcp SDK installed),
        // fall back to system Python. If neither found, GenerateMcpConfig()
        // will fall back to uvx zeo-mcp.
        std::string pythonPath;
        {
#ifdef __WXMSW__
            // Windows: find system Python from PATH
            char pathBuf[MAX_PATH];

            if( SearchPathA( NULL, "python3.exe", NULL, MAX_PATH, pathBuf, NULL ) > 0 )
                pythonPath = pathBuf;
            else if( SearchPathA( NULL, "python.exe", NULL, MAX_PATH, pathBuf, NULL ) > 0 )
                pythonPath = pathBuf;

            if( pythonPath.empty() )
                wxLogWarning( "AGENT_FRAME: Python3 not found in PATH for CC MCP config" );
            else
                wxLogInfo( "AGENT_FRAME: Found Python at %s", pythonPath.c_str() );
#else
            // Unix (macOS + Linux): search candidate paths
            std::vector<wxString> candidates;

#if defined(__WXOSX__)
            // macOS: try bundled Python first, then Homebrew, then system
            {
                wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
                wxString bundledPython = exePath.GetPath() + "/../Frameworks/Python.framework/Versions/3.10/bin/python3";
                candidates = { bundledPython, "/opt/homebrew/bin/python3", "/usr/local/bin/python3", "/usr/bin/python3" };
            }
#else
            // Linux: system Python (available in Docker and AppImage environments)
            candidates = { "/usr/bin/python3", "/usr/local/bin/python3" };
#endif

            for( const auto& candidate : candidates )
            {
                if( wxFileName::FileExists( candidate ) )
                {
                    pythonPath = candidate.ToStdString();
                    break;
                }
            }

            if( pythonPath.empty() )
                wxLogWarning( "AGENT_FRAME: No Python3 found for CC MCP config" );
            else
                wxLogInfo( "AGENT_FRAME: Found Python at %s", pythonPath.c_str() );
#endif
        }

        wxLogInfo( "AGENT_FRAME: CC start - socket=%s, python=%s",
                   apiSocketPath.c_str(), pythonPath.c_str() );

        m_ccController->Start( workDir.ToStdString(), apiSocketPath, pythonPath );

        // Hide plan button in Claude Code mode (CC manages its own planning)
        m_bridge->PushPlanMode( false );

        wxLogInfo( "AGENT_FRAME::DoModelChange - switched to Claude Code backend" );
    }
    else
    {
        // Switching away from Claude Code — commit history before killing CC
        if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
        {
            m_chatHistory = m_ccController->GetChatHistory();
            m_chatHistoryDb.Save( m_chatHistory );
            m_ccController->Cancel();
        }

        m_backend = AgentBackend::ZEO_AGENT;

        if( m_chatController )
            m_chatController->SetModel( m_currentModel );
    }

    SaveModelPreference( m_currentModel );
}

void AGENT_FRAME::DoSendClick()
{
    wxCommandEvent evt;
    OnSend( evt );
}

void AGENT_FRAME::DoStopClick()
{
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        m_ccController->Cancel();

        // Cancel any in-flight Python tool scripts and child processes
        TOOL_REGISTRY::Instance().CancelAll();
        std::string emptyPayload;
        Kiway().ExpressMail( FRAME_TERMINAL, MAIL_CANCEL_TOOL_EXECUTION, emptyPayload );
        wxLogInfo( "AGENT: DoStopClick — sent MAIL_CANCEL_TOOL_EXECUTION to terminal" );

        StopGeneratingAnimation();
        m_bridge->PushActionButtonState( "Send", true );
        return;
    }

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

void AGENT_FRAME::DoAutoApproveToggle()
{
    m_autoApprove = !m_autoApprove;
    m_bridge->PushAutoApprove( m_autoApprove );

    wxLogInfo( "AGENT_FRAME: Auto-approve mode %s", m_autoApprove ? "enabled" : "disabled" );
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

    if( !m_auth )
        return;

#ifndef __WXMAC__
    m_auth->SetCallbackHandler( this );

    if( !m_authCallbackBound )
    {
        Bind( EVT_AUTH_CALLBACK, &AGENT_FRAME::OnAuthCallback, this );
        m_authCallbackBound = true;
    }

    std::string authUrl;
    m_auth->StartOAuthFlow( "agent", &authUrl );

    // Show fallback dialog with clickable link (browser may not auto-open on Linux/Docker)
    if( !authUrl.empty() )
    {
        wxDialog dlg( this, wxID_ANY, _( "Sign In" ), wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_DIALOG_STYLE );
        wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

        sizer->Add( new wxStaticText( &dlg, wxID_ANY,
                _( "If the browser did not open automatically,\nclick the link below or copy it to your browser:" ) ),
                0, wxALL, 16 );

        wxString wxUrl = wxString::FromUTF8( authUrl );

        wxHyperlinkCtrl* link = new wxHyperlinkCtrl( &dlg, wxID_ANY, _( "Open sign-in page" ), wxUrl );
        sizer->Add( link, 0, wxLEFT | wxRIGHT, 16 );

        wxButton* copyBtn = new wxButton( &dlg, wxID_ANY, _( "Copy Link" ) );
        copyBtn->Bind( wxEVT_BUTTON, [wxUrl]( wxCommandEvent& ) {
            if( wxTheClipboard->Open() )
            {
                wxTheClipboard->SetData( new wxTextDataObject( wxUrl ) );
                wxTheClipboard->Close();
            }
        } );
        sizer->Add( copyBtn, 0, wxALL, 16 );

        sizer->Add( dlg.CreateStdDialogButtonSizer( wxOK ), 0, wxALL | wxEXPAND, 8 );

        dlg.SetSizer( sizer );
        dlg.Fit();
        dlg.CentreOnParent();
        dlg.ShowModal();
    }
#else
    m_auth->StartOAuthFlow( "agent" );
#endif
}

#ifndef __WXMAC__
void AGENT_FRAME::OnAuthCallback( wxCommandEvent& aEvent )
{
    std::string callbackUrl = aEvent.GetString().ToStdString();
    std::string errorMsg;

    if( m_auth && m_auth->HandleOAuthCallback( callbackUrl, errorMsg ) )
    {
        UpdateAuthUI();

        if( IsIconized() )
            Iconize( false );

        Show( true );
        Raise();
    }
    else
    {
        wxMessageBox( _( "Sign in failed: " ) + errorMsg, _( "Error" ), wxOK | wxICON_ERROR );
    }
}
#endif

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
    // Reentrancy guard: wxYield() in the polling loop below can process events that
    // trigger ExecuteAllTools() → another SendRequest(). The shared m_toolResponse
    // variable would be clobbered. Block the inner call with a clear error instead.
    static bool s_inSendRequest = false;

    struct ReentrancyGuard
    {
        bool& flag;
        ReentrancyGuard( bool& f ) : flag( f ) { flag = true; }
        ~ReentrancyGuard() { flag = false; }
    };

    if( s_inSendRequest )
    {
        wxLogWarning( "AGENT: SendRequest - reentrant call blocked (frame %d)", aDestFrame );
        return "Error: Concurrent tool execution blocked. Please retry.";
    }

    ReentrancyGuard guard( s_inSendRequest );

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
        dir.RemoveLastDir();  // python/ -> tools/
        dir.RemoveLastDir();  // tools/  -> agent/
        dir.AppendDir( "prompts" );
        promptsDir = dir.GetPath().ToStdString();
    }
    else
    {
        wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
        wxFileName dir( exePath.GetPath(), "" );
#if defined( __WXMAC__ )
        dir.RemoveLastDir();                  // remove MacOS
        dir.AppendDir( "SharedSupport" );
        dir.AppendDir( "agent" );
        dir.AppendDir( "prompts" );
#elif defined( __WXMSW__ )
        dir.AppendDir( "agent" );
        dir.AppendDir( "prompts" );
#else
        // Linux: installed under KICAD_DATA to avoid conflicting with the agent executable
        dir.AssignDir( PATHS::GetStockDataPath() );
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



// ============================================================================
// Async LLM Streaming Event Handlers
// ============================================================================

void AGENT_FRAME::StartAsyncLLMRequest()
{
    // Don't reset m_userScrolledUp here — respect the user's scroll position.
    // If they scrolled up to read, let them stay there. The "Jump to bottom"
    // button in the UI lets them re-engage when ready. Streaming content is
    // still accumulated in m_fullHtmlContent even when DOM updates are skipped.

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
        AppendHtml( "<p><span style=\"color:var(--accent-red)\">Error: Failed to start LLM request</span></p>" );
        m_conversationCtx.TransitionTo( AgentConversationState::IDLE );
        m_bridge->PushActionButtonState( "Send" );
    }
}


void AGENT_FRAME::OnRetryLastMessage()
{
    if( m_lastSentText.IsEmpty() )
        return;

    // Re-send through normal input flow so the message re-appears in chat and history
    m_pendingInputText = m_lastSentText;
    m_lastSentText.Clear();

    // Trigger the send path (same as user pressing Send)
    wxCommandEvent evt;
    OnSend( evt );
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

    bool isBusy = false;

    if( m_backend == AgentBackend::CLAUDE_CODE )
        isBusy = m_ccController && m_ccController->IsBusy();
    else
        isBusy = m_chatController ? m_chatController->IsBusy()
                                  : ( m_isGenerating || m_conversationCtx.GetState() != AgentConversationState::IDLE );

    if( isBusy )
    {
        wxMessageBox( _( "Please wait for the current response to complete before starting a new chat." ),
                      _( "Chat in Progress" ), wxOK | wxICON_INFORMATION );
        return;
    }

    // Save current CC history before starting a new session — CC accumulates
    // messages in memory and they may not have been persisted yet.
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        nlohmann::json ccHistory = m_ccController->GetChatHistory();

        if( !ccHistory.empty() )
        {
            m_chatHistory = ccHistory;
            m_chatHistoryDb.Save( m_chatHistory );
            wxLogInfo( "AGENT_FRAME::DoNewChat - saved CC history (%zu messages) before new chat",
                       m_chatHistory.size() );
        }

        m_ccController->NewSession();
    }
    else if( m_chatController )
    {
        m_chatController->NewChat();
    }

    m_chatHistory = nlohmann::json::array();
    m_apiContext = nlohmann::json::array();

    // UI reset - show onboarding for empty chat
    m_fullHtmlContent = BuildOnboardingHtml();
    m_showingOnboarding = true;
    SetHtml( m_fullHtmlContent );
    m_chatHistoryDb.StartNewConversation();
    m_chatHistoryDb.SetProjectPath( Kiway().Prj().GetProjectPath().ToStdString() );

    // Sync conversation ID to controller so it can persist streaming snapshots
    if( m_chatController )
        m_chatController->SetChatId( m_chatHistoryDb.GetConversationId() );

    m_bridge->PushChatTitle( "New Chat" );
    m_bridge->PushActiveChat( m_chatHistoryDb.GetConversationId() );

    // Clear historical thinking state
    m_historicalThinking.clear();
    m_historicalThinkingExpanded.clear();
    m_historicalToolResultExpanded.clear();
    m_currentThinkingIndex = -1;
    m_toolResultCounter = 0;
    m_queuedBubbleHtml.Clear();
    m_runningHtmlByIdx.clear();
    m_activeToolResultIdx = -1;

    // Clear active streaming/thinking state
    m_thinkingContent.Clear();
    m_thinkingHtml.Clear();
    m_toolCallHtml.Clear();
    m_thinkingExpanded = false;
    m_thinkingHtmlDirty = false;
    m_isThinking = false;
    m_currentResponse.clear();

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
    m_runningHtmlByIdx.clear();
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

    m_bridge->PushActiveChat( aConversationId );
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
        // Render saved error messages
        if( msg.contains( "_error" ) && msg["_error"] == true )
        {
            std::string errorMsg = msg.value( "message", "An error occurred." );
            m_fullHtmlContent += wxString::Format(
                "<div class=\"rounded-lg p-3 my-2\" style=\"background:rgba(232,85,85,0.08); "
                "border:1px solid rgba(232,85,85,0.2);\">"
                "<p class=\"text-accent-red text-[13px] m-0\">%s</p>"
                "</div>",
                wxString::FromUTF8( errorMsg ) );
            continue;
        }

        if( !msg.contains( "role" ) || !msg.contains( "content" ) )
            continue;

        // Render a divider for compaction markers (not shown as a chat message)
        if( msg.contains( "_compaction" ) && msg["_compaction"] == true )
        {
            m_fullHtmlContent +=
                "<div class=\"flex items-center my-4\">"
                "<div class=\"flex-1 border-t border-border-dark\"></div>"
                "<span class=\"mx-3 text-text-muted text-[11px]\">Context compacted</span>"
                "<div class=\"flex-1 border-t border-border-dark\"></div>"
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
                    "<div class=\"user-msg-row my-3\"><div class=\"user-msg py-2 px-3.5 whitespace-pre-wrap break-words\">%s</div></div>",
                    display );
            }
            else if( role == "assistant" )
            {
                // Strip *(Stopped)* marker and render as styled div to match live stop
                bool wasStopped = StripStoppedMarker( content );
                m_fullHtmlContent += AgentMarkdown::ToHtml( content );
                if( wasStopped )
                    m_fullHtmlContent += "<div class=\"text-text-muted text-[12px] mb-1\">Stopped</div>";
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
                            m_fullHtmlContent += "<div class=\"text-text-muted text-[12px] mb-1\">Stopped</div>";
                    }
                    else if( role == "user" )
                    {
                        // Right-aligned speech bubble style for user messages
                        display.Replace( "&", "&amp;" );
                        display.Replace( "<", "&lt;" );
                        display.Replace( ">", "&gt;" );
                        display.Replace( "\n", "<br>" );
                        m_fullHtmlContent += wxString::Format(
                            "<div class=\"user-msg-row my-3\"><div class=\"user-msg py-2 px-3.5 whitespace-pre-wrap break-words\">%s</div></div>",
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
                            "<a href=\"toggle:thinking:%d\" class=\"text-text-muted cursor-pointer no-underline thinking-link text-[12px]\">Thinking<span class=\"%s\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><polyline points=\"9 6 15 12 9 18\"/></svg></span></a>"
                            "<div class=\"thinking-content text-text-muted text-[12px] mt-1 mb-0 pl-3 border-l border-border-dark whitespace-pre-wrap%s\" data-toggle-type=\"thinking\" data-toggle-index=\"%d\" style=\"display:%s;\">%s</div>"
                            "</div>",
                            thinkingIndex, ChevronClass( expanded ), expandedClass, thinkingIndex, displayStyle, escapedText );
                    }
                }
                else if( blockType == "server_tool_use" )
                {
                    // Render server-side tool (e.g., web_search) with its result
                    std::string toolName = block.value( "name", "unknown" );
                    nlohmann::json toolInput = block.value( "input", nlohmann::json::object() );

                    // Build description from tool input
                    wxString desc;
                    if( toolName == "web_search" && toolInput.contains( "query" ) )
                        desc = "Searching: " + wxString::FromUTF8( toolInput["query"].get<std::string>() );
                    else if( toolName == "web_search" )
                        desc = "Searching the web";
                    else
                        desc = wxString::FromUTF8( toolName );

                    // Look ahead for matching result block (<name>_tool_result)
                    std::string expectedResultType = toolName + "_tool_result";
                    wxString resultText;

                    for( const auto& resultBlock : msg["content"] )
                    {
                        if( resultBlock.value( "type", "" ) == expectedResultType
                            && resultBlock.contains( "content" )
                            && resultBlock["content"].is_array() )
                        {
                            int total = 0;
                            int shown = 0;

                            for( const auto& entry : resultBlock["content"] )
                            {
                                std::string title = entry.value( "title", "" );
                                std::string url = entry.value( "url", "" );
                                if( title.empty() )
                                    continue;

                                total++;

                                if( shown < 2 )
                                {
                                    if( !resultText.IsEmpty() )
                                        resultText += "\n";
                                    resultText += "- " + wxString::FromUTF8( title );
                                    if( !url.empty() )
                                        resultText += " (" + wxString::FromUTF8( url ) + ")";
                                    shown++;
                                }
                            }

                            if( total > shown )
                                resultText += wxString::Format( "\n+%d more sources", total - shown );

                            break;
                        }
                    }

                    if( resultText.IsEmpty() )
                        resultText = "(completed)";

                    m_fullHtmlContent += BuildToolResultHtml( m_toolResultCounter, desc,
                                                             "var(--accent-green)",
                                                             resultText, wxEmptyString, false );
                    m_toolResultCounter++;
                }
                else if( blockType.find( "_tool_result" ) != std::string::npos )
                {
                    // Skip server tool result blocks - already rendered with server_tool_use above
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

                    wxString statusColor;

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

                    if( isPythonError || isError )
                        statusColor = "var(--accent-red)";
                    else
                        statusColor = "var(--accent-green)";

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
                                                             statusColor, fullFormatted,
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
                            "style=\"background:rgba(78,201,176,0.12); color:#5bbda6; padding:5px 14px; "
                            "border-radius:8px; font-size:12px; font-weight:500; "
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

    if( authenticated )
        MaybeShowCcPromo();
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

    std::string configPath = AGENT_AUTH::GetSupabaseConfigPath();

    if( !configPath.empty() )
    {
        std::ifstream configFile( configPath );

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
    // Auto-approve mode: skip the approval UI and approve directly
    if( m_autoApprove )
    {
        wxLogInfo( "AGENT_FRAME: Auto-approving open editor (%s)", aEditorType );
        OnApproveOpenEditor();
        // Re-raise agent frame so it stays on top of the newly opened editor
        Raise();
        return;
    }

    int idx = m_activeToolResultIdx;
    if( idx < 0 )
        return;

    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Open editor" ) : m_lastToolDesc;
    wxString approvalHtml = BuildToolApprovalHtml( idx, desc,
                                                    "Open", "agent:approve_open",
                                                    "#1a3d1a", "#4ade80" );

    // Replace the running tool box with the approval box in internal HTML
    auto it = m_runningHtmlByIdx.find( idx );
    if( it != m_runningHtmlByIdx.end() )
    {
        m_fullHtmlContent.Replace( it->second, approvalHtml );
        m_htmlBeforeAgentResponse.Replace( it->second, approvalHtml );
        it->second = approvalHtml;  // Track updated HTML for subsequent replacements
    }

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


//=============================================================================
// ERC Approval
//=============================================================================

void AGENT_FRAME::ShowERCApproval()
{
    // Auto-approve mode: skip the approval UI and approve directly
    if( m_autoApprove )
    {
        wxLogInfo( "AGENT_FRAME: Auto-approving run ERC" );
        OnApproveRunERC();
        Raise();
        return;
    }

    int idx = m_activeToolResultIdx;
    if( idx < 0 )
        return;

    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Run ERC" ) : m_lastToolDesc;
    wxString approvalHtml = BuildToolApprovalHtml( idx, desc,
                                                    "Run ERC", "agent:approve_erc",
                                                    "#1a3d1a", "#4ade80" );

    // Replace the running tool box with the approval box in internal HTML
    auto it = m_runningHtmlByIdx.find( idx );
    if( it != m_runningHtmlByIdx.end() )
    {
        m_fullHtmlContent.Replace( it->second, approvalHtml );
        m_htmlBeforeAgentResponse.Replace( it->second, approvalHtml );
        it->second = approvalHtml;
    }

    SetHtml( m_fullHtmlContent );
}


void AGENT_FRAME::OnApproveRunERC()
{
    wxLogInfo( "AGENT_FRAME::OnApproveRunERC called" );

    // Validate that we still have a pending request with a valid tool ID
    if( m_pendingERCToolId.empty() )
    {
        wxLogWarning( "OnApproveRunERC: empty tool ID - ignoring stale click" );
        return;
    }

    // Validate that the controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( m_pendingERCToolId ) )
    {
        wxLogWarning( "OnApproveRunERC: tool %s no longer pending - ignoring stale click",
                      m_pendingERCToolId.c_str() );
        m_pendingERCToolId.clear();
        m_pendingERCInput.clear();
        return;
    }

    std::string toolId = m_pendingERCToolId;
    nlohmann::json input = m_pendingERCInput;
    m_pendingERCToolId.clear();
    m_pendingERCInput.clear();

    // Execute the ERC tool via the normal tool registry
    TOOL_REGISTRY::Instance().SetSendRequestFn(
        [this]( int aFrameType, const std::string& aPayload ) -> std::string {
            return SendRequest( aFrameType, aPayload );
        } );
    TOOL_REGISTRY::Instance().SetSendFireAndForgetFn(
        [this]( int aFrameType, const std::string& aPayload ) {
            std::string payloadCopy = aPayload;
            Kiway().ExpressMail( static_cast<FRAME_T>( aFrameType ),
                                 MAIL_AGENT_REQUEST, payloadCopy );
        } );

    std::string result;
    bool success = false;

    try
    {
        result = TOOL_REGISTRY::Instance().ExecuteToolSync( "sch_run_erc", input );
        success = !result.empty() && result.find( "Error:" ) != 0;
    }
    catch( const std::exception& e )
    {
        wxLogError( "OnApproveRunERC: exception during ERC execution: %s", e.what() );
        result = std::string( "Error: ERC execution failed with exception: " ) + e.what();
        success = false;
    }
    catch( ... )
    {
        wxLogError( "OnApproveRunERC: unknown exception during ERC execution" );
        result = "Error: ERC execution failed with unknown exception";
        success = false;
    }

    if( m_chatController )
        m_chatController->HandleToolResult( toolId, result, success );
}


void AGENT_FRAME::OnRejectRunERC()
{
    wxLogInfo( "AGENT_FRAME::OnRejectRunERC called" );

    // Validate that we still have a pending request
    if( m_pendingERCToolId.empty() )
    {
        wxLogWarning( "OnRejectRunERC: empty tool ID - ignoring stale click" );
        return;
    }

    std::string toolId = m_pendingERCToolId;
    m_pendingERCToolId.clear();
    m_pendingERCInput.clear();

    // Validate controller still has this tool pending
    if( m_chatController && !m_chatController->HasPendingTool( toolId ) )
    {
        wxLogWarning( "OnRejectRunERC: tool %s no longer pending", toolId.c_str() );
        return;
    }

    if( m_chatController )
        m_chatController->HandleToolResult( toolId, "User declined to run ERC", false );
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

    // If compaction just finished, sync the compaction marker into frame history
    // so the "Context compacted" divider renders and persists
    if( m_isCompacting && m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        RenderChatHistory();
    }
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

    // If compaction just finished, sync the compaction marker into frame history
    if( m_isCompacting && m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        RenderChatHistory();
    }
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
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
        m_ccController->ClearCurrentResponse();
    else if( m_chatController )
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
        wxString runningHtml = BuildRunningToolHtml( idx, desc );
        m_runningHtmlByIdx[idx] = runningHtml;
        m_activeToolResultIdx = idx;

        // Map tool ID to DOM index so OnChatToolComplete can find the right element
        if( !data->toolId.empty() )
        {
            m_toolIdxByUseId[data->toolId] = idx;
            // Also store description by tool ID for OnChatToolComplete lookup
            m_toolDescByUseId[data->toolId] = desc;
        }

        wxLogInfo( "AGENT_FRAME::OnChatToolStart - assigned idx=%d (counter now %d)",
                   idx, m_toolResultCounter );

        // Append running box to permanent DOM
        AppendHtml( runningHtml );

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

    // sch_run_erc — approval-gated to prevent UI hangs on large projects
    if( data->toolName == "sch_run_erc" )
    {
        m_pendingERCToolId = data->toolId;
        m_pendingERCInput = data->input;
        ShowERCApproval();
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

    // Skip queued events that arrive after cancellation
    if( m_stopRequested )
    {
        delete data;
        return;
    }

    wxLogInfo( "AGENT_FRAME::OnChatToolComplete - tool: %s, success: %s",
            data->toolName.c_str(), data->success ? "true" : "false" );

    // Determine status color for dot indicator
    wxString statusColor;

    if( data->isPythonError || !data->success )
        statusColor = "var(--accent-red)";
    else
        statusColor = "var(--accent-green)";

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

    // Look up the DOM index for this tool by its ID.
    // With parallel tools, m_activeToolResultIdx may point to a different tool,
    // so we always prefer the per-tool map lookup.
    int idx = m_activeToolResultIdx;
    wxString desc = m_lastToolDesc.IsEmpty() ? wxString( "Tool execution" ) : m_lastToolDesc;

    if( !data->toolId.empty() )
    {
        auto idxIt = m_toolIdxByUseId.find( data->toolId );

        if( idxIt != m_toolIdxByUseId.end() )
        {
            idx = idxIt->second;
            m_toolIdxByUseId.erase( idxIt );
        }

        // Use per-tool description instead of shared m_lastToolDesc
        auto descIt = m_toolDescByUseId.find( data->toolId );
        if( descIt != m_toolDescByUseId.end() )
            desc = descIt->second;
    }

    // Build the text-only body content (no image - image is appended separately to avoid
    // passing megabytes of base64 data in a single JS string literal)
    wxString textBody = wxString::Format(
        "<pre class=\"text-text-secondary font-mono text-[12px] whitespace-pre-wrap "
        "break-words m-0 mt-2\">%s</pre>",
        fullFormatted );

    // Update status and text body in the existing DOM element via bridge
    m_bridge->PushToolResultUpdate( idx, statusColor, wxEmptyString, textBody );

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
    wxString completedHtml = BuildToolResultHtml( idx, desc, statusColor,
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

    // Replace THIS tool's running HTML with completed HTML in internal tracking
    auto runIt = m_runningHtmlByIdx.find( idx );
    if( runIt != m_runningHtmlByIdx.end() )
    {
        size_t prevLen = m_fullHtmlContent.length();
        m_fullHtmlContent.Replace( runIt->second, completedHtml );
        bool replaced = ( m_fullHtmlContent.length() != prevLen );
        m_htmlBeforeAgentResponse.Replace( runIt->second, completedHtml );
        m_runningHtmlByIdx.erase( runIt );

        if( !replaced )
            wxLogWarning( "AGENT_FRAME::OnChatToolComplete - Replace FAILED for idx=%d "
                          "(running HTML not found in m_fullHtmlContent)", idx );
    }

    // Safety net: if this tool had an image, re-push the status update on the next
    // event loop iteration. The 50+ image chunk scripts can delay or disrupt the
    // original updateToolResult call; this idempotent re-push ensures the DOM reflects
    // the completed state.
    if( data->hasImage && !data->imageBase64.empty() )
    {
        wxString safetyColor = statusColor;
        wxString safetyBody = textBody;
        int safetyIdx = idx;

        CallAfter( [this, safetyIdx, safetyColor, safetyBody]() {
            m_bridge->PushToolResultUpdate( safetyIdx, safetyColor, wxEmptyString,
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

    if( m_mcpAsyncPending )
    {
        // MCP path: store the result and signal the event-pump loop in the
        // MAIL_MCP_EXECUTE_AGENT_TOOL handler to stop spinning.
        m_mcpAsyncResult = result->result;
        m_mcpAsyncPending = false;
    }
    else
    {
        // Normal agent chat path: forward to the chat controller
        if( m_chatController )
        {
            m_chatController->HandleToolResult( result->tool_use_id, result->result,
                                                result->success );
        }
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
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController && !continuing )
    {
        m_chatHistory = m_ccController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        UploadCurrentChat();
        wxLogInfo( "AGENT_FRAME::OnChatTurnComplete - saved CC history (%zu messages)", m_chatHistory.size() );
    }
    else if( m_chatController )
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
        if( m_autoApprove )
        {
            wxLogInfo( "AGENT_FRAME: Auto-approving plan" );
            DoPlanApprove();
        }
        else
        {
            m_bridge->PushPlanApproval();
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
    m_runningHtmlByIdx.clear();
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

    // Display human-readable error message with optional retry button
    wxString friendly = HumanizeErrorMessage( data->message, data->httpCode, data->errorType );
    wxString retryBtn;
    if( data->canRetry && !m_lastSentText.IsEmpty() )
    {
        retryBtn = wxS( "<a href=\"agent:retry\" style=\"display:inline-block; margin-top:8px; "
                        "padding:4px 14px; border-radius:6px; font-size:12px; font-weight:500; "
                        "text-decoration:none; cursor:pointer; "
                        "background:rgba(232,85,85,0.15); color:var(--accent-red);\">Retry</a>" );
    }
    wxString errorHtml = wxString::Format(
        "<div class=\"rounded-lg p-3 my-2\" style=\"background:rgba(232,85,85,0.08); border:1px solid rgba(232,85,85,0.2);\">"
        "<p class=\"text-accent-red text-[13px] m-0\">%s</p>%s"
        "</div>",
        friendly, retryBtn );
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
    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        m_chatHistory = m_ccController->GetChatHistory();
    }
    else if( m_chatController )
    {
        m_chatHistory = m_chatController->GetChatHistory();
        m_apiContext = m_chatController->GetApiContext();
    }

    // Store the error in chat history so it's visible when the chat is reloaded.
    // The _error flag tells BuildApiContext() to skip this entry.
    {
        nlohmann::json errorEntry;
        errorEntry["_error"] = true;
        errorEntry["message"] = friendly.ToStdString();
        errorEntry["can_retry"] = data->canRetry;
        m_chatHistory.push_back( errorEntry );

        // Sync back to controller so its history stays in sync
        if( m_chatController )
            m_chatController->SetHistory( m_chatHistory );
    }

    // Sync frame's state machine with controller (now IDLE after error recovery)
    m_conversationCtx.SetState( AgentConversationState::IDLE );

    // Save the chat so error interactions are preserved in history
    m_chatHistoryDb.Save( m_chatHistory );

    // On genuine auth errors, invalidate the local session and show the sign-in
    // overlay.  Without SignOut() the local token still looks valid (hasn't hit its
    // expiry time), so IsAuthenticated() returns true and the sign-in button never
    // appears.  Skip SignOut for Vercel deployment-protection 401s (identified by
    // "bypass" in the error body) — those aren't auth failures.
    if( data->errorType == "authentication_error"
        || ( data->httpCode == 401 && data->message.find( "bypass" ) == std::string::npos ) )
    {
        if( m_auth )
            m_auth->SignOut();

        UpdateAuthUI();
    }

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
            std::string currentResponse = m_chatController ? m_chatController->GetCurrentResponse() : "";
            if( !m_thinkingHtml.IsEmpty() )
                streamingContent += m_thinkingHtml;
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

    if( m_backend == AgentBackend::CLAUDE_CODE && m_ccController )
    {
        m_chatHistory = m_ccController->GetChatHistory();
        m_chatHistoryDb.Save( m_chatHistory );
        UploadCurrentChat();
    }
    else if( m_chatController )
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

        // If using CC backend, sync loaded history into CC controller
        if( m_ccController )
            m_ccController->SetChatHistory( m_chatHistory );
    }

    // Clear historical thinking toggle state for new history
    m_historicalThinkingExpanded.clear();
    m_historicalToolResultExpanded.clear();
    m_currentThinkingIndex = -1;
    m_runningHtmlByIdx.clear();
    m_activeToolResultIdx = -1;

    // Render the loaded chat history (also advances m_toolResultCounter past
    // historical tool-result indices so new live tools get non-colliding DOM IDs)
    m_showingOnboarding = false;
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
// Claude Code Promotion Popup
// ============================================================================

void AGENT_FRAME::MaybeShowCcPromo()
{
    if( !m_auth || !m_auth->IsAuthenticated() )
        return;

    if( !m_ccPromoClaudeCodeAvailable )
        return;

    if( m_currentModel == "Claude Code (Opus)" )
        return;

    if( IsCcPromoDismissed() )
        return;

    m_bridge->PushShowCcPromo();
}


void AGENT_FRAME::OnCcPromoAccept()
{
    SaveCcPromoDismissed();

    // Switch to Claude Code model
    DoModelChange( "Claude Code (Opus)" );

    // Rebuild model list to reflect new selection
    std::vector<std::string> models = { "Claude 4.6 Opus", "Claude 4.6 Sonnet" };

    if( m_ccPromoClaudeCodeAvailable )
        models.push_back( "Claude Code (Opus)" );

    m_bridge->PushModelList( models, m_currentModel );

    // Glow animation on the model dropdown
    m_bridge->PushGlowModelDropdown();

    // Prefill setup message
    m_bridge->PushInputSetText(
        "Set up the Zeo MCP (https://www.zeo.dev/docs/features/agent/integrations/claude-code)" );
    m_bridge->PushInputFocus();
}


void AGENT_FRAME::OnCcPromoDismiss()
{
    SaveCcPromoDismissed();
}


void AGENT_FRAME::SaveCcPromoDismissed()
{
    wxString path = GetPreferencesPath();

    wxFileName fn( path );

    if( !wxFileName::DirExists( fn.GetPath() ) )
        wxFileName::Mkdir( fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    nlohmann::json prefs;
    std::ifstream inFile( path.ToStdString() );

    if( inFile.is_open() )
    {
        try { inFile >> prefs; }
        catch( ... ) { prefs = nlohmann::json::object(); }
        inFile.close();
    }

    prefs["cc_promo_dismissed"] = true;

    std::ofstream outFile( path.ToStdString() );

    if( outFile.is_open() )
    {
        outFile << prefs.dump( 2 );
        outFile.close();
    }
}


bool AGENT_FRAME::IsCcPromoDismissed()
{
    wxString path = GetPreferencesPath();

    std::ifstream file( path.ToStdString() );

    if( file.is_open() )
    {
        try
        {
            nlohmann::json prefs;
            file >> prefs;

            if( prefs.contains( "cc_promo_dismissed" ) && prefs["cc_promo_dismissed"].is_boolean() )
                return prefs["cc_promo_dismissed"].get<bool>();
        }
        catch( ... ) {}
    }

    return false;
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

    std::string configPathStr = AGENT_AUTH::GetSupabaseConfigPath();

    if( !configPathStr.empty() )
    {
        std::ifstream configFile( configPathStr );

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

    // Set Sentry user so crashes are attributed to the authenticated user
    std::string email = m_auth->GetUserEmail();
    if( !email.empty() )
    {
        APP_MONITOR::SENTRY::Instance()->SetUser( email );
    }

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
    wrapper["project_path"] = m_chatHistoryDb.GetProjectPath();
    wrapper["created_at"] = m_chatHistoryDb.GetCreatedAt();
    wrapper["last_updated"] = m_chatHistoryDb.GetLastUpdated();
    wrapper["messages"] = m_chatHistory;

    m_cloudSync->UploadChat( convId, wrapper.dump() );
}
