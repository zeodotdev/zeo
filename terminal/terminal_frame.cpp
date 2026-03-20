#include "terminal_frame.h"
#include "terminal_command_validator.h"
#include "headless_python_executor.h"
#include "tool_script_loader.h"
#include "agent_monitor_log.h"
#ifndef _WIN32
#include "pty_webview_panel.h"
#endif
#include <kiway_mail.h>
#include <mail_type.h>
#include <bitmaps.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <id.h>
#include <kiway.h>
#include <project.h>
#include <paths.h>
#include <kiplatform/environment.h>
#include <frame_type.h>
#include <eda_base_frame.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/menu.h>   // For menu IDs
#include <nlohmann/json.hpp>
#include <pgm_base.h>
#include <set>
#include <wx/weakref.h>

#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#include "macos_terminal_key_monitor.h"
#endif
#include <api/api_server.h>

// Define IDs for new commands
enum
{
    ID_NEW_TAB = wxID_HIGHEST + 1,
    ID_CLOSE_TAB,
    ID_SWITCH_TAB_1,
    ID_SWITCH_TAB_2,
    ID_SWITCH_TAB_3,
    ID_SWITCH_TAB_4,
    ID_SWITCH_TAB_5,
    ID_SWITCH_TAB_6,
    ID_SWITCH_TAB_7,
    ID_SWITCH_TAB_8,
    ID_SWITCH_TAB_9
};


// Python bootstrap: adds kipy to sys.path by searching common locations.
// Used by both sync (ExecuteCommandForAgent) and async (ExecuteCommandForAgentAsync) paths.
static const std::string KIPY_BOOTSTRAP =
    "import sys\n"
    "import os\n"
    "_kipy_found = False\n"
    "_search_paths = []\n"
    "if os.environ.get('KICAD_PYTHON_PATH'):\n"
    "    _search_paths.append(os.environ['KICAD_PYTHON_PATH'])\n"
    "_cwd = os.getcwd()\n"
    "for _i in range(6):\n"
    "    _search_paths.append(os.path.join(_cwd, 'kicad-python'))\n"
    "    _search_paths.append(os.path.join(_cwd, 'code', 'kicad-python'))\n"
    "    _cwd = os.path.dirname(_cwd)\n"
    "for _p in _search_paths:\n"
    "    if os.path.isdir(_p) and _p not in sys.path:\n"
    "        sys.path.insert(0, _p)\n"
    "        try:\n"
    "            import kipy\n"
    "            _kipy_found = True\n"
    "            break\n"
    "        except ImportError:\n"
    "            sys.path.remove(_p)\n"
    "if not _kipy_found:\n"
    "    try:\n"
    "        import kipy\n"
    "        _kipy_found = True\n"
    "    except ImportError:\n"
    "        pass\n";


// Build mode-specific Python init code (kipy imports + editor connection).
// socketPath is empty for sync execution, set for async (known socket).
static std::string BuildModeInitCode( const wxString& aMode, const std::string& aSocketPath = "" )
{
    std::string socketArg;
    if( !aSocketPath.empty() )
    {
        // Escape backslashes for Python string literal (Windows paths)
        std::string escaped = aSocketPath;
        for( size_t pos = 0; ( pos = escaped.find( '\\', pos ) ) != std::string::npos; pos += 2 )
            escaped.insert( pos, "\\" );
        socketArg = "socket_path='" + escaped + "', timeout_ms=5000";
    }

    std::string initCode = KIPY_BOOTSTRAP +
        "import kipy\n"
        "from kipy.geometry import Vector2\n"
        "kicad = kipy.KiCad(" + socketArg + ")\n";

    if( aMode == "sch" )
    {
        initCode +=
            "try:\n"
            "    sch = kicad.get_schematic()\n"
            "    if hasattr(sch, 'refresh_document'):\n"
            "        sch.refresh_document()\n"
            "except Exception:\n"
            "    raise RuntimeError('Schematic editor is not open. Use launch_editor to open it first.')\n";
    }
    else if( aMode == "pcb" )
    {
        initCode +=
            "try:\n"
            "    board = kicad.get_board()\n"
            "except Exception:\n"
            "    raise RuntimeError('PCB editor is not open. Use launch_editor to open it first.')\n";
    }

    return initCode;
}


// Wait for a headless Python execution to complete, polling wxYield for UI responsiveness.
static std::string WaitForPythonResult( HEADLESS_PYTHON_EXECUTOR* aExecutor,
                                        long aTimeoutMs = 30000 )
{
    wxLongLong startTime = wxGetLocalTimeMillis();

    while( aExecutor->IsPythonRunning() )
    {
        wxMilliSleep( 50 );
        wxYield();

        if( wxGetLocalTimeMillis() - startTime > aTimeoutMs )
            return "Error: Python execution timed out after 30 seconds";
    }

    std::string result = aExecutor->GetLastPythonResult();
    return result.empty() ? "(no output)" : result;
}


