#ifndef SYMBOL_GENERATOR_H
#define SYMBOL_GENERATOR_H

#include "tools/tool_handler.h"
#include <string>
#include <vector>
#include <map>

class AGENT_AUTH;

/**
 * Generates KiCad schematic symbols (.kicad_sym) from extracted datasheet data.
 *
 * Uses pin data (pin_name, pin_number, pin_type, function_group) from the
 * component database to deterministically create symbols with:
 * - Pins grouped by function_group
 * - Pin sides assigned by pin_type (power top, ground bottom, inputs left, outputs right)
 * - Proper symbol body sizing
 * - Standard KiCad properties (Reference, Value, Footprint, Datasheet)
 */
class SYMBOL_GENERATOR : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "generate_symbol" }; }
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

private:
    /**
     * Pin data parsed from the database query result.
     */
    struct PinData
    {
        std::string number;
        std::string name;
        std::string type;           // power_in, ground, input, output, bidirectional, etc.
        std::string function_group; // Power, GND, SPI0, UART0, GPIO, etc.
    };

    /**
     * Component metadata for symbol generation.
     */
    struct ComponentData
    {
        std::string part_number;
        std::string manufacturer;
        std::string description;
        std::string category;
        std::string datasheet_url;
        std::vector<PinData> pins;
        std::string footprint;      // First recommended footprint (if any)
    };

    /**
     * Fetch component data (including pins) from Supabase.
     */
    bool FetchComponentData( const std::string& aPartNumber,
                             const std::string& aManufacturer,
                             ComponentData& aData );

    /**
     * Generate the .kicad_sym file content from component data.
     */
    std::string GenerateSymbolContent( const ComponentData& aData,
                                        const std::string& aLibName );

    /**
     * Assign pins to sides (left/right/top/bottom) based on pin_type and function_group.
     */
    struct PinLayout
    {
        std::vector<const PinData*> top;    // power_in
        std::vector<const PinData*> bottom; // ground
        std::vector<const PinData*> left;   // inputs, bidirectional
        std::vector<const PinData*> right;  // outputs, open_drain, open_collector
    };

    PinLayout AssignPinSides( const std::vector<PinData>& aPins );

    /**
     * Map pin_type string to KiCad electrical type keyword.
     */
    static std::string PinTypeToKiCad( const std::string& aType );

    /**
     * Get the KiCad reference designator for a component category.
     */
    static std::string CategoryToReference( const std::string& aCategory );
};

#endif // SYMBOL_GENERATOR_H
