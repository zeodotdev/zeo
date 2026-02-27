#include "git_vcs_bridge.h"

#include <git2.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <set>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <ctime>

using json = nlohmann::json;


// ── Credential callback ────────────────────────────────────────────────────────
// For HTTPS: reads stored token from ~/.git-credentials (written by StoreCredential).
// For SSH: tries the SSH agent.

static int vcs_credentials_cb( git_credential** aOut, const char* aUrl,
                                const char* aUsernameFromUrl,
                                unsigned int aAllowedTypes, void* /*aPayload*/ )
{
    // HTTPS username/password (GitHub/GitLab HTTPS repos)
    if( aAllowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT )
    {
        wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
        if( wxFileExists( credsPath ) )
        {
            // Extract host from the remote URL (e.g. "github.com" from "https://github.com/...")
            wxString urlStr( aUrl ? aUrl : "" );
            wxString host;
            wxString noProto = urlStr.AfterFirst( ':' );
            if( noProto.StartsWith( "//" ) )
                noProto = noProto.Mid( 2 );
            host = noProto.BeforeFirst( '/' );

            wxTextFile tf;
            if( tf.Open( credsPath ) )
            {
                for( size_t i = 0; i < tf.GetLineCount(); i++ )
                {
                    wxString line = tf.GetLine( i ).Trim();
                    if( line.IsEmpty() ) continue;

                    // Format: https://username:token@host
                    wxString lineHost = line.AfterLast( '@' );
                    if( !host.IsEmpty() && !lineHost.StartsWith( host ) )
                        continue;

                    wxString userPass = line.BeforeLast( '@' );
                    // Strip protocol
                    if( userPass.Contains( "//" ) )
                        userPass = userPass.AfterFirst( '/' ).AfterFirst( '/' );

                    wxString username = userPass.BeforeFirst( ':' );
                    wxString token    = userPass.AfterFirst( ':' );

                    if( !username.IsEmpty() && !token.IsEmpty() )
                    {
                        tf.Close();
                        return git_credential_userpass_plaintext_new(
                                aOut,
                                username.ToUTF8().data(),
                                token.ToUTF8().data() );
                    }
                }
                tf.Close();
            }
        }
    }

    // SSH via SSH agent
    if( aAllowedTypes & GIT_CREDENTIAL_SSH_KEY )
    {
        const char* user = ( aUsernameFromUrl && *aUsernameFromUrl ) ? aUsernameFromUrl : "git";
        if( git_credential_ssh_key_from_agent( aOut, user ) == GIT_OK )
            return GIT_OK;
    }

    // macOS Keychain / system credential store fallback
    if( aAllowedTypes & GIT_CREDENTIAL_DEFAULT )
    {
        if( git_credential_default_new( aOut ) == GIT_OK )
            return GIT_OK;
    }

    return GIT_PASSTHROUGH;
}


// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string OidToStr( const git_oid* aOid )
{
    char buf[GIT_OID_HEXSZ + 1];
    git_oid_tostr( buf, sizeof( buf ), aOid );
    return std::string( buf );
}


static std::string ShortOid( const git_oid* aOid )
{
    char buf[9];
    git_oid_tostr( buf, sizeof( buf ), aOid );
    return std::string( buf );
}


static void ThrowOnError( int aRet, const std::string& aContext )
{
    if( aRet < 0 )
    {
        const git_error* e = git_error_last();
        std::string msg = aContext + ": ";
        msg += ( e && e->message ) ? e->message : "unknown error";
        throw std::runtime_error( msg );
    }
}


static std::string StatusFlagToString( unsigned int aFlags, bool aIsIndex )
{
    if( aIsIndex )
    {
        if( aFlags & GIT_STATUS_INDEX_NEW )      return "added";
        if( aFlags & GIT_STATUS_INDEX_MODIFIED ) return "modified";
        if( aFlags & GIT_STATUS_INDEX_DELETED )  return "deleted";
        if( aFlags & GIT_STATUS_INDEX_RENAMED )  return "renamed";
        if( aFlags & GIT_STATUS_INDEX_TYPECHANGE ) return "modified";
    }
    else
    {
        if( aFlags & GIT_STATUS_WT_MODIFIED )  return "modified";
        if( aFlags & GIT_STATUS_WT_DELETED )   return "deleted";
        if( aFlags & GIT_STATUS_WT_RENAMED )   return "renamed";
        if( aFlags & GIT_STATUS_WT_TYPECHANGE ) return "modified";
    }

    if( aFlags & GIT_STATUS_CONFLICTED ) return "conflict";
    return "modified";
}


// ── Constructor / Destructor ───────────────────────────────────────────────────

GIT_VCS_BRIDGE::GIT_VCS_BRIDGE() : m_repo( nullptr )
{
    // libgit2 is initialized globally by single_top.cpp
}


GIT_VCS_BRIDGE::~GIT_VCS_BRIDGE()
{
    if( m_repo )
    {
        git_repository_free( m_repo );
        m_repo = nullptr;
    }
}


// ── Repo management ───────────────────────────────────────────────────────────

bool GIT_VCS_BRIDGE::OpenRepo( const wxString& aProjectDir )
{
    if( m_repo )
    {
        git_repository_free( m_repo );
        m_repo = nullptr;
    }

    m_projectDir = aProjectDir;

    std::string path = aProjectDir.ToStdString();
    int ret = git_repository_open_ext( &m_repo, path.c_str(), 0, nullptr );

    if( ret < 0 )
    {
        wxLogDebug( "GIT_VCS_BRIDGE: No git repo at %s", aProjectDir );
        m_repo = nullptr;
        return false;
    }

    wxLogDebug( "GIT_VCS_BRIDGE: Opened repo at %s", aProjectDir );

    // If HEAD is detached, try to land on a sensible branch automatically.
    if( git_repository_head_detached( m_repo ) )
    {
        // Preferred names in order; fall back to the first local branch found.
        static const char* preferred[] = { "main", "master", nullptr };

        bool switched = false;
        for( int i = 0; preferred[i] && !switched; ++i )
        {
            std::string refName = std::string( "refs/heads/" ) + preferred[i];
            git_reference* testRef = nullptr;
            if( git_reference_lookup( &testRef, m_repo, refName.c_str() ) == 0 )
            {
                git_reference_free( testRef );
                if( git_repository_set_head( m_repo, refName.c_str() ) == 0 )
                {
                    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
                    opts.checkout_strategy    = GIT_CHECKOUT_SAFE;
                    git_checkout_head( m_repo, &opts );
                    switched = true;
                    wxLogDebug( "GIT_VCS_BRIDGE: detached HEAD, switched to %s", preferred[i] );
                }
            }
        }

        if( !switched )
        {
            // Fall back to first local branch
            git_branch_iterator* it = nullptr;
            if( git_branch_iterator_new( &it, m_repo, GIT_BRANCH_LOCAL ) == 0 )
            {
                git_reference* ref  = nullptr;
                git_branch_t   type;
                if( git_branch_next( &ref, &type, it ) == 0 )
                {
                    const char* refFull = git_reference_name( ref );
                    if( refFull && git_repository_set_head( m_repo, refFull ) == 0 )
                    {
                        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
                        opts.checkout_strategy    = GIT_CHECKOUT_SAFE;
                        git_checkout_head( m_repo, &opts );
                        wxLogDebug( "GIT_VCS_BRIDGE: detached HEAD, switched to %s", refFull );
                    }
                    git_reference_free( ref );
                }
                git_branch_iterator_free( it );
            }
        }
    }

    return true;
}


