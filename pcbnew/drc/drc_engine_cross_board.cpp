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


void DRC_ENGINE_CROSS_BOARD::CheckConnectorMatching()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& connections = projectFile.GetCrossBoardConnections();
    const auto& boardInfos = projectFile.GetBoardInfos();

    // Build board name map
    std::map<KIID, wxString> boardNames;
    for( const BOARD_INFO& info : boardInfos )
        boardNames[info.uuid] = info.displayName;

    // Check each cross-board connection
    for( const CROSS_BOARD_CONNECTION& conn : connections )
    {
        m_currentBoard1Uuid = conn.board1Uuid;
        m_currentBoard1Name = boardNames.count( conn.board1Uuid ) ?
                              boardNames[conn.board1Uuid] : _( "Unknown" );
        m_currentBoard2Uuid = conn.board2Uuid;
        m_currentBoard2Name = boardNames.count( conn.board2Uuid ) ?
                              boardNames[conn.board2Uuid] : _( "Unknown" );

        BOARD* board1 = GetBoardByUuid( conn.board1Uuid );
        BOARD* board2 = GetBoardByUuid( conn.board2Uuid );

        if( !board1 || !board2 )
        {
            AddViolation( CROSS_BOARD_DRC_TYPE::NET_INCOMPLETE,
                          CROSS_BOARD_DRC_SEVERITY::ERROR,
                          _( "Cross-board connection references unavailable board" ) );
            continue;
        }

        // Find pads by UUID
        PAD* pad1 = nullptr;
        PAD* pad2 = nullptr;

        for( FOOTPRINT* fp : board1->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->m_Uuid == conn.pad1Uuid )
                {
                    pad1 = pad;
                    m_currentConnectorRef = fp->GetReference();
                    break;
                }
            }
            if( pad1 )
                break;
        }

        for( FOOTPRINT* fp : board2->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->m_Uuid == conn.pad2Uuid )
                {
                    pad2 = pad;
                    break;
                }
            }
            if( pad2 )
                break;
        }

        if( !pad1 || !pad2 )
        {
            AddViolation( CROSS_BOARD_DRC_TYPE::NET_ORPHAN_CONNECTOR,
                          CROSS_BOARD_DRC_SEVERITY::WARNING,
                          _( "Cross-board connection references missing pad" ) );
            continue;
        }

        // Check net name matching
        if( m_connectorRules.checkNetNameMatch )
        {
            wxString net1 = pad1->GetNetname();
            wxString net2 = pad2->GetNetname();

            if( !NetNamesMatch( net1, net2 ) )
            {
                wxString details = wxString::Format(
                        _( "'%s' (board 1) vs '%s' (board 2)" ), net1, net2 );

                AddViolation( CROSS_BOARD_DRC_TYPE::CONNECTOR_NET_NAME_MISMATCH,
                              CROSS_BOARD_DRC_SEVERITY::WARNING,
                              _( "Net name mismatch on connector pin" ),
                              details );
            }
        }

        // Check for unassigned pins (no net)
        if( pad1->GetNetCode() <= 0 || pad2->GetNetCode() <= 0 )
        {
            AddViolation( CROSS_BOARD_DRC_TYPE::UNASSIGNED_CONNECTOR_PIN,
                          CROSS_BOARD_DRC_SEVERITY::WARNING,
                          _( "Connector pin has no net assignment" ) );
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

    // Check minimum power pins
    for( const auto& [netName, minPins] : m_powerRules.minPowerPins )
    {
        // TODO: Count connector pins for each power net across boards
        // and verify minimum pin count requirements

        if( m_reporter )
        {
            m_reporter->Report( wxString::Format(
                    _( "Power net '%s' requires minimum %d pins" ), netName, minPins ),
                    RPT_SEVERITY_INFO );
        }
    }
}


void DRC_ENGINE_CROSS_BOARD::CheckNetCompleteness()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    // For each board, check if connector pads have valid cross-board connections
    for( const BOARD_INFO& boardInfo : boardInfos )
    {
        BOARD* board = GetBoardByUuid( boardInfo.uuid );
        if( !board )
            continue;

        const auto& connectorPads = board->GetConnectorPads();

        for( const KIID& padUuid : connectorPads )
        {
            // Find the pad
            PAD* pad = nullptr;
            for( FOOTPRINT* fp : board->Footprints() )
            {
                for( PAD* p : fp->Pads() )
                {
                    if( p->m_Uuid == padUuid )
                    {
                        pad = p;
                        break;
                    }
                }
                if( pad )
                    break;
            }

            if( !pad )
                continue;

            // Check if this connector pad has a cross-board connection defined
            bool hasConnection = false;
            const auto& connections = projectFile.GetCrossBoardConnections();

            for( const CROSS_BOARD_CONNECTION& conn : connections )
            {
                if( ( conn.board1Uuid == boardInfo.uuid && conn.pad1Uuid == padUuid ) ||
                    ( conn.board2Uuid == boardInfo.uuid && conn.pad2Uuid == padUuid ) )
                {
                    hasConnection = true;
                    break;
                }
            }

            if( !hasConnection )
            {
                m_currentBoard1Uuid = boardInfo.uuid;
                m_currentBoard1Name = boardInfo.displayName;
                m_currentConnectorRef = pad->GetParentFootprint() ?
                                        pad->GetParentFootprint()->GetReference() : wxT( "?" );

                AddViolation( CROSS_BOARD_DRC_TYPE::NET_ORPHAN_CONNECTOR,
                              CROSS_BOARD_DRC_SEVERITY::INFO,
                              wxString::Format( _( "Connector pad %s:%s has no cross-board link" ),
                                                 m_currentConnectorRef, pad->GetNumber() ) );
            }
        }
    }
}


BOARD* DRC_ENGINE_CROSS_BOARD::GetBoardByUuid( const KIID& aBoardUuid )
{
    // Check cache first
    auto it = m_boardCache.find( aBoardUuid );
    if( it != m_boardCache.end() )
        return it->second;

    // TODO: Implement proper board loading from project
    // For now, return nullptr - boards would need to be loaded separately
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