// Map an editor mode string to a FRAME_T.  Returns FRAME_T(-1) for unknown modes.
static FRAME_T ModeToFrameType( const std::string& aMode )
{
    if( aMode == "sch" )
        return FRAME_SCH;
    if( aMode == "pcb" )
        return FRAME_PCB_EDITOR;
    return static_cast<FRAME_T>( -1 );
}


// Send undo transaction begin + snapshot to an editor frame.
static void BeginEditorTransaction( KIWAY& aKiway, FRAME_T aFrame )
{
    nlohmann::json beginMsg;
    beginMsg["sheet_uuid"] = "";
    std::string beginPayload = beginMsg.dump();
    aKiway.ExpressMail( aFrame, MAIL_AGENT_BEGIN_TRANSACTION, beginPayload );

    nlohmann::json snapshotMsg;
    snapshotMsg["type"] = "take_snapshot";
    std::string snapshotPayload = snapshotMsg.dump();
    aKiway.ExpressMail( aFrame, MAIL_AGENT_REQUEST, snapshotPayload );
}


// Send detect_changes + commit end-transaction to an editor frame.
static void EndEditorTransaction( KIWAY& aKiway, FRAME_T aFrame )
{
    nlohmann::json detectMsg;
    detectMsg["type"] = "detect_changes";
    std::string detectPayload = detectMsg.dump();
    aKiway.ExpressMail( aFrame, MAIL_AGENT_REQUEST, detectPayload );

    nlohmann::json endMsg;
    endMsg["commit"] = true;
    std::string endPayload = endMsg.dump();
    aKiway.ExpressMail( aFrame, MAIL_AGENT_END_TRANSACTION, endPayload );
}

BEGIN_EVENT_TABLE( TERMINAL_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, TERMINAL_FRAME::OnExit )
EVT_MENU( ID_NEW_TAB, TERMINAL_FRAME::OnNewTab )
EVT_MENU( ID_CLOSE_TAB, TERMINAL_FRAME::OnCloseTab )
EVT_MENU_RANGE( ID_SWITCH_TAB_1, ID_SWITCH_TAB_9, TERMINAL_FRAME::OnSwitchTab )
END_EVENT_TABLE()


void TERMINAL_FRAME::OnTerminalTitleChanged( wxCommandEvent& aEvent )
{
    // Find which tab this panel belongs to and update its title
    wxWindow* panel = dynamic_cast<wxWindow*>( aEvent.GetEventObject() );

    if( !panel )
        return;

    for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
    {
        if( m_notebook->GetPage( i ) == panel )
        {
            wxString title = aEvent.GetString();

            // Title is now pre-formatted as "process: directory" from the PTY panel
            // Just apply length limiting for tab display
            if( title.length() > 24 )
                title = title.Left( 22 ) + "...";

            if( title.IsEmpty() )
                title = "Shell";

            m_notebook->SetPageText( i, title );
            break;
        }
    }
}

TERMINAL_FRAME::TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_TERMINAL, "Terminal", wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_FRAME_STYLE, "terminal_frame_name", schIUScale ),
        m_asyncRequestPending( false ),
        m_hasAgentTargetSheet( false )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Create Notebook
    m_notebook = new wxAuiNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS
                                            | wxAUI_NB_MIDDLE_CLICK_CLOSE );

    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSE, &TERMINAL_FRAME::OnTabClosed, this );
    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSED, &TERMINAL_FRAME::OnTabClosedDone, this );
#ifndef _WIN32
    m_notebook->Bind( wxEVT_TERMINAL_TITLE_CHANGED, &TERMINAL_FRAME::OnTerminalTitleChanged, this );
