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
 * Unit tests for the schematic parser (sch_get_summary tool)
 */

#include <boost/test/unit_test.hpp>
#include <tools/schematic/sch_parser.h>
#include <tools/kicad_file/file_writer.h>
#include <cmath>
#include <fstream>

BOOST_AUTO_TEST_SUITE( SchParser )


/**
 * Helper to create a temporary schematic file for testing
 */
static std::string CreateTempSchematic( const std::string& aContent )
{
    std::string path = "/tmp/test_sch_parser_" + std::to_string( rand() ) + ".kicad_sch";
    std::ofstream file( path );
    file << aContent;
    file.close();
    return path;
}


/**
 * Helper to clean up temp file
 */
static void RemoveTempFile( const std::string& aPath )
{
    std::remove( aPath.c_str() );
}


/**
 * Test that GetSummary extracts basic symbol info
 */
BOOST_AUTO_TEST_CASE( GetSummaryBasicSymbol )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (paper "A4")
  (lib_symbols
    (symbol "Device:R"
      (symbol "R_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "R_1_1"
        (pin passive line (at 0 3.81 270) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
        (pin passive line (at 0 -3.81 90) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27))))
        )
      )
    )
  )
  (symbol
    (lib_id "Device:R")
    (at 100 50 0)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_CHECK_EQUAL( summary.symbols.size(), 1 );
    BOOST_CHECK_EQUAL( summary.symbols[0].reference, "R1" );
    BOOST_CHECK_EQUAL( summary.symbols[0].value, "10k" );
    BOOST_CHECK_EQUAL( summary.symbols[0].libId, "Device:R" );
    BOOST_CHECK_CLOSE( summary.symbols[0].x, 100.0, 0.01 );
    BOOST_CHECK_CLOSE( summary.symbols[0].y, 50.0, 0.01 );
    BOOST_CHECK_CLOSE( summary.symbols[0].angle, 0.0, 0.01 );
}


/**
 * Test that GetSummary extracts pin positions for a symbol at angle 0
 */
BOOST_AUTO_TEST_CASE( GetSummaryPinsNoRotation )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:R"
      (symbol "R_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "R_1_1"
        (pin passive line (at 0 3.81 270) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
        (pin passive line (at 0 -3.81 90) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27))))
        )
      )
    )
  )
  (symbol
    (lib_id "Device:R")
    (at 100 50 0)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_REQUIRE_EQUAL( summary.symbols.size(), 1 );
    BOOST_REQUIRE_EQUAL( summary.symbols[0].pins.size(), 2 );

    // Pin 1 at relative (0, 3.81) -> absolute (100, 50 + 3.81) = (100, 53.81)
    // Pin 2 at relative (0, -3.81) -> absolute (100, 50 - 3.81) = (100, 46.19)
    auto& pins = summary.symbols[0].pins;

    // Find pin 1 and pin 2
    const SchParser::PinInfo* pin1 = nullptr;
    const SchParser::PinInfo* pin2 = nullptr;
    for( const auto& pin : pins )
    {
        if( pin.number == "1" ) pin1 = &pin;
        if( pin.number == "2" ) pin2 = &pin;
    }

    BOOST_REQUIRE( pin1 != nullptr );
    BOOST_REQUIRE( pin2 != nullptr );

    BOOST_CHECK_EQUAL( pin1->number, "1" );
    BOOST_CHECK_CLOSE( pin1->x, 100.0, 0.01 );
    BOOST_CHECK_CLOSE( pin1->y, 53.81, 0.01 );

    BOOST_CHECK_EQUAL( pin2->number, "2" );
    BOOST_CHECK_CLOSE( pin2->x, 100.0, 0.01 );
    BOOST_CHECK_CLOSE( pin2->y, 46.19, 0.01 );
}


/**
 * Test that GetSummary correctly rotates pin positions for 90 degree rotation
 */
