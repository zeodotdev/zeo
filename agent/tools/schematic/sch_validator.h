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

#ifndef SCH_VALIDATOR_H
#define SCH_VALIDATOR_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace SchValidator
{

/**
 * Result of a validation operation.
 */
struct ValidationResult
{
    bool                     valid;
    bool                     syntaxOk;
    bool                     structureOk;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    ValidationResult() : valid( true ), syntaxOk( true ), structureOk( true ) {}

    void AddError( const std::string& aError )
    {
        errors.push_back( aError );
        valid = false;
    }

    void AddWarning( const std::string& aWarning )
    {
        warnings.push_back( aWarning );
    }

    nlohmann::json ToJson() const;
};

/**
 * Validate a schematic file.
 * Performs syntax validation (S-expression parsing) and structural validation
 * (required fields, UUID uniqueness, valid references).
 * @param aFilePath Path to the .kicad_sch file.
 * @return ValidationResult with errors and warnings.
 */
ValidationResult ValidateFile( const std::string& aFilePath );

/**
 * Validate schematic content string.
 * @param aContent The schematic content to validate.
 * @return ValidationResult with errors and warnings.
 */
ValidationResult ValidateContent( const std::string& aContent );

/**
 * Validate a single S-expression element (for sch_modify operations).
 * Checks that the element has proper structure for its type.
 * @param aElementType The expected element type (symbol, wire, junction, label, text).
 * @param aElement The S-expression content of the element.
 * @return ValidationResult with errors and warnings.
 */
ValidationResult ValidateElement( const std::string& aElementType, const std::string& aElement );

/**
 * Check if a symbol element has all required fields.
 * @param aContent The symbol S-expression content.
 * @return ValidationResult with specific errors for missing fields.
 */
ValidationResult ValidateSymbol( const std::string& aContent );

/**
 * Check if a wire element has valid structure.
 * @param aContent The wire S-expression content.
 * @return ValidationResult.
 */
ValidationResult ValidateWire( const std::string& aContent );

/**
 * Check if a junction element has valid structure.
 * @param aContent The junction S-expression content.
 * @return ValidationResult.
 */
ValidationResult ValidateJunction( const std::string& aContent );

/**
 * Check if a label element has valid structure.
 * @param aContent The label S-expression content.
 * @return ValidationResult.
 */
ValidationResult ValidateLabel( const std::string& aContent );

} // namespace SchValidator

#endif // SCH_VALIDATOR_H
