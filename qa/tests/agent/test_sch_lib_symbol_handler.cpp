/**
 * Unit tests for the sch_find_symbol tool handler
 */

#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>
#include <tools/handlers/python_tool_handler.h>

BOOST_AUTO_TEST_SUITE( SchLibSymbolHandler )


/**
 * Test that the handler registers the sch_find_symbol tool
 */
BOOST_AUTO_TEST_CASE( RegistersCorrectTool )
{
    PYTHON_TOOL_HANDLER handler;
    auto names = handler.GetToolNames();
    BOOST_CHECK( std::find( names.begin(), names.end(), "sch_find_symbol" ) != names.end() );
}


/**
 * Test that the handler requires IPC execution
 */
BOOST_AUTO_TEST_CASE( RequiresIPC )
{
    PYTHON_TOOL_HANDLER handler;
    BOOST_CHECK( handler.RequiresIPC( "sch_find_symbol" ) );
    BOOST_CHECK( !handler.RequiresIPC( "nonexistent_tool" ) );
}


/**
 * Test IPC command generation for exact match lookup
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandExactMatch )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should start with run_shell sch
    BOOST_CHECK( cmd.find( "run_shell sch" ) == 0 );

    // Should contain the lib_id
    BOOST_CHECK( cmd.find( "Device:R" ) != std::string::npos );

    // Should reference get_symbol_info for exact lookup
    BOOST_CHECK( cmd.find( "get_symbol_info" ) != std::string::npos );

    // Should read include_pins from TOOL_ARGS
    BOOST_CHECK( cmd.find( "include_pins = TOOL_ARGS.get" ) != std::string::npos );
}


/**
 * Test IPC command generation for wildcard pattern
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandWildcard )
{
    PYTHON_TOOL_HANDLER handler;
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
    PYTHON_TOOL_HANDLER handler;
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
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = {
        { "lib_id", "Device:R" },
        { "include_pins", false },
        { "max_suggestions", 5 }
    };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // JSON args should contain include_pins:false
    BOOST_CHECK( cmd.find( "\"include_pins\":false" ) != std::string::npos
                 || cmd.find( "\"include_pins\": false" ) != std::string::npos );

    // JSON args should contain max_suggestions:5
    BOOST_CHECK( cmd.find( "\"max_suggestions\":5" ) != std::string::npos
                 || cmd.find( "\"max_suggestions\": 5" ) != std::string::npos );
}


/**
 * Test IPC command generation with explicit pattern type
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandExplicitPatternType )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = {
        { "lib_id", "Device:R" },
        { "pattern_type", "wildcard" }
    };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // JSON args should contain the explicit pattern type
    BOOST_CHECK( cmd.find( "\"pattern_type\":\"wildcard\"" ) != std::string::npos
                 || cmd.find( "\"pattern_type\": \"wildcard\"" ) != std::string::npos );
}


/**
 * Test IPC command generation for bare symbol name (no library prefix)
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandBareSymbolName )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "R" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Should start with run_shell sch
    BOOST_CHECK( cmd.find( "run_shell sch" ) == 0 );

    // Should contain the lib_id
    BOOST_CHECK( cmd.find( "\"R\"" ) != std::string::npos );

    // Should use library.search for bare names (not get_symbol_info)
    BOOST_CHECK( cmd.find( "sch.library.search" ) != std::string::npos );

    // Should filter for exact name matches
    BOOST_CHECK( cmd.find( "r.name.lower() == lib_id.lower()" ) != std::string::npos );

    // Should handle multiple_matches case
    BOOST_CHECK( cmd.find( "multiple_matches" ) != std::string::npos );
}


/**
 * Test description generation for bare symbol name
 */
BOOST_AUTO_TEST_CASE( GetDescriptionBareSymbolName )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "R" } };
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "R" ) != std::string::npos );
    BOOST_CHECK( desc.find( "Getting symbol info" ) != std::string::npos );
}


/**
 * Test description generation for exact match
 */
BOOST_AUTO_TEST_CASE( GetDescriptionExactMatch )
{
    PYTHON_TOOL_HANDLER handler;
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
    PYTHON_TOOL_HANDLER handler;
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
    PYTHON_TOOL_HANDLER handler;
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
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = {};
    std::string desc = handler.GetDescription( "sch_find_symbol", input );

    BOOST_CHECK( !desc.empty() );
    BOOST_CHECK( desc.find( "Getting library symbol info" ) != std::string::npos );
}


/**
 * Test that the fallback path checks for exact matches in search results.
 * When get_symbol_info() fails but search() returns a result whose lib_id
 * matches the query exactly, the code should return 'found' not 'not_found'.
 */
BOOST_AUTO_TEST_CASE( GetIPCCommandExactMatchFallback )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:L" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // The fallback path (after get_symbol_info fails) should check search results
    // for an exact lib_id match before returning not_found
    BOOST_CHECK_MESSAGE( cmd.find( "r.lib_id == lib_id" ) != std::string::npos,
                         "Fallback path must check for exact lib_id match in search results" );
}


/**
 * Regression: generated code must access info.pins directly (not call nonexistent methods).
 * The old code called get_symbol_full()/get_symbol_pins() which don't exist in kipy,
 * silently failed, and returned empty pins:[].
 */
