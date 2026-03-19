#include "agent_cloud_sync.h"
#include <zeo/agent_auth.h>
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
    std::lock_guard<std::mutex> lock( m_authMutex );
    m_auth = aAuth;
}


AGENT_CLOUD_SYNC::AuthSnapshot AGENT_CLOUD_SYNC::SnapshotAuth()
{
    std::lock_guard<std::mutex> lock( m_authMutex );
    AuthSnapshot snap;

    if( m_auth && m_auth->IsAuthenticated() )
    {
        snap.accessToken = m_auth->GetAccessToken();
        snap.email = m_auth->GetUserEmail();
        snap.valid = !snap.accessToken.empty() && !snap.email.empty();
    }

    return snap;
}


// ============================================================================
// Public Upload Methods (all async — fire and forget)
// ============================================================================

void AGENT_CLOUD_SYNC::UploadChat( const std::string& aConversationId,
                                    const std::string& aJsonContent )
{
    if( !m_configured )
        return;

    if( aJsonContent.empty() || aJsonContent.size() > MAX_UPLOAD_SIZE )
        return;

    // Check if already uploaded at this size
    std::string key = "chat:" + aConversationId;

    if( IsAlreadyUploaded( key, aJsonContent.size() ) )
        return;

    // Snapshot auth on the main thread so background thread never touches m_auth
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return;

    // Capture values for the background thread
    std::string content = aJsonContent;
    std::string convId = aConversationId;
    std::string prefix = auth.email;
    std::string supabaseUrl = m_supabaseUrl;
    std::string anonKey = m_anonKey;

    std::thread( [this, prefix, convId, content, key, auth, supabaseUrl, anonKey]()
    {
        std::string storagePath = prefix + "/chats/" + convId + ".json";

        wxLogTrace( "Agent", "CLOUD_SYNC: Uploading chat %s (%zu bytes)",
                    convId.c_str(), content.size() );

        if( UploadToStorageWithToken( storagePath, content, auth.accessToken,
                                       supabaseUrl, anonKey ) )
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
    if( !m_configured )
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

    // Snapshot auth on the main thread so background thread never touches m_auth
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return;

    std::string prefix = auth.email;
    std::string supabaseUrl = m_supabaseUrl;
    std::string anonKey = m_anonKey;

    std::thread( [this, prefix, filename, content, key, auth, supabaseUrl, anonKey]()
    {
        std::string storagePath = prefix + "/logs/" + filename;

        wxLogTrace( "Agent", "CLOUD_SYNC: Uploading log %s (%zu bytes)",
                    filename.c_str(), content.size() );

        if( UploadToStorageWithToken( storagePath, content, auth.accessToken,
                                       supabaseUrl, anonKey ) )
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
    if( !m_configured )
        return;

    // Snapshot auth on the main thread so background thread never touches m_auth
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return;

    std::string prefix = auth.email;
    std::string supabaseUrl = m_supabaseUrl;
    std::string anonKey = m_anonKey;

    std::thread( [this, prefix, auth, supabaseUrl, anonKey]()
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

                    if( UploadToStorageWithToken( storagePath, content, auth.accessToken,
                                                   supabaseUrl, anonKey ) )
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

                        if( UploadToStorageWithToken( storagePath, content, auth.accessToken,
                                                       supabaseUrl, anonKey ) )
                        {
                            MarkUploaded( key, content.size() );
                        }
                    }
                }

                cont = logDirObj.GetNext( &filename );
            }
        }

        // 3. Download remote-only chats to local storage
        DownloadChatsWithToken( auth.accessToken, supabaseUrl, anonKey );

        wxLogTrace( "Agent", "CLOUD_SYNC: Full sync complete" );
    } ).detach();
}


// ============================================================================
// Supabase Storage Upload
// ============================================================================

bool AGENT_CLOUD_SYNC::UploadToStorage( const std::string& aStoragePath,
                                         const std::string& aContent )
{
    // Main-thread version: snapshot auth and delegate
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return false;

    return UploadToStorageWithToken( aStoragePath, aContent, auth.accessToken,
                                     m_supabaseUrl, m_anonKey );
}


