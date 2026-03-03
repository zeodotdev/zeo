#include "screenshot_handler.h"
#include "../tool_registry.h"
#include "../util/kicad_cli_util.h"
#include <frame_type.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <wx/base64.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/utils.h>


// Max image dimension in pixels per Claude API limits
static const int MAX_IMAGE_DIMENSION = 1568;

// Shell metacharacters that are unsafe in quoted command arguments
static const char* UNSAFE_PATH_CHARS = "`$\\\"!#&|;(){}[]<>?*~\n\r";

// Background colors from _builtin_default theme (builtin_color_themes.h).
// Used for compositing since SVG export doesn't include the canvas background.
static const unsigned char SCH_BG_R = 245, SCH_BG_G = 244, SCH_BG_B = 239; // LAYER_SCHEMATIC_BACKGROUND
static const unsigned char PCB_BG_R = 0,   PCB_BG_G = 16,  PCB_BG_B = 35;  // LAYER_PCB_BACKGROUND


/**
 * Simple scope guard that runs a cleanup function on destruction.
 * Ensures temp files are always cleaned up, even on early returns.
 */
class ScopeGuard
{
public:
    explicit ScopeGuard( std::function<void()> aCleanup ) : m_cleanup( std::move( aCleanup ) ) {}
    ~ScopeGuard() { if( m_cleanup ) m_cleanup(); }

    ScopeGuard( const ScopeGuard& ) = delete;
    ScopeGuard& operator=( const ScopeGuard& ) = delete;

private:
    std::function<void()> m_cleanup;
};


bool SCREENSHOT_HANDLER::IsPathSafeForShell( const std::string& aPath )
{
    return aPath.find_first_of( UNSAFE_PATH_CHARS ) == std::string::npos;
}



std::string SCREENSHOT_HANDLER::Execute( const std::string& aToolName,
                                          const nlohmann::json& aInput )
{
    if( aToolName == "screenshot" )
        return ExecuteScreenshot( aInput );

    return "Error: Unknown tool: " + aToolName;
}


std::string SCREENSHOT_HANDLER::GetDescription( const std::string& aToolName,
                                                 const nlohmann::json& aInput ) const
{
    std::string filePath = aInput.value( "file_path", "" );

    if( !filePath.empty() )
    {
        wxFileName fn( wxString::FromUTF8( filePath ) );
        return "Taking screenshot of " + fn.GetFullName().ToStdString();
    }

    return "Taking screenshot of current view";
}