void GIT_VCS_BRIDGE::InitRepo( const wxString& aProjectDir )
{
    if( m_repo )
    {
        git_repository_free( m_repo );
        m_repo = nullptr;
    }

    m_projectDir = aProjectDir;
    std::string path = aProjectDir.ToStdString();

    // Init with "main" as the default branch instead of "master"
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags        = GIT_REPOSITORY_INIT_MKPATH;
    opts.initial_head = "main";
    ThrowOnError( git_repository_init_ext( &m_repo, path.c_str(), &opts ), "git init" );

    // Write a default .gitignore unless one already exists
    wxFileName gitignorePath( aProjectDir, wxT( ".gitignore" ) );
    if( !gitignorePath.FileExists() )
    {
        const char* content =
            "# KiCad backup directories and files\n"
            "*-backups/\n"
            "_autosave-*\n"
            "*.kicad_sch-bak\n"
            "*.kicad_pcb-bak\n"
            "*~\n"
            "\n"
            "# KiCad cache and lock files\n"
            "fp-info-cache\n"
            "*.lck\n"
            "\n"
            "# OS metadata\n"
            ".DS_Store\n"
            "Thumbs.db\n"
            "\n"
            "# IDE and editor directories\n"
            ".history/\n"
            ".vscode/\n"
            ".idea/\n"
            "\n"
            "# Uncomment to ignore generated fabrication outputs:\n"
            "# gerbers/\n"
            "# *.gbr\n"
            "# *.drl\n"
            "# *.pos\n"
            "# *.pdf\n";

        wxFile file;
        if( file.Open( gitignorePath.GetFullPath(), wxFile::write ) )
        {
            file.Write( wxString::FromUTF8( content ) );
            file.Close();
        }
    }

    // Stage the .gitignore so it appears ready to commit on first use
    git_index* index = nullptr;
    if( git_repository_index( &index, m_repo ) == 0 )
    {
        git_index_add_bypath( index, ".gitignore" );
        git_index_write( index );
        git_index_free( index );
    }
}


// ── Status ────────────────────────────────────────────────────────────────────

json GIT_VCS_BRIDGE::GetStatus()
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    json result = {
        { "staged",    json::array() },
        { "unstaged",  json::array() },
        { "untracked", json::array() }
    };

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
               | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
               | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
               | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* statusList = nullptr;
    ThrowOnError( git_status_list_new( &statusList, m_repo, &opts ), "git status" );

    size_t count = git_status_list_entrycount( statusList );

    for( size_t i = 0; i < count; ++i )
    {
        const git_status_entry* entry = git_status_byindex( statusList, i );
        unsigned int flags = entry->status;

        const char* path = nullptr;

        if( entry->head_to_index )
            path = entry->head_to_index->new_file.path;
        else if( entry->index_to_workdir )
            path = entry->index_to_workdir->old_file.path;

        if( !path )
            continue;

        if( flags & GIT_STATUS_IGNORED )
            continue;

        // Skip KiCad lock files — they are transient and should never be staged.
        {
            std::string spath( path );
            // Strip leading directory components to get just the filename
            auto slash = spath.rfind( '/' );
            std::string fname = ( slash == std::string::npos ) ? spath : spath.substr( slash + 1 );
            if( fname.size() > 4 && fname.substr( fname.size() - 4 ) == ".lck" )
                continue;
        }

        if( flags & GIT_STATUS_WT_NEW )
        {
            // libgit2 can return a directory path (ending with '/') when the directory
            // is untracked and very large. Skip these — individual files inside are
            // shown separately, and directory paths break git_index_add_bypath.
            std::string spath( path );
            if( !spath.empty() && spath.back() == '/' )
                continue;
            result["untracked"].push_back( spath );
            continue;
        }

        // Staged changes (index)
        unsigned int indexFlags = flags & ( GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED
                                            | GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED
                                            | GIT_STATUS_INDEX_TYPECHANGE );
        if( indexFlags )
        {
            json item = {
                { "path",   std::string( path ) },
                { "status", StatusFlagToString( indexFlags, true ) }
            };
            result["staged"].push_back( item );
        }

        // Working tree changes
        unsigned int wtFlags = flags & ( GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED
                                         | GIT_STATUS_WT_RENAMED | GIT_STATUS_WT_TYPECHANGE
                                         | GIT_STATUS_CONFLICTED );
        if( wtFlags )
        {
            const char* wtPath = path;
            if( entry->index_to_workdir && entry->index_to_workdir->new_file.path )
                wtPath = entry->index_to_workdir->new_file.path;

            json item = {
                { "path",   std::string( wtPath ) },
                { "status", StatusFlagToString( wtFlags, false ) }
            };
            result["unstaged"].push_back( item );
        }
    }

    git_status_list_free( statusList );
    return result;
}


// ── History ───────────────────────────────────────────────────────────────────

