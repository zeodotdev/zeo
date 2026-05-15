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

#pragma once

#include <commit.h>
#include <kiid.h>
#include <map>
#include <set>
#include <chrono>

class EDA_ITEM;
class EDA_DRAW_FRAME;
class TOOL_MANAGER;

/**
 * Tracks a single property change for conflict detection and property-level merging.
 */
struct PROPERTY_CHANGE
{
    KIID        m_itemId;
    wxString    m_propertyName;     // "Position", "Value", "Footprint", etc.
    wxString    m_oldValue;
    wxString    m_newValue;
    int64_t     m_timestamp;        // Milliseconds since transaction start
    enum Source { USER, AGENT }     m_source;
};


/**
 * Types of conflicts that can occur during concurrent editing.
 */
enum class CONFLICT_TYPE
{
    NONE,               ///< No conflict
    SAME_ITEM,          ///< User and agent modified the same item
    SAME_PROPERTY,      ///< User and agent modified the same property of the same item
    SPATIAL_OVERLAP,    ///< Agent placed item where user placed one
    CONNECTION          ///< User/agent added same wire/connection
};


/**
 * Information about a detected conflict.
 */
struct CONFLICT_INFO
{
    KIID            m_itemId;
    CONFLICT_TYPE   m_type;
    wxString        m_propertyName;
    wxString        m_userValue;
    wxString        m_agentValue;
    EDA_ITEM*       m_userItem;
    EDA_ITEM*       m_agentItem;
    bool            m_canAutoMerge;     ///< True if changes are to different properties
};


/**
 * Resolution strategies for conflicts.
 */
enum class CONFLICT_RESOLUTION
{
    KEEP_USER,      ///< Discard agent's change for this item
    KEEP_AGENT,     ///< Discard user's change for this item
    AUTO_MERGE,     ///< Merge non-conflicting property changes
    MANUAL          ///< User must manually review
};


/**
 * AGENT_COMMIT manages agent changes that are staged but not yet committed to the
 * undo stack. This enables concurrent editing where:
 * - Agent changes are isolated until approved
 * - User can continue editing while agent works
 * - Conflicts are detected and can be resolved
 *
 * Unlike normal COMMITs that push to the undo stack immediately, AGENT_COMMIT
 * holds changes in a pending state until explicitly approved or rejected.
 */
class AGENT_COMMIT
{
public:
    AGENT_COMMIT();
    ~AGENT_COMMIT();

    /**
     * Set the target sheet for agent operations.
     * All subsequent operations will target this sheet regardless of what
     * sheet the user is currently viewing.
     */
    void SetTargetSheet( const KIID& aSheetId );

    /**
     * Get the target sheet UUID.
     */
    const KIID& GetTargetSheet() const { return m_targetSheet; }

    /**
     * Begin a new agent transaction.
     * Takes a snapshot of items for conflict detection.
     */
    void BeginTransaction();

    /**
     * Check if a transaction is currently active.
     */
    bool IsTransactionActive() const { return m_transactionActive; }

    /**
     * Stage an item addition in the target sheet.
     * @param aItem The item to add
     * @param aScreen The screen/sheet to add to (nullptr uses target sheet)
     */
    void StageAdd( EDA_ITEM* aItem, BASE_SCREEN* aScreen = nullptr );

    /**
     * Stage an item modification.
     * @param aItem The item being modified (must call before making changes)
     * @param aScreen The screen containing the item
     */
    void StageModify( EDA_ITEM* aItem, BASE_SCREEN* aScreen = nullptr );

    /**
     * Stage an item removal.
     * @param aItem The item to remove
     * @param aScreen The screen containing the item
     */
    void StageRemove( EDA_ITEM* aItem, BASE_SCREEN* aScreen = nullptr );

    /**
     * Record that a specific property was changed on an item.
     * Used for property-level conflict detection and merging.
     */
    void RecordPropertyChange( const KIID& aItemId, const wxString& aPropertyName,
                               const wxString& aOldValue, const wxString& aNewValue );

    /**
     * Check if there's a conflict with the given item.
     * @param aItemId The item to check
     * @return CONFLICT_INFO with details if conflict exists
     */
    CONFLICT_INFO CheckConflict( const KIID& aItemId ) const;