#endif

    mainSizer->Add( m_notebook, 1, wxEXPAND | wxALL, 0 );

    SetSizer( mainSizer );
    SetSize( 800, 600 );

    // Set window icon (multiple sizes for title bar + taskbar)
    {
        wxIcon icon;
        wxIconBundle icon_bundle;

        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_terminal, 16 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_terminal, 32 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_terminal, 48 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_terminal, 128 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_terminal, 256 ) );
        icon_bundle.AddIcon( icon );

        SetIcons( icon_bundle );
    }

    // Don't add initial terminal here - it will be added when the frame is shown
    // This prevents a visible terminal from appearing when the frame is created
    // just for headless agent command execution

    // Layout
    Layout();

    // Bind size event for proper resize handling
    Bind( wxEVT_SIZE, &TERMINAL_FRAME::OnSize, this );

    // Bind show event to create initial terminal when frame becomes visible
    Bind( wxEVT_SHOW, &TERMINAL_FRAME::OnShowWindow, this );

    // Headless executor for agent Python/shell commands
    m_headlessExecutor = new HEADLESS_PYTHON_EXECUTOR();

    // Setup Menu
    wxMenuBar* menuBar = new wxMenuBar();
    wxMenu*    fileMenu = new wxMenu();
    fileMenu->Append( ID_NEW_TAB, "New Terminal\tCtrl+T" );
    fileMenu->Append( ID_CLOSE_TAB, "Close Terminal\tCtrl+W" );
    fileMenu->AppendSeparator();
    fileMenu->Append( wxID_EXIT, "Exit" );
    menuBar->Append( fileMenu, "File" );

    // Window menu with tab switching shortcuts
    wxMenu* windowMenu = new wxMenu();
    windowMenu->Append( ID_SWITCH_TAB_1, "Terminal 1\tCtrl+1" );
    windowMenu->Append( ID_SWITCH_TAB_2, "Terminal 2\tCtrl+2" );
    windowMenu->Append( ID_SWITCH_TAB_3, "Terminal 3\tCtrl+3" );
    windowMenu->Append( ID_SWITCH_TAB_4, "Terminal 4\tCtrl+4" );
    windowMenu->Append( ID_SWITCH_TAB_5, "Terminal 5\tCtrl+5" );
    windowMenu->Append( ID_SWITCH_TAB_6, "Terminal 6\tCtrl+6" );
    windowMenu->Append( ID_SWITCH_TAB_7, "Terminal 7\tCtrl+7" );
    windowMenu->Append( ID_SWITCH_TAB_8, "Terminal 8\tCtrl+8" );
    windowMenu->Append( ID_SWITCH_TAB_9, "Terminal 9\tCtrl+9" );
    menuBar->Append( windowMenu, "Window" );

    SetMenuBar( menuBar );

#ifdef __APPLE__
    // Install native keyboard monitor to handle Cmd+key shortcuts
    // This is needed because WKWebView captures these through Cocoa responder chain
    // Use CallAfter to ensure the window is fully realized before getting its handle
    wxWeakRef<TERMINAL_FRAME> weakThisForKeyboard( this );
    CallAfter( [weakThisForKeyboard]() {
        if( !weakThisForKeyboard )
            return;

        InstallTerminalKeyboardMonitor( weakThisForKeyboard->GetHandle(),
            [weakThisForKeyboard]( TERMINAL_KEY_SHORTCUT shortcut ) {
                if( weakThisForKeyboard )
                    weakThisForKeyboard->HandleKeyShortcut( static_cast<int>( shortcut ) );
            } );
    } );
#endif
}

void TERMINAL_FRAME::UpdateTabClosing()
{
    // If 1 tab, remove close buttons. If >1, enable them.
    // For AuiNotebook, we can change the window style `wxAUI_NB_CLOSE_ON_ACTIVE_TAB` or use `SetCloseButton` if supported.
    // SetCloseButton is not standard wxAuiNotebook API in 3.0? It usually requires AuiTabCtrl access.
    // Simplest: Toggle the Style flag.

    long style = m_notebook->GetWindowStyleFlag();
    if( m_notebook->GetPageCount() <= 1 )
    {
        style &= ~wxAUI_NB_CLOSE_ON_ACTIVE_TAB;
        style &= ~wxAUI_NB_CLOSE_BUTTON; // Also remove global close button if present
    }
    else
    {
        style |= wxAUI_NB_CLOSE_ON_ACTIVE_TAB;
        // style |= wxAUI_NB_CLOSE_BUTTON;
    }
    m_notebook->SetWindowStyleFlag( style );
    m_notebook->Refresh(); // Redraw tabs
}

TERMINAL_FRAME::~TERMINAL_FRAME()
{
    wxLogInfo( "TERMINAL_FRAME destructor called" );

#ifdef __APPLE__
    RemoveTerminalKeyboardMonitor();
#endif

    delete m_headlessExecutor;
    m_headlessExecutor = nullptr;

    wxLogInfo( "TERMINAL_FRAME destructor complete" );
}


void TERMINAL_FRAME::CommonSettingsChanged( int aFlags )
{
    // TERMINAL_FRAME doesn't have toolbars or most of the infrastructure that
    // EDA_BASE_FRAME::CommonSettingsChanged expects.
    // Don't call base class to avoid RecreateToolbars() crash (m_toolbarSettings is null).
    // The terminal frame is a simple notebook-based shell interface that doesn't need
    // toolbar recreation or most common settings handling.
}


void TERMINAL_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

void TERMINAL_FRAME::OnNewTab( wxCommandEvent& event )
{
    AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );
}

void TERMINAL_FRAME::OnCloseTab( wxCommandEvent& event )
{
    int sel = m_notebook->GetSelection();
    if( sel != wxNOT_FOUND && m_notebook->GetPageCount() > 1 )
    {
        m_notebook->DeletePage( sel );
        UpdateTabClosing();
    }
}