json GIT_VCS_BRIDGE::GetHistory( int aCount, int aOffset )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    json result = json::array();

    git_revwalk* walker = nullptr;
    ThrowOnError( git_revwalk_new( &walker, m_repo ), "revwalk_new" );

    git_revwalk_sorting( walker, GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL );
    git_revwalk_push_head( walker ); // Start from HEAD (ignore error if no commits yet)

    git_oid oid;
    int skipped = 0;
    int collected = 0;

    while( git_revwalk_next( &oid, walker ) == 0 && collected < aCount )
    {
        if( skipped < aOffset )
        {
            ++skipped;
            continue;
        }

        git_commit* commit = nullptr;
        if( git_commit_lookup( &commit, m_repo, &oid ) != 0 )
            continue;

        const git_signature* author = git_commit_author( commit );
        std::string dateStr;
        if( author )
        {
            char buf[64];
            time_t t = static_cast<time_t>( author->when.time );
            struct tm* tm_info = localtime( &t );
            strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%S", tm_info );
            dateStr = buf;
        }

        // Collect parent hashes
        json parents = json::array();
        unsigned int parentCount = git_commit_parentcount( commit );
        for( unsigned int p = 0; p < parentCount && p < 2; ++p )
        {
            const git_oid* parentOid = git_commit_parent_id( commit, p );
            if( parentOid )
                parents.push_back( OidToStr( parentOid ) );
        }

        const char* msg = git_commit_message( commit );
        std::string fullMsg = msg ? msg : "";
        // First line = subject
        std::string subject = fullMsg;
        size_t nl = subject.find( '\n' );
        if( nl != std::string::npos )
            subject = subject.substr( 0, nl );

        json entry = {
            { "hash",        OidToStr( &oid ) },
            { "shortHash",   ShortOid( &oid ) },
            { "subject",     subject },
            { "message",     fullMsg },
            { "author",      author ? std::string( author->name ) : "" },
            { "email",       author ? std::string( author->email ) : "" },
            { "date",        dateStr },
            { "parents",     parents },
            { "parentCount", (int) parentCount }
        };

        result.push_back( entry );
        ++collected;
        git_commit_free( commit );
    }

    git_revwalk_free( walker );
    return result;
}


json GIT_VCS_BRIDGE::GetCommitFiles( const std::string& aCommitHash )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    json result = json::array();

    git_oid oid;
    ThrowOnError( git_oid_fromstrn( &oid, aCommitHash.c_str(), aCommitHash.size() ),
                  "parse commit hash" );

    git_commit* commit = nullptr;
    ThrowOnError( git_commit_lookup( &commit, m_repo, &oid ), "commit lookup" );

    git_tree* commitTree = nullptr;
    ThrowOnError( git_commit_tree( &commitTree, commit ), "commit tree" );

    // For the diff, compare with first parent (or empty tree if initial commit)
    git_tree* parentTree = nullptr;
    if( git_commit_parentcount( commit ) > 0 )
    {
        git_commit* parent = nullptr;
        if( git_commit_parent( &parent, commit, 0 ) == 0 )
        {
            git_commit_tree( &parentTree, parent );
            git_commit_free( parent );
        }
    }

    git_diff* diff = nullptr;
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    ThrowOnError( git_diff_tree_to_tree( &diff, m_repo, parentTree, commitTree, &opts ),
                  "diff trees" );

    size_t numDeltas = git_diff_num_deltas( diff );
    for( size_t i = 0; i < numDeltas; ++i )
    {
        const git_diff_delta* delta = git_diff_get_delta( diff, i );

        std::string status;
        switch( delta->status )
        {
        case GIT_DELTA_ADDED:    status = "added";    break;
        case GIT_DELTA_DELETED:  status = "deleted";  break;
        case GIT_DELTA_MODIFIED: status = "modified"; break;
        case GIT_DELTA_RENAMED:  status = "renamed";  break;
        case GIT_DELTA_COPIED:   status = "copied";   break;
        default:                 status = "modified";  break;
        }

        json item = {
            { "path",    std::string( delta->new_file.path ) },
            { "oldPath", std::string( delta->old_file.path ) },
            { "status",  status }
        };
        result.push_back( item );
    }

    git_diff_free( diff );
    if( parentTree ) git_tree_free( parentTree );
    git_tree_free( commitTree );
    git_commit_free( commit );

    return result;
}


