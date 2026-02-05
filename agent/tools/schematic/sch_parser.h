/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCH_PARSER_H
#define SCH_PARSER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace SchParser
{

/**
 * Section types for schematic file parsing.
 */
enum class SectionType
{
    HEADER,       // version, uuid, paper, title_block
    LIB_SYMBOLS,  // Cached library symbol definitions
    SYMBOLS,      // Placed component instances
    WIRES,        // Wire connections
    JUNCTIONS,    // Wire junction points
    LABELS,       // Net labels
    TEXT,         // Text annotations
    SHEETS,       // Hierarchical sheet references
    SHEET_INSTANCES,
    ALL           // Return entire file
};

/**
 * Information about a pin on a placed symbol.
 */
struct PinInfo
{
    std::string number;
    std::string name;
    double      x;      // Absolute X position
    double      y;      // Absolute Y position

    nlohmann::json ToJson() const;
};

/**
 * Information about a placed symbol in the schematic.
 */
struct SymbolInfo
{
    std::string uuid;
    std::string reference;
    std::string value;
    std::string libId;
    double      x;
    double      y;
    double      angle;
    int         unit;
    bool        mirrorX;
    bool        mirrorY;
    std::vector<PinInfo> pins;

    nlohmann::json ToJson() const;
};

/**
 * Information about a hierarchical sheet reference.
 */
struct SheetInfo
{
    std::string name;
    std::string filename;
    std::string uuid;

    nlohmann::json ToJson() const;
};

/**
 * Summary of a schematic file.
 */
struct SchematicSummary
{
    std::string              file;
    int                      version;
    std::string              uuid;
    std::string              paper;
    std::string              title;
    std::vector<SymbolInfo>  symbols;
    int                      wireCount;
    int                      junctionCount;
    std::vector<std::string> labels;
    std::vector<SheetInfo>   sheets;  // Child sheets referenced in this schematic
    std::string              spiceNetlist;

    nlohmann::json ToJson() const;
};

/**
 * Parse a schematic file and return a summary.
 * @param aFilePath Path to the .kicad_sch file.
 * @return Summary of the schematic, or empty summary on error.
 */
SchematicSummary GetSummary( const std::string& aFilePath );

/**
 * Generate a SPICE netlist from a schematic file using kicad-cli.
 * @param aSchematicPath Path to the .kicad_sch file.
 * @return SPICE netlist text, or empty string on failure.
 */
std::string GenerateSpiceNetlist( const std::string& aSchematicPath );

/**
 * Read a specific section from the schematic file.
 * @param aFilePath Path to the .kicad_sch file.
 * @param aSection The section type to read.
 * @param aFilter Optional filter pattern (e.g., "R*" for resistors, UUID for specific element).
 * @return Raw S-expression text for the requested section, or error string.
 */
std::string ReadSection( const std::string& aFilePath, SectionType aSection,
                         const std::string& aFilter = "" );

/**
 * Convert a section name string to SectionType enum.
 * @param aName Section name (header, symbols, wires, junctions, labels, lib_symbols, text, sheets, all).
 * @return The corresponding SectionType, or ALL if unknown.
 */
SectionType SectionFromString( const std::string& aName );

/**
 * Find elements by UUID in the schematic.
 * @param aContent The schematic file content.
 * @param aUuid The UUID to search for.
 * @return The S-expression text of the element, or empty string if not found.
 */
std::string FindByUuid( const std::string& aContent, const std::string& aUuid );

/**
 * Find symbols by reference designator pattern.
 * @param aContent The schematic file content.
 * @param aPattern Reference pattern (e.g., "R*", "C1", "U?").
 * @return Vector of S-expression texts for matching symbols.
 */
std::vector<std::string> FindSymbolsByReference( const std::string& aContent,
                                                  const std::string& aPattern );

/**
 * Delete an element from the schematic content by UUID.
 * @param aContent The schematic file content.
 * @param aUuid The UUID of the element to delete.
 * @return Modified content with element removed, or original content if not found.
 */
std::string DeleteByUuid( const std::string& aContent, const std::string& aUuid );

/**
 * Add a new element to the schematic content.
 * The element should be a complete S-expression for a symbol, wire, junction, or label.
 * @param aContent The schematic file content.
 * @param aElementType Type of element (symbol, wire, junction, label, text).
 * @param aElement The S-expression for the new element.
 * @return Modified content with element added.
 */
std::string AddElement( const std::string& aContent, const std::string& aElementType,
                        const std::string& aElement );

/**
 * Update an existing element in the schematic content.
 * @param aContent The schematic file content.
 * @param aUuid UUID of the element to update.
 * @param aNewElement The new S-expression to replace the old one.
 * @return Modified content with element updated, or original content if not found.
 */
std::string UpdateElement( const std::string& aContent, const std::string& aUuid,
                           const std::string& aNewElement );

} // namespace SchParser

#endif // SCH_PARSER_H
