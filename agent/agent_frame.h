#ifndef AGENT_FRAME_H
#define AGENT_FRAME_H

#include <kiway_player.h>
#include <wx/html/htmlwin.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/button.h>
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
#include <agent_workspace.h>

// Forward Declarations
class AGENT_THREAD;
class AGENT_AUTH;
class PENDING_CHANGES_PANEL;
class HISTORY_PANEL;
class wxStaticText;
struct CONFLICT_INFO;

enum
{
    ID_CHAT_COPY = wxID_HIGHEST + 1001,
    ID_CHAT_HISTORY_TOOL,
    ID_NEW_CHAT,
    ID_LOGIN,
    ID_LOGOUT,
    ID_CHAT_HISTORY_SHOW_ALL = 1999,
    ID_CHAT_HISTORY_MENU_BASE = 2000
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

    // Event handlers
    void OnSend( wxCommandEvent& aEvent );
    void OnStop( wxCommandEvent& aEvent );
    void OnAgentUpdate( wxCommandEvent& aEvent );
    void OnAgentComplete( wxCommandEvent& aEvent );
    void OnTextEnter( wxCommandEvent& aEvent );
    void OnSelectionPillClick( wxCommandEvent& aEvent );
    void OnHtmlLinkClick( wxHtmlLinkEvent& aEvent );
    void OnToolClick( wxCommandEvent& aEvent );
    void OnModelSelection( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );
    void OnInputKeyDown( wxKeyEvent& aEvent );
    void OnInputText( wxCommandEvent& aEvent );
    void OnChatRightClick( wxMouseEvent& aEvent );
    void OnChatScroll( wxScrollWinEvent& aEvent );
    void OnPopupClick( wxCommandEvent& aEvent );
    void OnHistoryTool( wxCommandEvent& aEvent );
    void OnHistoryMenuSelect( wxCommandEvent& aEvent );
    void OnHistoryShowAll( wxCommandEvent& aEvent );
    void OnNewChat( wxCommandEvent& aEvent );

    // History panel access (public for HISTORY_PANEL callbacks)
    AGENT_CHAT_HISTORY& GetChatHistoryDb() { return m_chatHistoryDb; }
    void LoadConversation( const std::string& aConversationId );
    void OnSignIn( wxCommandEvent& aEvent );
    void OnSize( wxSizeEvent& aEvent ) override;
    void OnPendingChangesClick( wxCommandEvent& aEvent );

    // Agent change approval (public for panel callbacks)
    void OnSchematicChangeHandled( bool aAccepted );
    void OnPcbChangeHandled( bool aAccepted );

    // Async tool execution event handlers
    void OnToolExecutionComplete( wxCommandEvent& aEvent );
    void OnToolExecutionError( wxCommandEvent& aEvent );
    void OnToolExecutionTimeout( wxTimerEvent& aEvent );
    void OnToolExecutionProgress( wxCommandEvent& aEvent );

    // Async LLM streaming event handlers
    void OnLLMStreamChunk( wxThreadEvent& aEvent );
    void OnLLMStreamComplete( wxThreadEvent& aEvent );
    void OnLLMStreamError( wxThreadEvent& aEvent );

    // Title generation event handler
    void OnTitleGeneratedEvent( wxThreadEvent& aEvent );

    // Tool call helper (legacy synchronous - will be deprecated)
    std::string SendRequest( int aDest, const std::string& aPayload );

    // Async tool execution (new non-blocking interface)
    void ExecuteToolAsync( const std::string& aToolName,
                           const nlohmann::json& aInput,
                           const std::string& aToolUseId );

    // State machine helpers
    AgentConversationState GetConversationState() const;
    bool                   CanAcceptUserInput() const;

