#ifndef KICAD_FILE_RENDERER_H
#define KICAD_FILE_RENDERER_H

#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <set>

/**
 * Generates visual before/after diffs for KiCad schematic and PCB files.
 * Parses the S-expression format to identify changed items by UUID, then
 * exports both versions to SVG using kicad-cli and returns highlighted results.
 */
class KICAD_FILE_RENDERER
{
public:
    /**
     * Generate a visual diff between two versions of a KiCad file.
     * @param aOldContent  File content at the old revision.
     * @param aNewContent  File content at the new revision.
     * @param aFilePath    File path (used to determine schematic vs PCB).
     * @return JSON: { success, beforeSvg, afterSvg, changedItems:[{uuid,changeType,itemType,x,y}] }
     */
    nlohmann::json GetVisualDiff( const std::string& aOldContent,
                                   const std::string& aNewContent,
                                   const std::string& aFilePath );

private:
    struct ItemInfo
    {
        std::string type;        // e.g. "symbol", "wire", "footprint"
        float       x = 0;      // position X in mm
        float       y = 0;      // position Y in mm
        std::string serialized;  // full S-expression text for change detection
    };

    /** Write content to a temp file, run kicad-cli export, return SVG text. */
    std::string ExportToSvg( const std::string& aContent, bool aIsSch );

    /** Parse S-expression content and extract top-level items that have UUIDs. */
    std::map<std::string, ItemInfo> ExtractItems( const std::string& aContent,
                                                   const std::set<std::string>& aTypes );

    /** Locate kicad-cli and build a command prefix with proper env vars. */
    std::string GetCliPrefix();

    std::string m_cliPrefix; // cached
};

#endif // KICAD_FILE_RENDERER_H
