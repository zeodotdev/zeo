#ifndef AGENT_FRAME_H
#define AGENT_FRAME_H

#include <kiway_player.h>

class AGENT_FRAME : public KIWAY_PLAYER
{
public:
    AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~AGENT_FRAME();

    // KIWAY_PLAYER virtual overrides
    bool OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override { return true; }
    void ShowChangedLanguage() override;
    void KiwayMailIn( KIWAY_EXPRESS& aEvent ) override;

    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Event handlers
    void onRunQuery( wxCommandEvent& aEvent );
    void onCancel( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );

private:
    wxButton*   m_btnRun;
    wxButton*   m_btnCancel;
    wxTextCtrl* m_textCtrl;

    DECLARE_EVENT_TABLE()
};

#endif // AGENT_FRAME_H
