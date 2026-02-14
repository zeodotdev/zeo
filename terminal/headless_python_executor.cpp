#include "headless_python_executor.h"
#include "python_exec_thread.h"

#include <wx/log.h>
#include <wx/utils.h>
#include <wx/txtstrm.h>
#include <wx/filename.h>
#include <python_scripting.h>
#include <api/api_handler_editor.h>


HEADLESS_PYTHON_EXECUTOR::HEADLESS_PYTHON_EXECUTOR() :
        m_pythonThread( nullptr ),
        m_pythonRunning( false ),
        m_pythonTimedOut( false ),
        m_pythonInitialized( false ),
        m_process( nullptr ),
        m_shellStdin( nullptr ),
        m_shellStdout( nullptr ),
        m_shellStderr( nullptr ),
        m_pid( -1 )
{
    m_pythonPollTimer.SetOwner( this );

    Bind( wxEVT_PYTHON_COMPLETE, &HEADLESS_PYTHON_EXECUTOR::OnPythonComplete, this );
    Bind( wxEVT_TIMER, &HEADLESS_PYTHON_EXECUTOR::OnPythonPollTimer, this,
          m_pythonPollTimer.GetId() );
}


HEADLESS_PYTHON_EXECUTOR::~HEADLESS_PYTHON_EXECUTOR()
{
    m_pythonPollTimer.Stop();

    if( m_pythonThread )
    {
        m_pythonThread->RequestStop();
        m_pythonThread->Wait();
        delete m_pythonThread;
        m_pythonThread = nullptr;
    }

    CleanupShell();
}


bool HEADLESS_PYTHON_EXECUTOR::EnsurePython()
{
#ifdef KICAD_SCRIPTING
    if( m_pythonInitialized )
        return true;

    if( !SCRIPTING::IsWxAvailable() )
    {
        wxString stockPath = SCRIPTING::PyScriptingPath( SCRIPTING::STOCK );
        wxString userPath = SCRIPTING::PyScriptingPath( SCRIPTING::USER );

        if( !InitPythonScripting( stockPath.ToUTF8(), userPath.ToUTF8() ) )
        {
            wxLogError( "HEADLESS_EXEC: Failed to initialize Python" );
            return false;
        }
    }

    m_pythonInitialized = true;
    return true;
#else
    return false;
#endif
}


std::string HEADLESS_PYTHON_EXECUTOR::RunPython( const std::string& aCode,
                                                  CompletionCallback aCallback )
{
#ifdef KICAD_SCRIPTING
    if( !EnsurePython() )
        return "Error: Python not initialized";

    if( m_pythonRunning.load() )
        return "Error: Python execution already in progress";

    SetIPCShellBlocking( true );

    m_pythonRunning.store( true );
    m_lastPythonResult.clear();
    m_pythonTimedOut = false;
    m_completionCallback = aCallback;

    m_pythonThread = new PYTHON_EXEC_THREAD( this, aCode );

    if( m_pythonThread->Run() != wxTHREAD_NO_ERROR )
    {
        delete m_pythonThread;
        m_pythonThread = nullptr;
        m_pythonRunning.store( false );
        m_completionCallback = nullptr;
        SetIPCShellBlocking( false );
        return "Error: Failed to start Python execution thread";
    }

    m_pythonStartTime = wxGetLocalTimeMillis();
    m_pythonPollTimer.Start( 50 );

    return "";
#else
    return "Error: Scripting not supported";
#endif
}


void HEADLESS_PYTHON_EXECUTOR::OnPythonComplete( wxThreadEvent& aEvent )
{
    m_lastPythonResult = aEvent.GetString().ToStdString();
    m_pythonRunning.store( false );
}


void HEADLESS_PYTHON_EXECUTOR::OnPythonPollTimer( wxTimerEvent& aEvent )
{
    if( !m_pythonRunning.load() )
    {
        FinishPythonExecution();
        return;
    }

    long timeoutMs = 10000;

    if( wxGetLocalTimeMillis() - m_pythonStartTime > timeoutMs )
    {
        m_pythonPollTimer.Stop();

        if( m_pythonThread )
        {
            m_pythonThread->RequestStop();
            m_pythonThread->Wait();
            delete m_pythonThread;
            m_pythonThread = nullptr;
        }

        m_pythonRunning.store( false );
        m_pythonTimedOut = true;

        wxLogWarning( "HEADLESS_EXEC: Python execution timed out" );
        FinishPythonExecution();
    }
}


