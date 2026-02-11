#ifndef SCH_CRUD_HANDLER_H
#define SCH_CRUD_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for schematic CRUD and navigation operations via kipy IPC.
 * Handles: sch_add, sch_update, sch_delete, sch_open_sheet, sch_connect_to_power, sch_add_batch
 *
 * All CRUD operations accept arrays (elements, updates, targets) for batch processing.
 * Single-item operations are just arrays with one element.
 *
 * These tools work on the LIVE schematic through the kipy Python API,
 * allowing real-time creation, modification, deletion, and navigation.
 */
class SCH_CRUD_HANDLER : public TOOL_HANDLER
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
    /**
     * Generate Python code for sch_add operation.
     */
    std::string GenerateAddCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_update operation (single item - legacy).
     */
    std::string GenerateUpdateCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_update operation (batch - processes updates array).
     */
    std::string GenerateUpdateBatchCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_delete operation.
     */
    std::string GenerateDeleteCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_batch_delete operation.
     */
    std::string GenerateBatchDeleteCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_open_sheet operation.
     */
    std::string GenerateOpenSheetCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_connect_to_power operation.
     * Connects a symbol pin to a power rail in one call.
     */
    std::string GenerateConnectToPowerCode( const nlohmann::json& aInput ) const;

    /**
     * Generate Python code for sch_add_batch operation.
     * Adds multiple elements in a single batch call for efficiency.
     */
    std::string GenerateAddBatchCode( const nlohmann::json& aInput ) const;

    /**
     * Generate common Python code header for file fallback operations.
     * Includes imports, file loading, and save function.
     */
    std::string GenerateFileFallbackHeader() const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;

    /**
     * Snap a position value (in mm) to the nearest grid point.
     * Default grid is 1.27mm (50 mil), the standard KiCad schematic grid.
     */
    static double SnapToGrid( double aMm, double aGrid = 1.27 );

    /**
     * Convert mm to nanometers (KiCad internal units).
     */
    std::string MmToNm( double aMm ) const;

    /**
     * Generate Python code preamble to refresh document specifier.
     * This handles close/reopen cycles where the document UUID changes.
     */
    std::string GenerateRefreshPreamble() const;

    /**
     * Generate Python code to check if editor is still open when IPC fails.
     * Used to prevent file fallback when editor is open (would cause data conflicts).
     */
    std::string GenerateEditorOpenCheck() const;
};

#endif // SCH_CRUD_HANDLER_H
