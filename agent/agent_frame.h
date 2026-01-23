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
#include <memory>
#include <nlohmann/json.hpp>

#include "agent_llm_client.h"
#include "agent_state.h"
#include "agent_events.h"
#include "agent_chat_history.h"

// Forward Declarations
class AGENT_THREAD;
class AGENT_AUTH;
class wxStaticText;

enum
{
    ID_CHAT_COPY = wxID_HIGHEST + 1001,
    ID_CHAT_HISTORY_TOOL,
    ID_NEW_CHAT,
    ID_LOGIN,
    ID_LOGOUT,
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
    void OnPopupClick( wxCommandEvent& aEvent );
    void OnHistoryTool( wxCommandEvent& aEvent );
    void OnHistoryMenuSelect( wxCommandEvent& aEvent );

    // Async tool execution event handlers
    void OnToolExecutionComplete( wxCommandEvent& aEvent );
    void OnToolExecutionError( wxCommandEvent& aEvent );
    void OnToolExecutionTimeout( wxTimerEvent& aEvent );
    void OnToolExecutionProgress( wxCommandEvent& aEvent );

    // Async LLM streaming event handlers
    void OnLLMStreamChunk( wxThreadEvent& aEvent );
    void OnLLMStreamComplete( wxThreadEvent& aEvent );
    void OnLLMStreamError( wxThreadEvent& aEvent );

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
    wxHtmlWindow* m_chatWindow;
    wxTextCtrl*   m_inputCtrl;
    wxButton*     m_actionButton;
    wxButton*     m_selectionPill;
    wxButton*     m_toolButton;
    wxButton*     m_historyButton;
    wxChoice*     m_modelChoice;
    wxPanel*      m_inputPanel;

    AGENT_THREAD* m_workerThread;
    AGENT_AUTH*   m_auth;
    std::string   m_authWebUrl;      // URL to auth web page (e.g., https://www.harold.so/auth)

    std::string m_toolResponse;
    std::string m_schJson;
    std::string m_pcbJson;
    std::string m_schSummary;
    std::string m_pcbSummary;

    // Chat State
    nlohmann::json m_chatHistory;     // Full history
    std::string    m_currentResponse; // Streaming accumulator
    std::string    m_pendingTool;     // Tool waiting for approval
    bool           m_stopRequested;   // Flag for sync wait loops

    // Chat History Persistence
    AGENT_CHAT_HISTORY m_chatHistoryDb;

    // Async Tool Execution State
    AgentConversationContext m_conversationCtx;  // State machine context
    wxTimer                  m_toolTimeoutTimer; // Timer for tool execution timeout
    static const int         TOOL_TIMEOUT_MS = 30000; // 30 second timeout

    // Model Context
    std::string    m_modelContext;    // Loaded API reference for current model
    std::string    m_currentModel;    // Currently selected model name
    void           LoadModelContext();
    std::string    GetSystemPrompt();

    // HTML Rendering
    wxString m_fullHtmlContent; // Complete HTML buffer
    void     AppendHtml( const wxString& aHtml );
    void     SetHtml( const wxString& aHtml );

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
};

#endif // AGENT_FRAME_H
