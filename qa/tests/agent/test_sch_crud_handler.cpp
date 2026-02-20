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
 * Unit tests for the sch_delete tool handler (SCH_CRUD_HANDLER),
 * including query-based deletion by item properties.
 */

#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>
#include <tools/schematic/sch_crud_handler.h>

BOOST_AUTO_TEST_SUITE( SchCrudHandler )


// --- Handler identity ---

BOOST_AUTO_TEST_CASE( CanHandleDeleteTool )
{
    SCH_CRUD_HANDLER handler;
    BOOST_CHECK( handler.CanHandle( "sch_delete" ) );
}


BOOST_AUTO_TEST_CASE( CanHandleRejectsOtherTools )
{
    SCH_CRUD_HANDLER handler;
    BOOST_CHECK( !handler.CanHandle( "other_tool" ) );
    BOOST_CHECK( !handler.CanHandle( "sch_connect_net" ) );
}


BOOST_AUTO_TEST_CASE( RequiresIPCForDelete )
{
    SCH_CRUD_HANDLER handler;
    BOOST_CHECK( handler.RequiresIPC( "sch_delete" ) );
}


// --- Input validation ---

BOOST_AUTO_TEST_CASE( DeleteErrorOnMissingTargets )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "other", "value" } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "targets" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteErrorOnNonArrayTargets )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", "not_an_array" } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
}


// --- GetDescription with string targets (regression) ---

BOOST_AUTO_TEST_CASE( DeleteDescriptionSingleRef )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting R1" );
}


BOOST_AUTO_TEST_CASE( DeleteDescriptionMultiple )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1", "R2" } ) } };
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting 2 elements" );
}


BOOST_AUTO_TEST_CASE( DeleteDescriptionFallback )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = nlohmann::json::object();
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting elements" );
}


// --- GetDescription with query targets ---

BOOST_AUTO_TEST_CASE( DeleteDescriptionQueryLabelWithText )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "label" }, { "text", "VCC" }
        } } ) }
    };
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting label 'VCC'" );
}


BOOST_AUTO_TEST_CASE( DeleteDescriptionQueryWire )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "wire" }
        } } ) }
    };
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting wire" );
}


BOOST_AUTO_TEST_CASE( DeleteDescriptionQueryNoText )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "junction" }, { "position", { 10.0, 20.0 } }
        } } ) }
    };
    std::string desc = handler.GetDescription( "sch_delete", input );

    BOOST_CHECK_EQUAL( desc, "Deleting junction" );
}


// --- String targets: generated code (regression) ---

BOOST_AUTO_TEST_CASE( DeleteStringTargetStartsWithRunShell )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "run_shell sch " ) == 0 );
}


BOOST_AUTO_TEST_CASE( DeleteStringTargetGeneratesStringTargetsList )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "string_targets" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "'R1'" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteStringTargetContainsUUIDCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "is_uuid" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteStringTargetContainsRefLookup )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "get_by_ref" ) != std::string::npos );
}


// --- Query targets: generated code structure ---

BOOST_AUTO_TEST_CASE( DeleteQueryTargetGeneratesQueryList )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "X" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "query_targets" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryTargetContainsPosMatch )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "X" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "def pos_match(" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryTargetContainsTypeDispatch )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "X" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "q_type" ) != std::string::npos );
}


// --- Query target: wire ---

BOOST_AUTO_TEST_CASE( DeleteQueryWireUsesGetWires )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "wire" }, { "start", { 1.0, 2.0 } }, { "end", { 3.0, 4.0 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "get_wires()" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryWireSerializesEndpoints )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "wire" }, { "start", { 1.0, 2.0 } }, { "end", { 3.0, 4.0 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    // The JSON dump should contain the coordinate values
    BOOST_CHECK( cmd.find( "1.0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "3.0" ) != std::string::npos );
}


// --- Query target: label ---

BOOST_AUTO_TEST_CASE( DeleteQueryLabelUsesGetAll )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "NET1" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "labels.get_all()" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryLabelMatchesText )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "NET1" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "NET1" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryGlobalLabelUsesClass )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "global_label" }, { "text", "SPI" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "GlobalLabel" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryHierLabelUsesClass )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "hierarchical_label" }, { "text", "DATA" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "HierarchicalLabel" ) != std::string::npos );
}


// --- Query target: junction ---

