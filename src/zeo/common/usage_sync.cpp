/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include <usage_sync.h>

#include <build_version.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <paths.h>

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/platinfo.h>
#include <wx/utils.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <fstream>

using json = nlohmann::json;

namespace
{
constexpr size_t MAX_FLUSH_BATCH    = 100;
constexpr size_t MAX_BUFFER_LINES   = 5000;   // hard cap so a long-offline session doesn't grow unbounded

std::string nowIso8601Utc()
{
    return wxDateTime::UNow().ToUTC().FormatISOCombined().ToStdString() + "Z";
}

std::string newUuid()
{
    boost::uuids::uuid u = boost::uuids::random_generator()();
    return boost::uuids::to_string( u );
}

std::string detectOs()
{
#if defined( __APPLE__ )
    return "darwin";
#elif defined( _WIN32 )
    return "windows";
#elif defined( __linux__ )
    return "linux";
#else
    return "unknown";
#endif
}

std::string detectPlatform()
{
    std::string os = detectOs();
#if defined( __aarch64__ ) || defined( _M_ARM64 )
    return os + "-arm64";
#elif defined( __x86_64__ ) || defined( _M_X64 )
    return os + "-x64";
#else
    return os;
#endif
}
}   // anonymous namespace


USAGE_SYNC* USAGE_SYNC::Instance()
{
    static USAGE_SYNC s_instance;
    return &s_instance;
}


USAGE_SYNC::USAGE_SYNC()
{
    m_sessionId  = newUuid();
    m_appVersion = GetZeoVersion().ToStdString();
    m_os         = detectOs();
    m_platform   = detectPlatform();
    m_anonUid    = readOrCreateAnonUid();

    // Honor opt-out file at startup
    wxFileName optOut( getOptOutPath() );

    if( optOut.Exists() )
        m_enabled.store( false );
}


USAGE_SYNC::~USAGE_SYNC()
{
    Cleanup();
}


void USAGE_SYNC::Cleanup()
{
    if( m_stopping.exchange( true ) )
        return;     // already cleaned up

    std::vector<WorkerEntry> toJoin;
    {
        std::lock_guard<std::mutex> lock( m_workersMutex );
        toJoin.swap( m_workers );
    }

    for( WorkerEntry& w : toJoin )
    {
        if( w.thread.joinable() )
            w.thread.join();
    }
}


void USAGE_SYNC::spawnWorker( std::function<void()> aWork )
{
    if( m_stopping.load() )
        return;

    // Each worker carries an atomic "done" flag it flips just before the
    // thread function returns. spawnWorker uses that flag to safely
    // garbage-collect entries whose function has already returned, without
    // ever blocking on a join() (calling join() on a still-running flush
    // would re-introduce the hang this whole patch is fixing).
    auto done = std::make_shared<std::atomic<bool>>( false );

    std::lock_guard<std::mutex> lock( m_workersMutex );

    for( auto it = m_workers.begin(); it != m_workers.end(); )
    {
        if( it->done && it->done->load() && it->thread.joinable() )
        {
            it->thread.join();
            it = m_workers.erase( it );
        }
        else
        {
            ++it;
        }
    }

    m_workers.emplace_back( WorkerEntry{
        std::thread( [aWork = std::move( aWork ), done]()
                     {
                         aWork();
                         done->store( true );
                     } ),
        done } );
}


void USAGE_SYNC::Configure( const std::string& aSupabaseUrl, const std::string& aAnonKey )
{
    {
        std::lock_guard<std::mutex> lock( m_configMutex );
        m_supabaseUrl = aSupabaseUrl;
        m_anonKey     = aAnonKey;
    }

    m_configured.store( !aSupabaseUrl.empty() && !aAnonKey.empty() );

    // Drain whatever has been buffered before Configure() was called.
    if( m_configured.load() && m_enabled.load() && !m_flushInFlight.exchange( true ) )
    {
        spawnWorker( [this]()
                     {
                         flushBlocking();
                         m_flushInFlight.store( false );
                     } );
    }
}


