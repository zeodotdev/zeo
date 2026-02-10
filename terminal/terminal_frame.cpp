#include "terminal_frame.h"
#include "terminal_panel_agent.h"
#include "terminal_command_validator.h"
#include <kiway_express.h>
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
#include <wx/button.h> // For wxButton
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
    ID_NEW_AGENT_TAB,
    ID_CLOSE_TAB
};

BEGIN_EVENT_TABLE( TERMINAL_FRAME, KIWAY_PLAYER )
EVT_MENU( wxID_EXIT, TERMINAL_FRAME::OnExit )
EVT_MENU( ID_NEW_TAB, TERMINAL_FRAME::OnNewTab )
EVT_MENU( ID_NEW_AGENT_TAB, TERMINAL_FRAME::OnNewAgentTab )
EVT_MENU( ID_CLOSE_TAB, TERMINAL_FRAME::OnCloseTab )
END_EVENT_TABLE()

TERMINAL_FRAME::TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        KIWAY_PLAYER( aKiway, aParent, FRAME_TERMINAL, "Terminal", wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_FRAME_STYLE, "terminal_frame_name", schIUScale ),
        m_asyncRequestPending( false ),
        m_hasAgentTargetSheet( false )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Add Toolbar for "New Terminal"
    wxBoxSizer* topBarSizer = new wxBoxSizer( wxHORIZONTAL );

    // Dev Terminal Button
    wxButton* newTabBtn =
            new wxButton( this, ID_NEW_TAB, "+ New Dev Terminal", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    topBarSizer->Add( newTabBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    // Agent Terminal Button (Green Text via SetForegroundColour if allowed, or just normal button)
    // wxButton color support varies by OS. On macOS standard buttons might not change color easily without custom paint.
    // Let's rely on text label for distinction or `SetForegroundColour`.
    wxButton* newAgentTabBtn = new wxButton( this, ID_NEW_AGENT_TAB, "+ New Agent Terminal", wxDefaultPosition,
                                             wxDefaultSize, wxBU_EXACTFIT );
    // Try setting color (might not work on all macOS versions/themes perfectly but good intent)
    // newAgentTabBtn->SetForegroundColour( wxColour( 0, 128, 0 ) );
    topBarSizer->Add( newAgentTabBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    topBarSizer->AddStretchSpacer();

    mainSizer->Add( topBarSizer, 0, wxEXPAND | wxALL, 0 );

    // Create Notebook
    m_notebook = new wxAuiNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS
                                            | wxAUI_NB_MIDDLE_CLICK_CLOSE );
    // Removed wxAUI_NB_CLOSE_ON_ACTIVE_TAB from default set to manage manually?
    // Actually, AUI styles apply to whole notebook.
    // We can toggle CLOSE_ON_ACTIVE_TAB dynamicallly or use SetCloseButton(idx, bool).

    // Connect events via Bind/Connect or Event Table
    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSE, &TERMINAL_FRAME::OnTabClosed, this );
    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSED, &TERMINAL_FRAME::OnTabClosedDone, this );

    // Bind buttons
    newTabBtn->Bind( wxEVT_BUTTON, &TERMINAL_FRAME::OnNewTab, this );
    newAgentTabBtn->Bind( wxEVT_BUTTON, &TERMINAL_FRAME::OnNewAgentTab, this );

    mainSizer->Add( m_notebook, 1, wxEXPAND | wxALL, 0 );

    SetSizer( mainSizer );
    Layout();
    SetSize( 800, 600 );

    // Add initial terminal
    AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );

    // Setup Menu (Optional, but good for keyboard shortcuts)
    wxMenuBar* menuBar = new wxMenuBar();
    wxMenu*    fileMenu = new wxMenu();
    fileMenu->Append( ID_NEW_TAB, "New Dev Terminal\tCtrl+T" );
    fileMenu->Append( ID_NEW_AGENT_TAB, "New Agent Terminal\tCtrl+Shift+T" );
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
}

void TERMINAL_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}