BOOST_AUTO_TEST_CASE( DeleteQueryJunctionUsesGetJunctions )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "junction" }, { "position", { 10.5, 20.3 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "get_junctions()" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryJunctionSerializesPos )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "junction" }, { "position", { 10.5, 20.3 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "10.5" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "20.3" ) != std::string::npos );
}


// --- Query target: no_connect ---

BOOST_AUTO_TEST_CASE( DeleteQueryNoConnectUsesAPI )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "no_connect" }, { "position", { 5.0, 5.0 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "get_no_connects()" ) != std::string::npos );
}


// --- Query target: bus_entry ---

BOOST_AUTO_TEST_CASE( DeleteQueryBusEntryUsesAPI )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "type", "bus_entry" }, { "position", { 8.0, 9.0 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "get_bus_entries" ) != std::string::npos );
}


// --- Mixed targets ---

BOOST_AUTO_TEST_CASE( DeleteMixedTargetsGeneratesBothLists )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( {
            "R1",
            nlohmann::json{ { "element_type", "label" }, { "text", "X" } }
        } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "string_targets" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "query_targets" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "'R1'" ) != std::string::npos );
}


// --- Result reporting ---

BOOST_AUTO_TEST_CASE( DeleteQueryResultContainsNotMatched )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "targets", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" }, { "text", "X" }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "queries_not_matched" ) != std::string::npos );
}


// --- Error handling ---

BOOST_AUTO_TEST_CASE( DeleteQueryContainsErrorHandling )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "try:" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "except Exception" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DeleteQueryContainsRefreshPreamble )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "targets", nlohmann::json::array( { "R1" } ) } };
    std::string cmd = handler.GetIPCCommand( "sch_delete", input );

    BOOST_CHECK( cmd.find( "refresh_document" ) != std::string::npos );
}


// --- Label angle: no angle= keyword in add_global/add_local/add_hierarchical ---
// Note: sch_add routes to GenerateAddBatchCode, so all inputs use elements array.

BOOST_AUTO_TEST_CASE( AddPowerStillUsesAngleKeyword )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:GND" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Power symbols DO support angle= keyword — verify it's still used
    BOOST_CHECK( cmd.find( "add_power('GND', pos_0, angle=" ) != std::string::npos );
}


// --- sch_add: reference auto-numbering ---

BOOST_AUTO_TEST_CASE( AddSymbolGeneratesUsedRefsScan )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "used_refs" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "symbols.get_all()" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolGeneratesNextRefFunction )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "def next_ref(prefix):" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolSetsRefViaProtoFields )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "_proto.fields" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "_f.name == 'Reference'" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "_f.text = _new_ref_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolCallsCrudUpdateItems )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "crud.update_items(sym_0)" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolResultIncludesReference )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "'reference': _new_ref_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolExtractsPrefixFromAutoRef )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Extracts prefix from auto-assigned reference (e.g. 'R' from 'R?')
    BOOST_CHECK( cmd.find( "_prefix_0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "next_ref(_prefix_0)" ) != std::string::npos );
}


// --- sch_add: power symbol auto-numbering ---

BOOST_AUTO_TEST_CASE( AddPowerAutoNumbersWithPWRPrefix )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:GND" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "next_ref('#PWR')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddPowerSetsRefViaProtoFields )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:VCC" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "pwr_0._proto.fields" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "_f.text = _pwr_ref_0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "crud.update_items(pwr_0)" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddPowerResultIncludesReference )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:GND" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "'reference': _pwr_ref_0" ) != std::string::npos );
}


// --- sch_add: results array ---

BOOST_AUTO_TEST_CASE( AddBatchUsesResultsArray )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:C" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "results = []" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "'results': results" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddBatchImportsRe )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 10.16, 20.32 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // re module needed for reference prefix extraction
    BOOST_CHECK( cmd.find( "import json, sys, re" ) != std::string::npos );
}


// --- sch_add: input validation ---

BOOST_AUTO_TEST_CASE( AddErrorOnMissingElements )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = { { "other", "value" } };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "error" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "elements" ) != std::string::npos );
}


// --- sch_add overlap detection ---