void TERMINAL_FRAME::OnTabClosed( wxAuiNotebookEvent& event )
{
    // Check if we are trying to close the last tab
    if( m_notebook->GetPageCount() <= 1 )
    {
        event.Veto();
        return;
    }

    // We need to update styles AFTER the page is removed.
    // Event is "PAGE_CLOSE", so page is still there.
    // We can't update style to remove button comfortably here for the *remaining* tab if we are closing the second to last.
    // We should use CallAfter or similar.
    // Or check `GetPageCount() - 1`

    // Ideally, we let it close, then update.
    // But wxAuiNotebookEvent doesn't have a "CLOSED" event that guarantees count update immediately in all versions?
    // Actually `wxEVT_AUINOTEBOOK_PAGE_CLOSED` exists.
}

void TERMINAL_FRAME::OnTabClosedDone( wxAuiNotebookEvent& event )
{
    UpdateTabClosing();
    event.Skip();
}


void TERMINAL_FRAME::OnSwitchTab( wxCommandEvent& event )
{
    int tabIndex = event.GetId() - ID_SWITCH_TAB_1;

    if( tabIndex >= 0 && tabIndex < (int) m_notebook->GetPageCount() )
    {
        m_notebook->SetSelection( tabIndex );
        FocusActiveTerminal();
    }
}


void TERMINAL_FRAME::FocusActiveTerminal()
{
#ifndef _WIN32
    if( wxWindow* page = m_notebook->GetCurrentPage() )
    {
        if( PTY_WEBVIEW_PANEL* ptyPanel = dynamic_cast<PTY_WEBVIEW_PANEL*>( page ) )
        {
            ptyPanel->FocusTerminal();
            return;
        }
    }
#endif

    // Fallback for non-PTY panels
    if( wxWindow* page = m_notebook->GetCurrentPage() )
        page->SetFocus();
}


void TERMINAL_FRAME::HandleKeyShortcut( int aShortcut )
{
#ifdef __APPLE__
    TERMINAL_KEY_SHORTCUT shortcut = static_cast<TERMINAL_KEY_SHORTCUT>( aShortcut );

    switch( shortcut )
    {
    case TERMINAL_KEY_SHORTCUT::NEW_TAB:
        AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );
        break;

    case TERMINAL_KEY_SHORTCUT::CLOSE_TAB:
        if( m_notebook->GetPageCount() > 1 )
        {
            int sel = m_notebook->GetSelection();
            if( sel != wxNOT_FOUND )
            {
                m_notebook->DeletePage( sel );
                UpdateTabClosing();
                FocusActiveTerminal();
            }
        }
        break;

    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_1:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_2:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_3:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_4:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_5:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_6:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_7:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_8:
    case TERMINAL_KEY_SHORTCUT::SWITCH_TAB_9:
    {
        int tabIndex = static_cast<int>( shortcut ) - static_cast<int>( TERMINAL_KEY_SHORTCUT::SWITCH_TAB_1 );
        if( tabIndex >= 0 && tabIndex < (int) m_notebook->GetPageCount() )
        {
            m_notebook->SetSelection( tabIndex );
            FocusActiveTerminal();
        }
        break;
    }
    }
#endif
}


void TERMINAL_FRAME::OnSize( wxSizeEvent& event )
{
    // Ensure proper layout when frame is resized
    Layout();
    event.Skip();
}


void TERMINAL_FRAME::OnShowWindow( wxShowEvent& event )
{
    // When the frame is shown for the first time, create an initial terminal tab
    // This prevents creating a visible terminal when the frame is only used for
    // headless agent command execution
    if( event.IsShown() && m_notebook->GetPageCount() == 0 )
    {
        AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );
    }

    event.Skip();
}


void TERMINAL_FRAME::AddTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode )
{
#ifndef _WIN32
    if( aMode == TERMINAL_PANEL::MODE_SYSTEM )
    {
        PTY_WEBVIEW_PANEL* panel = new PTY_WEBVIEW_PANEL( m_notebook );
        panel->StartShell();
        m_notebook->AddPage( panel, panel->GetTitle(), true );
    }
    else
#endif
    {
        TERMINAL_PANEL* panel = new TERMINAL_PANEL( m_notebook, aMode );
        m_notebook->AddPage( panel, panel->GetTitle(), true );
    }

    UpdateTabClosing();

    // Auto-focus the new terminal so user can start typing immediately
    // Use CallAfter to ensure focus is set after the panel is fully created
    CallAfter( [this]() {
        FocusActiveTerminal();
    } );
}

TERMINAL_PANEL* TERMINAL_FRAME::GetActivePanel()
{
    wxWindow* page = m_notebook->GetCurrentPage();
    return wxDynamicCast( page, TERMINAL_PANEL );
}

TERMINAL_PANEL* TERMINAL_FRAME::GetPanel( int aIndex )
{
    if( aIndex >= 0 && aIndex < (int) m_notebook->GetPageCount() )
    {
        return wxDynamicCast( m_notebook->GetPage( aIndex ), TERMINAL_PANEL );
    }
    return nullptr;
}

