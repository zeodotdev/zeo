#include "agent_monitor_log.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>


AGENT_MONITOR_LOG& AGENT_MONITOR_LOG::Instance()
{
    static AGENT_MONITOR_LOG instance;
    return instance;
}


AGENT_MONITOR_LOG::AGENT_MONITOR_LOG()
{
    wxString homeDir = wxFileName::GetHomeDir();
    wxString logDir = homeDir + wxS( "/Library/Logs/Zeo" );

    if( !wxFileName::DirExists( logDir ) )
        wxFileName::Mkdir( logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    m_logPath = ( logDir + wxS( "/agent-monitor.jsonl" ) ).ToStdString();
}


AGENT_MONITOR_LOG::~AGENT_MONITOR_LOG()
{
    std::lock_guard<std::mutex> lock( m_mutex );

    if( m_file.is_open() )
        m_file.close();
}


void AGENT_MONITOR_LOG::EnsureOpen()
{
    if( !m_file.is_open() )
    {
        TruncateIfNeeded();
        m_file.open( m_logPath, std::ios::app );
    }
}


void AGENT_MONITOR_LOG::TruncateIfNeeded()
{
    struct stat st;

    if( stat( m_logPath.c_str(), &st ) == 0 && st.st_size > 10 * 1024 * 1024 )
    {
        // Read last 5MB, rewrite file
        std::ifstream in( m_logPath, std::ios::binary );

        if( in.is_open() )
        {
            in.seekg( st.st_size - 5 * 1024 * 1024 );

            // Skip to next newline to avoid partial line
            std::string discard;
            std::getline( in, discard );

            std::string remainder( std::istreambuf_iterator<char>( in ), {} );
            in.close();

            std::ofstream out( m_logPath, std::ios::trunc );
            out << remainder;
            out.close();
        }
    }
}


std::string AGENT_MONITOR_LOG::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t( now );
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch() ) % 1000;

    std::ostringstream oss;
    oss << std::put_time( std::localtime( &time ), "%Y-%m-%dT%H:%M:%S" );
    oss << '.' << std::setfill( '0' ) << std::setw( 3 ) << ms.count();
    return oss.str();
}


std::string AGENT_MONITOR_LOG::EscapeJson( const std::string& aStr )
{
    std::string result;
    result.reserve( aStr.size() + 16 );

    for( char c : aStr )
    {
        switch( c )
        {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if( static_cast<unsigned char>( c ) < 0x20 )
            {
                char buf[8];
                snprintf( buf, sizeof( buf ), "\\u%04x", (unsigned char) c );
                result += buf;
            }
            else
            {
                result += c;
            }
        }
    }

    return result;
}


void AGENT_MONITOR_LOG::WriteLine( const std::string& aJson )
{
    EnsureOpen();

    if( m_file.is_open() )
    {
        m_file << aJson << '\n';
        m_file.flush();
    }
}


void AGENT_MONITOR_LOG::LogCommandStart( const std::string& aType, const std::string& aMode,
                                          const std::string& aCmd )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    std::string json = "{\"ts\":\"" + GetTimestamp()
                     + "\",\"event\":\"cmd_start\""
                     + ",\"type\":\"" + EscapeJson( aType )
                     + "\",\"mode\":\"" + EscapeJson( aMode )
                     + "\",\"cmd\":\"" + EscapeJson( aCmd ) + "\"}";

    WriteLine( json );
}


void AGENT_MONITOR_LOG::LogCommandEnd( const std::string& aType, const std::string& aMode,
                                        const std::string& aOutput, bool aSuccess,
                                        long aDurationMs )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    std::string json = "{\"ts\":\"" + GetTimestamp()
                     + "\",\"event\":\"cmd_end\""
                     + ",\"type\":\"" + EscapeJson( aType )
                     + "\",\"mode\":\"" + EscapeJson( aMode )
                     + "\",\"success\":" + ( aSuccess ? "true" : "false" )
                     + ",\"duration_ms\":" + std::to_string( aDurationMs )
                     + ",\"output\":\"" + EscapeJson( aOutput ) + "\"}";

    WriteLine( json );
}


void AGENT_MONITOR_LOG::LogError( const std::string& aContext, const std::string& aError )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    std::string json = "{\"ts\":\"" + GetTimestamp()
                     + "\",\"event\":\"error\""
                     + ",\"context\":\"" + EscapeJson( aContext )
                     + "\",\"error\":\"" + EscapeJson( aError ) + "\"}";

    WriteLine( json );
}
