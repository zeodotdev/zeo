#include "vcs_ipc_handler.h"
#include "vcs_frame.h"
#include "git_vcs_bridge.h"
#include "kicad_file_renderer.h"

#include <widgets/webview_panel.h>
#include <kiway.h>
#include <kiway_player.h>
#include <frame_type.h>
#include <mail_type.h>
#include <project.h>
#include <pgm_base.h>
#include <wx/app.h>
#include <wx/file.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/utils.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>

#include <memory>
#include <thread>

using json = nlohmann::json;


VCS_IPC_HANDLER::VCS_IPC_HANDLER( VCS_FRAME* aFrame, WEBVIEW_PANEL* aWebView ) :
        m_frame( aFrame ),
        m_webView( aWebView ),
        m_git( std::make_unique<GIT_VCS_BRIDGE>() )
{
    // Open the repo for the current project directory if one is loaded
    KIWAY&   kiway    = m_frame->Kiway();
    PROJECT& prj      = kiway.Prj();
    wxString prjPath  = prj.GetProjectPath();

    if( !prjPath.empty() )
    {
        m_git->OpenRepo( prjPath );
        m_frame->StartWatching( prjPath );
    }
}


VCS_IPC_HANDLER::~VCS_IPC_HANDLER() = default;


void VCS_IPC_HANDLER::OnMessage( const wxString& aMessage )
{
    json msg;

    try
    {
        msg = json::parse( aMessage.ToStdString() );
    }
    catch( const json::exception& e )
    {
        wxLogDebug( "VCS_IPC_HANDLER: Bad JSON: %s", e.what() );
        return;
    }

    if( !msg.contains( "action" ) )
        return;

    std::string action = msg["action"].get<std::string>();
    wxLogDebug( "VCS_IPC_HANDLER: action=%s", wxString( action ) );

    // If the project has changed since the last message, reopen the repo
    {
        KIWAY&   kiway   = m_frame->Kiway();
        PROJECT& prj     = kiway.Prj();
        wxString prjPath = prj.GetProjectPath();

        if( !prjPath.empty() && prjPath != m_git->GetProjectDir() )
        {
            m_git->OpenRepo( prjPath );
            m_frame->StartWatching( prjPath );
        }
    }

    try
    {
        if(      action == "get_status" )        HandleGetStatus( msg );
        else if( action == "get_history" )       HandleGetHistory( msg );
        else if( action == "get_commit_files" )  HandleGetCommitFiles( msg );
        else if( action == "get_text_diff" )     HandleGetTextDiff( msg );
        else if( action == "get_visual_diff" )   HandleGetVisualDiff( msg );
        else if( action == "get_branches" )      HandleGetBranches( msg );
        else if( action == "get_remotes" )       HandleGetRemotes( msg );
        else if( action == "stage" )             HandleStage( msg );
        else if( action == "unstage" )           HandleUnstage( msg );
        else if( action == "commit" )            HandleCommit( msg );
        else if( action == "push" )              HandlePush( msg );
        else if( action == "pull" )              HandlePull( msg );
        else if( action == "fetch" )             HandleFetch( msg );
        else if( action == "discard_changes" )   HandleDiscardChanges( msg );
        else if( action == "create_branch" )     HandleCreateBranch( msg );
        else if( action == "switch_branch" )     HandleSwitchBranch( msg );
        else if( action == "add_remote" )        HandleAddRemote( msg );
        else if( action == "init_repo" )         HandleInitRepo( msg );
        else if( action == "get_user_config" )   HandleGetUserConfig( msg );
        else if( action == "store_credential" )  HandleStoreCredential( msg );
        else if( action == "open_url" )          HandleOpenUrl( msg );
        else if( action == "connect_github" )     HandleConnectGitHub( msg );
        else if( action == "get_github_token" )   HandleGetGitHubToken( msg );
        else if( action == "disconnect_github" )  HandleDisconnectGitHub( msg );
        else if( action == "connect_gitlab" )     HandleConnectGitLab( msg );
        else if( action == "get_gitlab_token" )   HandleGetGitLabToken( msg );
        else if( action == "disconnect_gitlab" )  HandleDisconnectGitLab( msg );
        else if( action == "open_in_editor" )     HandleOpenInEditor( msg );
        else if( action == "console_log" )
        {
            std::string logMsg = msg.value( "message", "" );
            wxLogDebug( "VCS_JS: %s", wxString::FromUTF8( logMsg ) );
        }
        else
        {
            wxLogDebug( "VCS_IPC_HANDLER: Unknown action: %s", wxString( action ) );
        }
    }
    catch( const std::exception& e )
    {
        wxString reqId = msg.contains( "requestId" )
                ? wxString::FromUTF8( msg["requestId"].get<std::string>() )
                : wxString();

        wxLogError( "VCS_IPC_HANDLER: action=%s error: %s", wxString( action ), e.what() );
        SendError( reqId, wxString::FromUTF8( e.what() ) );
    }
}