    DECLARE_EVENT_TABLE()

private:
    wxHtmlWindow*  m_chatWindow;
    wxTextCtrl*    m_inputCtrl;
    wxStaticText*  m_chatNameLabel;
    wxButton*      m_actionButton;
    wxButton*      m_selectionPill;
    wxButton*      m_toolButton;
    wxButton*      m_newChatButton;
    wxButton*      m_historyButton;
    wxChoice*      m_modelChoice;
    wxPanel*       m_inputPanel;

    // Pending changes UI
    wxButton*               m_pendingChangesBtn;    // Shows when changes pending
    PENDING_CHANGES_PANEL*  m_pendingChangesPanel;  // Panel for managing changes

    // History panel UI
    HISTORY_PANEL*          m_historyPanel;         // Full history list panel

    // Sign-in overlay (shown when not authenticated)
    wxPanel*       m_signInOverlay;
    wxButton*      m_signInButton;

    AGENT_THREAD* m_workerThread;
    AGENT_AUTH*   m_auth;

    std::string m_toolResponse;
    std::string m_schJson;
    std::string m_pcbJson;
    std::string m_schSummary;
    std::string m_pcbSummary;

    // Chat State
    nlohmann::json m_chatHistory;     // Full history for display/persistence
    nlohmann::json m_apiContext;      // Context sent to API (may be compacted)
    std::string    m_currentResponse; // Streaming accumulator
    std::string    m_pendingTool;     // Tool waiting for approval
    bool           m_stopRequested;   // Flag for sync wait loops

    // Chat History Persistence
    AGENT_CHAT_HISTORY m_chatHistoryDb;

    // Async Tool Execution State
    AgentConversationContext m_conversationCtx;  // State machine context
    wxTimer                  m_toolTimeoutTimer; // Timer for tool execution timeout
    static const int         TOOL_TIMEOUT_MS = 30000; // 30 second timeout

    // Model Context (context prompt is now injected server-side via context_type)
    std::string    m_currentModel;    // Currently selected model name
    std::string    GetSystemPrompt();

    // HTML Rendering
    wxString m_fullHtmlContent;        // Complete HTML buffer
    wxString m_htmlBeforeAgentResponse; // HTML snapshot before streaming starts (for markdown re-render)
    wxString m_toolCallHtml;           // Accumulated tool call/result HTML (preserved during re-render)
    wxString m_thinkingHtml;           // Thinking block HTML (preserved during re-render)
    wxString m_thinkingContent;        // Raw accumulated thinking text
    bool     m_thinkingExpanded;       // Whether thinking is expanded (click to toggle)
    bool     m_isThinking;             // Whether currently in thinking phase (for loading animation)
    int      m_currentThinkingIndex;   // Index for current streaming thinking (for toggle:thinking:N links)
    wxString m_lastToolDesc;           // Temp storage for tool description during history replay
    bool     m_userScrolledUp;         // Track if user has scrolled up during generation
    void     RebuildThinkingHtml();    // Rebuild thinking HTML from m_thinkingContent

    // Historical thinking toggle state
    std::vector<wxString>  m_historicalThinking;          // Thinking content from loaded history
    std::set<int>          m_historicalThinkingExpanded;  // Which historical thinking blocks are expanded
    void                   RenderChatHistory();           // Re-render chat history with current toggle states

    void     AppendHtml( const wxString& aHtml );
    void     SetHtml( const wxString& aHtml );
    void     UpdateAgentResponse();    // Re-render current response with markdown
    void     AutoScrollToBottom();     // Scroll to bottom only if user hasn't scrolled up

    // Generating animation
    wxTimer  m_generatingTimer;        // Timer for animating dots
    int      m_generatingDots;         // Current dot count (0-3)
    bool     m_isGenerating;           // Whether we're currently streaming
    void     OnGeneratingTimer( wxTimerEvent& aEvent );
    void     StartGeneratingAnimation();
    void     StopGeneratingAnimation();

    // Chat title generation
    bool        m_needsTitleGeneration;  // Whether to generate title after first response
    std::string m_firstUserMessage;      // First user message for title generation
    void        GenerateChatTitle();     // Async call to generate title
    void        OnTitleGenerated( const std::string& aTitle, const std::string& aConversationId ); // Handle generated title