std::string SCREENSHOT_HANDLER::ExecuteScreenshot( const nlohmann::json& aInput )
{
    // Extract file_path parameter (optional — omit to screenshot current view)
    std::string filePath = aInput.value( "file_path", "" );

    bool isSchematic = false;
    bool isPcb = false;
    bool useCurrentView = filePath.empty();

    if( useCurrentView )
    {
        wxLogInfo( "SCREENSHOT: No file_path provided, will screenshot current view" );

        // Try schematic editor first via IPC, then PCB editor
        isSchematic = true;
    }
    else
    {
        wxLogInfo( "SCREENSHOT: Processing %s", filePath.c_str() );

        // Validate path is safe for shell commands
        if( !IsPathSafeForShell( filePath ) )
            return "Error: file_path contains characters that are not allowed";

        // Check file exists
        if( !wxFileName::FileExists( wxString::FromUTF8( filePath ) ) )
            return "Error: File not found: " + filePath;

        // Determine file type from extension
        wxFileName fn( wxString::FromUTF8( filePath ) );
        wxString ext = fn.GetExt().Lower();

        isSchematic = ( ext == "kicad_sch" );
        isPcb = ( ext == "kicad_pcb" );

        if( !isSchematic && !isPcb )
        {
            return "Error: Unsupported file type '" + ext.ToStdString()
                   + "'. Expected .kicad_sch or .kicad_pcb";
        }
    }

    // Create a unique temp directory atomically via mkdtemp to avoid TOCTOU races
    std::string tempTemplate = ( wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
                                 + "zeo_screenshot_XXXXXX" ).ToStdString();
    std::vector<char> tempBuf( tempTemplate.begin(), tempTemplate.end() );
    tempBuf.push_back( '\0' );

    if( !mkdtemp( tempBuf.data() ) )
        return "Error: Failed to create temporary directory";

    wxString tempDir = wxString::FromUTF8( tempBuf.data() );

    std::string tempDirStr = tempDir.ToStdString();

    // Scope guard ensures temp directory is cleaned up on any exit path
    ScopeGuard cleanupGuard( [tempDir]()
    {
        // Remove files in subdirectories first (e.g. svg_out/), then top-level files
        wxDir dir( tempDir );
        if( dir.IsOpened() )
        {
            wxString entry;

            // Remove subdirectories and their contents
            bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_DIRS );
            while( cont )
            {
                wxString subDir = tempDir + wxFileName::GetPathSeparator() + entry;
                wxDir sub( subDir );
                if( sub.IsOpened() )
                {
                    wxString subFile;
                    bool sc = sub.GetFirst( &subFile );
                    while( sc )
                    {
                        wxRemoveFile( subDir + wxFileName::GetPathSeparator() + subFile );
                        sc = sub.GetNext( &subFile );
                    }
                }
                wxFileName::Rmdir( subDir );
                cont = dir.GetNext( &entry );
            }

            // Remove top-level files
            cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
            while( cont )
            {
                wxRemoveFile( tempDir + wxFileName::GetPathSeparator() + entry );
                cont = dir.GetNext( &entry );
            }
        }
        wxFileName::Rmdir( tempDir );
    } );

    // Build view configuration for PCB screenshots
    nlohmann::json viewConfig;
    if( aInput.contains( "view" ) )
        viewConfig["view"] = aInput["view"];
    if( aInput.contains( "layers" ) )
        viewConfig["layers"] = aInput["layers"];
    if( aInput.contains( "show_zones" ) )
        viewConfig["show_zones"] = aInput["show_zones"];
    if( aInput.contains( "show_vias" ) )
        viewConfig["show_vias"] = aInput["show_vias"];
    if( aInput.contains( "show_pads" ) )
        viewConfig["show_pads"] = aInput["show_pads"];
    if( aInput.contains( "show_values" ) )
        viewConfig["show_values"] = aInput["show_values"];
    if( aInput.contains( "show_references" ) )
        viewConfig["show_references"] = aInput["show_references"];

    wxLogInfo( "SCREENSHOT: viewConfig = %s", viewConfig.dump().c_str() );

    // Export to SVG — prefer in-memory IPC export when editor is open,
    // fall back to kicad-cli disk-based export otherwise.
    // We always attempt IPC when the send function is available; SendRequest will
    // return an error if the target editor frame is not open, and we fall back.
    std::string svgPath;

    if( TOOL_REGISTRY::Instance().GetSendRequestFn() )
    {
        // When useCurrentView is true, first try schematic, then PCB.
        // Pass empty filePath for current view so IPC handler doesn't navigate.
        svgPath = ExportViaIpc( isSchematic, tempDirStr,
                                useCurrentView ? "" : filePath, viewConfig );

        // If screenshotting current view and schematic failed, try PCB
        if( svgPath.empty() && useCurrentView && isSchematic )
        {
            wxLogInfo( "SCREENSHOT: Schematic IPC failed for current view, trying PCB" );
            isSchematic = false;
            isPcb = true;
            svgPath = ExportViaIpc( false, tempDirStr, "", viewConfig );
        }

        if( svgPath.empty() )
            wxLogWarning( "SCREENSHOT: IPC export failed, falling back to kicad-cli" );
    }

    // Fall back to kicad-cli if IPC was not used or failed
    // (kicad-cli requires a file path, so this only works when one was provided)
    if( svgPath.empty() && !useCurrentView )
    {
        if( isSchematic )
            svgPath = ExportSchematicSvg( filePath, tempDirStr );
        else
            svgPath = ExportPcbSvg( filePath, tempDirStr );
    }

    if( svgPath.empty() )
    {
        return useCurrentView ? "Error: Failed to export current view. Is a schematic or PCB editor open?"
                              : "Error: Failed to export SVG from " + filePath;
    }

    // Convert SVG to PNG (NOTE: uses macOS sips, will fail on other platforms)
    std::string pngPath = tempDirStr + "/screenshot.png";

    if( !ConvertSvgToPng( svgPath, pngPath ) )
        return "Error: Failed to convert SVG to PNG. Is 'sips' available?";

    // Crop background and resize to fit API limits (non-fatal on failure).
    // Pass the theme's canvas background since SVG export doesn't include it.
    bool cropOk = isSchematic ? CropAndResize( pngPath, SCH_BG_R, SCH_BG_G, SCH_BG_B )
                              : CropAndResize( pngPath, PCB_BG_R, PCB_BG_G, PCB_BG_B );

    if( !cropOk )
        wxLogWarning( "SCREENSHOT: Crop and resize failed, using original image" );

    // Base64 encode the PNG
    std::string base64Data = ReadFileAsBase64( pngPath );

    if( base64Data.empty() )
        return "Error: Failed to read and encode PNG file";

    wxLogInfo( "SCREENSHOT: Success, base64 length=%zu", base64Data.length() );

    // Build result JSON envelope
    nlohmann::json result;
    std::string label;

    if( useCurrentView )
        label = isSchematic ? "current schematic sheet" : "current PCB view";
    else
        label = wxFileName( wxString::FromUTF8( filePath ) ).GetFullName().ToStdString();

    result["text"] = "Screenshot of " + label;
    result["image"] = {
        { "media_type", "image/png" },
        { "base64", base64Data }
    };

    return result.dump();
}