// ── Git read handlers ─────────────────────────────────────────────────────────

void VCS_IPC_HANDLER::HandleGetStatus( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    if( !m_git->HasRepo() )
    {
        SendResponse( reqId, true, json{
            { "hasRepo",   false },
            { "staged",    json::array() },
            { "unstaged",  json::array() },
            { "untracked", json::array() }
        } );
        return;
    }

    json status  = m_git->GetStatus();
    json ahead   = m_git->GetAheadBehind();
    json remotes = m_git->GetRemotes();

    // Check stored credentials for both GitHub and GitLab directly from ~/.git-credentials
    std::string ghUser = GIT_VCS_BRIDGE::GetUsernameForHost( "github.com" );
    std::string glUser = GIT_VCS_BRIDGE::GetUsernameForHost( "gitlab.com" );

    status["hasRepo"]        = true;
    status["ahead"]          = ahead["ahead"];
    status["behind"]         = ahead["behind"];
    status["remote"]         = ahead["remote"];
    status["hasRemote"]      = !remotes.empty();
    status["remotes"]        = remotes;
    status["repoPath"]       = m_git->GetProjectDir().ToStdString();
    status["hasGitHubAuth"]  = !ghUser.empty();
    status["githubUsername"] = ghUser;
    status["hasGitLabAuth"]  = !glUser.empty();
    status["gitlabUsername"] = glUser;

    SendResponse( reqId, true, status );
}


void VCS_IPC_HANDLER::HandleGetHistory( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    int count  = aMsg.value( "count",  50 );
    int offset = aMsg.value( "offset",  0 );

    json history = m_git->GetHistory( count, offset );
    SendResponse( reqId, true, json{ { "commits", history } } );
}


void VCS_IPC_HANDLER::HandleGetCommitFiles( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string commitHash = aMsg.value( "hash", "" );
    if( commitHash.empty() )
    {
        SendError( reqId, "Missing 'hash' parameter" );
        return;
    }

    json files = m_git->GetCommitFiles( commitHash );
    SendResponse( reqId, true, json{ { "files", files } } );
}


void VCS_IPC_HANDLER::HandleGetTextDiff( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string oldCommit = aMsg.value( "oldCommit", "" );
    std::string newCommit = aMsg.value( "newCommit", "" );
    std::string filePath  = aMsg.value( "filePath",  "" );

    json diff = m_git->GetTextDiff( oldCommit, newCommit, filePath );
    SendResponse( reqId, true, diff );
}


