/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 * @author Jon Evans <jon@craftyjon.com>
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

#include <project.h>
#include <project/component_class_settings.h>
#include <project/net_settings.h>
#include <project/tuning_profiles.h>
#include <settings/json_settings_internals.h>
#include <project/project_file.h>
#include <project/project_file_self_write_filter.h>
#include <project/board_project_settings_params.h>
#include "project_file_watcher.h"
#include <settings/common_settings.h>
#include <settings/parameters.h>
#include <wildcards_and_files_ext.h>
#include <project/project_file.h>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/log.h>


///! Update the schema version whenever a migration is required
const int projectFileSchemaVersion = 3;


// Out-of-line so the unique_ptr<PROJECT_FILE_WATCHER> destructor sees
// the complete watcher type (header forward-declares it).
PROJECT_FILE::~PROJECT_FILE()
{
    // Defensive: any SCOPED_PROJECT_FILE_OBSERVERs still alive when we
    // disappear would otherwise dangle on m_projectFile and crash on
    // ~SCOPED_PROJECT_FILE_OBSERVER → UnregisterObserver(). Lifetime
    // ordering can flip on us in the multi-board flow: the MBSCH editor
    // frame is wxWidgets-pending-delete and disposed at idle, which can
    // run AFTER SETTINGS_MANAGER has unloaded a project (e.g. propagator
    // ephemeral load/unload of sibling sub-projects). Null each scoped's
    // back-pointer here so its own dtor takes the no-op branch.
    //
    // Iterate over a snapshot — SCOPED's dtor mutates m_scopedObservers,
    // but here we're going down anyway, so direct write is safe and
    // doesn't invalidate the loop.
    for( SCOPED_PROJECT_FILE_OBSERVER* scoped : m_scopedObservers )
    {
        scoped->m_projectFile = nullptr;
        scoped->m_observer    = nullptr;
    }

    m_scopedObservers.clear();
}


PROJECT_FILE::PROJECT_FILE( const wxString& aFullPath ) :
        JSON_SETTINGS( aFullPath, SETTINGS_LOC::PROJECT, projectFileSchemaVersion ),
        m_ErcSettings( nullptr ),
        m_SchematicSettings( nullptr ),
        m_BoardSettings( nullptr ),
        m_MultiBoardSettings(),
        m_sheets(),
        m_topLevelSheets(),
        m_boards(),
        m_boardInfos(),
        m_crossBoardConnections(),
        m_componentAssignments(),
        m_project( nullptr ),
        m_wasMigrated( false )
{
    // Keep old files around
    m_deleteLegacyAfterMigration = false;

    m_params.emplace_back( new PARAM_LIST<FILE_INFO_PAIR>( "sheets", &m_sheets, {} ) );

    m_params.emplace_back( new PARAM_LIST<TOP_LEVEL_SHEET_INFO>( "schematic.top_level_sheets",
            &m_topLevelSheets, {} ) );

    m_params.emplace_back( new PARAM_LIST<FILE_INFO_PAIR>( "boards", &m_boards, {} ) );

    // Multi-board project support
    m_params.emplace_back( new PARAM_LIST<BOARD_INFO>( "multi_board.boards", &m_boardInfos, {} ) );

    m_params.emplace_back( new PARAM_LIST<CROSS_BOARD_CONNECTION>(
            "multi_board.cross_board_connections", &m_crossBoardConnections, {} ) );

    m_params.emplace_back( new PARAM_LIST<COMPONENT_BOARD_ASSIGNMENT>(
            "multi_board.component_assignments", &m_componentAssignments, {} ) );

    // Altium-style multi-board CONTAINER project fields
    m_params.emplace_back( new PARAM<bool>( "multi_board.container",
            &m_isMultiBoardContainer, false ) );

    m_params.emplace_back( new PARAM<wxString>( "multi_board.mbs_file",
            &m_mbsFileName, wxEmptyString ) );

    m_params.emplace_back( new PARAM_LIST<SUB_PROJECT_INFO>( "multi_board.sub_projects",
            &m_subProjects, {} ) );

    m_params.emplace_back( new PARAM_LIST<MB_CROSS_BOARD_NET>( "multi_board.cross_board_nets",
            &m_crossBoardNets, {} ) );

    m_params.emplace_back( new PARAM_LIST<CUSTOM_MATE>( "multi_board.assembly_3d.mates",
            &m_customMates, {} ) );

    m_params.emplace_back( new PARAM_LIST<ASSEMBLY_INSTANCE_STATE>(
            "multi_board.assembly_3d.instances", &m_assemblyInstances, {} ) );

    m_params.emplace_back( new PARAM<double>(
            "multi_board.assembly_3d.collision_threshold_mm",
            &m_collisionThresholdMm, 0.5 ) );

    // Per-project 3D viewer state. Three independent params under
    // `viewer3d.*` so each can be missing / present independently and
    // an empty override set serialises as `{}` (no diff vs. defaults).
    // ClearUnknownKeys = true on the maps so an in-memory replace
    // doesn't retain stale keys from disk after a deep-merge save.
    {
        auto* colorParam = new PARAM_LAMBDA<nlohmann::json>( "viewer3d.color_overrides",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [layer, c] : m_3DViewerColorOverrides )
                        ret[ std::to_string( layer ) ] = c.ToCSSString().ToStdString();

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_3DViewerColorOverrides.clear();

                    if( !aJson.is_object() )
                        return;

                    for( const auto& [k, v] : aJson.items() )
                    {
                        if( !v.is_string() )
                            continue;

                        try
                        {
                            int layer = std::stoi( k );
                            KIGFX::COLOR4D c;
                            c.SetFromWxString(
                                    wxString::FromUTF8( v.get<std::string>().c_str() ) );
                            m_3DViewerColorOverrides[layer] = c;
                        }
                        catch( ... )
                        {
                            // Bad key/value — drop it. Project-file edit
                            // by hand or a forward-compat write from a
                            // newer version. No reason to refuse to load.
                        }
                    }
                },
                nlohmann::json::object() );

        colorParam->SetClearUnknownKeys( true );
        m_params.emplace_back( colorParam );

        auto* visParam = new PARAM_LAMBDA<nlohmann::json>( "viewer3d.visibility_overrides",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [layer, vis] : m_3DViewerVisibilityOverrides )
                        ret[ std::to_string( layer ) ] = vis;

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_3DViewerVisibilityOverrides.clear();

                    if( !aJson.is_object() )
                        return;

                    for( const auto& [k, v] : aJson.items() )
                    {
                        if( !v.is_boolean() )
                            continue;

                        try
                        {
                            int layer = std::stoi( k );
                            m_3DViewerVisibilityOverrides[layer] = v.get<bool>();
                        }
                        catch( ... )
                        {
                        }
                    }
                },
                nlohmann::json::object() );

        visParam->SetClearUnknownKeys( true );
        m_params.emplace_back( visParam );

        // Optional bool: null means "user hasn't chosen — fall through
        // to app-wide cfg." JSON null serialises as absence of key, so
        // unset projects get no entry on disk.
        m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "viewer3d.use_stackup_colors",
                [&]() -> nlohmann::json
                {
                    if( m_3DViewerUseStackupColors.has_value() )
                        return *m_3DViewerUseStackupColors;

                    return nullptr;
                },
                [&]( const nlohmann::json& aJson )
                {
                    if( aJson.is_boolean() )
                        m_3DViewerUseStackupColors = aJson.get<bool>();
                    else
                        m_3DViewerUseStackupColors.reset();
                },
                nullptr ) );
    }

    // M5.2: sub-project back-reference to the enclosing container
    // .kicad_pro (relative path). Empty for standalone or container
    // projects. Container-aware code prefers this O(1) ref over the
    // legacy 6-level directory walk.
    m_params.emplace_back( new PARAM<wxString>( "multi_board.container_project_relative_path",
            &m_containerProjectRelativePath, wxEmptyString ) );

    // Container-level rule: minimum pin count per named power net.
    // Persisted as a JSON object `{ "GND": 4, "VCC": 2 }`. Empty by
    // default; consumed by the per-board cross-board power DRC.
    //
    // ClearUnknownKeys=true is mandatory for every map-backed rule set
    // below: JSON_SETTINGS::SaveToFile builds `toSave = m_original` then
    // calls `update(merge_objects=true)`, which DEEP-MERGES JSON objects
    // key-by-key. Without ClearUnknownKeys the on-disk `{"GND":4}` would
    // survive an in-memory replace to `{"+3V3":10}` (you'd get both keys),
    // and an in-memory clear to `{}` would be a no-op. Setting the flag
    // makes SaveToFile zero out the JSON path first so the merge becomes
    // a true replace. Vector-backed rule sets (cross_board_diff_pairs)
    // don't need this — JSON arrays are fully replaced by `update`.
    {
        auto* param = new PARAM_LAMBDA<nlohmann::json>( "multi_board.min_power_pins",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [netName, minPins] : m_minPowerPins )
                        ret[netName.utf8_string()] = minPins;

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_minPowerPins.clear();

                    if( !aJson.is_object() )
                        return;

                    for( auto it = aJson.begin(); it != aJson.end(); ++it )
                    {
                        if( it.value().is_number_integer() )
                        {
                            m_minPowerPins[wxString::FromUTF8( it.key() )] =
                                    it.value().get<int>();
                        }
                    }
                },
                nlohmann::json::object() );

        param->SetClearUnknownKeys( true );
        m_params.emplace_back( param );
    }

    // Container-level rule: maximum total trace length (nanometers)
    // per cross-board net, summed across all sub-projects. Persisted
    // as `{ "DDR_CMD0": 75000000, "USB_DP": 80000000 }`.
    {
        auto* param = new PARAM_LAMBDA<nlohmann::json>( "multi_board.max_length_nm",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [netName, maxNm] : m_maxLengthNm )
                        ret[netName.utf8_string()] = maxNm;

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_maxLengthNm.clear();

                    if( !aJson.is_object() )
                        return;

                    for( auto it = aJson.begin(); it != aJson.end(); ++it )
                    {
                        if( it.value().is_number_integer() )
                        {
                            m_maxLengthNm[wxString::FromUTF8( it.key() )] =
                                    it.value().get<int64_t>();
                        }
                    }
                },
                nlohmann::json::object() );

        param->SetClearUnknownKeys( true );
        m_params.emplace_back( param );
    }

    // Container-level rule: per-net current capacity. Each entry holds
    // expected DC current draw and per-pin current rating; the per-board
    // DRC compares (numPins * pinRating) against expectedAmps.
    {
        auto* param = new PARAM_LAMBDA<nlohmann::json>( "multi_board.current_rules",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [netName, rule] : m_currentRules )
                    {
                        ret[netName.utf8_string()] = {
                                { "expected_amps",     rule.expectedAmps },
                                { "pin_rating_amps",   rule.pinRatingAmps }
                        };
                    }

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_currentRules.clear();

                    if( !aJson.is_object() )
                        return;

                    for( auto it = aJson.begin(); it != aJson.end(); ++it )
                    {
                        if( !it.value().is_object() )
                            continue;

                        MB_CURRENT_RULE rule;

                        if( it.value().contains( "expected_amps" )
                            && it.value()["expected_amps"].is_number() )
                        {
                            rule.expectedAmps = it.value()["expected_amps"].get<double>();
                        }

                        if( it.value().contains( "pin_rating_amps" )
                            && it.value()["pin_rating_amps"].is_number() )
                        {
                            rule.pinRatingAmps = it.value()["pin_rating_amps"].get<double>();
                        }

                        m_currentRules[wxString::FromUTF8( it.key() )] = rule;
                    }
                },
                nlohmann::json::object() );

        param->SetClearUnknownKeys( true );
        m_params.emplace_back( param );
    }

    // Container-level rule: per-net voltage drop. Optional override fields
    // (trace_width_um / trace_sheet_r_mohm / contact_r_pin_mohm) fall back
    // to documented defaults when zero or absent.
    {
        auto* param = new PARAM_LAMBDA<nlohmann::json>( "multi_board.voltage_rules",
                [&]() -> nlohmann::json
                {
                    nlohmann::json ret = nlohmann::json::object();

                    for( const auto& [netName, rule] : m_voltageRules )
                    {
                        nlohmann::json entry = {
                                { "expected_amps",          rule.expectedAmps },
                                { "max_drop_mv",            rule.maxDropMv }
                        };

                        if( rule.traceWidthUm > 0.0 )
                            entry["trace_width_um"] = rule.traceWidthUm;

                        if( rule.traceSheetRMOhmsPerSq > 0.0 )
                            entry["trace_sheet_r_mohm"] = rule.traceSheetRMOhmsPerSq;

                        if( rule.contactRPerPinMOhms > 0.0 )
                            entry["contact_r_pin_mohm"] = rule.contactRPerPinMOhms;

                        ret[netName.utf8_string()] = entry;
                    }

                    return ret;
                },
                [&]( const nlohmann::json& aJson )
                {
                    m_voltageRules.clear();

                    if( !aJson.is_object() )
                        return;

                    for( auto it = aJson.begin(); it != aJson.end(); ++it )
                    {
                        if( !it.value().is_object() )
                            continue;

                        MB_VOLTAGE_RULE rule;

                        if( it.value().contains( "expected_amps" )
                            && it.value()["expected_amps"].is_number() )
                        {
                            rule.expectedAmps = it.value()["expected_amps"].get<double>();
                        }

                        if( it.value().contains( "max_drop_mv" )
                            && it.value()["max_drop_mv"].is_number() )
                        {
                            rule.maxDropMv = it.value()["max_drop_mv"].get<double>();
                        }

                        if( it.value().contains( "trace_width_um" )
                            && it.value()["trace_width_um"].is_number() )
                        {
                            rule.traceWidthUm = it.value()["trace_width_um"].get<double>();
                        }

                        if( it.value().contains( "trace_sheet_r_mohm" )
                            && it.value()["trace_sheet_r_mohm"].is_number() )
                        {
                            rule.traceSheetRMOhmsPerSq =
                                    it.value()["trace_sheet_r_mohm"].get<double>();
                        }

                        if( it.value().contains( "contact_r_pin_mohm" )
                            && it.value()["contact_r_pin_mohm"].is_number() )
                        {
                            rule.contactRPerPinMOhms =
                                    it.value()["contact_r_pin_mohm"].get<double>();
                        }

                        m_voltageRules[wxString::FromUTF8( it.key() )] = rule;
                    }
                },
                nlohmann::json::object() );

        param->SetClearUnknownKeys( true );
        m_params.emplace_back( param );
    }

    // Container-level rule: cross-board diff-pair declarations.
    // Persisted as `[{"a":"USB_DP","b":"USB_DN"}, ...]`.
    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "multi_board.cross_board_diff_pairs",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret = nlohmann::json::array();

                for( const auto& [a, b] : m_crossBoardDiffPairs )
                {
                    nlohmann::json entry = {
                            { "a", a.utf8_string() },
                            { "b", b.utf8_string() }
                    };
                    ret.push_back( entry );
                }

                return ret;
            },
            [&]( const nlohmann::json& aJson )
            {
                m_crossBoardDiffPairs.clear();

                if( !aJson.is_array() )
                    return;

                for( const nlohmann::json& entry : aJson )
                {
                    if( !entry.is_object() )
                        continue;

                    if( !entry.contains( "a" ) || !entry["a"].is_string() )
                        continue;

                    if( !entry.contains( "b" ) || !entry["b"].is_string() )
                        continue;

                    m_crossBoardDiffPairs.emplace_back(
                            wxString::FromUTF8( entry["a"].get<std::string>() ),
                            wxString::FromUTF8( entry["b"].get<std::string>() ) );
                }
            },
            nlohmann::json::array() ) );

    m_params.emplace_back( new PARAM_WXSTRING_MAP( "text_variables",
            &m_TextVars, {}, false, true /* array behavior, even though stored as a map */ ) );

    m_params.emplace_back( new PARAM_LIST<wxString>( "libraries.pinned_symbol_libs",
            &m_PinnedSymbolLibs, {} ) );

    m_params.emplace_back( new PARAM_LIST<wxString>( "libraries.pinned_footprint_libs",
            &m_PinnedFootprintLibs, {} ) );

    m_params.emplace_back( new PARAM_PATH_LIST( "cvpcb.equivalence_files",
            &m_EquivalenceFiles, {} ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.page_layout_descr_file",
            &m_BoardDrawingSheetFile, "" ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.last_paths.netlist",
            &m_PcbLastPath[LAST_PATH_NETLIST], "" ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.last_paths.idf",
            &m_PcbLastPath[LAST_PATH_IDF], "" ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.last_paths.vrml",
            &m_PcbLastPath[LAST_PATH_VRML], "" ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.last_paths.specctra_dsn",
            &m_PcbLastPath[LAST_PATH_SPECCTRADSN], "" ) );

    m_params.emplace_back( new PARAM_PATH( "pcbnew.last_paths.plot",
            &m_PcbLastPath[LAST_PATH_PLOT], "" ) );

    m_params.emplace_back( new PARAM<wxString>( "schematic.legacy_lib_dir",
            &m_LegacyLibDir, "" ) );

    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "schematic.legacy_lib_list",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret = nlohmann::json::array();

                for( const wxString& libName : m_LegacyLibNames )
                    ret.push_back( libName );

                return ret;
            },
            [&]( const nlohmann::json& aJson )
            {
                if( aJson.empty() || !aJson.is_array() )
                    return;

                m_LegacyLibNames.clear();

                for( const nlohmann::json& entry : aJson )
                    m_LegacyLibNames.push_back( entry.get<wxString>() );
            }, {} ) );

    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "schematic.bus_aliases",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret = nlohmann::json::object();

                for( const auto& alias : m_BusAliases )
                {
                    nlohmann::json members = nlohmann::json::array();

                    for( const wxString& member : alias.second )
                        members.push_back( member );

                    ret[ alias.first.ToStdString() ] = members;
                }

                return ret;
            },
            [&]( const nlohmann::json& aJson )
            {
                if( aJson.empty() || !aJson.is_object() )
                    return;

                m_BusAliases.clear();

                for( auto it = aJson.begin(); it != aJson.end(); ++it )
                {
                    const nlohmann::json& membersJson = it.value();

                    if( !membersJson.is_array() )
                        continue;

                    std::vector<wxString> members;

                    for( const nlohmann::json& entry : membersJson )
                    {
                        if( entry.is_string() )
                        {
                            wxString member = entry.get<wxString>().Strip( wxString::both );

                            if( !member.IsEmpty() )
                                members.push_back( member );
                        }
                    }

                    wxString name = wxString::FromUTF8( it.key().c_str() ).Strip( wxString::both );

                    if( !name.IsEmpty() )
                        m_BusAliases.emplace( name, std::move( members ) );
                }
            }, {} ) );

    m_NetSettings = std::make_shared<NET_SETTINGS>( this, "net_settings" );

    m_ComponentClassSettings =
            std::make_shared<COMPONENT_CLASS_SETTINGS>( this, "component_class_settings" );

    m_tuningProfileParameters = std::make_shared<TUNING_PROFILES>( this, "tuning_profiles" );

    m_params.emplace_back( new PARAM_LAYER_PRESET( "board.layer_presets", &m_LayerPresets ) );

    m_params.emplace_back( new PARAM_VIEWPORT( "board.viewports", &m_Viewports ) );

    m_params.emplace_back( new PARAM_VIEWPORT3D( "board.3dviewports", &m_Viewports3D ) );

    m_params.emplace_back( new PARAM_LAYER_PAIRS( "board.layer_pairs", m_LayerPairInfos ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.internal_id",
            &m_IP2581Bom.id, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.mpn",
            &m_IP2581Bom.MPN, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.mfg",
            &m_IP2581Bom.mfg, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.distpn",
            &m_IP2581Bom.distPN, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.dist",
            &m_IP2581Bom.dist, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.bom_rev",
            &m_IP2581Bom.bomRev, wxEmptyString ) );

    m_params.emplace_back( new PARAM<wxString>( "board.ipc2581.sch_revision",
            &m_IP2581Bom.schRevision, wxEmptyString ) );


    registerMigration( 1, 2, std::bind( &PROJECT_FILE::migrateSchema1To2, this ) );
    registerMigration( 2, 3, std::bind( &PROJECT_FILE::migrateSchema2To3, this ) );
}


