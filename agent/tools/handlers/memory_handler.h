#ifndef MEMORY_HANDLER_H
#define MEMORY_HANDLER_H

#include "tools/tool_handler.h"

/**
 * Client-side handler for the Anthropic memory tool (memory_20250818).
 *
 * Implements file-based memory operations (view, create, str_replace, insert,
 * delete, rename) within a project-local "memories" directory. The model
 * automatically uses this tool to persist context across conversations.
 *
 * All paths are restricted to <project_dir>/memories/ to prevent traversal.
 */
class MEMORY_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override
    {
        return { "memory" };
    }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

private:
    std::string ExecuteView( const nlohmann::json& aInput );
    std::string ExecuteCreate( const nlohmann::json& aInput );
    std::string ExecuteStrReplace( const nlohmann::json& aInput );
    std::string ExecuteInsert( const nlohmann::json& aInput );
    std::string ExecuteDelete( const nlohmann::json& aInput );
    std::string ExecuteRename( const nlohmann::json& aInput );

    /**
     * Resolve a virtual /memories/... path to a real filesystem path.
     * Returns empty string and sets aError if the path is invalid or escapes
     * the memories directory.
     */
    std::string ResolvePath( const std::string& aVirtualPath, std::string& aError ) const;

    /**
     * Return the absolute path to the memories directory for the current project.
     */
    std::string GetMemoriesDir() const;
};

#endif // MEMORY_HANDLER_H
