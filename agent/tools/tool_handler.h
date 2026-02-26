#ifndef TOOL_HANDLER_H
#define TOOL_HANDLER_H

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct LLM_TOOL;
class wxEvtHandler;

/**
 * Base interface for tool handlers.
 * Each tool handler is responsible for executing a specific tool or category of tools.
 */
class TOOL_HANDLER
{
public:
    virtual ~TOOL_HANDLER() = default;

    /**
     * Return the tool names this handler supports.
     * Called once at registration time to build the dispatch map.
     */
    virtual std::vector<std::string> GetToolNames() const = 0;

    /**
     * Execute the tool with the given input parameters.
     */
    virtual std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) = 0;

    /**
     * Generate a human-readable description for a tool call.
     */
    virtual std::string GetDescription( const std::string& aToolName,
                                        const nlohmann::json& aInput ) const = 0;

    /**
     * Check if this tool requires IPC (run_shell) execution rather than direct execution.
     */
    virtual bool RequiresIPC( const std::string& aToolName ) const { return false; }

    /**
     * Get the IPC command string for tools that require IPC execution.
     * Only called if RequiresIPC() returns true.
     */
    virtual std::string GetIPCCommand( const std::string& aToolName,
                                        const nlohmann::json& aInput ) const { return ""; }

    /**
     * Check if this tool executes asynchronously.
     */
    virtual bool IsAsync( const std::string& aToolName ) const { return false; }

    /**
     * Start asynchronous execution of the tool.
     * Only called if IsAsync() returns true. The tool should spawn a background
     * thread and post EVT_TOOL_EXECUTION_COMPLETE when finished.
     */
    virtual void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                               const std::string& aToolUseId, wxEvtHandler* aEventHandler )
    {
        // Default implementation does nothing - override in async handlers
    }

    /**
     * Return tool schemas fetched dynamically (e.g. from an MCP server).
     * Called before each LLM request to merge into the tool list.
     * Default returns empty — override in handlers that provide dynamic schemas.
     */
    virtual std::vector<LLM_TOOL> GetDynamicTools() const { return {}; }
};

#endif // TOOL_HANDLER_H