bool PROJECT_FILE::migrateSchema1To2()
{
    auto p( "/board/layer_presets"_json_pointer );

    if( !m_internals->contains( p ) || !m_internals->at( p ).is_array() )
        return true;

    nlohmann::json& presets = m_internals->at( p );

    for( nlohmann::json& entry : presets )
        PARAM_LAYER_PRESET::MigrateToV9Layers( entry );

    m_wasMigrated = true;

    return true;
}


bool PROJECT_FILE::migrateSchema2To3()
{
    auto p( "/board/layer_presets"_json_pointer );

    if( !m_internals->contains( p ) || !m_internals->at( p ).is_array() )
        return true;

    nlohmann::json& presets = m_internals->at( p );

    for( nlohmann::json& entry : presets )
        PARAM_LAYER_PRESET::MigrateToNamedRenderLayers( entry );

    m_wasMigrated = true;

    return true;
}


bool PROJECT_FILE::MigrateFromLegacy( wxConfigBase* aCfg )
{
    bool     ret = true;
    wxString str;
    long     index = 0;

    std::set<wxString> group_blacklist;

    // Legacy files don't store board info; they assume board matches project name
    // We will leave m_boards empty here so it can be populated with other code

    // First handle migration of data that will be stored locally in this object

    auto loadPinnedLibs =
            [&]( const std::string& aDest )
            {
                int      libIndex = 1;
                wxString libKey   = wxT( "PinnedItems" );
                libKey << libIndex;

                nlohmann::json libs = nlohmann::json::array();

                while( aCfg->Read( libKey, &str ) )
                {
                    libs.push_back( str );

                    aCfg->DeleteEntry( libKey, true );

                    libKey = wxT( "PinnedItems" );
                    libKey << ++libIndex;
                }

                Set( aDest, libs );
            };

    aCfg->SetPath( wxT( "/LibeditFrame" ) );
    loadPinnedLibs( "libraries.pinned_symbol_libs" );

    aCfg->SetPath( wxT( "/ModEditFrame" ) );
    loadPinnedLibs( "libraries.pinned_footprint_libs" );

    aCfg->SetPath( wxT( "/cvpcb/equfiles" ) );

    {
        int      eqIdx = 1;
        wxString eqKey = wxT( "EquName" );
        eqKey << eqIdx;

        nlohmann::json eqs = nlohmann::json::array();

        while( aCfg->Read( eqKey, &str ) )
        {
            eqs.push_back( str );

            eqKey = wxT( "EquName" );
            eqKey << ++eqIdx;
        }

        Set( "cvpcb.equivalence_files", eqs );
    }

    // All CvPcb params that we want to keep have been migrated above
    group_blacklist.insert( wxT( "/cvpcb" ) );

    aCfg->SetPath( wxT( "/eeschema" ) );
    fromLegacyString( aCfg, "LibDir", "schematic.legacy_lib_dir" );

    aCfg->SetPath( wxT( "/eeschema/libraries" ) );

    {
        int      libIdx = 1;
        wxString libKey = wxT( "LibName" );
        libKey << libIdx;

        nlohmann::json libs = nlohmann::json::array();

        while( aCfg->Read( libKey, &str ) )
        {
            libs.push_back( str );

            libKey = wxT( "LibName" );
            libKey << ++libIdx;
        }

        Set( "schematic.legacy_lib_list", libs );
    }

    group_blacklist.insert( wxT( "/eeschema" ) );

    aCfg->SetPath( wxT( "/text_variables" ) );

    {
        int      txtIdx = 1;
        wxString txtKey;
        txtKey << txtIdx;

        nlohmann::json vars = nlohmann::json();

        while( aCfg->Read( txtKey, &str ) )
        {
            wxArrayString tokens = wxSplit( str, ':' );

            if( tokens.size() == 2 )
                vars[ tokens[0].ToStdString() ] = tokens[1];

            txtKey.clear();
            txtKey << ++txtIdx;
        }

        Set( "text_variables", vars );
    }

    group_blacklist.insert( wxT( "/text_variables" ) );

    aCfg->SetPath( wxT( "/schematic_editor" ) );

    fromLegacyString( aCfg, "PageLayoutDescrFile",     "schematic.page_layout_descr_file" );
    fromLegacyString( aCfg, "PlotDirectoryName",       "schematic.plot_directory" );
    fromLegacyString( aCfg, "NetFmtName",              "schematic.net_format_name" );
    fromLegacy<bool>( aCfg, "SpiceAjustPassiveValues", "schematic.spice_adjust_passive_values" );
    fromLegacy<int>(  aCfg, "SubpartIdSeparator",      "schematic.subpart_id_separator" );
    fromLegacy<int>(  aCfg, "SubpartFirstId",          "schematic.subpart_first_id" );

    fromLegacy<int>( aCfg, "LineThickness",            "schematic.drawing.default_line_thickness" );
    fromLegacy<int>( aCfg, "WireThickness",            "schematic.drawing.default_wire_thickness" );
    fromLegacy<int>( aCfg, "BusThickness",             "schematic.drawing.default_bus_thickness" );
    fromLegacy<int>( aCfg, "LabSize",                  "schematic.drawing.default_text_size" );

    if( !fromLegacy<int>( aCfg, "PinSymbolSize",       "schematic.drawing.pin_symbol_size" ) )
    {
        // Use the default symbol size algorithm of Eeschema V5 (based on pin name/number size)
        Set( "schematic.drawing.pin_symbol_size", 0 );
    }

    fromLegacy<int>( aCfg, "JunctionSize",             "schematic.drawing.default_junction_size" );

    fromLegacyString( aCfg, "FieldNameTemplates",    "schematic.drawing.field_names" );

    if( !fromLegacy<double>( aCfg, "TextOffsetRatio",  "schematic.drawing.text_offset_ratio" ) )
    {
        // Use the spacing of Eeschema V5
        Set( "schematic.drawing.text_offset_ratio", 0.08 );
        Set( "schematic.drawing.label_size_ratio", 0.25 );
    }

    // All schematic_editor keys we keep are migrated above
    group_blacklist.insert( wxT( "/schematic_editor" ) );

    aCfg->SetPath( wxT( "/pcbnew" ) );

    fromLegacyString( aCfg, "PageLayoutDescrFile",       "pcbnew.page_layout_descr_file" );
    fromLegacyString( aCfg, "LastNetListRead",           "pcbnew.last_paths.netlist" );
    fromLegacyString( aCfg, "LastSTEPExportPath",        "pcbnew.last_paths.step" );
    fromLegacyString( aCfg, "LastIDFExportPath",         "pcbnew.last_paths.idf" );
    fromLegacyString( aCfg, "LastVRMLExportPath",        "pcbnew.last_paths.vmrl" );
    fromLegacyString( aCfg, "LastSpecctraDSNExportPath", "pcbnew.last_paths.specctra_dsn" );
    fromLegacyString( aCfg, "LastGenCADExportPath",      "pcbnew.last_paths.gencad" );

    std::string bp = "board.design_settings.";

    {
        int      idx = 1;
        wxString key = wxT( "DRCExclusion" );
        key << idx;

        nlohmann::json exclusions = nlohmann::json::array();

        while( aCfg->Read( key, &str ) )
        {
            exclusions.push_back( str );

            key = wxT( "DRCExclusion" );
            key << ++idx;
        }

        Set( bp + "drc_exclusions", exclusions );
    }

    fromLegacy<bool>( aCfg,   "AllowMicroVias",  bp + "rules.allow_microvias" );
    fromLegacy<bool>( aCfg,   "AllowBlindVias",  bp + "rules.allow_blind_buried_vias" );
    fromLegacy<double>( aCfg, "MinClearance",    bp + "rules.min_clearance" );
    fromLegacy<double>( aCfg, "MinTrackWidth",   bp + "rules.min_track_width" );
    fromLegacy<double>( aCfg, "MinViaAnnulus",   bp + "rules.min_via_annulus" );
    fromLegacy<double>( aCfg, "MinViaDiameter",  bp + "rules.min_via_diameter" );

    if( !fromLegacy<double>( aCfg, "MinThroughDrill", bp + "rules.min_through_hole_diameter" ) )
        fromLegacy<double>( aCfg, "MinViaDrill", bp + "rules.min_through_hole_diameter" );

    fromLegacy<double>( aCfg, "MinMicroViaDiameter",  bp + "rules.min_microvia_diameter" );
    fromLegacy<double>( aCfg, "MinMicroViaDrill",     bp + "rules.min_microvia_drill" );
    fromLegacy<double>( aCfg, "MinHoleToHole",        bp + "rules.min_hole_to_hole" );
    fromLegacy<double>( aCfg, "CopperEdgeClearance",  bp + "rules.min_copper_edge_clearance" );
    fromLegacy<double>( aCfg, "SolderMaskClearance",  bp + "rules.solder_mask_clearance" );
    fromLegacy<double>( aCfg, "SolderMaskMinWidth",   bp + "rules.solder_mask_min_width" );
    fromLegacy<double>( aCfg, "SolderPasteClearance", bp + "rules.solder_paste_clearance" );
    fromLegacy<double>( aCfg, "SolderPasteRatio",     bp + "rules.solder_paste_margin_ratio" );

    if( !fromLegacy<double>( aCfg, "SilkLineWidth", bp + "defaults.silk_line_width" ) )
        fromLegacy<double>( aCfg, "ModuleOutlineThickness", bp + "defaults.silk_line_width" );

    if( !fromLegacy<double>( aCfg, "SilkTextSizeV", bp + "defaults.silk_text_size_v" ) )
        fromLegacy<double>( aCfg, "ModuleTextSizeV", bp + "defaults.silk_text_size_v" );

    if( !fromLegacy<double>( aCfg, "SilkTextSizeH", bp + "defaults.silk_text_size_h" ) )
        fromLegacy<double>( aCfg, "ModuleTextSizeH", bp + "defaults.silk_text_size_h" );

    if( !fromLegacy<double>( aCfg, "SilkTextSizeThickness", bp + "defaults.silk_text_thickness" ) )
        fromLegacy<double>( aCfg, "ModuleTextSizeThickness", bp + "defaults.silk_text_thickness" );

    fromLegacy<bool>( aCfg, "SilkTextItalic",   bp + "defaults.silk_text_italic" );
    fromLegacy<bool>( aCfg, "SilkTextUpright",  bp + "defaults.silk_text_upright" );

    if( !fromLegacy<double>( aCfg, "CopperLineWidth", bp + "defaults.copper_line_width" ) )
        fromLegacy<double>( aCfg, "DrawSegmentWidth", bp + "defaults.copper_line_width" );

    if( !fromLegacy<double>( aCfg, "CopperTextSizeV", bp + "defaults.copper_text_size_v" ) )
        fromLegacy<double>( aCfg, "PcbTextSizeV", bp + "defaults.copper_text_size_v" );

    if( !fromLegacy<double>( aCfg, "CopperTextSizeH", bp + "defaults.copper_text_size_h" ) )
        fromLegacy<double>( aCfg, "PcbTextSizeH", bp + "defaults.copper_text_size_h" );

    if( !fromLegacy<double>( aCfg, "CopperTextThickness", bp + "defaults.copper_text_thickness" ) )
        fromLegacy<double>( aCfg, "PcbTextThickness", bp + "defaults.copper_text_thickness" );

    fromLegacy<bool>( aCfg, "CopperTextItalic",   bp + "defaults.copper_text_italic" );
    fromLegacy<bool>( aCfg, "CopperTextUpright",  bp + "defaults.copper_text_upright" );

    if( !fromLegacy<double>( aCfg, "EdgeCutLineWidth", bp + "defaults.board_outline_line_width" ) )
        fromLegacy<double>( aCfg, "BoardOutlineThickness",
                            bp + "defaults.board_outline_line_width" );

    fromLegacy<double>( aCfg, "CourtyardLineWidth",   bp + "defaults.courtyard_line_width" );

    fromLegacy<double>( aCfg, "FabLineWidth",         bp + "defaults.fab_line_width" );
    fromLegacy<double>( aCfg, "FabTextSizeV",         bp + "defaults.fab_text_size_v" );
    fromLegacy<double>( aCfg, "FabTextSizeH",         bp + "defaults.fab_text_size_h" );
    fromLegacy<double>( aCfg, "FabTextSizeThickness", bp + "defaults.fab_text_thickness" );
    fromLegacy<bool>(   aCfg, "FabTextItalic",        bp + "defaults.fab_text_italic" );
    fromLegacy<bool>(   aCfg, "FabTextUpright",       bp + "defaults.fab_text_upright" );

    if( !fromLegacy<double>( aCfg, "OthersLineWidth", bp + "defaults.other_line_width" ) )
        fromLegacy<double>( aCfg, "ModuleOutlineThickness", bp + "defaults.other_line_width" );

    fromLegacy<double>( aCfg, "OthersTextSizeV",         bp + "defaults.other_text_size_v" );
    fromLegacy<double>( aCfg, "OthersTextSizeH",         bp + "defaults.other_text_size_h" );
    fromLegacy<double>( aCfg, "OthersTextSizeThickness", bp + "defaults.other_text_thickness" );
    fromLegacy<bool>(   aCfg, "OthersTextItalic",        bp + "defaults.other_text_italic" );
    fromLegacy<bool>(   aCfg, "OthersTextUpright",       bp + "defaults.other_text_upright" );

    fromLegacy<int>( aCfg, "DimensionUnits",     bp + "defaults.dimension_units" );
    fromLegacy<int>( aCfg, "DimensionPrecision", bp + "defaults.dimension_precision" );

    std::string sev = bp + "rule_severities";

    fromLegacy<bool>( aCfg, "RequireCourtyardDefinitions", sev + "legacy_no_courtyard_defined" );

    fromLegacy<bool>( aCfg, "ProhibitOverlappingCourtyards", sev + "legacy_courtyards_overlap" );

    {
        int      idx     = 1;
        wxString keyBase = "TrackWidth";
        wxString key     = keyBase;
        double   val;

        nlohmann::json widths = nlohmann::json::array();

        key << idx;

        while( aCfg->Read( key, &val ) )
        {
            widths.push_back( val );
            key = keyBase;
            key << ++idx;
        }

        Set( bp + "track_widths", widths );
    }

    {
        int      idx     = 1;
        wxString keyBase = "ViaDiameter";
        wxString key     = keyBase;
        double   diameter;
        double   drill   = 1.0;

        nlohmann::json vias = nlohmann::json::array();

        key << idx;

        while( aCfg->Read( key, &diameter ) )
        {
            key = "ViaDrill";
            aCfg->Read( key << idx, &drill );

            nlohmann::json via = { { "diameter", diameter }, { "drill", drill } };
            vias.push_back( via );

            key = keyBase;
            key << ++idx;
        }

        Set( bp + "via_dimensions", vias );
    }

    {
        int      idx     = 1;
        wxString keyBase = "dPairWidth";
        wxString key     = keyBase;
        double   width;
        double   gap     = 1.0;
        double   via_gap = 1.0;

        nlohmann::json pairs = nlohmann::json::array();

        key << idx;

        while( aCfg->Read( key, &width ) )
        {
            key = "dPairGap";
            aCfg->Read( key << idx, &gap );

            key = "dPairViaGap";
            aCfg->Read( key << idx, &via_gap );

            nlohmann::json pair = { { "width", width }, { "gap", gap }, { "via_gap", via_gap } };
            pairs.push_back( pair );

            key = keyBase;
            key << ++idx;
        }

        Set( bp + "diff_pair_dimensions",  pairs );
    }

    group_blacklist.insert( wxT( "/pcbnew" ) );

    // General group is unused these days, we can throw it away
    group_blacklist.insert( wxT( "/general" ) );

    // Next load sheet names and put all other legacy data in the legacy dict
    aCfg->SetPath( wxT( "/" ) );

    auto loadSheetNames =
            [&]() -> bool
            {
                int            sheet = 1;
                wxString       entry;
                nlohmann::json arr   = nlohmann::json::array();

                wxLogTrace( traceSettings, wxT( "Migrating sheet names" ) );

                aCfg->SetPath( wxT( "/sheetnames" ) );

                while( aCfg->Read( wxString::Format( "%d", sheet++ ), &entry ) )
                {
                    wxArrayString tokens = wxSplit( entry, ':' );

                    if( tokens.size() == 2 )
                    {
                        wxLogTrace( traceSettings, wxT( "%d: %s = %s" ), sheet, tokens[0],
                                    tokens[1] );
                        arr.push_back( nlohmann::json::array( { tokens[0], tokens[1] } ) );
                    }
                }

                Set( "sheets", arr );

                aCfg->SetPath( "/" );

                return true;
            };

    std::vector<wxString> groups;

    groups.emplace_back( wxEmptyString );

    auto loadLegacyPairs =
            [&]( const std::string& aGroup ) -> bool
            {
                wxLogTrace( traceSettings, wxT( "Migrating group %s" ), aGroup );
                bool     success = true;
                wxString keyStr;
                wxString val;

                index = 0;

                while( aCfg->GetNextEntry( keyStr, index ) )
                {
                    if( !aCfg->Read( keyStr, &val ) )
                        continue;

                    std::string key( keyStr.ToUTF8() );

                    wxLogTrace( traceSettings, wxT( "    %s = %s" ), key, val );

                    try
                    {
                        Set( "legacy." + aGroup + "." + key, val );
                    }
                    catch( ... )
                    {
                        success = false;
                    }
                }

                return success;
            };

    for( size_t i = 0; i < groups.size(); i++ )
    {
        aCfg->SetPath( groups[i] );

        if( groups[i] == wxT( "/sheetnames" ) )
        {
            ret |= loadSheetNames();
            continue;
        }

        aCfg->DeleteEntry( wxT( "last_client" ), true );
        aCfg->DeleteEntry( wxT( "update" ), true );
        aCfg->DeleteEntry( wxT( "version" ), true );

        ret &= loadLegacyPairs( groups[i].ToStdString() );

        index = 0;

        while( aCfg->GetNextGroup( str, index ) )
        {
            wxString group = groups[i] + "/" + str;

            if( !group_blacklist.count( group ) )
                groups.emplace_back( group );
        }

        aCfg->SetPath( "/" );
    }

    return ret;
}


