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
    BOOST_CHECK( !handler.CanHandle( "sch_connect_to_power" ) );
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


BOOST_AUTO_TEST_CASE( ErrorOnPinSpecMissingColon )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = { { "pins", nlohmann::json::array( { "R1_1", "R2:2" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "R1_1" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "missing colon" ) != std::string::npos );
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
    BOOST_CHECK( cmd.find( "('R1', '1')" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "('R2', '2')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( PinOrderIsPreserved )
{
    SCH_CONNECT_NET_HANDLER handler;
    nlohmann::json input = {
        { "pins", nlohmann::json::array( { "U3:7", "R8:2", "R9:1" } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_connect_net", input );

    // U3 should appear before R8, R8 before R9
    size_t posU3 = cmd.find( "('U3', '7')" );
    size_t posR8 = cmd.find( "('R8', '2')" );
    size_t posR9 = cmd.find( "('R9', '1')" );

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

    // Single quote in ref should be escaped as \'
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


BOOST_AUTO_TEST_SUITE_END()
