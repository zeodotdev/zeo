#ifndef TERMINAL_FRAME_H
#define TERMINAL_FRAME_H

#include <kiway_player.h>
#include <wx/aui/auibook.h>
#include "terminal_panel.h"
#include <memory>
#include <string>

class HEADLESS_PYTHON_EXECUTOR;
class TOOL_SCRIPT_LOADER;

class TERMINAL_FRAME : public KIWAY_PLAYER
{
public:
    TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~TERMINAL_FRAME();

    // KIWAY_PLAYER overrides
    bool      OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override { return true; }
    void      ShowChangedLanguage() override {}
    void      KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) override;
    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Override CommonSettingsChanged to avoid toolbar recreation (TERMINAL_FRAME has no toolbars)
    void      CommonSettingsChanged( int aFlags ) override;

    // Event handlers
    void OnExit( wxCommandEvent& event );
    void OnNewTab( wxCommandEvent& event );
    void OnCloseTab( wxCommandEvent& event );
    void OnTabClosed( wxAuiNotebookEvent& event );
    void OnTabClosedDone( wxAuiNotebookEvent& event );
    void OnTerminalTitleChanged( wxCommandEvent& event );
    void OnSize( wxSizeEvent& event ) override;
    void OnSwitchTab( wxCommandEvent& event );
    void OnShowWindow( wxShowEvent& event );

    // Tab Management
    void            AddTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode = TERMINAL_PANEL::MODE_SYSTEM );
    void            UpdateTabClosing();
    TERMINAL_PANEL* GetActivePanel();
    TERMINAL_PANEL* GetPanel( int aIndex );

    // Agent Command Dispatch (legacy synchronous - will be deprecated)
    std::string ExecuteCommandForAgent( const wxString& aCmd );

    // Async Agent Command Dispatch (non-blocking, sends result via ExpressMail)
    void ExecuteCommandForAgentAsync( const wxString& aCmd );

    /**
     * Execute an MCP tool by name. Looks up the tool in the manifest, builds the
     * Python command, runs it synchronously with wxYield polling, and returns the result.
     * @param aToolName the tool name (e.g. "sch_add", "pcb_place")
     * @param aArgsJson the tool arguments as a JSON string
     * @return the tool execution result (JSON string or error)
     */
    std::string ExecuteMCPTool( const std::string& aToolName, const std::string& aArgsJson );

    /**
     * Lazy-init accessor for the tool script loader.
     */
    TOOL_SCRIPT_LOADER* GetToolLoader();

    DECLARE_EVENT_TABLE()

private:
    wxAuiNotebook* m_notebook;
    HEADLESS_PYTHON_EXECUTOR* m_headlessExecutor;
    std::unique_ptr<TOOL_SCRIPT_LOADER> m_toolLoader;

    // Track if we have an active async request
    bool m_asyncRequestPending;

    // Gather all directories the agent is allowed to modify files in
    std::vector<std::string> GetAllowedPaths();

    // Track the target sheet UUID for the current agent conversation turn.
    std::string m_agentTargetSheetUuid;
    bool m_hasAgentTargetSheet;

    // Helper to send result back to agent via ExpressMail
    void SendAgentResponse( const std::string& aResult );

    // Focus the active terminal panel
    void FocusActiveTerminal();

    // Handle keyboard shortcuts from native key monitor
    void HandleKeyShortcut( int aShortcut );
};

#endif // TERMINAL_FRAME_H
