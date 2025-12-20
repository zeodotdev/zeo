
#ifndef KICAD_AGENT_FRAME_H
#define KICAD_AGENT_FRAME_H

#include <kiway_player.h>
#include <wx/textctrl.h>
#include <wx/button.h>

class KICAD_AGENT_FRAME : public KIWAY_PLAYER
{
public:
    KICAD_AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~KICAD_AGENT_FRAME();

    // KIWAY_PLAYER overrides
    void      LoadSettings( APP_SETTINGS_BASE* aCfg ) override;
    void      SaveSettings( APP_SETTINGS_BASE* aCfg ) override;
    void      CommonSettingsChanged( int aFlags ) override;
    void      ProjectFileChanged();
    void      ShowChangedLanguage() override;
    bool      OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override;
    wxWindow* GetToolCanvas() const override { return nullptr; } // No tool framework canvas for now

protected:
    void doReCreateMenuBar() override;

private:
    void onRunQuery( wxCommandEvent& aEvent );
    void onCancel( wxCommandEvent& aEvent ); // Added for completeness, though Close() usually handles it
    void OnExit( wxCommandEvent& aEvent );

    wxTextCtrl* m_promptText;
    wxButton*   m_btnRun;
    wxButton*   m_btnCancel;

    DECLARE_EVENT_TABLE()
};

#endif // KICAD_AGENT_FRAME_H