bool PROJECT_FILE::LoadFromFile( const wxString& aDirectory )
{
    bool success = JSON_SETTINGS::LoadFromFile( aDirectory );

    if( success )
    {
        // Migrate from old single-root format to top_level_sheets format.
        // Multi-board containers have no root schematic of their own — their
        // MBS is referenced via multi_board.mbs_file — so leave the list empty
        // on a container project; otherwise loaders would try to open a
        // `<project>.kicad_sch` that never exists.
        if( m_topLevelSheets.empty() && m_project && !m_isMultiBoardContainer )
        {
            // Create a default top-level sheet entry based on the project name
            wxString projectName = m_project->GetProjectName();

            TOP_LEVEL_SHEET_INFO defaultSheet;
            defaultSheet.uuid = niluuid;  // Use niluuid for the first/default sheet
            defaultSheet.name = projectName;
            defaultSheet.filename = projectName + ".kicad_sch";

            m_topLevelSheets.push_back( std::move( defaultSheet ) );

            // Mark as migrated so it will be saved with the new format
            m_wasMigrated = true;

            wxLogTrace( traceSettings, wxT( "PROJECT_FILE: Migrated old single-root format to top_level_sheets" ) );
        }

        // When a project is created from a template, the top_level_sheets entries may
        // still reference the template's schematic filenames rather than the new project's.
        // The template copy renames files on disk but doesn't update the .kicad_pro content.
        // Detect this and fix the references so the schematic can be found.
        if( !m_topLevelSheets.empty() && m_project )
        {
            wxString projectPath = m_project->GetProjectPath();
            wxString projectName = m_project->GetProjectName();

            for( TOP_LEVEL_SHEET_INFO& sheetInfo : m_topLevelSheets )
            {
                wxFileName referencedFile( projectPath, sheetInfo.filename );

                if( referencedFile.FileExists() )
                    continue;

                // Try the project-name-based filename
                wxString expectedFile =
                        projectName + wxS( "." ) + FILEEXT::KiCadSchematicFileExtension;

                wxFileName candidateFile( projectPath, expectedFile );

                if( candidateFile.FileExists() )
                {
                    wxLogTrace( traceSettings,
                                wxT( "PROJECT_FILE: Fixing stale top_level_sheets reference "
                                     "'%s' -> '%s'" ),
                                sheetInfo.filename, expectedFile );

                    sheetInfo.filename = expectedFile;
                    sheetInfo.name = projectName;
                    m_wasMigrated = true;
                }
            }
        }
    }

    return success;
}


