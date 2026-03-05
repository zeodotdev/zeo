#include "pty_handler.h"

#include <wx/log.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __APPLE__
#include <util.h>
#include <mach-o/dyld.h>
#include <libgen.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#else
#include <pty.h>
#endif

wxDEFINE_EVENT( wxEVT_PTY_DATA, wxThreadEvent );
wxDEFINE_EVENT( wxEVT_PTY_EXIT, wxThreadEvent );


PTY_HANDLER::PTY_HANDLER( wxEvtHandler* aEventTarget ) :
        m_eventTarget( aEventTarget ),
        m_masterFd( -1 ),
        m_childPid( -1 ),
        m_running( false ),
        m_readerThread( nullptr )
{
}


PTY_HANDLER::~PTY_HANDLER()
{
    Stop();
}


bool PTY_HANDLER::Start( int aCols, int aRows )
{
    if( m_running.load() )
        return false;

    struct winsize ws;
    memset( &ws, 0, sizeof( ws ) );
    ws.ws_col = aCols;
    ws.ws_row = aRows;

    pid_t pid = forkpty( &m_masterFd, nullptr, nullptr, &ws );

    if( pid < 0 )
    {
        wxLogError( "PTY_HANDLER: forkpty() failed: %s", strerror( errno ) );
        return false;
    }

    if( pid == 0 )
    {
        // Child process - exec the user's shell
        setenv( "TERM", "xterm-256color", 1 );
        setenv( "COLORTERM", "truecolor", 1 );

        // Add the app's SharedSupport/bin to PATH so tools like `zeo` are available
#ifdef __APPLE__
        {
            char     exePath[4096];
            uint32_t size = sizeof( exePath );

            if( _NSGetExecutablePath( exePath, &size ) == 0 )
            {
                char* resolved = realpath( exePath, nullptr );

                if( resolved )
                {
                    // exePath is .app/Contents/MacOS/<binary>
                    // We want .app/Contents/SharedSupport/bin
                    char* macosDir = dirname( resolved );
                    std::string contentsDir = std::string( dirname( macosDir ) );
                    std::string toolsDir = contentsDir + "/SharedSupport/bin";

                    const char* existingPath = getenv( "PATH" );
                    std::string newPath = toolsDir + ":"
                                        + ( existingPath ? existingPath : "/usr/bin:/bin" );
                    setenv( "PATH", newPath.c_str(), 1 );
                    free( resolved );
                }
            }
        }
#endif

        const char* shell = getenv( "SHELL" );

        if( !shell || access( shell, X_OK ) != 0 )
            shell = "/bin/zsh";

        if( access( shell, X_OK ) != 0 )
            shell = "/bin/bash";

        if( access( shell, X_OK ) != 0 )
            shell = "/bin/sh";

        // Exec as login shell (prepend - to argv[0])
        std::string argv0 = std::string( "-" ) + shell;
        execlp( shell, argv0.c_str(), nullptr );

        // If exec fails
        _exit( 127 );
    }

    // Parent process
    m_childPid = pid;
    m_running.store( true );

    // Set master fd to non-blocking
    int flags = fcntl( m_masterFd, F_GETFL, 0 );

    if( flags >= 0 )
        fcntl( m_masterFd, F_SETFL, flags | O_NONBLOCK );

    // Start reader thread
    m_readerThread = new ReaderThread( this );

    if( m_readerThread->Run() != wxTHREAD_NO_ERROR )
    {
        wxLogError( "PTY_HANDLER: Failed to start reader thread" );
        delete m_readerThread;
        m_readerThread = nullptr;
        Stop();
        return false;
    }

    wxLogInfo( "PTY_HANDLER: Started shell PID %d, master fd %d, %dx%d",
               m_childPid, m_masterFd, aCols, aRows );

    return true;
}


