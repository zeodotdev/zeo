/*
 * WEBVIEW_BRIDGE implementation.
 *
 * Routes JSON messages between the unified webview (JS) and AGENT_FRAME (C++).
 * All JS→C++ messages are parsed here and dispatched to Handle*() methods.
 * All C++→JS state updates are serialized here and sent via RunScriptAsync().
 */

#include "webview_bridge.h"
#include "bridge_protocol.h"
#include "agent_frame.h"
#include <widgets/webview_panel.h>
#include <wx/log.h>


WEBVIEW_BRIDGE::WEBVIEW_BRIDGE( AGENT_FRAME* aFrame, WEBVIEW_PANEL* aWebView ) :
        m_frame( aFrame ),
        m_webView( aWebView )
{
    LogBridge( "INIT", "WEBVIEW_BRIDGE", "Bridge initialized" );
}


// ── JS → C++ dispatch ──────────────────────────────────────────────────

void WEBVIEW_BRIDGE::OnMessage( const wxString& aMessage )
{
    try
    {
        nlohmann::json msg = nlohmann::json::parse( aMessage.ToStdString() );
        std::string action = msg.value( "action", "" );

        LogBridge( "JS->C++", wxString::FromUTF8( action ) );

        // Chat input actions
        if( action == BridgeAction::SUBMIT )              HandleSubmit( msg );
        else if( action == BridgeAction::ATTACH_CLICK )   HandleAttachClick( msg );
        else if( action == BridgeAction::TEXT_CHANGED )    {} // tracked in JS, no C++ handling needed yet
        else if( action == BridgeAction::EDIT_QUEUED )    HandleEditQueued( msg );

        // Chat display actions
        else if( action == BridgeAction::LINK_CLICK )         HandleLinkClick( msg );
        else if( action == BridgeAction::COPY )               HandleCopy( msg );
        else if( action == BridgeAction::COPY_IMAGE )         HandleCopyImage( msg );
        else if( action == BridgeAction::PREVIEW_IMAGE )      HandlePreviewImage( msg );
        else if( action == BridgeAction::PREVIEW_FILE )       HandlePreviewFile( msg );
        else if( action == BridgeAction::THINKING_TOGGLED )   HandleThinkingToggled( msg );
        else if( action == BridgeAction::TOOLRESULT_TOGGLED ) HandleToolResultToggled( msg );
        else if( action == BridgeAction::SCROLL_ACTIVITY )    HandleScrollActivity( msg );

        // Top bar actions
        else if( action == BridgeAction::NEW_CHAT )       HandleNewChat( msg );
        else if( action == BridgeAction::HISTORY_OPEN )    HandleHistoryOpen( msg );
        else if( action == BridgeAction::HISTORY_SELECT )  HandleHistorySelect( msg );
        else if( action == BridgeAction::HISTORY_SEARCH )  HandleHistorySearch( msg );
        else if( action == BridgeAction::HISTORY_CLOSE )   HandleHistoryClose( msg );

        // Control row actions
        else if( action == BridgeAction::MODEL_CHANGE )         HandleModelChange( msg );
        else if( action == BridgeAction::SEND_CLICK )           HandleSendClick( msg );
        else if( action == BridgeAction::STOP_CLICK )           HandleStopClick( msg );
        else if( action == BridgeAction::SELECTION_PILL_CLICK ) HandleSelectionPillClick( msg );
        else if( action == BridgeAction::PLAN_TOGGLE )           HandlePlanToggle( msg );
        else if( action == BridgeAction::PLAN_APPROVE )          HandlePlanApprove( msg );

        // Pending changes actions
        else if( action == BridgeAction::PENDING_CHANGES_TOGGLE )       HandlePendingChangesToggle( msg );
        else if( action == BridgeAction::PENDING_CHANGES_ACCEPT_ALL ) HandlePendingChangesAcceptAll( msg );
        else if( action == BridgeAction::PENDING_CHANGES_REJECT_ALL ) HandlePendingChangesRejectAll( msg );
        else if( action == BridgeAction::PENDING_CHANGES_VIEW )       HandlePendingChangesView( msg );
        else if( action == BridgeAction::PENDING_CHANGES_ACCEPT_SHEET ) HandlePendingChangesAcceptSheet( msg );
        else if( action == BridgeAction::PENDING_CHANGES_REJECT_SHEET ) HandlePendingChangesRejectSheet( msg );

        // Auth actions
        else if( action == BridgeAction::SIGN_IN_CLICK )  HandleSignInClick( msg );

        // Lifecycle
        else if( action == BridgeAction::PAGE_READY )
        {
            wxLogInfo( "WEBVIEW_BRIDGE: PAGE_READY received, flushing %zu pending scripts",
                       m_pendingScripts.size() );
            m_pageReady = true;

            // Acknowledge so JS stops retrying (WebView2 handler registration race)
            m_webView->RunScriptAsync( wxS( "window._pageReadyAcked=true;" ) );

            FlushPendingScripts();

            // Enable JS debug logging when WXTRACE includes KICAD_AGENT
            if( wxLog::IsAllowedTraceMask( "KICAD_AGENT" ) )
                RunScript( "App.Chat.setDebug(true);" );
        }

        // Debug
        else if( action == BridgeAction::DEBUG )           HandleDebug( msg );

        else
        {
            LogBridge( "JS->C++", "UNKNOWN",
                       wxString::Format( "Unhandled action: %s", action ) );
        }
    }
    catch( const std::exception& e )
    {
        wxLogError( "WEBVIEW_BRIDGE: OnMessage parse error: %s", e.what() );
    }
}


