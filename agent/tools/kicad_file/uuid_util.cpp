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

#include "uuid_util.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <regex>

namespace UuidUtil
{

// Thread-local random number generator
static thread_local std::mt19937 s_rng( std::random_device{}() );


std::string GenerateUuid()
{
    std::uniform_int_distribution<int> dist( 0, 15 );
    std::uniform_int_distribution<int> dist2( 8, 11 );  // For variant field

    std::stringstream ss;
    ss << std::hex << std::setfill( '0' );

    // 8 hex chars
    for( int i = 0; i < 8; i++ )
        ss << dist( s_rng );
    ss << "-";

    // 4 hex chars
    for( int i = 0; i < 4; i++ )
        ss << dist( s_rng );
    ss << "-";

    // 4 hex chars (version 4 UUID - starts with 4)
    ss << "4";
    for( int i = 0; i < 3; i++ )
        ss << dist( s_rng );
    ss << "-";

    // 4 hex chars (variant - starts with 8, 9, a, or b)
    ss << std::hex << dist2( s_rng );
    for( int i = 0; i < 3; i++ )
        ss << dist( s_rng );
    ss << "-";

    // 12 hex chars
    for( int i = 0; i < 12; i++ )
        ss << dist( s_rng );

    return ss.str();
}


bool IsValidUuid( const std::string& aUuid )
{
    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    static const std::regex uuidPattern(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
    return std::regex_match( aUuid, uuidPattern );
}


std::set<std::string> ExtractUuids( const std::string& aContent )
{
    std::set<std::string> uuids;

    // Match (uuid "...") or (uuid ...) patterns
    static const std::regex uuidExtract(
        R"(\(uuid\s+\"?([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\"?\))"
    );

    std::sregex_iterator begin( aContent.begin(), aContent.end(), uuidExtract );
    std::sregex_iterator end;

    for( auto it = begin; it != end; ++it )
    {
        uuids.insert( it->str( 1 ) );
    }

    return uuids;
}


bool IsUuidUnique( const std::string& aUuid, const std::set<std::string>& aExistingUuids )
{
    return aExistingUuids.find( aUuid ) == aExistingUuids.end();
}


std::string GenerateUniqueUuid( const std::set<std::string>& aExistingUuids )
{
    std::string uuid;
    int maxAttempts = 100;

    do
    {
        uuid = GenerateUuid();
        maxAttempts--;
    }
    while( !IsUuidUnique( uuid, aExistingUuids ) && maxAttempts > 0 );

    return uuid;
}

} // namespace UuidUtil
