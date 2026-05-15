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

#include <agent_commit.h>
#include <eda_item.h>
#include <wx/log.h>

AGENT_COMMIT::AGENT_COMMIT() :
        m_transactionActive( false )
{
}


AGENT_COMMIT::~AGENT_COMMIT()
{
    Clear();
}


void AGENT_COMMIT::SetTargetSheet( const KIID& aSheetId )
{
    m_targetSheet = aSheetId;
}


void AGENT_COMMIT::BeginTransaction()
{
    if( m_transactionActive )
    {
        wxLogWarning( "AGENT_COMMIT: Beginning new transaction while one is already active" );
        Clear();
    }

    m_transactionActive = true;
    m_transactionStart = std::chrono::steady_clock::now();

    // Clear any previous state
    m_modifiedByAgent.clear();
    m_modifiedByUser.clear();
    m_agentPropertyChanges.clear();
    m_userPropertyChanges.clear();
    m_conflictResolutions.clear();
}


void AGENT_COMMIT::StageAdd( EDA_ITEM* aItem, BASE_SCREEN* aScreen )
{
    if( !m_transactionActive )
    {
        wxLogWarning( "AGENT_COMMIT: StageAdd called without active transaction" );
        return;
    }

    m_stagedAdds.push_back( { aItem, aScreen } );
    m_modifiedByAgent.insert( aItem->m_Uuid );
}


void AGENT_COMMIT::StageModify( EDA_ITEM* aItem, BASE_SCREEN* aScreen )
{
    if( !m_transactionActive )
    {
        wxLogWarning( "AGENT_COMMIT: StageModify called without active transaction" );
        return;
    }

    const KIID& itemId = aItem->m_Uuid;

    // Create a snapshot if we haven't already
    if( m_baseSnapshot.find( itemId ) == m_baseSnapshot.end() )
    {
        m_baseSnapshot[itemId] = makeSnapshot( aItem );
    }

    m_stagedModifies.push_back( { aItem, aScreen } );
    m_modifiedByAgent.insert( itemId );
}


void AGENT_COMMIT::StageRemove( EDA_ITEM* aItem, BASE_SCREEN* aScreen )
{
    if( !m_transactionActive )
    {
        wxLogWarning( "AGENT_COMMIT: StageRemove called without active transaction" );
        return;
    }

    // Create a snapshot before removal
    const KIID& itemId = aItem->m_Uuid;

    if( m_baseSnapshot.find( itemId ) == m_baseSnapshot.end() )
    {
        m_baseSnapshot[itemId] = makeSnapshot( aItem );
    }

    m_stagedRemoves.push_back( { aItem, aScreen } );
    m_modifiedByAgent.insert( itemId );
}


void AGENT_COMMIT::RecordPropertyChange( const KIID& aItemId, const wxString& aPropertyName,
                                          const wxString& aOldValue, const wxString& aNewValue )
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_transactionStart ).count();

    PROPERTY_CHANGE change;
    change.m_itemId = aItemId;
    change.m_propertyName = aPropertyName;
    change.m_oldValue = aOldValue;
    change.m_newValue = aNewValue;
    change.m_timestamp = elapsed;
    change.m_source = PROPERTY_CHANGE::AGENT;

    m_agentPropertyChanges.push_back( change );
}


CONFLICT_INFO AGENT_COMMIT::CheckConflict( const KIID& aItemId ) const
{
    CONFLICT_INFO info;
    info.m_itemId = aItemId;
    info.m_type = CONFLICT_TYPE::NONE;
    info.m_userItem = nullptr;
    info.m_agentItem = nullptr;
    info.m_canAutoMerge = false;

    // Check if both user and agent modified this item
    bool agentModified = m_modifiedByAgent.find( aItemId ) != m_modifiedByAgent.end();
    bool userModified = m_modifiedByUser.find( aItemId ) != m_modifiedByUser.end();

    if( !agentModified || !userModified )
    {
        return info;  // No conflict
    }

    info.m_type = CONFLICT_TYPE::SAME_ITEM;

    // Check if they modified the same property
    std::set<wxString> agentProperties;
    std::set<wxString> userProperties;

    for( const auto& change : m_agentPropertyChanges )
    {
        if( change.m_itemId == aItemId )
        {
            agentProperties.insert( change.m_propertyName );
            info.m_agentValue = change.m_newValue;
        }
    }

    for( const auto& change : m_userPropertyChanges )
    {
        if( change.m_itemId == aItemId )
        {
            userProperties.insert( change.m_propertyName );
            info.m_userValue = change.m_newValue;
        }
    }

    // Check for property-level conflicts
    for( const auto& prop : agentProperties )
    {
        if( userProperties.find( prop ) != userProperties.end() )
        {
            info.m_type = CONFLICT_TYPE::SAME_PROPERTY;
            info.m_propertyName = prop;
            info.m_canAutoMerge = false;
            return info;
        }
    }

    // If they modified different properties, we can auto-merge
    if( !agentProperties.empty() && !userProperties.empty() )
    {
        info.m_canAutoMerge = true;
    }

    return info;
}


void AGENT_COMMIT::RecordUserModification( const KIID& aItemId, const wxString& aPropertyName,
                                            const wxString& aOldValue, const wxString& aNewValue )
{
    m_modifiedByUser.insert( aItemId );

    if( !aPropertyName.IsEmpty() )
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_transactionStart ).count();

        PROPERTY_CHANGE change;
        change.m_itemId = aItemId;
        change.m_propertyName = aPropertyName;
        change.m_oldValue = aOldValue;
        change.m_newValue = aNewValue;
        change.m_timestamp = elapsed;
        change.m_source = PROPERTY_CHANGE::USER;

        m_userPropertyChanges.push_back( change );
    }
}


