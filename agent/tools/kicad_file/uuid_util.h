#ifndef UUID_UTIL_H
#define UUID_UTIL_H

#include <string>
#include <set>

namespace UuidUtil
{

/**
 * Generate a new UUID string in the format used by KiCad files.
 * Format: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
 * @return A new UUID string.
 */
std::string GenerateUuid();

/**
 * Check if a string is a valid UUID format.
 * @param aUuid The string to check.
 * @return true if the string is a valid UUID format.
 */
bool IsValidUuid( const std::string& aUuid );

/**
 * Extract all UUIDs from a KiCad file content string.
 * Looks for patterns like (uuid "...") or (uuid ...).
 * @param aContent The file content to search.
 * @return Set of all UUIDs found in the content.
 */
std::set<std::string> ExtractUuids( const std::string& aContent );

/**
 * Check if a UUID is unique within a set of existing UUIDs.
 * @param aUuid The UUID to check.
 * @param aExistingUuids Set of existing UUIDs.
 * @return true if the UUID is not in the existing set.
 */
bool IsUuidUnique( const std::string& aUuid, const std::set<std::string>& aExistingUuids );

/**
 * Generate a unique UUID that doesn't conflict with existing ones.
 * @param aExistingUuids Set of UUIDs to avoid.
 * @return A new unique UUID.
 */
std::string GenerateUniqueUuid( const std::set<std::string>& aExistingUuids );

} // namespace UuidUtil

#endif // UUID_UTIL_H
