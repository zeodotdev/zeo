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
    void OnTextEnter( wxCommandEvent& aEvent ); // Handled via KeyDown now, but kept for legacy
    void OnKeyDown( wxKeyEvent& aEvent );
    void OnChar( wxKeyEvent& aEvent );

    // Command Processing
    void ExecuteCommand( const wxString& aCmd );
    void ProcessSystemCommand( const wxString& aCmd );
    void ProcessAgentCommand( const wxString& aCmd );

    void OnExit( wxCommandEvent& event );

    enum TERMINAL_MODE
    {
        MODE_SYSTEM,
        MODE_PCB_PYTHON
    };

    DECLARE_EVENT_TABLE()

private:
    wxTextCtrl* m_outputCtrl;
    // wxTextCtrl* m_inputCtrl; // Removed in favor of unified window

    long                  m_lastPromptPos;
    std::vector<wxString> m_history;
    int                   m_historyIndex;
    TERMINAL_MODE         m_mode;

    const wxString PROMPT_SYSTEM = "sys> ";
    const wxString PROMPT_PYTHON = "pcb> ";

    wxString GetPrompt() const { return m_mode == MODE_PCB_PYTHON ? PROMPT_PYTHON : PROMPT_SYSTEM; }
};

#endif // TERMINAL_FRAME_H
