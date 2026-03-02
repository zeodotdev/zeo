#ifndef GIT_VCS_BRIDGE_H
#define GIT_VCS_BRIDGE_H

#include <nlohmann/json.hpp>
#include <wx/string.h>
#include <git2.h>
#include <string>
#include <vector>

/**
 * Wraps libgit2 operations for the VCS app.
 * All methods return JSON-serializable results or throw std::runtime_error on failure.
 */
class GIT_VCS_BRIDGE
{
public:
    GIT_VCS_BRIDGE();
    ~GIT_VCS_BRIDGE();

    /**
     * Open (or create) the git repository for the given project directory.
     * Must be called before any other methods.
     * @return true if a repo was found/created.
     */
    bool OpenRepo( const wxString& aProjectDir );

    /**
     * Return true if a git repository is currently open.
     */
    bool HasRepo() const { return m_repo != nullptr; }

    // ── Status ───────────────────────────────────────────────────────────

    /**
     * Get working tree status.
     * @return JSON: { staged: [{path, status}], unstaged: [{path, status}], untracked: [path] }
     *         status is one of: "added", "modified", "deleted", "renamed", "conflict"
     */
    nlohmann::json GetStatus();

    // ── History ──────────────────────────────────────────────────────────

    /**
     * Get commit history for the current branch.
     * @param aCount  Maximum number of commits to return.
     * @param aOffset Number of commits to skip (for pagination).
     * @return JSON array of { hash, shortHash, message, author, email, date, parentHashes }
     */
    nlohmann::json GetHistory( int aCount = 50, int aOffset = 0 );

    /**
     * Get files changed in a specific commit.
     * @param aCommitHash Full or abbreviated commit OID hex string.
     * @return JSON array of { path, status } where status is "added"|"modified"|"deleted"|"renamed"
     */
    nlohmann::json GetCommitFiles( const std::string& aCommitHash );

    /**
     * Get unified text diff for a file between two commits.
     * Pass empty string for aOldCommit to diff against working tree.
     * @return JSON: { oldContent, newContent, hunks: [{header, lines: [{type, content}]}] }
     */
    nlohmann::json GetTextDiff( const std::string& aOldCommit, const std::string& aNewCommit,
                                const std::string& aFilePath );

    /**
     * Get file content at a specific commit as a UTF-8 string.
     */
    std::string GetFileAtCommit( const std::string& aCommitHash, const std::string& aFilePath );

    // ── Branches ─────────────────────────────────────────────────────────

    /**
     * Get all local and remote branches.
     * @return JSON: { current, local: [name], remote: [name] }
     */
    nlohmann::json GetBranches();

    /**
     * Get configured remotes.
     * @return JSON array of { name, url }
     */
    nlohmann::json GetRemotes();

    /**
     * Get counts of commits ahead/behind the remote tracking branch.
     * @return JSON: { ahead, behind, remote }  — remote is the tracking branch name or null
     */
    nlohmann::json GetAheadBehind();

    // ── Write operations ─────────────────────────────────────────────────

    /**
     * Stage files (add to index).
     */
    void Stage( const std::vector<std::string>& aPaths );

    /**
     * Unstage files (reset HEAD).
     */
    void Unstage( const std::vector<std::string>& aPaths );

    /**
     * Create a commit with the staged changes.
     * @return JSON: { hash, message }
     */
    nlohmann::json Commit( const std::string& aMessage, const std::string& aAuthorName,
                           const std::string& aAuthorEmail );

    /**
     * Push current branch to the given remote.
     * @return JSON: { success, message }
     */
    nlohmann::json Push( const std::string& aRemote = "origin" );

    /**
     * Pull (fetch + merge/rebase) from tracking remote.
     * @return JSON: { success, message, conflicts: [path] }
     */
    nlohmann::json Pull();

    /**
     * Fetch from remote without merging.
     */
    void Fetch( const std::string& aRemote = "origin" );

    /**
     * Discard working-tree changes for specified files (checkout HEAD).
     */
    void DiscardChanges( const std::vector<std::string>& aPaths );

    /**
     * Create a new branch from HEAD.
     */
    void CreateBranch( const std::string& aBranchName );

    /**
     * Switch to an existing branch.
     */
    void SwitchBranch( const std::string& aBranchName );

    /**
     * Add a remote.
     */
    void AddRemote( const std::string& aName, const std::string& aUrl );

    /**
     * Initialize a new git repository at the current project directory.
     */
    void InitRepo( const wxString& aProjectDir );

    /**
     * Get the git user.name and user.email from config.
     * @return JSON: { name, email }
     */
    nlohmann::json GetUserConfig();

    /**
     * Store HTTPS credentials for a host in ~/.git-credentials and in
     * ~/Library/Application Support/kicad/vcs_credentials.json for UI use.
     * Sets credential.helper = store in global git config.
     * Static so it can be called from session_manager without a bridge instance.
     */
    static void StoreCredential( const std::string& aHost, const std::string& aUsername,
                                 const std::string& aToken );

    /**
     * Read stored VCS credentials for UI display (not the raw token).
     * @return JSON: { connected: bool, host: str, username: str }
     */
    static nlohmann::json GetVcsCredential();
    static std::string GetGitHubToken();
    static std::string GetGitLabToken();

    /**
     * Get the stored username for a given host from ~/.git-credentials.
     * Returns empty string if no credential is found for the host.
     */
    static std::string GetUsernameForHost( const std::string& aHost );

    /**
     * Resolve a symbolic ref or abbreviated OID to a full 40-char hex SHA.
     * Handles "HEAD", branch names, and hex commit hashes.
     * Returns the input unchanged if resolution fails.
     */
    std::string ResolveRef( const std::string& aRef );

    /**
     * Get the project (working directory) path.
     */
    wxString GetProjectDir() const { return m_projectDir; }

private:
    /**
     * Look up a commit OID from a hash string (full or abbreviated).
     * Returns true if found and writes to aOidOut.
     */
    bool ResolveOid( const std::string& aHashStr, git_oid& aOidOut );

    git_repository* m_repo;
    wxString        m_projectDir;
};

#endif // GIT_VCS_BRIDGE_H