namespace
{
// Mirrors AGENT_AUTH::GetSupabaseConfigPath(), which lives in the `common`
// target and is therefore not linkable from kicommon. The duplicate is
// intentional and small — both locations look up the same file.
std::string findSupabaseConfigPath()
{
    // Dev build: alongside the agent/ source dir.
    {
        wxFileName devPath( wxString::FromUTF8( __FILE__ ) );
        devPath.RemoveLastDir();   // common/ -> source root
        devPath.AppendDir( "agent" );
        devPath.SetFullName( "supabase_config.json" );

        if( wxFileExists( devPath.GetFullPath() ) )
            return devPath.GetFullPath().ToStdString();
    }

    // Installed: KICAD_STOCK_DATA_HOME
    {
        wxString stockData = wxGetenv( "KICAD_STOCK_DATA_HOME" );

        if( !stockData.empty() )
        {
            wxFileName p( stockData, "supabase_config.json" );

            if( wxFileExists( p.GetFullPath() ) )
                return p.GetFullPath().ToStdString();
        }
    }

    // Fallback install location
    {
        wxFileName p( "/usr/share/kicad", "supabase_config.json" );

        if( wxFileExists( p.GetFullPath() ) )
            return p.GetFullPath().ToStdString();
    }

    return "";
}
}   // anonymous namespace


void USAGE_SYNC::AutoConfigure()
{
    std::string path = findSupabaseConfigPath();

    if( path.empty() )
        return;

    std::ifstream in( path );

    if( !in.is_open() )
        return;

    std::string url;
    std::string key;

    try
    {
        json config = json::parse( in );
        url = config.value( "project_url", "" );
        key = config.value( "publishable_key", "" );
    }
    catch( ... )
    {
        return;
    }

    if( !url.empty() && !key.empty() )
        Configure( url, key );
}


void USAGE_SYNC::SetUser( const std::string& aEmail )
{
    bool identityChanged = false;
    {
        std::lock_guard<std::mutex> lock( m_userMutex );

        if( aEmail != m_userEmail )
        {
            m_userEmail = aEmail;
            identityChanged = true;
        }
    }

    // If the user signed in (or out) mid-session, the open session_start
    // recorded the previous identity. Emit a session_identify so downstream
    // analytics can attribute the whole session to the resolved user_key.
    if( identityChanged && m_sessionStarted.load() && m_enabled.load()
        && !m_stopping.load() )
    {
        json row = buildRow( "session_identify", "", "", nullptr );
        recordEvent( row );

        if( m_configured.load() && !m_flushInFlight.exchange( true ) )
        {
            spawnWorker( [this]()
                         {
                             flushBlocking();
                             m_flushInFlight.store( false );
                         } );
        }
    }
}


void USAGE_SYNC::SetEnabled( bool aEnabled )
{
    m_enabled.store( aEnabled );

    wxFileName optOut( getOptOutPath() );

    if( aEnabled )
    {
        if( optOut.Exists() )
            wxRemoveFile( optOut.GetFullPath() );
    }
    else
    {
        if( !optOut.Exists() )
        {
            wxFFile f( optOut.GetFullPath(), "w" );
            f.Write( "" );
            f.Close();
        }
    }
}


bool USAGE_SYNC::IsEnabled() const
{
    return m_enabled.load();
}


std::string USAGE_SYNC::getOptOutPath()
{
    return wxFileName( PATHS::GetUserCachePath(), "usage-opt-out" ).GetFullPath().ToStdString();
}


std::string USAGE_SYNC::getAnonUidPath()
{
    return wxFileName( PATHS::GetUserCachePath(), "usage-uid" ).GetFullPath().ToStdString();
}


std::string USAGE_SYNC::getBufferPath()
{
    return wxFileName( PATHS::GetUserCachePath(), "usage_events.jsonl" ).GetFullPath().ToStdString();
}


