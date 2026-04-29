/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "drc_engine_cross_board.h"

#include <board.h>
#include <footprint.h>
#include <multi_board/sub_project_board_loader.h>
#include <pad.h>
#include <project.h>
#include <project/project_file.h>
#include <reporter.h>


wxString CROSS_BOARD_DRC_ITEM::GetTypeString() const
{
    switch( type )
    {
    case CROSS_BOARD_DRC_TYPE::CONNECTOR_PIN_COUNT_MISMATCH:
        return _( "Connector pin count mismatch" );
    case CROSS_BOARD_DRC_TYPE::CONNECTOR_NET_NAME_MISMATCH:
        return _( "Connector net name mismatch" );
    case CROSS_BOARD_DRC_TYPE::CONNECTOR_ELECTRICAL_TYPE_MISMATCH:
        return _( "Connector electrical type mismatch" );
    case CROSS_BOARD_DRC_TYPE::SIGNAL_LENGTH_EXCEEDED:
        return _( "Signal length exceeded" );
    case CROSS_BOARD_DRC_TYPE::SIGNAL_LENGTH_MISMATCH:
        return _( "Signal length mismatch" );
    case CROSS_BOARD_DRC_TYPE::POWER_INSUFFICIENT_PINS:
        return _( "Insufficient power pins" );
    case CROSS_BOARD_DRC_TYPE::POWER_VOLTAGE_DROP:
        return _( "Voltage drop exceeded" );
    case CROSS_BOARD_DRC_TYPE::NET_INCOMPLETE:
        return _( "Incomplete cross-board net" );
    case CROSS_BOARD_DRC_TYPE::NET_ORPHAN_CONNECTOR:
        return _( "Orphan connector" );
    case CROSS_BOARD_DRC_TYPE::UNASSIGNED_CONNECTOR_PIN:
        return _( "Unassigned connector pin" );
    default:
        return _( "Unknown" );
    }
}


wxString CROSS_BOARD_DRC_ITEM::GetSeverityString() const
{
    switch( severity )
    {
    case CROSS_BOARD_DRC_SEVERITY::INFO:
        return _( "Info" );
    case CROSS_BOARD_DRC_SEVERITY::WARNING:
        return _( "Warning" );
    case CROSS_BOARD_DRC_SEVERITY::ERROR:
        return _( "Error" );
    default:
        return _( "Unknown" );
    }
}


DRC_ENGINE_CROSS_BOARD::DRC_ENGINE_CROSS_BOARD( PROJECT* aProject ) :
        m_project( aProject ),
        m_reporter( nullptr )
{
}


void DRC_ENGINE_CROSS_BOARD::RunAllChecks()
{
    ClearViolations();

    if( m_reporter )
        m_reporter->Report( _( "Running cross-board DRC checks..." ), RPT_SEVERITY_INFO );

    // Connector matching checks
    CheckConnectorMatching();

    // Signal integrity checks (if enabled)
    if( m_signalRules.enabled )
        CheckSignalIntegrity();

    // Power distribution checks (if enabled)
    if( m_powerRules.enabled )
        CheckPowerDistribution();

    // Net completeness checks
    CheckNetCompleteness();

    if( m_reporter )
    {
        m_reporter->Report( wxString::Format( _( "Cross-board DRC complete: %d errors, %d warnings" ),
                                               GetErrorCount(), GetWarningCount() ),
                            RPT_SEVERITY_INFO );
    }
}


namespace
{
// Locate a pad on a board by (componentRef, pinNumber). Returns the pad and,
// via the optional out-param, the parent footprint for context-message
// purposes. Returns nullptr if the connector or pin doesn't exist on the board
// — caller handles that as an orphan endpoint.
PAD* findPadByRefAndNumber( BOARD* aBoard, const wxString& aRef, const wxString& aPinNumber,
                            FOOTPRINT** aFootprintOut = nullptr )
{
    if( !aBoard )
        return nullptr;

    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        if( fp->GetReference() != aRef )
            continue;

        for( PAD* pad : fp->Pads() )
        {
            if( pad->GetNumber() == aPinNumber )
            {
                if( aFootprintOut )
                    *aFootprintOut = fp;

                return pad;
            }
        }
    }

    return nullptr;
}
}  // namespace


