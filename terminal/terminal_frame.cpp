#include "terminal_frame.h"
#include "terminal_panel_agent.h"
#include <kiway_express.h>
#include <mail_type.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <id.h>
#include <kiway.h>
#include <wx/log.h>
#include <wx/menu.h>   // For menu IDs
#include <wx/button.h> // For wxButton

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
                      wxDEFAULT_FRAME_STYLE, "terminal_frame_name", schIUScale )
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
    // Format: "run_terminal_command [tab_id] [mode] [command...]"
    // Or legacy: "run_terminal_command [mode] [command...]" (implies active tab)

    wxString cmd = aCmd;
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

        // Let's implement smart dispatch here.
        if( modeArg == "sys" )
            return panel->ProcessSystemCommand( actualCmd );
        if( modeArg == "ipc" || modeArg == "python" )
            return panel->RunLocalPython( actualCmd ); // Pre-req: mode switch?

        // If mode is just implied by current panel state?
        // Agent Prompt says: "run_terminal_command [mode] [command]"
        // If we supply ID: "run_terminal_command [id] [mode] [command]"

        // For python modes, we need to ensure python is init.
        // RunLocalPython does that.
        // But auto-loading pcb/sch is done in ExecuteCommand "pcb" handler.
        // If Agent says "pcb print...", we want that auto-load.
        // We should move `EnsurePython` and mode switch logic to be accessible/return string.
        // Or simplest: Just call `RunLocalPython`.
        // If specific setup needed, Agent does it?
        // Let's stick to: "sys" -> ProcessSystemCommand. "pcb/sch" -> RunLocalPython (with optional setup if needed).

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
    if( firstArg == "ipc" || firstArg == "python" )
    {
        // Handle auto-switch/init if we want to be nice, or just run python.
        // If we execute "pcb", the panel outputs "Entering PCB Mode".
        // But that returns void.
        // We want the OUTPUT of the command "print(...)".
        // If the command is JUST "pcb file", we return nothing useful?
        // Agent usage: "run_terminal_command pcb print(...)"
        // We should run python code `print(...)`.
        // If we need to init PCB, maybe we should have `RunLocalPython` handle it?
        // No, `RunLocalPython` executes raw code.
        return active->RunLocalPython( rest );
    }

    return "Error: Unknown mode/command format.";
}

void TERMINAL_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    if( aEvent.Command() == MAIL_AGENT_REQUEST )
    {
        std::string payload = aEvent.GetPayload();
        std::string result = ExecuteCommandForAgent( payload );
        Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, result );
    }
}