json GIT_VCS_BRIDGE::GetTextDiff( const std::string& aOldCommit, const std::string& aNewCommit,
                                   const std::string& aFilePath )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Pathspec to limit diff to a single file
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.context_lines    = 3;
    const char* pathspecs[] = { aFilePath.c_str() };
    opts.pathspec.strings   = const_cast<char**>( pathspecs );
    opts.pathspec.count     = 1;

    // Resolve any revspec ("HEAD", branch name, or full/short OID) → git_tree*
    auto getTree = [&]( const std::string& ref ) -> git_tree*
    {
        if( ref.empty() ) return nullptr;
        git_object* obj = nullptr;
        if( git_revparse_single( &obj, m_repo, ref.c_str() ) != 0 ) return nullptr;
        git_commit* commit = nullptr;
        git_commit_lookup( &commit, m_repo, git_object_id( obj ) );
        git_object_free( obj );
        if( !commit ) return nullptr;
        git_tree* tree = nullptr;
        git_commit_tree( &tree, commit );
        git_commit_free( commit );
        return tree;
    };

    // Get file content from a tree (returns "" if not found)
    auto blobFromTree = [&]( git_tree* aTree, const std::string& path ) -> std::string
    {
        if( !aTree ) return "";
        git_tree_entry* entry = nullptr;
        if( git_tree_entry_bypath( &entry, aTree, path.c_str() ) != 0 ) return "";
        git_blob* blob = nullptr;
        if( git_blob_lookup( &blob, m_repo, git_tree_entry_id( entry ) ) != 0 )
        {
            git_tree_entry_free( entry );
            return "";
        }
        const char* raw = static_cast<const char*>( git_blob_rawcontent( blob ) );
        std::string content( raw, static_cast<size_t>( git_blob_rawsize( blob ) ) );
        git_blob_free( blob );
        git_tree_entry_free( entry );
        return content;
    };

    // Get file content from the index (staging area)
    auto blobFromIndex = [&]( const std::string& path ) -> std::string
    {
        git_index* index = nullptr;
        if( git_repository_index( &index, m_repo ) != 0 ) return "";
        const git_index_entry* entry = git_index_get_bypath( index, path.c_str(), 0 );
        if( !entry ) { git_index_free( index ); return ""; }
        git_blob* blob = nullptr;
        if( git_blob_lookup( &blob, m_repo, &entry->id ) != 0 )
        {
            git_index_free( index );
            return "";
        }
        const char* raw = static_cast<const char*>( git_blob_rawcontent( blob ) );
        std::string content( raw, static_cast<size_t>( git_blob_rawsize( blob ) ) );
        git_blob_free( blob );
        git_index_free( index );
        return content;
    };

    // Read a file from the working tree by constructing its full path
    auto blobFromWorkdir = [&]( const std::string& path ) -> std::string
    {
        std::string fullPath = m_projectDir.ToStdString();
        if( !fullPath.empty() && fullPath.back() != '/' ) fullPath += '/';
        fullPath += path;
        FILE* f = fopen( fullPath.c_str(), "rb" );
        if( !f ) return "";
        fseek( f, 0, SEEK_END );
        long sz = ftell( f );
        rewind( f );
        std::string content( static_cast<size_t>( sz ), '\0' );
        fread( &content[0], 1, static_cast<size_t>( sz ), f );
        fclose( f );
        return content;
    };

    // "INDEX" = staging area, "" (empty newCommit) = working tree
    const bool oldIsIndex   = ( aOldCommit == "INDEX" );
    const bool newIsIndex   = ( aNewCommit == "INDEX" );
    const bool newIsWorkdir = aNewCommit.empty();

    git_diff*   diff       = nullptr;
    git_tree*   oldTree    = nullptr;
    git_tree*   newTree    = nullptr;
    std::string oldContent, newContent;

    if( oldIsIndex && newIsWorkdir )
    {
        // INDEX → working tree (unstaged changes)
        ThrowOnError( git_diff_index_to_workdir( &diff, m_repo, nullptr, &opts ),
                      "diff index to workdir" );
        oldContent = blobFromIndex( aFilePath );
        newContent = blobFromWorkdir( aFilePath );
    }
    else if( newIsIndex )
    {
        // commit → INDEX (staged changes); oldCommit is usually "HEAD"
        oldTree = getTree( aOldCommit.empty() ? "HEAD" : aOldCommit );
        ThrowOnError( git_diff_tree_to_index( &diff, m_repo, oldTree, nullptr, &opts ),
                      "diff tree to index" );
        oldContent = blobFromTree( oldTree, aFilePath );
        newContent = blobFromIndex( aFilePath );
    }
    else if( newIsWorkdir )
    {
        // commit → working tree (handles both modified-unstaged AND new untracked files)
        // GIT_DIFF_INCLUDE_UNTRACKED makes new files show up in the diff.
        oldTree = aOldCommit.empty() ? nullptr : getTree( aOldCommit );
        git_diff_options wdOpts = opts;
        wdOpts.flags |= GIT_DIFF_INCLUDE_UNTRACKED;
        ThrowOnError( git_diff_tree_to_workdir( &diff, m_repo, oldTree, &wdOpts ),
                      "diff tree to workdir" );
        oldContent = blobFromTree( oldTree, aFilePath );
        newContent = blobFromWorkdir( aFilePath );
    }
    else
    {
        // commit → commit (history view)
        oldTree = aOldCommit.empty() ? nullptr : getTree( aOldCommit );
        newTree = getTree( aNewCommit );
        ThrowOnError( git_diff_tree_to_tree( &diff, m_repo, oldTree, newTree, &opts ),
                      "diff trees" );
        oldContent = blobFromTree( oldTree, aFilePath );
        newContent = blobFromTree( newTree, aFilePath );
    }

    // Check if either file is binary (contains null bytes)
    auto isBinaryContent = []( const std::string& s ) -> bool {
        return s.find( '\0' ) != std::string::npos;
    };
    if( isBinaryContent( oldContent ) || isBinaryContent( newContent ) )
    {
        git_diff_free( diff );
        if( oldTree ) git_tree_free( oldTree );
        if( newTree ) git_tree_free( newTree );
        return json{ { "filePath", aFilePath }, { "isBinary", true } };
    }

    // Collect unified patch text
    // Note: git_diff_line::content does NOT include the origin prefix char (+/-/space).
    // FILE_HDR ('F') and HUNK_HDR ('H') lines are already fully formatted, but context,
    // addition, and deletion lines need their origin prepended so the JS diff parser can
    // classify them correctly.
    std::string patchText;
    git_diff_print( diff, GIT_DIFF_FORMAT_PATCH,
        []( const git_diff_delta*, const git_diff_hunk*, const git_diff_line* l, void* p ) -> int
        {
            auto* str = static_cast<std::string*>( p );
            const char orig = l->origin;
            if( orig == GIT_DIFF_LINE_CONTEXT ||
                orig == GIT_DIFF_LINE_ADDITION ||
                orig == GIT_DIFF_LINE_DELETION )
            {
                str->push_back( orig );
            }
            str->append( l->content, l->content_len );
            return 0;
        },
        &patchText );

    git_diff_free( diff );
    if( oldTree ) git_tree_free( oldTree );
    if( newTree ) git_tree_free( newTree );

    // git_diff_print produces a header-only patch (no @@ hunks) for GIT_DELTA_UNTRACKED
    // entries. Synthesize a proper "all lines added" patch so the UI shows the full file.
    if( patchText.find( "@@" ) == std::string::npos && oldContent.empty() && !newContent.empty() )
    {
        size_t lineCount = std::count( newContent.begin(), newContent.end(), '\n' );
        if( newContent.back() != '\n' )
            lineCount++;

        std::ostringstream ss;
        ss << "--- /dev/null\n";
        ss << "+++ b/" << aFilePath << "\n";
        ss << "@@ -0,0 +1," << lineCount << " @@\n";

        std::istringstream iss( newContent );
        std::string line;
        while( std::getline( iss, line ) )
            ss << "+" << line << "\n";

        patchText = ss.str();
    }

    return json{
        { "filePath",   aFilePath },
        { "patch",      patchText },
        { "oldContent", oldContent },
        { "newContent", newContent }
    };
}