void DRC_ENGINE_CROSS_BOARD::CheckConnectorMatching()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& crossBoardNets = projectFile.GetCrossBoardNets();
    const auto& subProjects = projectFile.GetSubProjects();

    // Build sub-project name map for violation messages.
    std::map<KIID, wxString> subProjectNames;

    for( const SUB_PROJECT_INFO& sp : subProjects )
    {
        subProjectNames[sp.uuid] = sp.displayName.IsEmpty() ? sp.name : sp.displayName;
    }

    // Each MB_CROSS_BOARD_NET has N endpoints (one per sub-project pad on this
    // net). Resolve every endpoint to a pad on its sub-project board, then
    // compare endpoints pairwise within the net for net-name / unassigned-pin
    // rule violations.
    for( const MB_CROSS_BOARD_NET& net : crossBoardNets )
    {
        struct ResolvedEndpoint
        {
            const MB_CROSS_BOARD_NET_ENDPOINT* endpoint;
            BOARD*                             board;
            PAD*                               pad;
            wxString                           subProjectName;
        };

        std::vector<ResolvedEndpoint> resolved;
        resolved.reserve( net.endpoints.size() );

        for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
        {
            BOARD* board = GetBoardByUuid( ep.subProjectUuid );

            if( !board )
            {
                m_currentBoard1Uuid = ep.subProjectUuid;
                m_currentBoard1Name = subProjectNames.count( ep.subProjectUuid )
                                              ? subProjectNames[ep.subProjectUuid]
                                              : _( "Unknown" );
                m_currentBoard2Uuid = KIID( 0 );
                m_currentBoard2Name = wxEmptyString;
                m_currentConnectorRef = ep.componentRef;

                AddViolation( CROSS_BOARD_DRC_TYPE::NET_INCOMPLETE,
                              CROSS_BOARD_DRC_SEVERITY::ERROR,
                              wxString::Format( _( "Cross-board net '%s' references "
                                                   "unavailable sub-project board" ),
                                                net.name ) );
                continue;
            }

            FOOTPRINT* fp = nullptr;
            PAD* pad = findPadByRefAndNumber( board, ep.componentRef, ep.pinNumber, &fp );

            if( !pad )
            {
                m_currentBoard1Uuid = ep.subProjectUuid;
                m_currentBoard1Name = subProjectNames.count( ep.subProjectUuid )
                                              ? subProjectNames[ep.subProjectUuid]
                                              : _( "Unknown" );
                m_currentBoard2Uuid = KIID( 0 );
                m_currentBoard2Name = wxEmptyString;
                m_currentConnectorRef = ep.componentRef;

                AddViolation( CROSS_BOARD_DRC_TYPE::NET_ORPHAN_CONNECTOR,
                              CROSS_BOARD_DRC_SEVERITY::WARNING,
                              wxString::Format( _( "Cross-board net '%s' endpoint %s:%s "
                                                   "missing on sub-project '%s'" ),
                                                net.name, ep.componentRef, ep.pinNumber,
                                                m_currentBoard1Name ) );
                continue;
            }

            resolved.push_back( { &ep, board, pad,
                                  subProjectNames.count( ep.subProjectUuid )
                                          ? subProjectNames[ep.subProjectUuid]
                                          : ep.componentRef } );
        }

        // Pairwise checks within the net: net-name match and unassigned-pin.
        for( size_t i = 0; i < resolved.size(); ++i )
        {
            for( size_t j = i + 1; j < resolved.size(); ++j )
            {
                const ResolvedEndpoint& a = resolved[i];
                const ResolvedEndpoint& b = resolved[j];

                m_currentBoard1Uuid = a.endpoint->subProjectUuid;
                m_currentBoard1Name = a.subProjectName;
                m_currentBoard2Uuid = b.endpoint->subProjectUuid;
                m_currentBoard2Name = b.subProjectName;
                m_currentConnectorRef = a.endpoint->componentRef;

                if( m_connectorRules.checkNetNameMatch )
                {
                    const wxString& net1 = a.pad->GetNetname();
                    const wxString& net2 = b.pad->GetNetname();

                    if( !NetNamesMatch( net1, net2 ) )
                    {
                        wxString details = wxString::Format(
                                _( "'%s' (%s) vs '%s' (%s) on cross-board net '%s'" ),
                                net1, a.subProjectName, net2, b.subProjectName, net.name );

                        AddViolation( CROSS_BOARD_DRC_TYPE::CONNECTOR_NET_NAME_MISMATCH,
                                      CROSS_BOARD_DRC_SEVERITY::WARNING,
                                      _( "Net name mismatch on connector pin" ), details );
                    }
                }

                if( a.pad->GetNetCode() <= 0 || b.pad->GetNetCode() <= 0 )
                {
                    AddViolation( CROSS_BOARD_DRC_TYPE::UNASSIGNED_CONNECTOR_PIN,
                                  CROSS_BOARD_DRC_SEVERITY::WARNING,
                                  _( "Connector pin has no net assignment" ) );
                }
            }
        }
    }
}


