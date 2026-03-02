#ifndef VCS_IPC_HANDLER_H
#define VCS_IPC_HANDLER_H

#include <nlohmann/json.hpp>
#include <wx/string.h>
#include <memory>

class VCS_FRAME;
class WEBVIEW_PANEL;
class GIT_VCS_BRIDGE;

/**
 * Routes JSON messages from the webview JS to git operations.
 * All JS→C++ communication passes through this handler.
 */
class VCS_IPC_HANDLER
{
public:
    VCS_IPC_HANDLER( VCS_FRAME* aFrame, WEBVIEW_PANEL* aWebView );
    ~VCS_IPC_HANDLER();

    /**
     * Entry point: called when the webview sends a JSON message via window.webkit.messageHandlers.vcs
     */
    void OnMessage( const wxString& aMessage );

private:
    // ── Git read operations ──────────────────────────────────────────────
    void HandleGetStatus( const nlohmann::json& aMsg );
    void HandleGetHistory( const nlohmann::json& aMsg );
    void HandleGetCommitFiles( const nlohmann::json& aMsg );
    void HandleGetTextDiff( const nlohmann::json& aMsg );
    void HandleGetVisualDiff( const nlohmann::json& aMsg );
    void HandleGetBranches( const nlohmann::json& aMsg );
    void HandleGetRemotes( const nlohmann::json& aMsg );

    // ── Git write operations ─────────────────────────────────────────────
    void HandleStage( const nlohmann::json& aMsg );
    void HandleUnstage( const nlohmann::json& aMsg );
    void HandleCommit( const nlohmann::json& aMsg );
    void HandlePush( const nlohmann::json& aMsg );
    void HandlePull( const nlohmann::json& aMsg );
    void HandleDiscardChanges( const nlohmann::json& aMsg );
    void HandleCreateBranch( const nlohmann::json& aMsg );
    void HandleSwitchBranch( const nlohmann::json& aMsg );
    void HandleAddRemote( const nlohmann::json& aMsg );
    void HandleFetch( const nlohmann::json& aMsg );
    void HandleInitRepo( const nlohmann::json& aMsg );
    void HandleGetUserConfig( const nlohmann::json& aMsg );
    void HandleStoreCredential( const nlohmann::json& aMsg );
    void HandleOpenUrl( const nlohmann::json& aMsg );
    void HandleConnectGitHub( const nlohmann::json& aMsg );
    void HandleGetGitHubToken( const nlohmann::json& aMsg );
    void HandleDisconnectGitHub( const nlohmann::json& aMsg );
    void HandleConnectGitLab( const nlohmann::json& aMsg );
    void HandleGetGitLabToken( const nlohmann::json& aMsg );
    void HandleDisconnectGitLab( const nlohmann::json& aMsg );
    void HandleOpenInEditor( const nlohmann::json& aMsg );

    // ── Helpers ──────────────────────────────────────────────────────────
    void SendResponse( const wxString& aRequestId, bool aSuccess,
                       const nlohmann::json& aPayload );
    void SendError( const wxString& aRequestId, const wxString& aError );

    VCS_FRAME*                    m_frame;
    WEBVIEW_PANEL*                m_webView;
    std::unique_ptr<GIT_VCS_BRIDGE> m_git;
};

#endif // VCS_IPC_HANDLER_H
