#ifndef PCB_SETUP_HANDLER_H
#define PCB_SETUP_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for PCB board setup operations (pcb_setup).
 * Provides read/write access to board settings: stackup, design rules,
 * text/graphics defaults, grid, DRC severities, net classes, title block, origins.
 * Executes via kipy IPC (run_shell pcb) rather than direct file access.
 */
class PCB_SETUP_HANDLER : public TOOL_HANDLER
{
public:
    PCB_SETUP_HANDLER() = default;
    ~PCB_SETUP_HANDLER() override = default;

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
     * Generate Python code for GET action - retrieves all board settings.
     */
    std::string GenerateGetCode() const;

    /**
     * Generate Python code for SET action - updates provided settings.
     */
    std::string GenerateSetCode( const nlohmann::json& aInput ) const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // PCB_SETUP_HANDLER_H
