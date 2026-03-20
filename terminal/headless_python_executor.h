#ifndef HEADLESS_PYTHON_EXECUTOR_H
#define HEADLESS_PYTHON_EXECUTOR_H

#include <wx/event.h>
#include <wx/timer.h>
#include <wx/process.h>
#include <string>
#include <atomic>
#include <functional>

#ifndef _WIN32
#include <sys/types.h>   // pid_t
#else
typedef int pid_t;        // Windows placeholder
#endif

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

    /**
     * Cancel any in-flight Python execution by raising KeyboardInterrupt
     * in the running Python thread. Also kills any tracked child process
     * (e.g., Java Freerouting autorouter). Safe to call when nothing is running.
     */
    void CancelExecution();

    bool        IsPythonRunning() const { return m_pythonRunning.load(); }
    std::string GetLastPythonResult() const { return m_lastPythonResult; }

    /**
     * Register an external child process PID to be killed on cancel.
     * Called by tool handlers that spawn subprocesses (e.g., autorouter).
     * Set to 0 to clear.
     */
    void SetChildProcessPid( pid_t aPid ) { m_childProcessPid.store( aPid ); }

private:
    bool EnsurePython();

    // Python thread execution
    PYTHON_EXEC_THREAD* m_pythonThread;
    std::atomic<bool>   m_pythonRunning;
    std::atomic<pid_t>  m_childProcessPid;   // External subprocess to kill on cancel (e.g. Freerouting)
    bool                m_pythonCancelled;   // True when CancelExecution() was called
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