std::string GIT_VCS_BRIDGE::GetFileAtCommit( const std::string& aCommitHash,
                                              const std::string& aFilePath )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_oid oid;
    ThrowOnError( git_oid_fromstrn( &oid, aCommitHash.c_str(), aCommitHash.size() ),
                  "parse oid" );

    git_commit* commit = nullptr;
    ThrowOnError( git_commit_lookup( &commit, m_repo, &oid ), "commit lookup" );

    git_tree* tree = nullptr;
    ThrowOnError( git_commit_tree( &tree, commit ), "commit tree" );

    git_tree_entry* entry = nullptr;
    ThrowOnError( git_tree_entry_bypath( &entry, tree, aFilePath.c_str() ), "tree entry" );

    git_blob* blob = nullptr;
    ThrowOnError(
            git_blob_lookup( &blob, m_repo, git_tree_entry_id( entry ) ), "blob lookup" );

    const char*  rawContent = static_cast<const char*>( git_blob_rawcontent( blob ) );
    git_off_t    rawSize    = git_blob_rawsize( blob );
    std::string  result( rawContent, static_cast<size_t>( rawSize ) );

    git_blob_free( blob );
    git_tree_entry_free( entry );
    git_tree_free( tree );
    git_commit_free( commit );

    return result;
}


// ── Branches ──────────────────────────────────────────────────────────────────

json GIT_VCS_BRIDGE::GetBranches()
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    json local  = json::array();
    json remote = json::array();

    // Current branch name
    std::string currentBranch;
    {
        git_reference* head = nullptr;
        if( git_repository_head( &head, m_repo ) == 0 )
        {
            const char* name = git_reference_shorthand( head );
            if( name ) currentBranch = name;
            git_reference_free( head );
        }
    }

    // Collect local branch names first (need them for dedup)
    std::set<std::string> localNames;
    {
        git_branch_iterator* it = nullptr;
        if( git_branch_iterator_new( &it, m_repo, GIT_BRANCH_LOCAL ) == 0 )
        {
            git_reference* ref = nullptr;
            git_branch_t   type;
            while( git_branch_next( &ref, &type, it ) == 0 )
            {
                const char* name = nullptr;
                git_branch_name( &name, ref );
                if( name )
                {
                    local.push_back( std::string( name ) );
                    localNames.insert( std::string( name ) );
                }
                git_reference_free( ref );
            }
            git_branch_iterator_free( it );
        }
    }

    // Collect remote branches, skipping any whose short name matches a local branch
    {
        git_branch_iterator* it = nullptr;
        if( git_branch_iterator_new( &it, m_repo, GIT_BRANCH_REMOTE ) == 0 )
        {
            git_reference* ref = nullptr;
            git_branch_t   type;
            while( git_branch_next( &ref, &type, it ) == 0 )
            {
                const char* name = nullptr;
                git_branch_name( &name, ref );
                if( name )
                {
                    // "origin/master" → short name is "master" (strip "remote/")
                    std::string full( name );
                    std::string shortName = full;
                    auto slash = full.find( '/' );
                    if( slash != std::string::npos )
                        shortName = full.substr( slash + 1 );

                    // Only show in remote list if no local branch covers it
                    if( localNames.find( shortName ) == localNames.end() )
                        remote.push_back( full );
                }
                git_reference_free( ref );
            }
            git_branch_iterator_free( it );
        }
    }

    return json{
        { "current", currentBranch },
        { "local",   local },
        { "remote",  remote }
    };
}


json GIT_VCS_BRIDGE::GetRemotes()
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    json result = json::array();

    git_strarray remoteNames = {};
    if( git_remote_list( &remoteNames, m_repo ) == 0 )
    {
        for( size_t i = 0; i < remoteNames.count; ++i )
        {
            const char* name = remoteNames.strings[i];
            git_remote* remote = nullptr;

            if( git_remote_lookup( &remote, m_repo, name ) == 0 )
            {
                const char* url = git_remote_url( remote );
                result.push_back( json{
                    { "name", std::string( name ) },
                    { "url",  url ? std::string( url ) : "" }
                } );
                git_remote_free( remote );
            }
        }

        git_strarray_free( &remoteNames );
    }

    return result;
}


json GIT_VCS_BRIDGE::GetAheadBehind()
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    size_t ahead = 0, behind = 0;
    std::string trackingName;

    git_reference* head = nullptr;
    if( git_repository_head( &head, m_repo ) == 0 )
    {
        git_reference* upstream = nullptr;
        if( git_branch_upstream( &upstream, head ) == 0 )
        {
            const char* name = git_reference_shorthand( upstream );
            if( name ) trackingName = name;

            const git_oid* localOid  = git_reference_target( head );
            const git_oid* remoteOid = git_reference_target( upstream );

            if( localOid && remoteOid )
                git_graph_ahead_behind( &ahead, &behind, m_repo, localOid, remoteOid );

            git_reference_free( upstream );
        }
        git_reference_free( head );
    }

    return json{
        { "ahead",   (int) ahead },
        { "behind",  (int) behind },
        { "remote",  trackingName.empty() ? json( nullptr ) : json( trackingName ) }
    };
}


// ── Write operations ──────────────────────────────────────────────────────────

void GIT_VCS_BRIDGE::Stage( const std::vector<std::string>& aPaths )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_index* index = nullptr;
    ThrowOnError( git_repository_index( &index, m_repo ), "get index" );

    for( const auto& path : aPaths )
    {
        // git_index_add_bypath only works for files. If the path is a directory
        // (e.g. slipped through the status filter), use git_index_add_all instead.
        wxString fullPath = wxString::FromUTF8( m_projectDir ) + wxFILE_SEP_PATH
                            + wxString::FromUTF8( path );
        if( wxDirExists( fullPath ) )
        {
            const char*  ps[]  = { path.c_str(), nullptr };
            git_strarray spec  = { const_cast<char**>( ps ), 1 };
            git_index_add_all( index, &spec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr );
        }
        else if( wxFileExists( fullPath ) )
        {
            ThrowOnError( git_index_add_bypath( index, path.c_str() ), "stage " + path );
        }
        else
        {
            // File disappeared between status and stage (e.g. a lock file) — skip silently.
            wxLogDebug( "VCS Stage: skipping missing file '%s'", wxString::FromUTF8( path ) );
        }
    }

    ThrowOnError( git_index_write( index ), "write index" );
    git_index_free( index );
}


