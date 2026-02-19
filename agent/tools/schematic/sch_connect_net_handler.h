#ifndef SCH_CONNECT_NET_HANDLER_H
#define SCH_CONNECT_NET_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for sch_connect_net and sch_connect_to_power — auto-routed wiring tools.
 *
 * sch_connect_net: connects multiple component pins on the same net
 * with trunk-and-branch wiring and automatic junctions.
 *
 * sch_connect_to_power: connects one or more pins to a power rail,
 * placing power symbols with auto-rotation and auto-routed wires.
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
     * Generate Python code for sch_connect_to_power operation.
     * Places power symbols and auto-routes wires from pins to power.
     */
    std::string GenerateConnectToPowerCode( const nlohmann::json& aInput ) const;

    /**
     * Helper to escape strings for Python code generation.
     */
    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // SCH_CONNECT_NET_HANDLER_H
