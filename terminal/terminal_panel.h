#ifndef TERMINAL_PANEL_H
#define TERMINAL_PANEL_H

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/timer.h>
#include <vector>
#include <string>
#include <atomic>

// Forward declaration
class PYTHON_EXEC_THREAD;

class TERMINAL_PANEL : public wxPanel
{
public:
    enum TERMINAL_MODE
    {
        MODE_SYSTEM,
        MODE_PYTHON, // Standard Embedded Python
        MODE_IPC     // IPC Python Shell (kicad-python)
    };

    TERMINAL_PANEL( wxWindow* aParent, TERMINAL_MODE aMode = MODE_SYSTEM );
    virtual ~TERMINAL_PANEL();

    // Command Processing
    void                ExecuteCommand( const wxString& aCmd );
    virtual std::string ProcessSystemCommand( const wxString& aCmd );
    virtual std::string RunLocalPython( const wxString& aCmd );

    // API for Container/Agent
    TERMINAL_MODE    GetMode() const { return m_mode; }
    void             SetMode( TERMINAL_MODE aMode );
    virtual wxString GetTitle() const;

    // Event handlers
    void OnKeyDown( wxKeyEvent& aEvent );
    void OnChar( wxKeyEvent& aEvent );

    DECLARE_EVENT_TABLE()

protected:
    wxTextCtrl* m_outputCtrl;

    long                  m_lastPromptPos;
    std::vector<wxString> m_history;
    int                   m_historyIndex;
    TERMINAL_MODE         m_mode;
    bool                  m_pythonInitialized;

    // Persistent Shell
    wxProcess*      m_process;
    wxOutputStream* m_shellStdin;
    wxInputStream*  m_shellStdout;
    wxInputStream*  m_shellStderr;
    long            m_pid;
    void            InitShell();
    void            CleanupShell();

    const wxString PROMPT_SYSTEM = "sys> ";
    const wxString PROMPT_PYTHON = ">>> ";
    const wxString PROMPT_IPC = "ipc> ";

    virtual wxString GetPrompt() const;
    bool             EnsurePython();

    // Python thread execution support
    PYTHON_EXEC_THREAD* m_pythonThread;
    std::atomic<bool>   m_pythonRunning;
    std::string         m_lastPythonResult;
    wxTimer             m_pythonPollTimer;
    wxLongLong          m_pythonStartTime;

    void OnPythonOutput( wxThreadEvent& aEvent );
    void OnPythonComplete( wxThreadEvent& aEvent );
    void OnPythonPollTimer( wxTimerEvent& aEvent );
    void FinishPythonExecution();
};

#endif // TERMINAL_PANEL_H