bool PROJECT_FILE::SaveToFile( const wxString& aDirectory, bool aForce )
{
    // T3 Phase 3 — reentrancy guard.
    //
    // A save can be triggered recursively if any code reachable from
    // JSON serialisation (PARAM_LAMBDA getters, observers, event
    // handlers fired by Set/Store) calls SaveToFile again. Without
    // a guard the on-disk file ends up half-written. We coalesce: the
    // outermost call does the actual write, recursive calls just set
    // the "save again when done" flag and return success.
    //
    // Note: this is single-threaded reentrancy, not cross-thread —
    // KiCad's main thread does all I/O. No std::atomic needed.
    if( m_saveInProgress )
    {
        m_saveAgainPending = true;
        return true;
    }

    m_saveInProgress = true;

    // Defensive write-site guard: if aDirectory is empty AND m_filename
    // is also a bare basename (no directory), JSON_SETTINGS::SaveToFile
    // would write to the current working directory — which during a
    // multi-board peer-load sequence is the CONTAINER's directory, not
    // the sub-project's. Result is a stray <sub-project>.kicad_pro at
    // the container root. Refuse the save and log so the upstream caller
    // is identifiable.
    {
        wxFileName fn( GetFilename() );

        if( aDirectory.IsEmpty() && fn.GetPath().IsEmpty() )
        {
            wxLogWarning( wxT( "PROJECT_FILE::SaveToFile REFUSED — both aDirectory "
                               "and m_filename ('%s') lack a directory component. "
                               "Saving would write to cwd and produce a stray "
                               "%s.%s. Caller must pass an absolute aDirectory." ),
                          GetFilename(),
                          wxFileName( GetFilename() ).GetName(),
                          FILEEXT::ProjectFileExtension );
            m_saveInProgress = false;
            return false;
        }
    }

    // Standalone edit/save paths (e.g. the MBSCH save hook writing
    // cross_board_nets into a sibling container) construct a
    // PROJECT_FILE without routing through SETTINGS_MANAGER, so
    // m_project is null. Fall back to deriving the project name from
    // the JSON_SETTINGS filename instead of crashing in that case.
    wxString projectName;

    if( m_project )
        projectName = m_project->GetProjectName();
    else
        projectName = wxFileName( GetFilename() ).GetName();

    Set( "meta.filename", projectName + "." + FILEEXT::ProjectFileExtension );

    // Loop on m_saveAgainPending so a recursive save during the parent
    // serialisation gets reflected without a new external call. Bound
    // to a small iteration limit to defend against pathological
    // setter/observer feedback.
    bool result = false;
    int  attempts = 0;

    do
    {
        m_saveAgainPending = false;

        // Even if parameters were not modified, we should resave after
        // migration. Captured here so a re-entered save inside the loop
        // doesn't lose the migration flag.
        bool force = aForce || m_wasMigrated;

        // If we're actually going ahead and doing the save, the flag
        // that keeps code from doing the save should be cleared.
        m_wasMigrated = false;

        result = JSON_SETTINGS::SaveToFile( aDirectory, force );

        if( !result )
            break;

        // Stamp the self-write filter so Phase 4's file watcher
        // ignores the modification event we just generated. Use the
        // canonical absolute path the JSON_SETTINGS layer wrote to —
        // we reconstruct it the same way SaveToFile does internally
        // (aDirectory + GetFilename() + extension).
        wxFileName written;

        if( aDirectory.IsEmpty() )
            written.Assign( GetFilename() );
        else
            written.Assign( aDirectory, GetFilename() );

        // SaveToFile appends the project file extension if missing.
        if( written.GetExt().IsEmpty() )
            written.SetExt( wxString::FromUTF8( FILEEXT::ProjectFileExtension ) );

        if( written.IsOk() )
            PROJECT_FILE_SELF_WRITE_FILTER::Mark( written.GetFullPath() );

        // T3 Phase 4: in-memory state matches disk again. Cleared
        // after each successful inner save so a re-entered SaveToFile
        // sees a fresh-clean state.
        m_multiBoardDirty = false;
    }
    while( m_saveAgainPending && ++attempts < 4 );

    if( m_saveAgainPending )
    {
        // Pathological feedback — give up the loop, log a hint. The
        // pending flag is cleared so the next external SaveToFile
        // starts fresh.
        wxLogTrace( wxT( "PROJECT_FILE" ),
                    wxT( "SaveToFile gave up reentrant retry after %d attempts" ),
                    attempts );
        m_saveAgainPending = false;
    }

    m_saveInProgress = false;
    return result;
}


