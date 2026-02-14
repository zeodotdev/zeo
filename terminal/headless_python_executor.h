#ifndef HEADLESS_PYTHON_EXECUTOR_H
#define HEADLESS_PYTHON_EXECUTOR_H

#include <wx/event.h>
#include <wx/timer.h>
#include <wx/process.h>
#include <string>
#include <atomic>
#include <functional>

class PYTHON_EXEC_THREAD;

class HEADLESS_PYTHON_EXECUTOR : public wxEvtHandler
{
public:
    HEADLESS_PYTHON_EXECUTOR();
    ~HEADLESS_PYTHON_EXECUTOR();

    using CompletionCallback = std::function<void( const std::string&, bool )>;

    /**
     * Execute Python code asynchronously.
     * Returns empty string on success (async started), or "Error:..." on immediate failure.
     * Calls aCallback when execution completes.
     */
    std::string RunPython( const std::string& aCode, CompletionCallback aCallback );

    /**
     * Execute a system (bash) command synchronously.
     * Returns captured stdout+stderr output.
     */
    std::string RunSystemCommand( const wxString& aCmd );

    bool        IsPythonRunning() const { return m_pythonRunning.load(); }
    std::string GetLastPythonResult() const { return m_lastPythonResult; }

private:
    bool EnsurePython();

    // Python thread execution
    PYTHON_EXEC_THREAD* m_pythonThread;
    std::atomic<bool>   m_pythonRunning;
    std::string         m_lastPythonResult;
    wxTimer             m_pythonPollTimer;
    wxLongLong          m_pythonStartTime;
    CompletionCallback  m_completionCallback;
    bool                m_pythonTimedOut;
    bool                m_pythonInitialized;

    // Persistent shell for system commands
    wxProcess*      m_process;
    wxOutputStream* m_shellStdin;
    wxInputStream*  m_shellStdout;
    wxInputStream*  m_shellStderr;
    long            m_pid;

    void InitShell();
    void CleanupShell();

    // Event handlers
    void OnPythonComplete( wxThreadEvent& aEvent );
    void OnPythonPollTimer( wxTimerEvent& aEvent );
    void FinishPythonExecution();
};

#endif // HEADLESS_PYTHON_EXECUTOR_H
