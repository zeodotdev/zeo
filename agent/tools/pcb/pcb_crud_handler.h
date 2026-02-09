#ifndef PCB_CRUD_HANDLER_H
#define PCB_CRUD_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for PCB IPC-based operations via kipy.
 * Handles: pcb_get_summary, pcb_read_section, pcb_run_drc, pcb_set_outline,
 *          pcb_sync_schematic, pcb_place, pcb_add, pcb_update, pcb_delete,
 *          pcb_get_pads, pcb_get_footprint, pcb_route, pcb_get_nets, pcb_export
 */
class PCB_CRUD_HANDLER : public TOOL_HANDLER
{
public:
    bool CanHandle( const std::string& aToolName ) const override;

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;

    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool RequiresIPC( const std::string& aToolName ) const override;

    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    // Query operations
    std::string GenerateGetSummaryCode( const nlohmann::json& aInput ) const;
    std::string GenerateReadSectionCode( const nlohmann::json& aInput ) const;
    std::string GenerateValidateCode( const nlohmann::json& aInput ) const;
    std::string GenerateRunDrcCode( const nlohmann::json& aInput ) const;
    std::string GenerateGetPadsCode( const nlohmann::json& aInput ) const;
    std::string GenerateGetFootprintCode( const nlohmann::json& aInput ) const;
    std::string GenerateGetNetsCode( const nlohmann::json& aInput ) const;

    // Modification operations
    std::string GenerateSetOutlineCode( const nlohmann::json& aInput ) const;
    std::string GenerateSyncSchematicCode( const nlohmann::json& aInput ) const;
    std::string GeneratePlaceCode( const nlohmann::json& aInput ) const;
    std::string GenerateAddBatchCode( const nlohmann::json& aInput ) const;
    std::string GenerateUpdateBatchCode( const nlohmann::json& aInput ) const;
    std::string GenerateDeleteBatchCode( const nlohmann::json& aInput ) const;
    std::string GenerateRouteCode( const nlohmann::json& aInput ) const;
    std::string GenerateExportCode( const nlohmann::json& aInput ) const;

    // Helpers
    std::string EscapePythonString( const std::string& aStr ) const;
    std::string MmToNm( double aMm ) const;
    std::string GenerateRefToFootprintMap() const;
};

#endif // PCB_CRUD_HANDLER_H