std::string TERMINAL_FRAME::ExecuteCommandForAgent( const wxString& aCmd )
{
    // Format options:
    //   run_shell sch <python_code>     - IPC Python shell with kipy/sch pre-imported
    //   run_shell pcb <python_code>     - IPC Python shell with kipy/board pre-imported
    //   run_terminal <bash_command>     - Unix shell command
    //   run_terminal_command [mode] [command]  - Legacy format (deprecated)

    wxString cmd = aCmd;

    // Handle new simplified commands
    if( cmd.StartsWith( "run_shell " ) )
    {
        wxString rest = cmd.Mid( 10 ).Trim( false );
        wxString mode = rest.BeforeFirst( ' ' );
        wxString code = rest.AfterFirst( ' ' );

        std::string fullCode = BuildModeInitCode( mode ) + code.ToStdString();
        std::string immediateResult = m_headlessExecutor->RunPython( fullCode, nullptr );

        if( !immediateResult.empty() && immediateResult.find( "Error:" ) == 0 )
            return immediateResult;

        return WaitForPythonResult( m_headlessExecutor );
    }

    if( cmd.StartsWith( "run_terminal " ) )
    {
        wxString bashCmd = cmd.Mid( 13 ).Trim( false );

        auto allowedPaths = GetAllowedPaths();
        TerminalValidationResult validation =
            TerminalCommandValidator::ValidateCommand( bashCmd.ToStdString(), allowedPaths );

        if( !validation.valid )
        {
            wxLogInfo( "TERMINAL: Command blocked (sync): %s", validation.error );
            return validation.error;
        }

        return m_headlessExecutor->RunSystemCommand( bashCmd );
    }

    // Legacy format: run_terminal_command [mode] [command]
    if( cmd.StartsWith( "run_terminal_command " ) )
        cmd = cmd.Mid( 21 );
    cmd.Trim( false ).Trim();

    if( cmd == "list" )
    {
        std::string result = "Open Terminals:\n";
        for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
        {
            TERMINAL_PANEL* p = GetPanel( i );
            if( p )
            {
                result += std::to_string( i ) + ": " + p->GetTitle().ToStdString() + "\n";
            }
        }
        return result;
    }

    // Legacy fallback: dispatch via headless executor based on mode prefix
    wxString firstArg = cmd.BeforeFirst( ' ' );
    wxString rest = cmd.AfterFirst( ' ' );

    if( firstArg == "sys" )
        return m_headlessExecutor->RunSystemCommand( rest );

    // For Python modes, use headless executor with sync wait
    std::string pythonCode;

    if( firstArg == "sch" )
    {
        pythonCode = "import kipy\n"
                     "from kipy.geometry import Vector2\n"
                     "kicad = kipy.KiCad()\n"
                     "sch = kicad.get_schematic()\n"
                     "if hasattr(sch, 'refresh_document'):\n"
                     "    sch.refresh_document()\n"
                     + rest.ToStdString();
    }
    else if( firstArg == "pcb" )
    {
        pythonCode = "import kipy\n"
                     "from kipy.geometry import Vector2\n"
                     "kicad = kipy.KiCad()\n"
                     "board = kicad.get_board()\n"
                     + rest.ToStdString();
    }
    else if( firstArg == "ipc" || firstArg == "python" )
    {
        pythonCode = rest.ToStdString();
    }
    else
    {
        return "Error: Unknown mode/command format.";
    }

    std::string immediateResult = m_headlessExecutor->RunPython( pythonCode, nullptr );

    if( !immediateResult.empty() && immediateResult.find( "Error:" ) == 0 )
        return immediateResult;

    return WaitForPythonResult( m_headlessExecutor );
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_REQUEST )
    {
        std::string payload = aEvent.GetPayload();

        wxLogInfo( "TERMINAL: Received MAIL_AGENT_REQUEST, payload: %s",
                   wxString::FromUTF8( payload ) );

        // Use async execution to avoid blocking the UI thread
        ExecuteCommandForAgentAsync( payload );
    }
    else if( aEvent.Command() == MAIL_MCP_EXECUTE_TOOL )
    {
        // MCP tool execution routed through the project manager.
        // The payload is a JSON object with "tool_name" and "tool_args_json" fields.
        // We execute it synchronously (with wxYield polling) and write the result
        // back into the payload so the caller gets it when ExpressMail returns.
        std::string payload = aEvent.GetPayload();

        wxLogDebug( "TERMINAL: Received MAIL_MCP_EXECUTE_TOOL, payload_len=%zu", payload.length() );

        std::string result;

        try
        {
            nlohmann::json msg = nlohmann::json::parse( payload );
            std::string toolName = msg.value( "tool_name", "" );
            std::string toolArgsJson = msg.value( "tool_args_json", "{}" );

            wxLogDebug( "TERMINAL: MAIL_MCP_EXECUTE_TOOL tool='%s' args_len=%zu",
                        toolName, toolArgsJson.size() );

            result = ExecuteMCPTool( toolName, toolArgsJson );
        }
        catch( const nlohmann::json::exception& e )
        {
            wxLogError( "TERMINAL: MAIL_MCP_EXECUTE_TOOL JSON parse error: %s", e.what() );

            // Fallback: treat payload as a legacy "run_shell" command
            wxLogDebug( "TERMINAL: Falling back to legacy ExecuteCommandForAgent" );
            result = ExecuteCommandForAgent( payload );
        }

        wxLogDebug( "TERMINAL: MAIL_MCP_EXECUTE_TOOL complete, result_len=%zu", result.length() );

        // Write result back into the payload — the caller (API_HANDLER_PROJECT) reads it
        // after ExpressMail returns since the payload is passed by reference
        aEvent.SetPayload( result );
    }
    else if( aEvent.Command() == MAIL_MCP_GET_TOOL_SCHEMAS )
    {
        // Return the raw tool manifest JSON for schema discovery.
        TOOL_SCRIPT_LOADER* loader = GetToolLoader();

        if( loader )
        {
            wxLogDebug( "TERMINAL: MAIL_MCP_GET_TOOL_SCHEMAS returning manifest" );
            aEvent.SetPayload( loader->GetManifestJson() );
        }
        else
        {
            wxLogWarning( "TERMINAL: MAIL_MCP_GET_TOOL_SCHEMAS — tool loader not available" );
            aEvent.SetPayload( "" );
        }
    }
    else if( aEvent.Command() == MAIL_CANCEL_TOOL_EXECUTION )
    {
        wxLogInfo( "TERMINAL: Received MAIL_CANCEL_TOOL_EXECUTION" );
        CancelToolExecution();
    }
}


