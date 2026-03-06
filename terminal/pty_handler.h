#ifndef PTY_HANDLER_H
#define PTY_HANDLER_H

#include <wx/event.h>
#include <wx/thread.h>
#include <string>
#include <atomic>

wxDECLARE_EVENT( wxEVT_PTY_DATA, wxThreadEvent );
wxDECLARE_EVENT( wxEVT_PTY_EXIT, wxThreadEvent );

class PTY_HANDLER
{
public:
    PTY_HANDLER( wxEvtHandler* aEventTarget );
    ~PTY_HANDLER();

    bool Start( int aCols = 80, int aRows = 24 );
    void Stop();

    void Write( const std::string& aData );
    void Write( const char* aData, size_t aLen );

    void Resize( int aCols, int aRows );

    bool  IsRunning() const { return m_running.load(); }
    pid_t GetChildPid() const { return m_childPid; }
    int   GetMasterFd() const { return m_masterFd; }

    // Get the foreground process of the terminal (the process the user is interacting with)
    pid_t       GetForegroundPid() const;
    std::string GetForegroundProcessName() const;
    std::string GetForegroundCwd() const;

    // Get a friendly display name for the foreground process
    // For Python processes, this extracts the script name (e.g., "zeo" from "python3 /path/to/zeo")
    std::string GetForegroundDisplayName() const;

private:
    class ReaderThread : public wxThread
    {
    public:
        ReaderThread( PTY_HANDLER* aOwner );
        void* Entry() override;
    private:
        PTY_HANDLER* m_owner;
    };

    wxEvtHandler*     m_eventTarget;
    int               m_masterFd;
    pid_t             m_childPid;
    std::atomic<bool> m_running;
    ReaderThread*     m_readerThread;
};

#endif // PTY_HANDLER_H
