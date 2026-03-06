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
// Stub implementations for Windows — Claude Code subprocess not yet supported
CC_SUBPROCESS::CC_SUBPROCESS( wxEvtHandler* aEventSink ) : m_eventSink( aEventSink ) {}
CC_SUBPROCESS::~CC_SUBPROCESS() { Stop(); }
bool CC_SUBPROCESS::Start( const std::string&, const std::string&, const std::string&, const std::string&, const std::string& ) { return false; }
void CC_SUBPROCESS::Stop() { m_running.store( false ); }
void CC_SUBPROCESS::Cleanup() {}
void CC_SUBPROCESS::SendUserMessage( const std::string& ) {}
CC_SUBPROCESS::ReaderThread::ReaderThread( CC_SUBPROCESS* aOwner, int aStdoutFd, int aStderrFd )
    : wxThread( wxTHREAD_DETACHED ), m_owner( aOwner ), m_stdoutFd( aStdoutFd ), m_stderrFd( aStderrFd ) {}
wxThread::ExitCode CC_SUBPROCESS::ReaderThread::Entry() { return (wxThread::ExitCode) 0; }
#else // !_WIN32


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
// ReaderThread — reads stdout/stderr in background and posts events
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
