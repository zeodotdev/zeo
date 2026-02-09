#ifndef SCH_LIB_SYMBOL_HANDLER_H
#define SCH_LIB_SYMBOL_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for querying library symbols (sch_get_lib_symbol).
 * Supports exact match, wildcard patterns, and regex patterns.
 * Returns symbol information including pin positions for wiring.
 * Executes via kipy IPC (run_shell).
 */
class SCH_LIB_SYMBOL_HANDLER : public TOOL_HANDLER
{
public:
    SCH_LIB_SYMBOL_HANDLER() = default;
    ~SCH_LIB_SYMBOL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;
};

#endif // SCH_LIB_SYMBOL_HANDLER_H
