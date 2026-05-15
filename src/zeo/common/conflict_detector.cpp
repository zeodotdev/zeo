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

#include <conflict_detector.h>
#include <agent_workspace.h>
#include <eda_item.h>
#include <sch_item.h>
#include <board_item.h>
#include <wx/log.h>


//=============================================================================
// SCH_CONFLICT_DETECTOR Implementation
//=============================================================================

SCH_CONFLICT_DETECTOR::SCH_CONFLICT_DETECTOR() :
        m_workspace( nullptr ),
        m_enabled( false )
{
}


SCH_CONFLICT_DETECTOR::~SCH_CONFLICT_DETECTOR()
{
}


void SCH_CONFLICT_DETECTOR::SetWorkspace( AGENT_WORKSPACE* aWorkspace )
{
    m_workspace = aWorkspace;
}


void SCH_CONFLICT_DETECTOR::SetAgentWorkingSet( const std::set<KIID>& aWorkingSet )
{
    m_agentWorkingSet = aWorkingSet;
}


void SCH_CONFLICT_DETECTOR::AddToWorkingSet( const KIID& aItemId )
{
    m_agentWorkingSet.insert( aItemId );
}


void SCH_CONFLICT_DETECTOR::RemoveFromWorkingSet( const KIID& aItemId )
{
    m_agentWorkingSet.erase( aItemId );
}


void SCH_CONFLICT_DETECTOR::ClearWorkingSet()
{
    m_agentWorkingSet.clear();
}


bool SCH_CONFLICT_DETECTOR::IsInWorkingSet( const KIID& aItemId ) const
{
    return m_agentWorkingSet.find( aItemId ) != m_agentWorkingSet.end();
}


void SCH_CONFLICT_DETECTOR::OnSchItemsAdded( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aSchItems, wxT( "added" ) );
}


void SCH_CONFLICT_DETECTOR::OnSchItemsRemoved( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aSchItems, wxT( "removed" ) );
}


void SCH_CONFLICT_DETECTOR::OnSchItemsChanged( SCHEMATIC& aSch, std::vector<SCH_ITEM*>& aSchItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aSchItems, wxT( "changed" ) );
}


void SCH_CONFLICT_DETECTOR::OnSchSheetChanged( SCHEMATIC& aSch )
{
    // User changed sheets - not a conflict, but we might want to track this
}


void SCH_CONFLICT_DETECTOR::checkForConflicts( std::vector<SCH_ITEM*>& aItems,
                                                const wxString& aChangeType )
{
    for( SCH_ITEM* item : aItems )
    {
        if( !item )
            continue;

        // Cast to EDA_ITEM to access m_Uuid
        EDA_ITEM* edaItem = static_cast<EDA_ITEM*>( item );
        const KIID& itemId = edaItem->m_Uuid;

        if( IsInWorkingSet( itemId ) )
        {
            wxLogDebug( "SCH_CONFLICT_DETECTOR: User %s item %s that's in agent's working set",
                        aChangeType, itemId.AsString() );

            // Notify the workspace
            if( m_workspace )
            {
                m_workspace->RecordUserEdit( itemId, aChangeType );
            }

            // Call the callback if set
            if( m_userEditCallback )
            {
                m_userEditCallback( itemId, aChangeType, wxEmptyString, wxEmptyString );
            }
        }
    }
}


//=============================================================================
// BOARD_CONFLICT_DETECTOR Implementation
//=============================================================================

BOARD_CONFLICT_DETECTOR::BOARD_CONFLICT_DETECTOR() :
        m_workspace( nullptr ),
        m_enabled( false )
{
}


BOARD_CONFLICT_DETECTOR::~BOARD_CONFLICT_DETECTOR()
{
}


void BOARD_CONFLICT_DETECTOR::SetWorkspace( AGENT_WORKSPACE* aWorkspace )
{
    m_workspace = aWorkspace;
}


void BOARD_CONFLICT_DETECTOR::SetAgentWorkingSet( const std::set<KIID>& aWorkingSet )
{
    m_agentWorkingSet = aWorkingSet;
}


