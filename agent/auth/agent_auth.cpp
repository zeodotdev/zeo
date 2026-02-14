#include "agent_auth.h"
#include "../agent_llm_client.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <wx/datetime.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/intl.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <map>

using json = nlohmann::json;

static const size_t MAX_SESSION_FILE_SIZE = 1024 * 1024;  // 1MB safety limit

AGENT_AUTH::AGENT_AUTH() :
        m_authWebUrl( ZEO_BASE_URL + "/auth" ),
        m_tokenExpiry( 0 )
{
}

AGENT_AUTH::~AGENT_AUTH()
{
}

void AGENT_AUTH::Configure( const std::string& aProjectUrl, const std::string& aAnonKey )
{
    m_projectUrl = aProjectUrl;
    m_anonKey = aAnonKey;
    LoadSession();
}


void AGENT_AUTH::SignOut()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userEmail.clear();
    m_firstName.clear();
    m_avatarUrl.clear();
    m_tokenExpiry = 0;
    ClearSession();
    wxLogTrace( "Agent", "Signed out" );
}

bool AGENT_AUTH::StartOAuthFlow( const std::string& aSource )
{
    std::string callback = "kicad-agent://callback";
    if( !aSource.empty() )
        callback += "/" + aSource;  // Use path: kicad-agent://callback/agent

    std::ostringstream authUrl;
    authUrl << m_authWebUrl << "?redirect_uri=" << callback << "&signout=true";

    if( !wxLaunchDefaultBrowser( authUrl.str() ) )
    {
        wxMessageBox( _( "Could not open browser. Please check your default browser settings." ),
                      _( "Error" ), wxOK | wxICON_ERROR );
        return false;
    }

    return true;
}

bool AGENT_AUTH::IsAuthenticated()
{
    if( m_accessToken.empty() )
        return false;

    if( IsTokenExpired() )
    {
        // Try to proactively refresh (we're within the 5-min buffer)
        if( !RefreshToken() )
        {
            // Refresh failed, but if the token hasn't actually expired yet,
            // it's still valid at the server — don't block the user
            return !IsTokenHardExpired();
        }
    }

    return true;
}


std::string AGENT_AUTH::GetFirstName()
{
    return m_firstName;
}

std::string AGENT_AUTH::GetAvatarUrl()
{
    return m_avatarUrl;
}

std::string AGENT_AUTH::GetUserEmail()
{
    return m_userEmail;
}

std::string AGENT_AUTH::GetAccessToken()
{
    if( m_accessToken.empty() )
        return "";

    if( IsTokenExpired() )
    {
        if( !RefreshToken() )
        {
            // Refresh failed, but if the token hasn't actually expired yet,
            // it's still usable — return it so the request can proceed
            if( !IsTokenHardExpired() )
                return m_accessToken;

            return "";
        }
    }

    return m_accessToken;
}


bool AGENT_AUTH::RefreshToken()
{
    // Reload from disk first - another instance (e.g., launcher) may have already
    // refreshed the token. Supabase rotates refresh tokens, so using a stale one fails.
    TryReloadSession();

    if( !IsTokenExpired() )
    {
        wxLogTrace( "Agent", "Token already refreshed by another instance" );
        return true;
    }

    if( m_refreshToken.empty() )
        return false;

    KICAD_CURL_EASY curl;

    std::string url = m_projectUrl + "/auth/v1/token?grant_type=refresh_token";
    curl.SetURL( url );
    curl.SetHeader( "apikey", m_anonKey );
    curl.SetHeader( "Content-Type", "application/json" );

    json requestBody;
    requestBody["refresh_token"] = m_refreshToken;

    std::string jsonStr = requestBody.dump();
    curl.SetPostFields( jsonStr );

    try
    {
        curl.Perform();
        long httpCode = curl.GetResponseStatusCode();

        if( httpCode == 200 )
        {
            auto response = json::parse( curl.GetBuffer() );

            m_accessToken = response["access_token"].get<std::string>();
            m_refreshToken = response["refresh_token"].get<std::string>();
            m_tokenExpiry = response["expires_in"].get<long long>() + wxDateTime::GetTimeNow();

            SaveSession();
            wxLogTrace( "Agent", "Token refreshed successfully" );
            return true;
        }
        else
        {
            wxLogTrace( "Agent", "Token refresh failed with code %ld, checking disk", httpCode );

            // Another instance may have refreshed while our request was in flight
            TryReloadSession();

            if( !IsTokenExpired() )
            {
                wxLogTrace( "Agent", "Token recovered from disk after failed refresh" );
                return true;
            }

            // Only clear session if the token is truly expired (not just in the buffer).
            // Within the buffer, the token is still valid at the server — clearing the
            // session would be premature and cause "Please sign in" for a usable token.
            if( IsTokenHardExpired() )
            {
                wxLogTrace( "Agent", "Token refresh failed definitively, clearing session" );
                ClearSession();
            }
            else
            {
                wxLogTrace( "Agent", "Token refresh failed but token still valid, keeping session" );
            }
            return false;
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "Token refresh exception: %s", e.what() );

        // Network error - check if another instance refreshed successfully
        TryReloadSession();

        if( !IsTokenExpired() )
            return true;

        return false;
    }
}


