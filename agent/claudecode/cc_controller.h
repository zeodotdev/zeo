#ifndef CC_CONTROLLER_H
#define CC_CONTROLLER_H

#include <wx/event.h>
#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <vector>
#include <memory>

class CC_SUBPROCESS;

/**
 * CC_CONTROLLER translates Claude Code NDJSON stream events into EVT_CHAT_*
 * events that AGENT_FRAME already handles. This allows the existing chat UI
 * to render Claude Code output with no rendering changes.
 */
class CC_CONTROLLER : public wxEvtHandler
{
public:
    CC_CONTROLLER( wxEvtHandler* aEventSink );
    ~CC_CONTROLLER();

    /**
     * Start a new Claude Code session.
     * @param aWorkingDir  Working directory for the subprocess
     * @param aApiSocketPath  Path to KiCad API socket (for MCP config generation)
     * @param aPythonPath  Path to bundled Python3 binary (for MCP server command)
     */
    void Start( const std::string& aWorkingDir,
                const std::string& aApiSocketPath = "", const std::string& aPythonPath = "" );

    bool SendMessage( const std::string& aText );
    void Cancel();
    void NewSession();
    void ResumeSession( const std::string& aSessionId );

    bool IsBusy() const { return m_busy; }
    bool IsRunning() const;
    std::string GetSessionId() const { return m_sessionId; }
    const std::string& GetCurrentResponse() const { return m_currentResponse; }
    void ClearCurrentResponse() { m_currentResponse.clear(); }

    // Chat history persistence (same format as Zeo agent)
    const nlohmann::json& GetChatHistory() const { return m_chatHistory; }
    void SetChatHistory( const nlohmann::json& aHistory ) { m_chatHistory = aHistory; }
    void CommitAssistantMessage();

private:
    // Event handlers for raw CC subprocess events
    void OnCCLine( wxThreadEvent& aEvent );
    void OnCCExit( wxThreadEvent& aEvent );
    void OnCCError( wxThreadEvent& aEvent );

    // NDJSON parsing
    void ParseLine( const std::string& aLine );
    void HandleStreamEvent( const nlohmann::json& aEvent );
    void HandleAssistantMessage( const nlohmann::json& aMsg );
    void HandleUserMessage( const nlohmann::json& aMsg );
    void HandleResultMessage( const nlohmann::json& aMsg );

    // Content block tracking
    void HandleContentBlockStart( const nlohmann::json& aEvent );
    void HandleContentBlockDelta( const nlohmann::json& aEvent );
    void HandleContentBlockStop( const nlohmann::json& aEvent );

    // State reset
    void ResetTurnState();

    // MCP config helper
    std::string GenerateMcpConfig();
    static bool IsAuthError( const std::string& aStderr );

    wxEvtHandler*                  m_eventSink;
    std::unique_ptr<CC_SUBPROCESS> m_subprocess;
    std::string                    m_workingDir;
    std::string                    m_apiSocketPath;
    std::string                    m_pythonPath;
    std::string                    m_mcpConfigPath;  // Generated temp file path

    // Session state
    std::string m_sessionId;
    bool        m_busy = false;
    bool        m_intentionalStop = false;  // Suppress error on intentional process kill
    std::string m_lastStderrLine;             // Last stderr line (for exit error context)
    std::string m_stderrBuffer;               // Accumulated stderr (for auth error detection)
    int         m_ccTurnCount = 0;            // Turns in current CC subprocess (for context injection)

    // Accumulation state for current assistant turn
    std::string m_currentResponse;       // Accumulated text content
    wxString    m_thinkingContent;        // Accumulated thinking text
    int         m_thinkingIndex = 0;      // Global thinking block counter
    bool        m_inThinking = false;     // Currently inside a thinking block

    // Content block tracking (keyed by block index from stream events)
    enum class BlockType { TEXT, THINKING, TOOL_USE, UNKNOWN };

    struct ContentBlock
    {
        BlockType   type = BlockType::UNKNOWN;
        int         index = -1;
        std::string toolId;
        std::string toolName;      // Raw name (e.g. mcp__zeo__check_status)
        std::string displayName;   // Cleaned name (e.g. check_status)
        std::string toolInput;     // Accumulated JSON input string
    };

    std::map<int, ContentBlock> m_activeBlocks;

    // Chat history (Anthropic API message format, same as Zeo agent)
    nlohmann::json m_chatHistory = nlohmann::json::array();
    nlohmann::json m_currentAssistantContent = nlohmann::json::array();  // Content blocks for current msg

    // Tool tracking across turns
    int m_toolResultCounter = 0;
    std::vector<std::string> m_pendingToolIds;    // Tool IDs awaiting results
    std::map<std::string, std::string> m_pendingToolNames;  // toolId → display name
};

#endif // CC_CONTROLLER_H
