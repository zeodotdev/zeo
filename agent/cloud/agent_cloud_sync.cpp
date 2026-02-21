#include "agent_cloud_sync.h"
#include "auth/agent_auth.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <fstream>
#include <sstream>
#include <thread>

using json = nlohmann::json;

static const std::string BUCKET_NAME = "user-data";
static const size_t MAX_UPLOAD_SIZE = 50 * 1024 * 1024;  // 50MB safety limit


AGENT_CLOUD_SYNC::AGENT_CLOUD_SYNC() :
        m_auth( nullptr ),
        m_configured( false )
{
}


AGENT_CLOUD_SYNC::~AGENT_CLOUD_SYNC()
{
}


void AGENT_CLOUD_SYNC::Configure( const std::string& aSupabaseUrl, const std::string& aAnonKey )
{
    m_supabaseUrl = aSupabaseUrl;
    m_anonKey = aAnonKey;
    LoadSyncState();
    m_configured = true;
    wxLogTrace( "Agent", "CLOUD_SYNC: Configured with %s", aSupabaseUrl.c_str() );
}


void AGENT_CLOUD_SYNC::SetAuth( AGENT_AUTH* aAuth )
{
    m_auth = aAuth;
}


// ============================================================================
// Public Upload Methods (all async — fire and forget)
// ============================================================================

void AGENT_CLOUD_SYNC::UploadChat( const std::string& aConversationId,
                                    const std::string& aJsonContent )
{
    if( !m_configured || !m_auth || !m_auth->IsAuthenticated() )
        return;

    if( aJsonContent.empty() || aJsonContent.size() > MAX_UPLOAD_SIZE )
        return;

    // Check if already uploaded at this size
    std::string key = "chat:" + aConversationId;

    if( IsAlreadyUploaded( key, aJsonContent.size() ) )
        return;

    // Capture values for the background thread
    std::string content = aJsonContent;
    std::string convId = aConversationId;
    std::string prefix = GetUserPrefix();

    if( prefix.empty() )
        return;

    std::thread( [this, prefix, convId, content, key]()
    {
        std::string storagePath = prefix + "/chats/" + convId + ".json";

        wxLogTrace( "Agent", "CLOUD_SYNC: Uploading chat %s (%zu bytes)",
                    convId.c_str(), content.size() );

        if( UploadToStorage( storagePath, content ) )
        {
            MarkUploaded( key, content.size() );
            wxLogTrace( "Agent", "CLOUD_SYNC: Chat %s uploaded successfully", convId.c_str() );
        }
        else
        {
            wxLogTrace( "Agent", "CLOUD_SYNC: Chat %s upload failed", convId.c_str() );
        }
    } ).detach();
}


void AGENT_CLOUD_SYNC::UploadLog( const std::string& aLogFilePath )
{
    if( !m_configured || !m_auth || !m_auth->IsAuthenticated() )
        return;

    wxFileName fn( wxString::FromUTF8( aLogFilePath ) );

    if( !fn.FileExists() )
        return;

    // Skip empty log files
    if( fn.GetSize() == 0 )
        return;

    std::string filename = fn.GetFullName().ToStdString();
    std::string key = "log:" + filename;

    // Read the file to check size
    std::string content;

    if( !ReadFile( aLogFilePath, content ) )
        return;

    if( content.size() > MAX_UPLOAD_SIZE )
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: Log file too large to upload: %s (%zu bytes)",
                    filename.c_str(), content.size() );
        return;
    }

    if( IsAlreadyUploaded( key, content.size() ) )
        return;

    std::string prefix = GetUserPrefix();

    if( prefix.empty() )
        return;

    std::thread( [this, prefix, filename, content, key]()
    {
        std::string storagePath = prefix + "/logs/" + filename;

        wxLogTrace( "Agent", "CLOUD_SYNC: Uploading log %s (%zu bytes)",
                    filename.c_str(), content.size() );

        if( UploadToStorage( storagePath, content ) )
        {
            MarkUploaded( key, content.size() );
            wxLogTrace( "Agent", "CLOUD_SYNC: Log %s uploaded successfully", filename.c_str() );
        }
        else
        {
            wxLogTrace( "Agent", "CLOUD_SYNC: Log %s upload failed", filename.c_str() );
        }
    } ).detach();
}