bool PROJECT_FILE::SaveAs( const wxString& aDirectory, const wxString& aFile )
{
    wxFileName oldFilename( GetFilename() );
    wxString   oldProjectName = oldFilename.GetName();
    wxString   oldProjectPath = oldFilename.GetPath();

    Set( "meta.filename", aFile + "." + FILEEXT::ProjectFileExtension );
    SetFilename( aFile );

    auto updatePath =
            [&]( wxString& aPath )
            {
                if( aPath.StartsWith( oldProjectName + wxS( "." ) ) )
                    aPath.Replace( oldProjectName, aFile, false );
                else if( aPath.StartsWith( oldProjectPath + wxS( "/" ) ) )
                    aPath.Replace( oldProjectPath, aDirectory, false );
            };

    updatePath( m_BoardDrawingSheetFile );

    for( int ii = LAST_PATH_FIRST; ii < (int) LAST_PATH_SIZE; ++ii )
        updatePath( m_PcbLastPath[ ii ] );

    auto updatePathByPtr =
            [&]( const std::string& aPtr )
            {
                if( std::optional<wxString> path = Get<wxString>( aPtr ) )
                {
                    updatePath( path.value() );
                    Set( aPtr, path.value() );
                }
            };

    updatePathByPtr( "schematic.page_layout_descr_file" );
    updatePathByPtr( "schematic.plot_directory" );
    updatePathByPtr( "schematic.ngspice.workbook_filename" );
    updatePathByPtr( "pcbnew.page_layout_descr_file" );

    for( auto& sheetInfo : m_topLevelSheets )
    {
        updatePath( sheetInfo.filename );

        // Also update the display name if it matches the old project name
        if( sheetInfo.name == oldProjectName )
            sheetInfo.name = aFile;
    }

    // If we're actually going ahead and doing the save, the flag that keeps code from doing the save
    // should be cleared at this point
    m_wasMigrated = false;

    // While performing Save As, we have already checked that we can write to the directory
    // so don't carry the previous flag
    SetReadOnly( false );
    return JSON_SETTINGS::SaveToFile( aDirectory, true );
}


wxString PROJECT_FILE::getFileExt() const
{
    return FILEEXT::ProjectFileExtension;
}


wxString PROJECT_FILE::getLegacyFileExt() const
{
    return FILEEXT::LegacyProjectFileExtension;
}


void to_json( nlohmann::json& aJson, const FILE_INFO_PAIR& aPair )
{
    aJson = nlohmann::json::array( { aPair.first.AsString().ToUTF8(), aPair.second.ToUTF8() } );
}


void from_json( const nlohmann::json& aJson, FILE_INFO_PAIR& aPair )
{
    wxCHECK( aJson.is_array() && aJson.size() == 2, /* void */ );
    aPair.first  = KIID( wxString( aJson[0].get<std::string>().c_str(), wxConvUTF8 ) );
    aPair.second = wxString( aJson[1].get<std::string>().c_str(), wxConvUTF8 );
}


void to_json( nlohmann::json& aJson, const TOP_LEVEL_SHEET_INFO& aInfo )
{
    aJson = nlohmann::json::object();
    aJson["uuid"] = aInfo.uuid.AsString().ToUTF8();
    aJson["name"] = aInfo.name.ToUTF8();
    aJson["filename"] = aInfo.filename.ToUTF8();
}


void from_json( const nlohmann::json& aJson, TOP_LEVEL_SHEET_INFO& aInfo )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "uuid" ) )
        aInfo.uuid = KIID( wxString( aJson["uuid"].get<std::string>().c_str(), wxConvUTF8 ) );

    if( aJson.contains( "name" ) )
        aInfo.name = wxString( aJson["name"].get<std::string>().c_str(), wxConvUTF8 );

    if( aJson.contains( "filename" ) )
        aInfo.filename = wxString( aJson["filename"].get<std::string>().c_str(), wxConvUTF8 );
}


// =============================================================================
// Multi-board support implementations
// =============================================================================

BOARD_INFO* PROJECT_FILE::GetBoardInfo( const KIID& aUuid )
{
    for( BOARD_INFO& info : m_boardInfos )
    {
        if( info.uuid == aUuid )
            return &info;
    }
    return nullptr;
}


const BOARD_INFO* PROJECT_FILE::GetBoardInfo( const KIID& aUuid ) const
{
    for( const BOARD_INFO& info : m_boardInfos )
    {
        if( info.uuid == aUuid )
            return &info;
    }
    return nullptr;
}


BOARD_INFO* PROJECT_FILE::GetActiveBoardInfo()
{
    for( BOARD_INFO& info : m_boardInfos )
    {
        if( info.isActive )
            return &info;
    }

    // If no board is marked active but we have boards, return the first one
    if( !m_boardInfos.empty() )
        return &m_boardInfos.front();

    return nullptr;
}


bool PROJECT_FILE::SetActiveBoard( const KIID& aUuid )
{
    bool found = false;

    for( BOARD_INFO& info : m_boardInfos )
    {
        if( info.uuid == aUuid )
        {
            info.isActive = true;
            found = true;
        }
        else
        {
            info.isActive = false;
        }
    }

    return found;
}


void PROJECT_FILE::AddBoard( const BOARD_INFO& aInfo )
{
    // Check if board with same UUID already exists
    for( const BOARD_INFO& existing : m_boardInfos )
    {
        if( existing.uuid == aInfo.uuid )
            return;  // Already exists
    }

    m_boardInfos.push_back( aInfo );

    // If this is the first board, make it active
    if( m_boardInfos.size() == 1 )
        m_boardInfos.back().isActive = true;
}


bool PROJECT_FILE::RemoveBoard( const KIID& aUuid )
{
    auto it = std::find_if( m_boardInfos.begin(), m_boardInfos.end(),
            [&aUuid]( const BOARD_INFO& info ) { return info.uuid == aUuid; } );

    if( it == m_boardInfos.end() )
        return false;

    bool wasActive = it->isActive;
    m_boardInfos.erase( it );

    // If we removed the active board, make the first remaining board active
    if( wasActive && !m_boardInfos.empty() )
        m_boardInfos.front().isActive = true;

    // Also remove any cross-board connections involving this board
    m_crossBoardConnections.erase(
            std::remove_if( m_crossBoardConnections.begin(), m_crossBoardConnections.end(),
                    [&aUuid]( const CROSS_BOARD_CONNECTION& conn )
                    {
                        return conn.board1Uuid == aUuid || conn.board2Uuid == aUuid;
                    } ),
            m_crossBoardConnections.end() );

    // Also remove component assignments to this board
    for( COMPONENT_BOARD_ASSIGNMENT& assignment : m_componentAssignments )
    {
        assignment.boardUuids.erase(
                std::remove( assignment.boardUuids.begin(), assignment.boardUuids.end(), aUuid ),
                assignment.boardUuids.end() );
    }

    // Clean up empty assignments
    m_componentAssignments.erase(
            std::remove_if( m_componentAssignments.begin(), m_componentAssignments.end(),
                    []( const COMPONENT_BOARD_ASSIGNMENT& a ) { return a.boardUuids.empty(); } ),
            m_componentAssignments.end() );

    return true;
}


void PROJECT_FILE::AddCrossBoardConnection( const CROSS_BOARD_CONNECTION& aConnection )
{
    // Check for duplicates (including reverse direction)
    for( const CROSS_BOARD_CONNECTION& existing : m_crossBoardConnections )
    {
        if( existing == aConnection )
            return;

        // Also check reverse direction
        if( existing.board1Uuid == aConnection.board2Uuid &&
            existing.pad1Uuid == aConnection.pad2Uuid &&
            existing.board2Uuid == aConnection.board1Uuid &&
            existing.pad2Uuid == aConnection.pad1Uuid )
            return;
    }

    m_crossBoardConnections.push_back( aConnection );
}


bool PROJECT_FILE::RemoveCrossBoardConnection( const KIID& aBoard1, const KIID& aPad1,
                                                const KIID& aBoard2, const KIID& aPad2 )
{
    auto it = std::find_if( m_crossBoardConnections.begin(), m_crossBoardConnections.end(),
            [&]( const CROSS_BOARD_CONNECTION& conn )
            {
                // Match in either direction
                return ( conn.board1Uuid == aBoard1 && conn.pad1Uuid == aPad1 &&
                         conn.board2Uuid == aBoard2 && conn.pad2Uuid == aPad2 ) ||
                       ( conn.board1Uuid == aBoard2 && conn.pad1Uuid == aPad2 &&
                         conn.board2Uuid == aBoard1 && conn.pad2Uuid == aPad1 );
            } );

    if( it == m_crossBoardConnections.end() )
        return false;

    m_crossBoardConnections.erase( it );
    return true;
}


COMPONENT_BOARD_ASSIGNMENT* PROJECT_FILE::GetComponentAssignment( const wxString& aReference )
{
    for( COMPONENT_BOARD_ASSIGNMENT& assignment : m_componentAssignments )
    {
        if( assignment.reference == aReference )
            return &assignment;
    }
    return nullptr;
}


void PROJECT_FILE::AssignComponentToBoard( const wxString& aReference, const KIID& aBoardUuid,
                                            bool aReplace )
{
    COMPONENT_BOARD_ASSIGNMENT* existing = GetComponentAssignment( aReference );

    if( existing )
    {
        if( aReplace )
        {
            existing->boardUuids.clear();
            existing->boardUuids.push_back( aBoardUuid );
        }
        else
        {
            // Add to existing assignment if not already present
            if( !existing->IsAssignedTo( aBoardUuid ) )
                existing->boardUuids.push_back( aBoardUuid );
        }
    }
    else
    {
        m_componentAssignments.emplace_back( aReference, aBoardUuid );
    }
}


void PROJECT_FILE::UnassignComponentFromBoard( const wxString& aReference, const KIID& aBoardUuid )
{
    COMPONENT_BOARD_ASSIGNMENT* existing = GetComponentAssignment( aReference );

    if( existing )
    {
        existing->boardUuids.erase(
                std::remove( existing->boardUuids.begin(), existing->boardUuids.end(), aBoardUuid ),
                existing->boardUuids.end() );

        // If no boards left, remove the assignment entirely
        if( existing->boardUuids.empty() )
        {
            m_componentAssignments.erase(
                    std::remove_if( m_componentAssignments.begin(), m_componentAssignments.end(),
                            [&aReference]( const COMPONENT_BOARD_ASSIGNMENT& a )
                            {
                                return a.reference == aReference;
                            } ),
                    m_componentAssignments.end() );
        }
    }
}


// =============================================================================
// Multi-board CONTAINER project management (sub-projects + cross-board nets)
// =============================================================================

SUB_PROJECT_INFO* PROJECT_FILE::GetSubProject( const KIID& aUuid )
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
                            [&aUuid]( const SUB_PROJECT_INFO& sp )
                            { return sp.uuid == aUuid; } );

    return ( it != m_subProjects.end() ) ? &( *it ) : nullptr;
}


const SUB_PROJECT_INFO* PROJECT_FILE::GetSubProject( const KIID& aUuid ) const
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
                            [&aUuid]( const SUB_PROJECT_INFO& sp )
                            { return sp.uuid == aUuid; } );

    return ( it != m_subProjects.end() ) ? &( *it ) : nullptr;
}


void PROJECT_FILE::AddSubProject( const SUB_PROJECT_INFO& aInfo )
{
    m_subProjects.push_back( aInfo );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::SUB_PROJECTS );
}


wxFileName PROJECT_FILE::ResolveMbsPath() const
{
    if( m_mbsFileName.IsEmpty() )
        return wxFileName();

    wxFileName containerDir( GetFullFilename() );
    containerDir.SetFullName( wxEmptyString );

    return wxFileName( containerDir.GetPath(), m_mbsFileName );
}


wxFileName PROJECT_FILE::ResolveSubProjectPath( const SUB_PROJECT_INFO& aInfo ) const
{
    wxFileName rel( aInfo.relativePath );

    if( rel.IsAbsolute() )
        return rel;

    // Container directory = directory of <container>.kicad_pro.
    wxFileName containerDir( GetFullFilename() );
    containerDir.SetFullName( wxEmptyString );

    // MakeAbsolute preserves subdirectories embedded in the relative
    // path. Constructing wxFileName(dir, name) with slashes in `name`
    // is not well-defined — this avoids that trap.
    rel.MakeAbsolute( containerDir.GetFullPath() );
    rel.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    return rel;
}