BOOST_AUTO_TEST_CASE( GetSummaryPins90Rotation )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:R"
      (symbol "R_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "R_1_1"
        (pin passive line (at 0 3.81 270) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
        (pin passive line (at 0 -3.81 90) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27))))
        )
      )
    )
  )
  (symbol
    (lib_id "Device:R")
    (at 100 50 90)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_REQUIRE_EQUAL( summary.symbols.size(), 1 );
    BOOST_REQUIRE_EQUAL( summary.symbols[0].pins.size(), 2 );

    // For 90 degree rotation (x' = x*cos - y*sin, y' = x*sin + y*cos):
    // Pin 1 at relative (0, 3.81) rotated 90 -> (-3.81, 0) + symbol pos = (96.19, 50)
    // Pin 2 at relative (0, -3.81) rotated 90 -> (3.81, 0) + symbol pos = (103.81, 50)
    auto& pins = summary.symbols[0].pins;

    const SchParser::PinInfo* pin1 = nullptr;
    const SchParser::PinInfo* pin2 = nullptr;
    for( const auto& pin : pins )
    {
        if( pin.number == "1" ) pin1 = &pin;
        if( pin.number == "2" ) pin2 = &pin;
    }

    BOOST_REQUIRE( pin1 != nullptr );
    BOOST_REQUIRE( pin2 != nullptr );

    BOOST_CHECK_CLOSE( pin1->x, 96.19, 0.01 );
    BOOST_CHECK_CLOSE( pin1->y, 50.0, 0.01 );

    BOOST_CHECK_CLOSE( pin2->x, 103.81, 0.01 );
    BOOST_CHECK_CLOSE( pin2->y, 50.0, 0.01 );
}


/**
 * Test that GetSummary handles mirror X correctly
 */
BOOST_AUTO_TEST_CASE( GetSummaryPinsMirrorX )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:R"
      (symbol "R_0_1"
        (polyline (pts (xy 0 0) (xy 1 1)))
      )
      (symbol "R_1_1"
        (pin passive line (at 0 3.81 270) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
        (pin passive line (at 0 -3.81 90) (length 2.54)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27))))
        )
      )
    )
  )
  (symbol
    (lib_id "Device:R")
    (at 100 50 0)
    (mirror x)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_REQUIRE_EQUAL( summary.symbols.size(), 1 );
    BOOST_CHECK( summary.symbols[0].mirrorX );
    BOOST_CHECK( !summary.symbols[0].mirrorY );
    BOOST_REQUIRE_EQUAL( summary.symbols[0].pins.size(), 2 );

    // Mirror X flips Y coordinates:
    // Pin 1 at relative (0, 3.81) with mirrorX -> (0, -3.81) + symbol pos = (100, 46.19)
    // Pin 2 at relative (0, -3.81) with mirrorX -> (0, 3.81) + symbol pos = (100, 53.81)
    auto& pins = summary.symbols[0].pins;

    const SchParser::PinInfo* pin1 = nullptr;
    const SchParser::PinInfo* pin2 = nullptr;
    for( const auto& pin : pins )
    {
        if( pin.number == "1" ) pin1 = &pin;
        if( pin.number == "2" ) pin2 = &pin;
    }

    BOOST_REQUIRE( pin1 != nullptr );
    BOOST_REQUIRE( pin2 != nullptr );

    // Pin positions should be swapped due to mirror
    BOOST_CHECK_CLOSE( pin1->x, 100.0, 0.01 );
    BOOST_CHECK_CLOSE( pin1->y, 46.19, 0.01 );

    BOOST_CHECK_CLOSE( pin2->x, 100.0, 0.01 );
    BOOST_CHECK_CLOSE( pin2->y, 53.81, 0.01 );
}


/**
 * Test that pins from unit 0 (common to all units) are included
 */