void AGENT_CLOUD_SYNC::SyncAll()
{
    if( !m_configured || !m_auth || !m_auth->IsAuthenticated() )
        return;

    std::string prefix = GetUserPrefix();

    if( prefix.empty() )
        return;

    std::thread( [this, prefix]()
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: Starting full sync for %s", prefix.c_str() );

        // 1. Upload all chat files
        std::string chatDir = GetChatDir();
        wxDir dir( wxString::FromUTF8( chatDir ) );

        if( dir.IsOpened() )
        {
            wxString filename;
            bool cont = dir.GetFirst( &filename, "*.json", wxDIR_FILES );

            while( cont )
            {
                wxString fullPath = wxString::FromUTF8( chatDir ) + wxFileName::GetPathSeparator()
                                    + filename;
                std::string convId = wxFileName( filename ).GetName().ToStdString();
                std::string key = "chat:" + convId;

                std::string content;

                if( ReadFile( fullPath.ToStdString(), content )
                    && !content.empty()
                    && content.size() <= MAX_UPLOAD_SIZE
                    && !IsAlreadyUploaded( key, content.size() ) )
                {
                    std::string storagePath = prefix + "/chats/" + convId + ".json";

                    wxLogTrace( "Agent", "CLOUD_SYNC: Syncing chat %s (%zu bytes)",
                                convId.c_str(), content.size() );

                    if( UploadToStorage( storagePath, content ) )
                    {
                        MarkUploaded( key, content.size() );
                    }
                }

                cont = dir.GetNext( &filename );
            }
        }

        // 2. Upload all log files
        std::string logDir = GetLogDir();
        wxDir logDirObj( wxString::FromUTF8( logDir ) );

        if( logDirObj.IsOpened() )
        {
            wxString filename;
            bool cont = logDirObj.GetFirst( &filename, "agent-*.log", wxDIR_FILES );

            while( cont )
            {
                wxString fullPath = wxString::FromUTF8( logDir ) + wxFileName::GetPathSeparator()
                                    + filename;
                std::string fname = filename.ToStdString();
                std::string key = "log:" + fname;

                wxFileName fn( fullPath );

                // Skip empty logs
                if( fn.GetSize() > 0 )
                {
                    std::string content;

                    if( ReadFile( fullPath.ToStdString(), content )
                        && !content.empty()
                        && content.size() <= MAX_UPLOAD_SIZE
                        && !IsAlreadyUploaded( key, content.size() ) )
                    {
                        std::string storagePath = prefix + "/logs/" + fname;

                        wxLogTrace( "Agent", "CLOUD_SYNC: Syncing log %s (%zu bytes)",
                                    fname.c_str(), content.size() );

                        if( UploadToStorage( storagePath, content ) )
                        {
                            MarkUploaded( key, content.size() );
                        }
                    }
                }

                cont = logDirObj.GetNext( &filename );
            }
        }

        wxLogTrace( "Agent", "CLOUD_SYNC: Full sync complete" );
    } ).detach();
}


// ============================================================================
// Supabase Storage Upload
// ============================================================================

