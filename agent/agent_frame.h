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

// Forward Declarations
class AGENT_THREAD;

enum
{
    ID_CHAT_COPY = wxID_HIGHEST + 1001
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

    // Tool call helper
    std::string SendRequest( int aDest, const std::string& aPayload );

    DECLARE_EVENT_TABLE()

private:
    wxHtmlWindow* m_chatWindow;
    wxTextCtrl*   m_inputCtrl;
    wxButton*     m_actionButton;
    wxButton*     m_plusButton;
    wxButton*     m_selectionPill;
    wxButton*     m_toolButton;
    wxChoice*     m_modelChoice;
    wxPanel*      m_inputPanel;

    AGENT_THREAD* m_workerThread;

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

    // Model Context
    std::string    m_modelContext;    // Loaded API reference for current model
    std::string    m_currentModel;    // Currently selected model name
    void           LoadModelContext();
    std::string    GetSystemPrompt();

    // HTML Rendering
    wxString m_fullHtmlContent; // Complete HTML buffer
    void     AppendHtml( const wxString& aHtml );
    void     SetHtml( const wxString& aHtml );

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
};

#endif // AGENT_FRAME_H
