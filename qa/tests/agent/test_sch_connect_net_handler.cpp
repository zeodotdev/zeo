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
 * Unit tests for the sch_connect_net tool handler
 */

#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>
#include <tools/schematic/sch_connect_net_handler.h>

BOOST_AUTO_TEST_SUITE( SchConnectNetHandler )


// --- Handler identity ---

BOOST_AUTO_TEST_CASE( CanHandleCorrectTool )
{
    SCH_CONNECT_NET_HANDLER handler;
    BOOST_CHECK( handler.CanHandle( "sch_connect_net" ) );
}


BOOST_AUTO_TEST_CASE( CanHandleRejectsOtherTools )
{
    SCH_CONNECT_NET_HANDLER handler;
    BOOST_CHECK( !handler.CanHandle( "sch_add" ) );
    BOOST_CHECK( !handler.CanHandle( "other_tool" ) );
}


BOOST_AUTO_TEST_CASE( RequiresIPC )
{
    SCH_CONNECT_NET_HANDLER handler;
    BOOST_CHECK( handler.RequiresIPC( "sch_connect_net" ) );
}


// --- Input validation ---

BOOST_AUTO_TEST_CASE( ErrorOnMissingPinsKey )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "other", "value" } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "pins array" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( ErrorOnNonArrayPins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", "not_an_array" } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( ErrorOnSinglePin )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "at least 2" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( ErrorOnEmptyPinsArray )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array() } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( LabelSpecWithoutColonIsAccepted )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "VCC", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Label specs (no colon) should produce ('label', 'VCC', '') tuples, not a validation error
    BOOST_CHECK( cmd.find( "pin_specs = [" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "('label', 'VCC', '')" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "('pin', 'R2', '2')" ) != std::string::npos );
}


// --- GetDescription ---

BOOST_AUTO_TEST_CASE( DescriptionListsTwoPins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string desc = handler.GetDescription( "sch_connect_net", input );

    BOOST_CHECK_EQUAL( desc, "Connecting R1:1, R2:2" );
}


BOOST_AUTO_TEST_CASE( DescriptionListsThreePins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "U1:7", "R3:1", "R4:2" } ) } };
    std::string desc = handler.GetDescription( "sch_connect_net", input );

    BOOST_CHECK_EQUAL( desc, "Connecting U1:7, R3:1, R4:2" );
}


BOOST_AUTO_TEST_CASE( DescriptionShowsCountForManyPins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = {
        { "pins", nlohmann::json::array( { "R1:1", "R2:1", "R3:1", "R4:1" } ) }
    };
    std::string desc = handler.GetDescription( "sch_connect_net", input );

    BOOST_CHECK_EQUAL( desc, "Connecting 4 pins" );
}


BOOST_AUTO_TEST_CASE( DescriptionFallbackWhenNoPins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = nlohmann::json::object();
    std::string desc = handler.GetDescription( "sch_connect_net", input );

    BOOST_CHECK_EQUAL( desc, "Connecting pins" );
}


// --- Python code generation ---

