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
 * Unit tests for the sch_find_symbol tool handler
 */

#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>
#include <tools/schematic/sch_lib_symbol_handler.h>

BOOST_AUTO_TEST_SUITE( SchLibSymbolHandler )


/**
 * Test that the handler correctly identifies its tool name
 */
BOOST_AUTO_TEST_CASE( CanHandleCorrectTool )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    BOOST_CHECK( handler.CanHandle( "sch_find_symbol" ) );
    BOOST_CHECK( !handler.CanHandle( "sch_get_summary" ) );
    BOOST_CHECK( !handler.CanHandle( "sch_run_erc" ) );
    BOOST_CHECK( !handler.CanHandle( "other_tool" ) );
}


/**
 * Test that the handler requires IPC execution
 */
BOOST_AUTO_TEST_CASE( RequiresIPC )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    BOOST_CHECK( handler.RequiresIPC( "sch_find_symbol" ) );
    BOOST_CHECK( !handler.RequiresIPC( "other_tool" ) );
}


/**
 * Test IPC command generation for exact match lookup
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandExactMatch )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should start with run_shell sch
    BOOST_CHECK( cmd.find( "run_shell sch" ) == 0 );

    // Should contain the lib_id
    BOOST_CHECK( cmd.find( "Device:R" ) != std::string::npos );

    // Should reference get_symbol_info for exact lookup
    BOOST_CHECK( cmd.find( "get_symbol_info" ) != std::string::npos );

    // Should have default include_pins = True
    BOOST_CHECK( cmd.find( "include_pins = True" ) != std::string::npos );
}


/**
 * Test IPC command generation for wildcard pattern
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandWildcard )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Connector:Conn_01x*" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should start with run_shell sch
    BOOST_CHECK( cmd.find( "run_shell sch" ) == 0 );

    // Should contain fnmatch for wildcard matching
    BOOST_CHECK( cmd.find( "fnmatch" ) != std::string::npos );

    // Should contain the pattern
    BOOST_CHECK( cmd.find( "Connector:Conn_01x*" ) != std::string::npos );
}


/**
 * Test IPC command generation for regex pattern
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandRegex )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R_[0-9]{4}" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should start with run_shell sch
    BOOST_CHECK( cmd.find( "run_shell sch" ) == 0 );

    // Should contain re.compile or re.fullmatch for regex matching
    BOOST_CHECK( cmd.find( "re.compile" ) != std::string::npos ||
                 cmd.find( "fullmatch" ) != std::string::npos );

    // Should contain the pattern
    BOOST_CHECK( cmd.find( "Device:R_[0-9]{4}" ) != std::string::npos );
}


/**
 * Test IPC command generation with optional parameters
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandWithOptions )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = {
        { "lib_id", "Device:R" },
        { "include_pins", false },
        { "max_suggestions", 5 }
    };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should have include_pins = False
    BOOST_CHECK( cmd.find( "include_pins = False" ) != std::string::npos );

    // Should have max_suggestions = 5
    BOOST_CHECK( cmd.find( "max_suggestions = 5" ) != std::string::npos );
}


/**
 * Test IPC command generation with explicit pattern type
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandExplicitPatternType )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = {
        { "lib_id", "Device:R" },
        { "pattern_type", "wildcard" }
    };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should pass the explicit pattern type
    BOOST_CHECK( cmd.find( "pattern_type = \"wildcard\"" ) != std::string::npos );
}


/**
 * Test description generation for exact match
 */
BOOST_AUTO_TEST_CASE( GetDescriptionExactMatch )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "Device:R" ) != std::string::npos );
    BOOST_CHECK( desc.find( "Getting symbol info" ) != std::string::npos );
}


/**
 * Test description generation for wildcard pattern
 */
BOOST_AUTO_TEST_CASE( GetDescriptionWildcard )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Connector:Conn_*" } };
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "Searching symbols matching" ) != std::string::npos );
}


/**
 * Test description generation for regex pattern
 */
BOOST_AUTO_TEST_CASE( GetDescriptionRegex )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:C_[0-9]+" } };
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "regex" ) != std::string::npos );
}


/**
 * Test description generation with empty lib_id
 */
BOOST_AUTO_TEST_CASE( GetDescriptionEmpty )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = {};
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "Getting library symbol info" ) != std::string::npos );
}


/**
 * Test that Execute returns error (requires IPC)
 */
BOOST_AUTO_TEST_CASE( ExecuteReturnsError )
{
    SCH_LIB_SYMBOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string result = handler.Execute( "sch_find_symbol", input );

    BOOST_CHECK( result.find( "Error" ) != std::string::npos );
    BOOST_CHECK( result.find( "IPC" ) != std::string::npos );
}


BOOST_AUTO_TEST_SUITE_END()