BOOST_AUTO_TEST_CASE( GeneratedCodeAccessesPinDataDirectly )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" }, { "include_pins", true } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Must read pins directly from SymbolInfo.pins (populated by search/get_symbol_info)
    BOOST_CHECK_MESSAGE( cmd.find( "info.pins" ) != std::string::npos,
        "Generated code must access info.pins directly" );

    // Must NOT call nonexistent methods that silently fail
    BOOST_CHECK_MESSAGE( cmd.find( "get_symbol_full" ) == std::string::npos,
        "Must not call nonexistent get_symbol_full()" );
    BOOST_CHECK_MESSAGE( cmd.find( "get_symbol_pins" ) == std::string::npos,
        "Must not call nonexistent get_symbol_pins()" );

    // Must convert nanometers to millimeters
    BOOST_CHECK_MESSAGE( cmd.find( "1_000_000" ) != std::string::npos ||
                         cmd.find( "1000000" ) != std::string::npos,
        "Must convert nm to mm with scale factor" );
}


/**
 * Regression: generated code must extract position_x/position_y from LibPinInfo.
 */
BOOST_AUTO_TEST_CASE( GeneratedCodeExtractsPinPositions )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" }, { "include_pins", true } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Must extract position_x, position_y from LibPinInfo
    BOOST_CHECK_MESSAGE( cmd.find( "position_x" ) != std::string::npos,
        "Must extract position_x from pins" );
    BOOST_CHECK_MESSAGE( cmd.find( "position_y" ) != std::string::npos,
        "Must extract position_y from pins" );
}


/**
 * Verify that include_pins=false skips pin extraction.
 */
BOOST_AUTO_TEST_CASE( PinsSkippedWhenNotRequested )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" }, { "include_pins", false } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // JSON args should contain include_pins:false
    BOOST_CHECK( cmd.find( "\"include_pins\":false" ) != std::string::npos
                 || cmd.find( "\"include_pins\": false" ) != std::string::npos );
}


/**
 * Regression: bare wildcard patterns (no library prefix) must also match against
 * r.name, not just r.lib_id.  Otherwise a search for "ESP32*" won't match
 * "RF_Module:ESP32-C3-MINI-1" because fnmatch("RF_Module:ESP32-C3-MINI-1", "ESP32*")
 * returns False.
 */
BOOST_AUTO_TEST_CASE( BareWildcardMatchesSymbolName )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "ESP32*" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Pattern has no ':', so has_lib_prefix should be false
    BOOST_CHECK_MESSAGE( cmd.find( "has_lib_prefix = ':' in lib_id" ) != std::string::npos,
                         "Must check whether pattern contains library prefix" );

    // Must match against r.name for bare patterns (wildcard path)
    BOOST_CHECK_MESSAGE( cmd.find( "fnmatch.fnmatch(r.name, lib_id)" ) != std::string::npos,
                         "Bare wildcard must also match against r.name" );
}


/**
 * Verify that wildcard patterns WITH a library prefix only match against r.lib_id.
 */
BOOST_AUTO_TEST_CASE( PrefixedWildcardMatchesLibIdOnly )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "RF_Module:ESP32*" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Pattern has ':', so should only match against r.lib_id
    BOOST_CHECK_MESSAGE( cmd.find( "fnmatch.fnmatch(r.lib_id, lib_id)" ) != std::string::npos,
                         "Prefixed wildcard must match against r.lib_id" );
}


/**
 * Regression: bare regex patterns must also match against r.name.
 */
BOOST_AUTO_TEST_CASE( BareRegexMatchesSymbolName )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "ESP32[_-]C3.*" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Must match against r.name for bare regex patterns
    BOOST_CHECK_MESSAGE( cmd.find( "pattern.fullmatch(r.name)" ) != std::string::npos,
                         "Bare regex must also match against r.name" );
}


/**
 * Test that Execute returns error (requires IPC)
 */
BOOST_AUTO_TEST_CASE( ExecuteReturnsError )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string result = handler.Execute( "sch_find_symbol", input );

    BOOST_CHECK( result.find( "Error" ) != std::string::npos );
    BOOST_CHECK( result.find( "IPC" ) != std::string::npos );
}


/**
 * Test that generated code extracts bounding box data from SymbolInfo.
 */
BOOST_AUTO_TEST_CASE( GeneratedCodeExtractsBoundingBox )
{
    PYTHON_TOOL_HANDLER handler;
    nlohmann::json input = { { "lib_id", "Device:R" } };
    std::string cmd = handler.GetIPCCommand( "sch_find_symbol", input );

    // Must extract bounding box fields from SymbolInfo
    BOOST_CHECK_MESSAGE( cmd.find( "body_bbox_min_x_nm" ) != std::string::npos,
        "Must extract body_bbox_min_x_nm from symbol info" );
    BOOST_CHECK_MESSAGE( cmd.find( "body_bbox_max_x_nm" ) != std::string::npos,
        "Must extract body_bbox_max_x_nm from symbol info" );

    // Must include body_size in the result
    BOOST_CHECK_MESSAGE( cmd.find( "body_size" ) != std::string::npos,
        "Must include body_size in format_symbol result" );

    // Must convert nm to mm
    BOOST_CHECK_MESSAGE( cmd.find( "1_000_000" ) != std::string::npos,
        "Must convert nm to mm" );
}


BOOST_AUTO_TEST_SUITE_END()