void PTY_HANDLER::Stop()
{
    wxLogInfo( "PTY_HANDLER::Stop() called" );

    if( !m_running.load() )
    {
        wxLogInfo( "PTY_HANDLER::Stop() - not running, returning early" );
        return;
    }

    m_running.store( false );
    wxLogInfo( "PTY_HANDLER::Stop() - set m_running to false" );

    // Close master fd FIRST to trigger hangup on the slave side
    // This causes the shell to receive SIGHUP naturally
    if( m_masterFd >= 0 )
    {
        wxLogInfo( "PTY_HANDLER::Stop() - closing master fd %d", m_masterFd );
        close( m_masterFd );
        m_masterFd = -1;
    }

    // Wait for reader thread to finish (it will exit due to closed fd)
    if( m_readerThread )
    {
        wxLogInfo( "PTY_HANDLER::Stop() - waiting for reader thread..." );
        m_readerThread->Wait();
        wxLogInfo( "PTY_HANDLER::Stop() - reader thread finished" );
        delete m_readerThread;
        m_readerThread = nullptr;
    }

    // Clean up child process
    if( m_childPid > 0 )
    {
        // Check if already exited (likely due to PTY hangup)
        int   status;
        pid_t result = waitpid( m_childPid, &status, WNOHANG );
        wxLogInfo( "PTY_HANDLER::Stop() - waitpid WNOHANG returned %d", result );

        if( result == 0 )
        {
            // Still running, send SIGHUP
            wxLogInfo( "PTY_HANDLER::Stop() - sending SIGHUP to PID %d", m_childPid );
            kill( m_childPid, SIGHUP );

            // Brief wait then check again
            usleep( 50000 ); // 50ms
            result = waitpid( m_childPid, &status, WNOHANG );
            wxLogInfo( "PTY_HANDLER::Stop() - waitpid after SIGHUP returned %d", result );

            if( result == 0 )
            {
                // Still running, force kill
                wxLogInfo( "PTY_HANDLER::Stop() - sending SIGKILL to PID %d", m_childPid );
                kill( m_childPid, SIGKILL );

                // Use WNOHANG in a loop with timeout instead of blocking forever
                for( int i = 0; i < 20; i++ )  // 2 second timeout
                {
                    usleep( 100000 ); // 100ms
                    result = waitpid( m_childPid, &status, WNOHANG );
                    wxLogInfo( "PTY_HANDLER::Stop() - waitpid attempt %d returned %d", i, result );

                    if( result != 0 )
                        break;
                }

                if( result == 0 )
                {
                    wxLogWarning( "PTY_HANDLER::Stop() - process %d did not exit after SIGKILL, giving up", m_childPid );
                }
            }
        }

        m_childPid = -1;
    }

    wxLogInfo( "PTY_HANDLER::Stop() - complete" );
}


void PTY_HANDLER::Write( const std::string& aData )
{
    Write( aData.c_str(), aData.size() );
}


void PTY_HANDLER::Write( const char* aData, size_t aLen )
{
    if( m_masterFd < 0 || !m_running.load() )
        return;

    size_t written = 0;

    while( written < aLen )
    {
        ssize_t n = write( m_masterFd, aData + written, aLen - written );

        if( n < 0 )
        {
            if( errno == EAGAIN || errno == EINTR )
                continue;

            wxLogError( "PTY_HANDLER: write failed: %s", strerror( errno ) );
            break;
        }

        written += n;
    }
}


void PTY_HANDLER::Resize( int aCols, int aRows )
{
    if( m_masterFd < 0 )
        return;

    struct winsize ws;
    memset( &ws, 0, sizeof( ws ) );
    ws.ws_col = aCols;
    ws.ws_row = aRows;

    if( ioctl( m_masterFd, TIOCSWINSZ, &ws ) < 0 )
        wxLogError( "PTY_HANDLER: ioctl(TIOCSWINSZ) failed: %s", strerror( errno ) );
}


// Reader thread implementation

PTY_HANDLER::ReaderThread::ReaderThread( PTY_HANDLER* aOwner ) :
        wxThread( wxTHREAD_JOINABLE ),
        m_owner( aOwner )
{
}