// ── JS → C++ handlers ──────────────────────────────────────────────────

void WEBVIEW_BRIDGE::HandleSubmit( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeSubmit( aMsg );
}

void WEBVIEW_BRIDGE::HandleAttachClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeAttachClick();
}

void WEBVIEW_BRIDGE::HandleEditQueued( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeEditQueued();
}

void WEBVIEW_BRIDGE::HandleLinkClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeLinkClick( aMsg );
}

void WEBVIEW_BRIDGE::HandleCopy( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeCopy( aMsg );
}

void WEBVIEW_BRIDGE::HandleCopyImage( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeCopyImage( aMsg );
}

void WEBVIEW_BRIDGE::HandlePreviewImage( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgePreviewImage( aMsg );
}

void WEBVIEW_BRIDGE::HandlePreviewFile( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgePreviewFile( aMsg );
}

void WEBVIEW_BRIDGE::HandleThinkingToggled( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeThinkingToggled( aMsg );
}

void WEBVIEW_BRIDGE::HandleToolResultToggled( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeToolResultToggled( aMsg );
}

void WEBVIEW_BRIDGE::HandleScrollActivity( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeScrollActivity( aMsg );
}

void WEBVIEW_BRIDGE::HandleNewChat( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoNewChat();
}

void WEBVIEW_BRIDGE::HandleHistoryOpen( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->OnBridgeHistoryOpen();
}

void WEBVIEW_BRIDGE::HandleHistorySelect( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string id = aMsg.value( "conversation_id", "" );
    if( !id.empty() )
        m_frame->LoadConversation( id );
}

void WEBVIEW_BRIDGE::HandleHistorySearch( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string query = aMsg.value( "query", "" );
    m_frame->OnBridgeHistorySearch( wxString::FromUTF8( query ) );
}

void WEBVIEW_BRIDGE::HandleHistoryClose( const nlohmann::json& aMsg )
{
    PushHistoryShow( false );
}

void WEBVIEW_BRIDGE::HandleModelChange( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string model = aMsg.value( "model", "" );
    if( !model.empty() )
        m_frame->DoModelChange( model );
}

void WEBVIEW_BRIDGE::HandlePlanToggle( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoPlanToggle();
}

void WEBVIEW_BRIDGE::HandlePlanApprove( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoPlanApprove();
}

void WEBVIEW_BRIDGE::HandleSendClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoSendClick();
}

void WEBVIEW_BRIDGE::HandleStopClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoStopClick();
}

void WEBVIEW_BRIDGE::HandleSelectionPillClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoSelectionPillClick();
}

void WEBVIEW_BRIDGE::HandlePendingChangesToggle( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoPendingChangesToggle();
}

void WEBVIEW_BRIDGE::HandlePendingChangesAcceptAll( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoPendingChangesAcceptAll();
}

void WEBVIEW_BRIDGE::HandlePendingChangesRejectAll( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoPendingChangesRejectAll();
}

void WEBVIEW_BRIDGE::HandlePendingChangesView( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string path = aMsg.value( "path", "" );
    bool isPcb = aMsg.value( "is_pcb", false );
    m_frame->DoPendingChangesView( wxString::FromUTF8( path ), isPcb );
}

void WEBVIEW_BRIDGE::HandlePendingChangesAcceptSheet( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string path = aMsg.value( "path", "" );
    bool isPcb = aMsg.value( "is_pcb", false );
    m_frame->DoPendingChangesAcceptSheet( wxString::FromUTF8( path ), isPcb );
}