bool AGENT_CLOUD_SYNC::UploadToStorageWithToken( const std::string& aStoragePath,
                                                   const std::string& aContent,
                                                   const std::string& aAccessToken,
                                                   const std::string& aSupabaseUrl,
                                                   const std::string& aAnonKey )
{
    if( aAccessToken.empty() )
        return false;

    // Supabase Storage REST API:
    // PUT /storage/v1/object/{bucket}/{path}
    std::string url = aSupabaseUrl + "/storage/v1/object/" + BUCKET_NAME + "/" + aStoragePath;

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + aAccessToken );
        curl.SetHeader( "apikey", aAnonKey );
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
    std::lock_guard<std::mutex> lock( m_authMutex );

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
#ifdef __APPLE__
    wxString logDir = wxFileName::GetHomeDir() + wxS( "/Library/Logs/Zeo" );
#else
    wxString logDir = wxStandardPaths::Get().GetUserDataDir() + wxFileName::GetPathSeparator() + wxS( "logs" );
#endif
    return logDir.ToStdString();
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


std::string AGENT_CLOUD_SYNC::GetCurrentLogFilename()
{
    std::string logDir = GetLogDir();
    wxDir dir( wxString::FromUTF8( logDir ) );

    if( !dir.IsOpened() )
        return "";

    // Find the most recently modified agent-*.log file
    wxString bestFile;
    wxDateTime bestTime;

    wxString filename;
    bool cont = dir.GetFirst( &filename, "agent-*.log", wxDIR_FILES );

    while( cont )
    {
        wxString fullPath = wxString::FromUTF8( logDir ) + wxFileName::GetPathSeparator() + filename;
        wxFileName fn( fullPath );
        wxDateTime modTime = fn.GetModificationTime();

        if( !bestTime.IsValid() || modTime.IsLaterThan( bestTime ) )
        {
            bestTime = modTime;
            bestFile = filename;
        }

        cont = dir.GetNext( &filename );
    }

    return bestFile.ToStdString();
}


std::string AGENT_CLOUD_SYNC::GetUserEmail()
{
    return GetUserPrefix();
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


// ============================================================================
// Supabase Storage Download
// ============================================================================

void AGENT_CLOUD_SYNC::DownloadChats()
{
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return;

    DownloadChatsWithToken( auth.accessToken, m_supabaseUrl, m_anonKey );
}


void AGENT_CLOUD_SYNC::DownloadChatsWithToken( const std::string& aAccessToken,
                                                 const std::string& aSupabaseUrl,
                                                 const std::string& aAnonKey )
{
    if( !m_configured || aAccessToken.empty() )
        return;

    // Use SnapshotAuth for prefix (email) — safe, uses mutex
    std::string prefix = GetUserPrefix();

    if( prefix.empty() )
        return;

    wxLogTrace( "Agent", "CLOUD_SYNC: Checking for remote-only chats" );

    // 1. List remote chat files
    json remoteFiles = ListRemoteChatsWith( aAccessToken, aSupabaseUrl, aAnonKey, prefix );

    if( remoteFiles.empty() || !remoteFiles.is_array() )
        return;

    // 2. Build set of local chat files with their sizes
    std::string chatDir = GetChatDir();

    // Ensure local chat directory exists
    wxString wxChatDir = wxString::FromUTF8( chatDir );

    if( !wxFileName::DirExists( wxChatDir ) )
    {
        wxFileName::Mkdir( wxChatDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
    }

    std::map<std::string, size_t> localFiles;
    wxDir dir( wxChatDir );

    if( dir.IsOpened() )
    {
        wxString filename;
        bool cont = dir.GetFirst( &filename, "*.json", wxDIR_FILES );

        while( cont )
        {
            wxString fullPath = wxChatDir + wxFileName::GetPathSeparator() + filename;
            wxFileName fn( fullPath );
            localFiles[filename.ToStdString()] = static_cast<size_t>( fn.GetSize().GetValue() );
            cont = dir.GetNext( &filename );
        }
    }

    // 3. Download missing or newer remote chats
    int downloaded = 0;

    for( const auto& entry : remoteFiles )
    {
        if( !entry.contains( "name" ) )
            continue;

        std::string remoteName = entry["name"].get<std::string>();

        // Skip non-JSON files
        if( remoteName.size() < 5
            || remoteName.substr( remoteName.size() - 5 ) != ".json" )
            continue;

        size_t remoteSize = 0;

        if( entry.contains( "metadata" ) && entry["metadata"].contains( "size" ) )
            remoteSize = entry["metadata"]["size"].get<size_t>();

        auto it = localFiles.find( remoteName );

        if( it != localFiles.end() && it->second >= remoteSize )
            continue;  // Local copy exists and is same size or larger

        // Download this chat
        std::string storagePath = prefix + "/chats/" + remoteName;
        std::string content;

        if( DownloadFromStorageWithToken( storagePath, content, aAccessToken,
                                           aSupabaseUrl, aAnonKey )
            && !content.empty() )
        {
            std::string localPath = chatDir + "/" + remoteName;
            std::ofstream outFile( localPath, std::ios::binary );

            if( outFile.is_open() )
            {
                outFile.write( content.data(), content.size() );
                outFile.close();
                downloaded++;

                wxLogTrace( "Agent", "CLOUD_SYNC: Downloaded chat %s (%zu bytes)",
                            remoteName.c_str(), content.size() );
            }
        }
    }

    if( downloaded > 0 )
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: Downloaded %d remote chats", downloaded );
    }
    else
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: No new remote chats to download" );
    }
}


