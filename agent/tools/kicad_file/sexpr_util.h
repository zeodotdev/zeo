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

#ifndef SEXPR_UTIL_H
#define SEXPR_UTIL_H

#include <string>
#include <vector>
#include <memory>
#include <sexpr/sexpr.h>
#include <sexpr/sexpr_parser.h>

namespace SexprUtil
{

/**
 * Result of a syntax validation operation.
 */
struct ValidationResult
{
    bool        valid;
    std::string error;
    int         errorLine;

    ValidationResult() : valid( true ), errorLine( 0 ) {}
    ValidationResult( const std::string& aError, int aLine = 0 )
        : valid( false ), error( aError ), errorLine( aLine ) {}
};

/**
 * Validate the syntax of an S-expression string.
 * @param aContent The S-expression content to validate.
 * @return Validation result with error details if invalid.
 */
ValidationResult ValidateSyntax( const std::string& aContent );

/**
 * Parse an S-expression string and return the parsed structure.
 * @param aContent The S-expression content to parse.
 * @return Unique pointer to the parsed SEXPR, or nullptr on error.
 */
std::unique_ptr<SEXPR::SEXPR> Parse( const std::string& aContent );

/**
 * Parse an S-expression file.
 * @param aFilePath Path to the file to parse.
 * @return Unique pointer to the parsed SEXPR, or nullptr on error.
 */
std::unique_ptr<SEXPR::SEXPR> ParseFile( const std::string& aFilePath );

/**
 * Get the type/name of a top-level S-expression list.
 * For example, "(kicad_sch ...)" returns "kicad_sch".
 * @param aSexpr The S-expression to examine.
 * @return The type name, or empty string if not a list.
 */
std::string GetListType( const SEXPR::SEXPR* aSexpr );

/**
 * Find all child lists with a specific type.
 * @param aSexpr The parent S-expression to search.
 * @param aType The type name to find (e.g., "symbol", "wire").
 * @return Vector of pointers to matching child lists.
 */
std::vector<const SEXPR::SEXPR*> FindChildren( const SEXPR::SEXPR* aSexpr, const std::string& aType );

/**
 * Find the first child list with a specific type.
 * @param aSexpr The parent S-expression to search.
 * @param aType The type name to find.
 * @return Pointer to the first matching child, or nullptr if not found.
 */
const SEXPR::SEXPR* FindFirstChild( const SEXPR::SEXPR* aSexpr, const std::string& aType );

/**
 * Get a string value from a simple property list like (name "value").
 * @param aSexpr The property S-expression.
 * @return The string value, or empty string if not found or wrong format.
 */
std::string GetStringValue( const SEXPR::SEXPR* aSexpr );

/**
 * Get an integer value from a simple property list like (version 20250114).
 * @param aSexpr The property S-expression.
 * @return The integer value, or 0 if not found or wrong format.
 */
int GetIntValue( const SEXPR::SEXPR* aSexpr );

/**
 * Get a double value from a simple property list.
 * @param aSexpr The property S-expression.
 * @return The double value, or 0.0 if not found or wrong format.
 */
double GetDoubleValue( const SEXPR::SEXPR* aSexpr );

/**
 * Extract coordinates from an (at x y [angle]) expression.
 * @param aSexpr The 'at' S-expression.
 * @param aX Output X coordinate.
 * @param aY Output Y coordinate.
 * @param aAngle Output angle (optional, defaults to 0).
 * @return true if coordinates were extracted successfully.
 */
bool GetCoordinates( const SEXPR::SEXPR* aSexpr, double& aX, double& aY, double& aAngle );

/**
 * Convert an S-expression back to string representation.
 * @param aSexpr The S-expression to convert.
 * @param aIndentLevel Current indentation level.
 * @return String representation of the S-expression.
 */
std::string ToString( const SEXPR::SEXPR* aSexpr, int aIndentLevel = 0 );

} // namespace SexprUtil

#endif // SEXPR_UTIL_H
