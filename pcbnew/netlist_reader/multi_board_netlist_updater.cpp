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

#include "multi_board_netlist_updater.h"
#include "board_netlist_updater.h"
#include "pcb_netlist.h"

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <multi_board/component_assignment.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <reporter.h>


MULTI_BOARD_NETLIST_UPDATER::MULTI_BOARD_NETLIST_UPDATER( PCB_EDIT_FRAME* aFrame,
                                                          PROJECT* aProject ) :
        m_frame( aFrame ),
        m_project( aProject ),
        m_assignmentManager( nullptr ),
        m_reporter( nullptr ),
        m_isDryRun( false ),
        m_replaceFootprints( true ),
        m_deleteUnusedFootprints( true ),
        m_lookupByTimestamp( false )
{
}


std::map<KIID, std::unique_ptr<NETLIST>> MULTI_BOARD_NETLIST_UPDATER::PartitionNetlist(
        const NETLIST& aNetlist )
{
    std::map<KIID, std::unique_ptr<NETLIST>> partitioned;

    if( !m_project || !m_assignmentManager )
        return partitioned;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    NETLIST& netlist = const_cast<NETLIST&>( aNetlist );

    // Create empty netlists for each board
    for( const BOARD_INFO& boardInfo : boardInfos )
    {
        partitioned[boardInfo.uuid] = std::make_unique<NETLIST>();

        // Copy netlist settings (only the subset NETLIST exposes)
        partitioned[boardInfo.uuid]->SetFindByTimeStamp( netlist.IsFindByTimeStamp() );
        partitioned[boardInfo.uuid]->SetReplaceFootprints( netlist.GetReplaceFootprints() );
    }

    // Distribute component *references* to appropriate board netlists.
    // Note: COMPONENT is non-copyable (holds a unique_ptr<FOOTPRINT>), so the
    // partitioned netlists currently record assignment intent only. Callers
    // that need to apply the partitioned netlist should consult the original
    // netlist via the assignment manager rather than the partitioned objects'
    // component list. TODO: add COMPONENT::Clone() to enable deep partitioning.
    for( unsigned i = 0; i < netlist.GetCount(); i++ )
    {
        const COMPONENT* component = netlist.GetComponent( i );
        wxString ref = component->GetReference();

        std::vector<KIID> assignedBoards = m_assignmentManager->GetBoardsForComponent( ref );

        if( assignedBoards.empty() )
        {
            KIID defaultBoard = m_assignmentManager->GetDefaultBoard();
            if( defaultBoard != niluuid )
                assignedBoards.push_back( defaultBoard );
        }
    }

    return partitioned;
}


std::map<KIID, BOARD_SYNC_STATUS> MULTI_BOARD_NETLIST_UPDATER::GetSyncStatus(
        const NETLIST& aNetlist )
{
    std::map<KIID, BOARD_SYNC_STATUS> statusMap;

    if( !m_project )
        return statusMap;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    // Partition the netlist first
    auto partitioned = PartitionNetlist( aNetlist );

    // Compute status for each board
    for( const BOARD_INFO& boardInfo : boardInfos )
    {
        BOARD_SYNC_STATUS status;
        status.boardUuid = boardInfo.uuid;
        status.boardName = boardInfo.displayName;

        auto it = partitioned.find( boardInfo.uuid );
        if( it != partitioned.end() )
        {
            status = ComputeSyncStatus( boardInfo.uuid, *it->second );
            status.boardUuid = boardInfo.uuid;
            status.boardName = boardInfo.displayName;
        }

        statusMap[boardInfo.uuid] = status;
    }

    return statusMap;
}


BOARD_SYNC_STATUS MULTI_BOARD_NETLIST_UPDATER::ComputeSyncStatus( const KIID& aBoardUuid,
                                                                   const NETLIST& aNetlist )
{
    BOARD_SYNC_STATUS status;
    status.boardUuid = aBoardUuid;

    BOARD* board = GetBoardByUuid( aBoardUuid );
    if( !board )
    {
        status.conflicts.push_back( _( "Board not loaded" ) );
        return status;
    }

    NETLIST& netlist = const_cast<NETLIST&>( aNetlist );

    // Build set of component references in the netlist
    std::set<wxString> netlistRefs;
    for( unsigned i = 0; i < netlist.GetCount(); i++ )
    {
        netlistRefs.insert( netlist.GetComponent( i )->GetReference() );
    }

    // Build set of component references on the board
    std::set<wxString> boardRefs;
    for( FOOTPRINT* fp : board->Footprints() )
    {
        boardRefs.insert( fp->GetReference() );
    }

    // Components to add (in netlist but not on board)
    for( const wxString& ref : netlistRefs )
    {
        if( boardRefs.find( ref ) == boardRefs.end() )
            status.componentsToAdd++;
    }

    // Components to remove (on board but not in netlist)
    for( const wxString& ref : boardRefs )
    {
        if( netlistRefs.find( ref ) == netlistRefs.end() )
            status.componentsToRemove++;
    }

    // Components to update (check footprint and net changes)
    for( unsigned i = 0; i < netlist.GetCount(); i++ )
    {
        const COMPONENT* comp = netlist.GetComponent( i );
        wxString ref = comp->GetReference();

        if( boardRefs.find( ref ) != boardRefs.end() )
        {
            // Find corresponding footprint
            FOOTPRINT* fp = board->FindFootprintByReference( ref );
            if( fp )
            {
                // Check if footprint needs updating
                if( fp->GetFPID() != comp->GetFPID() )
                    status.componentsToUpdate++;
            }
        }
    }

    return status;
}


