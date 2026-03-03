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

#include <agent_change_tracker.h>


AGENT_CHANGE_TRACKER::AGENT_CHANGE_TRACKER()
{
}


AGENT_CHANGE_TRACKER::~AGENT_CHANGE_TRACKER()
{
}


void AGENT_CHANGE_TRACKER::TrackItem( const KIID& aItemId, const wxString& aSheetPath,
                                      AGENT_CHANGE_TYPE aChangeType )
{
    TrackItem( aItemId, aSheetPath, aChangeType, BOX2I() );
}


void AGENT_CHANGE_TRACKER::TrackItem( const KIID& aItemId, const wxString& aSheetPath,
                                      AGENT_CHANGE_TYPE aChangeType, const BOX2I& aDeletedBBox )
{
    auto it = m_trackedItems.find( aItemId );
    if( it != m_trackedItems.end() )
    {
        if( it->second.changeType == AGENT_CHANGE_TYPE::ADDED )
        {
            if( aChangeType == AGENT_CHANGE_TYPE::DELETED )
            {
                // Agent created then deleted this item — net result is nothing, untrack it
                m_trackedItems.erase( it );
                return;
            }

            // A subsequent CHANGED entry doesn't change the fact that the agent created this item
            return;
        }
    }

    m_trackedItems[aItemId] = { aSheetPath, aChangeType, aDeletedBBox };
}


void AGENT_CHANGE_TRACKER::TrackItem( const KIID& aItemId, AGENT_CHANGE_TYPE aChangeType )
{
    // For PCB items, use empty string for sheet path
    TrackItem( aItemId, wxEmptyString, aChangeType );
}


void AGENT_CHANGE_TRACKER::UntrackItem( const KIID& aItemId )
{
    m_trackedItems.erase( aItemId );
}


bool AGENT_CHANGE_TRACKER::IsTracked( const KIID& aItemId ) const
{
    return m_trackedItems.find( aItemId ) != m_trackedItems.end();
}


void AGENT_CHANGE_TRACKER::ClearTrackedItems()
{
    m_trackedItems.clear();
}


std::set<wxString> AGENT_CHANGE_TRACKER::GetAffectedSheets() const
{
    std::set<wxString> sheets;

    for( const auto& [kiid, info] : m_trackedItems )
    {
        sheets.insert( info.sheetPath );
    }

    return sheets;
}


std::set<KIID> AGENT_CHANGE_TRACKER::GetTrackedItemsOnSheet( const wxString& aSheetPath ) const
{
    std::set<KIID> items;

    for( const auto& [kiid, info] : m_trackedItems )
    {
        if( info.sheetPath == aSheetPath )
            items.insert( kiid );
    }

    return items;
}


size_t AGENT_CHANGE_TRACKER::UntrackItemsOnSheetAndNested( const wxString& aSheetPath )
{
    std::vector<KIID> toRemove;

    for( const auto& [kiid, info] : m_trackedItems )
    {
        // Match exact path or any path that starts with this path
        // e.g., aSheetPath="/root/sub" matches "/root/sub", "/root/sub/nested", etc.
        if( info.sheetPath == aSheetPath || info.sheetPath.StartsWith( aSheetPath + "/" ) )
        {
            toRemove.push_back( kiid );
        }
    }

    for( const KIID& kiid : toRemove )
    {
        m_trackedItems.erase( kiid );
    }

    return toRemove.size();
}


std::set<KIID> AGENT_CHANGE_TRACKER::GetAllTrackedItems() const
{
    std::set<KIID> items;

    for( const auto& [kiid, info] : m_trackedItems )
    {
        items.insert( kiid );
    }

    return items;
}


wxString AGENT_CHANGE_TRACKER::GetSheetPathForItem( const KIID& aItemId ) const
{
    auto it = m_trackedItems.find( aItemId );
    if( it != m_trackedItems.end() )
        return it->second.sheetPath;

    return wxEmptyString;
}


AGENT_CHANGE_TYPE AGENT_CHANGE_TRACKER::GetChangeType( const KIID& aItemId ) const
{
    auto it = m_trackedItems.find( aItemId );
    if( it != m_trackedItems.end() )
        return it->second.changeType;

    return AGENT_CHANGE_TYPE::ADDED;
}


BOX2I AGENT_CHANGE_TRACKER::GetDeletedBBox( const KIID& aItemId ) const
{
    auto it = m_trackedItems.find( aItemId );
    if( it != m_trackedItems.end() && it->second.changeType == AGENT_CHANGE_TYPE::DELETED )
        return it->second.deletedBBox;

    return BOX2I();
}


bool AGENT_CHANGE_TRACKER::HasChanges() const
{
    return !m_trackedItems.empty();
}


size_t AGENT_CHANGE_TRACKER::GetTrackedItemCount() const
{
    return m_trackedItems.size();
}
