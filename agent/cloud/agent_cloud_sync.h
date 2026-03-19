#ifndef AGENT_CLOUD_SYNC_H
#define AGENT_CLOUD_SYNC_H

#include <string>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

class AGENT_AUTH;

/**
 * Syncs chat logs and application log files with Supabase Storage.
 *
 * Storage layout:
 *   user-data/{email}/chats/{conversation_id}.json
 *   user-data/{email}/logs/{log_filename}.log
 *
 * Uploads are fire-and-forget on a background thread.
 * Downloads pull remote-only chats to local storage on startup.
 */
class AGENT_CLOUD_SYNC
{
public:
    AGENT_CLOUD_SYNC();
    ~AGENT_CLOUD_SYNC();

    /**
     * Configure the Supabase connection for storage uploads.
     * @param aSupabaseUrl  Supabase project URL (e.g., https://xxxxx.supabase.co)
     * @param aAnonKey      Supabase anon/public key
     */
    void Configure( const std::string& aSupabaseUrl, const std::string& aAnonKey );

    /**
     * Set the auth provider (for access tokens and user email).
     */
    void SetAuth( AGENT_AUTH* aAuth );

    /**
     * Upload a single chat conversation to cloud storage (async, background thread).
     * @param aConversationId  The conversation ID (used as filename)
     * @param aJsonContent     The full chat JSON content to upload
     */
    void UploadChat( const std::string& aConversationId, const std::string& aJsonContent );

    /**
     * Upload a single log file to cloud storage (async, background thread).
     * @param aLogFilePath  Full local path to the log file
     */
    void UploadLog( const std::string& aLogFilePath );

    /**
     * Upload all unsynced chats and recent log files, then download
     * any remote-only chats to local storage.
     * Runs on a background thread. Safe to call from any thread.
     */
    void SyncAll();

    /**
     * Download chats from cloud that are missing locally or larger in the cloud.
     * Runs synchronously on the calling thread.
     */
    void DownloadChats();

    /**
     * Get the filename of the most recently modified agent log file.
     * @return Log filename (e.g., "agent-2024-01-15T10-30-00.log") or empty string
     */
    std::string GetCurrentLogFilename();

    /**
     * Get the storage path prefix for the current user (the user's email).
     * @return User email or empty string if not authenticated
     */
    std::string GetUserEmail();

private:
    /**
     * Upload content to Supabase Storage (blocking, runs on calling thread).
     * @param aStoragePath  Path within the bucket (e.g., "email/chats/id.json")
     * @param aContent      File content to upload
     * @return true on success
     */
    bool UploadToStorage( const std::string& aStoragePath, const std::string& aContent );
    bool UploadToStorageWithToken( const std::string& aStoragePath,
                                    const std::string& aContent,
                                    const std::string& aAccessToken,
                                    const std::string& aSupabaseUrl,
                                    const std::string& aAnonKey );

    /**
     * Build the storage path prefix for the current user.
     * @return "{email}" or empty string if not authenticated
     */
    std::string GetUserPrefix();

    /**
     * Check if a file has already been uploaded (same size).
     */
    bool IsAlreadyUploaded( const std::string& aKey, size_t aSize );

    /**
     * Mark a file as uploaded in the local sync state.
     */
    void MarkUploaded( const std::string& aKey, size_t aSize );

    /**
     * Load sync state from disk.
     */
    void LoadSyncState();

    /**
     * Save sync state to disk.
     */
    void SaveSyncState();

    /**
     * Get the path to the sync state file.
     */
    std::string GetSyncStatePath();

    /**
     * Get the log directory path (~/Library/Logs/Zeo/).
     */
    std::string GetLogDir();

    /**
     * Get the chat history directory path.
     */
    std::string GetChatDir();

    /**
     * Read a file's contents into a string.
     */
    bool ReadFile( const std::string& aPath, std::string& aContent );

    /**
     * List remote chat files in the user's cloud storage folder.
     * @return JSON array of file objects from Supabase Storage list API.
     */
    nlohmann::json ListRemoteChats();
    nlohmann::json ListRemoteChatsWith( const std::string& aAccessToken,
                                         const std::string& aSupabaseUrl,
                                         const std::string& aAnonKey,
                                         const std::string& aPrefix );

    /**
     * Download a file from Supabase Storage (blocking).
     * @param aStoragePath  Path within the bucket
     * @param aContent      Output: downloaded file content
     * @return true on success
     */
    bool DownloadFromStorage( const std::string& aStoragePath, std::string& aContent );
    bool DownloadFromStorageWithToken( const std::string& aStoragePath,
                                        std::string& aContent,
                                        const std::string& aAccessToken,
                                        const std::string& aSupabaseUrl,
                                        const std::string& aAnonKey );
    void DownloadChatsWithToken( const std::string& aAccessToken,
                                  const std::string& aSupabaseUrl,
                                  const std::string& aAnonKey );

    AGENT_AUTH*        m_auth;
    std::mutex         m_authMutex;      // Protects m_auth access across threads
    std::string        m_supabaseUrl;
    std::string        m_anonKey;

    nlohmann::json     m_syncState;      // Tracks uploaded files { "chats": {}, "logs": {} }
    std::mutex         m_syncStateMutex; // Protects m_syncState reads/writes
    std::atomic<bool>  m_configured;

    // Snapshot auth credentials on the main thread for use by background threads.
    // This avoids accessing m_auth from detached threads (use-after-free risk).
    struct AuthSnapshot
    {
        std::string accessToken;
        std::string email;
        bool        valid = false;
    };
    AuthSnapshot SnapshotAuth();
};

#endif // AGENT_CLOUD_SYNC_H
