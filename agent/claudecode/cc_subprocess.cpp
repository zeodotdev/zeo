#include "cc_subprocess.h"
#include "cc_events.h"

#include <wx/log.h>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#endif
#include <errno.h>
#include <sstream>
#include <nlohmann/json.hpp>


#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════════════════
// Windows implementation using CreateProcess / CreatePipe / PeekNamedPipe
// Mirrors ProcessUtil::RunCommand pattern from process_util.h
// ═══════════════════════════════════════════════════════════════════════════

CC_SUBPROCESS::CC_SUBPROCESS( wxEvtHandler* aEventSink ) :
    m_eventSink( aEventSink )
{
}


CC_SUBPROCESS::~CC_SUBPROCESS()
{
    Stop();
}


/**
 * Find the claude CLI executable on Windows.
 * Searches PATH, then common install locations.
 * @return Full path to claude.exe, or empty string if not found.
 */
static std::string FindClaudeExe()
{
    // Check if 'claude' is on PATH by searching with SearchPathA
    char pathBuf[MAX_PATH];

    if( SearchPathA( NULL, "claude.exe", NULL, MAX_PATH, pathBuf, NULL ) > 0 )
        return std::string( pathBuf );

    if( SearchPathA( NULL, "claude.cmd", NULL, MAX_PATH, pathBuf, NULL ) > 0 )
        return std::string( pathBuf );

    // Try common npm global install locations
    const char* appData = getenv( "APPDATA" );

    if( appData )
    {
        std::string npmPath = std::string( appData ) + "\\npm\\claude.cmd";
        if( GetFileAttributesA( npmPath.c_str() ) != INVALID_FILE_ATTRIBUTES )
            return npmPath;
    }

    const char* localAppData = getenv( "LOCALAPPDATA" );

    if( localAppData )
    {
        std::string path = std::string( localAppData ) + "\\Programs\\claude\\claude.exe";
        if( GetFileAttributesA( path.c_str() ) != INVALID_FILE_ATTRIBUTES )
            return path;
    }

    return std::string();
}