// ============================================================================
// Session Persistence
// ============================================================================

void AGENT_AUTH::LoadSession()
{
    // Clear in-memory state first - this ensures that if storage is empty
    // (e.g., after sign-out from another app), we don't keep stale tokens
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userEmail.clear();
    m_firstName.clear();
    m_avatarUrl.clear();
    m_tokenExpiry = 0;

    std::string sessionData;

    if( ReadSessionFile( sessionData ) )
    {
        try
        {
            nlohmann::json session = nlohmann::json::parse( sessionData );
            m_accessToken = session.value( "access_token", "" );
            m_refreshToken = session.value( "refresh_token", "" );
            m_userEmail = session.value( "user_email", "" );
            m_firstName = session.value( "first_name", "" );
            m_avatarUrl = session.value( "avatar_url", "" );
            m_tokenExpiry = session.value( "token_expiry", 0LL );
            wxLogTrace( "Agent", "Loaded session for %s", m_userEmail.c_str() );
        }
        catch( const std::exception& e )
        {
            wxLogTrace( "Agent", "Failed to parse session: %s", e.what() );
        }
    }
}

void AGENT_AUTH::SaveSession()
{
    nlohmann::json session;
    session["access_token"] = m_accessToken;
    session["refresh_token"] = m_refreshToken;
    session["user_email"] = m_userEmail;
    session["first_name"] = m_firstName;
    session["avatar_url"] = m_avatarUrl;
    session["token_expiry"] = m_tokenExpiry;

    WriteSessionFile( session.dump() );
    wxLogTrace( "Agent", "Saved session to secure storage" );
}

void AGENT_AUTH::ClearSession()
{
    std::string path = GetSessionFilePath();
    std::remove( path.c_str() );
    wxLogTrace( "Agent", "Cleared session from secure storage" );
}