    // Auth helpers
    void UpdateAuthUI();
    bool CheckAuthentication(); // Returns true if authenticated

    // Native Tool Calling
    std::vector<LLM_TOOL>              m_tools;              // Available tools
    std::unique_ptr<AGENT_LLM_CLIENT>  m_llmClient;          // LLM client instance
    nlohmann::json                     m_pendingToolCalls;   // Tool calls awaiting execution

    void InitializeTools();                                                    // Setup tool definitions
    std::string ExecuteTool( const std::string& aName, const nlohmann::json& aInput );  // Execute a single tool
    void HandleLLMEvent( const LLM_EVENT& aEvent );                           // Process LLM events
    void ContinueConversation();                                               // Continue after tool results
    void AddToolResultToHistory( const std::string& aToolUseId, const std::string& aResult );
    void AddAssistantToolUseToHistory( const nlohmann::json& aToolUseBlocks );

    // Async tool execution helpers
    void ProcessToolResult( const std::string& aToolUseId, const std::string& aResult, bool aSuccess );
    void ContinueConversationWithToolResult();  // Non-blocking continuation
    std::string BuildToolPayload( const std::string& aToolName, const nlohmann::json& aInput );

    // Async LLM streaming helpers
    void StartAsyncLLMRequest();  // Start an async LLM request
    void HandleLLMChunk( const LLMStreamChunk& aChunk );  // Process a streaming chunk
    void RetryLastRequest();      // Retry after context recovery

    // Agent change approval
    bool m_hasPendingSchChanges;    // True if schematic has pending agent changes
    bool m_hasPendingPcbChanges;    // True if PCB has pending agent changes
    wxString m_pendingSchSheetPath; // Sheet path for pending schematic changes (human-readable)
    wxString m_pendingPcbFilename;  // Filename for pending PCB changes
    void CheckForPendingChanges();  // Query editors for pending changes
    void ShowApproveRejectButtons(); // Show pending changes button in control bar
    void ClearApprovalButtons( bool aIsSchematic );  // Clear approval buttons when diff overlay dismissed

    // Concurrent editing support
    AGENT_WORKSPACE m_agentWorkspace;   // Manages agent's editing context

    /**
     * Set the target sheet for agent operations.
     * Allows agent to work on a specific sheet while user views another.
     */
    void SetAgentTargetSheet( const KIID& aSheetId, const wxString& aSheetName );

    /**
     * Get the agent workspace for direct access.
     */
    AGENT_WORKSPACE& GetAgentWorkspace() { return m_agentWorkspace; }

    /**
     * Begin a new agent transaction for concurrent editing.
     */
    void BeginAgentTransaction();

    /**
     * End the current agent transaction.
     * @param aCommit If true, prepare changes for approval; if false, discard
     */
    void EndAgentTransaction( bool aCommit );

    /**
     * Handle conflict detection notification from editors.
     */
    void OnConflictDetected( const KIID& aItemId, const CONFLICT_INFO& aInfo );

    /**
     * Handle conflict resolution from UI.
     */
    void OnConflictResolved( const KIID& aItemId, CONFLICT_RESOLUTION aResolution );

    /**
     * Update the conflict display in the pending changes panel.
     */
    void UpdateConflictDisplay();

    // Pending editor open request
    bool        m_pendingOpenSch;       // True if schematic editor open is pending approval
    bool        m_pendingOpenPcb;       // True if PCB editor open is pending approval
    std::string m_pendingOpenToolId;    // Tool use ID for the pending open request
    void ShowOpenEditorApproval( const wxString& aEditorType );
    void OnApproveOpenEditor();
    void OnRejectOpenEditor();
    bool DoOpenEditor( FRAME_T aFrameType );
};

#endif // AGENT_FRAME_H