BOOST_AUTO_TEST_CASE( GetSummaryPinsUnit0 )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (lib_symbols
    (symbol "Device:R"
      (symbol "R_0_1"
        (pin passive line (at 0 5 270) (length 2.54)
          (name "common" (effects (font (size 1.27 1.27))))
          (number "C" (effects (font (size 1.27 1.27))))
        )
      )
      (symbol "R_1_1"
        (pin passive line (at 0 -5 90) (length 2.54)
          (name "unit1" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27))))
        )
      )
    )
  )
  (symbol
    (lib_id "Device:R")
    (at 100 50 0)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_REQUIRE_EQUAL( summary.symbols.size(), 1 );
    // Should have both the unit 0 pin (common) and the unit 1 pin
    BOOST_REQUIRE_EQUAL( summary.symbols[0].pins.size(), 2 );

    bool foundCommon = false;
    bool foundUnit1 = false;
    for( const auto& pin : summary.symbols[0].pins )
    {
        if( pin.number == "C" ) foundCommon = true;
        if( pin.number == "1" ) foundUnit1 = true;
    }

    BOOST_CHECK_MESSAGE( foundCommon, "Should include pin from unit 0 (common)" );
    BOOST_CHECK_MESSAGE( foundUnit1, "Should include pin from unit 1" );
}


/**
 * Test that ToJson includes pins array
 */
BOOST_AUTO_TEST_CASE( SymbolInfoToJsonIncludesPins )
{
    SchParser::SymbolInfo info;
    info.uuid = "test-uuid";
    info.reference = "R1";
    info.value = "10k";
    info.libId = "Device:R";
    info.x = 100;
    info.y = 50;
    info.angle = 0;
    info.unit = 1;
    info.mirrorX = false;
    info.mirrorY = false;

    SchParser::PinInfo pin1;
    pin1.number = "1";
    pin1.name = "~";
    pin1.x = 100;
    pin1.y = 53.81;
    info.pins.push_back( pin1 );

    SchParser::PinInfo pin2;
    pin2.number = "2";
    pin2.name = "~";
    pin2.x = 100;
    pin2.y = 46.19;
    info.pins.push_back( pin2 );

    auto json = info.ToJson();

    BOOST_CHECK( json.contains( "pins" ) );
    BOOST_CHECK( json["pins"].is_array() );
    BOOST_CHECK_EQUAL( json["pins"].size(), 2 );
    BOOST_CHECK_EQUAL( json["pins"][0]["number"], "1" );
    BOOST_CHECK_EQUAL( json["pins"][0]["pos"][0], 100 );
    BOOST_CHECK_CLOSE( json["pins"][0]["pos"][1].get<double>(), 53.81, 0.01 );
}


/**
 * Test that GenerateSpiceNetlist returns empty string for invalid path
 */
BOOST_AUTO_TEST_CASE( GenerateSpiceNetlistInvalidPath )
{
    std::string result = SchParser::GenerateSpiceNetlist( "" );
    BOOST_CHECK( result.empty() );
}


/**
 * Test that GetSummary does not include spice_netlist (moved to separate tool)
 */
BOOST_AUTO_TEST_CASE( GetSummaryNoSpiceNetlist )
{
    std::string content = R"(
(kicad_sch
  (version 20251028)
  (generator "test")
  (generator_version "9.0")
  (uuid "12345678-1234-1234-1234-123456789abc")
  (paper "A4")
  (lib_symbols)
  (symbol
    (lib_id "Device:R")
    (at 100 50 0)
    (unit 1)
    (uuid "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    (property "Reference" "R1" (at 100 48 0) (effects (font (size 1.27 1.27))))
    (property "Value" "10k" (at 100 52 0) (effects (font (size 1.27 1.27))))
  )
)
)";

    std::string path = CreateTempSchematic( content );
    auto summary = SchParser::GetSummary( path );
    RemoveTempFile( path );

    BOOST_CHECK_EQUAL( summary.symbols.size(), 1 );
    BOOST_CHECK_EQUAL( summary.symbols[0].reference, "R1" );
    BOOST_CHECK_EQUAL( summary.version, 20251028 );

    // spice_netlist should not be in the JSON output
    auto json = summary.ToJson();
    BOOST_CHECK( !json.contains( "spice_netlist" ) );
}


BOOST_AUTO_TEST_SUITE_END()
