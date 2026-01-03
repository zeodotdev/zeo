#ifndef TERMINAL_FRAME_H
#define TERMINAL_FRAME_H

#include <kiway_player.h>
#include <wx/textctrl.h>

class TERMINAL_FRAME : public KIWAY_PLAYER
{
public:
    TERMINAL_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~TERMINAL_FRAME();

    // KIWAY_PLAYER virtual overrides
    bool OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override { return true; }
    void ShowChangedLanguage() override {}
    void KiwayMailIn( KIWAY_EXPRESS& aEvent ) override;

    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Event handlers
    void OnTextEnter( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );

    DECLARE_EVENT_TABLE()

private:
    wxTextCtrl* m_outputCtrl;
    wxTextCtrl* m_inputCtrl;
};

#endif // TERMINAL_FRAME_H