// =============================================================================
// M7.2: NET_SETTINGS replication
// =============================================================================
//
// Each project (container + sub-projects) carries its own net_settings, so
// every project remains fully usable when opened standalone. The container
// pushes its net classes to sub-projects on save; conflict resolution lives
// in `MultiBoardPropagateNetSettings()` (common/project/multi_board_propagate_settings.cpp).
//
// `NetSettings()` is now a pure inline accessor (declared in the header),
// no overlay logic at the runtime call site.


bool PROJECT_FILE::RemoveSubProject( const KIID& aUuid )
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
                            [&aUuid]( const SUB_PROJECT_INFO& sp )
                            { return sp.uuid == aUuid; } );

    if( it == m_subProjects.end() )
        return false;

    m_subProjects.erase( it );

    // Clean up cross-board nets that reference the removed sub-project.
    for( auto& net : m_crossBoardNets )
    {
        net.endpoints.erase(
                std::remove_if( net.endpoints.begin(), net.endpoints.end(),
                                [&aUuid]( const MB_CROSS_BOARD_NET_ENDPOINT& ep )
                                { return ep.subProjectUuid == aUuid; } ),
                net.endpoints.end() );
    }

    // Drop nets that now have fewer than 2 endpoints.
    m_crossBoardNets.erase(
            std::remove_if( m_crossBoardNets.begin(), m_crossBoardNets.end(),
                            []( const MB_CROSS_BOARD_NET& net )
                            { return net.endpoints.size() < 2; } ),
            m_crossBoardNets.end() );

    // We mutated two fields. Coalesce so observers see one event per
    // affected field, not the bookkeeping order.
    {
        PROJECT_FILE_SUSPEND_NOTIFY notifyGuard( *this );
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::SUB_PROJECTS );
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CROSS_BOARD_NETS );
    }

    return true;
}


// =============================================================================
// T3 — Observable accessor layer for multi_board.* fields
// =============================================================================

void PROJECT_FILE::RegisterObserver( PROJECT_FILE_OBSERVER* aObserver )
{
    if( !aObserver )
        return;

    // Idempotent: don't double-register. Observers are typically held
    // via SCOPED_PROJECT_FILE_OBSERVER which guarantees pairing, but a
    // belt-and-braces check here avoids subtle duplicate-event bugs.
    if( std::find( m_observers.begin(), m_observers.end(), aObserver ) != m_observers.end() )
        return;

    m_observers.push_back( aObserver );
}


void PROJECT_FILE::UnregisterObserver( PROJECT_FILE_OBSERVER* aObserver )
{
    m_observers.erase( std::remove( m_observers.begin(), m_observers.end(), aObserver ),
                       m_observers.end() );
}


void PROJECT_FILE::NotifyMultiBoardChanged( MULTI_BOARD_FIELD aField )
{
    // Mark the project dirty for any *real* mutation event. The two
    // synthetic file-watcher events don't represent in-memory edits,
    // so they don't flip the flag.
    if( aField != MULTI_BOARD_FIELD::EXTERNAL_RELOAD
        && aField != MULTI_BOARD_FIELD::EXTERNAL_RELOAD_DIRTY )
    {
        m_multiBoardDirty = true;
    }

    if( m_notifySuspendDepth > 0 )
    {
        m_pendingNotifications.insert( aField );
        return;
    }

    // Snapshot before dispatch — observers may unregister themselves
    // during the call (e.g. a dialog close handler), and we don't want
    // to skip subsequent observers or dereference an erased entry.
    std::vector<PROJECT_FILE_OBSERVER*> snapshot = m_observers;

    for( PROJECT_FILE_OBSERVER* obs : snapshot )
    {
        // Re-check membership so that an earlier observer's destructor
        // can't trigger a use-after-free on a later observer in this
        // same fan-out.
        if( std::find( m_observers.begin(), m_observers.end(), obs ) == m_observers.end() )
            continue;

        obs->OnMultiBoardFieldChanged( aField );
    }
}


// ----- Min power pins -----

void PROJECT_FILE::SetMinPowerPin( const wxString& aNet, int aMinPins )
{
    auto& slot = m_minPowerPins[aNet];

    if( slot == aMinPins )
        return;

    slot = aMinPins;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MIN_POWER_PINS );
}


void PROJECT_FILE::RemoveMinPowerPin( const wxString& aNet )
{
    if( m_minPowerPins.erase( aNet ) > 0 )
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MIN_POWER_PINS );
}


void PROJECT_FILE::ClearMinPowerPins()
{
    if( m_minPowerPins.empty() )
        return;

    m_minPowerPins.clear();
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MIN_POWER_PINS );
}


// ----- Max length per cross-board net -----

void PROJECT_FILE::SetMaxLengthNm( const wxString& aNet, int64_t aMaxNm )
{
    auto& slot = m_maxLengthNm[aNet];

    if( slot == aMaxNm )
        return;

    slot = aMaxNm;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MAX_LENGTH_NM );
}


void PROJECT_FILE::RemoveMaxLengthNm( const wxString& aNet )
{
    if( m_maxLengthNm.erase( aNet ) > 0 )
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MAX_LENGTH_NM );
}


void PROJECT_FILE::ClearMaxLengthNm()
{
    if( m_maxLengthNm.empty() )
        return;

    m_maxLengthNm.clear();
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MAX_LENGTH_NM );
}


// ----- Cross-board diff pairs -----

void PROJECT_FILE::AddCrossBoardDiffPair( const wxString& aNetA, const wxString& aNetB )
{
    // Skip if the (a,b) or (b,a) pair already exists. Order in storage
    // is preserved for round-trip cleanliness, but matching ignores it.
    auto matches = [&]( const std::pair<wxString, wxString>& p )
    {
        return ( p.first == aNetA && p.second == aNetB )
               || ( p.first == aNetB && p.second == aNetA );
    };

    if( std::any_of( m_crossBoardDiffPairs.begin(), m_crossBoardDiffPairs.end(), matches ) )
        return;

    m_crossBoardDiffPairs.emplace_back( aNetA, aNetB );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CROSS_BOARD_DIFF_PAIRS );
}


void PROJECT_FILE::RemoveCrossBoardDiffPair( const wxString& aNetA, const wxString& aNetB )
{
    auto matches = [&]( const std::pair<wxString, wxString>& p )
    {
        return ( p.first == aNetA && p.second == aNetB )
               || ( p.first == aNetB && p.second == aNetA );
    };

    auto it = std::remove_if( m_crossBoardDiffPairs.begin(), m_crossBoardDiffPairs.end(),
                              matches );

    if( it == m_crossBoardDiffPairs.end() )
        return;

    m_crossBoardDiffPairs.erase( it, m_crossBoardDiffPairs.end() );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CROSS_BOARD_DIFF_PAIRS );
}


void PROJECT_FILE::ClearCrossBoardDiffPairs()
{
    if( m_crossBoardDiffPairs.empty() )
        return;

    m_crossBoardDiffPairs.clear();
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CROSS_BOARD_DIFF_PAIRS );
}


// ----- Per-net current rule -----

void PROJECT_FILE::SetCrossBoardCurrentRule( const wxString& aNet, const MB_CURRENT_RULE& aRule )
{
    auto it = m_currentRules.find( aNet );

    if( it != m_currentRules.end() && it->second.expectedAmps == aRule.expectedAmps
        && it->second.pinRatingAmps == aRule.pinRatingAmps )
    {
        return;
    }

    m_currentRules[aNet] = aRule;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CURRENT_RULES );
}


void PROJECT_FILE::RemoveCrossBoardCurrentRule( const wxString& aNet )
{
    if( m_currentRules.erase( aNet ) > 0 )
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CURRENT_RULES );
}


void PROJECT_FILE::ClearCrossBoardCurrentRules()
{
    if( m_currentRules.empty() )
        return;

    m_currentRules.clear();
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CURRENT_RULES );
}


// ----- Per-net voltage-drop rule -----

void PROJECT_FILE::SetCrossBoardVoltageRule( const wxString& aNet, const MB_VOLTAGE_RULE& aRule )
{
    auto it = m_voltageRules.find( aNet );

    if( it != m_voltageRules.end() && it->second.expectedAmps == aRule.expectedAmps
        && it->second.maxDropMv == aRule.maxDropMv
        && it->second.traceWidthUm == aRule.traceWidthUm
        && it->second.traceSheetRMOhmsPerSq == aRule.traceSheetRMOhmsPerSq
        && it->second.contactRPerPinMOhms == aRule.contactRPerPinMOhms )
    {
        return;
    }

    m_voltageRules[aNet] = aRule;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::VOLTAGE_RULES );
}


void PROJECT_FILE::RemoveCrossBoardVoltageRule( const wxString& aNet )
{
    if( m_voltageRules.erase( aNet ) > 0 )
        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::VOLTAGE_RULES );
}


void PROJECT_FILE::ClearCrossBoardVoltageRules()
{
    if( m_voltageRules.empty() )
        return;

    m_voltageRules.clear();
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::VOLTAGE_RULES );
}


// ----- Existing setters, now wired to notify -----

void PROJECT_FILE::SetCrossBoardNets( std::vector<MB_CROSS_BOARD_NET> aNets )
{
    m_crossBoardNets = std::move( aNets );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CROSS_BOARD_NETS );
}


void PROJECT_FILE::SetCustomMates( std::vector<CUSTOM_MATE> aMates )
{
    m_customMates = std::move( aMates );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CUSTOM_MATES );
}


void PROJECT_FILE::SetAssemblyInstances( std::vector<ASSEMBLY_INSTANCE_STATE> aStates )
{
    m_assemblyInstances = std::move( aStates );
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::ASSEMBLY_INSTANCES );
}


void PROJECT_FILE::SetCollisionThresholdMm( double aMm )
{
    if( m_collisionThresholdMm == aMm )
        return;

    m_collisionThresholdMm = aMm;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::COLLISION_THRESHOLD );
}


void PROJECT_FILE::Set3DViewerColorOverrides( std::map<int, KIGFX::COLOR4D> aOverrides )
{
    // PARAM_LAMBDA getters return m_3DViewer* verbatim, so the next
    // SaveToFile sees the values diverging from disk and persists. No
    // observer notification needed — nothing else listens.
    m_3DViewerColorOverrides = std::move( aOverrides );
}


void PROJECT_FILE::Set3DViewerVisibilityOverrides( std::map<int, bool> aOverrides )
{
    m_3DViewerVisibilityOverrides = std::move( aOverrides );
}


void PROJECT_FILE::Set3DViewerUseStackupColors( std::optional<bool> aValue )
{
    m_3DViewerUseStackupColors = aValue;
}


void PROJECT_FILE::SetMbsFileName( const wxString& aName )
{
    if( m_mbsFileName == aName )
        return;

    m_mbsFileName = aName;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::MBS_FILE_NAME );
}


void PROJECT_FILE::SetContainerProjectRelativePath( const wxString& aPath )
{
    if( m_containerProjectRelativePath == aPath )
        return;

    m_containerProjectRelativePath = aPath;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::CONTAINER_PROJECT_PATH );
}


// =============================================================================
// T3 — Subscription RAII helpers (declared in project_file_observer.h)
// =============================================================================

