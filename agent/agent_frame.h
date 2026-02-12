#ifndef AGENT_FRAME_H
#define AGENT_FRAME_H

#include <kiway_player.h>
#include <widgets/webview_panel.h>
#include <wx/timer.h>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <nlohmann/json.hpp>

#include "agent_llm_client.h"
#include "agent_state.h"
#include "agent_events.h"
#include "agent_chat_history.h"
#include "view/file_attach.h"
#include <agent_workspace.h>

// Forward Declarations
class AGENT_AUTH;
class CHAT_CONTROLLER;
class WEBVIEW_BRIDGE;
struct CONFLICT_INFO;

enum
{
    ID_CHAT_COPY = wxID_HIGHEST + 1001,
    ID_COPY_IMAGE,
};

class AGENT_FRAME : public KIWAY_PLAYER
{
public:
    AGENT_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~AGENT_FRAME();

    // KIWAY_PLAYER virtual overrides
    bool OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override { return true; }
    void ShowChangedLanguage() override;
    void KiwayMailIn( KIWAY_EXPRESS& aEvent ) override;

    wxWindow* GetToolCanvas() const override { return (wxWindow*) this; }

    // Core event handlers
    void OnSend( wxCommandEvent& aEvent );
    void OnStop( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );
    void OnChatRightClick( wxMouseEvent& aEvent );
    void OnChatScroll( wxScrollWinEvent& aEvent );
    void OnPopupClick( wxCommandEvent& aEvent );
    void OnSize( wxSizeEvent& aEvent ) override;

    // History access (public for bridge callbacks)
    AGENT_CHAT_HISTORY& GetChatHistoryDb() { return m_chatHistoryDb; }
    void LoadConversation( const std::string& aConversationId );

    // ── Bridge callback methods (called by WEBVIEW_BRIDGE) ───────────────

    // Chat input
    void OnBridgeSubmit( const nlohmann::json& aMsg );
    void OnBridgeAttachClick();

    // Chat display
    void OnBridgeLinkClick( const nlohmann::json& aMsg );
    void OnBridgeCopy( const nlohmann::json& aMsg );
    void OnBridgeCopyImage( const nlohmann::json& aMsg );
    void OnBridgePreviewImage( const nlohmann::json& aMsg );
    void OnBridgePreviewFile( const nlohmann::json& aMsg );
    void OnBridgeThinkingToggled( const nlohmann::json& aMsg );
    void OnBridgeToolResultToggled( const nlohmann::json& aMsg );
    void OnBridgeScrollActivity( const nlohmann::json& aMsg );

    // Top bar
    void OnBridgeHistoryOpen();
    void OnBridgeHistorySearch( const wxString& aQuery );

    // ── Bridge-triggered actions (called by WEBVIEW_BRIDGE) ──────────────

    void DoNewChat();
    void DoModelChange( const std::string& aModel );
    void DoTrackToggle();
    void DoPlanToggle();
    void DoPlanApprove();
    void DoSendClick();
    void DoStopClick();
    void DoSelectionPillClick();
    void DoPendingChangesToggle();
    void DoPendingChangesAcceptAll();
    void DoPendingChangesRejectAll();
    void DoPendingChangesView( const wxString& aPath, bool aIsPcb );
    void DoSignIn();

    // Agent change approval
    void OnSchematicChangeHandled( bool aAccepted );
    void OnPcbChangeHandled( bool aAccepted );

    // Async LLM streaming event handlers
    void OnLLMStreamChunk( wxThreadEvent& aEvent );
    void OnLLMStreamComplete( wxThreadEvent& aEvent );
    void OnLLMStreamError( wxThreadEvent& aEvent );

    // Controller event handlers (events from CHAT_CONTROLLER)
    void OnChatTextDelta( wxThreadEvent& aEvent );
    void OnChatThinkingStart( wxThreadEvent& aEvent );
    void OnChatThinkingDelta( wxThreadEvent& aEvent );
    void OnChatThinkingDone( wxThreadEvent& aEvent );
    void OnChatToolGenerating( wxThreadEvent& aEvent );
    void OnChatToolStart( wxThreadEvent& aEvent );
    void OnChatToolComplete( wxThreadEvent& aEvent );
    void OnChatTurnComplete( wxThreadEvent& aEvent );
    void OnChatError( wxThreadEvent& aEvent );
    void OnChatStateChanged( wxThreadEvent& aEvent );
    void OnChatTitleDelta( wxThreadEvent& aEvent );
    void OnChatTitleGenerated( wxThreadEvent& aEvent );
    void OnChatHistoryLoaded( wxThreadEvent& aEvent );

    // Async tool execution completion (for background tools like autorouter)
    void OnAsyncToolComplete( wxCommandEvent& aEvent );

    // Override CommonSettingsChanged to avoid toolbar recreation (AGENT_FRAME has no toolbars)
    void CommonSettingsChanged( int aFlags ) override;

    // Tool call helper (synchronous)
    std::string SendRequest( int aDest, const std::string& aPayload );

    DECLARE_EVENT_TABLE()

private:
    // ── UI ────────────────────────────────────────────────────────────────

    WEBVIEW_PANEL*                  m_webView;   // Single unified webview (entire UI)
    std::unique_ptr<WEBVIEW_BRIDGE> m_bridge;    // JS↔C++ message router

    wxString       m_pendingInputText;   // Text from JS submit message, read by OnSend
    std::vector<FILE_ATTACHMENT> m_pendingAttachments;  // File attachments from JS submit

    // Message queue (queued during generation, auto-sent when turn completes)
    wxString                     m_queuedInputText;
    std::vector<FILE_ATTACHMENT> m_queuedAttachments;
    wxString                     m_queuedMsgHtml;  // Tracks queued message HTML in DOM
    bool                         m_turnCompleteForQueue = false;  // Set when turn ends (continuing=false)