void VCS_IPC_HANDLER::HandleGetVisualDiff( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string oldCommit  = aMsg.value( "oldCommit",  "" );
    std::string newCommit  = aMsg.value( "newCommit",  "" );
    std::string filePath   = aMsg.value( "filePath",   "" );
    bool        skipBefore = aMsg.value( "skipBefore", false );

    // Extract file contents from git for both revisions.
    // Resolve symbolic refs (e.g. "HEAD") to a real commit SHA first,
    // because GetFileAtCommit only accepts 40-char hex OIDs.
    std::string oldContent, newContent;

    try
    {
        // Skip loading the before content if the caller already has it cached.
        if( !skipBefore && !oldCommit.empty() && oldCommit != "INDEX" )
            oldContent = m_git->GetFileAtCommit(
                    m_git->ResolveRef( oldCommit ), filePath );
    }
    catch( ... ) {}

    try
    {
        if( newCommit.empty() )
        {
            // Read current working-tree version
            wxString fullPath = m_git->GetProjectDir() + wxFILE_SEP_PATH
                                + wxString::FromUTF8( filePath );
            wxFile f( fullPath, wxFile::read );
            if( f.IsOpened() )
            {
                wxFileOffset sz = f.Length();
                newContent.resize( static_cast<size_t>( sz ) );
                f.Read( &newContent[0], static_cast<size_t>( sz ) );
            }
        }
        else if( newCommit != "INDEX" )
        {
            newContent = m_git->GetFileAtCommit( newCommit, filePath );
        }
    }
    catch( ... ) {}

    // Run the renderer on a background thread so kicad-cli exports don't freeze the UI.
    // wxCallAfter posts the SendResponse back to the main thread when done.
    auto renderer = std::make_shared<KICAD_FILE_RENDERER>();

    std::thread( [this, reqId, renderer,
                  oldContent  = std::move( oldContent ),
                  newContent  = std::move( newContent ),
                  filePath,
                  skipBefore]() mutable
    {
        json result = renderer->GetVisualDiff( oldContent, newContent, filePath );

        // When skipBefore is set, we intentionally passed empty oldContent —
        // override fileIsNew so the JS side doesn't hide the before pane.
        if( skipBefore )
        {
            result["fileIsNew"]     = false;
            result["beforeSkipped"] = true;
        }

        bool success = result.value( "success", false );

        wxTheApp->CallAfter( [this, reqId, result = std::move( result ), success]()
        {
            SendResponse( reqId, success, result );
        } );
    } ).detach();
}


void VCS_IPC_HANDLER::HandleGetUserConfig( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    json cfg = m_git->GetUserConfig();
    SendResponse( reqId, true, cfg );
}


void VCS_IPC_HANDLER::HandleGetBranches( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    json branches = m_git->GetBranches();
    SendResponse( reqId, true, branches );
}


void VCS_IPC_HANDLER::HandleGetRemotes( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    json remotes = m_git->GetRemotes();
    SendResponse( reqId, true, json{ { "remotes", remotes } } );
}


// ── Git write handlers ────────────────────────────────────────────────────────

void VCS_IPC_HANDLER::HandleStage( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::vector<std::string> paths;
    if( aMsg.contains( "paths" ) && aMsg["paths"].is_array() )
    {
        for( const auto& p : aMsg["paths"] )
            paths.push_back( p.get<std::string>() );
    }

    m_git->Stage( paths );
    SendResponse( reqId, true, json{ { "staged", paths } } );
}


void VCS_IPC_HANDLER::HandleUnstage( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::vector<std::string> paths;
    if( aMsg.contains( "paths" ) && aMsg["paths"].is_array() )
    {
        for( const auto& p : aMsg["paths"] )
            paths.push_back( p.get<std::string>() );
    }

    m_git->Unstage( paths );
    SendResponse( reqId, true, json{ { "unstaged", paths } } );
}


void VCS_IPC_HANDLER::HandleCommit( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string message     = aMsg.value( "message",     "" );
    std::string authorName  = aMsg.value( "authorName",  "" );
    std::string authorEmail = aMsg.value( "authorEmail", "" );

    if( message.empty() )
    {
        SendError( reqId, "Commit message cannot be empty" );
        return;
    }

    // Fall back to git config if author not provided
    if( authorName.empty() || authorEmail.empty() )
    {
        json cfg = m_git->GetUserConfig();
        if( authorName.empty() )  authorName  = cfg.value( "name",  "Unknown" );
        if( authorEmail.empty() ) authorEmail = cfg.value( "email", "unknown@unknown.com" );
    }

    json result = m_git->Commit( message, authorName, authorEmail );
    SendResponse( reqId, true, result );
}


void VCS_IPC_HANDLER::HandlePush( const json& aMsg )
{
    wxString    reqId  = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();
    std::string remote = aMsg.value( "remote", "origin" );

    std::thread( [this, reqId, remote]()
    {
        json result;
        bool success = false;
        try
        {
            result  = m_git->Push( remote );
            success = result.value( "success", false );
        }
        catch( const std::exception& e )
        {
            result  = json{ { "success", false }, { "message", e.what() } };
        }
        wxTheApp->CallAfter( [this, reqId, result = std::move( result ), success]()
        {
            SendResponse( reqId, success, result );
        } );
    } ).detach();
}