std::string SCREENSHOT_HANDLER::ExportViaIpc( bool aIsSchematic, const std::string& aTempDir,
                                              const std::string& aFilePath,
                                              const nlohmann::json& aViewConfig )
{
    // Create svg_out subdirectory (schematic plotter writes into this directory)
    std::string outputDir = aTempDir + "/svg_out";
    wxFileName::Mkdir( wxString::FromUTF8( outputDir ), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    int targetFrame = aIsSchematic ? FRAME_SCH : FRAME_PCB_EDITOR;

    nlohmann::json cmd;
    cmd["type"] = "export_screenshot";
    cmd["output_dir"] = outputDir;

    // Pass the filename so the IPC handler can find the correct sheet
    if( !aFilePath.empty() )
    {
        wxFileName fn( wxString::FromUTF8( aFilePath ) );
        cmd["sheet_file"] = fn.GetFullName().ToStdString();
    }

    // Pass view configuration for PCB screenshots
    if( !aViewConfig.empty() )
        cmd["view_config"] = aViewConfig;

    wxLogInfo( "SCREENSHOT: Requesting in-memory export via IPC (frame=%d, file=%s)",
               targetFrame, aFilePath.empty() ? "current" : aFilePath.c_str() );

    std::string responseStr = TOOL_REGISTRY::Instance().GetSendRequestFn()( targetFrame, cmd.dump() );

    if( responseStr.empty() )
    {
        wxLogError( "SCREENSHOT: IPC export returned empty response" );
        return std::string();
    }

    // Parse response JSON
    nlohmann::json response = nlohmann::json::parse( responseStr, nullptr, false );

    if( response.is_discarded() )
    {
        wxLogError( "SCREENSHOT: IPC export response is not valid JSON: %s",
                    responseStr.c_str() );
        return std::string();
    }

    bool success = response.value( "success", false );

    if( !success )
    {
        wxLogError( "SCREENSHOT: IPC export reported failure" );
        return std::string();
    }

    std::string svgPath = response.value( "svg_path", "" );

    if( svgPath.empty() || !wxFileName::FileExists( wxString::FromUTF8( svgPath ) ) )
    {
        wxLogError( "SCREENSHOT: IPC export SVG not found at: %s", svgPath.c_str() );
        return std::string();
    }

    wxLogInfo( "SCREENSHOT: IPC export succeeded: %s", svgPath.c_str() );
    return svgPath;
}


std::string SCREENSHOT_HANDLER::ExportSchematicSvg( const std::string& aFilePath,
                                                     const std::string& aTempDir )
{
    std::string cmdPrefix = KiCadCliUtil::GetKicadCliCommandPrefix();
    if( cmdPrefix.empty() )
    {
        wxLogError( "SCREENSHOT: kicad-cli not found" );
        return std::string();
    }

    // SVG export outputs to a directory (one SVG per sheet)
    std::string outputDir = aTempDir + "/svg_out";
    wxFileName::Mkdir( wxString::FromUTF8( outputDir ), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    std::string cmd = cmdPrefix
                      + " sch export svg --exclude-drawing-sheet"
                      + " --theme _builtin_default"
                      + " -o \"" + outputDir + "\" \"" + aFilePath + "\"";

    std::string stdoutStr, stderrStr;
    int exitCode = RunCommand( cmd, stdoutStr, stderrStr );

    if( !stderrStr.empty() )
        wxLogWarning( "SCREENSHOT: kicad-cli stderr: %s", stderrStr.c_str() );

    if( exitCode != 0 )
    {
        wxLogError( "SCREENSHOT: kicad-cli sch export failed (exit %d)", exitCode );
        return std::string();
    }

    // Find the first SVG file in the output directory
    wxDir dir( wxString::FromUTF8( outputDir ) );
    if( !dir.IsOpened() )
        return std::string();

    wxString svgFilename;
    if( !dir.GetFirst( &svgFilename, "*.svg", wxDIR_FILES ) )
    {
        wxLogError( "SCREENSHOT: No SVG files found in output directory" );
        return std::string();
    }

    return outputDir + "/" + svgFilename.ToStdString();
}


std::string SCREENSHOT_HANDLER::ExportPcbSvg( const std::string& aFilePath,
                                               const std::string& aTempDir )
{
    std::string cmdPrefix = KiCadCliUtil::GetKicadCliCommandPrefix();
    if( cmdPrefix.empty() )
    {
        wxLogError( "SCREENSHOT: kicad-cli not found" );
        return std::string();
    }

    std::string outputPath = aTempDir + "/pcb.svg";

    std::string cmd = cmdPrefix
                      + " pcb export svg"
                      + " --exclude-drawing-sheet --fit-page-to-board"
                      + " --theme _builtin_default"
                      + " --layers F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,Edge.Cuts"
                      + " -o \"" + outputPath + "\" \"" + aFilePath + "\"";

    std::string stdoutStr, stderrStr;
    int exitCode = RunCommand( cmd, stdoutStr, stderrStr );

    if( !stderrStr.empty() )
        wxLogWarning( "SCREENSHOT: kicad-cli stderr: %s", stderrStr.c_str() );

    if( exitCode != 0 )
    {
        wxLogError( "SCREENSHOT: kicad-cli pcb export failed (exit %d)", exitCode );
        return std::string();
    }

    if( !wxFileName::FileExists( wxString::FromUTF8( outputPath ) ) )
    {
        wxLogError( "SCREENSHOT: PCB SVG not found at: %s", outputPath.c_str() );
        return std::string();
    }

    return outputPath;
}


bool SCREENSHOT_HANDLER::ConvertSvgToPng( const std::string& aSvgPath,
                                           const std::string& aPngPath )
{
    // Rasterize at 4096px so CropAndResize has plenty of pixels to crop from
    // before downscaling to the API limit (1568px).
    std::string cmd;

#ifdef __APPLE__
    cmd = "sips -s format png -Z 4096"
          " \"" + aSvgPath + "\" --out \"" + aPngPath + "\"";
#else
    // Linux: use rsvg-convert (librsvg2-bin) for SVG to PNG conversion
    cmd = "rsvg-convert -w 4096 -h 4096 --keep-aspect-ratio"
          " -o \"" + aPngPath + "\" \"" + aSvgPath + "\"";
#endif

    std::string stdoutStr, stderrStr;
    int exitCode = RunCommand( cmd, stdoutStr, stderrStr );

    if( !stderrStr.empty() )
        wxLogWarning( "SCREENSHOT: SVG convert stderr: %s", stderrStr.c_str() );

    if( exitCode != 0 )
    {
        wxLogError( "SCREENSHOT: SVG to PNG conversion failed (exit %d)", exitCode );
        return false;
    }

    return wxFileName::FileExists( wxString::FromUTF8( aPngPath ) );
}


bool SCREENSHOT_HANDLER::CropAndResize( const std::string& aPngPath,
                                        unsigned char aBgR, unsigned char aBgG,
                                        unsigned char aBgB )
{
    wxImage image;

    if( !image.LoadFile( wxString::FromUTF8( aPngPath ), wxBITMAP_TYPE_PNG ) )
    {
        wxLogError( "SCREENSHOT: Failed to load PNG for cropping" );
        return false;
    }

    int width = image.GetWidth();
    int height = image.GetHeight();

    wxLogInfo( "SCREENSHOT: Using background color RGB(%d, %d, %d)", aBgR, aBgG, aBgB );

    // Composite transparent pixels onto the specified background color.
    // SVG export doesn't include the canvas background, so we apply it here.
    if( image.HasAlpha() )
    {
        unsigned char* data = image.GetData();
        unsigned char* alpha = image.GetAlpha();

        for( int i = 0; i < width * height; ++i )
        {
            float a = alpha[i] / 255.0f;
            data[i * 3]     = static_cast<unsigned char>( data[i * 3]     * a + aBgR * ( 1 - a ) );
            data[i * 3 + 1] = static_cast<unsigned char>( data[i * 3 + 1] * a + aBgG * ( 1 - a ) );
            data[i * 3 + 2] = static_cast<unsigned char>( data[i * 3 + 2] * a + aBgB * ( 1 - a ) );
        }

        image.ClearAlpha();
    }

    // Find bounding box of content pixels (those that differ from the background).
    // Threshold of 5 catches antialiasing artifacts at content edges.
    static const int BG_THRESHOLD = 5;

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;

    unsigned char* data = image.GetData();

    for( int y = 0; y < height; ++y )
    {
        for( int x = 0; x < width; ++x )
        {
            int idx = ( y * width + x ) * 3;

            if( std::abs( data[idx]     - aBgR ) > BG_THRESHOLD
                || std::abs( data[idx + 1] - aBgG ) > BG_THRESHOLD
                || std::abs( data[idx + 2] - aBgB ) > BG_THRESHOLD )
            {
                if( x < minX ) minX = x;
                if( y < minY ) minY = y;
                if( x > maxX ) maxX = x;
                if( y > maxY ) maxY = y;
            }
        }
    }

    // All-background image: just resize if needed
    if( maxX < 0 || maxY < 0 )
    {
        if( width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION )
        {
            double scale = std::min( static_cast<double>( MAX_IMAGE_DIMENSION ) / width,
                                     static_cast<double>( MAX_IMAGE_DIMENSION ) / height );
            image.Rescale( static_cast<int>( width * scale ),
                           static_cast<int>( height * scale ), wxIMAGE_QUALITY_HIGH );
        }

        return image.SaveFile( wxString::FromUTF8( aPngPath ), wxBITMAP_TYPE_PNG );
    }

    // Add padding: 5% of content dimension, minimum 10px
    int contentW = maxX - minX + 1;
    int contentH = maxY - minY + 1;
    int padX = std::max( 10, contentW / 20 );
    int padY = std::max( 10, contentH / 20 );

    int cropX = std::max( 0, minX - padX );
    int cropY = std::max( 0, minY - padY );
    int cropW = std::min( width - cropX, contentW + 2 * padX );
    int cropH = std::min( height - cropY, contentH + 2 * padY );

    if( cropX != 0 || cropY != 0 || cropW != width || cropH != height )
    {
        image = image.GetSubImage( wxRect( cropX, cropY, cropW, cropH ) );
        width = image.GetWidth();
        height = image.GetHeight();
    }

    // Resize so longest side <= MAX_IMAGE_DIMENSION
    if( width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION )
    {
        double scale = std::min( static_cast<double>( MAX_IMAGE_DIMENSION ) / width,
                                 static_cast<double>( MAX_IMAGE_DIMENSION ) / height );
        image.Rescale( static_cast<int>( width * scale ),
                       static_cast<int>( height * scale ), wxIMAGE_QUALITY_HIGH );
    }

    wxLogInfo( "SCREENSHOT: Cropped %dx%d -> %dx%d",
               width, height, image.GetWidth(), image.GetHeight() );

    return image.SaveFile( wxString::FromUTF8( aPngPath ), wxBITMAP_TYPE_PNG );
}


std::string SCREENSHOT_HANDLER::ReadFileAsBase64( const std::string& aFilePath )
{
    std::ifstream file( aFilePath, std::ios::binary | std::ios::ate );
    if( !file.is_open() )
        return std::string();

    std::streamsize size = file.tellg();
    file.seekg( 0, std::ios::beg );

    if( size <= 0 )
        return std::string();

    std::vector<char> buffer( size );
    if( !file.read( buffer.data(), size ) )
        return std::string();

    file.close();

    return wxBase64Encode( buffer.data(), buffer.size() ).ToStdString();
}


int SCREENSHOT_HANDLER::RunCommand( const std::string& aCommand, std::string& aStdout,
                                     std::string& aStderr, int aTimeoutSec )
{
    aStdout.clear();
    aStderr.clear();

    // Create pipes for capturing stdout and stderr directly (no temp files)
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
        wxLogError( "SCREENSHOT: Command timed out after %d seconds", aTimeoutSec );
        return -1;
    }

    int status;
    waitpid( pid, &status, 0 );
    int exitCode = -1;

    if( WIFEXITED( status ) )
        exitCode = WEXITSTATUS( status );
    else if( WIFSIGNALED( status ) )
        wxLogError( "SCREENSHOT: Process killed by signal %d", WTERMSIG( status ) );

    return exitCode;
}
