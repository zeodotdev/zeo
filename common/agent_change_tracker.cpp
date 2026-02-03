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


void AGENT_CHANGE_TRACKER::TrackItem( const KIID& aItemId, const wxString& aSheetPath )
{
    m_trackedItems[aItemId] = aSheetPath;
}


void AGENT_CHANGE_TRACKER::TrackItem( const KIID& aItemId )
{
    // For PCB items, use empty string for sheet path
    TrackItem( aItemId, wxEmptyString );
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
    m_undoBaseline = 0;
}


std::set<wxString> AGENT_CHANGE_TRACKER::GetAffectedSheets() const
{
    std::set<wxString> sheets;

    for( const auto& [kiid, sheetPath] : m_trackedItems )
    {
        sheets.insert( sheetPath );
    }

    return sheets;
}


std::set<KIID> AGENT_CHANGE_TRACKER::GetTrackedItemsOnSheet( const wxString& aSheetPath ) const
{
    std::set<KIID> items;

    for( const auto& [kiid, sheetPath] : m_trackedItems )
    {
        if( sheetPath == aSheetPath )
            items.insert( kiid );
    }

    return items;
}


size_t AGENT_CHANGE_TRACKER::UntrackItemsOnSheetAndNested( const wxString& aSheetPath )
{
    std::vector<KIID> toRemove;

    for( const auto& [kiid, sheetPath] : m_trackedItems )
    {
        // Match exact path or any path that starts with this path
        // e.g., aSheetPath="/root/sub" matches "/root/sub", "/root/sub/nested", etc.
        if( sheetPath == aSheetPath || sheetPath.StartsWith( aSheetPath + "/" ) )
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

    for( const auto& [kiid, sheetPath] : m_trackedItems )
    {
        items.insert( kiid );
    }

    return items;
}


wxString AGENT_CHANGE_TRACKER::GetSheetPathForItem( const KIID& aItemId ) const
{
    auto it = m_trackedItems.find( aItemId );
    if( it != m_trackedItems.end() )
        return it->second;

    return wxEmptyString;
}


void AGENT_CHANGE_TRACKER::SetUndoBaseline( int aUndoCount )
{
    m_undoBaseline = aUndoCount;
}


int AGENT_CHANGE_TRACKER::GetUndoBaseline() const
{
    return m_undoBaseline;
}


bool AGENT_CHANGE_TRACKER::HasChanges() const
{
    return !m_trackedItems.empty();
}


size_t AGENT_CHANGE_TRACKER::GetTrackedItemCount() const
{
    return m_trackedItems.size();
}