SCOPED_PROJECT_FILE_OBSERVER::SCOPED_PROJECT_FILE_OBSERVER( PROJECT_FILE&          aProjectFile,
                                                            PROJECT_FILE_OBSERVER* aObserver ) :
        m_projectFile( &aProjectFile ),
        m_observer( aObserver )
{
    // Register in the back-pointer table BEFORE the observer registration —
    // PROJECT_FILE's dtor walks that table to null us out, so we need to
    // be in it for the entire window we hold a raw m_projectFile pointer.
    m_projectFile->m_scopedObservers.push_back( this );

    if( m_observer )
        m_projectFile->RegisterObserver( m_observer );
}


SCOPED_PROJECT_FILE_OBSERVER::~SCOPED_PROJECT_FILE_OBSERVER()
{
    // m_projectFile is nulled by PROJECT_FILE's dtor when the file dies
    // first — in that case there's nothing to unregister and no back-pointer
    // entry to clean up. This is the ordering-safety branch the whole
    // back-pointer table exists for.
    if( !m_projectFile )
        return;

    if( m_observer )
        m_projectFile->UnregisterObserver( m_observer );

    auto& list = m_projectFile->m_scopedObservers;
    list.erase( std::remove( list.begin(), list.end(), this ), list.end() );
}


SCOPED_PROJECT_FILE_OBSERVER::SCOPED_PROJECT_FILE_OBSERVER(
        SCOPED_PROJECT_FILE_OBSERVER&& aOther ) noexcept :
        m_projectFile( aOther.m_projectFile ),
        m_observer( aOther.m_observer )
{
    if( m_projectFile )
    {
        // Replace aOther's slot in the back-pointer table with us. In-place
        // edit (no erase + push_back) preserves order and avoids
        // reallocation hazards if PROJECT_FILE dtor is iterating in parallel
        // (it isn't on the same thread, but cheaper to just swap anyway).
        for( SCOPED_PROJECT_FILE_OBSERVER*& slot : m_projectFile->m_scopedObservers )
        {
            if( slot == &aOther )
            {
                slot = this;
                break;
            }
        }
    }

    aOther.m_projectFile = nullptr;
    aOther.m_observer    = nullptr;
}


SCOPED_PROJECT_FILE_OBSERVER& SCOPED_PROJECT_FILE_OBSERVER::operator=(
        SCOPED_PROJECT_FILE_OBSERVER&& aOther ) noexcept
{
    if( this == &aOther )
        return *this;

    // Tear down our existing subscription (if any), mirroring dtor.
    if( m_projectFile )
    {
        if( m_observer )
            m_projectFile->UnregisterObserver( m_observer );

        auto& list = m_projectFile->m_scopedObservers;
        list.erase( std::remove( list.begin(), list.end(), this ), list.end() );
    }

    m_projectFile = aOther.m_projectFile;
    m_observer    = aOther.m_observer;

    if( m_projectFile )
    {
        for( SCOPED_PROJECT_FILE_OBSERVER*& slot : m_projectFile->m_scopedObservers )
        {
            if( slot == &aOther )
            {
                slot = this;
                break;
            }
        }
    }

    aOther.m_projectFile = nullptr;
    aOther.m_observer    = nullptr;

    return *this;
}


PROJECT_FILE_SUSPEND_NOTIFY::PROJECT_FILE_SUSPEND_NOTIFY( PROJECT_FILE& aProjectFile ) :
        m_projectFile( aProjectFile )
{
    ++m_projectFile.m_notifySuspendDepth;
}


PROJECT_FILE_SUSPEND_NOTIFY::~PROJECT_FILE_SUSPEND_NOTIFY()
{
    if( --m_projectFile.m_notifySuspendDepth > 0 )
        return;

    // Outermost guard exits — drain pending. Move into a local so any
    // re-entrant edits during dispatch (which now go straight through
    // because depth is 0) don't get tangled with the drain.
    std::set<MULTI_BOARD_FIELD> pending;
    pending.swap( m_projectFile.m_pendingNotifications );

    for( MULTI_BOARD_FIELD field : pending )
        m_projectFile.NotifyMultiBoardChanged( field );
}


// =============================================================================
// T3 Phase 4 — file watcher API + external-change reload
// =============================================================================

bool PROJECT_FILE::EnableFileWatcher( const wxString& aAbsPath )
{
    if( aAbsPath.IsEmpty() )
    {
        DisableFileWatcher();
        return false;
    }

    if( !m_watcher )
        m_watcher = std::make_unique<PROJECT_FILE_WATCHER>( *this );

    return m_watcher->Start( aAbsPath );
}


void PROJECT_FILE::DisableFileWatcher()
{
    if( !m_watcher )
        return;

    m_watcher->Stop();
    // Keep the instance alive so re-enable doesn't allocate again.
    // The watcher itself is cheap; the OS resources (kqueue / inotify
    // FD) are released by Stop.
}


void PROJECT_FILE::OnExternalFileChange()
{
    // Dirty in-memory state means the user is mid-edit. We don't
    // overwrite their work — we surface the conflict to subscribers
    // (typically dialogs) and let them decide. Phase 6 will add the
    // banner UX; until then, observers can listen for this code and
    // log / prompt as desired.
    if( m_multiBoardDirty )
    {
        wxLogTrace( wxT( "PROJECT_FILE" ),
                    wxT( "OnExternalFileChange: dirty in-memory state, surfacing "
                         "EXTERNAL_RELOAD_DIRTY for '%s'" ),
                    m_watcher ? m_watcher->WatchedPath() : wxString( wxEmptyString ) );

        NotifyMultiBoardChanged( MULTI_BOARD_FIELD::EXTERNAL_RELOAD_DIRTY );
        return;
    }

    // Clean state — safe to blanket-reload from disk. Re-running
    // LoadFromFile through JSON_SETTINGS overwrites every PARAM-bound
    // member from the on-disk JSON. We hold the watcher steady through
    // the reload — wxFileSystemWatcher events post to the main loop
    // and are processed serially, so a second event mid-reload would
    // queue and arrive after we return.
    //
    // Suppress per-field notifications during the reload — JSON_SETTINGS
    // doesn't go through our setters anyway, but in case any future
    // override does, we collapse the noise into a single synthetic
    // EXTERNAL_RELOAD event below.
    {
        PROJECT_FILE_SUSPEND_NOTIFY guard( *this );

        // LoadFromFile returns false if the file doesn't exist or
        // can't be parsed. Either way, we don't blanket-reload — log
        // and bail. The watcher will re-fire if/when the file
        // reappears in a parseable state.
        if( !LoadFromFile() )
        {
            wxLogTrace( wxT( "PROJECT_FILE" ),
                        wxT( "OnExternalFileChange: reload failed for '%s'" ),
                        m_watcher ? m_watcher->WatchedPath() : wxString( wxEmptyString ) );

            // Drop any per-field events the failed load might have
            // queued so we don't fire bogus notifications on the
            // way out.
            m_pendingNotifications.clear();
            return;
        }

        // Squash any per-field events the reload may have queued.
        m_pendingNotifications.clear();
    }

    // Reload populated fields from disk; in-memory now matches disk.
    m_multiBoardDirty = false;

    wxLogTrace( wxT( "PROJECT_FILE" ),
                wxT( "OnExternalFileChange: clean reload from '%s'" ),
                m_watcher ? m_watcher->WatchedPath() : wxString( wxEmptyString ) );

    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::EXTERNAL_RELOAD );
}


bool PROJECT_FILE::ReloadFromDiskDiscardingChanges()
{
    // Same shape as the clean-state branch of OnExternalFileChange,
    // but unconditional — the caller has explicitly opted to discard
    // any in-memory edits.
    {
        PROJECT_FILE_SUSPEND_NOTIFY guard( *this );

        if( !LoadFromFile() )
        {
            wxLogTrace( wxT( "PROJECT_FILE" ),
                        wxT( "ReloadFromDiskDiscardingChanges: reload failed for '%s'" ),
                        m_watcher ? m_watcher->WatchedPath() : wxString( wxEmptyString ) );

            m_pendingNotifications.clear();
            return false;
        }

        m_pendingNotifications.clear();
    }

    m_multiBoardDirty = false;
    NotifyMultiBoardChanged( MULTI_BOARD_FIELD::EXTERNAL_RELOAD );
    return true;
}


// =============================================================================
// Multi-board design settings management
// =============================================================================

void PROJECT_FILE::RegisterBoardSettings( const KIID& aBoardUuid, BOARD_DESIGN_SETTINGS* aSettings )
{
    if( aBoardUuid == niluuid || !aSettings )
        return;

    m_MultiBoardSettings[aBoardUuid] = aSettings;
}


void PROJECT_FILE::UnregisterBoardSettings( const KIID& aBoardUuid )
{
    m_MultiBoardSettings.erase( aBoardUuid );
}


BOARD_DESIGN_SETTINGS* PROJECT_FILE::GetBoardSettings( const KIID& aBoardUuid ) const
{
    auto it = m_MultiBoardSettings.find( aBoardUuid );

    if( it != m_MultiBoardSettings.end() )
        return it->second;

    return nullptr;
}


// =============================================================================
// JSON serialization for multi-board types
// =============================================================================

void to_json( nlohmann::json& aJson, const BOARD_INFO& aInfo )
{
    aJson = nlohmann::json::object();
    aJson["uuid"] = aInfo.uuid.AsString().ToUTF8();
    aJson["filename"] = aInfo.filename.ToUTF8();
    aJson["display_name"] = aInfo.displayName.ToUTF8();
    aJson["is_active"] = aInfo.isActive;
}


void from_json( const nlohmann::json& aJson, BOARD_INFO& aInfo )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "uuid" ) )
        aInfo.uuid = KIID( wxString( aJson["uuid"].get<std::string>().c_str(), wxConvUTF8 ) );

    if( aJson.contains( "filename" ) )
        aInfo.filename = wxString( aJson["filename"].get<std::string>().c_str(), wxConvUTF8 );

    if( aJson.contains( "display_name" ) )
        aInfo.displayName = wxString( aJson["display_name"].get<std::string>().c_str(), wxConvUTF8 );

    if( aJson.contains( "is_active" ) )
        aInfo.isActive = aJson["is_active"].get<bool>();
}


void to_json( nlohmann::json& aJson, const CROSS_BOARD_CONNECTION& aConnection )
{
    aJson = nlohmann::json::object();
    aJson["board1_uuid"] = aConnection.board1Uuid.AsString().ToUTF8();
    aJson["pad1_uuid"] = aConnection.pad1Uuid.AsString().ToUTF8();
    aJson["board2_uuid"] = aConnection.board2Uuid.AsString().ToUTF8();
    aJson["pad2_uuid"] = aConnection.pad2Uuid.AsString().ToUTF8();
}


void from_json( const nlohmann::json& aJson, CROSS_BOARD_CONNECTION& aConnection )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "board1_uuid" ) )
        aConnection.board1Uuid = KIID( wxString( aJson["board1_uuid"].get<std::string>().c_str(), wxConvUTF8 ) );

    if( aJson.contains( "pad1_uuid" ) )
        aConnection.pad1Uuid = KIID( wxString( aJson["pad1_uuid"].get<std::string>().c_str(), wxConvUTF8 ) );

    if( aJson.contains( "board2_uuid" ) )
        aConnection.board2Uuid = KIID( wxString( aJson["board2_uuid"].get<std::string>().c_str(), wxConvUTF8 ) );

    if( aJson.contains( "pad2_uuid" ) )
        aConnection.pad2Uuid = KIID( wxString( aJson["pad2_uuid"].get<std::string>().c_str(), wxConvUTF8 ) );
}