bool AGENT_AUTH::TryReloadSession()
{
    std::string sessionData;

    if( !ReadSessionFile( sessionData ) )
        return false;

    try
    {
        auto session = nlohmann::json::parse( sessionData );
        std::string newAccess = session.value( "access_token", "" );
        std::string newRefresh = session.value( "refresh_token", "" );

        if( newAccess.empty() || newRefresh.empty() )
            return false;

        m_accessToken = newAccess;
        m_refreshToken = newRefresh;
        m_tokenExpiry = session.value( "token_expiry", 0LL );
        m_userEmail = session.value( "user_email", "" );
        m_firstName = session.value( "first_name", "" );
        m_avatarUrl = session.value( "avatar_url", "" );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

bool AGENT_AUTH::IsTokenExpired()
{
    // Consider expired if within 5 minutes of expiry (proactive refresh buffer)
    return ( wxDateTime::GetTimeNow() + 300 ) >= m_tokenExpiry;
}

bool AGENT_AUTH::IsTokenHardExpired()
{
    // Token is actually expired and will be rejected by the server
    return wxDateTime::GetTimeNow() >= m_tokenExpiry;
}


// ============================================================================
// Secure File I/O
// ============================================================================

std::string AGENT_AUTH::GetSessionFilePath()
{
    // Follow the same pattern as AGENT_CHAT_HISTORY::GetHistoryDir()
    // wxStandardPaths returns ~/Library/Application Support/{appname}
    // We want ~/Library/Application Support/kicad/
    wxString appSupport = wxStandardPaths::Get().GetUserDataDir();
    wxFileName dir( appSupport, wxEmptyString );
    dir.RemoveLastDir();  // Remove app-specific dir
    dir.AppendDir( "kicad" );
    return dir.GetPath().ToStdString() + "/agent_session.json";
}

bool AGENT_AUTH::WriteSessionFile( const std::string& aData )
{
    std::string filePath = GetSessionFilePath();

    // Ensure parent directory exists
    wxString dir = wxFileName( wxString::FromUTF8( filePath ) ).GetPath();

    if( !wxFileName::DirExists( dir ) )
    {
        if( !wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            wxLogTrace( "Agent", "SecureStore: Failed to create directory %s",
                        dir.ToStdString().c_str() );
            return false;
        }
    }

    // Write to temp file first, then rename (atomic-ish write)
    std::string tempPath = filePath + ".tmp";
    std::ofstream file( tempPath, std::ios::binary | std::ios::trunc );

    if( !file.is_open() )
        return false;

    file.write( aData.data(), aData.size() );
    file.close();

    if( file.fail() )
    {
        std::remove( tempPath.c_str() );
        return false;
    }

    // Set restrictive permissions before rename
    chmod( tempPath.c_str(), 0600 );

    // Rename into place
    if( std::rename( tempPath.c_str(), filePath.c_str() ) != 0 )
    {
        std::remove( tempPath.c_str() );
        return false;
    }

    return true;
}

bool AGENT_AUTH::ReadSessionFile( std::string& aData )
{
    std::string filePath = GetSessionFilePath();
    std::ifstream file( filePath, std::ios::binary | std::ios::ate );

    if( !file.is_open() )
        return false;

    std::streamsize size = file.tellg();

    if( size <= 0 )
        return false;

    if( static_cast<size_t>( size ) > MAX_SESSION_FILE_SIZE )
    {
        wxLogTrace( "Agent", "SecureStore: Session file too large (%lld bytes): %s",
                    static_cast<long long>( size ), filePath.c_str() );
        return false;
    }

    file.seekg( 0 );
    aData.resize( static_cast<size_t>( size ) );
    file.read( &aData[0], size );

    if( file.fail() )
    {
        aData.clear();
        return false;
    }

    return true;
}


// ============================================================================
// URL Parsing Helpers
// ============================================================================

std::string AGENT_AUTH::UrlDecode( const std::string& value )
{
    std::string result;
    result.reserve( value.size() );

    for( size_t i = 0; i < value.size(); ++i )
    {
        if( value[i] == '%' && i + 2 < value.size() )
        {
            int hexValue;
            std::istringstream iss( value.substr( i + 1, 2 ) );
            if( iss >> std::hex >> hexValue )
            {
                result += static_cast<char>( hexValue );
                i += 2;
            }
            else
            {
                result += value[i];
            }
        }
        else if( value[i] == '+' )
        {
            result += ' ';
        }
        else
        {
            result += value[i];
        }
    }

    return result;
}

std::map<std::string, std::string> AGENT_AUTH::ParseQueryParams( const std::string& url )
{
    std::map<std::string, std::string> params;

    size_t queryStart = url.find( '?' );
    if( queryStart == std::string::npos )
        return params;

    std::string query = url.substr( queryStart + 1 );

    std::stringstream ss( query );
    std::string pair;
    while( std::getline( ss, pair, '&' ) )
    {
        size_t eq = pair.find( '=' );
        if( eq != std::string::npos )
        {
            std::string key = pair.substr( 0, eq );
            std::string value = UrlDecode( pair.substr( eq + 1 ) );
            params[key] = value;
        }
    }

    return params;
}

bool AGENT_AUTH::HandleOAuthCallback( const std::string& aCallbackUrl, std::string& aErrorMsg )
{
    auto params = ParseQueryParams( aCallbackUrl );

    // Check for error in callback
    if( params.count( "error" ) )
    {
        aErrorMsg = params.count( "error_description" ) ? params["error_description"] : params["error"];
        wxLogTrace( "Agent", "OAuth error: %s", aErrorMsg.c_str() );
        return false;
    }

    // Tokens received directly from auth web page
    if( params.count( "access_token" ) )
    {
        m_accessToken = params["access_token"];

        if( params.count( "refresh_token" ) )
            m_refreshToken = params["refresh_token"];

        if( params.count( "expires_in" ) )
            m_tokenExpiry = std::stoll( params["expires_in"] ) + wxDateTime::GetTimeNow();

        // Get user email from params or fetch from API
        // Get user email from params
        if( params.count( "user_email" ) )
        {
            m_userEmail = params["user_email"];
        }

        // Fetch user info from Supabase if needed (to get name and avatar)
        // We do this if we don't have the metadata (which implicitly comes from the user endpoint)
        // or if we somehow missed the email
        if( m_userEmail.empty() || m_firstName.empty() )
        {
            KICAD_CURL_EASY curl;
            curl.SetURL( m_projectUrl + "/auth/v1/user" );
            curl.SetHeader( "apikey", m_anonKey );
            curl.SetHeader( "Authorization", "Bearer " + m_accessToken );

            try
            {
                curl.Perform();
                if( curl.GetResponseStatusCode() == 200 )
                {
                    auto response = json::parse( curl.GetBuffer() );

                    if( m_userEmail.empty() )
                        m_userEmail = response["email"].get<std::string>();

                    if( response.contains( "user_metadata" ) )
                    {
                        auto metadata = response["user_metadata"];
                        if( metadata.contains( "avatar_url" ) )
                            m_avatarUrl = metadata["avatar_url"].get<std::string>();

                        // Try to get first name from full_name
                        if( metadata.contains( "full_name" ) )
                        {
                            std::string fullName = metadata["full_name"].get<std::string>();
                            size_t spacePos = fullName.find( ' ' );
                            if( spacePos != std::string::npos )
                                m_firstName = fullName.substr( 0, spacePos );
                            else
                                m_firstName = fullName;
                        }
                    }
                }
            }
            catch( ... )
            {
                if( m_userEmail.empty() )
                    m_userEmail = "user@unknown.com";
            }
        }

        SaveSession();
        // wxLogMessage( "OAuth sign in successful" );
        return true;
    }

    aErrorMsg = "No access token in callback";
    return false;
}