std::vector<CONFLICT_INFO> AGENT_COMMIT::GetConflicts() const
{
    std::vector<CONFLICT_INFO> conflicts;

    // Find items modified by both user and agent
    for( const auto& itemId : m_modifiedByAgent )
    {
        if( m_modifiedByUser.find( itemId ) != m_modifiedByUser.end() )
        {
            CONFLICT_INFO info = CheckConflict( itemId );

            if( info.m_type != CONFLICT_TYPE::NONE )
            {
                // Check if this conflict has been resolved
                auto resolution = m_conflictResolutions.find( itemId );

                if( resolution == m_conflictResolutions.end() )
                {
                    conflicts.push_back( info );
                }
            }
        }
    }

    return conflicts;
}


void AGENT_COMMIT::ResolveConflict( const KIID& aItemId, CONFLICT_RESOLUTION aResolution )
{
    m_conflictResolutions[aItemId] = aResolution;
}


bool AGENT_COMMIT::AllConflictsResolved() const
{
    for( const auto& itemId : m_modifiedByAgent )
    {
        if( m_modifiedByUser.find( itemId ) != m_modifiedByUser.end() )
        {
            CONFLICT_INFO info = CheckConflict( itemId );

            if( info.m_type != CONFLICT_TYPE::NONE )
            {
                auto resolution = m_conflictResolutions.find( itemId );

                if( resolution == m_conflictResolutions.end() )
                {
                    // Check if we can auto-merge
                    if( !info.m_canAutoMerge )
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}


void AGENT_COMMIT::ApplyToCommit( COMMIT& aCommit, const wxString& aMessage, int aFlags )
{
    // Apply adds
    for( auto& [item, screen] : m_stagedAdds )
    {
        // Check if this item has a conflict resolution
        auto resolution = m_conflictResolutions.find( item->m_Uuid );

        if( resolution != m_conflictResolutions.end() &&
            resolution->second == CONFLICT_RESOLUTION::KEEP_USER )
        {
            // User's version wins, skip agent's add
            delete item;
            continue;
        }

        aCommit.Add( item, screen );
    }

    // Apply modifications
    for( auto& [item, screen] : m_stagedModifies )
    {
        auto resolution = m_conflictResolutions.find( item->m_Uuid );

        if( resolution != m_conflictResolutions.end() &&
            resolution->second == CONFLICT_RESOLUTION::KEEP_USER )
        {
            // User's version wins, restore from snapshot
            auto snapshot = m_baseSnapshot.find( item->m_Uuid );

            if( snapshot != m_baseSnapshot.end() )
            {
                // Restore the original state
                // The item is already in the user's modified state, so we skip
                continue;
            }
        }

        aCommit.Modify( item, screen );
    }

    // Apply removes
    for( auto& [item, screen] : m_stagedRemoves )
    {
        auto resolution = m_conflictResolutions.find( item->m_Uuid );

        if( resolution != m_conflictResolutions.end() &&
            resolution->second == CONFLICT_RESOLUTION::KEEP_USER )
        {
            // User's version wins, don't remove
            continue;
        }

        aCommit.Remove( item, screen );
    }

    // Clear the staged changes since they're now in the commit
    m_stagedAdds.clear();
    m_stagedModifies.clear();
    m_stagedRemoves.clear();
}


void AGENT_COMMIT::RevertAll()
{
    // Delete added items (they were created by agent)
    for( auto& [item, screen] : m_stagedAdds )
    {
        delete item;
    }

    // Restore modified items from snapshots
    for( auto& [itemId, snapshot] : m_baseSnapshot )
    {
        // Find the current item and restore its state
        // This is a simplified version - real implementation would need
        // access to the document to find and restore items
        delete snapshot;
    }

    Clear();
}


void AGENT_COMMIT::EndTransaction( bool aApply )
{
    if( !m_transactionActive )
    {
        wxLogWarning( "AGENT_COMMIT: EndTransaction called without active transaction" );
        return;
    }

    if( !aApply )
    {
        RevertAll();
    }

    m_transactionActive = false;
}


bool AGENT_COMMIT::HasChanges() const
{
    return !m_stagedAdds.empty() || !m_stagedModifies.empty() || !m_stagedRemoves.empty();
}


size_t AGENT_COMMIT::GetChangeCount() const
{
    return m_stagedAdds.size() + m_stagedModifies.size() + m_stagedRemoves.size();
}


void AGENT_COMMIT::Clear()
{
    // Clean up snapshots
    for( auto& [itemId, snapshot] : m_baseSnapshot )
    {
        delete snapshot;
    }

    m_baseSnapshot.clear();
    m_stagedAdds.clear();
    m_stagedModifies.clear();
    m_stagedRemoves.clear();
    m_modifiedByAgent.clear();
    m_modifiedByUser.clear();
    m_agentPropertyChanges.clear();
    m_userPropertyChanges.clear();
    m_conflictResolutions.clear();
    m_transactionActive = false;
}


EDA_ITEM* AGENT_COMMIT::makeSnapshot( EDA_ITEM* aItem )
{
    // Create a deep copy of the item
    return aItem->Clone();
}


bool AGENT_COMMIT::canAutoMerge( const KIID& aItemId ) const
{
    CONFLICT_INFO info = CheckConflict( aItemId );
    return info.m_canAutoMerge;
}
