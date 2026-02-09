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