BOOST_AUTO_TEST_CASE( IPCCommandStartsWithRunShell )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "run_shell sch " ) == 0 );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsImports )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "import json" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "from kipy.geometry import Vector2" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsSnapHelper )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "def snap_to_grid(" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsRefreshPreamble )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "refresh_document" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsPinSpecs )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "pin_specs = [" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "('pin', 'R1', '1')" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "('pin', 'R2', '2')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinOrderIsPreserved )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = {
        { "pins", nlohmann::json::array( { "U3:7", "R8:2", "R9:1" } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // U3 should appear before R8, R8 before R9
    size_t posU3 = cmd.find( "('pin', 'U3', '7')" );
    size_t posR8 = cmd.find( "('pin', 'R8', '2')" );
    size_t posR9 = cmd.find( "('pin', 'R9', '1')" );

    BOOST_REQUIRE( posU3 != std::string::npos );
    BOOST_REQUIRE( posR8 != std::string::npos );
    BOOST_REQUIRE( posR9 != std::string::npos );
    BOOST_CHECK( posU3 < posR8 );
    BOOST_CHECK( posR8 < posR9 );
}


BOOST_AUTO_TEST_CASE( SpecialCharsInRefAreEscaped )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = {
        { "pins", nlohmann::json::array( { "R1'x:1", "R2:2" } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Single quote in ref should be escaped as \' (inside 3-tuple)
    BOOST_CHECK( cmd.find( "R1\\'x" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsErrorHandling )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "try:" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "except Exception" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "print(json.dumps(result" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsRoutingLogic )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Should contain the pin resolution and wiring logic
    BOOST_CHECK( cmd.find( "pin_positions" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "add_wire" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "wire_count" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( GeneratedCodeContainsTrunkLogicForThreePlusPins )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = {
        { "pins", nlohmann::json::array( { "R1:1", "R2:2", "R3:1" } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Trunk-and-branch logic markers
    BOOST_CHECK( cmd.find( "trunk" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "obstacles" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "add_junction" ) != std::string::npos );
}


// --- Orientation enum mapping (regression: was treating 0-3 enum as degrees) ---

BOOST_AUTO_TEST_CASE( OrientationUsesEnumNotDegrees )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Must use direct enum comparisons, not degree-based math
    BOOST_CHECK( cmd.find( "pin_orientation == 0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "pin_orientation == 1" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "pin_orientation == 2" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "pin_orientation == 3" ) != std::string::npos );

    // The old buggy code used modulo-360 degree math — must NOT be present
    BOOST_CHECK_EQUAL( cmd.find( "% 360" ), std::string::npos );
    BOOST_CHECK_EQUAL( cmd.find( "ang < 45" ), std::string::npos );
    BOOST_CHECK_EQUAL( cmd.find( "ang >= 315" ), std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinRightEscapesLeft )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // PIN_RIGHT (enum 0): pin body extends right, so escape LEFT (-1.27, 0)
    size_t pos = cmd.find( "pin_orientation == 0" );
    BOOST_REQUIRE( pos != std::string::npos );

    std::string after = cmd.substr( pos, 200 );
    BOOST_CHECK( after.find( "-1.27, 0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinLeftEscapesRight )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // PIN_LEFT (enum 1): pin body extends left, so escape RIGHT (1.27, 0)
    size_t pos = cmd.find( "pin_orientation == 1" );
    BOOST_REQUIRE( pos != std::string::npos );

    std::string after = cmd.substr( pos, 200 );
    BOOST_CHECK( after.find( "1.27, 0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinUpEscapesDown )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // PIN_UP (enum 2): pin body extends up, so escape DOWN (0, 1.27)
    size_t pos = cmd.find( "pin_orientation == 2" );
    BOOST_REQUIRE( pos != std::string::npos );

    std::string after = cmd.substr( pos, 200 );
    BOOST_CHECK( after.find( "0, 1.27" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinDownEscapesUp )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // PIN_DOWN (enum 3): pin body extends down, so escape UP (0, -1.27)
    size_t pos = cmd.find( "pin_orientation == 3" );
    BOOST_REQUIRE( pos != std::string::npos );

    std::string after = cmd.substr( pos, 200 );
    BOOST_CHECK( after.find( "0, -1.27" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AllOrientationsMustNotMapToSameDirection )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // The old bug caused all 4 enum values (0-3) to fall into the same branch
    // (ang < 45 was true for all). Verify each enum has a distinct branch
    // by checking that the code after each enum comparison differs.
    auto getEscape = [&]( const std::string& enumCheck ) -> std::string
    {
        size_t pos = cmd.find( enumCheck );
        if( pos == std::string::npos )
            return "";
        // Extract the escape assignment (out_dx, out_dy = ...)
        std::string after = cmd.substr( pos, 200 );
        size_t eqPos = after.find( "out_dx, out_dy =" );
        if( eqPos == std::string::npos )
            return "";
        return after.substr( eqPos, 40 );
    };

    std::string esc0 = getEscape( "pin_orientation == 0" );
    std::string esc1 = getEscape( "pin_orientation == 1" );
    std::string esc2 = getEscape( "pin_orientation == 2" );
    std::string esc3 = getEscape( "pin_orientation == 3" );

    BOOST_REQUIRE( !esc0.empty() );
    BOOST_REQUIRE( !esc1.empty() );
    BOOST_REQUIRE( !esc2.empty() );
    BOOST_REQUIRE( !esc3.empty() );

    // All four must be different — the old bug made them all identical
    BOOST_CHECK( esc0 != esc1 );
    BOOST_CHECK( esc0 != esc2 );
    BOOST_CHECK( esc0 != esc3 );
    BOOST_CHECK( esc1 != esc2 );
    BOOST_CHECK( esc1 != esc3 );
    BOOST_CHECK( esc2 != esc3 );
}


// --- Wire overlap junction exemptions (regression: arrival wires blocked escape from shared pin) ---

BOOST_AUTO_TEST_CASE( AStarSkipsWireOverlapAtStartCell )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // A* must skip wire overlap checks when leaving the start cell.
    // The guard "(gx, gy) != (gx0, gy0)" before the h_wire_cells/v_wire_cells
    // checks ensures the first step from a pin is never blocked by an arrival wire.
    BOOST_CHECK( cmd.find( "(gx, gy) != (gx0, gy0)" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PathHitsObstacleBuildsEndpointAdjacencySet )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // _path_hits_obstacle must build an adjacency set around endpoints
    // so wire overlap near pin junctions is allowed.
    BOOST_CHECK( cmd.find( "_ep_adj" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PathHitsObstacleExemptsEndpointNeighborsFromWireOverlap )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1:1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // Wire overlap checks in _path_hits_obstacle must gate on "not in _ep_adj"
    // for both vertical and horizontal segments.
    BOOST_CHECK( cmd.find( "not in _ep_adj and" ) != std::string::npos );

    // Should appear twice: once for v_wire_cells, once for h_wire_cells
    size_t first = cmd.find( "not in _ep_adj" );
    BOOST_REQUIRE( first != std::string::npos );
    size_t second = cmd.find( "not in _ep_adj", first + 1 );
    BOOST_CHECK( second != std::string::npos );
}


BOOST_AUTO_TEST_SUITE_END()
