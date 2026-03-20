#ifndef PROCESS_UTIL_H
#define PROCESS_UTIL_H

#include <string>
#include <chrono>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <wx/log.h>

namespace ProcessUtil
{

/**
 * Create a unique temporary directory.
 * Cross-platform replacement for POSIX mkdtemp().
 *
 * @param aPrefix  Prefix for the directory name (e.g. "zeo_autoroute_")
 * @return Full path to the created directory, or empty string on failure.
 */
inline std::string MakeTempDir( const std::string& aPrefix )
{
#ifdef _WIN32
    char tempPath[MAX_PATH];
    DWORD len = GetTempPathA( MAX_PATH, tempPath );

    if( len == 0 || len > MAX_PATH )
        return std::string();

    LARGE_INTEGER counter;
    QueryPerformanceCounter( &counter );
    std::string dirName = aPrefix + std::to_string( counter.QuadPart )
                          + "_" + std::to_string( GetCurrentProcessId() );
    std::string fullPath = std::string( tempPath ) + dirName;

    if( !CreateDirectoryA( fullPath.c_str(), NULL ) )
    {
        if( GetLastError() != ERROR_ALREADY_EXISTS )
            return std::string();
    }

    return fullPath;
#else
    std::string tempTemplate = std::string( "/tmp/" ) + aPrefix + "XXXXXX";
    std::vector<char> buf( tempTemplate.begin(), tempTemplate.end() );
    buf.push_back( '\0' );

    if( !mkdtemp( buf.data() ) )
        return std::string();

    return std::string( buf.data() );
#endif
}


/**
 * Run a shell command, capturing stdout and stderr with an optional timeout.
 * Cross-platform replacement for fork/exec/pipe/poll/waitpid.
 *
 * @param aCommand     The command to execute (passed to shell).
 * @param aStdout      [out] Captured stdout.
 * @param aStderr      [out] Captured stderr.
 * @param aTimeoutSec  Timeout in seconds (default 120). Returns -1 on timeout.
 * @param aChildPid    [optional] Atomic to receive the child PID while running.
 *                     Set to the child PID after fork, cleared to 0 after wait.
 *                     If non-null and the value is set to -1 externally, the child
 *                     will be killed (cancel support).
 * @return Exit code, or -1 on timeout/error.
 */
inline int RunCommand( const std::string& aCommand, std::string& aStdout,
                       std::string& aStderr, int aTimeoutSec = 120,
                       std::atomic<pid_t>* aChildPid = nullptr )
{
    aStdout.clear();
    aStderr.clear();

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( SECURITY_ATTRIBUTES );
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;

    if( !CreatePipe( &hStdoutRead, &hStdoutWrite, &sa, 0 ) )
        return -1;

    if( !SetHandleInformation( hStdoutRead, HANDLE_FLAG_INHERIT, 0 ) )
    {
        CloseHandle( hStdoutRead );
        CloseHandle( hStdoutWrite );
        return -1;
    }

    if( !CreatePipe( &hStderrRead, &hStderrWrite, &sa, 0 ) )
    {
        CloseHandle( hStdoutRead );
        CloseHandle( hStdoutWrite );
        return -1;
    }

    if( !SetHandleInformation( hStderrRead, HANDLE_FLAG_INHERIT, 0 ) )
    {
        CloseHandle( hStdoutRead );
        CloseHandle( hStdoutWrite );
        CloseHandle( hStderrRead );
        CloseHandle( hStderrWrite );
        return -1;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory( &pi, sizeof( pi ) );

    std::string cmdLine = "cmd /c \"" + aCommand + "\"";
    std::vector<char> cmdBuf( cmdLine.begin(), cmdLine.end() );
    cmdBuf.push_back( '\0' );

    if( !CreateProcessA( NULL, cmdBuf.data(), NULL, NULL, TRUE,
                         CREATE_NO_WINDOW, NULL, NULL, &si, &pi ) )
    {
        CloseHandle( hStdoutRead );
        CloseHandle( hStdoutWrite );
        CloseHandle( hStderrRead );
        CloseHandle( hStderrWrite );
        return -1;
    }

    CloseHandle( hStdoutWrite );
    CloseHandle( hStderrWrite );

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds( aTimeoutSec );
    char buf[4096];
    DWORD bytesRead;

    bool stdoutDone = false;
    bool stderrDone = false;

    while( !stdoutDone || !stderrDone )
    {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now() );

        if( remaining.count() <= 0 )
        {
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hStdoutRead );
            CloseHandle( hStderrRead );
            wxLogError( "ProcessUtil: Command timed out after %d seconds", aTimeoutSec );
            return -1;
        }

        if( !stdoutDone )
        {
            DWORD avail = 0;
            PeekNamedPipe( hStdoutRead, NULL, 0, NULL, &avail, NULL );

            if( avail > 0 )
            {
                if( ReadFile( hStdoutRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                    && bytesRead > 0 )
                {
                    buf[bytesRead] = '\0';
                    aStdout += buf;
                }
            }
            else
            {
                DWORD exitCode;
                if( GetExitCodeProcess( pi.hProcess, &exitCode )
                    && exitCode != STILL_ACTIVE )
                {
                    while( ReadFile( hStdoutRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                           && bytesRead > 0 )
                    {
                        buf[bytesRead] = '\0';
                        aStdout += buf;
                    }
                    stdoutDone = true;
                }
            }
        }

        if( !stderrDone )
        {
            DWORD avail = 0;
            PeekNamedPipe( hStderrRead, NULL, 0, NULL, &avail, NULL );

            if( avail > 0 )
            {
                if( ReadFile( hStderrRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                    && bytesRead > 0 )
                {
                    buf[bytesRead] = '\0';
                    aStderr += buf;
                }
            }
            else
            {
                DWORD exitCode;
                if( GetExitCodeProcess( pi.hProcess, &exitCode )
                    && exitCode != STILL_ACTIVE )
                {
                    while( ReadFile( hStderrRead, buf, sizeof( buf ) - 1, &bytesRead, NULL )
                           && bytesRead > 0 )
                    {
                        buf[bytesRead] = '\0';
                        aStderr += buf;
                    }
                    stderrDone = true;
                }
            }
        }

        if( !stdoutDone || !stderrDone )
            Sleep( 10 );
    }

    WaitForSingleObject( pi.hProcess, INFINITE );

    DWORD exitCode = 0;
    GetExitCodeProcess( pi.hProcess, &exitCode );

    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    CloseHandle( hStdoutRead );
    CloseHandle( hStderrRead );

    return static_cast<int>( exitCode );

#else
    int stdoutPipe[2];
    int stderrPipe[2];

    if( pipe( stdoutPipe ) != 0 )
        return -1;

    if( pipe( stderrPipe ) != 0 )
    {
        close( stdoutPipe[0] );
        close( stdoutPipe[1] );
        return -1;
    }

    pid_t pid = fork();

    if( pid < 0 )
    {
        close( stdoutPipe[0] );
        close( stdoutPipe[1] );
        close( stderrPipe[0] );
        close( stderrPipe[1] );
        return -1;
    }

    if( pid == 0 )
    {
        close( stdoutPipe[0] );
        close( stderrPipe[0] );
        dup2( stdoutPipe[1], STDOUT_FILENO );
        dup2( stderrPipe[1], STDERR_FILENO );
        close( stdoutPipe[1] );
        close( stderrPipe[1] );

        execl( "/bin/sh", "sh", "-c", aCommand.c_str(), nullptr );
        _exit( 127 );
    }

    // Expose child PID for external cancel support
    if( aChildPid )
        aChildPid->store( pid );

    close( stdoutPipe[1] );
    close( stderrPipe[1] );

    struct pollfd fds[2] = {
        { stdoutPipe[0], POLLIN, 0 },
        { stderrPipe[0], POLLIN, 0 }
    };

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds( aTimeoutSec );
    bool timedOut = false;
    int activeFds = 2;
    char buf[4096];

    while( activeFds > 0 )
    {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now() );

        if( remaining.count() <= 0 )
        {
            timedOut = true;
            break;
        }

        int ret = poll( fds, 2, static_cast<int>( remaining.count() ) );

        if( ret < 0 )
        {
            if( errno == EINTR )
                continue;
            break;
        }

        for( int i = 0; i < 2; ++i )
        {
            if( fds[i].fd < 0 )
                continue;

            if( fds[i].revents & ( POLLIN | POLLHUP ) )
            {
                ssize_t n = read( fds[i].fd, buf, sizeof( buf ) - 1 );

                if( n > 0 )
                {
                    buf[n] = '\0';
                    ( i == 0 ? aStdout : aStderr ) += buf;
                }
                else
                {
                    close( fds[i].fd );
                    fds[i].fd = -1;
                    --activeFds;
                }
            }
            else if( fds[i].revents & ( POLLERR | POLLNVAL ) )
            {
                close( fds[i].fd );
                fds[i].fd = -1;
                --activeFds;
            }
        }
    }

    for( int i = 0; i < 2; ++i )
    {
        if( fds[i].fd >= 0 )
            close( fds[i].fd );
    }

    if( timedOut )
    {
        kill( pid, SIGKILL );
        waitpid( pid, nullptr, 0 );

        if( aChildPid )
            aChildPid->store( 0 );

        wxLogError( "ProcessUtil: Command timed out after %d seconds", aTimeoutSec );
        return -1;
    }

    int status;
    waitpid( pid, &status, 0 );

    if( aChildPid )
        aChildPid->store( 0 );

    int exitCode = -1;

    if( WIFEXITED( status ) )
        exitCode = WEXITSTATUS( status );
    else if( WIFSIGNALED( status ) )
        wxLogError( "ProcessUtil: Process killed by signal %d", WTERMSIG( status ) );

    return exitCode;
#endif
}

} // namespace ProcessUtil

#endif // PROCESS_UTIL_H