void HEADLESS_PYTHON_EXECUTOR::FinishPythonExecution()
{
    wxLogInfo( "HEADLESS_EXEC: FinishPythonExecution, timedOut=%d, hasCallback=%d",
               m_pythonTimedOut, m_completionCallback != nullptr );

    m_pythonPollTimer.Stop();

    if( m_pythonThread )
    {
        m_pythonThread->Wait();
        delete m_pythonThread;
        m_pythonThread = nullptr;
    }

    SetIPCShellBlocking( false );

    if( m_completionCallback )
    {
        bool        success = !m_pythonTimedOut;
        std::string result = m_pythonTimedOut
                ? "Error: Python execution timed out"
                : ( m_lastPythonResult.empty() ? "(no output)" : m_lastPythonResult );

        wxLogInfo( "HEADLESS_EXEC: Invoking callback, success=%d, result_len=%zu",
                   success, result.length() );

        m_completionCallback( result, success );
        m_completionCallback = nullptr;
    }

    m_pythonTimedOut = false;
}


// ---- System command execution ----

void HEADLESS_PYTHON_EXECUTOR::InitShell()
{
    if( m_process )
        return;

    m_process = new wxProcess( this );
    m_process->Redirect();

    wxString shell = "/bin/zsh";

    if( !wxFileExists( shell ) )
        shell = "/bin/bash";

    m_pid = wxExecute( shell + " -s", wxEXEC_ASYNC, m_process );

    if( m_pid > 0 )
    {
        m_shellStdin = m_process->GetOutputStream();
        m_shellStdout = m_process->GetInputStream();
        m_shellStderr = m_process->GetErrorStream();
    }
    else
    {
        wxLogError( "HEADLESS_EXEC: Failed to launch system shell" );
        delete m_process;
        m_process = nullptr;
    }
}


void HEADLESS_PYTHON_EXECUTOR::CleanupShell()
{
    if( m_process )
    {
        m_process->Detach();

        if( m_process->GetOutputStream() )
        {
            wxTextOutputStream os( *m_process->GetOutputStream() );
            os << "exit\n";
            m_process->CloseOutput();
        }

        if( m_pid > 0 && wxProcess::Exists( m_pid ) )
            wxProcess::Kill( m_pid, wxSIGKILL );

        // Don't delete m_process -- wxWidgets holds a reference in wxExecuteData
        // that triggers use-after-free if deleted before async signal handling.
        m_process = nullptr;
        m_pid = -1;
    }
}


std::string HEADLESS_PYTHON_EXECUTOR::RunSystemCommand( const wxString& aCmd )
{
    InitShell();

    if( !m_process || !m_shellStdin )
        return "Error: Shell not running.\n";

    wxString sentinel = "__KICAD_AGENT_CMD_END__";
    wxString cmdLine = aCmd + "\n";
    cmdLine += "echo \"" + sentinel + "\"\n";

    m_shellStdin->Write( cmdLine.c_str(), cmdLine.Length() );

    std::string fullOutput;
    bool        sentinelFound = false;
    wxLongLong  startTime = wxGetLocalTimeMillis();
    long        timeoutMs = 15000;

    while( !sentinelFound )
    {
        bool hasData = false;

        if( m_shellStdout && m_shellStdout->CanRead() )
        {
            char   buf[1024];
            m_shellStdout->Read( buf, sizeof( buf ) - 1 );
            size_t count = m_shellStdout->LastRead();

            if( count > 0 )
            {
                buf[count] = 0;
                wxString chunk = wxString::FromUTF8( buf );

                int pos = chunk.Find( sentinel );

                if( pos != wxNOT_FOUND )
                {
                    sentinelFound = true;
                    chunk = chunk.Left( pos );
                    chunk.Trim();
                }

                fullOutput += chunk.ToStdString();
                hasData = true;
            }
        }

        if( m_shellStderr && m_shellStderr->CanRead() )
        {
            char   buf[1024];
            m_shellStderr->Read( buf, sizeof( buf ) - 1 );
            size_t count = m_shellStderr->LastRead();

            if( count > 0 )
            {
                buf[count] = 0;
                fullOutput += wxString::FromUTF8( buf ).ToStdString();
                hasData = true;
            }
        }

        if( !hasData )
        {
            wxYield();
            wxMilliSleep( 10 );
        }

        if( wxGetLocalTimeMillis() - startTime > timeoutMs )
        {
            wxLogWarning( "HEADLESS_EXEC: System command timed out" );
            break;
        }
    }

    return fullOutput;
}
