#include "pcb_autoroute_handler.h"
#include "../tool_registry.h"
#include "../util/kicad_cli_util.h"
#include <frame_type.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <wx/app.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <thread>
#include "../../agent_events.h"


/**
 * Simple scope guard that runs a cleanup function on destruction.
 */
class AutorouteScopeGuard
{
public:
    explicit AutorouteScopeGuard( std::function<void()> aCleanup ) :
        m_cleanup( std::move( aCleanup ) ) {}
    ~AutorouteScopeGuard() { if( m_cleanup ) m_cleanup(); }

    AutorouteScopeGuard( const AutorouteScopeGuard& ) = delete;
    AutorouteScopeGuard& operator=( const AutorouteScopeGuard& ) = delete;

private:
    std::function<void()> m_cleanup;
};


std::string PCB_AUTOROUTE_HANDLER::Execute( const std::string& aToolName,
                                             const nlohmann::json& aInput )
{
    if( aToolName == "pcb_autoroute" )
        return ExecuteAutoroute( aInput );

    return "Error: Unknown tool: " + aToolName;
}


std::string PCB_AUTOROUTE_HANDLER::GetDescription( const std::string& aToolName,
                                                    const nlohmann::json& aInput ) const
{
    int maxPasses = aInput.value( "max_passes", 50 );
    return "Running Freerouting autorouter (max " + std::to_string( maxPasses ) + " passes)";
}


