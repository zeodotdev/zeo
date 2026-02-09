#ifndef SCH_SIM_HANDLER_H
#define SCH_SIM_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic simulation operations (sch_run_simulation).
 * Requires KiCad's schematic editor to be open with a document loaded.
 * Executes via kipy IPC (run_shell) rather than direct file access.
 */
class SCH_SIM_HANDLER : public TOOL_HANDLER
{
public:
    SCH_SIM_HANDLER() = default;
    ~SCH_SIM_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    /**
     * Generate Python code for sch_run_simulation operation.
     */
    std::string GenerateRunSimCode( const nlohmann::json& aInput ) const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // SCH_SIM_HANDLER_H
