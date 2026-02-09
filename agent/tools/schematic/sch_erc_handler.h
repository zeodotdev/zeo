#ifndef SCH_ERC_HANDLER_H
#define SCH_ERC_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic ERC operations (sch_run_erc).
 * Requires KiCad's schematic editor to be open with a document loaded.
 * Executes via kipy IPC (run_shell) rather than direct file access.
 */
class SCH_ERC_HANDLER : public TOOL_HANDLER
{
public:
    SCH_ERC_HANDLER() = default;
    ~SCH_ERC_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;
};

#endif // SCH_ERC_HANDLER_H
