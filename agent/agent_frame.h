#ifndef AGENT_FRAME_H
#define AGENT_FRAME_H
#include <kiway_player.h>
#include <wx/html/htmlwin.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/button.h>

class AGENT_THREAD;

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
    // Event handlers
    // Event handlers
    void OnSend( wxCommandEvent& aEvent );
    void OnStop( wxCommandEvent& aEvent );
    void OnModelSelection( wxCommandEvent& aEvent );
    void OnTextEnter( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );

    // Thread Event Handlers
    void OnAgentUpdate( wxCommandEvent& aEvent );
    void OnAgentComplete( wxCommandEvent& aEvent );

private:
    wxHtmlWindow* m_chatWindow;
    wxTextCtrl*   m_inputCtrl;
    wxButton*     m_plusButton;
    wxChoice*     m_modeChoice;
    wxChoice*     m_modelChoice;
    wxButton*     m_actionButton;  // Send/Stop
    wxButton*     m_selectionPill; // Displays selected item info

    wxPanel* m_inputPanel;           // Wrapper for input area styling
    wxString m_lastSelectionPayload; // Store full context

    AGENT_THREAD* m_workerThread;

    // Event handlers
    void OnSelectionPillClick( wxCommandEvent& aEvent );
    void OnInputKeyDown( wxKeyEvent& aEvent );
    void OnInputText( wxCommandEvent& aEvent );

    DECLARE_EVENT_TABLE()
};

#endif // AGENT_FRAME_H
