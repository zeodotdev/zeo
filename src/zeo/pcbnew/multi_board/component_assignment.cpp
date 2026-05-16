/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "component_assignment.h"

#include <netlist_reader/pcb_netlist.h>
#include <project/project_file.h>

#include <algorithm>


COMPONENT_ASSIGNMENT_MANAGER::COMPONENT_ASSIGNMENT_MANAGER()
{
}


void COMPONENT_ASSIGNMENT_MANAGER::Clear()
{
    m_componentAssignments.clear();
    m_defaultBoardUuid = KIID();
}


void COMPONENT_ASSIGNMENT_MANAGER::AssignToBoard( const wxString& aReference,
                                                   const KIID& aBoardUuid )
{
    auto& boards = m_componentAssignments[aReference];

    // Check if already assigned to this board
    auto it = std::find( boards.begin(), boards.end(), aBoardUuid );
    if( it == boards.end() )
        boards.push_back( aBoardUuid );
}


void COMPONENT_ASSIGNMENT_MANAGER::UnassignFromBoard( const wxString& aReference,
                                                       const KIID& aBoardUuid )
{
    auto it = m_componentAssignments.find( aReference );
    if( it == m_componentAssignments.end() )
        return;

    auto& boards = it->second;
    boards.erase( std::remove( boards.begin(), boards.end(), aBoardUuid ), boards.end() );

    // Remove the entry entirely if no boards remain
    if( boards.empty() )
        m_componentAssignments.erase( it );
}


void COMPONENT_ASSIGNMENT_MANAGER::AssignToMultipleBoards( const wxString& aReference,
                                                            const std::vector<KIID>& aBoardUuids )
{
    if( aBoardUuids.empty() )
    {
        m_componentAssignments.erase( aReference );
    }
    else
    {
        m_componentAssignments[aReference] = aBoardUuids;
    }
}


std::vector<KIID> COMPONENT_ASSIGNMENT_MANAGER::GetBoardsForComponent(
        const wxString& aReference ) const
{
    auto it = m_componentAssignments.find( aReference );
    if( it != m_componentAssignments.end() )
        return it->second;

    // Return default board if no explicit assignment
    if( m_defaultBoardUuid != niluuid )
        return { m_defaultBoardUuid };

    return {};
}


bool COMPONENT_ASSIGNMENT_MANAGER::IsMultiBoardComponent( const wxString& aReference ) const
{
    auto it = m_componentAssignments.find( aReference );
    return it != m_componentAssignments.end() && it->second.size() > 1;
}


bool COMPONENT_ASSIGNMENT_MANAGER::IsAssignedToBoard( const wxString& aReference,
                                                       const KIID& aBoardUuid ) const
{
    auto it = m_componentAssignments.find( aReference );
    if( it != m_componentAssignments.end() )
    {
        const auto& boards = it->second;
        return std::find( boards.begin(), boards.end(), aBoardUuid ) != boards.end();
    }

    // Check if this is the default board
    return m_defaultBoardUuid == aBoardUuid;
}


bool COMPONENT_ASSIGNMENT_MANAGER::HasAssignment( const wxString& aReference ) const
{
    auto it = m_componentAssignments.find( aReference );
    return it != m_componentAssignments.end() && !it->second.empty();
}


std::set<wxString> COMPONENT_ASSIGNMENT_MANAGER::GetComponentsForBoard(
        const KIID& aBoardUuid ) const
{
    std::set<wxString> components;

    for( const auto& [ref, boards] : m_componentAssignments )
    {
        if( std::find( boards.begin(), boards.end(), aBoardUuid ) != boards.end() )
            components.insert( ref );
    }

    return components;
}


std::set<wxString> COMPONENT_ASSIGNMENT_MANAGER::GetMultiBoardComponents() const
{
    std::set<wxString> multiBoard;

    for( const auto& [ref, boards] : m_componentAssignments )
    {
        if( boards.size() > 1 )
            multiBoard.insert( ref );
    }

    return multiBoard;
}


std::set<wxString> COMPONENT_ASSIGNMENT_MANAGER::GetUnassignedComponents(
        const NETLIST& aNetlist ) const
{
    std::set<wxString> unassigned;

    NETLIST& netlist = const_cast<NETLIST&>( aNetlist );

    for( unsigned i = 0; i < netlist.GetCount(); i++ )
    {
        const COMPONENT* component = netlist.GetComponent( i );
        wxString ref = component->GetReference();

        if( !HasAssignment( ref ) && m_defaultBoardUuid == niluuid )
            unassigned.insert( ref );
    }

    return unassigned;
}


void COMPONENT_ASSIGNMENT_MANAGER::AutoAssignByNetConnectivity(
        const NETLIST& aNetlist,
        const std::vector<KIID>& aExistingBoardUuids )
{
    if( aExistingBoardUuids.empty() )
        return;

    // For now, implement a simple heuristic:
    // - If only one board exists, assign all components to it
    // - Otherwise, leave unassigned for manual assignment
    //
    // TODO: Implement more sophisticated net-based analysis:
    // - Analyze which nets span which boards
    // - Assign components based on their net connections
    // - Mark components connected to cross-board nets as multi-board

    if( aExistingBoardUuids.size() == 1 )
    {
        // Single board - assign all components
        const KIID& boardUuid = aExistingBoardUuids[0];
        NETLIST& netlist = const_cast<NETLIST&>( aNetlist );

        for( unsigned i = 0; i < netlist.GetCount(); i++ )
        {
            const COMPONENT* component = netlist.GetComponent( i );
            wxString ref = component->GetReference();

            if( !HasAssignment( ref ) )
                AssignToBoard( ref, boardUuid );
        }
    }
}


void COMPONENT_ASSIGNMENT_MANAGER::SaveToProject( PROJECT_FILE* aProjectFile )
{
    if( !aProjectFile )
        return;

    // Clear existing assignments and rebuild from our map
    auto& assignments = aProjectFile->GetComponentAssignments();
    assignments.clear();

    for( const auto& [ref, boards] : m_componentAssignments )
    {
        assignments.emplace_back( ref, boards );
    }
}


void COMPONENT_ASSIGNMENT_MANAGER::LoadFromProject( const PROJECT_FILE* aProjectFile )
{
    if( !aProjectFile )
        return;

    m_componentAssignments.clear();

    const auto& assignments = aProjectFile->GetComponentAssignments();
    for( const auto& assignment : assignments )
    {
        m_componentAssignments[assignment.reference] = assignment.boardUuids;
    }

    // Set default board to the active board if available
    const BOARD_INFO* activeBoard =
            const_cast<PROJECT_FILE*>( aProjectFile )->GetActiveBoardInfo();
    if( activeBoard )
        m_defaultBoardUuid = activeBoard->uuid;
}