void WEBVIEW_BRIDGE::HandlePendingChangesRejectSheet( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    std::string path = aMsg.value( "path", "" );
    bool isPcb = aMsg.value( "is_pcb", false );
    m_frame->DoPendingChangesRejectSheet( wxString::FromUTF8( path ), isPcb );
}

void WEBVIEW_BRIDGE::HandleSignInClick( const nlohmann::json& aMsg )
{
    if( !m_frame ) return;
    m_frame->DoSignIn();
}

void WEBVIEW_BRIDGE::HandleDebug( const nlohmann::json& aMsg )
{
    std::string debugMsg = aMsg.value( "message", "" );
    wxLogTrace( "AGENT_BRIDGE", "JS_DEBUG: %s", debugMsg );
}


// ── C++ → JS push methods ──────────────────────────────────────────────

void WEBVIEW_BRIDGE::PushChatTitle( const wxString& aTitle )
{
    LogBridge( "C++->JS", "setTitle" );
    RunScript( wxString::Format( "App.TopBar.setTitle('%s');", EscapeJs( aTitle ) ) );
}

void WEBVIEW_BRIDGE::PushHistoryList( const nlohmann::json& aEntries )
{
    LogBridge( "C++->JS", "setHistoryList" );
    wxString json = wxString::FromUTF8( aEntries.dump() );
    RunScript( wxString::Format( "App.TopBar.setHistoryList(%s);", json ) );
}

void WEBVIEW_BRIDGE::PushHistoryShow( bool aShow )
{
    LogBridge( "C++->JS", "showHistory" );
    RunScript( wxString::Format( "App.TopBar.showHistory(%s);",
                                 aShow ? "true" : "false" ) );
}

void WEBVIEW_BRIDGE::PushActiveChat( const std::string& aConversationId )
{
    LogBridge( "C++->JS", "setActiveChat" );
    RunScript( wxString::Format( "App.TopBar.setActiveChat('%s');",
                                 EscapeJs( wxString::FromUTF8( aConversationId ) ) ) );
}

void WEBVIEW_BRIDGE::PushStreamingContent( const wxString& aHtml )
{
    // No log here — this fires at 20Hz during streaming and would flood the log
    RunScript( wxString::Format( "App.Chat.updateStreamingContent('%s');",
                                 EscapeJs( aHtml ) ) );
}

void WEBVIEW_BRIDGE::PushAppendChat( const wxString& aHtml )
{
    LogBridge( "C++->JS", "appendMessage" );
    RunScript( wxString::Format( "App.Chat.appendMessage('%s');", EscapeJs( aHtml ) ) );
}

void WEBVIEW_BRIDGE::PushAppendToEnd( const wxString& aHtml )
{
    LogBridge( "C++->JS", "appendToEnd" );
    RunScript( wxString::Format(
        "(function(){var w=document.querySelector('.content-wrapper');"
        "if(w){var t=document.createElement('div');t.innerHTML='%s';"
        "while(t.firstChild)w.appendChild(t.firstChild);}})();",
        EscapeJs( aHtml ) ) );
}

void WEBVIEW_BRIDGE::PushReplaceQueuedBubble( const wxString& aHtml )
{
    LogBridge( "C++->JS", "replaceQueuedBubble" );
    RunScript( wxString::Format(
        "(function(){var el=document.getElementById('queued-msg');"
        "if(el){var t=document.createElement('div');t.innerHTML='%s';"
        "el.parentNode.replaceChild(t.firstChild,el);}})();",
        EscapeJs( aHtml ) ) );
}

void WEBVIEW_BRIDGE::PushFinalizeStreaming()
{
    LogBridge( "C++->JS", "finalizeStreaming" );
    RunScript( "App.Chat.clearToggleCache(); App.Chat.finalizeStreaming();" );
}

void WEBVIEW_BRIDGE::PushToolResultUpdate( int aIndex, const wxString& aStatusClass,
                                           const wxString& aStatusText,
                                           const wxString& aBodyHtml )
{
    LogBridge( "C++->JS", wxString::Format( "updateToolResult(%d)", aIndex ) );
    RunScript( wxString::Format(
        "App.Chat.updateToolResult(%d, '%s', '%s', '%s');",
        aIndex, EscapeJs( aStatusClass ), EscapeJs( aStatusText ),
        EscapeJs( aBodyHtml ) ) );
}

void WEBVIEW_BRIDGE::PushToolResultImageBegin( int aIndex, const wxString& aPrefix )
{
    RunScript( wxString::Format( "App.Chat.toolImgBegin(%d, '%s');",
                                 aIndex, EscapeJs( aPrefix ) ) );
}

