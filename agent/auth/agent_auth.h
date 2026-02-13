#ifndef AGENT_AUTH_H
#define AGENT_AUTH_H

#include <string>
#include <functional>
#include <map>

/**
 * Supabase authentication manager.
 * Handles token storage and session management.
 * Authentication flow is handled by the external auth web page.
 *
 * Session data is stored in ~/Library/Application Support/kicad/agent_session.json
 * with 0600 permissions (owner read/write only). Security model matches
 * git-credential-store: file permissions are the boundary.
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

    /**
     * Get the current access token for API requests.
     * Refreshes if expired.
     * @return Access token, or empty string if not authenticated
     */
    std::string GetAccessToken();
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
     * Reload session from secure storage.
     * Call this when notified that auth state changed externally.
     */
    void LoadSession();

    /**
     * Start the OAuth flow by launching the browser to the auth web page.
     * @param aSource Optional source identifier (e.g., "agent") to track where sign-in originated
     * @return True if browser was launched successfully
     */
    bool StartOAuthFlow( const std::string& aSource = "" );

private:
    std::string m_projectUrl;
    std::string m_anonKey;
    std::string m_authWebUrl;
    std::string m_accessToken;
    std::string m_refreshToken;
    std::string m_userEmail;
    std::string m_firstName;
    std::string m_avatarUrl;
    long long   m_tokenExpiry; // Unix timestamp

    // Session persistence
    void SaveSession();
    void ClearSession();

    // Non-destructive reload: only updates in-memory state if disk has valid tokens
    bool TryReloadSession();

    // Secure file I/O (0600 permissions, atomic write)
    std::string GetSessionFilePath();
    bool        WriteSessionFile( const std::string& aData );
    bool        ReadSessionFile( std::string& aData );

    // Check if access token is expired (includes 5-minute proactive refresh buffer)
    bool IsTokenExpired();

    // Check if access token is actually expired (no buffer — token is unusable)
    bool IsTokenHardExpired();

    // Parse query parameters from URL
    std::map<std::string, std::string> ParseQueryParams( const std::string& url );

    // URL decode a string
    std::string UrlDecode( const std::string& value );
};

#endif // AGENT_AUTH_H
