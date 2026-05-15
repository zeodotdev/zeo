#ifndef COMPONENT_SEARCH_HANDLER_H
#define COMPONENT_SEARCH_HANDLER_H

#include "tools/tool_handler.h"
#include <mutex>
#include <string>

class wxEvtHandler;

/**
 * Handler for PCBParts MCP server tools (https://pcbparts.dev/mcp).
 *
 * Fetches tool schemas from the MCP server at startup via tools/list.
 * GetToolNames() provides a static routing list; schemas come from MCP.
 */
class COMPONENT_SEARCH_HANDLER : public TOOL_HANDLER
{
public:
    COMPONENT_SEARCH_HANDLER();

    std::vector<std::string> GetToolNames() const override
    {
        return {
            "jlc_search", "jlc_list_attributes", "jlc_get_part", "jlc_find_alternatives",
            "jlc_validate_bom", "jlc_get_pinout", "jlc_export_bom",
            "mouser_search", "mouser_get_part",
            "digikey_search", "digikey_get_part",
            "cse_search", "cse_get_kicad"
        };
    }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }
    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

    /**
     * Return MCP tool schemas fetched from pcbparts.dev/mcp at startup.
     * Only returns schemas for tools listed in GetToolNames().
     */
    std::vector<LLM_TOOL> GetDynamicTools() const override;

private:
    /**
     * Build JSON-RPC 2.0 request, POST to pcbparts.dev/mcp, parse SSE response.
     */
    std::string CallMCP( const std::string& aMcpTool, const nlohmann::json& aArgs );

    /**
     * Fetch tool schemas from the MCP server (background thread).
     */
    void FetchMcpSchemas();

    mutable std::mutex      m_mcpMutex;
    std::vector<LLM_TOOL>   m_mcpTools;   ///< Schemas fetched from MCP
    bool                    m_mcpFetched = false;
};

#endif // COMPONENT_SEARCH_HANDLER_H