void DRC_ENGINE_CROSS_BOARD::CheckSignalIntegrity()
{
    // TODO: Implement signal integrity checks
    // This would require trace length calculations across boards

    if( m_reporter )
        m_reporter->Report( _( "Signal integrity checks not yet implemented" ),
                            RPT_SEVERITY_INFO );
}


void DRC_ENGINE_CROSS_BOARD::CheckPowerDistribution()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& crossBoardNets = projectFile.GetCrossBoardNets();
    const auto& subProjects = projectFile.GetSubProjects();

    std::map<KIID, wxString> subProjectNames;

    for( const SUB_PROJECT_INFO& sp : subProjects )
        subProjectNames[sp.uuid] = sp.displayName.IsEmpty() ? sp.name : sp.displayName;

    // Per-sub-project pin count per declared power net. A "power net" here is
    // any cross-board net whose canonical name matches a key in the rules'
    // minPowerPins map. Each endpoint counts as one pin on that sub-project.
    std::map<std::pair<KIID, wxString>, int> pinCounts;

    for( const MB_CROSS_BOARD_NET& net : crossBoardNets )
    {
        auto it = m_powerRules.minPowerPins.find( net.name );

        if( it == m_powerRules.minPowerPins.end() )
            continue;

        for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
            pinCounts[{ ep.subProjectUuid, net.name }]++;
    }

    // Flag any sub-project whose pin count for a declared power net is below
    // the minimum. A net never reached on this sub-project (count = 0) is
    // skipped — that's an "unrelated board" case, not a violation.
    for( const auto& [netName, minPins] : m_powerRules.minPowerPins )
    {
        for( const SUB_PROJECT_INFO& sp : subProjects )
        {
            auto countIt = pinCounts.find( { sp.uuid, netName } );

            if( countIt == pinCounts.end() )
                continue;

            int count = countIt->second;

            if( count < minPins )
            {
                m_currentBoard1Uuid = sp.uuid;
                m_currentBoard1Name = subProjectNames[sp.uuid];
                m_currentBoard2Uuid = KIID( 0 );
                m_currentBoard2Name = wxEmptyString;
                m_currentConnectorRef = wxEmptyString;

                AddViolation( CROSS_BOARD_DRC_TYPE::POWER_INSUFFICIENT_PINS,
                              CROSS_BOARD_DRC_SEVERITY::WARNING,
                              wxString::Format( _( "Power net '%s' has %d pin(s) on sub-project "
                                                   "'%s' (rule requires %d)" ),
                                                netName, count, m_currentBoard1Name, minPins ) );
            }
        }
    }

    // Current capacity and voltage drop checks need analog modeling
    // (per-net trace resistance, source impedance) and are not implemented.
}


void DRC_ENGINE_CROSS_BOARD::CheckNetCompleteness()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& subProjects = projectFile.GetSubProjects();
    const auto& crossBoardNets = projectFile.GetCrossBoardNets();

    // For each sub-project, check that every connector pad on the loaded
    // board is referenced by at least one cross-board net endpoint.
    for( const SUB_PROJECT_INFO& sp : subProjects )
    {
        BOARD* board = GetBoardByUuid( sp.uuid );

        if( !board )
            continue;

        wxString subProjectName = sp.displayName.IsEmpty() ? sp.name : sp.displayName;

        for( const KIID& padUuid : board->GetConnectorPads() )
        {
            PAD* pad = nullptr;
            FOOTPRINT* parentFp = nullptr;

            for( FOOTPRINT* fp : board->Footprints() )
            {
                for( PAD* p : fp->Pads() )
                {
                    if( p->m_Uuid == padUuid )
                    {
                        pad = p;
                        parentFp = fp;
                        break;
                    }
                }

                if( pad )
                    break;
            }

            if( !pad )
                continue;

            const wxString ref = parentFp ? parentFp->GetReference() : wxT( "?" );
            const wxString pinNumber = pad->GetNumber();

            bool hasEndpoint = false;

            for( const MB_CROSS_BOARD_NET& net : crossBoardNets )
            {
                for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
                {
                    if( ep.subProjectUuid == sp.uuid && ep.componentRef == ref
                        && ep.pinNumber == pinNumber )
                    {
                        hasEndpoint = true;
                        break;
                    }
                }

                if( hasEndpoint )
                    break;
            }

            if( !hasEndpoint )
            {
                m_currentBoard1Uuid = sp.uuid;
                m_currentBoard1Name = subProjectName;
                m_currentBoard2Uuid = KIID( 0 );
                m_currentBoard2Name = wxEmptyString;
                m_currentConnectorRef = ref;

                AddViolation( CROSS_BOARD_DRC_TYPE::NET_ORPHAN_CONNECTOR,
                              CROSS_BOARD_DRC_SEVERITY::INFO,
                              wxString::Format( _( "Connector pad %s:%s has no cross-board link" ),
                                                ref, pinNumber ) );
            }
        }
    }
}