    wxImage     m_pendingCopyImage;  // Image waiting for context menu "Copy Image" action

    // Selection pill state (label stored here since the button is now in JS)
    wxString    m_currentSelectionLabel;

    // Track Agent state
    bool        m_isTrackingAgent;

    // Plan mode state
    AgentMode   m_agentMode = AgentMode::EXECUTE;

    // Pending changes state (business logic moved from PENDING_CHANGES_PANEL)
    std::set<wxString> m_pendingSchSheets;  // Sheets with schematic changes
    bool               m_hasPcbChanges;
    wxString           m_pcbFilename;
    void QueryPendingChanges();  // Query editors and push results to JS

    // ── Auth ──────────────────────────────────────────────────────────────

    AGENT_AUTH*   m_auth;
    void UpdateAuthUI();
    bool CheckAuthentication();

    // ── Chat State ────────────────────────────────────────────────────────

    std::string m_toolResponse;
    std::string m_schJson;
    std::string m_pcbJson;
    std::string m_schSummary;
    std::string m_pcbSummary;

    nlohmann::json m_chatHistory;     // Full history for display/persistence
    nlohmann::json m_apiContext;      // Context sent to API (may be compacted)
    std::string    m_currentResponse; // Streaming accumulator
    bool           m_stopRequested;   // Flag for sync wait loops

    AGENT_CHAT_HISTORY m_chatHistoryDb;

    // Async Tool Execution State
    AgentConversationContext m_conversationCtx;
    wxTimer                  m_toolTimeoutTimer;
    static const int         TOOL_TIMEOUT_MS = 30000;

    // Model Context (context prompt is now injected server-side via context_type)
    std::string    m_currentModel;    // Currently selected model name

    // ── HTML Rendering ────────────────────────────────────────────────────

    // m_fullHtmlContent tracks the chat area inner HTML (messages, tool results, streaming divs).
    // It does NOT include the page template (which is loaded once and never replaced).
    wxString m_fullHtmlContent;
    wxString m_htmlBeforeAgentResponse; // Snapshot before streaming starts (for markdown re-render)
    wxString m_toolCallHtml;           // Accumulated tool call/result HTML
    wxString m_thinkingHtml;           // Thinking block HTML
    wxString m_thinkingContent;        // Raw accumulated thinking text
    bool     m_thinkingExpanded;
    bool     m_isThinking;
    bool     m_isStreamingMarkdown;
    bool     m_thinkingHtmlDirty;      // Deferred rebuild flag for thinking HTML
    int      m_currentThinkingIndex;
    wxString m_lastToolDesc;
    bool     m_userScrolledUp;
    long     m_lastScrollActivityMs;
    bool     m_htmlUpdatePending;
    bool     m_htmlUpdateNeeded;
    wxTimer  m_htmlUpdateTimer;
    void     RebuildThinkingHtml();

    // Historical thinking toggle state
    std::vector<wxString>  m_historicalThinking;
    std::set<int>          m_historicalThinkingExpanded;

    // Historical tool result toggle state
    std::set<int>          m_historicalToolResultExpanded;
    int                    m_toolResultCounter;

    // Active tool result - lives in permanent DOM, not streaming div
    wxString               m_activeRunningHtml;
    int                    m_activeToolResultIdx;

    void                   RenderChatHistory();

    void     AppendHtml( const wxString& aHtml );
    void     SetHtml( const wxString& aHtml );
    void     UpdateAgentResponse();
    wxString BuildStreamingContent();
    void     FlushStreamingContentUpdate( bool aForce = false );
    void     AutoScrollToBottom();

    // Generating animation
    wxTimer  m_generatingTimer;
    int      m_generatingDots;
    bool     m_isGenerating;
    bool     m_isCompacting;
    wxString m_generatingToolName;
    void     OnGeneratingTimer( wxTimerEvent& aEvent );
    void     OnHtmlUpdateTimer( wxTimerEvent& aEvent );
    void     StartGeneratingAnimation();
    void     StopGeneratingAnimation();

    // Message queueing (queue during generation, auto-send when turn completes)
    bool     HasQueuedMessage() const;
    void     QueueMessage();
    void     SendQueuedMessage();
    void     ClearQueuedMessage();
    void     DoCancelOperation( bool aShowStopped );

    // ── Tools & LLM ──────────────────────────────────────────────────────

    std::vector<LLM_TOOL>              m_tools;
    std::unique_ptr<AGENT_LLM_CLIENT>  m_llmClient;
    nlohmann::json                     m_pendingToolCalls;

    std::unique_ptr<CHAT_CONTROLLER> m_chatController;

    void InitializeTools();

    // Multi-project access helpers
    std::vector<wxString> GetOpenEditorFiles();
    std::vector<wxString> GetAllowedPaths();

    // Async LLM streaming helpers
    void StartAsyncLLMRequest();
    void RetryLastRequest();

    // ── Concurrent Editing ────────────────────────────────────────────────

    AGENT_WORKSPACE m_agentWorkspace;
    void SetAgentTargetSheet( const KIID& aSheetId, const wxString& aSheetName );
    AGENT_WORKSPACE& GetAgentWorkspace() { return m_agentWorkspace; }

    // Pending editor open request
    bool        m_pendingOpenSch;
    bool        m_pendingOpenPcb;
    std::string m_pendingOpenToolId;
    wxString    m_pendingOpenFilePath;
    void ShowOpenEditorApproval( const wxString& aEditorType );
    void OnApproveOpenEditor();
    void OnRejectOpenEditor();

    bool DoOpenEditor( FRAME_T aFrameType );
};

#endif // AGENT_FRAME_H
