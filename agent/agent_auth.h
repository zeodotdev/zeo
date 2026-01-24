#ifndef AGENT_AUTH_H
#define AGENT_AUTH_H

#include <string>
#include <functional>
#include <map>
#include "agent_keychain.h"

/**
 * Supabase authentication manager.
 * Handles token storage and session management.
 * Authentication flow is handled by the external auth web page.
 */
class AGENT_AUTH
{
public:
    AGENT_AUTH();
    ~AGENT_AUTH();

    /**
     * Configure the Supabase connection.
     * @param aProjectUrl Your Supabase project URL (e.g., https://xxxxx.supabase.co)
     * @param aAnonKey Your Supabase anon/public key
     */
    void Configure( const std::string& aProjectUrl, const std::string& aAnonKey );

    /**
     * Sign out the current user.
     */
    void SignOut();

    /**
     * Check if user is currently authenticated.
     * @return True if valid session exists
     */
    bool IsAuthenticated();

    /**
     * Get the current user's email.
     * @return Email address, or empty string if not authenticated
     */
    std::string GetUserEmail();
    std::string GetFirstName();
    std::string GetAvatarUrl();

    /**
     * Handle OAuth callback with tokens from auth web page.
     * @param aCallbackUrl Full callback URL with token parameters
     * @param aErrorMsg Output parameter for error message
     * @return True if successful
     */
    bool HandleOAuthCallback( const std::string& aCallbackUrl, std::string& aErrorMsg );

    /**
     * Refresh the access token using the refresh token.
     * Called automatically when access token expires.
     * @return True if successful
     */
    bool RefreshToken();

    /**
     * Reload session from keychain.
     * Call this when notified that auth state changed externally.
     */
    void LoadSession();

private:
    std::string m_projectUrl;
    std::string m_anonKey;
    std::string m_accessToken;
    std::string m_refreshToken;
    std::string m_userEmail;
    std::string m_firstName;
    std::string m_avatarUrl;
    long long   m_tokenExpiry; // Unix timestamp

    AGENT_KEYCHAIN m_keychain;

    // Save tokens to keychain
    void SaveSession();

    // Clear tokens from keychain
    void ClearSession();

    // Check if access token is expired
    bool IsTokenExpired();

    // Parse query parameters from URL
    std::map<std::string, std::string> ParseQueryParams( const std::string& url );
    
    // URL decode a string
    std::string UrlDecode( const std::string& value );
};

#endif // AGENT_AUTH_H