void TERMINAL_FRAME::CancelToolExecution()
{
    wxLogInfo( "TERMINAL: CancelToolExecution called" );

    if( m_headlessExecutor )
        m_headlessExecutor->CancelExecution();
}


void TERMINAL_FRAME::SendAgentResponse( const std::string& aResult )
{
    m_asyncRequestPending = false;

    wxLogInfo( "TERMINAL: SendAgentResponse, result: %s",
               wxString::FromUTF8( aResult ) );

    // ExpressMail takes a non-const reference, so we need a copy
    std::string result = aResult;
    Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, result );
}


void TERMINAL_FRAME::ExecuteCommandForAgentAsync( const wxString& aCmd )
{
    // If already processing a request, return error
    if( m_asyncRequestPending )
    {
        SendAgentResponse( "Error: Another request is already in progress" );
        return;
    }

    wxString cmd = aCmd;
    cmd.Trim( false );  // Trim leading whitespace
    cmd.Trim( true );   // Trim trailing whitespace

    // Handle new simplified commands (run_shell sch/pcb <code>)
    if( cmd.StartsWith( "run_shell " ) )
    {
        wxString rest = cmd.Mid( 10 ).Trim( false );
        wxString mode = rest.BeforeFirst( ' ' );
        wxString code = rest.AfterFirst( ' ' );

        // Get the API server socket path so kipy connects to the correct instance
        std::string socketPath;
#ifdef KICAD_IPC_API
        socketPath = Pgm().GetApiServer().SocketPath();
        wxLogInfo( "TERMINAL: Using API socket path: %s", socketPath.c_str() );

        if( socketPath.empty() )
        {
            wxLogWarning( "TERMINAL: API server socket path is empty — server may be disabled. "
                          "Enable it in Preferences > Scripting > Enable Scripting Server." );
            SendAgentResponse( "Error: Zeo IPC API server is not running.\n"
                               "Enable it in Preferences > Scripting > 'Enable Scripting Server', then restart Zeo." );
            return;
        }
#endif

        std::string initCode = BuildModeInitCode( mode, socketPath );

        // Begin undo transaction on the target editor
        FRAME_T editorFrame = ModeToFrameType( mode.ToStdString() );
        if( editorFrame != static_cast<FRAME_T>( -1 ) )
            BeginEditorTransaction( Kiway(), editorFrame );

        // Set up completion callback and execute via headless executor
        m_asyncRequestPending = true;

        std::string modeStr = mode.ToStdString();
        std::string codeStr = code.ToStdString();
        wxLongLong cmdStartTime = wxGetLocalTimeMillis();
        AGENT_MONITOR_LOG::Instance().LogCommandStart( "run_shell", modeStr, codeStr );

        auto callback = [this, editorFrame, modeStr, cmdStartTime]( const std::string& result, bool success ) {
            long durationMs = ( wxGetLocalTimeMillis() - cmdStartTime ).ToLong();
            AGENT_MONITOR_LOG::Instance().LogCommandEnd( "run_shell", modeStr, result, success, durationMs );

            wxLogInfo( "TERMINAL: Python completion callback invoked, success=%d, result_len=%zu",
                       success, result.length() );

            // End undo transaction on the target editor
            if( editorFrame != static_cast<FRAME_T>( -1 ) )
                EndEditorTransaction( Kiway(), editorFrame );

            SendAgentResponse( result );
        };

        std::string fullCode = initCode + code.ToStdString();
        wxLogInfo( "TERMINAL: Starting Python execution, code_len=%zu", fullCode.length() );
        std::string immediateResult = m_headlessExecutor->RunPython( fullCode, callback );

        if( !immediateResult.empty() && immediateResult.find( "Error:" ) == 0 )
        {
            wxLogError( "TERMINAL: RunPython immediate error: %s", immediateResult );
            SendAgentResponse( immediateResult );
        }

        return;
    }

    // Handle run_terminal (bash commands) - headless execution
    if( cmd.StartsWith( "run_terminal " ) )
    {
        wxString bashCmd = cmd.Mid( 13 ).Trim( false );

        auto allowedPaths = GetAllowedPaths();
        TerminalValidationResult validation =
            TerminalCommandValidator::ValidateCommand( bashCmd.ToStdString(), allowedPaths );

        if( !validation.valid )
        {
            wxLogInfo( "TERMINAL: Command blocked (async): %s", validation.error );
            AGENT_MONITOR_LOG::Instance().LogError( "validation", validation.error );
            SendAgentResponse( validation.error );
            return;
        }

        m_asyncRequestPending = true;
        std::string bashCmdStr = bashCmd.ToStdString();
        AGENT_MONITOR_LOG::Instance().LogCommandStart( "run_terminal", "", bashCmdStr );
        wxLongLong termStartTime = wxGetLocalTimeMillis();

        // Use wxWeakRef to guard against use-after-free if frame is destroyed
        // before CallAfter executes (e.g., during YieldFor() re-entrant event processing)
        wxWeakRef<TERMINAL_FRAME> weakThis( this );
        CallAfter( [weakThis, bashCmd, termStartTime]() {
            if( !weakThis )
            {
                // Frame was destroyed, abort - log for debugging
                wxLogWarning( "TERMINAL: CallAfter aborted - frame was destroyed" );
                return;
            }

            std::string result = weakThis->m_headlessExecutor->RunSystemCommand( bashCmd );
            long durationMs = ( wxGetLocalTimeMillis() - termStartTime ).ToLong();
            std::string output = result.empty() ? "(no output)" : result;
            AGENT_MONITOR_LOG::Instance().LogCommandEnd( "run_terminal", "", output, true, durationMs );
            weakThis->SendAgentResponse( output );
        } );
        return;
    }

    // Legacy format and other commands - fall back to sync version for now
    // Note: The sync version still has blocking waits for Python, so we should
    // eventually migrate all Python commands to async
    std::string result = ExecuteCommandForAgent( aCmd );
    SendAgentResponse( result );
}