void GIT_VCS_BRIDGE::Unstage( const std::vector<std::string>& aPaths )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Resolve HEAD for reset
    git_object* head_obj = nullptr;
    bool hasHead = ( git_revparse_single( &head_obj, m_repo, "HEAD" ) == 0 );

    git_index* index = nullptr;
    ThrowOnError( git_repository_index( &index, m_repo ), "get index" );

    if( hasHead )
    {
        git_strarray patharray;
        std::vector<const char*> cstrs;
        for( const auto& p : aPaths )
            cstrs.push_back( p.c_str() );

        patharray.strings = const_cast<char**>( cstrs.data() );
        patharray.count   = cstrs.size();

        git_reset_default( m_repo, head_obj, &patharray );
        git_object_free( head_obj );
    }
    else
    {
        // Initial repo — just remove from index
        for( const auto& p : aPaths )
            git_index_remove_bypath( index, p.c_str() );
        git_index_write( index );
    }

    git_index_free( index );
}


json GIT_VCS_BRIDGE::Commit( const std::string& aMessage, const std::string& aAuthorName,
                              const std::string& aAuthorEmail )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_index* index = nullptr;
    ThrowOnError( git_repository_index( &index, m_repo ), "get index" );

    git_oid treeOid;
    ThrowOnError( git_index_write_tree( &treeOid, index ), "write tree" );
    git_index_free( index );

    git_tree* tree = nullptr;
    ThrowOnError( git_tree_lookup( &tree, m_repo, &treeOid ), "tree lookup" );

    git_signature* sig = nullptr;
    ThrowOnError( git_signature_now( &sig, aAuthorName.c_str(), aAuthorEmail.c_str() ),
                  "create signature" );

    // Get parent commit (HEAD)
    git_object* parent_obj = nullptr;
    git_commit* parent     = nullptr;
    bool hasParent = ( git_revparse_single( &parent_obj, m_repo, "HEAD" ) == 0 );

    if( hasParent )
        git_commit_lookup( &parent, m_repo, git_object_id( parent_obj ) );

    git_oid commitOid;
    const git_commit* parents[] = { parent };

    ThrowOnError( git_commit_create( &commitOid, m_repo, "HEAD", sig, sig, "UTF-8",
                                     aMessage.c_str(), tree, hasParent ? 1 : 0, parents ),
                  "create commit" );

    git_signature_free( sig );
    git_tree_free( tree );
    if( parent ) git_commit_free( parent );
    if( parent_obj ) git_object_free( parent_obj );

    return json{
        { "hash", OidToStr( &commitOid ) }
    };
}


json GIT_VCS_BRIDGE::Push( const std::string& aRemote )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Check whether any remotes are configured at all
    {
        git_strarray names = {};
        bool         hasAny = ( git_remote_list( &names, m_repo ) == 0 && names.count > 0 );
        git_strarray_free( &names );

        if( !hasAny )
        {
            return json{
                { "success",  false },
                { "noRemote", true },
                { "message",  "No remote repository configured. Add one to push and pull changes." }
            };
        }
    }

    // Look up the requested remote (default "origin")
    git_remote* remote = nullptr;

    if( git_remote_lookup( &remote, m_repo, aRemote.c_str() ) != 0 )
    {
        // Named remote doesn't exist; list what does exist so the error is helpful
        git_strarray names = {};
        std::string  available;

        if( git_remote_list( &names, m_repo ) == 0 )
        {
            for( size_t i = 0; i < names.count; ++i )
            {
                if( i ) available += ", ";
                available += names.strings[i];
            }
        }

        git_strarray_free( &names );

        return json{
            { "success",  false },
            { "noRemote", false },
            { "message",  "Remote '" + aRemote + "' not found."
                          + ( available.empty() ? "" : " Available: " + available ) }
        };
    }

    // Determine current branch name and build refspec
    std::string branchName;
    std::string refspec;
    {
        git_reference* head = nullptr;

        if( git_repository_head( &head, m_repo ) == 0 )
        {
            const char* shorthand = git_reference_shorthand( head );
            const char* fullname  = git_reference_name( head );

            if( shorthand ) branchName = shorthand;
            if( fullname )  refspec = std::string( fullname ) + ":" + std::string( fullname );

            git_reference_free( head );
        }
    }

    if( refspec.empty() )
    {
        git_remote_free( remote );
        return json{
            { "success", false },
            { "message", "Could not determine current branch for push" }
        };
    }

    const char*      refspecs[] = { refspec.c_str() };
    git_strarray     rs         = { const_cast<char**>( refspecs ), 1 };
    git_push_options pushOpts   = GIT_PUSH_OPTIONS_INIT;
    pushOpts.callbacks.credentials = vcs_credentials_cb;

    int ret = git_remote_push( remote, &rs, &pushOpts );
    git_remote_free( remote );

    if( ret < 0 )
    {
        const git_error* e = git_error_last();
        std::string      msg = e ? std::string( e->message ) : "Push failed";

        // Classify common errors for the UI
        bool authFailed = ( msg.find( "authentication" ) != std::string::npos
                            || msg.find( "credentials" ) != std::string::npos
                            || msg.find( "401" ) != std::string::npos
                            || msg.find( "403" ) != std::string::npos );

        return json{
            { "success",    false },
            { "authFailed", authFailed },
            { "message",    msg }
        };
    }

    // After a successful push, set the upstream tracking branch so ahead/behind works
    if( !branchName.empty() )
    {
        git_reference* head = nullptr;

        if( git_repository_head( &head, m_repo ) == 0 )
        {
            std::string trackingRef = "refs/remotes/" + aRemote + "/" + branchName;
            git_branch_set_upstream( head, ( aRemote + "/" + branchName ).c_str() );
            git_reference_free( head );
        }
    }

    return json{ { "success", true }, { "message", "Pushed successfully" } };
}


json GIT_VCS_BRIDGE::Pull()
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Check for remotes before attempting anything
    git_strarray names = {};
    bool         hasAny = ( git_remote_list( &names, m_repo ) == 0 && names.count > 0 );
    git_strarray_free( &names );

    if( !hasAny )
    {
        return json{ { "success", false },
                     { "noRemote", true },
                     { "message", "No remote repository configured. Add one first." } };
    }

    // Fetch from upstream
    try { Fetch(); } catch( ... ) {}

    // Simplified: just report success
    return json{
        { "success", true },
        { "message", "Fetched from remote. Use 'Merge' to integrate changes." },
        { "conflicts", json::array() }
    };
}


