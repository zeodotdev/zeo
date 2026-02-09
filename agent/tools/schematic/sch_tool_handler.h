#ifndef SCH_TOOL_HANDLER_H
#define SCH_TOOL_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for schematic file operations (sch_* tools).
 * Implements direct reading, modification, and writing of .kicad_sch files.
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

    /**
     * sch_get_summary supports IPC for live state queries.
     * Returns true for sch_get_summary to try IPC first.
     */
    bool RequiresIPC( const std::string& aToolName ) const override;

    /**
     * Generate IPC command for sch_get_summary.
     * Queries live schematic state via kipy API.
     */
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

    /**
     * Set the project path for path validation.
     * File write operations will be restricted to this directory.
     */
    void SetProjectPath( const std::string& aPath ) override { m_projectPath = aPath; }

private:
    std::string m_projectPath;  ///< Project directory for path validation
    /**
     * Execute sch_get_summary tool.
     * Returns a JSON summary of the schematic file.
     */
    std::string ExecuteGetSummary( const nlohmann::json& aInput );

    /**
     * Execute sch_read_section tool.
     * Returns raw S-expression text for requested section.
     */
    std::string ExecuteReadSection( const nlohmann::json& aInput );

    /**
     * Execute sch_modify tool.
     * Adds, updates, or deletes elements in the schematic.
     */
    std::string ExecuteModify( const nlohmann::json& aInput );

    /**
     * Execute sch_export_spice_netlist tool.
     * Generates a SPICE netlist from the schematic using kicad-cli.
     */
    std::string ExecuteExportSpiceNetlist( const nlohmann::json& aInput );
};

#endif // SCH_TOOL_HANDLER_H