void VCS_IPC_HANDLER::HandlePull( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::thread( [this, reqId]()
    {
        json result;
        bool success = false;
        try
        {
            result  = m_git->Pull();
            success = result.value( "success", false );
        }
        catch( const std::exception& e )
        {
            result  = json{ { "success", false }, { "message", e.what() } };
        }
        wxTheApp->CallAfter( [this, reqId, result = std::move( result ), success]()
        {
            SendResponse( reqId, success, result );
        } );
    } ).detach();
}


void VCS_IPC_HANDLER::HandleFetch( const json& aMsg )
{
    wxString    reqId  = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();
    std::string remote = aMsg.value( "remote", "" );

    std::thread( [this, reqId, remote]()
    {
        json result;
        bool success = false;
        try
        {
            m_git->Fetch( remote );
            result  = json{ { "message", "Fetch complete" } };
            success = true;
        }
        catch( const std::exception& e )
        {
            std::string msg     = e.what();
            bool        noRemote = ( msg.find( "No remote" ) != std::string::npos );
            result = json{ { "success", false }, { "message", msg } };
            if( noRemote )
                result["noRemote"] = true;
        }
        wxTheApp->CallAfter( [this, reqId, result = std::move( result ), success]()
        {
            SendResponse( reqId, success, result );
        } );
    } ).detach();
}


void VCS_IPC_HANDLER::HandleDiscardChanges( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::vector<std::string> paths;
    if( aMsg.contains( "paths" ) && aMsg["paths"].is_array() )
    {
        for( const auto& p : aMsg["paths"] )
            paths.push_back( p.get<std::string>() );
    }

    m_git->DiscardChanges( paths );
    SendResponse( reqId, true, json{ { "discarded", paths } } );
}


void VCS_IPC_HANDLER::HandleCreateBranch( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string name = aMsg.value( "name", "" );
    if( name.empty() )
    {
        SendError( reqId, "Branch name is required" );
        return;
    }

    m_git->CreateBranch( name );
    SendResponse( reqId, true, json{ { "branch", name } } );
}


void VCS_IPC_HANDLER::HandleSwitchBranch( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string name = aMsg.value( "name", "" );
    if( name.empty() )
    {
        SendError( reqId, "Branch name is required" );
        return;
    }

    m_git->SwitchBranch( name );
    SendResponse( reqId, true, json{ { "branch", name } } );
}


void VCS_IPC_HANDLER::HandleAddRemote( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string name = aMsg.value( "name", "" );
    std::string url  = aMsg.value( "url",  "" );

    if( name.empty() || url.empty() )
    {
        SendError( reqId, "Remote name and URL are required" );
        return;
    }

    m_git->AddRemote( name, url );
    SendResponse( reqId, true, json{ { "name", name }, { "url", url } } );
}


void VCS_IPC_HANDLER::HandleInitRepo( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    KIWAY&   kiway   = m_frame->Kiway();
    PROJECT& prj     = kiway.Prj();
    wxString prjPath = prj.GetProjectPath();

    m_git->InitRepo( prjPath );
    SendResponse( reqId, true, json{ { "path", prjPath.ToStdString() } } );
}


void VCS_IPC_HANDLER::HandleStoreCredential( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string host     = aMsg.value( "host",     "" );
    std::string username = aMsg.value( "username", "" );
    std::string token    = aMsg.value( "token",    "" );

    if( host.empty() || username.empty() || token.empty() )
    {
        SendError( reqId, "host, username, and token are required" );
        return;
    }

    m_git->StoreCredential( host, username, token );
    SendResponse( reqId, true, json{ { "host", host }, { "username", username } } );
}


void VCS_IPC_HANDLER::HandleOpenUrl( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string url = aMsg.value( "url", "" );
    if( url.empty() )
    {
        SendError( reqId, "url is required" );
        return;
    }

    wxLaunchDefaultBrowser( wxString::FromUTF8( url ) );
    SendResponse( reqId, true, json::object() );
}


void VCS_IPC_HANDLER::HandleConnectGitHub( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    // Open the VCS-specific GitHub OAuth page in the user's default browser.
    // The page requests 'repo' scope and redirects back via kicad-agent://vcs-callback.
    wxLaunchDefaultBrowser( "https://zeo.dev/auth/connect-vcs" );
    SendResponse( reqId, true, json::object() );
}


void VCS_IPC_HANDLER::HandleGetGitHubToken( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string token = GIT_VCS_BRIDGE::GetGitHubToken();
    SendResponse( reqId, true, json{ { "token", token } } );
}