std::string PCB_AUTOROUTE_HANDLER::ExecuteAutoroute( const nlohmann::json& aInput )
{
    wxLogInfo( "AUTOROUTE: Starting autoroute workflow" );

    // Check that we have the send function for IPC
    if( !TOOL_REGISTRY::Instance().GetSendRequestFn() )
    {
        return "Error: IPC send function not available. Cannot communicate with PCB editor.";
    }

    // Check that PCB editor is open
    if( !TOOL_REGISTRY::Instance().IsPcbEditorOpen() )
    {
        return "Error: PCB editor must be open to run autorouter.";
    }

    // Get parameters
    int maxPasses = aInput.value( "max_passes", 50 );
    int timeout = aInput.value( "timeout", 300 );

    // Validate parameters
    if( maxPasses < 1 || maxPasses > 999 )
        maxPasses = 50;
    if( timeout < 30 || timeout > 1800 )
        timeout = 300;

    // Check that Freerouting JAR exists
    std::string jarPath = KiCadCliUtil::GetFreeroutingJarPath();
    if( jarPath.empty() )
    {
        return "Error: Freerouting JAR not found in application bundle. "
               "Please ensure freerouting.jar is installed in SharedSupport/freerouting/";
    }

    // Create temp directory for DSN/SES files
    std::string tempTemplate = ( wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
                                 + "zeo_autoroute_XXXXXX" ).ToStdString();
    std::vector<char> tempBuf( tempTemplate.begin(), tempTemplate.end() );
    tempBuf.push_back( '\0' );

    if( !mkdtemp( tempBuf.data() ) )
        return "Error: Failed to create temporary directory";

    std::string tempDir = tempBuf.data();

    // Scope guard ensures temp directory is cleaned up on any exit path
    AutorouteScopeGuard cleanupGuard( [tempDir]()
    {
        wxDir dir( wxString::FromUTF8( tempDir ) );
        if( dir.IsOpened() )
        {
            wxString entry;
            bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
            while( cont )
            {
                wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                cont = dir.GetNext( &entry );
            }
        }
        wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );
    } );

    std::string dsnPath = tempDir + "/board.dsn";
    std::string sesPath = tempDir + "/board.ses";

    wxLogInfo( "AUTOROUTE: Temp dir: %s", tempDir.c_str() );
    wxLogInfo( "AUTOROUTE: DSN path: %s", dsnPath.c_str() );
    wxLogInfo( "AUTOROUTE: SES path: %s", sesPath.c_str() );

    // Step 0: Take snapshot before any changes (for diff view)
    wxLogInfo( "AUTOROUTE: Taking snapshot for diff view" );

    nlohmann::json snapshotCmd;
    snapshotCmd["type"] = "take_snapshot";
    TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, snapshotCmd.dump() );

    // Step 1: Export DSN via IPC
    wxLogInfo( "AUTOROUTE: Requesting DSN export via IPC" );

    nlohmann::json exportCmd;
    exportCmd["type"] = "export_specctra_dsn";
    exportCmd["output_path"] = dsnPath;

    std::string exportResponse = TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, exportCmd.dump() );

    if( exportResponse.empty() )
    {
        return "Error: DSN export failed - empty response from PCB editor";
    }

    nlohmann::json exportResult = nlohmann::json::parse( exportResponse, nullptr, false );
    if( exportResult.is_discarded() || !exportResult.value( "success", false ) )
    {
        std::string errMsg = exportResult.is_discarded() ? exportResponse
                             : exportResult.value( "error", "Unknown error" );
        return "Error: DSN export failed: " + errMsg;
    }

    // Verify DSN file was created
    if( !wxFileName::FileExists( wxString::FromUTF8( dsnPath ) ) )
    {
        return "Error: DSN file was not created at " + dsnPath;
    }

    wxLogInfo( "AUTOROUTE: DSN export successful" );

    // Step 2: Run Freerouting
    wxLogInfo( "AUTOROUTE: Running Freerouting (max_passes=%d, timeout=%ds)", maxPasses, timeout );

    std::string routingResult = RunFreerouting( dsnPath, sesPath, maxPasses, timeout );

    nlohmann::json routingJson = nlohmann::json::parse( routingResult, nullptr, false );
    if( routingJson.is_discarded() || !routingJson.value( "success", false ) )
    {
        return routingResult;  // Return the error message
    }

    // Verify SES file was created
    if( !wxFileName::FileExists( wxString::FromUTF8( sesPath ) ) )
    {
        return "Error: Freerouting did not produce output SES file";
    }

    wxLogInfo( "AUTOROUTE: Freerouting completed successfully" );

    // Step 3: Import SES via IPC
    wxLogInfo( "AUTOROUTE: Requesting SES import via IPC" );

    nlohmann::json importCmd;
    importCmd["type"] = "import_specctra_session";
    importCmd["input_path"] = sesPath;

    std::string importResponse = TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, importCmd.dump() );

    if( importResponse.empty() )
    {
        return "Error: SES import failed - empty response from PCB editor";
    }

    nlohmann::json importResult = nlohmann::json::parse( importResponse, nullptr, false );
    if( importResult.is_discarded() || !importResult.value( "success", false ) )
    {
        std::string errMsg = importResult.is_discarded() ? importResponse
                             : importResult.value( "error", "Unknown error" );
        return "Error: SES import failed: " + errMsg;
    }

    wxLogInfo( "AUTOROUTE: SES import successful" );

    // Trigger diff view to show the changes
    wxLogInfo( "AUTOROUTE: Triggering diff view" );

    nlohmann::json detectCmd;
    detectCmd["type"] = "detect_changes";
    TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, detectCmd.dump() );

    // Build final result
    nlohmann::json result;
    result["success"] = true;
    result["message"] = "Autorouting completed successfully";

    // Include statistics from Freerouting if available
    if( routingJson.contains( "routed" ) )
        result["routed"] = routingJson["routed"];
    if( routingJson.contains( "total" ) )
        result["total"] = routingJson["total"];
    if( routingJson.contains( "failed" ) )
        result["failed"] = routingJson["failed"];

    // Include import statistics if available
    if( importResult.contains( "tracks_added" ) )
        result["tracks_added"] = importResult["tracks_added"];
    if( importResult.contains( "vias_added" ) )
        result["vias_added"] = importResult["vias_added"];

    return result.dump();
}