void to_json( nlohmann::json& aJson, const COMPONENT_BOARD_ASSIGNMENT& aAssignment )
{
    aJson = nlohmann::json::object();
    aJson["reference"] = aAssignment.reference.ToUTF8();

    nlohmann::json boards = nlohmann::json::array();
    for( const KIID& uuid : aAssignment.boardUuids )
        boards.push_back( uuid.AsString().ToUTF8() );

    aJson["board_uuids"] = boards;
}


void from_json( const nlohmann::json& aJson, COMPONENT_BOARD_ASSIGNMENT& aAssignment )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "reference" ) )
        aAssignment.reference = wxString( aJson["reference"].get<std::string>().c_str(), wxConvUTF8 );

    if( aJson.contains( "board_uuids" ) && aJson["board_uuids"].is_array() )
    {
        aAssignment.boardUuids.clear();
        for( const auto& uuid : aJson["board_uuids"] )
        {
            aAssignment.boardUuids.emplace_back(
                    wxString( uuid.get<std::string>().c_str(), wxConvUTF8 ) );
        }
    }
}


void to_json( nlohmann::json& aJson, const SUB_PROJECT_INFO& aInfo )
{
    aJson = nlohmann::json{
        { "uuid",         aInfo.uuid.AsString().ToUTF8() },
        { "name",         aInfo.name.ToUTF8() },
        { "path",         aInfo.relativePath.ToUTF8() },
        { "display_name", aInfo.displayName.ToUTF8() },
        { "role",         aInfo.role.ToUTF8() }
    };
}


void from_json( const nlohmann::json& aJson, SUB_PROJECT_INFO& aInfo )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "uuid" ) )
        aInfo.uuid = KIID( wxString::FromUTF8( aJson["uuid"].get<std::string>().c_str() ) );

    if( aJson.contains( "name" ) )
        aInfo.name = wxString::FromUTF8( aJson["name"].get<std::string>().c_str() );

    if( aJson.contains( "path" ) )
        aInfo.relativePath = wxString::FromUTF8( aJson["path"].get<std::string>().c_str() );

    if( aJson.contains( "display_name" ) )
        aInfo.displayName = wxString::FromUTF8( aJson["display_name"].get<std::string>().c_str() );

    if( aJson.contains( "role" ) )
        aInfo.role = wxString::FromUTF8( aJson["role"].get<std::string>().c_str() );
}


void to_json( nlohmann::json& aJson, const MB_CROSS_BOARD_NET_ENDPOINT& aEp )
{
    aJson = nlohmann::json{
        { "sub_project_uuid", aEp.subProjectUuid.AsString().ToUTF8() },
        { "component",        aEp.componentRef.ToUTF8() },
        { "pin",              aEp.pinNumber.ToUTF8() },
        { "pin_name",         aEp.pinName.ToUTF8() }
    };
}


void from_json( const nlohmann::json& aJson, MB_CROSS_BOARD_NET_ENDPOINT& aEp )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "sub_project_uuid" ) )
    {
        aEp.subProjectUuid =
                KIID( wxString::FromUTF8( aJson["sub_project_uuid"].get<std::string>().c_str() ) );
    }

    if( aJson.contains( "component" ) )
        aEp.componentRef = wxString::FromUTF8( aJson["component"].get<std::string>().c_str() );

    if( aJson.contains( "pin" ) )
        aEp.pinNumber = wxString::FromUTF8( aJson["pin"].get<std::string>().c_str() );

    if( aJson.contains( "pin_name" ) )
        aEp.pinName = wxString::FromUTF8( aJson["pin_name"].get<std::string>().c_str() );
}


void to_json( nlohmann::json& aJson, const MB_CROSS_BOARD_NET& aNet )
{
    nlohmann::json endpoints = nlohmann::json::array();

    for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : aNet.endpoints )
    {
        nlohmann::json e;
        to_json( e, ep );
        endpoints.push_back( e );
    }

    aJson = nlohmann::json{
        { "uuid",      aNet.uuid.AsString().ToUTF8() },
        { "name",      aNet.name.ToUTF8() },
        { "endpoints", endpoints }
    };
}


void from_json( const nlohmann::json& aJson, MB_CROSS_BOARD_NET& aNet )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "uuid" ) )
        aNet.uuid = KIID( wxString::FromUTF8( aJson["uuid"].get<std::string>().c_str() ) );

    if( aJson.contains( "name" ) )
        aNet.name = wxString::FromUTF8( aJson["name"].get<std::string>().c_str() );

    if( aJson.contains( "endpoints" ) && aJson["endpoints"].is_array() )
    {
        aNet.endpoints.clear();
        for( const auto& ep : aJson["endpoints"] )
        {
            MB_CROSS_BOARD_NET_ENDPOINT endpoint;
            from_json( ep, endpoint );
            aNet.endpoints.push_back( endpoint );
        }
    }
}


namespace
{

const char* customMateRoleToStr( CUSTOM_MATE_ROLE aRole )
{
    switch( aRole )
    {
    case CUSTOM_MATE_ROLE::PRIMARY:   return "primary";
    case CUSTOM_MATE_ROLE::SECONDARY: return "secondary";
    case CUSTOM_MATE_ROLE::DISABLED:  return "disabled";
    }
    return "primary";
}


CUSTOM_MATE_ROLE customMateRoleFromStr( const std::string& aStr )
{
    if( aStr == "secondary" ) return CUSTOM_MATE_ROLE::SECONDARY;
    if( aStr == "disabled" )  return CUSTOM_MATE_ROLE::DISABLED;
    return CUSTOM_MATE_ROLE::PRIMARY;
}


const char* customMateTypeToStr( CUSTOM_MATE_TYPE aType )
{
    switch( aType )
    {
    case CUSTOM_MATE_TYPE::CONNECTOR:     return "connector";
    case CUSTOM_MATE_TYPE::MOUNTING_HOLE: return "mounting_hole";
    case CUSTOM_MATE_TYPE::ALIGNMENT:     return "alignment";
    }
    return "connector";
}


CUSTOM_MATE_TYPE customMateTypeFromStr( const std::string& aStr )
{
    if( aStr == "mounting_hole" ) return CUSTOM_MATE_TYPE::MOUNTING_HOLE;
    if( aStr == "alignment" )     return CUSTOM_MATE_TYPE::ALIGNMENT;
    return CUSTOM_MATE_TYPE::CONNECTOR;
}


nlohmann::json customMateEndToJson( const CUSTOM_MATE_END& aEnd )
{
    return nlohmann::json{
        { "sub_project_uuid", aEnd.subProjectUuid.AsString().ToUTF8() },
        { "footprint_ref",    aEnd.footprintRef.ToUTF8() }
    };
}


void customMateEndFromJson( const nlohmann::json& aJson, CUSTOM_MATE_END& aEnd )
{
    if( aJson.contains( "sub_project_uuid" ) )
    {
        aEnd.subProjectUuid =
                KIID( wxString::FromUTF8( aJson["sub_project_uuid"].get<std::string>().c_str() ) );
    }

    if( aJson.contains( "footprint_ref" ) )
        aEnd.footprintRef = wxString::FromUTF8( aJson["footprint_ref"].get<std::string>().c_str() );
}

} // namespace


void to_json( nlohmann::json& aJson, const CUSTOM_MATE& aMate )
{
    aJson = nlohmann::json{
        { "uuid",  aMate.uuid.AsString().ToUTF8() },
        { "role",  customMateRoleToStr( aMate.role ) },
        { "type",  customMateTypeToStr( aMate.type ) },
        { "end_a", customMateEndToJson( aMate.endA ) },
        { "end_b", customMateEndToJson( aMate.endB ) }
    };

    // Offset is omitted entirely when not set so the JSON stays small
    // and round-trips cleanly for the common no-offset case.
    if( aMate.hasOffset )
    {
        aJson["offset"] = nlohmann::json{
            { "translation",
              { aMate.offsetTranslation.x, aMate.offsetTranslation.y, aMate.offsetTranslation.z } },
            { "rotation",
              { aMate.offsetRotation.x, aMate.offsetRotation.y, aMate.offsetRotation.z } }
        };
    }
}


void from_json( const nlohmann::json& aJson, CUSTOM_MATE& aMate )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "uuid" ) )
        aMate.uuid = KIID( wxString::FromUTF8( aJson["uuid"].get<std::string>().c_str() ) );

    if( aJson.contains( "role" ) )
        aMate.role = customMateRoleFromStr( aJson["role"].get<std::string>() );

    if( aJson.contains( "type" ) )
        aMate.type = customMateTypeFromStr( aJson["type"].get<std::string>() );

    if( aJson.contains( "end_a" ) )
        customMateEndFromJson( aJson["end_a"], aMate.endA );

    if( aJson.contains( "end_b" ) )
        customMateEndFromJson( aJson["end_b"], aMate.endB );

    aMate.hasOffset = false;
    aMate.offsetTranslation = VECTOR3D( 0.0, 0.0, 0.0 );
    aMate.offsetRotation    = VECTOR3D( 0.0, 0.0, 0.0 );

    if( aJson.contains( "offset" ) && aJson["offset"].is_object() )
    {
        const auto& offset = aJson["offset"];

        if( offset.contains( "translation" ) && offset["translation"].is_array()
            && offset["translation"].size() == 3 )
        {
            aMate.offsetTranslation = VECTOR3D( offset["translation"][0].get<double>(),
                                                offset["translation"][1].get<double>(),
                                                offset["translation"][2].get<double>() );
            aMate.hasOffset = true;
        }

        if( offset.contains( "rotation" ) && offset["rotation"].is_array()
            && offset["rotation"].size() == 3 )
        {
            aMate.offsetRotation = VECTOR3D( offset["rotation"][0].get<double>(),
                                             offset["rotation"][1].get<double>(),
                                             offset["rotation"][2].get<double>() );
            aMate.hasOffset = true;
        }
    }
}


void to_json( nlohmann::json& aJson, const ASSEMBLY_INSTANCE_STATE& aState )
{
    aJson = nlohmann::json{
        { "sub_project", aState.subProjectUuid.AsString().ToUTF8() },
        { "position",    { aState.position.x, aState.position.y, aState.position.z } },
        { "rotation",    { aState.rotation.x, aState.rotation.y, aState.rotation.z } },
        { "visible",     aState.visible },
        { "transparent", aState.transparent },
        { "opacity",     aState.opacity }
    };
}


void from_json( const nlohmann::json& aJson, ASSEMBLY_INSTANCE_STATE& aState )
{
    wxCHECK( aJson.is_object(), /* void */ );

    if( aJson.contains( "sub_project" ) )
    {
        aState.subProjectUuid =
                KIID( wxString::FromUTF8( aJson["sub_project"].get<std::string>().c_str() ) );
    }

    if( aJson.contains( "position" ) && aJson["position"].is_array()
        && aJson["position"].size() == 3 )
    {
        aState.position = VECTOR3D( aJson["position"][0].get<double>(),
                                    aJson["position"][1].get<double>(),
                                    aJson["position"][2].get<double>() );
    }

    if( aJson.contains( "rotation" ) && aJson["rotation"].is_array()
        && aJson["rotation"].size() == 3 )
    {
        aState.rotation = VECTOR3D( aJson["rotation"][0].get<double>(),
                                    aJson["rotation"][1].get<double>(),
                                    aJson["rotation"][2].get<double>() );
    }

    if( aJson.contains( "visible" ) )
        aState.visible = aJson["visible"].get<bool>();

    if( aJson.contains( "transparent" ) )
        aState.transparent = aJson["transparent"].get<bool>();

    if( aJson.contains( "opacity" ) )
        aState.opacity = aJson["opacity"].get<double>();
}