bool CC_SUBPROCESS::Start( const std::string& aWorkingDir, const std::string& aMcpConfigPath,
                           const std::string& aModel, const std::string& aSessionId,
                           const std::string& aSystemPrompt )
{
    if( m_running.load() )
        Stop();

    std::string claudePath = FindClaudeExe();

    if( claudePath.empty() )
    {
        wxLogError( "CC_SUBPROCESS: claude CLI not found" );
        return false;
    }

    // Create pipes with inheritable write/read ends for the child
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( SECURITY_ATTRIBUTES );
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;

    if( !CreatePipe( &hStdinRead, &hStdinWrite, &sa, 0 ) )
    {
        wxLogError( "CC_SUBPROCESS: Failed to create stdin pipe" );
        return false;
    }

    // Parent's write end of stdin should NOT be inherited
    SetHandleInformation( hStdinWrite, HANDLE_FLAG_INHERIT, 0 );

    if( !CreatePipe( &hStdoutRead, &hStdoutWrite, &sa, 0 ) )
    {
        CloseHandle( hStdinRead );
        CloseHandle( hStdinWrite );
        return false;
    }

    SetHandleInformation( hStdoutRead, HANDLE_FLAG_INHERIT, 0 );

    if( !CreatePipe( &hStderrRead, &hStderrWrite, &sa, 0 ) )
    {
        CloseHandle( hStdinRead );
        CloseHandle( hStdinWrite );
        CloseHandle( hStdoutRead );
        CloseHandle( hStdoutWrite );
        return false;
    }

    SetHandleInformation( hStderrRead, HANDLE_FLAG_INHERIT, 0 );

    // Build command line
    std::string cmdLine = "\"" + claudePath + "\""
                          " -p"
                          " --output-format stream-json"
                          " --input-format stream-json"
                          " --verbose"
                          " --include-partial-messages"
                          " --permission-mode bypassPermissions"
                          " --model " + aModel;

    if( !aMcpConfigPath.empty() )
        cmdLine += " --mcp-config \"" + aMcpConfigPath + "\"";

    if( !aSessionId.empty() )
        cmdLine += " --resume " + aSessionId;

    if( !aSystemPrompt.empty() )
        cmdLine += " --append-system-prompt \"" + aSystemPrompt + "\"";

    // Set up STARTUPINFO with redirected handles
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory( &pi, sizeof( pi ) );

    // Build mutable command line buffer (CreateProcessA requires it)
    std::vector<char> cmdBuf( cmdLine.begin(), cmdLine.end() );
    cmdBuf.push_back( '\0' );

    // Remove CLAUDECODE env var to avoid nested session detection.
    // SetEnvironmentVariable before CreateProcess affects the inherited env.
    // We restore it after CreateProcess returns.
    char prevClaudeCode[256] = {};
    DWORD hadClaudeCode = GetEnvironmentVariableA( "CLAUDECODE", prevClaudeCode,
                                                    sizeof( prevClaudeCode ) );

    if( hadClaudeCode > 0 )
        SetEnvironmentVariableA( "CLAUDECODE", NULL );

    BOOL created = CreateProcessA(
        NULL,
        cmdBuf.data(),
        NULL, NULL,
        TRUE,                    // inherit handles
        CREATE_NO_WINDOW,        // no console window
        NULL,                    // inherit parent environment (minus CLAUDECODE)
        aWorkingDir.empty() ? NULL : aWorkingDir.c_str(),
        &si,
        &pi
    );

    // Restore CLAUDECODE if it was set
    if( hadClaudeCode > 0 )
        SetEnvironmentVariableA( "CLAUDECODE", prevClaudeCode );

    // Close child-side pipe ends (parent doesn't need them)
    CloseHandle( hStdinRead );
    CloseHandle( hStdoutWrite );
    CloseHandle( hStderrWrite );

    if( !created )
    {
        wxLogError( "CC_SUBPROCESS: CreateProcess failed (error %lu) for: %s",
                    GetLastError(), claudePath.c_str() );
        CloseHandle( hStdinWrite );
        CloseHandle( hStdoutRead );
        CloseHandle( hStderrRead );
        return false;
    }

    // Don't need the thread handle
    CloseHandle( pi.hThread );

    m_hProcess = pi.hProcess;
    m_hStdinWrite = hStdinWrite;
    m_hStdoutRead = hStdoutRead;
    m_hStderrRead = hStderrRead;
    m_running.store( true );

    // Start background reader thread
    m_readerThread = std::make_unique<ReaderThread>( this, m_hStdoutRead, m_hStderrRead );

    if( m_readerThread->Create() != wxTHREAD_NO_ERROR
        || m_readerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogError( "CC_SUBPROCESS: Failed to start reader thread" );
        Stop();
        return false;
    }

    wxLogInfo( "CC_SUBPROCESS: Started claude (pid=%lu) in %s",
               GetProcessId( m_hProcess ), aWorkingDir.c_str() );
    return true;
}


void CC_SUBPROCESS::Stop()
{
    if( !m_running.load() )
        return;

    m_running.store( false );

    if( m_hProcess )
    {
        // Give the process a chance to exit gracefully
        if( WaitForSingleObject( m_hProcess, 500 ) == WAIT_TIMEOUT )
        {
            TerminateProcess( m_hProcess, 1 );
            WaitForSingleObject( m_hProcess, 5000 );
        }
    }

    Cleanup();
}


void CC_SUBPROCESS::Cleanup()
{
    if( m_hStdinWrite )
    {
        CloseHandle( m_hStdinWrite );
        m_hStdinWrite = NULL;
    }

    if( m_hStdoutRead )
    {
        CloseHandle( m_hStdoutRead );
        m_hStdoutRead = NULL;
    }

    if( m_hStderrRead )
    {
        CloseHandle( m_hStderrRead );
        m_hStderrRead = NULL;
    }

    if( m_hProcess )
    {
        CloseHandle( m_hProcess );
        m_hProcess = NULL;
    }

    // Reader thread is detached, it will exit on its own when handles close
    m_readerThread.release();
}


void CC_SUBPROCESS::SendUserMessage( const std::string& aText )
{
    if( !m_running.load() || !m_hStdinWrite )
        return;

    nlohmann::json msg;
    msg["type"] = "user";
    msg["message"]["role"] = "user";
    msg["message"]["content"] = aText;

    std::string line = msg.dump() + "\n";

    wxLogInfo( "CC_SUBPROCESS: Sending user message (%zu bytes)", line.size() );

    DWORD written;
    BOOL ok = WriteFile( m_hStdinWrite, line.c_str(),
                         static_cast<DWORD>( line.size() ), &written, NULL );

    if( !ok )
        wxLogError( "CC_SUBPROCESS: Failed to write to stdin (error %lu)", GetLastError() );
}


