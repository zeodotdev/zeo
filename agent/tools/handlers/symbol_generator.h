#ifndef SYMBOL_GENERATOR_H
#define SYMBOL_GENERATOR_H

#include "tools/tool_handler.h"
#include "../agent_events.h"
#include <string>
#include <vector>
#include <map>

class AGENT_AUTH;
class wxEvtHandler;

/**
 * Generates KiCad schematic symbols (.kicad_sym) from extracted datasheet data.
 *
 * Self-contained workflow:
 * 1. Check local .kicad_sym files for existing symbol with matching Datasheet URL
 * 2. Check DB for existing extraction data (by datasheet URL)
 * 3. Auto-trigger extraction if needed (sync, 30-60s)
 * 4. Generate symbol with pins grouped by function and assigned to sides
 *
 * Runs async since extraction can take 30-60s.
 */
class SYMBOL_GENERATOR : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "generate_symbol" }; }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }

    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

private:
    struct PinData
    {
        std::string number;
        std::string name;
        std::string type;           // power_in, ground, input, output, bidirectional, etc.
        std::string function_group; // Power, GND, SPI0, UART0, GPIO, etc.
    };

    struct ComponentData
    {
        std::string part_number;
        std::string manufacturer;
        std::string description;
        std::string category;
        std::string datasheet_url;
        std::vector<PinData> pins;
        std::string footprint;
    };

    /**
     * Scan local .kicad_sym files in the project directory for a symbol whose
     * Datasheet property matches the given URL.
     * Returns the lib_id (e.g. "project:LT8711HE") if found, empty string if not.
     */
    std::string FindLocalSymbolByDatasheet( const std::string& aDatasheetUrl,
                                             const std::string& aProjectPath );

    /**
     * Fetch component data from Supabase by datasheet URL, component ID, or part number.
     */
    bool FetchComponentData( const std::string& aPartNumber,
                             const std::string& aManufacturer,
                             ComponentData& aData,
                             const std::string& aComponentId = "",
                             const std::string& aDatasheetUrl = "" );

    /**
     * Call the extract-datasheet edge function in sync mode.
     * Returns the component_id on success, empty string on failure.
     */
    std::string TriggerExtractionSync( const std::string& aPartNumber,
                                        const std::string& aManufacturer,
                                        const std::string& aDatasheetUrl );

    /**
     * Run the full generate workflow (called from background thread).
     */
    std::string DoGenerate( const std::string& aPartNumber,
                            const std::string& aManufacturer,
                            const std::string& aDatasheetUrl,
                            const std::string& aComponentId,
                            const std::string& aLibraryName,
                            const std::string& aProjectPath );

    std::string GenerateSymbolContent( const ComponentData& aData,
                                        const std::string& aLibName );

    struct PinLayout
    {
        std::vector<const PinData*> top;
        std::vector<const PinData*> bottom;
        std::vector<const PinData*> left;
        std::vector<const PinData*> right;
    };

    PinLayout AssignPinSides( const std::vector<PinData>& aPins );
    static std::string PinTypeToKiCad( const std::string& aType );
    static std::string CategoryToReference( const std::string& aCategory );
};

#endif // SYMBOL_GENERATOR_H