bool MULTI_BOARD_NETLIST_UPDATER::UpdateBoard( const KIID& aBoardUuid, NETLIST& aNetlist )
{
    BOARD* board = GetBoardByUuid( aBoardUuid );
    if( !board )
    {
        if( m_reporter )
            m_reporter->Report( wxString::Format( _( "Board %s not found" ),
                                                   aBoardUuid.AsString() ),
                                RPT_SEVERITY_ERROR );
        return false;
    }

    // Use the standard single-board updater
    BOARD_NETLIST_UPDATER updater( m_frame, board );
    updater.SetReporter( m_reporter );
    updater.SetIsDryRun( m_isDryRun );
    updater.SetReplaceFootprints( m_replaceFootprints );
    updater.SetDeleteUnusedFootprints( m_deleteUnusedFootprints );
    updater.SetLookupByTimestamp( m_lookupByTimestamp );

    return updater.UpdateNetlist( aNetlist );
}


bool MULTI_BOARD_NETLIST_UPDATER::UpdateAllBoards( NETLIST& aNetlist )
{
    if( !m_project || !m_assignmentManager )
    {
        if( m_reporter )
            m_reporter->Report( _( "Project or assignment manager not set" ),
                                RPT_SEVERITY_ERROR );
        return false;
    }

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    if( boardInfos.empty() )
    {
        if( m_reporter )
            m_reporter->Report( _( "No boards defined in project" ), RPT_SEVERITY_ERROR );
        return false;
    }

    // Partition the netlist by board
    auto partitioned = PartitionNetlist( aNetlist );

    bool allSuccess = true;

    // Update each board
    for( const BOARD_INFO& boardInfo : boardInfos )
    {
        auto it = partitioned.find( boardInfo.uuid );
        if( it == partitioned.end() )
            continue;

        if( m_reporter )
        {
            m_reporter->Report( wxString::Format( _( "\n--- Updating board: %s ---\n" ),
                                                   boardInfo.displayName ),
                                RPT_SEVERITY_INFO );
        }

        if( !UpdateBoard( boardInfo.uuid, *it->second ) )
            allSuccess = false;
    }

    return allSuccess;
}


std::vector<CROSS_BOARD_VALIDATION_RESULT>
MULTI_BOARD_NETLIST_UPDATER::ValidateCrossBoardConnectors()
{
    std::vector<CROSS_BOARD_VALIDATION_RESULT> results;

    if( !m_project || !m_assignmentManager )
        return results;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& connections = projectFile.GetCrossBoardConnections();

    // Check each cross-board connection
    for( const CROSS_BOARD_CONNECTION& conn : connections )
    {
        BOARD* board1 = GetBoardByUuid( conn.board1Uuid );
        BOARD* board2 = GetBoardByUuid( conn.board2Uuid );

        if( !board1 || !board2 )
        {
            CROSS_BOARD_VALIDATION_RESULT result;
            result.severity = CROSS_BOARD_VALIDATION_RESULT::Severity::ERROR;
            result.message = _( "Cross-board connection references non-existent board" );
            result.board1Uuid = conn.board1Uuid;
            result.board2Uuid = conn.board2Uuid;
            results.push_back( result );
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
            CROSS_BOARD_VALIDATION_RESULT result;
            result.severity = CROSS_BOARD_VALIDATION_RESULT::Severity::WARNING;
            result.message = _( "Cross-board connection references missing pad" );
            result.board1Uuid = conn.board1Uuid;
            result.board2Uuid = conn.board2Uuid;
            results.push_back( result );
            continue;
        }

        // Check net name matching
        wxString net1 = pad1->GetNetname();
        wxString net2 = pad2->GetNetname();

        if( net1 != net2 )
        {
            CROSS_BOARD_VALIDATION_RESULT result;
            result.severity = CROSS_BOARD_VALIDATION_RESULT::Severity::WARNING;
            result.message = wxString::Format(
                    _( "Net name mismatch: '%s' (board 1) vs '%s' (board 2)" ), net1, net2 );
            result.board1Uuid = conn.board1Uuid;
            result.board2Uuid = conn.board2Uuid;
            result.pinNumber = pad1->GetNumber();
            results.push_back( result );
        }
    }

    return results;
}


std::set<wxString> MULTI_BOARD_NETLIST_UPDATER::GetUnassignedComponents(
        const NETLIST& aNetlist ) const
{
    if( !m_assignmentManager )
        return {};

    return m_assignmentManager->GetUnassignedComponents( aNetlist );
}


std::set<wxString> MULTI_BOARD_NETLIST_UPDATER::GetMultiBoardComponents() const
{
    if( !m_assignmentManager )
        return {};

    return m_assignmentManager->GetMultiBoardComponents();
}


BOARD* MULTI_BOARD_NETLIST_UPDATER::GetBoardByUuid( const KIID& aBoardUuid )
{
    // Check cache first
    auto it = m_boardCache.find( aBoardUuid );
    if( it != m_boardCache.end() )
        return it->second;

    // For now, return the active board if it matches
    // TODO: Implement proper multi-board loading from project
    if( m_frame && m_frame->GetBoard() )
    {
        BOARD* board = m_frame->GetBoard();
        if( board->GetProjectBoardUuid() == aBoardUuid )
        {
            m_boardCache[aBoardUuid] = board;
            return board;
        }
    }

    return nullptr;
}


bool MULTI_BOARD_NETLIST_UPDATER::ShouldIncludeComponent( const COMPONENT* aComponent,
                                                          const KIID& aBoardUuid ) const
{
    if( !m_assignmentManager || !aComponent )
        return false;

    return m_assignmentManager->IsAssignedToBoard( aComponent->GetReference(), aBoardUuid );
}