void VCS_IPC_HANDLER::HandleDisconnectGitHub( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    // Remove github.com entry from ~/.git-credentials
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
    if( wxFileExists( credsPath ) )
    {
        wxTextFile tf;
        if( tf.Open( credsPath ) )
        {
            wxArrayString kept;
            for( size_t i = 0; i < tf.GetLineCount(); i++ )
            {
                wxString line = tf.GetLine( i ).Trim();
                if( !line.IsEmpty() && !line.Contains( "github.com" ) )
                    kept.Add( line );
            }
            tf.Close();

            wxFile out( credsPath, wxFile::write );
            if( out.IsOpened() )
            {
                for( const wxString& line : kept )
                    out.Write( line + "\n" );
                out.Close();
            }
        }
    }

    // Clear vcs_credentials.json
    wxString jsonPath = wxStandardPaths::Get().GetUserDataDir()
                        + wxFILE_SEP_PATH + "vcs_credentials.json";
    wxFile jf( jsonPath, wxFile::write );
    if( jf.IsOpened() )
    {
        jf.Write( wxString::FromUTF8( json{ { "connected", false } }.dump() ) );
        jf.Close();
    }

    SendResponse( reqId, true, json::object() );
}


// ── Helpers ───────────────────────────────────────────────────────────────────

void VCS_IPC_HANDLER::HandleConnectGitLab( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    wxLaunchDefaultBrowser( "https://zeo.dev/auth/connect-gitlab" );
    SendResponse( reqId, true, json::object() );
}


void VCS_IPC_HANDLER::HandleGetGitLabToken( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string token = GIT_VCS_BRIDGE::GetGitLabToken();
    SendResponse( reqId, true, json{ { "token", token } } );
}


void VCS_IPC_HANDLER::HandleDisconnectGitLab( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    // Remove gitlab.com entry from ~/.git-credentials
    wxString credsPath = wxGetHomeDir() + wxFILE_SEP_PATH + ".git-credentials";
    if( wxFileExists( credsPath ) )
    {
        wxTextFile tf;
        if( tf.Open( credsPath ) )
        {
            wxArrayString kept;
            for( size_t i = 0; i < tf.GetLineCount(); i++ )
            {
                wxString line = tf.GetLine( i ).Trim();
                if( !line.IsEmpty() && !line.Contains( "gitlab.com" ) )
                    kept.Add( line );
            }
            tf.Close();

            wxFile out( credsPath, wxFile::write );
            if( out.IsOpened() )
            {
                for( const wxString& line : kept )
                    out.Write( line + "\n" );
                out.Close();
            }
        }
    }

    // Re-read vcs_credentials.json; if it was for GitLab, clear it
    json cred = GIT_VCS_BRIDGE::GetVcsCredential();
    if( cred.value( "host", "" ) == "gitlab.com" )
    {
        wxString jsonPath = wxStandardPaths::Get().GetUserDataDir()
                            + wxFILE_SEP_PATH + "vcs_credentials.json";
        wxFile jf( jsonPath, wxFile::write );
        if( jf.IsOpened() )
        {
            jf.Write( wxString::FromUTF8( json{ { "connected", false } }.dump() ) );
            jf.Close();
        }
    }

    SendResponse( reqId, true, json::object() );
}


void VCS_IPC_HANDLER::SendResponse( const wxString& aRequestId, bool aSuccess,
                                     const json& aPayload )
{
    if( !aSuccess )
    {
        std::string msg = aPayload.value( "message", aPayload.value( "error", "(no message)" ) );
        wxLogError( "VCS_IPC_HANDLER: SendResponse failure reqId=%s: %s",
                    aRequestId, wxString::FromUTF8( msg ) );
    }

    json response = {
        { "requestId", aRequestId.ToStdString() },
        { "success",   aSuccess },
        { "payload",   aPayload }
    };

    wxString script = wxString::Format(
            wxS( "if(typeof VcsApp !== 'undefined') VcsApp.onResponse(%s);" ),
            wxString::FromUTF8( response.dump() ) );

    m_webView->RunScriptAsync( script );
}


