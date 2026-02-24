#ifndef DATASHEET_HANDLER_H
#define DATASHEET_HANDLER_H

#include "tools/tool_handler.h"
#include <string>

class AGENT_AUTH;

/**
 * Handles datasheet extraction integration with Supabase Edge Functions.
 *
 * Provides:
 * - `datasheet_query` tool: queries extraction data for a component
 * - Background extraction trigger: fires after tools that expose symbol datasheet URLs
 */
class DATASHEET_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "datasheet_query" }; }
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    /**
     * Scan a tool result for symbols with datasheet URLs and trigger background
     * extraction for each. Works with both sch_add and sch_get_summary results.
     *
     * @param aToolName   The tool that was executed
     * @param aToolResult The tool result string (JSON)
     */
    static void MaybeTriggerExtraction( const std::string& aToolName,
                                        const std::string& aToolResult );

private:
    std::string QueryExtractionData( const std::string& aPartNumber,
                                     const std::string& aManufacturer );

    static void TriggerExtractionAsync( const std::string& aPartNumber,
                                        const std::string& aManufacturer,
                                        const std::string& aDatasheetUrl );

    /**
     * Derive a part number and manufacturer from a lib_id string.
     * e.g., "SiliconLabs:CP2102N" → part="CP2102N", mfg="SiliconLabs"
     *       "Device:R" → part="R", mfg=""
     */
    static void ParseLibId( const std::string& aLibId,
                            std::string& aPartNumber,
                            std::string& aManufacturer );
};

#endif // DATASHEET_HANDLER_H
