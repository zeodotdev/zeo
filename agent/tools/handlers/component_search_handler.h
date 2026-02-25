#ifndef COMPONENT_SEARCH_HANDLER_H
#define COMPONENT_SEARCH_HANDLER_H

#include "tools/tool_handler.h"
#include <string>

class wxEvtHandler;

/**
 * Gateway handler for the PCBParts MCP server (https://pcbparts.dev/mcp).
 *
 * Proxies a single `component_search` tool to 17 underlying MCP tools across
 * JLCPCB, Mouser, and DigiKey suppliers. The action + supplier combination is
 * mapped to the appropriate MCP tool name at execution time.
 */
class COMPONENT_SEARCH_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "component_search" }; }
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }
    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

private:
    /**
     * Map an action + supplier pair to the MCP tool name.
     * Returns empty string if the combination is not supported.
     */
    std::string MapToMCPTool( const std::string& aAction, const std::string& aSupplier ) const;

    /**
     * Build JSON-RPC 2.0 request, POST to pcbparts.dev/mcp, parse SSE response.
     */
    std::string CallMCP( const std::string& aMcpTool, const nlohmann::json& aArgs );
};

#endif // COMPONENT_SEARCH_HANDLER_H