BOOST_AUTO_TEST_CASE( AddSymbolGeneratesOverlapPreamble )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "placed_bboxes = []" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "def _bboxes_overlap(a, b):" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "get_bounding_box(_esym" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolGeneratesBboxCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "get_bounding_box(sym_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddPreambleStoresObstacleRef )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Preamble should store reference of existing symbols for obstacle identification
    BOOST_CHECK( cmd.find( "'ref': getattr(_esym, 'reference', '?')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddOverlapErrorIdentifiesObstacle )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Overlap error should identify the obstacle by reference via _overlap_info
    BOOST_CHECK( cmd.find( "_overlap_info" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "_pb.get('ref', '?')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolGeneratesRemoveOnOverlap )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "remove_items([sym_0])" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddSymbolOverlapErrorMessage )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "symbol" },
            { "lib_id", "Device:R" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "Placement rejected: {_overlap_info(_new_bbox_0)}" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddPowerGeneratesBboxCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:VCC" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "get_bounding_box(pwr_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddPowerGeneratesRemoveOnOverlap )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "power" },
            { "lib_id", "power:VCC" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "remove_items([pwr_0])" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddWireSkipsOverlapCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "wire" },
            { "points", nlohmann::json::array( { nlohmann::json::array( { 50.8, 50.8 } ), nlohmann::json::array( { 76.2, 50.8 } ) } ) }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Wires should NOT have overlap detection
    BOOST_CHECK( cmd.find( "get_bounding_box(wc_0" ) == std::string::npos );
    BOOST_CHECK( cmd.find( "_overlap_0" ) == std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddBatchSymbolsSharePlacedBboxes )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( {
            nlohmann::json{
                { "element_type", "symbol" },
                { "lib_id", "Device:R" },
                { "position", { 50.8, 50.8 } }
            },
            nlohmann::json{
                { "element_type", "symbol" },
                { "lib_id", "Device:C" },
                { "position", { 76.2, 50.8 } }
            }
        } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    // Both symbols should have overlap checks
    BOOST_CHECK( cmd.find( "get_bounding_box(sym_0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "get_bounding_box(sym_1" ) != std::string::npos );

    // Both should append to shared placed_bboxes list on success (with ref for obstacle identification)
    BOOST_CHECK( cmd.find( "placed_bboxes.append({'ref': _new_ref_0, **_new_bbox_0})" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "placed_bboxes.append({'ref': _new_ref_1, **_new_bbox_1})" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddLabelGeneratesBboxCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" },
            { "text", "NET1" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "get_bounding_box(lbl_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AddLabelGeneratesRemoveOnOverlap )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "elements", nlohmann::json::array( { nlohmann::json{
            { "element_type", "label" },
            { "text", "NET1" },
            { "position", { 50.8, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_add", input );

    BOOST_CHECK( cmd.find( "remove_items([lbl_0])" ) != std::string::npos );
}


// --- sch_update overlap detection ---

BOOST_AUTO_TEST_CASE( UpdatePositionGeneratesOverlapPreamble )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "updates", nlohmann::json::array( { nlohmann::json{
            { "target", "R1" },
            { "position", { 76.2, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_update", input );

    BOOST_CHECK( cmd.find( "placed_bboxes = []" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "def _bboxes_overlap(a, b):" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "'id': str(_esym.id.value)" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "'ref': getattr(_esym, 'reference', '?')" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( UpdatePositionGeneratesRevertOnOverlap )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "updates", nlohmann::json::array( { nlohmann::json{
            { "target", "R1" },
            { "position", { 76.2, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_update", input );

    // Should save original position
    BOOST_CHECK( cmd.find( "_old_sym_x_0" ) != std::string::npos );
    BOOST_CHECK( cmd.find( "_old_sym_y_0" ) != std::string::npos );

    // Should revert and report on overlap
    BOOST_CHECK( cmd.find( "Move rejected: overlaps {_obstacle_ref_0}" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( UpdateWithoutPositionSkipsOverlapPreamble )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "updates", nlohmann::json::array( { nlohmann::json{
            { "target", "R1" },
            { "angle", 90.0 }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_update", input );

    BOOST_CHECK( cmd.find( "placed_bboxes" ) == std::string::npos );
    BOOST_CHECK( cmd.find( "_bboxes_overlap" ) == std::string::npos );
}


BOOST_AUTO_TEST_CASE( UpdatePositionSkipsSelfInOverlapCheck )
{
    SCH_CRUD_HANDLER handler;
    nlohmann::json input = {
        { "updates", nlohmann::json::array( { nlohmann::json{
            { "target", "R1" },
            { "position", { 76.2, 50.8 } }
        } } ) }
    };
    std::string cmd = handler.GetIPCCommand( "sch_update", input );

    BOOST_CHECK( cmd.find( "_pb.get('id') == _item_id_0" ) != std::string::npos );
}


BOOST_AUTO_TEST_SUITE_END()
