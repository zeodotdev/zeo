#ifndef SCH_TOOL_HANDLER_H
#define SCH_TOOL_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic operations (sch_* tools).
 * All tools query live editor state via kipy IPC.
 */
class SCH_TOOL_HANDLER : public TOOL_HANDLER
{
public:
    SCH_TOOL_HANDLER() = default;
    ~SCH_TOOL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    /**
     * Generate IPC Python code for sch_read_section.
     * Queries specific sections of the live schematic via kipy API.
     */
    std::string GenerateReadSectionIPCCommand( const nlohmann::json& aInput ) const;
};

#endif // SCH_TOOL_HANDLER_H
