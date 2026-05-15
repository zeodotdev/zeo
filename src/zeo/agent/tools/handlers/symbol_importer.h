#ifndef SYMBOL_IMPORTER_H
#define SYMBOL_IMPORTER_H

#include "tools/tool_handler.h"
#include <string>

class wxEvtHandler;

/**
 * Imports a pre-built KiCad symbol + footprint into the project library.
 *
 * Designed for the output of component_search { action: "get_kicad" }
 * (cse_get_kicad MCP tool), which always returns both:
 *   - kicad_symbol   — raw .kicad_sym S-expression (ComponentSearchEngine/SamacSys)
 *   - kicad_footprint — raw .kicad_mod S-expression (custom footprint, always present)
 *
 * Intended workflow:
 *   1. component_search { action: "get_kicad", query: "LM358P" }
 *      → { kicad_symbol: "...", kicad_footprint: "..." }
 *   2. sch_import_symbol { kicad_symbol: "...", kicad_footprint: "..." }
 *      → writes project.kicad_sym + project.pretty/NAME.kicad_mod
 *      → updates sym-lib-table + fp-lib-table
 *      → returns { lib_id: "project:LM358P", footprint_lib_id: "project:SOIC-8_3.9x4.9mm_P1.27mm" }
 *   3. sch_add { elements: [{ lib_id: "project:LM358P", ... }] }
 */
class SYMBOL_IMPORTER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "sch_import_symbol" }; }

    std::string Execute( const std::string& aToolName,
                         const nlohmann::json& aInput ) override;

    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }

    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

private:
    /**
     * Run the full import workflow on a background thread.
     */
    std::string DoImport( const std::string& aSymContent,
                          const std::string& aFpContent,
                          const std::string& aSymbolName,
                          const std::string& aLibraryName,
                          const std::string& aProjectPath,
                          bool aForce );

    /**
     * Scan kicad_symbol content for the first top-level symbol name.
     * Skips sub-unit entries (NAME_N_N pattern).
     * Works with or without the (kicad_symbol_lib ...) wrapper.
     */
    std::string ExtractSymbolName( const std::string& aContent );

    /**
     * Extract all top-level (symbol ...) blocks from within a (kicad_symbol_lib ...) wrapper.
     * Returns the blocks concatenated, suitable for injecting into an existing library file.
     * If there is no wrapper, returns aContent as-is.
     */
    std::string ExtractSymbolBlocks( const std::string& aContent );

    /**
     * Extract the footprint name from a .kicad_mod S-expression.
     * Looks for (footprint "NAME" or (module "NAME" at the top level.
     * Returns empty string if not found.
     */
    std::string ExtractFootprintName( const std::string& aContent );

    /**
     * Set (or replace) the Footprint property value inside symbol content.
     */
    std::string SetFootprintProperty( const std::string& aContent,
                                      const std::string& aFootprintLibId );

    /**
     * Ensure the symbol library is registered in projectPath/sym-lib-table.
     * Creates the table if it does not exist.
     * Returns true if a new entry was written.
     */
    bool UpdateSymLibTable( const std::string& aProjectPath,
                            const std::string& aLibraryName );

    /**
     * Write a .kicad_mod file into projectPath/libraryName.pretty/fpName.kicad_mod.
     * Creates the .pretty directory if needed.
     * Returns the footprint lib_id (libraryName:fpName) on success, empty string on failure.
     */
    std::string WriteFootprint( const std::string& aFpContent,
                                const std::string& aFpName,
                                const std::string& aLibraryName,
                                const std::string& aProjectPath,
                                bool aForce );

    /**
     * Ensure the footprint library is registered in projectPath/fp-lib-table.
     * Creates the table if it does not exist.
     */
    void UpdateFpLibTable( const std::string& aProjectPath,
                           const std::string& aLibraryName );
};

#endif // SYMBOL_IMPORTER_H