void* PTY_HANDLER::ReaderThread::Entry()
{
    char buf[4096];

    while( m_owner->m_running.load() )
    {
        struct pollfd pfd;
        pfd.fd = m_owner->m_masterFd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll( &pfd, 1, 100 ); // 100ms timeout for periodic running check

        if( ret < 0 )
        {
            if( errno == EINTR )
                continue;

            break;
        }

        if( ret == 0 )
            continue; // Timeout, check running flag

        if( pfd.revents & ( POLLERR | POLLHUP | POLLNVAL ) )
        {
            // PTY closed (child exited)
            break;
        }

        if( pfd.revents & POLLIN )
        {
            ssize_t n = read( m_owner->m_masterFd, buf, sizeof( buf ) );

            if( n > 0 )
            {
                wxThreadEvent* event = new wxThreadEvent( wxEVT_PTY_DATA );
                event->SetPayload( std::string( buf, n ) );
                wxQueueEvent( m_owner->m_eventTarget, event );
            }
            else if( n == 0 )
            {
                // EOF
                break;
            }
            else if( errno != EAGAIN && errno != EINTR )
            {
                break;
            }
        }
    }

    // Notify that PTY has exited
    m_owner->m_running.store( false );
    wxThreadEvent* exitEvent = new wxThreadEvent( wxEVT_PTY_EXIT );
    wxQueueEvent( m_owner->m_eventTarget, exitEvent );

    return nullptr;
}


pid_t PTY_HANDLER::GetForegroundPid() const
{
    if( m_masterFd < 0 || !m_running.load() )
        return -1;

    // tcgetpgrp returns the foreground process group ID of the terminal
    pid_t fgPid = tcgetpgrp( m_masterFd );
    return fgPid;
}


std::string PTY_HANDLER::GetForegroundProcessName() const
{
    pid_t fgPid = GetForegroundPid();

    if( fgPid <= 0 )
        return "";

#ifdef __APPLE__
    // Get process name using proc_name
    char name[PROC_PIDPATHINFO_MAXSIZE];

    if( proc_name( fgPid, name, sizeof( name ) ) > 0 )
        return std::string( name );

#else
    // Linux: read /proc/<pid>/comm
    char path[64];
    snprintf( path, sizeof( path ), "/proc/%d/comm", fgPid );

    FILE* f = fopen( path, "r" );

    if( f )
    {
        char name[256];

        if( fgets( name, sizeof( name ), f ) )
        {
            // Remove trailing newline
            size_t len = strlen( name );

            if( len > 0 && name[len - 1] == '\n' )
                name[len - 1] = '\0';

            fclose( f );
            return std::string( name );
        }

        fclose( f );
    }
#endif

    return "";
}


std::string PTY_HANDLER::GetForegroundCwd() const
{
    pid_t fgPid = GetForegroundPid();

    if( fgPid <= 0 )
        return "";

#ifdef __APPLE__
    struct proc_vnodepathinfo vnodeInfo;
    int sz = proc_pidinfo( fgPid, PROC_PIDVNODEPATHINFO, 0, &vnodeInfo, sizeof( vnodeInfo ) );

    if( sz > 0 )
        return std::string( vnodeInfo.pvi_cdir.vip_path );

#else
    // Linux: readlink /proc/<pid>/cwd
    char path[64];
    snprintf( path, sizeof( path ), "/proc/%d/cwd", fgPid );

    char cwd[PATH_MAX];
    ssize_t len = readlink( path, cwd, sizeof( cwd ) - 1 );

    if( len > 0 )
    {
        cwd[len] = '\0';
        return std::string( cwd );
    }
#endif

    return "";
}


// Names to ignore when extracting script display name
static bool IsIgnoredName( const std::string& aName )
{
    if( aName.empty() )
        return true;

    // Lowercase for comparison
    std::string lower = aName;

    for( char& c : lower )
    {
        if( c >= 'A' && c <= 'Z' )
            c = c - 'A' + 'a';
    }

    // Ignore temp directories, common system paths, and generic names
    if( lower == "tmp" || lower == "temp" || lower == "var" || lower == "private" ||
        lower == "folders" || lower == "t" || lower == "bin" || lower == "usr" ||
        lower == "local" || lower == "lib" || lower == "python" || lower == "python3" ||
        lower == "site-packages" || lower == "scripts" || lower == "contents" ||
        lower == "macos" || lower == "resources" || lower == "sharedsupport" ||
        lower == "frameworks" || lower == "applications" || lower == "opt" ||
        lower == "homebrew" || lower == "cellar" )
    {
        return true;
    }

    // Ignore names that look like temp files (start with tmp/pip/wheel or contain random chars)
    if( lower.substr( 0, 3 ) == "tmp" || lower.substr( 0, 3 ) == "pip" ||
        lower.substr( 0, 5 ) == "wheel" )
    {
        return true;
    }

    // Ignore if it looks like a hash or random string (mostly hex chars)
    int hexCount = 0;

    for( char c : lower )
    {
        if( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) )
            hexCount++;
    }

    if( lower.length() > 8 && hexCount > (int) lower.length() * 2 / 3 )
        return true;

    return false;
}


