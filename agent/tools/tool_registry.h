#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

#include "llm_tool.h"
class AGENT_AUTH;
class TOOL_HANDLER;
class wxEvtHandler;

/**
 * Singleton registry that owns all TOOL_HANDLER instances and dispatches
 * tool calls to the appropriate handler via a tool-name → handler map.
 *
 * Also holds shared state (editor open flags, IPC send function)
 * that individual handlers can read via the singleton when executing.
 */
class TOOL_REGISTRY
{
public:
    using SendRequestFn = std::function<std::string( int, const std::string& )>;

    static TOOL_REGISTRY& Instance();

    // --- Tool dispatch ---

    /**
     * Collect dynamic tool schemas from all handlers (e.g. MCP-fetched schemas).
     */
    std::vector<LLM_TOOL> GetDynamicTools() const;

    bool        HasHandler( const std::string& aToolName ) const;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput );
    std::string GetDescription( const std::string& aToolName, const nlohmann::json& aInput ) const;
    bool        RequiresIPC( const std::string& aToolName ) const;
    std::string GetIPCCommand( const std::string& aToolName, const nlohmann::json& aInput ) const;
    bool        IsAsync( const std::string& aToolName ) const;
    void        ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                              const std::string& aToolUseId, wxEvtHandler* aEventHandler );

    /**
     * Execute a tool synchronously.  Routes IPC tools through the terminal frame
     * via m_sendRequestFn, and dispatches direct tools to the handler.
     * Also handles the built-in run_terminal tool.
     * Caller must call SetSendRequestFn() before invoking this.
     */
    std::string ExecuteToolSync( const std::string& aToolName, const nlohmann::json& aInput );

    // --- Shared state (set by chat_controller / agent_frame, read by handlers) ---

    void SetSchematicEditorOpen( bool aOpen )             { m_schematicEditorOpen = aOpen; }
    bool IsSchematicEditorOpen() const                    { return m_schematicEditorOpen; }

    void SetPcbEditorOpen( bool aOpen )                   { m_pcbEditorOpen = aOpen; }
    bool IsPcbEditorOpen() const                          { return m_pcbEditorOpen; }

    void SetProjectPath( const std::string& aPath )       { m_projectPath = aPath; }
    const std::string& GetProjectPath() const             { return m_projectPath; }

    void SetProjectName( const std::string& aName )       { m_projectName = aName; }
    const std::string& GetProjectName() const             { return m_projectName; }

    void SetOpenEditorFiles( std::vector<std::string> aF ) { m_openEditorFiles = std::move( aF ); }
    const std::vector<std::string>& GetOpenEditorFiles() const { return m_openEditorFiles; }

    void SetSendRequestFn( SendRequestFn aFn )            { m_sendRequestFn = std::move( aFn ); }
    const SendRequestFn& GetSendRequestFn() const         { return m_sendRequestFn; }

    void SetAuth( AGENT_AUTH* aAuth )                      { m_auth = aAuth; }
    AGENT_AUTH* GetAuth() const                            { return m_auth; }

    void SetSupabaseUrl( const std::string& aUrl )         { m_supabaseUrl = aUrl; }
    const std::string& GetSupabaseUrl() const              { return m_supabaseUrl; }

    void SetSupabaseAnonKey( const std::string& aKey )     { m_supabaseAnonKey = aKey; }
    const std::string& GetSupabaseAnonKey() const          { return m_supabaseAnonKey; }

    using ReloadSymbolLibFn = std::function<void( const std::string& )>;
    void SetReloadSymbolLibFn( ReloadSymbolLibFn aFn )     { m_reloadSymbolLibFn = std::move( aFn ); }
    void ReloadSymbolLib( const std::string& aLibName ) const
    {
        if( m_reloadSymbolLibFn )
            m_reloadSymbolLibFn( aLibName );
    }

    using ReloadFootprintLibFn = std::function<void( const std::string& )>;
    void SetReloadFootprintLibFn( ReloadFootprintLibFn aFn ) { m_reloadFootprintLibFn = std::move( aFn ); }
    void ReloadFootprintLib( const std::string& aLibName ) const
    {
        if( m_reloadFootprintLibFn )
            m_reloadFootprintLibFn( aLibName );
    }

private:
    TOOL_REGISTRY();
    ~TOOL_REGISTRY() = default;

    TOOL_REGISTRY( const TOOL_REGISTRY& ) = delete;
    TOOL_REGISTRY& operator=( const TOOL_REGISTRY& ) = delete;

    /**
     * Register a handler: queries GetToolNames() and maps each name to the handler.
     */
    void Register( std::unique_ptr<TOOL_HANDLER> aHandler );

    TOOL_HANDLER* FindHandler( const std::string& aToolName ) const;

    std::vector<std::unique_ptr<TOOL_HANDLER>>       m_handlers;  // owns the handlers
    std::unordered_map<std::string, TOOL_HANDLER*>   m_toolMap;   // tool name → handler (non-owning)

    // Shared state
    bool          m_schematicEditorOpen = false;
    bool          m_pcbEditorOpen = false;
    std::string   m_projectPath;
    std::string   m_projectName;
    std::vector<std::string> m_openEditorFiles;
    SendRequestFn m_sendRequestFn;

    // Auth and Supabase config (for datasheet extraction)
    AGENT_AUTH*    m_auth = nullptr;
    std::string   m_supabaseUrl;
    std::string   m_supabaseAnonKey;
    ReloadSymbolLibFn m_reloadSymbolLibFn;
    ReloadFootprintLibFn m_reloadFootprintLibFn;
};

#endif // TOOL_REGISTRY_H