std::string PCB_AUTOROUTE_HANDLER::RunFreerouting( const std::string& aDsnPath,
                                                    const std::string& aSesPath,
                                                    int aMaxPasses, int aTimeoutSec )
{
    std::string jarPath = KiCadCliUtil::GetFreeroutingJarPath();
    std::string javaPath = KiCadCliUtil::GetJavaPath();

    // Build the Freerouting command
    // -de: design input file (DSN)
    // -do: design output file (SES)
    // -mp: max passes
    // --gui.enabled=false: headless mode
    std::string cmd = "\"" + javaPath + "\" -jar \"" + jarPath + "\""
                      + " --gui.enabled=false"
                      + " -de \"" + aDsnPath + "\""
                      + " -do \"" + aSesPath + "\""
                      + " -mp " + std::to_string( aMaxPasses );

    wxLogInfo( "AUTOROUTE: Executing: %s", cmd.c_str() );

    std::string stdoutStr, stderrStr;
    int exitCode = RunCommand( cmd, stdoutStr, stderrStr, aTimeoutSec );

    if( !stderrStr.empty() )
        wxLogWarning( "AUTOROUTE: Freerouting stderr: %s", stderrStr.c_str() );

    if( !stdoutStr.empty() )
        wxLogInfo( "AUTOROUTE: Freerouting stdout: %s", stdoutStr.c_str() );

    nlohmann::json result;

    if( exitCode == -1 )
    {
        result["success"] = false;
        result["error"] = "Freerouting timed out after " + std::to_string( aTimeoutSec ) + " seconds";
        return result.dump();
    }

    if( exitCode != 0 )
    {
        result["success"] = false;
        result["error"] = "Freerouting exited with code " + std::to_string( exitCode );
        if( !stderrStr.empty() )
            result["stderr"] = stderrStr;
        return result.dump();
    }

    // Parse Freerouting output for statistics
    // Example output: "Autorouter: X of Y connections routed"
    result["success"] = true;

    // Try to extract routing statistics from stdout
    // Freerouting outputs progress like "Routing pass X, Y connections remaining"
    // and completion like "All connections routed" or "X unrouted connections remaining"
    size_t pos = stdoutStr.find( "connections routed" );
    if( pos != std::string::npos )
    {
        // Try to extract numbers
        // Look backwards for the number
        size_t numEnd = stdoutStr.rfind( " of ", pos );
        if( numEnd != std::string::npos )
        {
            size_t numStart = stdoutStr.rfind( ' ', numEnd - 1 );
            if( numStart != std::string::npos )
            {
                try
                {
                    int routed = std::stoi( stdoutStr.substr( numStart + 1, numEnd - numStart - 1 ) );
                    size_t totalEnd = stdoutStr.find( ' ', numEnd + 4 );
                    int total = std::stoi( stdoutStr.substr( numEnd + 4, totalEnd - numEnd - 4 ) );
                    result["routed"] = routed;
                    result["total"] = total;
                    result["failed"] = total - routed;
                }
                catch( ... )
                {
                    // Parsing failed, that's ok - we'll just not have statistics
                }
            }
        }
    }

    return result.dump();
}


int PCB_AUTOROUTE_HANDLER::RunCommand( const std::string& aCommand, std::string& aStdout,
                                        std::string& aStderr, int aTimeoutSec )
{
    aStdout.clear();
    aStderr.clear();

    // Create pipes for capturing stdout and stderr
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
        // Child process: redirect stdout/stderr to pipes and exec command
        close( stdoutPipe[0] );
        close( stderrPipe[0] );
        dup2( stdoutPipe[1], STDOUT_FILENO );
        dup2( stderrPipe[1], STDERR_FILENO );
        close( stdoutPipe[1] );
        close( stderrPipe[1] );

        execl( "/bin/sh", "sh", "-c", aCommand.c_str(), nullptr );
        _exit( 127 );
    }

    // Parent process: read from pipes with timeout via poll()
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

    // Clean up any still-open fds
    for( int i = 0; i < 2; ++i )
    {
        if( fds[i].fd >= 0 )
            close( fds[i].fd );
    }

    if( timedOut )
    {
        kill( pid, SIGKILL );
        waitpid( pid, nullptr, 0 );
        wxLogError( "AUTOROUTE: Command timed out after %d seconds", aTimeoutSec );
        return -1;
    }

    int status;
    waitpid( pid, &status, 0 );
    int exitCode = -1;

    if( WIFEXITED( status ) )
        exitCode = WEXITSTATUS( status );
    else if( WIFSIGNALED( status ) )
        wxLogError( "AUTOROUTE: Process killed by signal %d", WTERMSIG( status ) );

    return exitCode;
}