// Check if a name is a known Zeo command
static bool IsKnownCommand( const std::string& aName )
{
    std::string lower = aName;

    for( char& c : lower )
    {
        if( c >= 'A' && c <= 'Z' )
            c = c - 'A' + 'a';
    }

    return lower == "zeo" || lower == "shell" || lower == "monitor" || lower == "tool";
}


// Helper to extract a display name from a script path or module name
static std::string ExtractScriptDisplayName( const std::string& aArg )
{
    if( aArg.empty() )
        return "";

    std::string name = aArg;

    // Extract filename from path
    size_t lastSlash = name.rfind( '/' );

    if( lastSlash != std::string::npos )
        name = name.substr( lastSlash + 1 );

    // Remove .py extension if present
    if( name.size() > 3 && name.substr( name.size() - 3 ) == ".py" )
        name = name.substr( 0, name.size() - 3 );

    // For module names like "zeo.cli", take the first component
    // But only if it looks like a module (contains dots but not at start)
    if( !name.empty() && name[0] != '.' )
    {
        size_t dot = name.find( '.' );

        if( dot != std::string::npos && dot > 0 )
            name = name.substr( 0, dot );
    }

    // Check if this is a name we should ignore (unless it's a known command)
    if( !IsKnownCommand( name ) && IsIgnoredName( name ) )
        return "";

    // Capitalize first letter for nicer display (e.g., "zeo" -> "Zeo")
    if( !name.empty() && name[0] >= 'a' && name[0] <= 'z' )
        name[0] = name[0] - 'a' + 'A';

    return name;
}


