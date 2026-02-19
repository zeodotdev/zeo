#ifndef SCH_CONNECT_NET_HANDLER_H
#define SCH_CONNECT_NET_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for sch_connect_net — auto-routed wiring tool.
 *
 * Connects multiple component pins on the same net with trunk-and-branch
 * wiring and automatic junctions.  When routing to/from a power symbol
 * (#PWR), automatically tries flipping it 180 degrees if the initial path
 * is awkwardly long (>1.8x Manhattan distance).
 */
class SCH_CONNECT_NET_HANDLER : public TOOL_HANDLER
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
     * Generate Python code for sch_connect_net operation.
     */
    std::string GenerateConnectNetCode( const nlohmann::json& aInput ) const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // SCH_CONNECT_NET_HANDLER_H
