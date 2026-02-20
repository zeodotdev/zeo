#ifndef SCH_COMPANION_HANDLER_H
#define SCH_COMPANION_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for placing companion components adjacent to IC pins (sch_place_companions).
 *
 * Companion circuits are small supporting parts (decoupling caps, pull-up/down resistors,
 * termination resistors, filter caps, LED indicators) that wire directly to specific IC pins.
 *
 * The tool calculates optimal positions based on IC pin geometry and orientation:
 * - Gets IC pin position and escape direction via get_transformed_pin_position()
 * - Places companion symbol adjacent to pin (offset by N grid units in escape direction)
 * - Draws short wire stub from IC pin to companion pin 1
 * - Adds power symbols or text labels at companion terminals as specified
 *
 * This enables deterministic, overlap-free placement of IC support circuitry.
 */
class SCH_COMPANION_HANDLER : public TOOL_HANDLER
{
public:
    SCH_COMPANION_HANDLER() = default;
    ~SCH_COMPANION_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    std::string GeneratePlaceCompanionsCode( const nlohmann::json& aInput ) const;
    void GenerateChainCode( std::ostringstream& aCode,
                            const nlohmann::json& aChainItems,
                            const std::string& aParentSymVar,
                            const std::string& aEscapeDirVar,
                            const std::string& aPrefix,
                            int& aGlobalIndex,
                            bool aParentReversed ) const;
    std::string EscapePythonString( const std::string& aStr ) const;
    static double SnapToGrid( double aMm, double aGrid = 1.27 );
};

#endif // SCH_COMPANION_HANDLER_H