BOARD* DRC_ENGINE_CROSS_BOARD::GetBoardByUuid( const KIID& aBoardUuid )
{
    // Check cache first.
    auto it = m_boardCache.find( aBoardUuid );
    if( it != m_boardCache.end() )
        return it->second;

    // Load the sub-project's BOARD on demand. The KIID is a SUB_PROJECT_INFO::uuid
    // (the container model's stable sub-project identifier). The loader returns an
    // owner-managed BOARD per R9 in MULTI_BOARD_REFACTOR_PLAN.md.
    if( m_project )
    {
        if( std::unique_ptr<BOARD> loaded = LoadSubProjectBoard( *m_project, aBoardUuid ) )
        {
            BOARD* raw = loaded.get();
            m_loadedSubBoards.push_back( std::move( loaded ) );
            m_boardCache[aBoardUuid] = raw;
            return raw;
        }
    }

    return nullptr;
}


void DRC_ENGINE_CROSS_BOARD::AddViolation( CROSS_BOARD_DRC_TYPE aType,
                                            CROSS_BOARD_DRC_SEVERITY aSeverity,
                                            const wxString& aMessage,
                                            const wxString& aDetails )
{
    CROSS_BOARD_DRC_ITEM item;
    item.type = aType;
    item.severity = aSeverity;
    item.message = aMessage;
    item.details = aDetails;
    item.board1Uuid = m_currentBoard1Uuid;
    item.board1Name = m_currentBoard1Name;
    item.board2Uuid = m_currentBoard2Uuid;
    item.board2Name = m_currentBoard2Name;
    item.connectorRef = m_currentConnectorRef;

    m_violations.push_back( item );

    if( m_reporter )
    {
        SEVERITY severity = RPT_SEVERITY_INFO;
        if( aSeverity == CROSS_BOARD_DRC_SEVERITY::WARNING )
            severity = RPT_SEVERITY_WARNING;
        else if( aSeverity == CROSS_BOARD_DRC_SEVERITY::ERROR )
            severity = RPT_SEVERITY_ERROR;

        m_reporter->Report( aMessage, severity );
    }
}


bool DRC_ENGINE_CROSS_BOARD::NetNamesMatch( const wxString& aNet1, const wxString& aNet2 ) const
{
    // Direct match
    if( aNet1 == aNet2 )
        return true;

    // Check aliases
    auto it1 = m_connectorRules.netAliases.find( aNet1 );
    if( it1 != m_connectorRules.netAliases.end() && it1->second == aNet2 )
        return true;

    auto it2 = m_connectorRules.netAliases.find( aNet2 );
    if( it2 != m_connectorRules.netAliases.end() && it2->second == aNet1 )
        return true;

    return false;
}


int DRC_ENGINE_CROSS_BOARD::GetErrorCount() const
{
    int count = 0;
    for( const auto& item : m_violations )
    {
        if( item.severity == CROSS_BOARD_DRC_SEVERITY::ERROR )
            count++;
    }
    return count;
}


int DRC_ENGINE_CROSS_BOARD::GetWarningCount() const
{
    int count = 0;
    for( const auto& item : m_violations )
    {
        if( item.severity == CROSS_BOARD_DRC_SEVERITY::WARNING )
            count++;
    }
    return count;
}
