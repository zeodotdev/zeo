/*
 * WEBVIEW_BRIDGE: JS↔C++ message router for the unified webview.
 *
 * Decouples the webview's message protocol from AGENT_FRAME business logic.
 * All JS messages are dispatched here; all C++→JS state pushes go through here.
 */

#ifndef WEBVIEW_BRIDGE_H
#define WEBVIEW_BRIDGE_H

#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class AGENT_FRAME;
class WEBVIEW_PANEL;

class WEBVIEW_BRIDGE
{
public:
    WEBVIEW_BRIDGE( AGENT_FRAME* aFrame, WEBVIEW_PANEL* aWebView );

    /**
     * Handle an incoming JSON message from the webview.
     * Called by the WEBVIEW_PANEL message handler callback.
     */
    void OnMessage( const wxString& aMessage );

    // ── C++ → JS push methods ──────────────────────────────────────────

    // Top bar
    void PushChatTitle( const wxString& aTitle );
    void PushHistoryList( const nlohmann::json& aEntries );
    void PushHistoryShow( bool aShow );

    // Chat area
    void PushStreamingContent( const wxString& aHtml );
    void PushAppendChat( const wxString& aHtml );
    void PushFinalizeStreaming();
    void PushToolResultUpdate( int aIndex, const wxString& aStatusClass,
                               const wxString& aStatusText, const wxString& aBodyHtml );
    void PushToolResultImageBegin( int aIndex, const wxString& aPrefix );
    void PushToolResultImageChunk( int aIndex, const wxString& aChunk );
    void PushToolResultImageEnd( int aIndex );
    void PushFullChatContent( const wxString& aHtml );
    void PushReplaceQueuedMessage( const wxString& aHtml );
    void PushRemoveQueuedMessage();

    // Input area
    void PushInputClear();
    void PushInputFocus();
    void PushInputSetText( const wxString& aText );
    void PushInputAppendText( const wxString& aText );
    void PushAddAttachment( const wxString& aBase64, const wxString& aMediaType,
                            const wxString& aFilename );

    // Notifications
    void PushShowToast( const wxString& aMessage );

    // Controls
    void PushActionButtonState( const wxString& aLabel, bool aEnabled = true );
    void PushModelList( const std::vector<std::string>& aModels,
                        const std::string& aSelected );
    void PushTrackingState( bool aTracking );
    void PushTrackButtonVisible( bool aVisible );
    void PushSelectionPill( const wxString& aLabel, bool aVisible );
    // Pending changes panel
    void PushPendingChanges( const nlohmann::json& aData );
    void PushPendingChangesShow( bool aShow );

    // Auth
    void PushAuthState( bool aAuthenticated );

private:
    // ── JS → C++ action handlers ───────────────────────────────────────

    // Chat input
    void HandleSubmit( const nlohmann::json& aMsg );
    void HandleAttachClick( const nlohmann::json& aMsg );

    // Chat display
    void HandleLinkClick( const nlohmann::json& aMsg );
    void HandleCopy( const nlohmann::json& aMsg );
    void HandleCopyImage( const nlohmann::json& aMsg );
    void HandlePreviewImage( const nlohmann::json& aMsg );
    void HandlePreviewFile( const nlohmann::json& aMsg );
    void HandleThinkingToggled( const nlohmann::json& aMsg );
    void HandleToolResultToggled( const nlohmann::json& aMsg );
    void HandleScrollActivity( const nlohmann::json& aMsg );

    // Top bar
    void HandleNewChat( const nlohmann::json& aMsg );
    void HandleHistoryOpen( const nlohmann::json& aMsg );
    void HandleHistorySelect( const nlohmann::json& aMsg );
    void HandleHistorySearch( const nlohmann::json& aMsg );
    void HandleHistoryClose( const nlohmann::json& aMsg );

    // Controls
    void HandleModelChange( const nlohmann::json& aMsg );
    void HandleTrackToggle( const nlohmann::json& aMsg );
    void HandleSendClick( const nlohmann::json& aMsg );
    void HandleStopClick( const nlohmann::json& aMsg );
    void HandleSelectionPillClick( const nlohmann::json& aMsg );

    // Pending changes
    void HandlePendingChangesToggle( const nlohmann::json& aMsg );
    void HandlePendingChangesAcceptAll( const nlohmann::json& aMsg );
    void HandlePendingChangesRejectAll( const nlohmann::json& aMsg );
    void HandlePendingChangesView( const nlohmann::json& aMsg );

    // Auth
    void HandleSignInClick( const nlohmann::json& aMsg );

    // Debug
    void HandleDebug( const nlohmann::json& aMsg );

    // ── Helpers ────────────────────────────────────────────────────────

    void RunScript( const wxString& aScript );

    /**
     * Escape a string for safe embedding in a JS string literal (single-quoted).
     */
    static wxString EscapeJs( const wxString& aStr );

    void LogBridge( const wxString& aDirection, const wxString& aAction,
                    const wxString& aDetail = wxEmptyString );

    void FlushPendingScripts();

    AGENT_FRAME*   m_frame;
    WEBVIEW_PANEL* m_webView;
    bool           m_pageReady = false;
    std::vector<wxString> m_pendingScripts;
};

#endif // WEBVIEW_BRIDGE_H