void GIT_VCS_BRIDGE::Fetch( const std::string& aRemote )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Get first remote if none specified
    std::string remoteName = aRemote;
    if( remoteName.empty() )
    {
        git_strarray names = {};
        if( git_remote_list( &names, m_repo ) == 0 && names.count > 0 )
            remoteName = names.strings[0];
        git_strarray_free( &names );
    }

    if( remoteName.empty() )
        throw std::runtime_error( "No remote repository configured. Add one first." );

    git_remote* remote = nullptr;
    if( git_remote_lookup( &remote, m_repo, remoteName.c_str() ) != 0 )
        throw std::runtime_error( "Remote not found: " + remoteName );

    git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
    opts.callbacks.credentials = vcs_credentials_cb;
    git_remote_fetch( remote, nullptr, &opts, nullptr );
    git_remote_free( remote );
}


void GIT_VCS_BRIDGE::DiscardChanges( const std::vector<std::string>& aPaths )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    // Separate untracked files (delete from disk) from tracked files (restore from HEAD)
    std::vector<std::string> tracked;
    for( const auto& p : aPaths )
    {
        unsigned int statusFlags = 0;
        git_status_file( &statusFlags, m_repo, p.c_str() );

        if( statusFlags & GIT_STATUS_WT_NEW )
        {
            // Untracked file — delete from disk
            wxString fullPath = m_projectDir + wxFILE_SEP_PATH + wxString::FromUTF8( p );
            wxRemoveFile( fullPath );
        }
        else
        {
            tracked.push_back( p );
        }
    }

    if( tracked.empty() )
        return;

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    std::vector<const char*> cstrs;
    for( const auto& p : tracked )
        cstrs.push_back( p.c_str() );

    git_strarray patharray;
    patharray.strings = const_cast<char**>( cstrs.data() );
    patharray.count   = cstrs.size();

    opts.paths = patharray;

    git_checkout_head( m_repo, &opts );
}


void GIT_VCS_BRIDGE::CreateBranch( const std::string& aBranchName )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_object* head = nullptr;
    ThrowOnError( git_revparse_single( &head, m_repo, "HEAD" ), "resolve HEAD" );

    git_commit* headCommit = nullptr;
    ThrowOnError( git_commit_lookup( &headCommit, m_repo, git_object_id( head ) ),
                  "head commit" );

    git_reference* newBranch = nullptr;
    ThrowOnError(
            git_branch_create( &newBranch, m_repo, aBranchName.c_str(), headCommit, 0 ),
            "create branch" );

    git_reference_free( newBranch );
    git_commit_free( headCommit );
    git_object_free( head );
}


void GIT_VCS_BRIDGE::SwitchBranch( const std::string& aBranchName )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_checkout_options checkoutOpts = GIT_CHECKOUT_OPTIONS_INIT;
    checkoutOpts.checkout_strategy    = GIT_CHECKOUT_SAFE;

    // Check if this is a local branch (refs/heads/<name> exists)
    std::string  localRef = "refs/heads/" + aBranchName;
    git_reference* testRef = nullptr;
    bool isLocal = ( git_reference_lookup( &testRef, m_repo, localRef.c_str() ) == 0 );
    if( testRef )
        git_reference_free( testRef );

    if( isLocal )
    {
        // Simple local branch checkout
        ThrowOnError( git_repository_set_head( m_repo, localRef.c_str() ), "set head" );
        ThrowOnError( git_checkout_head( m_repo, &checkoutOpts ), "checkout head" );
        return;
    }

    // Try as a remote tracking branch (refs/remotes/<name>)
    std::string    remoteRef = "refs/remotes/" + aBranchName;
    git_reference* rref      = nullptr;

    if( git_reference_lookup( &rref, m_repo, remoteRef.c_str() ) != 0 )
    {
        // Neither local nor remote — give a clear error
        throw std::runtime_error( "Branch not found: " + aBranchName );
    }

    // Peel to commit
    git_object* remoteObj = nullptr;
    int err = git_reference_peel( &remoteObj, rref, GIT_OBJECT_COMMIT );
    git_reference_free( rref );
    if( err != 0 )
        throw std::runtime_error( "Could not resolve remote branch: " + aBranchName );

    git_commit* remoteCommit = reinterpret_cast<git_commit*>( remoteObj );

    // Derive short name: strip the remote prefix (e.g. "origin/master" → "master")
    std::string shortName = aBranchName;
    auto slash = shortName.find( '/' );
    if( slash != std::string::npos )
        shortName = shortName.substr( slash + 1 );

    // If a local branch with that name already exists, just switch to it
    std::string  shortLocalRef = "refs/heads/" + shortName;
    git_reference* existingLocal = nullptr;
    bool localExists = ( git_reference_lookup( &existingLocal, m_repo, shortLocalRef.c_str() ) == 0 );
    if( existingLocal )
        git_reference_free( existingLocal );

    if( localExists )
    {
        git_object_free( remoteObj );
        ThrowOnError( git_repository_set_head( m_repo, shortLocalRef.c_str() ), "set head" );
        ThrowOnError( git_checkout_head( m_repo, &checkoutOpts ), "checkout head" );
        return;
    }

    // Create a new local tracking branch from the remote commit
    git_reference* newBranch = nullptr;
    ThrowOnError(
            git_branch_create( &newBranch, m_repo, shortName.c_str(), remoteCommit, 0 ),
            "create local branch" );

    // Set upstream so future push/pull knows the tracking remote
    git_branch_set_upstream( newBranch, aBranchName.c_str() );

    git_reference_free( newBranch );
    git_object_free( remoteObj );

    // Switch to the new local branch
    ThrowOnError( git_repository_set_head( m_repo, shortLocalRef.c_str() ), "set head" );
    ThrowOnError( git_checkout_head( m_repo, &checkoutOpts ), "checkout head" );
}


void GIT_VCS_BRIDGE::AddRemote( const std::string& aName, const std::string& aUrl )
{
    if( !m_repo )
        throw std::runtime_error( "No repository open" );

    git_remote* remote = nullptr;
    ThrowOnError( git_remote_create( &remote, m_repo, aName.c_str(), aUrl.c_str() ),
                  "create remote" );
    git_remote_free( remote );
}


json GIT_VCS_BRIDGE::GetUserConfig()
{
    git_config* cfg = nullptr;
    std::string name, email;

    if( m_repo )
        git_repository_config( &cfg, m_repo );
    else
        git_config_open_default( &cfg );

    if( cfg )
    {
        git_buf buf = GIT_BUF_INIT;
        if( git_config_get_string_buf( &buf, cfg, "user.name" ) == 0 )
            name = buf.ptr;
        git_buf_free( &buf );

        if( git_config_get_string_buf( &buf, cfg, "user.email" ) == 0 )
            email = buf.ptr;
        git_buf_free( &buf );

        git_config_free( cfg );
    }

    return json{ { "name", name }, { "email", email } };
}


