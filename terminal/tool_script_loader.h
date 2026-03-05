#ifndef TOOL_SCRIPT_LOADER_H
#define TOOL_SCRIPT_LOADER_H

#include <string>
#include <unordered_map>

/**
 * Loads tool_manifest.json and Python scripts from disk at construction time.
 * Provides BuildCommand() to assemble the full "run_shell <app> <initCode>" string
 * for each registered tool, and GetManifestJson() for tool schema discovery.
 */
class TOOL_SCRIPT_LOADER
{
public:
    TOOL_SCRIPT_LOADER();

    bool HasTool( const std::string& aName ) const;
    std::string GetApp( const std::string& aName ) const;
    bool IsReadOnly( const std::string& aName ) const;
    std::string BuildCommand( const std::string& aName, const std::string& aArgsJson ) const;

    /**
     * Build just the Python code for a tool (without "run_shell <app>" prefix).
     * Returns the TOOL_ARGS assignment + preamble + optional bbox + tool script.
     * Returns empty string if the tool is not found.
     */
    std::string BuildPythonCode( const std::string& aName, const std::string& aArgsJson ) const;

    const std::string& GetManifestJson() const;

private:
    struct ToolEntry
    {
        std::string app;
        std::string script;    // Loaded script content
        bool        needsBbox;
        bool        readOnly;
    };

    static std::string FindPythonDir();
    static std::string ReadFile( const std::string& aPath );

    std::string m_pythonDir;
    std::string m_preamble;
    std::string m_bbox;
    std::string m_manifestJson;
    std::unordered_map<std::string, ToolEntry> m_tools;
};

#endif // TOOL_SCRIPT_LOADER_H