    /**
     * Record that the user modified an item.
     * Called by the conflict detector when user makes changes.
     */
    void RecordUserModification( const KIID& aItemId, const wxString& aPropertyName = wxEmptyString,
                                 const wxString& aOldValue = wxEmptyString,
                                 const wxString& aNewValue = wxEmptyString );

    /**
     * Get all items modified by the agent.
     */
    const std::set<KIID>& GetModifiedByAgent() const { return m_modifiedByAgent; }

    /**
     * Get all items modified by the user during this transaction.
     */
    const std::set<KIID>& GetModifiedByUser() const { return m_modifiedByUser; }

    /**
     * Get all detected conflicts.
     */
    std::vector<CONFLICT_INFO> GetConflicts() const;

    /**
     * Resolve a conflict with the specified strategy.
     * @param aItemId The conflicting item
     * @param aResolution How to resolve the conflict
     */
    void ResolveConflict( const KIID& aItemId, CONFLICT_RESOLUTION aResolution );

    /**
     * Check if all conflicts have been resolved.
     */
    bool AllConflictsResolved() const;

    /**
     * Apply all staged changes to the actual document.
     * Creates proper undo entries. Call only after all conflicts are resolved.
     * @param aCommit The actual commit to use for pushing changes
     * @param aMessage The undo message
     * @param aFlags Commit flags (SKIP_UNDO, APPEND_UNDO, etc.)
     */
    void ApplyToCommit( COMMIT& aCommit, const wxString& aMessage, int aFlags = 0 );

    /**
     * Revert all staged changes without applying them.
     */
    void RevertAll();

    /**
     * End the current transaction.
     * @param aApply If true, applies changes; if false, reverts them
     */
    void EndTransaction( bool aApply );

    /**
     * Check if there are any staged changes.
     */
    bool HasChanges() const;

    /**
     * Get the count of staged changes.
     */
    size_t GetChangeCount() const;

    /**
     * Get the list of added items.
     */
    const std::vector<std::pair<EDA_ITEM*, BASE_SCREEN*>>& GetAddedItems() const
    {
        return m_stagedAdds;
    }

    /**
     * Get the list of modified items (with their original copies).
     */
    const std::map<KIID, EDA_ITEM*>& GetModifiedItems() const
    {
        return m_baseSnapshot;
    }

    /**
     * Get the list of removed items.
     */
    const std::vector<std::pair<EDA_ITEM*, BASE_SCREEN*>>& GetRemovedItems() const
    {
        return m_stagedRemoves;
    }

    /**
     * Clear all staged changes and reset state.
     */
    void Clear();

private:
    /// Create a deep copy of an item for the base snapshot
    EDA_ITEM* makeSnapshot( EDA_ITEM* aItem );

    /// Check if a property change can be auto-merged
    bool canAutoMerge( const KIID& aItemId ) const;

private:
    KIID                                        m_targetSheet;
    bool                                        m_transactionActive;
    std::chrono::steady_clock::time_point       m_transactionStart;

    // Staged changes (not yet committed)
    std::vector<std::pair<EDA_ITEM*, BASE_SCREEN*>>  m_stagedAdds;
    std::vector<std::pair<EDA_ITEM*, BASE_SCREEN*>>  m_stagedModifies;
    std::vector<std::pair<EDA_ITEM*, BASE_SCREEN*>>  m_stagedRemoves;

    // Base snapshot for conflict detection (copies of items at transaction start)
    std::map<KIID, EDA_ITEM*>                   m_baseSnapshot;

    // Tracking which items were modified by agent vs user
    std::set<KIID>                              m_modifiedByAgent;
    std::set<KIID>                              m_modifiedByUser;

    // Property-level change tracking for fine-grained conflict detection
    std::vector<PROPERTY_CHANGE>                m_agentPropertyChanges;
    std::vector<PROPERTY_CHANGE>                m_userPropertyChanges;

    // Conflict resolution decisions
    std::map<KIID, CONFLICT_RESOLUTION>         m_conflictResolutions;
};