std::string USAGE_SYNC::readOrCreateAnonUid()
{
    wxFileName uidFile( getAnonUidPath() );

    if( uidFile.Exists() )
    {
        wxFFile in( uidFile.GetFullPath(), "r" );
        wxString contents;

        if( in.IsOpened() && in.ReadAll( &contents ) )
        {
            in.Close();
            wxString trimmed = contents;
            trimmed.Trim( true ).Trim( false );

            if( trimmed.length() == 36 )
                return trimmed.ToStdString();
        }
    }

    std::string fresh = newUuid();
    wxFFile out( uidFile.GetFullPath(), "w" );

    if( out.IsOpened() )
    {
        out.Write( fresh );
        out.Close();
    }

    return fresh;
}


std::string USAGE_SYNC::getUserKey()
{
    std::lock_guard<std::mutex> lock( m_userMutex );

    if( !m_userEmail.empty() )
        return m_userEmail;

    return "anon:" + m_anonUid;
}


bool USAGE_SYNC::getIsAnonymous()
{
    std::lock_guard<std::mutex> lock( m_userMutex );
    return m_userEmail.empty();
}


json USAGE_SYNC::buildRow( const std::string& aType,
                           const std::string& aName,
                           const std::string& aArea,
                           const json*        aProperties )
{
    json row = {
        { "ts",            nowIso8601Utc() },
        { "user_key",      getUserKey() },
        { "is_anonymous",  getIsAnonymous() },
        { "session_id",    m_sessionId },
        { "type",          aType },
        { "app_version",   m_appVersion },
        { "os",            m_os },
        { "platform",      m_platform }
    };

    if( !aName.empty() )
        row["name"] = aName;
    else
        row["name"] = nullptr;

    if( !aArea.empty() )
        row["app_area"] = aArea;
    else
        row["app_area"] = nullptr;

    if( aProperties && !aProperties->is_null() )
        row["properties"] = *aProperties;
    else
        row["properties"] = json::object();

    return row;
}


void USAGE_SYNC::recordEvent( const json& aRow )
{
    std::lock_guard<std::mutex> lock( m_bufferMutex );

    std::ofstream out( getBufferPath(), std::ios::app );

    if( !out.is_open() )
    {
        wxLogTrace( "UsageSync", "Buffer write failed: %s", getBufferPath().c_str() );
        return;
    }

    out << aRow.dump() << "\n";
    out.close();
}


void USAGE_SYNC::TrackEvent( const std::string& aName,
                             const std::string& aArea,
                             const json*        aProperties )
{
    if( !m_enabled.load() || m_stopping.load() )
        return;

    json row = buildRow( "event", aName, aArea, aProperties );
    recordEvent( row );

    if( m_configured.load() && !m_flushInFlight.exchange( true ) )
    {
        spawnWorker( [this]()
                     {
                         flushBlocking();
                         m_flushInFlight.store( false );
                     } );
    }
}


void USAGE_SYNC::SessionStart()
{
    if( !m_enabled.load() || m_stopping.load() )
        return;

    if( m_sessionStarted.exchange( true ) )
        return;     // already started in this process

    json row = buildRow( "session_start", "", "", nullptr );
    recordEvent( row );

    if( m_configured.load() && !m_flushInFlight.exchange( true ) )
    {
        spawnWorker( [this]()
                     {
                         flushBlocking();
                         m_flushInFlight.store( false );
                     } );
    }
}


void USAGE_SYNC::SessionEnd()
{
    if( !m_enabled.load() || !m_sessionStarted.load() )
        return;

    json row = buildRow( "session_end", "", "", nullptr );
    recordEvent( row );

    // Best-effort synchronous flush so the closing session_end has a chance
    // to land before process exit. m_stopping isn't set yet — that's
    // Cleanup()'s job.
    if( m_configured.load() && !m_flushInFlight.exchange( true ) )
    {
        flushBlocking();
        m_flushInFlight.store( false );
    }
}


