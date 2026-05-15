#ifndef FOOTPRINT_GENERATOR_H
#define FOOTPRINT_GENERATOR_H

#include "tools/tool_handler.h"
#include "../agent_events.h"
#include <string>
#include <vector>

class AGENT_AUTH;
class wxEvtHandler;

/**
 * Generates KiCad PCB footprints (.kicad_mod) from extracted datasheet data.
 *
 * Workflow:
 * 1. Try to match extracted package dimensions to an existing KiCad library footprint
 * 2. If no match, generate a custom footprint from package specs
 * 3. Write to project-local .pretty library
 *
 * Runs async since extraction can take 30-60s.
 */
class FOOTPRINT_GENERATOR : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "generate_footprint" }; }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }

    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

    /**
     * Full generate workflow (runs on background thread).
     * Public so symbol_generator can chain into footprint generation.
     * Returns JSON string with status/lib_id/error.
     */
    std::string DoGenerate( const std::string& aPartNumber,
                            const std::string& aManufacturer,
                            const std::string& aDatasheetUrl,
                            const std::string& aComponentId,
                            const std::string& aLibraryName,
                            const std::string& aProjectPath,
                            bool aForce = false );

private:
    struct PackageData
    {
        std::string package_type;      // "QFN-64", "SOIC-8", etc.
        int         pin_count = 0;
        double      body_width_mm = 0; // D dimension
        double      body_length_mm = 0;// E dimension
        double      pin_pitch_mm = 0;  // e dimension
        bool        has_thermal_pad = false;
        double      ep_width_mm = 0;   // exposed pad width (E2)
        double      ep_length_mm = 0;  // exposed pad length (D2)
        double      terminal_width_mm = 0;  // lead width (b)
        double      terminal_length_mm = 0; // lead length (L)
        double      pad_width_mm = 0;  // recommended land pattern pad width
        double      pad_length_mm = 0; // recommended land pattern pad length
    };

    struct ComponentData
    {
        std::string part_number;
        std::string manufacturer;
        std::string description;
        std::string datasheet_url;
        PackageData package;
    };

    /**
     * Try to match extracted package dimensions against the standard KiCad
     * footprint libraries. Returns a fully-qualified lib_id if found.
     */
    std::string MatchStandardFootprint( const PackageData& aPkg );

    /**
     * Fetch component + package data from Supabase.
     */
    bool FetchComponentData( const std::string& aPartNumber,
                             const std::string& aManufacturer,
                             ComponentData& aData,
                             const std::string& aComponentId = "",
                             const std::string& aDatasheetUrl = "" );

    /**
     * Call the extract-datasheet edge function in sync mode.
     */
    std::string TriggerExtractionSync( const std::string& aPartNumber,
                                        const std::string& aManufacturer,
                                        const std::string& aDatasheetUrl );

    /**
     * Generate .kicad_mod S-expression content for a QFN/DFN package.
     */
    std::string GenerateQfnFootprint( const PackageData& aPkg,
                                       const std::string& aName );

    /**
     * Build the canonical footprint name from package dimensions.
     * e.g., "QFN-64-1EP_9x9mm_P0.5mm_EP7.15x7.15mm"
     */
    std::string BuildFootprintName( const PackageData& aPkg );

    /**
     * Ensure fp-lib-table has an entry for our project library.
     */
    void EnsureFpLibTableEntry( const std::string& aProjectPath,
                                 const std::string& aLibraryName );
};

#endif // FOOTPRINT_GENERATOR_H