void WEBVIEW_BRIDGE::PushToolResultImageChunk( int aIndex, const wxString& aChunk )
{
    RunScript( wxString::Format( "App.Chat.toolImgChunk(%d, '%s');",
                                 aIndex, EscapeJs( aChunk ) ) );
}

void WEBVIEW_BRIDGE::PushToolResultImageEnd( int aIndex )
{
    RunScript( wxString::Format( "App.Chat.toolImgEnd(%d);", aIndex ) );
}

void WEBVIEW_BRIDGE::PushToolResultActionButton( int aIndex, const wxString& aText,
                                                  const wxString& aHref )
{
    LogBridge( "C++->JS", wxString::Format( "toolActionBtn(%d)", aIndex ) );
    RunScript( wxString::Format( "App.Chat.toolActionBtn(%d, '%s', '%s');",
                                  aIndex, EscapeJs( aText ), EscapeJs( aHref ) ) );
}

void WEBVIEW_BRIDGE::PushCancelRunningTools()
{
    RunScript(
        "(function(){var ss=document.querySelectorAll('.tool-status');"
        "ss.forEach(function(s){if(s.innerHTML.indexOf('Running')!==-1){"
        "s.className='text-text-muted text-[12px] ml-auto';"
        "s.innerHTML='<strong>Cancelled</strong>';}});})();" );
}

void WEBVIEW_BRIDGE::PushFullChatContent( const wxString& aHtml )
{
    LogBridge( "C++->JS", "setFullContent" );
    RunScript( wxString::Format( "App.Chat.setFullContent('%s');", EscapeJs( aHtml ) ) );
}

void WEBVIEW_BRIDGE::PushFinalizeFirstQueuedMessage()
{
    LogBridge( "C++->JS", "finalizeFirstQueuedMessage" );
    RunScript( "var el=document.querySelector('.queued-msg');"
               "if(el){el.classList.remove('queued-msg');el.removeAttribute('id');el.style.opacity='';}" );
}

void WEBVIEW_BRIDGE::PushFinalizeAllQueuedMessages()
{
    LogBridge( "C++->JS", "finalizeAllQueuedMessages" );
    RunScript( "document.querySelectorAll('.queued-msg').forEach(function(el){"
               "el.classList.remove('queued-msg');el.removeAttribute('id');el.style.opacity='';});" );
}

void WEBVIEW_BRIDGE::PushRemoveAllQueuedMessages()
{
    LogBridge( "C++->JS", "removeAllQueuedMessages" );
    RunScript( "document.querySelectorAll('.queued-msg').forEach(function(el){el.remove();});" );
}

void WEBVIEW_BRIDGE::PushQueueCount( int aCount )
{
    RunScript( wxString::Format( "App.Input.setQueueCount(%d);", aCount ) );
}

void WEBVIEW_BRIDGE::PushInputClear()
{
    LogBridge( "C++->JS", "clearInput" );
    RunScript( "App.Input.clear();" );
}

void WEBVIEW_BRIDGE::PushInputFocus()
{
    RunScript( "App.Input.focus();" );
}

void WEBVIEW_BRIDGE::PushInputSetText( const wxString& aText )
{
    RunScript( wxString::Format( "App.Input.setText('%s');", EscapeJs( aText ) ) );
}

void WEBVIEW_BRIDGE::PushInputAppendText( const wxString& aText )
{
    RunScript( wxString::Format( "App.Input.appendText('%s');", EscapeJs( aText ) ) );
}

void WEBVIEW_BRIDGE::PushInputPrependText( const wxString& aText )
{
    RunScript( wxString::Format( "App.Input.prependText('%s');", EscapeJs( aText ) ) );
}

void WEBVIEW_BRIDGE::PushAddAttachment( const wxString& aBase64, const wxString& aMediaType,
                                        const wxString& aFilename )
{
    LogBridge( "C++->JS", wxString::Format( "addAttachment(%s)", aFilename ) );
    RunScript( wxString::Format( "App.Input.addAttachment('%s', '%s', '%s');",
                                 EscapeJs( aBase64 ), EscapeJs( aMediaType ),
                                 EscapeJs( aFilename ) ) );
}

