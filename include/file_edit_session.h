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

#ifndef FILE_EDIT_SESSION_H
#define FILE_EDIT_SESSION_H

#include <kiid.h>
#include <set>
#include <map>
#include <memory>
#include <wx/string.h>

class EDA_ITEM;
class AGENT_CHANGE_TRACKER;

/**
 * FILE_EDIT_SESSION manages the lifecycle of an agent file edit operation.
 *
 * This class supports the following workflow:
 * 1. Agent requests file edit session begin
 * 2. Editor takes a snapshot of current item state
 * 3. Agent writes directly to the file
 * 4. Agent validates the file (lint check)
 * 5. Editor reloads file and computes diff against snapshot
 * 6. Diff overlay is shown for user approval
 * 7. Session ends with commit or abort
 *
 * The session tracks added, modified, and deleted items to support
 * proper undo/redo integration.
 */
class FILE_EDIT_SESSION
{
public:
    /**
     * Session state machine states.
     */
    enum class State
    {
        IDLE,            ///< No active session
        SNAPSHOT_TAKEN,  ///< Snapshot has been taken, waiting for file write
        LINT_PASSED,     ///< File written and lint validation passed
        RELOADED,        ///< File has been reloaded and diff computed
        COMMITTED        ///< Session committed (changes accepted)
    };

    FILE_EDIT_SESSION();
    ~FILE_EDIT_SESSION();

    /**
     * Begin a new file edit session.
     * @param aFilePath The path to the file being edited.
     * @return true if session started successfully.
     */
    bool BeginSession( const wxString& aFilePath );

    /**
     * End the current session.
     * @param aCommit true to commit changes, false to abort.
     */
    void EndSession( bool aCommit );

    /**
     * Get the current session state.
     */
    State GetState() const { return m_state; }

    /**
     * Check if a session is active.
     */
    bool IsActive() const { return m_state != State::IDLE; }

    /**
     * Get the file path for this session.
     */
    const wxString& GetFilePath() const { return m_filePath; }

    /**
     * Record a snapshot of an item's current state.
     * Called before the agent modifies the file.
     * @param aItemId The KIID of the item.
     * @param aClone A clone of the item in its current state (takes ownership).
     */
    void RecordItemSnapshot( const KIID& aItemId, std::unique_ptr<EDA_ITEM> aClone );

    /**
     * Get all item IDs that were in the snapshot.
     */
    std::set<KIID> GetSnapshotItemIds() const;

    /**
     * Check if an item was in the snapshot.
     */
    bool WasInSnapshot( const KIID& aItemId ) const;

    /**
     * Get the snapshot clone for an item.
     * @return The cloned item, or nullptr if not found.
     */
    const EDA_ITEM* GetSnapshotClone( const KIID& aItemId ) const;

    /**
     * Mark state as lint passed.
     */
    void SetLintPassed() { m_state = State::LINT_PASSED; }

    /**
     * Mark state as reloaded (diff computed).
     */
    void SetReloaded() { m_state = State::RELOADED; }

    /**
     * Record an item as added (present after reload but not in snapshot).
     */
    void RecordAddedItem( const KIID& aItemId );

    /**
     * Record an item as modified (different after reload vs snapshot).
     */
    void RecordModifiedItem( const KIID& aItemId );

    /**
     * Record an item as deleted (in snapshot but not after reload).
     */
    void RecordDeletedItem( const KIID& aItemId );

    /**
     * Get all items that were added.
     */
    const std::set<KIID>& GetAddedItems() const { return m_addedItems; }

    /**
     * Get all items that were modified.
     */
    const std::set<KIID>& GetModifiedItems() const { return m_modifiedItems; }

    /**
     * Get all items that were deleted.
     */
    const std::set<KIID>& GetDeletedItems() const { return m_deletedItems; }

    /**
     * Clear diff results.
     */
    void ClearDiffResults();

    /**
     * Populate an AGENT_CHANGE_TRACKER with all changed items.
     * @param aTracker The tracker to populate.
     * @param aSheetPath The sheet path for schematic items (empty for PCB).
     */
    void PopulateTracker( AGENT_CHANGE_TRACKER* aTracker, const wxString& aSheetPath ) const;

private:
    State m_state = State::IDLE;
    wxString m_filePath;

    // Snapshot: clones of items before agent file write
    std::map<KIID, std::unique_ptr<EDA_ITEM>> m_snapshotClones;

    // Diff results after reload
    std::set<KIID> m_addedItems;
    std::set<KIID> m_modifiedItems;
    std::set<KIID> m_deletedItems;
};

#endif // FILE_EDIT_SESSION_H
