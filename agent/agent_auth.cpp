#include "agent_auth.h"
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>
#include <wx/datetime.h>
#include <sstream>
#include <map>

using json = nlohmann::json;

AGENT_AUTH::AGENT_AUTH() :
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
    // wxLogMessage( "Signed out" );
}

bool AGENT_AUTH::IsAuthenticated()
{
    if( m_accessToken.empty() )
        return false;

    if( IsTokenExpired() )
    {
        // Try to refresh
        if( !RefreshToken() )
            return false;
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


bool AGENT_AUTH::RefreshToken()
{
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
            wxLogTrace( "Agent", "Token refresh failed with code %ld", httpCode );
            ClearSession();
            return false;
        }
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "Token refresh exception: %s", e.what() );
        return false;
    }
}

void AGENT_AUTH::LoadSession()
{
    // Tokens will now persist between app sessions via keychain
    std::string accessToken, refreshToken, userEmail, firstName, avatarUrl, expiryStr;
    
    if( m_keychain.GetPassword( "kicad.agent.supabase", "access_token", accessToken ) &&
        m_keychain.GetPassword( "kicad.agent.supabase", "refresh_token", refreshToken ) &&
        m_keychain.GetPassword( "kicad.agent.supabase", "user_email", userEmail ) &&
        m_keychain.GetPassword( "kicad.agent.supabase", "token_expiry", expiryStr ) )
    {
        m_keychain.GetPassword( "kicad.agent.supabase", "first_name", firstName );
        m_keychain.GetPassword( "kicad.agent.supabase", "avatar_url", avatarUrl );

        m_accessToken = accessToken;
        m_refreshToken = refreshToken;
        m_userEmail = userEmail;
        m_firstName = firstName;
        m_avatarUrl = avatarUrl;
        
        try
        {
            m_tokenExpiry = std::stoll( expiryStr );
            wxLogTrace( "Agent", "Loaded session for %s", m_userEmail.c_str() );
        }
        catch( ... )
        {
            m_tokenExpiry = 0;
        }
    }
}

void AGENT_AUTH::SaveSession()
{
    m_keychain.SetPassword( "kicad.agent.supabase", "access_token", m_accessToken );
    m_keychain.SetPassword( "kicad.agent.supabase", "refresh_token", m_refreshToken );
    m_keychain.SetPassword( "kicad.agent.supabase", "user_email", m_userEmail );
    m_keychain.SetPassword( "kicad.agent.supabase", "first_name", m_firstName );
    m_keychain.SetPassword( "kicad.agent.supabase", "avatar_url", m_avatarUrl );
    m_keychain.SetPassword( "kicad.agent.supabase", "token_expiry", std::to_string( m_tokenExpiry ) );
    wxLogTrace( "Agent", "Saved session to keychain" );
}

void AGENT_AUTH::ClearSession()
{
    m_keychain.DeletePassword( "kicad.agent.supabase", "access_token" );
    m_keychain.DeletePassword( "kicad.agent.supabase", "refresh_token" );
    m_keychain.DeletePassword( "kicad.agent.supabase", "user_email" );
    m_keychain.DeletePassword( "kicad.agent.supabase", "first_name" );
    m_keychain.DeletePassword( "kicad.agent.supabase", "avatar_url" );
    m_keychain.DeletePassword( "kicad.agent.supabase", "token_expiry" );
    wxLogTrace( "Agent", "Cleared session from keychain" );
}

bool AGENT_AUTH::IsTokenExpired()
{
    // Consider expired if within 5 minutes of expiry
    return ( wxDateTime::GetTimeNow() + 300 ) >= m_tokenExpiry;
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