void USAGE_SYNC::flushBlocking()
{
    std::string url;
    std::string key;
    {
        std::lock_guard<std::mutex> lock( m_configMutex );
        url = m_supabaseUrl;
        key = m_anonKey;
    }

    if( url.empty() || key.empty() )
        return;

    const std::string bufferPath   = getBufferPath();
    const std::string flushingPath = bufferPath + ".flushing";

    // Rotate the buffer file under a *brief* lock. The lock-held section
    // does NOTHING beyond a fast wxFileExists + (rename | no-op). If a
    // previous flush left a `.flushing` file behind, we leave the live
    // buffer untouched and drain `.flushing` first; the buffer rotates in
    // on the next flush. This guarantees TrackEvent on the main thread
    // never waits behind slow file I/O.
    {
        std::lock_guard<std::mutex> lock( m_bufferMutex );

        if( !wxFileExists( flushingPath ) )
        {
            if( !wxFileExists( bufferPath ) )
                return;     // nothing to flush

            wxRenameFile( bufferPath, flushingPath );
        }
        // else: drain the pre-existing flushing file this round.
    }

    // From here on we own the .flushing file exclusively — no other code
    // touches it, so we don't need m_bufferMutex.

    std::vector<std::string> pending;
    {
        std::ifstream in( flushingPath );

        if( !in.is_open() )
            return;

        std::string line;

        while( std::getline( in, line ) )
        {
            if( !line.empty() )
                pending.push_back( line );
        }
    }

    if( pending.empty() )
    {
        wxRemoveFile( flushingPath );
        return;
    }

    // Cap how much we send per flush; drop oldest if we're badly backed up.
    if( pending.size() > MAX_BUFFER_LINES )
    {
        pending.erase( pending.begin(),
                       pending.begin() + ( pending.size() - MAX_BUFFER_LINES ) );
    }

    std::string endpoint = url;

    if( !endpoint.empty() && endpoint.back() == '/' )
        endpoint.pop_back();

    endpoint += "/rest/v1/usage_events";

    std::vector<std::string> failed;

    for( size_t i = 0; i < pending.size(); i += MAX_FLUSH_BATCH )
    {
        if( m_stopping.load() )
        {
            for( size_t j = i; j < pending.size(); ++j )
                failed.push_back( pending[j] );

            break;
        }

        size_t end = std::min( i + MAX_FLUSH_BATCH, pending.size() );

        json batch = json::array();

        for( size_t j = i; j < end; ++j )
        {
            try
            {
                batch.push_back( json::parse( pending[j] ) );
            }
            catch( ... )
            {
                // Malformed buffer line — drop it.
            }
        }

        if( batch.empty() )
            continue;

        std::string body = batch.dump();

        KICAD_CURL_EASY curl;
        curl.SetURL( endpoint );
        curl.SetHeader( "apikey", key );
        curl.SetHeader( "Authorization", "Bearer " + key );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetHeader( "Prefer", "return=minimal" );
        curl.SetPostFields( body );

        try
        {
            curl.Perform();
            int code = curl.GetResponseStatusCode();

            if( code < 200 || code >= 300 )
            {
                wxLogTrace( "UsageSync", "Flush HTTP %d for batch of %zu", code, end - i );

                for( size_t j = i; j < end; ++j )
                    failed.push_back( pending[j] );
            }
        }
        catch( ... )
        {
            // Network/transport failure: keep the batch around for next flush.
            for( size_t j = i; j < end; ++j )
                failed.push_back( pending[j] );
        }
    }

    if( failed.empty() )
    {
        wxRemoveFile( flushingPath );
    }
    else
    {
        // Re-write the failed rows into the flushing file (no lock needed —
        // we exclusively own it). They'll be retried on the next flush.
        std::ofstream out( flushingPath, std::ios::trunc );

        if( out.is_open() )
        {
            for( const std::string& line : failed )
                out << line << "\n";
        }
    }
}