void PCB_AUTOROUTE_HANDLER::ExecuteAsync( const std::string& aToolName,
                                           const nlohmann::json& aInput,
                                           const std::string& aToolUseId,
                                           wxEvtHandler* aEventHandler )
{
    wxLogInfo( "AUTOROUTE ASYNC: Starting async autoroute workflow" );

    // Validate preconditions on the main thread
    if( !TOOL_REGISTRY::Instance().GetSendRequestFn() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: IPC send function not available. Cannot communicate with PCB editor.";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    if( !TOOL_REGISTRY::Instance().IsPcbEditorOpen() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: PCB editor must be open to run autorouter.";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    // Get parameters
    int maxPasses = aInput.value( "max_passes", 50 );
    int timeout = aInput.value( "timeout", 300 );

    if( maxPasses < 1 || maxPasses > 999 )
        maxPasses = 50;
    if( timeout < 30 || timeout > 1800 )
        timeout = 300;

    // Check that Freerouting JAR exists
    std::string jarPath = KiCadCliUtil::GetFreeroutingJarPath();
    if( jarPath.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: Freerouting JAR not found in application bundle.";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    // Create temp directory for DSN/SES files (on main thread)
    std::string tempTemplate = ( wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
                                 + "zeo_autoroute_XXXXXX" ).ToStdString();
    std::vector<char> tempBuf( tempTemplate.begin(), tempTemplate.end() );
    tempBuf.push_back( '\0' );

    if( !mkdtemp( tempBuf.data() ) )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: Failed to create temporary directory";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    std::string tempDir = tempBuf.data();
    std::string dsnPath = tempDir + "/board.dsn";
    std::string sesPath = tempDir + "/board.ses";

    wxLogInfo( "AUTOROUTE ASYNC: Temp dir: %s", tempDir.c_str() );

    // Step 0: Take snapshot before any changes (for diff view)
    wxLogInfo( "AUTOROUTE ASYNC: Taking snapshot for diff view" );

    nlohmann::json snapshotCmd;
    snapshotCmd["type"] = "take_snapshot";
    TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, snapshotCmd.dump() );

    // Step 1: Export DSN via IPC (on main thread - fast operation)
    wxLogInfo( "AUTOROUTE ASYNC: Requesting DSN export via IPC" );

    nlohmann::json exportCmd;
    exportCmd["type"] = "export_specctra_dsn";
    exportCmd["output_path"] = dsnPath;

    std::string exportResponse = TOOL_REGISTRY::Instance().GetSendRequestFn()( FRAME_PCB_EDITOR, exportCmd.dump() );

    if( exportResponse.empty() )
    {
        // Cleanup temp dir
        wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: DSN export failed - empty response from PCB editor";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    nlohmann::json exportResult = nlohmann::json::parse( exportResponse, nullptr, false );
    if( exportResult.is_discarded() || !exportResult.value( "success", false ) )
    {
        // Cleanup temp dir
        wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

        std::string errMsg = exportResult.is_discarded() ? exportResponse
                             : exportResult.value( "error", "Unknown error" );
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = "Error: DSN export failed: " + errMsg;
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "AUTOROUTE ASYNC: DSN export successful, spawning background thread" );

    // Capture what we need for the background thread
    // NOTE: sendRequestFn captured by value won't work from another thread (KIWAY is main-thread only)
    // So we capture it and use CallAfter to execute on main thread
    auto sendRequestFn = TOOL_REGISTRY::Instance().GetSendRequestFn();

    // Step 2: Spawn background thread for Freerouting
    std::thread( [=]()
    {
        wxLogInfo( "AUTOROUTE ASYNC: Background thread started" );

        // Run Freerouting (the slow part)
        std::string javaPath = KiCadCliUtil::GetJavaPath();
        std::string cmd = "\"" + javaPath + "\" -jar \"" + jarPath + "\""
                          + " --gui.enabled=false"
                          + " -de \"" + dsnPath + "\""
                          + " -do \"" + sesPath + "\""
                          + " -mp " + std::to_string( maxPasses );

        wxLogInfo( "AUTOROUTE ASYNC: Executing: %s", cmd.c_str() );

        std::string stdoutStr, stderrStr;

        // Create pipes for capturing stdout and stderr
        int stdoutPipe[2];
        int stderrPipe[2];

        if( pipe( stdoutPipe ) != 0 || pipe( stderrPipe ) != 0 )
        {
            wxTheApp->CallAfter( [=]()
            {
                // Cleanup temp dir
                wxDir dir( wxString::FromUTF8( tempDir ) );
                if( dir.IsOpened() )
                {
                    wxString entry;
                    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                    while( cont )
                    {
                        wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                        cont = dir.GetNext( &entry );
                    }
                }
                wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = "Error: Failed to create pipes for Freerouting";
                result.success = false;
                PostToolResult( aEventHandler, result );
            } );
            return;
        }

        pid_t pid = fork();

        if( pid < 0 )
        {
            close( stdoutPipe[0] );
            close( stdoutPipe[1] );
            close( stderrPipe[0] );
            close( stderrPipe[1] );

            wxTheApp->CallAfter( [=]()
            {
                // Cleanup
                wxDir dir( wxString::FromUTF8( tempDir ) );
                if( dir.IsOpened() )
                {
                    wxString entry;
                    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                    while( cont )
                    {
                        wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                        cont = dir.GetNext( &entry );
                    }
                }
                wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = "Error: Failed to fork Freerouting process";
                result.success = false;
                PostToolResult( aEventHandler, result );
            } );
            return;
        }

        if( pid == 0 )
        {
            // Child process
            close( stdoutPipe[0] );
            close( stderrPipe[0] );
            dup2( stdoutPipe[1], STDOUT_FILENO );
            dup2( stderrPipe[1], STDERR_FILENO );
            close( stdoutPipe[1] );
            close( stderrPipe[1] );

            execl( "/bin/sh", "sh", "-c", cmd.c_str(), nullptr );
            _exit( 127 );
        }

        // Parent process: read from pipes with timeout
        close( stdoutPipe[1] );
        close( stderrPipe[1] );

        struct pollfd fds[2] = {
            { stdoutPipe[0], POLLIN, 0 },
            { stderrPipe[0], POLLIN, 0 }
        };

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( timeout );
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
                        ( i == 0 ? stdoutStr : stderrStr ) += buf;
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

        // Clean up any still-open fds
        for( int i = 0; i < 2; ++i )
        {
            if( fds[i].fd >= 0 )
                close( fds[i].fd );
        }

        int exitCode = -1;

        if( timedOut )
        {
            kill( pid, SIGKILL );
            waitpid( pid, nullptr, 0 );

            wxTheApp->CallAfter( [=]()
            {
                // Cleanup
                wxDir dir( wxString::FromUTF8( tempDir ) );
                if( dir.IsOpened() )
                {
                    wxString entry;
                    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                    while( cont )
                    {
                        wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                        cont = dir.GetNext( &entry );
                    }
                }
                wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

                nlohmann::json resultJson;
                resultJson["success"] = false;
                resultJson["error"] = "Freerouting timed out after " + std::to_string( timeout ) + " seconds";

                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = resultJson.dump();
                result.success = false;
                PostToolResult( aEventHandler, result );
            } );
            return;
        }

        int status;
        waitpid( pid, &status, 0 );

        if( WIFEXITED( status ) )
            exitCode = WEXITSTATUS( status );

        wxLogInfo( "AUTOROUTE ASYNC: Freerouting finished with exit code %d", exitCode );

        if( exitCode != 0 )
        {
            wxTheApp->CallAfter( [=]()
            {
                // Cleanup
                wxDir dir( wxString::FromUTF8( tempDir ) );
                if( dir.IsOpened() )
                {
                    wxString entry;
                    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                    while( cont )
                    {
                        wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                        cont = dir.GetNext( &entry );
                    }
                }
                wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

                nlohmann::json resultJson;
                resultJson["success"] = false;
                resultJson["error"] = "Freerouting exited with code " + std::to_string( exitCode );
                if( !stderrStr.empty() )
                    resultJson["stderr"] = stderrStr;

                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = resultJson.dump();
                result.success = false;
                PostToolResult( aEventHandler, result );
            } );
            return;
        }

        // Check if SES file was created
        bool sesExists = false;
        {
            std::ifstream sesFile( sesPath );
            sesExists = sesFile.good();
        }

        if( !sesExists )
        {
            wxTheApp->CallAfter( [=]()
            {
                // Cleanup
                wxDir dir( wxString::FromUTF8( tempDir ) );
                if( dir.IsOpened() )
                {
                    wxString entry;
                    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                    while( cont )
                    {
                        wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                        cont = dir.GetNext( &entry );
                    }
                }
                wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = "Error: Freerouting did not produce output SES file";
                result.success = false;
                PostToolResult( aEventHandler, result );
            } );
            return;
        }

        wxLogInfo( "AUTOROUTE ASYNC: Freerouting completed successfully, importing SES" );

        // Step 3: Import SES via IPC (must be done on main thread)
        wxTheApp->CallAfter( [=]()
        {
            wxLogInfo( "AUTOROUTE ASYNC: Importing SES on main thread" );

            nlohmann::json importCmd;
            importCmd["type"] = "import_specctra_session";
            importCmd["input_path"] = sesPath;

            std::string importResponse = sendRequestFn( FRAME_PCB_EDITOR, importCmd.dump() );

            // Cleanup temp directory
            wxDir dir( wxString::FromUTF8( tempDir ) );
            if( dir.IsOpened() )
            {
                wxString entry;
                bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
                while( cont )
                {
                    wxRemoveFile( wxString::FromUTF8( tempDir ) + wxFileName::GetPathSeparator() + entry );
                    cont = dir.GetNext( &entry );
                }
            }
            wxFileName::Rmdir( wxString::FromUTF8( tempDir ) );

            if( importResponse.empty() )
            {
                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = "Error: SES import failed - empty response from PCB editor";
                result.success = false;
                PostToolResult( aEventHandler, result );
                return;
            }

            nlohmann::json importResult = nlohmann::json::parse( importResponse, nullptr, false );
            if( importResult.is_discarded() || !importResult.value( "success", false ) )
            {
                std::string errMsg = importResult.is_discarded() ? importResponse
                                     : importResult.value( "error", "Unknown error" );
                ToolExecutionResult result;
                result.tool_use_id = aToolUseId;
                result.tool_name = aToolName;
                result.result = "Error: SES import failed: " + errMsg;
                result.success = false;
                PostToolResult( aEventHandler, result );
                return;
            }

            wxLogInfo( "AUTOROUTE ASYNC: SES import successful" );

            // Trigger diff view to show the changes
            wxLogInfo( "AUTOROUTE ASYNC: Triggering diff view" );

            nlohmann::json detectCmd;
            detectCmd["type"] = "detect_changes";
            sendRequestFn( FRAME_PCB_EDITOR, detectCmd.dump() );

            // Build final success result
            nlohmann::json finalResult;
            finalResult["success"] = true;
            finalResult["message"] = "Autorouting completed successfully";

            if( importResult.contains( "tracks_added" ) )
                finalResult["tracks_added"] = importResult["tracks_added"];
            if( importResult.contains( "vias_added" ) )
                finalResult["vias_added"] = importResult["vias_added"];

            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name = aToolName;
            result.result = finalResult.dump();
            result.success = true;
            PostToolResult( aEventHandler, result );
        } );

    } ).detach();  // Detach thread - it manages its own lifetime via CallAfter
}