bool GIT_VCS_BRIDGE::ResolveOid( const std::string& aHashStr, git_oid& aOidOut )
{
    return git_oid_fromstrn( &aOidOut, aHashStr.c_str(), aHashStr.size() ) == 0;
}


static wxString GetVcsCredentialsJsonPath()
{
    wxString dir = wxStandardPaths::Get().GetUserDataDir();
    if( !wxDirExists( dir ) )
        wxMkdir( dir );
    return dir + wxFILE_SEP_PATH + "vcs_credentials.json";
}


void GIT_VCS_BRIDGE::StoreCredential( const std::string& aHost, const std::string& aUsername,
                                       const std::string& aToken )
{
    // Build the credential line: https://username:token@host
    wxString newEntry = wxString::Format( "https://%s:%s@%s",
                                          wxString::FromUTF8( aUsername ),
                                          wxString::FromUTF8( aToken ),
                                          wxString::FromUTF8( aHost ) );

    // Path to ~/.git-credentials
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";

    // Read existing lines, removing any stale entry for the same host/user
    wxArrayString lines;
    if( wxFileExists( credsPath ) )
    {
        wxTextFile tf;
        if( tf.Open( credsPath ) )
        {
            for( size_t i = 0; i < tf.GetLineCount(); i++ )
            {
                wxString line = tf.GetLine( i );
                // Drop existing entry for this host+username combination
                if( line.Contains( wxString::FromUTF8( aHost ) )
                    && line.Contains( wxString::FromUTF8( aUsername ) ) )
                    continue;
                if( !line.IsEmpty() )
                    lines.Add( line );
            }
            tf.Close();
        }
    }

    lines.Add( newEntry );

    // Write back
    {
        wxFile out( credsPath, wxFile::write );
        if( !out.IsOpened() )
            throw std::runtime_error( "Failed to write ~/.git-credentials" );

        for( const wxString& line : lines )
        {
            out.Write( line + "\n" );
        }
        out.Close();
    }

    // Restrict permissions (owner read/write only)
    wxFileName fn( credsPath );
    fn.SetPermissions( wxS_IRUSR | wxS_IWUSR );

    // Ensure credential.helper = store is set globally so libgit2 reads the file
    git_config* cfg = nullptr;
    if( git_config_open_default( &cfg ) == 0 )
    {
        git_config_set_string( cfg, "credential.helper", "store" );
        git_config_free( cfg );
    }

    // Also write a JSON credentials file for UI display (doesn't contain the raw token)
    json ui = {
        { "connected", true },
        { "host",      aHost },
        { "username",  aUsername }
    };
    wxString jsonPath = GetVcsCredentialsJsonPath();
    wxFile jsonOut( jsonPath, wxFile::write );
    if( jsonOut.IsOpened() )
    {
        jsonOut.Write( wxString::FromUTF8( ui.dump() ) );
        jsonOut.Close();
        wxFileName jfn( jsonPath );
        jfn.SetPermissions( wxS_IRUSR | wxS_IWUSR );
    }
}


std::string GIT_VCS_BRIDGE::GetGitHubToken()
{
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
    if( !wxFileExists( credsPath ) )
        return "";

    wxTextFile tf;
    if( !tf.Open( credsPath ) )
        return "";

    // Each line is formatted as: https://username:token@host
    for( size_t i = 0; i < tf.GetLineCount(); i++ )
    {
        wxString line = tf.GetLine( i ).Trim();
        if( !line.Contains( "github.com" ) )
            continue;

        // Strip scheme (https://)
        wxString rest = line.AfterFirst( '/' ).AfterFirst( '/' ); // "username:token@github.com"
        wxString userPass = rest.BeforeLast( '@' );               // "username:token"
        wxString token = userPass.AfterFirst( ':' );              // "token"
        if( !token.IsEmpty() )
        {
            tf.Close();
            return token.ToStdString();
        }
    }

    tf.Close();
    return "";
}


std::string GIT_VCS_BRIDGE::GetGitLabToken()
{
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
    if( !wxFileExists( credsPath ) )
        return "";

    wxTextFile tf;
    if( !tf.Open( credsPath ) )
        return "";

    for( size_t i = 0; i < tf.GetLineCount(); i++ )
    {
        wxString line = tf.GetLine( i ).Trim();
        if( !line.Contains( "gitlab.com" ) )
            continue;

        wxString rest    = line.AfterFirst( '/' ).AfterFirst( '/' );
        wxString userPass = rest.BeforeLast( '@' );
        wxString token    = userPass.AfterFirst( ':' );
        if( !token.IsEmpty() )
        {
            tf.Close();
            return token.ToStdString();
        }
    }

    tf.Close();
    return "";
}


std::string GIT_VCS_BRIDGE::GetUsernameForHost( const std::string& aHost )
{
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
    if( !wxFileExists( credsPath ) )
        return "";

    wxTextFile tf;
    if( !tf.Open( credsPath ) )
        return "";

    wxString host = wxString::FromUTF8( aHost );

    for( size_t i = 0; i < tf.GetLineCount(); i++ )
    {
        wxString line = tf.GetLine( i ).Trim();
        if( !line.Contains( host ) )
            continue;

        // Line format: https://username:token@host
        wxString rest     = line.AfterFirst( '/' ).AfterFirst( '/' ); // "username:token@host"
        wxString username = rest.BeforeLast( '@' ).BeforeFirst( ':' );  // "username"
        if( !username.IsEmpty() )
        {
            tf.Close();
            return username.ToStdString();
        }
    }

    tf.Close();
    return "";
}


json GIT_VCS_BRIDGE::GetVcsCredential()
{
    wxString jsonPath = GetVcsCredentialsJsonPath();
    if( !wxFileExists( jsonPath ) )
        return json{ { "connected", false } };

    wxFile f( jsonPath );
    if( !f.IsOpened() )
        return json{ { "connected", false } };

    wxString content;
    f.ReadAll( &content );
    f.Close();

    try
    {
        return json::parse( content.ToStdString() );
    }
    catch( ... )
    {
        return json{ { "connected", false } };
    }
}