void VCS_IPC_HANDLER::SendError( const wxString& aRequestId, const wxString& aError )
{
    wxLogError( "VCS_IPC_HANDLER: SendError reqId=%s: %s", aRequestId, aError );

    json response = {
        { "requestId", aRequestId.ToStdString() },
        { "success",   false },
        { "error",     aError.ToStdString() }
    };

    wxString script = wxString::Format(
            wxS( "if(typeof VcsApp !== 'undefined') VcsApp.onResponse(%s);" ),
            wxString::FromUTF8( response.dump() ) );

    m_webView->RunScriptAsync( script );
}


void VCS_IPC_HANDLER::HandleOpenInEditor( const json& aMsg )
{
    wxString reqId = aMsg.contains( "requestId" )
            ? wxString::FromUTF8( aMsg["requestId"].get<std::string>() )
            : wxString();

    std::string filePath     = aMsg.value( "filePath", "" );
    json        changedItems = aMsg.contains( "changedItems" ) ? aMsg["changedItems"]
                                                                : json::array();

    if( filePath.empty() )
    {
        SendError( reqId, "No file path provided" );
        return;
    }

    // Determine editor frame type from file extension
    bool isSch = filePath.size() > 10 && filePath.substr( filePath.size() - 10 ) == ".kicad_sch";
    bool isPcb = filePath.size() > 10 && filePath.substr( filePath.size() - 10 ) == ".kicad_pcb";

    if( !isSch && !isPcb )
    {
        SendError( reqId, "Not a schematic or PCB file" );
        return;
    }

    // Build full path
    wxString projDir  = wxString::FromUTF8( m_git->GetProjectDir() );
    wxString fullPath = projDir + wxFileName::GetPathSeparator()
                        + wxString::FromUTF8( filePath );

    if( !wxFileName::FileExists( fullPath ) )
    {
        SendError( reqId, "File not found: " + fullPath.ToStdString() );
        return;
    }

    FRAME_T frameType = isSch ? FRAME_SCH : FRAME_PCB_EDITOR;

    // Open (or raise) the editor frame and load the file
    KIWAY_PLAYER* player = m_frame->Kiway().Player( frameType, true, m_frame );

    if( !player )
    {
        SendError( reqId, "Failed to open editor" );
        return;
    }

    player->OpenProjectFiles( { fullPath } );
    player->Show( true );
    player->Raise();

    SendResponse( reqId, true, json::object() );

    if( changedItems.empty() )
        return;

    // Build MAIL_AGENT_WORKING_SET payload — UUIDs only
    json wsPayload;
    wsPayload["sheet_path"] = "/";
    wsPayload["items"]      = json::array();

    // Compute bounding box in editor IU from item mm coordinates.
    // Schematic: 25400 IU/mm (1 IU = 1 mil).  PCB: 1 000 000 IU/mm (1 IU = 1 nm).
    const double iuPerMm = isSch ? 25400.0 : 1000000.0;
    const double padMm   = 20.0; // 20 mm padding around all changed items

    double minX =  1e18, minY =  1e18;
    double maxX = -1e18, maxY = -1e18;

    for( const auto& item : changedItems )
    {
        std::string uuid = item.value( "uuid", "" );

        if( !uuid.empty() )
            wsPayload["items"].push_back( uuid );

        double x = item.value( "x", 0.0 );
        double y = item.value( "y", 0.0 );

        if( x < minX ) minX = x;
        if( y < minY ) minY = y;
        if( x > maxX ) maxX = x;
        if( y > maxY ) maxY = y;
    }

    // Send working set so the dynamic tracker lights up items
    std::string wsStr = wsPayload.dump();
    m_frame->Kiway().ExpressMail( frameType, MAIL_AGENT_WORKING_SET, wsStr, m_frame );

    // Send MAIL_SHOW_DIFF with padded bounding box
    long long bx = static_cast<long long>( ( minX - padMm ) * iuPerMm );
    long long by = static_cast<long long>( ( minY - padMm ) * iuPerMm );
    long long bw = static_cast<long long>( ( maxX - minX + 2.0 * padMm ) * iuPerMm );
    long long bh = static_cast<long long>( ( maxY - minY + 2.0 * padMm ) * iuPerMm );

    json diffPayload = {
        { "x", bx },
        { "y", by },
        { "w", bw },
        { "h", bh }
    };

    std::string diffStr = diffPayload.dump();
    m_frame->Kiway().ExpressMail( frameType, MAIL_SHOW_DIFF, diffStr, m_frame );
}