void TERMINAL_FRAME::OnNewTab( wxCommandEvent& event )
{
    AddTerminal( TERMINAL_PANEL::MODE_SYSTEM );
}

void TERMINAL_FRAME::OnNewAgentTab( wxCommandEvent& event )
{
    AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
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
    TERMINAL_PANEL* panel = new TERMINAL_PANEL( m_notebook, aMode );
    m_notebook->AddPage( panel, panel->GetTitle(), true );
    UpdateTabClosing();
}

void TERMINAL_FRAME::AddAgentTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode )
{
    AGENT_TERMINAL_PANEL* panel = new AGENT_TERMINAL_PANEL( m_notebook, aMode );
    m_notebook->AddPage( panel, panel->GetTitle(), true );
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

        // Find or create agent terminal
        TERMINAL_PANEL* active = nullptr;
        for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
        {
            TERMINAL_PANEL* p = GetPanel( i );
            if( dynamic_cast<AGENT_TERMINAL_PANEL*>( p ) )
            {
                active = p;
                break;
            }
        }
        if( !active )
        {
            AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
            active = GetPanel( m_notebook->GetPageCount() - 1 );
        }
        if( !active )
            return "Error: Could not create/find Agent terminal.";

        std::string initCode;

        // Common initialization: add kicad-python to sys.path if not already there
        // Searches in order: env var, relative paths from executable, then direct import
        std::string commonInit =
            "import sys\n"
            "import os\n"
            "# Find kicad-python/kipy module\n"
            "_kipy_found = False\n"
            "_search_paths = []\n"
            "# 1. Check environment variable\n"
            "if os.environ.get('KICAD_PYTHON_PATH'):\n"
            "    _search_paths.append(os.environ['KICAD_PYTHON_PATH'])\n"
            "# 2. Search relative to current directory and parent directories\n"
            "_cwd = os.getcwd()\n"
            "for _i in range(6):\n"
            "    _search_paths.append(os.path.join(_cwd, 'kicad-python'))\n"
            "    _search_paths.append(os.path.join(_cwd, 'code', 'kicad-python'))\n"
            "    _cwd = os.path.dirname(_cwd)\n"
            "# 3. Try each search path\n"
            "for _p in _search_paths:\n"
            "    if os.path.isdir(_p) and _p not in sys.path:\n"
            "        sys.path.insert(0, _p)\n"
            "        try:\n"
            "            import kipy\n"
            "            _kipy_found = True\n"
            "            break\n"
            "        except ImportError:\n"
            "            sys.path.remove(_p)\n"
            "# 4. Try direct import (if kipy is installed)\n"
            "if not _kipy_found:\n"
            "    try:\n"
            "        import kipy\n"
            "        _kipy_found = True\n"
            "    except ImportError:\n"
            "        pass\n";

        if( mode == "sch" )
        {
            initCode = commonInit +
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad()\n"
                "sch = kicad.get_schematic()\n"
                "# Refresh document specifier to handle close/reopen cycles\n"
                "if hasattr(sch, 'refresh_document'):\n"
                "    sch.refresh_document()\n";
        }
        else if( mode == "pcb" )
        {
            initCode = commonInit +
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad()\n"
                "board = kicad.get_board()\n";
        }

        // Start Python execution (async)
        active->RunLocalPython( initCode + code.ToStdString() );

        // Wait for Python to complete (with timeout)
        wxLongLong startTime = wxGetLocalTimeMillis();
        const long timeoutMs = 30000; // 30 second timeout

        while( active->IsPythonRunning() )
        {
            wxMilliSleep( 50 );
            wxYield(); // Allow UI events and timers to process

            if( wxGetLocalTimeMillis() - startTime > timeoutMs )
            {
                return "Error: Python execution timed out after 30 seconds";
            }
        }

        // Return the result
        std::string result = active->GetLastPythonResult();
        return result.empty() ? "(no output)" : result;
    }

    if( cmd.StartsWith( "run_terminal " ) )
    {
        wxString bashCmd = cmd.Mid( 13 ).Trim( false );

        // Validate the command against allowed directory restrictions
        auto allowedPaths = GetAllowedPaths();
        TerminalValidationResult validation =
            TerminalCommandValidator::ValidateCommand( bashCmd.ToStdString(), allowedPaths );

        if( !validation.valid )
        {
            wxLogInfo( "TERMINAL: Command blocked (sync): %s", validation.error );
            return validation.error;
        }

        // Find or create agent terminal
        TERMINAL_PANEL* active = nullptr;
        for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
        {
            TERMINAL_PANEL* p = GetPanel( i );
            if( dynamic_cast<AGENT_TERMINAL_PANEL*>( p ) )
            {
                active = p;
                break;
            }
        }
        if( !active )
        {
            AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
            active = GetPanel( m_notebook->GetPageCount() - 1 );
        }
        if( !active )
            return "Error: Could not create/find Agent terminal.";

        return active->ProcessSystemCommand( bashCmd );
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

    if( cmd.StartsWith( "create_agent " ) )
    {
        // "create_agent sys", "create_agent ipc"
        wxString                      modeStr = cmd.Mid( 13 ).Trim( false );
        TERMINAL_PANEL::TERMINAL_MODE mode = TERMINAL_PANEL::MODE_SYSTEM;
        if( modeStr == "ipc" )
            mode = TERMINAL_PANEL::MODE_IPC;
        if( modeStr == "python" )
            mode = TERMINAL_PANEL::MODE_PYTHON;

        AddAgentTerminal( mode );
        return "Agent Terminal created. ID: " + std::to_string( m_notebook->GetPageCount() - 1 );
    }

    if( cmd.StartsWith( "create " ) )
    {
        // "create sys", "create ipc"
        wxString                      modeStr = cmd.Mid( 7 ).Trim( false );
        TERMINAL_PANEL::TERMINAL_MODE mode = TERMINAL_PANEL::MODE_SYSTEM;
        if( modeStr == "ipc" )
            mode = TERMINAL_PANEL::MODE_IPC;
        if( modeStr == "python" )
            mode = TERMINAL_PANEL::MODE_PYTHON;

        AddTerminal( mode );
        return "Dev Terminal created. ID: " + std::to_string( m_notebook->GetPageCount() - 1 );
    }

    wxString firstArg = cmd.BeforeFirst( ' ' );
    wxString rest = cmd.AfterFirst( ' ' );

    // Check if first arg is a number (tab ID)
    long tabId;
    if( firstArg.ToLong( &tabId ) )
    {
        // Targeted command
        TERMINAL_PANEL* panel = GetPanel( (int) tabId );
        if( !panel )
            return "Error: Invalid terminal ID " + firstArg.ToStdString();

        // Now parse mode/cmd from rest
        // "run_terminal_command 1 pcb print(x)"
        // rest = "pcb print(x)"
        wxString modeArg = rest.BeforeFirst( ' ' );
        wxString actualCmd = rest.AfterFirst( ' ' );

        // Mode switch check
        if( modeArg == "sys" && panel->GetMode() != TERMINAL_PANEL::MODE_SYSTEM )
            panel->SetMode( TERMINAL_PANEL::MODE_SYSTEM ); // Or check if compatible?
        // Actually, panel handles mode switching via ExecuteCommand usually.
        // But for explicit Agent commands, we might want to force it?
        // Let's iterate: panel->ExecuteCommand handles the heavy lifting but requires "pcb ..." prefix for mode switch?
        // If the command is "pcb ...", panel switches mode.
        // So we just pass "modeArg actualCmd" to panel?
        // i.e. we reconstruct command.

        panel->ExecuteCommand( rest );                           // Just pass the rest!
        return "Command sent to tab " + std::to_string( tabId ); // Wait, we need Output!
        // TERMINAL_PANEL::ExecuteCommand returns void (UI upate).
        // We implemented ProcessSystemCommand/RunLocalPython returning string in Panel.
        // But ExecuteCommand determines logic.
        // Refactoring needed: ExecuteCommand should probably return string?
        // Or we duplicate the dispatch logic here for Agent?
        // Agent wants output.
        // Let's look at `panel->ExecuteCommand`. It adds output to CTRL.
        // Capture is tricky if we just call ExecuteCommand.
        // We should call the specific helper functions if we know what we are doing.

        // Smart dispatch based on mode
        if( modeArg == "sys" )
            return panel->ProcessSystemCommand( actualCmd );
        if( modeArg == "sch" )
        {
            // Schematic mode: pre-import kipy, connect to schematic, import Vector2
            std::string initCode =
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad()\n"
                "sch = kicad.get_schematic()\n"
                "# Refresh document specifier to handle close/reopen cycles\n"
                "if hasattr(sch, 'refresh_document'):\n"
                "    sch.refresh_document()\n";
            return panel->RunLocalPython( initCode + actualCmd.ToStdString() );
        }
        if( modeArg == "pcb" )
        {
            // PCB mode: pre-import kipy, connect to board, import Vector2
            std::string initCode =
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad()\n"
                "board = kicad.get_board()\n";
            return panel->RunLocalPython( initCode + actualCmd.ToStdString() );
        }
        if( modeArg == "ipc" || modeArg == "python" )
            return panel->RunLocalPython( actualCmd );

        return panel->RunLocalPython( rest ); // fallback
    }

    // No ID (Active Tab) -> CHANGED: Default to dedicated Agent Tab
    TERMINAL_PANEL* active = nullptr;

    // Search for existing Agent Terminal
    for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
    {
        TERMINAL_PANEL* p = GetPanel( i );
        if( dynamic_cast<AGENT_TERMINAL_PANEL*>( p ) )
        {
            active = p;
            break;
        }
    }

    // If no Agent Terminal exists, create one
    if( !active )
    {
        // AddAgentTerminal returns void, we need to get the pointer
        AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
        // It's the last one now
        active = GetPanel( m_notebook->GetPageCount() - 1 );
    }

    if( !active )
        return "Error: Could not create/find Agent terminal.";

    // "run_terminal_command sys ls"
    // firstArg = "sys", rest = "ls"
    if( firstArg == "sys" )
        return active->ProcessSystemCommand( rest );
    if( firstArg == "sch" )
    {
        // Schematic mode: pre-import kipy, connect to schematic, import Vector2
        std::string initCode =
            "import kipy\n"
            "from kipy.geometry import Vector2\n"
            "kicad = kipy.KiCad()\n"
            "sch = kicad.get_schematic()\n"
            "# Refresh document specifier to handle close/reopen cycles\n"
            "if hasattr(sch, 'refresh_document'):\n"
            "    sch.refresh_document()\n";
        return active->RunLocalPython( initCode + rest.ToStdString() );
    }
    if( firstArg == "pcb" )
    {
        // PCB mode: pre-import kipy, connect to board, import Vector2
        std::string initCode =
            "import kipy\n"
            "from kipy.geometry import Vector2\n"
            "kicad = kipy.KiCad()\n"
            "board = kicad.get_board()\n";
        return active->RunLocalPython( initCode + rest.ToStdString() );
    }
    if( firstArg == "ipc" || firstArg == "python" )
    {
        // Raw IPC/python mode - no pre-imports
        return active->RunLocalPython( rest );
    }

    return "Error: Unknown mode/command format.";
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_REQUEST )
    {
        std::string payload = aEvent.GetPayload();

        wxLogInfo( "TERMINAL: Received MAIL_AGENT_REQUEST, payload: %s",
                   wxString::FromUTF8( payload ) );

        // Use async execution to avoid blocking the UI thread
        ExecuteCommandForAgentAsync( payload );
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
    if( cmd.StartsWith( "run_shell " ) || cmd.StartsWith( wxT("run_shell ") ) )
    {
        wxString rest = cmd.Mid( 10 ).Trim( false );
        wxString mode = rest.BeforeFirst( ' ' );
        wxString code = rest.AfterFirst( ' ' );

        // Find or create agent terminal
        TERMINAL_PANEL* active = nullptr;
        for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
        {
            TERMINAL_PANEL* p = GetPanel( i );
            if( dynamic_cast<AGENT_TERMINAL_PANEL*>( p ) )
            {
                active = p;
                break;
            }
        }
        if( !active )
        {
            AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
            active = GetPanel( m_notebook->GetPageCount() - 1 );
        }
        if( !active )
        {
            SendAgentResponse( "Error: Could not create/find Agent terminal." );
            return;
        }

        std::string initCode;

        // Common initialization: add kicad-python to sys.path if not already there
        // Searches in order: env var, relative paths from executable, then direct import
        std::string commonInit =
            "import sys\n"
            "import os\n"
            "# Find kicad-python/kipy module\n"
            "_kipy_found = False\n"
            "_search_paths = []\n"
            "# 1. Check environment variable\n"
            "if os.environ.get('KICAD_PYTHON_PATH'):\n"
            "    _search_paths.append(os.environ['KICAD_PYTHON_PATH'])\n"
            "# 2. Search relative to current directory and parent directories\n"
            "_cwd = os.getcwd()\n"
            "for _i in range(6):\n"
            "    _search_paths.append(os.path.join(_cwd, 'kicad-python'))\n"
            "    _search_paths.append(os.path.join(_cwd, 'code', 'kicad-python'))\n"
            "    _cwd = os.path.dirname(_cwd)\n"
            "# 3. Try each search path\n"
            "for _p in _search_paths:\n"
            "    if os.path.isdir(_p) and _p not in sys.path:\n"
            "        sys.path.insert(0, _p)\n"
            "        try:\n"
            "            import kipy\n"
            "            _kipy_found = True\n"
            "            break\n"
            "        except ImportError:\n"
            "            sys.path.remove(_p)\n"
            "# 4. Try direct import (if kipy is installed)\n"
            "if not _kipy_found:\n"
            "    try:\n"
            "        import kipy\n"
            "        _kipy_found = True\n"
            "    except ImportError:\n"
            "        pass\n";

        // Get the API server socket path so kipy connects to the correct instance
        std::string socketPath;
#ifdef KICAD_IPC_API
        socketPath = Pgm().GetApiServer().SocketPath();
        wxLogInfo( "TERMINAL: Using API socket path: %s", socketPath.c_str() );
#endif

        if( mode == "sch" )
        {
            initCode = commonInit +
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad(socket_path='" + socketPath + "', timeout_ms=5000)\n"
                "sch = kicad.get_schematic()\n"
                "# Refresh document specifier to handle close/reopen cycles\n"
                "if hasattr(sch, 'refresh_document'):\n"
                "    sch.refresh_document()\n";

            // Begin agent transaction for concurrent editing support
            nlohmann::json beginMsg;
            beginMsg["sheet_uuid"] = "";  // Empty means current sheet
            std::string beginPayload = beginMsg.dump();
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_BEGIN_TRANSACTION, beginPayload );

            // Tell SCH editor to take a snapshot before Python execution
            nlohmann::json snapshotMsg;
            snapshotMsg["type"] = "take_snapshot";
            std::string snapshotPayload = snapshotMsg.dump();
            Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REQUEST, snapshotPayload );
        }
        else if( mode == "pcb" )
        {
            initCode = commonInit +
                "import kipy\n"
                "from kipy.geometry import Vector2\n"
                "kicad = kipy.KiCad(socket_path='" + socketPath + "', timeout_ms=5000)\n"
                "board = kicad.get_board()\n";

            // Begin agent transaction for concurrent editing support
            nlohmann::json beginMsg;
            beginMsg["sheet_uuid"] = "";  // Not applicable for PCB
            std::string beginPayload = beginMsg.dump();
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_BEGIN_TRANSACTION, beginPayload );

            // Tell PCB editor to take a snapshot before Python execution
            nlohmann::json snapshotMsg;
            snapshotMsg["type"] = "take_snapshot";
            std::string snapshotPayload = snapshotMsg.dump();
            Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REQUEST, snapshotPayload );
        }

        // Set up completion callback BEFORE starting execution
        bool isPcbMode = ( mode == "pcb" );
        bool isSchMode = ( mode == "sch" );
        m_asyncRequestPending = true;
        wxLogInfo( "TERMINAL: Setting up Python completion callback, mode=%s",
                   isPcbMode ? "pcb" : ( isSchMode ? "sch" : "unknown" ) );
        active->SetPythonCompletionCallback(
            [this, isPcbMode, isSchMode]( const std::string& result, bool success ) {
                wxLogInfo( "TERMINAL: Python completion callback invoked, success=%d, result_len=%zu",
                           success, result.length() );
                // If this was pcb mode, tell PCB editor to detect changes and end transaction
                if( isPcbMode )
                {
                    nlohmann::json detectMsg;
                    detectMsg["type"] = "detect_changes";
                    std::string detectPayload = detectMsg.dump();
                    Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REQUEST, detectPayload );

                    // End agent transaction for concurrent editing support
                    nlohmann::json endMsg;
                    endMsg["commit"] = true;
                    std::string endPayload = endMsg.dump();
                    Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_END_TRANSACTION, endPayload );
                }

                // If this was sch mode, tell SCH editor to detect changes and end transaction
                if( isSchMode )
                {
                    nlohmann::json detectMsg;
                    detectMsg["type"] = "detect_changes";
                    std::string detectPayload = detectMsg.dump();
                    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REQUEST, detectPayload );

                    // End agent transaction for concurrent editing support
                    nlohmann::json endMsg;
                    endMsg["commit"] = true;
                    std::string endPayload = endMsg.dump();
                    Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_END_TRANSACTION, endPayload );
                }

                // This will be called from the main thread when Python completes
                SendAgentResponse( result );
            } );

        // Display the command being executed in the Agent Terminal
        active->DisplayAgentCommand( code, mode );

        // Start Python execution (async, returns immediately)
        std::string fullCode = initCode + code.ToStdString();
        wxLogInfo( "TERMINAL: Starting Python execution, code_len=%zu", fullCode.length() );
        std::string immediateResult = active->RunLocalPython( fullCode );

        // Check if RunLocalPython returned an immediate error (didn't start async execution)
        if( !immediateResult.empty() && immediateResult.find( "Error:" ) == 0 )
        {
            wxLogError( "TERMINAL: RunLocalPython immediate error: %s", immediateResult );
            // Clear callback and send error response immediately
            active->ClearPythonCompletionCallback();
            SendAgentResponse( immediateResult );
        }
        else
        {
            wxLogInfo( "TERMINAL: Python execution started async, waiting for callback" );
        }
        // Otherwise, execution started - callback will be invoked when done
        return;
    }

    // Handle run_terminal (bash commands) - these are still synchronous but fast
    if( cmd.StartsWith( "run_terminal " ) )
    {
        wxString bashCmd = cmd.Mid( 13 ).Trim( false );

        // Validate the command against allowed directory restrictions
        auto allowedPaths = GetAllowedPaths();
        TerminalValidationResult validation =
            TerminalCommandValidator::ValidateCommand( bashCmd.ToStdString(), allowedPaths );

        if( !validation.valid )
        {
            wxLogInfo( "TERMINAL: Command blocked (async): %s", validation.error );
            SendAgentResponse( validation.error );
            return;
        }

        TERMINAL_PANEL* active = nullptr;
        for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
        {
            TERMINAL_PANEL* p = GetPanel( i );
            if( dynamic_cast<AGENT_TERMINAL_PANEL*>( p ) )
            {
                active = p;
                break;
            }
        }
        if( !active )
        {
            AddAgentTerminal( TERMINAL_PANEL::MODE_SYSTEM );
            active = GetPanel( m_notebook->GetPageCount() - 1 );
        }
        if( !active )
        {
            SendAgentResponse( "Error: Could not create/find Agent terminal." );
            return;
        }

        // Display the command being executed in the Agent Terminal
        active->DisplayAgentCommand( bashCmd, "shell" );

        // System commands are still synchronous (they're typically fast)
        std::string result = active->ProcessSystemCommand( bashCmd );
        SendAgentResponse( result );
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
