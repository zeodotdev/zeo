#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

class TOOL_HANDLER;

/**
 * Registry for direct file tools (sch_*, pcb_*).
 * This factory creates and manages tool handlers for direct KiCad file manipulation.
 */
class TOOL_REGISTRY
{
public:
    /**
     * Get the singleton instance.
     */
    static TOOL_REGISTRY& Instance();

    /**
     * Check if the given tool name is handled by a registered direct tool handler.
     * @param aToolName The name of the tool to check.
     * @return true if a handler exists for this tool.
     */
    bool HasHandler( const std::string& aToolName ) const;

    /**
     * Execute a direct tool.
     * @param aToolName The name of the tool to execute.
     * @param aInput The JSON input parameters for the tool.
     * @return The result string from tool execution, or error string starting with "Error:".
     */
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput );

    /**
     * Generate a human-readable description for a tool call.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return A human-readable description string.
     */
    std::string GetDescription( const std::string& aToolName, const nlohmann::json& aInput ) const;

    /**
     * Set the project path for path validation in all handlers.
     * @param aPath The absolute path to the project directory.
     */
    void SetProjectPath( const std::string& aPath );

    /**
     * Check if a tool requires IPC (run_shell) execution.
     * @param aToolName The name of the tool to check.
     * @return true if the tool requires IPC execution.
     */
    bool RequiresIPC( const std::string& aToolName ) const;

    /**
     * Get the IPC command string for a tool.
     * @param aToolName The name of the tool.
     * @param aInput The tool input parameters as JSON.
     * @return The command string to execute via run_shell.
     */
    std::string GetIPCCommand( const std::string& aToolName, const nlohmann::json& aInput ) const;

    /**
     * Set whether the schematic editor is currently open.
     * When open, file-based write operations are blocked to prevent data conflicts.
     * @param aOpen true if the schematic editor is open.
     */
    void SetSchematicEditorOpen( bool aOpen );

    /**
     * Set whether the PCB editor is currently open.
     * When open, file-based write operations are blocked to prevent data conflicts.
     * @param aOpen true if the PCB editor is open.
     */
    void SetPcbEditorOpen( bool aOpen );

private:
    TOOL_REGISTRY();
    ~TOOL_REGISTRY() = default;

    // Delete copy and move constructors
    TOOL_REGISTRY( const TOOL_REGISTRY& ) = delete;
    TOOL_REGISTRY& operator=( const TOOL_REGISTRY& ) = delete;

    std::vector<std::unique_ptr<TOOL_HANDLER>> m_handlers;
};

#endif // TOOL_REGISTRY_H