TOOL_SCRIPT_LOADER* TERMINAL_FRAME::GetToolLoader()
{
    if( !m_toolLoader )
    {
        wxLogDebug( "TERMINAL: Lazy-initializing TOOL_SCRIPT_LOADER" );
        m_toolLoader = std::make_unique<TOOL_SCRIPT_LOADER>();
        wxLogDebug( "TERMINAL: TOOL_SCRIPT_LOADER initialized" );
    }

    return m_toolLoader.get();
}


std::string TERMINAL_FRAME::ExecuteMCPTool( const std::string& aToolName,
                                             const std::string& aArgsJson )
{
    wxLogDebug( "TERMINAL: ExecuteMCPTool('%s') args_len=%zu", aToolName, aArgsJson.size() );

    TOOL_SCRIPT_LOADER* loader = GetToolLoader();

    if( !loader->HasTool( aToolName ) )
    {
        wxLogWarning( "TERMINAL: ExecuteMCPTool — unknown tool '%s'", aToolName );
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Unknown tool: " + aToolName;
        return err.dump();
    }

    std::string app = loader->GetApp( aToolName );
    bool readOnly = loader->IsReadOnly( aToolName );

    wxLogDebug( "TERMINAL: ExecuteMCPTool tool='%s' app='%s' readOnly=%d", aToolName, app, readOnly );

    // Build the Python code directly (no "run_shell" prefix round-trip)
    std::string pythonCode = loader->BuildPythonCode( aToolName, aArgsJson );

    if( pythonCode.empty() )
    {
        wxLogError( "TERMINAL: ExecuteMCPTool — BuildPythonCode returned empty for '%s'", aToolName );
        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Failed to build command for tool: " + aToolName;
        return err.dump();
    }

    // Get the editor frame type for transaction management
    FRAME_T editorFrame = ModeToFrameType( app );

    // Begin undo transaction on the target editor for non-read-only tools
    // (after validation so we don't leak an open transaction on early-return errors)
    if( !readOnly && editorFrame != static_cast<FRAME_T>( -1 ) )
    {
        wxLogDebug( "TERMINAL: BeginEditorTransaction for tool '%s'", aToolName );
        BeginEditorTransaction( Kiway(), editorFrame );
    }

    // Get the API server socket path so kipy connects to the correct instance
    std::string socketPath;
#ifdef KICAD_IPC_API
    socketPath = Pgm().GetApiServer().SocketPath();
    wxLogDebug( "TERMINAL: ExecuteMCPTool using API socket: %s", socketPath );

    if( socketPath.empty() )
    {
        wxLogWarning( "TERMINAL: API server socket path is empty" );

        if( !readOnly && editorFrame != static_cast<FRAME_T>( -1 ) )
            EndEditorTransaction( Kiway(), editorFrame );

        nlohmann::json err;
        err["status"] = "error";
        err["message"] = "Zeo IPC API server is not running.";
        return err.dump();
    }
#endif

    std::string initCode = BuildModeInitCode( wxString( app ), socketPath );
    std::string code = initCode + pythonCode;

    wxLogDebug( "TERMINAL: ExecuteMCPTool running Python, code_len=%zu", code.size() );

    // Execute synchronously with wxYield polling
    std::string immediateResult = m_headlessExecutor->RunPython( code, nullptr );

    if( !immediateResult.empty() && immediateResult.find( "Error:" ) == 0 )
    {
        wxLogError( "TERMINAL: ExecuteMCPTool immediate error: %s", immediateResult );

        if( !readOnly && editorFrame != static_cast<FRAME_T>( -1 ) )
            EndEditorTransaction( Kiway(), editorFrame );

        return immediateResult;
    }

    std::string result = WaitForPythonResult( m_headlessExecutor );

    wxLogDebug( "TERMINAL: ExecuteMCPTool result_len=%zu", result.size() );

    // End undo transaction on the target editor for non-read-only tools
    if( !readOnly && editorFrame != static_cast<FRAME_T>( -1 ) )
    {
        wxLogDebug( "TERMINAL: EndEditorTransaction for tool '%s'", aToolName );
        EndEditorTransaction( Kiway(), editorFrame );
    }

    return result;
}


