#ifndef PYTHON_TOOL_HANDLER_H
#define PYTHON_TOOL_HANDLER_H

#include "../tool_handler.h"
#include <functional>
#include <string>
#include <unordered_map>

/**
 * Generic handler for all IPC-based Python script tools.
 *
 * Each tool maps to a single Python script file loaded from disk at startup.
 * The handler provides GetToolNames / RequiresIPC / GetIPCCommand / GetDescription
 * for every registered tool, eliminating the need for per-tool C++ handler classes.
 *
 * Tools that do NOT follow the simple "run_shell <app> <preamble><script>" pattern
 * (e.g. screenshot_handler, pcb_autoroute_handler) remain as separate classes.
 */
class PYTHON_TOOL_HANDLER : public TOOL_HANDLER
{
public:
    PYTHON_TOOL_HANDLER();

    std::vector<std::string> GetToolNames() const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;
    bool        RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

    /**
     * Locate the python/ scripts directory.
     * On macOS: Contents/SharedSupport/agent/python/ relative to the executable.
     * On Linux: <KICAD_DATA>/agent/python/.
     * Falls back to AGENT_PYTHON_DIR env var (for development/testing).
     */
    static std::string FindPythonDir();

private:
    using DescribeFn = std::function<std::string( const nlohmann::json& )>;

    struct ToolEntry
    {
        std::string app;        // "sch" or "pcb"
        std::string script;     // Python script content (loaded from disk)
        DescribeFn  describe;   // human-readable description generator
    };

    void Register( const std::string& aToolName, const std::string& aApp,
                   const std::string& aScriptPath, DescribeFn aDescribe );

    /**
     * Read a file from disk and return its contents.
     * @return File contents, or empty string on failure.
     */
    static std::string ReadFile( const std::string& aPath );

    /**
     * Build the full IPC command string: "run_shell <app> <TOOL_ARGS preamble><script>"
     */
    static std::string BuildIPCCommand( const std::string& aApp,
                                         const nlohmann::json& aInput,
                                         const std::string& aScript );

    std::string m_pythonDir;    // resolved path to the python/ scripts directory
    std::string m_preamble;     // cached content of common/preamble.py
    std::string m_bbox;         // cached content of common/bbox.py

    std::unordered_map<std::string, ToolEntry> m_tools;
};

#endif // PYTHON_TOOL_HANDLER_H