void BOARD_CONFLICT_DETECTOR::AddToWorkingSet( const KIID& aItemId )
{
    m_agentWorkingSet.insert( aItemId );
}


void BOARD_CONFLICT_DETECTOR::RemoveFromWorkingSet( const KIID& aItemId )
{
    m_agentWorkingSet.erase( aItemId );
}


void BOARD_CONFLICT_DETECTOR::ClearWorkingSet()
{
    m_agentWorkingSet.clear();
}


bool BOARD_CONFLICT_DETECTOR::IsInWorkingSet( const KIID& aItemId ) const
{
    return m_agentWorkingSet.find( aItemId ) != m_agentWorkingSet.end();
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemAdded( BOARD& aBoard, BOARD_ITEM* aBoardItem )
{
    if( !m_enabled || !aBoardItem )
        return;

    checkForConflict( aBoardItem, wxT( "added" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemsAdded( BOARD& aBoard,
                                                  std::vector<BOARD_ITEM*>& aBoardItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aBoardItems, wxT( "added" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemRemoved( BOARD& aBoard, BOARD_ITEM* aBoardItem )
{
    if( !m_enabled || !aBoardItem )
        return;

    checkForConflict( aBoardItem, wxT( "removed" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemsRemoved( BOARD& aBoard,
                                                    std::vector<BOARD_ITEM*>& aBoardItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aBoardItems, wxT( "removed" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemChanged( BOARD& aBoard, BOARD_ITEM* aBoardItem )
{
    if( !m_enabled || !aBoardItem )
        return;

    checkForConflict( aBoardItem, wxT( "changed" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardItemsChanged( BOARD& aBoard,
                                                    std::vector<BOARD_ITEM*>& aBoardItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aBoardItems, wxT( "changed" ) );
}


void BOARD_CONFLICT_DETECTOR::OnBoardNetSettingsChanged( BOARD& aBoard )
{
    // Net settings changed - not typically a direct conflict with item edits
}


void BOARD_CONFLICT_DETECTOR::OnBoardHighlightNetChanged( BOARD& aBoard )
{
    // Highlight change - not a conflict
}


void BOARD_CONFLICT_DETECTOR::OnBoardRatsnestChanged( BOARD& aBoard )
{
    // Ratsnest change - not a direct conflict, but may indicate connectivity changes
}


void BOARD_CONFLICT_DETECTOR::OnBoardCompositeUpdate( BOARD& aBoard,
                                                       std::vector<BOARD_ITEM*>& aAddedItems,
                                                       std::vector<BOARD_ITEM*>& aRemovedItems,
                                                       std::vector<BOARD_ITEM*>& aChangedItems )
{
    if( !m_enabled )
        return;

    checkForConflicts( aAddedItems, wxT( "added" ) );
    checkForConflicts( aRemovedItems, wxT( "removed" ) );
    checkForConflicts( aChangedItems, wxT( "changed" ) );
}


void BOARD_CONFLICT_DETECTOR::checkForConflicts( std::vector<BOARD_ITEM*>& aItems,
                                                  const wxString& aChangeType )
{
    for( BOARD_ITEM* item : aItems )
    {
        checkForConflict( item, aChangeType );
    }
}


void BOARD_CONFLICT_DETECTOR::checkForConflict( BOARD_ITEM* aItem, const wxString& aChangeType )
{
    if( !aItem )
        return;

    // Cast to EDA_ITEM to access m_Uuid
    EDA_ITEM* edaItem = static_cast<EDA_ITEM*>( aItem );
    const KIID& itemId = edaItem->m_Uuid;

    if( IsInWorkingSet( itemId ) )
    {
        wxLogDebug( "BOARD_CONFLICT_DETECTOR: User %s item %s that's in agent's working set",
                    aChangeType, itemId.AsString() );

        // Notify the workspace
        if( m_workspace )
        {
            m_workspace->RecordUserEdit( itemId, aChangeType );
        }

        // Call the callback if set
        if( m_userEditCallback )
        {
            m_userEditCallback( itemId, aChangeType, wxEmptyString, wxEmptyString );
        }
    }
}
