#ifndef FILE_IO_HANDLER_H
#define FILE_IO_HANDLER_H

#include "tools/tool_handler.h"

/**
 * Handler for file create/write/read operations on text files.
 * Supports: file_write (create or overwrite), file_read (read contents).
 * Intended for non-schematic/PCB files like .txt, .md, .py, .json, etc.
 */
class FILE_IO_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override
    {
        return { "file_write", "file_read" };
    }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

private:
    std::string ExecuteWrite( const nlohmann::json& aInput );
    std::string ExecuteRead( const nlohmann::json& aInput );

    /**
     * Validate that the file path is safe to write/read.
     * Returns empty string if valid, or an error message if not.
     */
    std::string ValidatePath( const std::string& aPath, bool aIsWrite ) const;
};

#endif // FILE_IO_HANDLER_H
