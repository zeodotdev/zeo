#ifndef SCH_SETUP_HANDLER_H
#define SCH_SETUP_HANDLER_H

#include "../tool_handler.h"
#include <nlohmann/json.hpp>
#include <string>

/**
 * Tool handler for schematic setup/formatting operations via kipy IPC.
 * Handles: sch_setup
 *
 * This tool allows reading and modifying schematic document settings including:
 * - Page size and title block
 * - Text/symbol defaults
 * - Connection settings (junction size, hop-over, grid)
 * - Inter-sheet references
 * - Dashed lines settings
 * - Operating-point overlay settings
 *
 * Requires KiCad's schematic editor to be open with a document loaded.
 */
class SCH_SETUP_HANDLER : public TOOL_HANDLER
{
public:
    SCH_SETUP_HANDLER() = default;
    ~SCH_SETUP_HANDLER() override = default;

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
     * Generate Python code for getting all schematic settings.
     */
    std::string GenerateGetCode() const;

    /**
     * Generate Python code for setting schematic settings.
     */
    std::string GenerateSetCode( const nlohmann::json& aInput ) const;

    /**
     * Escape a string for use in Python code.
     */
    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // SCH_SETUP_HANDLER_H