void WEBVIEW_BRIDGE::PushShowToast( const wxString& aMessage, const wxString& aType )
{
    LogBridge( "C++->JS", wxString::Format( "showToast(%s)", aMessage ) );

    if( aType.IsEmpty() )
        RunScript( wxString::Format( "App.showToast('%s');", EscapeJs( aMessage ) ) );
    else
        RunScript( wxString::Format( "App.showToast('%s', 2500, '%s');",
                                     EscapeJs( aMessage ), EscapeJs( aType ) ) );
}

void WEBVIEW_BRIDGE::PushActionButtonState( const wxString& aLabel, bool aEnabled )
{
    LogBridge( "C++->JS", wxString::Format( "setActionButton(%s)", aLabel ) );
    RunScript( wxString::Format( "App.Controls.setActionButton('%s', %s);",
                                 EscapeJs( aLabel ), aEnabled ? "true" : "false" ) );
}

void WEBVIEW_BRIDGE::PushModelList( const std::vector<std::string>& aModels,
                                    const std::string& aSelected )
{
    LogBridge( "C++->JS", "setModelList" );
    nlohmann::json j;
    j["models"] = aModels;
    j["selected"] = aSelected;
    RunScript( wxString::Format( "App.Controls.setModelList(%s);",
                                 wxString::FromUTF8( j.dump() ) ) );
}

void WEBVIEW_BRIDGE::PushPlanMode( bool aPlanMode )
{
    LogBridge( "C++->JS", wxString::Format( "setPlanMode(%s)",
                                            aPlanMode ? "true" : "false" ) );
    RunScript( wxString::Format( "App.Controls.setPlanMode(%s);",
                                 aPlanMode ? "true" : "false" ) );
}

void WEBVIEW_BRIDGE::PushPlanApproval()
{
    LogBridge( "C++->JS", "showPlanApproval" );
    RunScript( "App.Chat.showPlanApproval();" );
}

void WEBVIEW_BRIDGE::PushRemovePlanApproval()
{
    LogBridge( "C++->JS", "removePlanApproval" );
    RunScript( "App.Chat.removePlanApproval();" );
}

void WEBVIEW_BRIDGE::PushSelectionPill( const wxString& aLabel, bool aVisible )
{
    RunScript( wxString::Format( "App.Controls.setSelectionPill('%s', %s);",
                                 EscapeJs( aLabel ), aVisible ? "true" : "false" ) );
}

void WEBVIEW_BRIDGE::PushPendingChanges( const nlohmann::json& aData )
{
    LogBridge( "C++->JS", "updatePendingChanges" );
    RunScript( wxString::Format( "App.PendingChanges.update(%s);",
                                 wxString::FromUTF8( aData.dump() ) ) );
}

void WEBVIEW_BRIDGE::PushPendingChangesShow( bool aShow )
{
    RunScript( wxString::Format( "App.PendingChanges.show(%s);",
                                 aShow ? "true" : "false" ) );
}

void WEBVIEW_BRIDGE::PushAuthState( bool aAuthenticated )
{
    LogBridge( "C++->JS", wxString::Format( "setAuthState(%s)",
                                            aAuthenticated ? "true" : "false" ) );
    RunScript( wxString::Format( "App.Auth.setState(%s);",
                                 aAuthenticated ? "true" : "false" ) );
}


// ── Helpers ────────────────────────────────────────────────────────────

void WEBVIEW_BRIDGE::RunScript( const wxString& aScript )
{
    if( !m_pageReady )
    {
        m_pendingScripts.push_back( aScript );
        return;
    }
    if( !m_webView ) return;
    m_webView->RunScriptAsync( aScript );
}

void WEBVIEW_BRIDGE::FlushPendingScripts()
{
    if( !m_webView )
    {
        m_pendingScripts.clear();
        return;
    }

    for( const auto& script : m_pendingScripts )
        m_webView->RunScriptAsync( script );

    m_pendingScripts.clear();
}

wxString WEBVIEW_BRIDGE::EscapeJs( const wxString& aStr )
{
    wxString escaped = aStr;
    escaped.Replace( "\\", "\\\\" );
    escaped.Replace( "'", "\\'" );
    escaped.Replace( "\n", "\\n" );
    escaped.Replace( "\r", "\\r" );
    escaped.Replace( "</", "<\\/" );  // Prevent </script> injection
    return escaped;
}

void WEBVIEW_BRIDGE::LogBridge( const wxString& aDirection, const wxString& aAction,
                                const wxString& aDetail )
{
    if( aDetail.IsEmpty() )
        wxLogTrace( "AGENT_BRIDGE", "[%s] %s", aDirection, aAction );
    else
        wxLogTrace( "AGENT_BRIDGE", "[%s] %s - %s", aDirection, aAction, aDetail );
}