std::string PTY_HANDLER::GetForegroundDisplayName() const
{
    std::string procName = GetForegroundProcessName();

    if( procName.empty() )
        return "";

    // Check if this is a Python process - if so, try to get a better name from args
    bool isPython = ( procName.find( "python" ) != std::string::npos ||
                      procName.find( "Python" ) != std::string::npos );

    if( !isPython )
        return procName;

    // For Python processes, try to extract the script name from command line
    pid_t fgPid = GetForegroundPid();

    if( fgPid <= 0 )
        return procName;

#ifdef __APPLE__
    // Use sysctl to get the command line arguments
    int    mib[3] = { CTL_KERN, KERN_PROCARGS2, fgPid };
    size_t size = 0;

    // First call to get size
    if( sysctl( mib, 3, nullptr, &size, nullptr, 0 ) < 0 )
        return procName;

    // Allocate buffer
    std::vector<char> buffer( size );

    if( sysctl( mib, 3, buffer.data(), &size, nullptr, 0 ) < 0 )
        return procName;

    // KERN_PROCARGS2 format: argc (int) + executable path + nulls + argv[0] + null + argv[1] + ...
    if( size < sizeof( int ) )
        return procName;

    // Skip argc
    char* ptr = buffer.data() + sizeof( int );
    char* end = buffer.data() + size;

    // Skip the executable path (e.g., /usr/bin/python3)
    while( ptr < end && *ptr != '\0' )
        ptr++;

    // Skip null bytes between executable and argv[0]
    while( ptr < end && *ptr == '\0' )
        ptr++;

    if( ptr >= end )
        return procName;

    // Collect all arguments into a vector for easier processing
    std::vector<std::string> args;
    bool afterDashM = false;

    while( ptr < end )
    {
        if( *ptr != '\0' )
        {
            std::string arg( ptr );
            args.push_back( arg );

            // Skip to end of this argument
            while( ptr < end && *ptr != '\0' )
                ptr++;
        }

        // Skip null separators
        while( ptr < end && *ptr == '\0' )
            ptr++;
    }

    // Now scan arguments looking for useful script/command names
    std::string scriptName;
    std::string subCommand;

    for( size_t i = 0; i < args.size(); i++ )
    {
        const std::string& arg = args[i];

        // Skip flags
        if( !arg.empty() && arg[0] == '-' )
        {
            // Remember if we saw -m (next arg is module name)
            if( arg == "-m" && i + 1 < args.size() )
            {
                std::string name = ExtractScriptDisplayName( args[i + 1] );

                if( !name.empty() )
                {
                    scriptName = name;

                    // Look for subcommand after module name
                    if( i + 2 < args.size() && !args[i + 2].empty() && args[i + 2][0] != '-' )
                    {
                        subCommand = args[i + 2];

                        // Capitalize subcommand
                        if( !subCommand.empty() && subCommand[0] >= 'a' && subCommand[0] <= 'z' )
                            subCommand[0] = subCommand[0] - 'a' + 'A';
                    }

                    break;
                }
            }

            continue;
        }

        // Try to extract a useful name from this argument
        std::string name = ExtractScriptDisplayName( arg );

        if( !name.empty() )
        {
            scriptName = name;

            // Look for subcommand (next non-flag argument)
            if( i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-' )
            {
                // Check if next arg looks like a subcommand (short, no path chars)
                const std::string& nextArg = args[i + 1];

                if( nextArg.find( '/' ) == std::string::npos &&
                    nextArg.find( '.' ) == std::string::npos &&
                    nextArg.length() < 20 )
                {
                    subCommand = nextArg;

                    // Capitalize subcommand
                    if( !subCommand.empty() && subCommand[0] >= 'a' && subCommand[0] <= 'z' )
                        subCommand[0] = subCommand[0] - 'a' + 'A';
                }
            }

            break;
        }
    }

    // Build final display name
    if( !scriptName.empty() )
    {
        if( !subCommand.empty() )
            return scriptName + " " + subCommand;

        return scriptName;
    }

    return procName;

#else
    // Linux: read /proc/<pid>/cmdline
    char pathBuf[64];
    snprintf( pathBuf, sizeof( pathBuf ), "/proc/%d/cmdline", fgPid );

    FILE* f = fopen( pathBuf, "r" );

    if( !f )
        return procName;

    char cmdline[4096];
    size_t len = fread( cmdline, 1, sizeof( cmdline ) - 1, f );
    fclose( f );

    if( len == 0 )
        return procName;

    cmdline[len] = '\0';

    // Collect all arguments
    std::vector<std::string> args;
    char* ptr = cmdline;
    char* end = cmdline + len;

    while( ptr < end )
    {
        if( *ptr != '\0' )
        {
            std::string arg( ptr );
            args.push_back( arg );

            while( ptr < end && *ptr != '\0' )
                ptr++;
        }

        ptr++;
    }

    // Scan arguments looking for useful script/command names
    std::string scriptName;
    std::string subCommand;

    for( size_t i = 0; i < args.size(); i++ )
    {
        const std::string& arg = args[i];

        if( !arg.empty() && arg[0] == '-' )
        {
            if( arg == "-m" && i + 1 < args.size() )
            {
                std::string name = ExtractScriptDisplayName( args[i + 1] );

                if( !name.empty() )
                {
                    scriptName = name;

                    if( i + 2 < args.size() && !args[i + 2].empty() && args[i + 2][0] != '-' )
                    {
                        subCommand = args[i + 2];

                        if( !subCommand.empty() && subCommand[0] >= 'a' && subCommand[0] <= 'z' )
                            subCommand[0] = subCommand[0] - 'a' + 'A';
                    }

                    break;
                }
            }

            continue;
        }

        std::string name = ExtractScriptDisplayName( arg );

        if( !name.empty() )
        {
            scriptName = name;

            if( i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-' )
            {
                const std::string& nextArg = args[i + 1];

                if( nextArg.find( '/' ) == std::string::npos &&
                    nextArg.find( '.' ) == std::string::npos &&
                    nextArg.length() < 20 )
                {
                    subCommand = nextArg;

                    if( !subCommand.empty() && subCommand[0] >= 'a' && subCommand[0] <= 'z' )
                        subCommand[0] = subCommand[0] - 'a' + 'A';
                }
            }

            break;
        }
    }

    if( !scriptName.empty() )
    {
        if( !subCommand.empty() )
            return scriptName + " " + subCommand;

        return scriptName;
    }

    return procName;
#endif
}