bool AGENT_CLOUD_SYNC::UploadToStorage( const std::string& aStoragePath,
                                         const std::string& aContent )
{
    if( !m_auth )
        return false;

    std::string accessToken = m_auth->GetAccessToken();

    if( accessToken.empty() )
        return false;

    // Supabase Storage REST API:
    // PUT /storage/v1/object/{bucket}/{path}
    std::string url = m_supabaseUrl + "/storage/v1/object/" + BUCKET_NAME + "/" + aStoragePath;

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetHeader( "apikey", m_anonKey );
        curl.SetHeader( "Content-Type", "application/octet-stream" );
        curl.SetHeader( "x-upsert", "true" );
        curl.SetPostFields( aContent );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode >= 200 && httpCode < 300 )
        {
            return true;
        }
        else
        {
            wxLogTrace( "Agent", "CLOUD_SYNC: Upload failed with HTTP %ld for %s: %s",
                        httpCode, aStoragePath.c_str(), curl.GetBuffer().c_str() );
            return false;
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: Upload exception for %s: %s",
                    aStoragePath.c_str(), e.what() );
        return false;
    }
}


// ============================================================================
// Helpers
// ============================================================================

std::string AGENT_CLOUD_SYNC::GetUserPrefix()
{
    if( !m_auth )
        return "";

    std::string email = m_auth->GetUserEmail();

    if( email.empty() )
        return "";

    return email;
}


bool AGENT_CLOUD_SYNC::IsAlreadyUploaded( const std::string& aKey, size_t aSize )
{
    std::lock_guard<std::mutex> lock( m_syncStateMutex );

    if( m_syncState.contains( aKey ) )
    {
        auto& entry = m_syncState[aKey];
        size_t uploadedSize = entry.value( "size", (size_t) 0 );

        return uploadedSize == aSize;
    }

    return false;
}


void AGENT_CLOUD_SYNC::MarkUploaded( const std::string& aKey, size_t aSize )
{
    {
        std::lock_guard<std::mutex> lock( m_syncStateMutex );
        m_syncState[aKey] = { { "size", aSize } };
    }

    SaveSyncState();
}


// ============================================================================
// Sync State Persistence
// ============================================================================

std::string AGENT_CLOUD_SYNC::GetSyncStatePath()
{
    wxString appSupport = wxStandardPaths::Get().GetUserDataDir();
    wxFileName dir( appSupport, wxEmptyString );
    dir.RemoveLastDir();
    dir.AppendDir( "kicad" );
    return dir.GetPath().ToStdString() + "/agent_sync_state.json";
}


void AGENT_CLOUD_SYNC::LoadSyncState()
{
    std::lock_guard<std::mutex> lock( m_syncStateMutex );

    std::string path = GetSyncStatePath();
    std::ifstream file( path );

    if( file.is_open() )
    {
        try
        {
            file >> m_syncState;
        }
        catch( ... )
        {
            m_syncState = json::object();
        }
    }
    else
    {
        m_syncState = json::object();
    }
}


void AGENT_CLOUD_SYNC::SaveSyncState()
{
    std::lock_guard<std::mutex> lock( m_syncStateMutex );

    std::string path = GetSyncStatePath();
    std::ofstream file( path );

    if( file.is_open() )
    {
        file << m_syncState.dump( 2 );
    }
}


std::string AGENT_CLOUD_SYNC::GetLogDir()
{
    wxString homeDir = wxFileName::GetHomeDir();
    return ( homeDir + wxS( "/Library/Logs/Zeo" ) ).ToStdString();
}


std::string AGENT_CLOUD_SYNC::GetChatDir()
{
    wxString appSupport = wxStandardPaths::Get().GetUserDataDir();
    wxFileName dir( appSupport, wxEmptyString );
    dir.RemoveLastDir();
    dir.AppendDir( "kicad" );
    dir.AppendDir( "agent_chats" );
    return dir.GetPath().ToStdString();
}


bool AGENT_CLOUD_SYNC::ReadFile( const std::string& aPath, std::string& aContent )
{
    std::ifstream file( aPath, std::ios::binary | std::ios::ate );

    if( !file.is_open() )
        return false;

    std::streamsize size = file.tellg();

    if( size <= 0 )
        return false;

    file.seekg( 0 );
    aContent.resize( static_cast<size_t>( size ) );
    file.read( &aContent[0], size );

    return !file.fail();
}