json AGENT_CLOUD_SYNC::ListRemoteChats()
{
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return json::array();

    return ListRemoteChatsWith( auth.accessToken, m_supabaseUrl, m_anonKey, auth.email );
}


json AGENT_CLOUD_SYNC::ListRemoteChatsWith( const std::string& aAccessToken,
                                              const std::string& aSupabaseUrl,
                                              const std::string& aAnonKey,
                                              const std::string& aPrefix )
{
    if( aAccessToken.empty() || aPrefix.empty() )
        return json::array();

    // Supabase Storage list API:
    // POST /storage/v1/object/list/{bucket}
    std::string url = aSupabaseUrl + "/storage/v1/object/list/" + BUCKET_NAME;

    json body;
    body["prefix"] = aPrefix + "/chats/";
    body["limit"] = 1000;

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + aAccessToken );
        curl.SetHeader( "apikey", aAnonKey );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetPostFields( body.dump() );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode >= 200 && httpCode < 300 )
        {
            return json::parse( curl.GetBuffer() );
        }
        else
        {
            wxLogTrace( "Agent", "CLOUD_SYNC: List remote chats failed with HTTP %ld: %s",
                        httpCode, curl.GetBuffer().c_str() );
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: List remote chats exception: %s", e.what() );
    }

    return json::array();
}


bool AGENT_CLOUD_SYNC::DownloadFromStorage( const std::string& aStoragePath,
                                             std::string& aContent )
{
    AuthSnapshot auth = SnapshotAuth();

    if( !auth.valid )
        return false;

    return DownloadFromStorageWithToken( aStoragePath, aContent, auth.accessToken,
                                         m_supabaseUrl, m_anonKey );
}


bool AGENT_CLOUD_SYNC::DownloadFromStorageWithToken( const std::string& aStoragePath,
                                                       std::string& aContent,
                                                       const std::string& aAccessToken,
                                                       const std::string& aSupabaseUrl,
                                                       const std::string& aAnonKey )
{
    if( aAccessToken.empty() )
        return false;

    // Supabase Storage download:
    // GET /storage/v1/object/{bucket}/{path}
    std::string url = aSupabaseUrl + "/storage/v1/object/" + BUCKET_NAME + "/" + aStoragePath;

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + aAccessToken );
        curl.SetHeader( "apikey", aAnonKey );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode >= 200 && httpCode < 300 )
        {
            aContent = curl.GetBuffer();
            return true;
        }
        else
        {
            wxLogTrace( "Agent", "CLOUD_SYNC: Download failed with HTTP %ld for %s",
                        httpCode, aStoragePath.c_str() );
            return false;
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "CLOUD_SYNC: Download exception for %s: %s",
                    aStoragePath.c_str(), e.what() );
        return false;
    }
}