#ifdef __APPLE__
static std::vector<std::string> GetExternalEditorFileDirs()
{
    std::vector<std::string> dirs;
    std::set<std::string> seen;
    pid_t myPid = getpid();

    int bufSize = proc_listpids( PROC_ALL_PIDS, 0, NULL, 0 );

    if( bufSize <= 0 )
        return dirs;

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

        if( procPath.find( "eeschema" ) == std::string::npos &&
            procPath.find( "pcbnew" ) == std::string::npos )
            continue;

        wxLogInfo( "TERMINAL: Found external editor process: PID %d (%s)", pids[i], procPath );

        // Get the process's cwd — eeschema/pcbnew set cwd to the file's directory
        struct proc_vnodepathinfo vnodePathInfo;
        int sz = proc_pidinfo( pids[i], PROC_PIDVNODEPATHINFO, 0,
                               &vnodePathInfo, sizeof( vnodePathInfo ) );

        if( sz <= 0 )
            continue;

        std::string cwd( vnodePathInfo.pvi_cdir.vip_path );

        if( !cwd.empty() && seen.insert( cwd ).second )
        {
            wxLogInfo( "TERMINAL: External editor cwd: %s", cwd );
            dirs.push_back( cwd );
        }
    }

    return dirs;
}
#endif


std::vector<std::string> TERMINAL_FRAME::GetAllowedPaths()
{
    std::vector<std::string> paths;

    // KiCad documents root: ~/Documents/KiCad (all versions)
    wxString docsPath;

    if( wxGetEnv( wxT( "KICAD_DOCUMENTS_HOME" ), &docsPath ) )
    {
        paths.push_back( docsPath.ToStdString() );
    }
    else
    {
        wxFileName kicadDocs;
        kicadDocs.AssignDir( KIPLATFORM::ENV::GetDocumentsPath() );
        kicadDocs.AppendDir( KICAD_PATH_STR );
        paths.push_back( kicadDocs.GetPath().ToStdString() );
    }

    // Active project directory
    wxString projPath = Kiway().Prj().GetProjectPath();

    if( !projPath.IsEmpty() )
        paths.push_back( projPath.ToStdString() );

    // Directories of files open in editors (same process)
    for( FRAME_T ft : { FRAME_SCH, FRAME_PCB_EDITOR } )
    {
        KIWAY_PLAYER* player = Kiway().Player( ft, false );

        if( player && player->IsShown() )
        {
            wxString f = player->GetCurrentFileName();

            if( !f.IsEmpty() )
            {
                wxFileName fn( f );
                paths.push_back( fn.GetPath().ToStdString() );
            }
        }
    }

    // Directories of files open in standalone editor processes
#ifdef __APPLE__
    for( const auto& dir : GetExternalEditorFileDirs() )
        paths.push_back( dir );
#endif

    wxString pathList;
    for( const auto& p : paths )
    {
        if( !pathList.IsEmpty() )
            pathList += ", ";
        pathList += p;
    }
    wxLogInfo( "TERMINAL: GetAllowedPaths: [%s]", pathList );

    return paths;
}
