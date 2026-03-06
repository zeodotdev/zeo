#ifndef CC_SUBPROCESS_H
#define CC_SUBPROCESS_H

#include <wx/event.h>
#include <wx/thread.h>
#include <string>
#include <atomic>
#include <memory>

/**
 * CC_SUBPROCESS manages a Claude Code CLI process (`claude -p`) communicating
 * via NDJSON over stdin/stdout pipes.
 *
 * Background reader thread reads lines from stdout and posts EVT_CC_LINE events
 * to the event sink (main thread). Stderr is captured and posted as EVT_CC_ERROR.
 * Process exit is posted as EVT_CC_EXIT.
 */
class CC_SUBPROCESS
{
public:
    CC_SUBPROCESS( wxEvtHandler* aEventSink );
    ~CC_SUBPROCESS();

    /**
     * Start the Claude Code subprocess.
     * @param aWorkingDir  Working directory for the process
     * @param aMcpConfigPath  Path to MCP config JSON file (can be empty)
     * @param aModel  Claude model to use (e.g. "claude-opus-4-6")
     * @param aSessionId  Optional session ID to resume
     * @param aSystemPrompt  Optional system prompt to append via --append-system-prompt
     * @return true if process started successfully
     */
    bool Start( const std::string& aWorkingDir, const std::string& aMcpConfigPath,
                const std::string& aModel = "claude-opus-4-6",
                const std::string& aSessionId = "",
                const std::string& aSystemPrompt = "" );

    void Stop();
    bool IsRunning() const { return m_running.load(); }

    /**
     * Send a user message via stdin as NDJSON.
     * Format: {"type":"user","message":{"role":"user","content":"<text>"}}
     */
    void SendUserMessage( const std::string& aText );

private:
    class ReaderThread : public wxThread
    {
    public:
        ReaderThread( CC_SUBPROCESS* aOwner, int aStdoutFd, int aStderrFd );
        wxThread::ExitCode Entry() override;

    private:
        CC_SUBPROCESS* m_owner;
        int            m_stdoutFd;
        int            m_stderrFd;
    };

    wxEvtHandler*                  m_eventSink;
    pid_t                          m_pid = -1;
    int                            m_stdinFd = -1;
    int                            m_stdoutFd = -1;
    int                            m_stderrFd = -1;
    std::atomic<bool>              m_running{false};
    std::unique_ptr<ReaderThread>  m_readerThread;

    void Cleanup();
};

#endif // CC_SUBPROCESS_H
