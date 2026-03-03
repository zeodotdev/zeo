#ifndef PTY_WEBVIEW_PANEL_H
#define PTY_WEBVIEW_PANEL_H

#include <wx/panel.h>
#include <wx/timer.h>
#include <memory>
#include <string>
#include <functional>

class WEBVIEW_PANEL;
class PTY_HANDLER;

// Custom event for terminal title changes
wxDECLARE_EVENT( wxEVT_TERMINAL_TITLE_CHANGED, wxCommandEvent );

class PTY_WEBVIEW_PANEL : public wxPanel
{
public:
    PTY_WEBVIEW_PANEL( wxWindow* aParent );
    virtual ~PTY_WEBVIEW_PANEL();

    bool StartShell();
    bool IsShellRunning() const;

    virtual wxString GetTitle() const;

    // Agent integration
    using AgentCallback = std::function<void( const std::string&, bool )>;
    void ExecuteForAgent( const std::string& aCmd, AgentCallback aCallback );

    void WriteToShell( const std::string& aData );
    void DisplayAgentCommand( const wxString& aCmd, const wxString& aMode );

    int GetTermCols() const { return m_cols; }
    int GetTermRows() const { return m_rows; }

protected:
    WEBVIEW_PANEL*                 m_webView;
    std::unique_ptr<PTY_HANDLER>   m_pty;

    int      m_cols;
    int      m_rows;
    bool     m_pageReady;
    bool     m_shellRequested;
    wxString m_termTitle;

    // Buffer PTY data received before JS is ready
    std::string m_pendingOutput;

    // Agent capture state
    AgentCallback m_agentCallback;
    bool          m_agentCapturing;
    std::string   m_agentCaptureBuffer;
    std::string   m_agentSentinelStart;
    std::string   m_agentSentinelEnd;
    bool          m_agentSentinelStartFound;
    wxTimer       m_agentTimeoutTimer;
    static const int AGENT_TIMEOUT_MS = 15000;

    // Queued agent command (if called before PTY ready)
    std::string   m_queuedAgentCmd;
    AgentCallback m_queuedAgentCallback;

    // Event handlers
    void OnPtyData( wxThreadEvent& aEvent );
    void OnPtyExit( wxThreadEvent& aEvent );
    void OnMessage( const wxString& aMsg );
    void OnAgentTimeout( wxTimerEvent& aEvent );

    // Helpers
    void SendDataToTerminal( const std::string& aData );
    void ProcessAgentCapture( const std::string& aData );
    static wxString GetTerminalHtml( bool aLightMode = false );
};

#endif // PTY_WEBVIEW_PANEL_H
