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

/**
 * Unit tests for schematic version injection in sch_write tool
 */

#include <boost/test/unit_test.hpp>
#include <regex>
#include <string>
#include <sch_file_versions.h>

namespace
{

// Replicate the version injection logic for testing
std::string InjectSchematicVersion( const std::string& aContent )
{
    std::string result = aContent;

    std::regex versionRegex( R"(\(version\s+\d+\))" );
    result = std::regex_replace( result, versionRegex,
                                 "(version " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) + ")" );

    return result;
}

} // anonymous namespace


BOOST_AUTO_TEST_SUITE( SchVersionInjection )


/**
 * Test that old version numbers are replaced with current version
 */
BOOST_AUTO_TEST_CASE( ReplaceOldVersion )
{
    std::string content = R"(
(kicad_sch
  (version 20231120)
  (generator "eeschema")
)
)";

    std::string result = InjectSchematicVersion( content );

    std::string expectedVersion = "(version " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) + ")";
    BOOST_CHECK_MESSAGE( result.find( expectedVersion ) != std::string::npos,
                         "Version should be updated to " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) );
    BOOST_CHECK_MESSAGE( result.find( "(version 20231120)" ) == std::string::npos,
                         "Old version should be replaced" );
}


/**
 * Test that already-correct version remains unchanged
 */
BOOST_AUTO_TEST_CASE( CorrectVersionUnchanged )
{
    std::string expectedVersion = "(version " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) + ")";
    std::string content = "(kicad_sch\n  " + expectedVersion + "\n)";

    std::string result = InjectSchematicVersion( content );

    BOOST_CHECK_MESSAGE( result.find( expectedVersion ) != std::string::npos,
                         "Correct version should remain" );
}


/**
 * Test handling of extra whitespace in version field
 */
BOOST_AUTO_TEST_CASE( HandleExtraWhitespace )
{
    std::string content = "(version   12345)";

    std::string result = InjectSchematicVersion( content );

    std::string expectedVersion = "(version " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) + ")";
    BOOST_CHECK_MESSAGE( result.find( expectedVersion ) != std::string::npos,
                         "Version with extra whitespace should be replaced" );
}


/**
 * Test that content without version field is unchanged
 */
BOOST_AUTO_TEST_CASE( NoVersionFieldUnchanged )
{
    std::string content = "(kicad_sch (uuid \"abc\"))";

    std::string result = InjectSchematicVersion( content );

    BOOST_CHECK_EQUAL( result, content );
}


/**
 * Test multiple version fields (edge case - should replace all)
 */
BOOST_AUTO_TEST_CASE( MultipleVersionFields )
{
    std::string content = "(version 20200101) (version 20210101)";

    std::string result = InjectSchematicVersion( content );

    std::string expectedVersion = "(version " + std::to_string( SEXPR_SCHEMATIC_FILE_VERSION ) + ")";

    // Count occurrences of expected version
    size_t count = 0;
    size_t pos = 0;
    while( ( pos = result.find( expectedVersion, pos ) ) != std::string::npos )
    {
        ++count;
        pos += expectedVersion.length();
    }

    BOOST_CHECK_EQUAL( count, 2 );
}


BOOST_AUTO_TEST_SUITE_END()
