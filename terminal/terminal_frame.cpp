#include "terminal_frame.h"
#include "terminal_command_validator.h"
#include "headless_python_executor.h"
#include "agent_monitor_log.h"
#include "pty_webview_panel.h"
#include <kiway_mail.h>
#include <mail_type.h>
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

#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#include <api/api_server.h>

// Define IDs for new commands
enum
{
    ID_NEW_TAB = wxID_HIGHEST + 1,
    ID_CLOSE_TAB
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
        socketArg = "socket_path='" + aSocketPath + "', timeout_ms=5000";

    std::string initCode = KIPY_BOOTSTRAP +
        "import kipy\n"
        "from kipy.geometry import Vector2\n"
        "kicad = kipy.KiCad(" + socketArg + ")\n";

    if( aMode == "sch" )
    {
        initCode +=
            "sch = kicad.get_schematic()\n"
            "if hasattr(sch, 'refresh_document'):\n"
            "    sch.refresh_document()\n";
    }
    else if( aMode == "pcb" )
    {
        initCode += "board = kicad.get_board()\n";
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
            wxString newTitle = aEvent.GetString();

            // Extract a short title for the tab (last component of path or command)
            // e.g., "user@host:~/projects/myapp" -> "myapp"
            // or "vim file.cpp" -> "vim file.cpp"
            wxString shortTitle = newTitle;

            // If it looks like a path prompt (contains : and /), extract last dir
            int colonPos = newTitle.Find( ':' );
            if( colonPos != wxNOT_FOUND && newTitle.Find( '/' ) != wxNOT_FOUND )
            {
                wxString pathPart = newTitle.Mid( colonPos + 1 );
                pathPart.Trim( true ).Trim( false );

                // Get the last path component
                if( pathPart.EndsWith( "/" ) )
                    pathPart.RemoveLast();

                int lastSlash = pathPart.Find( '/', true );  // Find from end
                if( lastSlash != wxNOT_FOUND )
                    shortTitle = pathPart.Mid( lastSlash + 1 );
                else if( pathPart == "~" )
                    shortTitle = "~";
                else
                    shortTitle = pathPart;
            }

            // Limit length for tab display
            if( shortTitle.length() > 20 )
                shortTitle = shortTitle.Left( 18 ) + "...";

            if( shortTitle.IsEmpty() )
                shortTitle = "Shell";

            m_notebook->SetPageText( i, shortTitle );
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
    m_notebook->Bind( wxEVT_TERMINAL_TITLE_CHANGED, &TERMINAL_FRAME::OnTerminalTitleChanged, this );

    mainSizer->Add( m_notebook, 1, wxEXPAND | wxALL, 0 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 800, 600 );

    // Add initial terminal
    AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );

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
    SetMenuBar( menuBar );
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
    delete m_headlessExecutor;
    m_headlessExecutor = nullptr;
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

void TERMINAL_FRAME::AddTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode )
{
    if( aMode == TERMINAL_PANEL::MODE_SYSTEM )
    {
        PTY_WEBVIEW_PANEL* panel = new PTY_WEBVIEW_PANEL( m_notebook );
        panel->StartShell();
        m_notebook->AddPage( panel, panel->GetTitle(), true );
    }
    else
    {
        TERMINAL_PANEL* panel = new TERMINAL_PANEL( m_notebook, aMode );
        m_notebook->AddPage( panel, panel->GetTitle(), true );
    }

    UpdateTabClosing();
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
        // Synchronous execution for MCP tool calls routed through the project manager.
        // The payload contains a "run_shell <app> <script>" command.
        // We execute it synchronously (with wxYield polling) and write the result
        // back into the payload so the caller gets it when ExpressMail returns.
        std::string cmd = aEvent.GetPayload();

        wxLogInfo( "TERMINAL: Received MAIL_MCP_EXECUTE_TOOL, cmd_len=%zu", cmd.length() );

        std::string result = ExecuteCommandForAgent( cmd );

        wxLogInfo( "TERMINAL: MAIL_MCP_EXECUTE_TOOL complete, result_len=%zu", result.length() );

        // Write result back into the payload — the caller (API_HANDLER_PROJECT) reads it
        // after ExpressMail returns since the payload is passed by reference
        aEvent.SetPayload( result );
    }
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

        CallAfter( [this, bashCmd, termStartTime]() {
            std::string result = m_headlessExecutor->RunSystemCommand( bashCmd );
            long durationMs = ( wxGetLocalTimeMillis() - termStartTime ).ToLong();
            std::string output = result.empty() ? "(no output)" : result;
            AGENT_MONITOR_LOG::Instance().LogCommandEnd( "run_terminal", "", output, true, durationMs );
            SendAgentResponse( output );
        } );
        return;
    }

    // Legacy format and other commands - fall back to sync version for now
    // Note: The sync version still has blocking waits for Python, so we should
    // eventually migrate all Python commands to async
    std::string result = ExecuteCommandForAgent( aCmd );
    SendAgentResponse( result );
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
