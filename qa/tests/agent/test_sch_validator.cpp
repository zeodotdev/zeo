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
 * Unit tests for the agent's schematic validator (sch_validate tool)
 */

#include <boost/test/unit_test.hpp>
#include <tools/schematic/sch_validator.h>

BOOST_AUTO_TEST_SUITE( SchValidator )


/**
 * Test that a minimal valid schematic passes validation
 */
BOOST_AUTO_TEST_CASE( ValidMinimalSchematic )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK( result.valid );
    BOOST_CHECK( result.syntaxOk );
    BOOST_CHECK( result.structureOk );
    BOOST_CHECK( result.errors.empty() );
}


/**
 * Test that valid lib_symbols with correct sub-symbol names pass validation
 */
BOOST_AUTO_TEST_CASE( ValidLibSymbols )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:C"
      (symbol "C_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "C_1_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
    )
  )
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( result.valid, "Valid lib_symbols should pass validation" );
    BOOST_CHECK( result.errors.empty() );
}


/**
 * Test that lib_symbols with invalid sub-symbol prefix fails validation
 */
BOOST_AUTO_TEST_CASE( InvalidSubSymbolPrefix )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:C"
      (symbol "WRONG_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
    )
  )
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( !result.valid, "Mismatched sub-symbol prefix should fail" );
    BOOST_CHECK( !result.errors.empty() );

    // Check that error message mentions the prefix mismatch
    bool foundPrefixError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "prefix" ) != std::string::npos ||
            error.find( "doesn't match" ) != std::string::npos )
        {
            foundPrefixError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundPrefixError, "Error should mention prefix mismatch" );
}


/**
 * Test that lib_symbols with invalid body style fails validation
 */
BOOST_AUTO_TEST_CASE( InvalidBodyStyle )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:C"
      (symbol "C_0_3"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
    )
  )
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( !result.valid, "Invalid body style (3) should fail" );

    bool foundBodyStyleError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "body style" ) != std::string::npos )
        {
            foundBodyStyleError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundBodyStyleError, "Error should mention body style" );
}


/**
 * Test that lib_symbols with invalid sub-symbol name format fails validation
 */
BOOST_AUTO_TEST_CASE( InvalidSubSymbolNameFormat )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:C"
      (symbol "C01"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
    )
  )
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( !result.valid, "Invalid sub-symbol name format should fail" );

    bool foundFormatError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "format" ) != std::string::npos )
        {
            foundFormatError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundFormatError, "Error should mention format" );
}


/**
 * Test validation with DeMorgan body style (style 2)
 */
BOOST_AUTO_TEST_CASE( ValidDeMorganBodyStyle )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:C"
      (symbol "C_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "C_0_2"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
    )
  )
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( result.valid, "Body style 2 (DeMorgan) should be valid" );
}


/**
 * Test that missing required fields are caught
 */
BOOST_AUTO_TEST_CASE( MissingVersion )
{
    std::string content = R"(
(kicad_sch
  (generator "test")
  (uuid "12345678-1234-1234-1234-123456789abc")
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK( !result.valid );

    bool foundVersionError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "version" ) != std::string::npos )
        {
            foundVersionError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundVersionError, "Error should mention missing version" );
}


/**
 * Test that missing UUID is caught
 */
BOOST_AUTO_TEST_CASE( MissingUuid )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK( !result.valid );

    bool foundUuidError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "uuid" ) != std::string::npos )
        {
            foundUuidError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundUuidError, "Error should mention missing uuid" );
}


/**
 * Test syntax error detection - unbalanced parentheses
 */
BOOST_AUTO_TEST_CASE( SyntaxErrorUnbalancedParens )
{
    std::string content = "(kicad_sch (version 20251028) (uuid \"abc\"";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK_MESSAGE( !result.valid, "Unbalanced parentheses should fail validation" );
    BOOST_CHECK_MESSAGE( !result.syntaxOk, "Unbalanced parentheses should fail syntax check" );
}


/**
 * Test that non-schematic files are rejected
 */
BOOST_AUTO_TEST_CASE( NotASchematic )
{
    std::string content = R"(
(kicad_pcb
  (version 20251028)
  (uuid "12345678-1234-1234-1234-123456789abc")
)
)";

    auto result = SchValidator::ValidateContent( content );
    BOOST_CHECK( !result.valid );

    bool foundTypeError = false;
    for( const auto& error : result.errors )
    {
        if( error.find( "kicad_sch" ) != std::string::npos ||
            error.find( "Not a KiCad schematic" ) != std::string::npos )
        {
            foundTypeError = true;
            break;
        }
    }
    BOOST_CHECK_MESSAGE( foundTypeError, "Error should mention expected file type" );
}


BOOST_AUTO_TEST_SUITE_END()
