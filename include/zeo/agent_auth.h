#ifndef AGENT_AUTH_H
#define AGENT_AUTH_H

#include <string>
#include <functional>
#include <map>
#include <memory>

#ifndef __WXMAC__
class AUTH_CALLBACK_SERVER;
class wxEvtHandler;
#endif

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
    using AuthStateCallback = std::function<void()>;

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
     * Register a callback that fires whenever auth state changes
     * (sign-in, sign-out, token refresh).  Used by SESSION_MANAGER
     * to keep the launcher UI in sync regardless of which window
     * initiated the sign-in.
     */
    void SetAuthStateCallback( AuthStateCallback aCallback )
    {
        m_authStateCallback = std::move( aCallback );
    }

    /**
     * Start the OAuth flow by launching the browser to the auth web page.
     * @param aSource Optional source identifier (e.g., "agent") to track where sign-in originated
     * @param aAuthUrlOut If non-null, receives the auth URL (for fallback UI on Linux)
     * @return True if the OAuth flow was started
     */
    bool StartOAuthFlow( const std::string& aSource = "", std::string* aAuthUrlOut = nullptr );

    /**
     * Get the current access token.  Checks the running AGENT_AUTH instance first
     * (if one exists), then falls back to reading the session file from disk.
     * Returns empty string if no session, token expired, or file unreadable.
     * Does NOT attempt token refresh — caller should prompt user to sign in if empty.
     *
     * This is a lightweight static method that can be called from anywhere without
     * needing a configured AGENT_AUTH instance.
     */
    static std::string ReadAccessTokenFromDisk();

    /**
     * Find the supabase_config.json file.  Checks the source tree first
     * (for dev builds), then the installed data path (for AppImage/packages).
     * @return Full path to config file, or empty string if not found.
     */
    static std::string GetSupabaseConfigPath();

#ifndef __WXMAC__
    /**
     * Set the event handler that will receive EVT_AUTH_CALLBACK from the local
     * HTTP callback server (Linux only).
     */
    void SetCallbackHandler( wxEvtHandler* aHandler );
#endif

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
    static std::string GetSessionFilePath();
    bool               WriteSessionFile( const std::string& aData );
    static bool        ReadSessionFile( std::string& aData );

    // Check if access token is expired (includes 5-minute proactive refresh buffer)
    bool IsTokenExpired();

    // Check if access token is actually expired (no buffer — token is unusable)
    bool IsTokenHardExpired();

    // Parse query parameters from URL
    std::map<std::string, std::string> ParseQueryParams( const std::string& url );

    // URL decode a string
    std::string UrlDecode( const std::string& value );

    AuthStateCallback m_authStateCallback;

    // Static pointer to the active configured instance, so ReadAccessTokenFromDisk()
    // can return in-memory tokens even when the session file wasn't written.
    static AGENT_AUTH* s_activeInstance;

#ifndef __WXMAC__
    std::unique_ptr<AUTH_CALLBACK_SERVER> m_callbackServer;
    wxEvtHandler*                         m_callbackHandler = nullptr;
#endif
};

#endif // AGENT_AUTH_H