// ═══════════════════════════════════════════════════════════════════════════
// ReaderThread (Windows) — reads stdout/stderr via PeekNamedPipe and posts events
// ═══════════════════════════════════════════════════════════════════════════

CC_SUBPROCESS::ReaderThread::ReaderThread( CC_SUBPROCESS* aOwner,
                                           HANDLE aStdoutRead, HANDLE aStderrRead ) :
    wxThread( wxTHREAD_DETACHED ),
    m_owner( aOwner ),
    m_stdoutRead( aStdoutRead ),
    m_stderrRead( aStderrRead )
{
}


wxThread::ExitCode CC_SUBPROCESS::ReaderThread::Entry()
{
    std::string stdoutBuf;
    std::string stderrBuf;
    char buf[8192];
    DWORD bytesRead;

    while( m_owner->m_running.load() )
    {
        bool didWork = false;

        // Read stdout
        if( m_stdoutRead )
        {
            DWORD avail = 0;

            if( PeekNamedPipe( m_stdoutRead, NULL, 0, NULL, &avail, NULL ) && avail > 0 )
            {
                if( ReadFile( m_stdoutRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                    && bytesRead > 0 )
                {
                    stdoutBuf.append( buf, bytesRead );
                    didWork = true;

                    // Extract complete NDJSON lines
                    size_t pos;

                    while( ( pos = stdoutBuf.find( '\n' ) ) != std::string::npos )
                    {
                        std::string line = stdoutBuf.substr( 0, pos );
                        stdoutBuf.erase( 0, pos + 1 );

                        if( !line.empty() )
                        {
                            wxThreadEvent* evt = new wxThreadEvent( EVT_CC_LINE );
                            evt->SetString( wxString::FromUTF8( line.c_str(), line.size() ) );
                            wxQueueEvent( m_owner->m_eventSink, evt );
                        }
                    }
                }
            }
            else if( !PeekNamedPipe( m_stdoutRead, NULL, 0, NULL, &avail, NULL ) )
            {
                // Pipe broken — process likely exited
                m_stdoutRead = NULL;
            }
        }

        // Read stderr
        if( m_stderrRead )
        {
            DWORD avail = 0;

            if( PeekNamedPipe( m_stderrRead, NULL, 0, NULL, &avail, NULL ) && avail > 0 )
            {
                if( ReadFile( m_stderrRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                    && bytesRead > 0 )
                {
                    stderrBuf.append( buf, bytesRead );
                    didWork = true;

                    size_t pos;

                    while( ( pos = stderrBuf.find( '\n' ) ) != std::string::npos )
                    {
                        std::string line = stderrBuf.substr( 0, pos );
                        stderrBuf.erase( 0, pos + 1 );

                        if( !line.empty() )
                        {
                            wxLogInfo( "CC_SUBPROCESS stderr: %s", line.c_str() );

                            wxThreadEvent* evt = new wxThreadEvent( EVT_CC_ERROR );
                            evt->SetString( wxString::FromUTF8( line.c_str(), line.size() ) );
                            wxQueueEvent( m_owner->m_eventSink, evt );
                        }
                    }
                }
            }
            else if( !PeekNamedPipe( m_stderrRead, NULL, 0, NULL, &avail, NULL ) )
            {
                m_stderrRead = NULL;
            }
        }

        // Check if process has exited
        if( !m_stdoutRead && !m_stderrRead )
            break;

        if( m_owner->m_hProcess )
        {
            DWORD exitCode;

            if( GetExitCodeProcess( m_owner->m_hProcess, &exitCode )
                && exitCode != STILL_ACTIVE )
            {
                // Drain remaining data
                if( m_stdoutRead )
                {
                    while( ReadFile( m_stdoutRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                           && bytesRead > 0 )
                    {
                        stdoutBuf.append( buf, bytesRead );
                    }

                    size_t pos;

                    while( ( pos = stdoutBuf.find( '\n' ) ) != std::string::npos )
                    {
                        std::string line = stdoutBuf.substr( 0, pos );
                        stdoutBuf.erase( 0, pos + 1 );

                        if( !line.empty() )
                        {
                            wxThreadEvent* evt = new wxThreadEvent( EVT_CC_LINE );
                            evt->SetString( wxString::FromUTF8( line.c_str(), line.size() ) );
                            wxQueueEvent( m_owner->m_eventSink, evt );
                        }
                    }
                }

                break;
            }
        }

        if( !didWork )
            Sleep( 10 ); // avoid busy-waiting when no data available
    }

    // Get exit code
    m_owner->m_running.store( false );

    DWORD exitCode = (DWORD) -1;

    if( m_owner->m_hProcess )
        GetExitCodeProcess( m_owner->m_hProcess, &exitCode );

    wxThreadEvent* evt = new wxThreadEvent( EVT_CC_EXIT );
    evt->SetInt( static_cast<int>( exitCode ) );
    wxQueueEvent( m_owner->m_eventSink, evt );

    wxLogInfo( "CC_SUBPROCESS: Reader thread exiting (exit code=%d)", (int) exitCode );

    return (wxThread::ExitCode) 0;
}


#else // !_WIN32
// ═══════════════════════════════════════════════════════════════════════════
// POSIX implementation using fork / pipe / poll
// ═══════════════════════════════════════════════════════════════════════════

CC_SUBPROCESS::CC_SUBPROCESS( wxEvtHandler* aEventSink ) :
    m_eventSink( aEventSink )
{
}


CC_SUBPROCESS::~CC_SUBPROCESS()
{
    Stop();
}


bool CC_SUBPROCESS::Start( const std::string& aWorkingDir, const std::string& aMcpConfigPath,
                           const std::string& aModel, const std::string& aSessionId,
                           const std::string& aSystemPrompt )
{
    if( m_running.load() )
        Stop();

    // Create pipes: stdin (parent writes), stdout (parent reads), stderr (parent reads)
    int stdinPipe[2], stdoutPipe[2], stderrPipe[2];

    if( pipe( stdinPipe ) != 0 || pipe( stdoutPipe ) != 0 || pipe( stderrPipe ) != 0 )
    {
        wxLogError( "CC_SUBPROCESS: Failed to create pipes" );
        return false;
    }

    pid_t pid = fork();

    if( pid < 0 )
    {
        wxLogError( "CC_SUBPROCESS: Failed to fork" );
        close( stdinPipe[0] ); close( stdinPipe[1] );
        close( stdoutPipe[0] ); close( stdoutPipe[1] );
        close( stderrPipe[0] ); close( stderrPipe[1] );
        return false;
    }

    if( pid == 0 )
    {
        // ── Child process ───────────────────────────────────────────────

        // Redirect stdin/stdout/stderr
        close( stdinPipe[1] );   // close write end of stdin
        close( stdoutPipe[0] );  // close read end of stdout
        close( stderrPipe[0] );  // close read end of stderr

        dup2( stdinPipe[0], STDIN_FILENO );
        dup2( stdoutPipe[1], STDOUT_FILENO );
        dup2( stderrPipe[1], STDERR_FILENO );

        close( stdinPipe[0] );
        close( stdoutPipe[1] );
        close( stderrPipe[1] );

        // Clear CLAUDECODE env var to avoid "nested session" detection
        unsetenv( "CLAUDECODE" );

        // Change to working directory
        if( !aWorkingDir.empty() )
            chdir( aWorkingDir.c_str() );

        // Build command arguments
        // claude -p --output-format stream-json --verbose --include-partial-messages
        //        --permission-mode bypassPermissions --model <model>
        //        [--mcp-config <path>] [--resume <id>]
        std::vector<std::string> args;
        args.push_back( "claude" );
        args.push_back( "-p" );
        args.push_back( "--output-format" );
        args.push_back( "stream-json" );
        args.push_back( "--input-format" );
        args.push_back( "stream-json" );
        args.push_back( "--verbose" );
        args.push_back( "--include-partial-messages" );
        args.push_back( "--permission-mode" );
        args.push_back( "bypassPermissions" );
        args.push_back( "--model" );
        args.push_back( aModel );

        if( !aMcpConfigPath.empty() )
        {
            args.push_back( "--mcp-config" );
            args.push_back( aMcpConfigPath );
        }

        if( !aSessionId.empty() )
        {
            args.push_back( "--resume" );
            args.push_back( aSessionId );
        }

        if( !aSystemPrompt.empty() )
        {
            args.push_back( "--append-system-prompt" );
            args.push_back( aSystemPrompt );
        }

        // Convert to C-style argv
        std::vector<const char*> argv;
        for( const auto& a : args )
            argv.push_back( a.c_str() );
        argv.push_back( nullptr );

        // Try common claude locations
        // First try PATH via execvp
        execvp( "claude", const_cast<char* const*>( argv.data() ) );

        // If execvp fails, try common locations
        const char* paths[] = {
            "/usr/local/bin/claude",
            "/opt/homebrew/bin/claude",
            nullptr
        };

        for( int i = 0; paths[i]; ++i )
        {
            argv[0] = paths[i];
            execv( paths[i], const_cast<char* const*>( argv.data() ) );
        }

        // Try ~/.local/bin/claude
        std::string homePath = std::string( getenv( "HOME" ) ? getenv( "HOME" ) : "" )
                               + "/.local/bin/claude";
        argv[0] = homePath.c_str();
        execv( homePath.c_str(), const_cast<char* const*>( argv.data() ) );

        _exit( 127 );
    }

    // ── Parent process ──────────────────────────────────────────────────

    close( stdinPipe[0] );    // close read end of stdin pipe
    close( stdoutPipe[1] );   // close write end of stdout pipe
    close( stderrPipe[1] );   // close write end of stderr pipe

    m_pid = pid;
    m_stdinFd = stdinPipe[1];
    m_stdoutFd = stdoutPipe[0];
    m_stderrFd = stderrPipe[0];
    m_running.store( true );

    // Set stdout/stderr to non-blocking (reader uses poll())
    fcntl( m_stdoutFd, F_SETFL, fcntl( m_stdoutFd, F_GETFL ) | O_NONBLOCK );
    fcntl( m_stderrFd, F_SETFL, fcntl( m_stderrFd, F_GETFL ) | O_NONBLOCK );

    // Start background reader thread
    m_readerThread = std::make_unique<ReaderThread>( this, m_stdoutFd, m_stderrFd );

    if( m_readerThread->Create() != wxTHREAD_NO_ERROR
        || m_readerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogError( "CC_SUBPROCESS: Failed to start reader thread" );
        Stop();
        return false;
    }

    wxLogInfo( "CC_SUBPROCESS: Started claude (pid=%d) in %s", (int) m_pid, aWorkingDir.c_str() );
    return true;
}


void CC_SUBPROCESS::Stop()
{
    if( !m_running.load() )
        return;

    m_running.store( false );

    if( m_pid > 0 )
    {
        kill( m_pid, SIGTERM );

        // Give it a moment, then force kill
        int status;
        pid_t result = waitpid( m_pid, &status, WNOHANG );

        if( result == 0 )
        {
            usleep( 500000 ); // 500ms grace period
            result = waitpid( m_pid, &status, WNOHANG );

            if( result == 0 )
            {
                kill( m_pid, SIGKILL );
                waitpid( m_pid, &status, 0 );
            }
        }

        m_pid = -1;
    }

    Cleanup();
}


void CC_SUBPROCESS::Cleanup()
{
    if( m_stdinFd >= 0 )
    {
        close( m_stdinFd );
        m_stdinFd = -1;
    }

    // stdout/stderr fds are closed by the reader thread or here if thread not started
    if( m_stdoutFd >= 0 )
    {
        close( m_stdoutFd );
        m_stdoutFd = -1;
    }

    if( m_stderrFd >= 0 )
    {
        close( m_stderrFd );
        m_stderrFd = -1;
    }

    // Reader thread is detached, it will exit on its own when fds close
    m_readerThread.release();
}


void CC_SUBPROCESS::SendUserMessage( const std::string& aText )
{
    if( !m_running.load() || m_stdinFd < 0 )
        return;

    nlohmann::json msg;
    msg["type"] = "user";
    msg["message"]["role"] = "user";
    msg["message"]["content"] = aText;

    std::string line = msg.dump() + "\n";

    wxLogInfo( "CC_SUBPROCESS: Sending user message (%zu bytes)", line.size() );

    ssize_t written = write( m_stdinFd, line.c_str(), line.size() );

    if( written < 0 )
        wxLogError( "CC_SUBPROCESS: Failed to write to stdin: %s", strerror( errno ) );
}


// ═══════════════════════════════════════════════════════════════════════════
// ReaderThread (POSIX) — reads stdout/stderr in background and posts events
// ═══════════════════════════════════════════════════════════════════════════

CC_SUBPROCESS::ReaderThread::ReaderThread( CC_SUBPROCESS* aOwner, int aStdoutFd, int aStderrFd ) :
    wxThread( wxTHREAD_DETACHED ),
    m_owner( aOwner ),
    m_stdoutFd( aStdoutFd ),
    m_stderrFd( aStderrFd )
{
}


wxThread::ExitCode CC_SUBPROCESS::ReaderThread::Entry()
{
    struct pollfd fds[2] = {
        { m_stdoutFd, POLLIN, 0 },
        { m_stderrFd, POLLIN, 0 }
    };

    std::string stdoutBuf;
    std::string stderrBuf;
    char buf[8192];
    int activeFds = 2;

    while( m_owner->m_running.load() && activeFds > 0 )
    {
        int ret = poll( fds, 2, 100 ); // 100ms poll timeout for shutdown check

        if( ret < 0 )
        {
            if( errno == EINTR )
                continue;
            break;
        }

        if( ret == 0 )
            continue; // timeout, loop back to check m_running

        // Read stdout
        if( fds[0].fd >= 0 && ( fds[0].revents & ( POLLIN | POLLHUP ) ) )
        {
            ssize_t n = read( fds[0].fd, buf, sizeof( buf ) - 1 );

            if( n > 0 )
            {
                stdoutBuf.append( buf, n );

                // Extract complete lines (NDJSON: one JSON object per line)
                size_t pos;
                while( ( pos = stdoutBuf.find( '\n' ) ) != std::string::npos )
                {
                    std::string line = stdoutBuf.substr( 0, pos );
                    stdoutBuf.erase( 0, pos + 1 );

                    if( !line.empty() )
                    {
                        wxThreadEvent* evt = new wxThreadEvent( EVT_CC_LINE );
                        evt->SetString( wxString::FromUTF8( line.c_str(), line.size() ) );
                        wxQueueEvent( m_owner->m_eventSink, evt );
                    }
                }
            }
            else if( n == 0 )
            {
                // EOF on stdout
                close( fds[0].fd );
                fds[0].fd = -1;
                activeFds--;
            }
        }

        // Read stderr
        if( fds[1].fd >= 0 && ( fds[1].revents & ( POLLIN | POLLHUP ) ) )
        {
            ssize_t n = read( fds[1].fd, buf, sizeof( buf ) - 1 );

            if( n > 0 )
            {
                stderrBuf.append( buf, n );

                // Post stderr lines
                size_t pos;
                while( ( pos = stderrBuf.find( '\n' ) ) != std::string::npos )
                {
                    std::string line = stderrBuf.substr( 0, pos );
                    stderrBuf.erase( 0, pos + 1 );

                    if( !line.empty() )
                    {
                        wxLogInfo( "CC_SUBPROCESS stderr: %s", line.c_str() );

                        wxThreadEvent* evt = new wxThreadEvent( EVT_CC_ERROR );
                        evt->SetString( wxString::FromUTF8( line.c_str(), line.size() ) );
                        wxQueueEvent( m_owner->m_eventSink, evt );
                    }
                }
            }
            else if( n == 0 )
            {
                close( fds[1].fd );
                fds[1].fd = -1;
                activeFds--;
            }
        }
    }

    // Process has exited — reap it
    int status = 0;
    if( m_owner->m_pid > 0 )
        waitpid( m_owner->m_pid, &status, WNOHANG );

    m_owner->m_running.store( false );

    int exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;

    wxThreadEvent* evt = new wxThreadEvent( EVT_CC_EXIT );
    evt->SetInt( exitCode );
    wxQueueEvent( m_owner->m_eventSink, evt );

    wxLogInfo( "CC_SUBPROCESS: Reader thread exiting (exit code=%d)", exitCode );

    return (wxThread::ExitCode) 0;
}

#endif // !_WIN32
