#ifndef TERMINAL_FRAME_H
#define TERMINAL_FRAME_H

#include <kiway_player.h>
#include <wx/aui/auibook.h>
#include "terminal_panel.h"

class TERMINAL_FRAME : public KIWAY_PLAYER
{
public:
    TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~TERMINAL_FRAME();

    // KIWAY_PLAYER overrides
    bool      OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override { return true; }
    void      ShowChangedLanguage() override {}
    void      KiwayMailIn( KIWAY_EXPRESS& aEvent ) override;
    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Event handlers
    void OnExit( wxCommandEvent& event );
    void OnNewTab( wxCommandEvent& event );
    void OnNewAgentTab( wxCommandEvent& event );
    void OnCloseTab( wxCommandEvent& event );
    void OnTabClosed( wxAuiNotebookEvent& event );
    void OnTabClosedDone( wxAuiNotebookEvent& event );

    // Tab Management
    void            AddTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode = TERMINAL_PANEL::MODE_SYSTEM );
    void            AddAgentTerminal( TERMINAL_PANEL::TERMINAL_MODE aMode = TERMINAL_PANEL::MODE_SYSTEM );
    void            UpdateTabClosing();
    TERMINAL_PANEL* GetActivePanel();
    TERMINAL_PANEL* GetPanel( int aIndex );

    // Agent Command Dispatch
    std::string ExecuteCommandForAgent( const wxString& aCmd );

    DECLARE_EVENT_TABLE()

private:
    wxAuiNotebook* m_notebook;
};

#endif // TERMINAL_FRAME_H
