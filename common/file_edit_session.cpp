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

#include <file_edit_session.h>
#include <agent_change_tracker.h>
#include <eda_item.h>


FILE_EDIT_SESSION::FILE_EDIT_SESSION()
{
}


FILE_EDIT_SESSION::~FILE_EDIT_SESSION()
{
}


bool FILE_EDIT_SESSION::BeginSession( const wxString& aFilePath )
{
    if( m_state != State::IDLE )
        return false;

    m_filePath = aFilePath;
    m_snapshotClones.clear();
    ClearDiffResults();
    m_state = State::SNAPSHOT_TAKEN;

    return true;
}


void FILE_EDIT_SESSION::EndSession( bool aCommit )
{
    if( aCommit )
        m_state = State::COMMITTED;
    else
        m_state = State::IDLE;

    // Clear snapshot data
    m_snapshotClones.clear();
    ClearDiffResults();
    m_filePath.clear();
    m_state = State::IDLE;
}


void FILE_EDIT_SESSION::RecordItemSnapshot( const KIID& aItemId, std::unique_ptr<EDA_ITEM> aClone )
{
    if( aClone )
        m_snapshotClones[aItemId] = std::move( aClone );
}


std::set<KIID> FILE_EDIT_SESSION::GetSnapshotItemIds() const
{
    std::set<KIID> ids;

    for( const auto& [kiid, clone] : m_snapshotClones )
    {
        ids.insert( kiid );
    }

    return ids;
}


bool FILE_EDIT_SESSION::WasInSnapshot( const KIID& aItemId ) const
{
    return m_snapshotClones.find( aItemId ) != m_snapshotClones.end();
}


const EDA_ITEM* FILE_EDIT_SESSION::GetSnapshotClone( const KIID& aItemId ) const
{
    auto it = m_snapshotClones.find( aItemId );
    if( it != m_snapshotClones.end() )
        return it->second.get();

    return nullptr;
}


void FILE_EDIT_SESSION::RecordAddedItem( const KIID& aItemId )
{
    m_addedItems.insert( aItemId );
}


void FILE_EDIT_SESSION::RecordModifiedItem( const KIID& aItemId )
{
    m_modifiedItems.insert( aItemId );
}


void FILE_EDIT_SESSION::RecordDeletedItem( const KIID& aItemId )
{
    m_deletedItems.insert( aItemId );
}


void FILE_EDIT_SESSION::ClearDiffResults()
{
    m_addedItems.clear();
    m_modifiedItems.clear();
    m_deletedItems.clear();
}


void FILE_EDIT_SESSION::PopulateTracker( AGENT_CHANGE_TRACKER* aTracker,
                                          const wxString& aSheetPath ) const
{
    if( !aTracker )
        return;

    // Track all added and modified items
    for( const KIID& kiid : m_addedItems )
    {
        aTracker->TrackItem( kiid, aSheetPath );
    }

    for( const KIID& kiid : m_modifiedItems )
    {
        aTracker->TrackItem( kiid, aSheetPath );
    }

    // Note: deleted items are not tracked since they no longer exist
    // The tracker is for items that can be displayed in the diff overlay
}
