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

#include <agent_commit.h>
#include <kiid.h>
#include <set>
#include <map>
#include <functional>

class EDA_ITEM;
class EDA_DRAW_FRAME;
class KIWAY;

/**
 * Callback type for conflict notifications.
 * Parameters: item UUID, conflict info
 */
using CONFLICT_CALLBACK = std::function<void( const KIID&, const CONFLICT_INFO& )>;

/**
 * Callback type for transaction state changes.
 * Parameters: is_active, target_sheet
 */
using TRANSACTION_CALLBACK = std::function<void( bool, const KIID& )>;


/**
 * AGENT_WORKSPACE manages the agent's editing context independent of the user's view.
 *
 * This class provides:
 * - Sheet targeting: Agent operations can target a specific sheet regardless of
 *   what sheet the user is currently viewing
 * - Transaction management: Groups agent operations into atomic units
 * - Conflict detection: Tracks user edits and detects conflicts with agent changes
 * - Approval workflow: Staged changes aren't applied until user approves
 *
 * Usage:
 * 1. SetTargetSheet() - Specify which sheet the agent will work on
 * 2. BeginTransaction() - Start a new editing session
 * 3. Make changes through the AGENT_COMMIT
 * 4. EndTransaction() with commit=true when done
 * 5. User reviews and approves/rejects via ApproveChanges()/RejectChanges()
 */
class AGENT_WORKSPACE
{
public:
    AGENT_WORKSPACE();
    ~AGENT_WORKSPACE();

    /**
     * Initialize the workspace with a KIWAY for cross-frame communication.
     */
    void SetKiway( KIWAY* aKiway ) { m_kiway = aKiway; }

    /**
     * Set the target sheet for subsequent agent operations.
     * Operations will be applied to this sheet regardless of what sheet
     * the user is currently viewing.
     *
     * @param aSheetId The UUID of the target sheet
     */
    void SetTargetSheet( const KIID& aSheetId );

    /**
     * Get the current target sheet UUID.
     */
    const KIID& GetTargetSheet() const { return m_targetSheet; }

    /**
     * Check if the target sheet differs from the user's current sheet.
     */
    bool IsTargetingDifferentSheet( const KIID& aCurrentUserSheet ) const;

    /**
     * Begin a new agent transaction.
     * All subsequent changes will be tracked for conflict detection.
     *
     * @return true if transaction started successfully
     */
    bool BeginTransaction();

    /**
     * Check if a transaction is currently active.
     */
    bool IsTransactionActive() const { return m_transactionActive; }

    /**
     * End the current transaction.
     *
     * @param aCommit If true, prepares changes for approval; if false, discards changes
     * @return true if transaction ended successfully
     */
    bool EndTransaction( bool aCommit );

    /**
     * Get the AGENT_COMMIT for staging changes.
     * Changes staged here won't be applied until approved.
     */
    AGENT_COMMIT& GetAgentCommit() { return m_agentCommit; }
    const AGENT_COMMIT& GetAgentCommit() const { return m_agentCommit; }

    /**
     * Record that the user modified an item.
     * Called by the conflict detector when it observes user edits.
     *
     * @param aItemId The UUID of the modified item
     * @param aPropertyName Optional specific property that was changed
     * @param aOldValue Optional old value
     * @param aNewValue Optional new value
     */
    void RecordUserEdit( const KIID& aItemId, const wxString& aPropertyName = wxEmptyString,
                         const wxString& aOldValue = wxEmptyString,
                         const wxString& aNewValue = wxEmptyString );

    /**
     * Check if there's a conflict with the given item.
     */
    CONFLICT_INFO HasConflictWith( const KIID& aItemId ) const;

    /**
     * Get all current conflicts.
     */
    std::vector<CONFLICT_INFO> GetConflicts() const;

    /**
     * Resolve a specific conflict.
     */
    void ResolveConflict( const KIID& aItemId, CONFLICT_RESOLUTION aResolution );

    /**
     * Check if all conflicts have been resolved.
     */
    bool AllConflictsResolved() const;

    /**
     * Get items that the agent has modified.
     * Used by the conflict detector to know which user edits to watch.
     */
    const std::set<KIID>& GetAgentWorkingSet() const { return m_agentCommit.GetModifiedByAgent(); }

    /**
     * Check if the agent has pending changes.
     */
    bool HasPendingChanges() const { return m_agentCommit.HasChanges(); }

    /**
     * Get count of pending changes.
     */
    size_t GetPendingChangeCount() const { return m_agentCommit.GetChangeCount(); }

    /**
     * Approve all pending changes.
     * Applies the staged changes to the document and creates undo entries.
     *
     * @param aCommit The actual COMMIT to use for applying changes
     * @param aMessage The undo message
     * @param aFlags Commit flags
     * @return true if changes were applied successfully
     */
    bool ApproveChanges( COMMIT& aCommit, const wxString& aMessage, int aFlags = 0 );

    /**
     * Reject all pending changes.
     * Discards all staged agent changes.
     *
     * @return true if changes were rejected successfully
     */
    bool RejectChanges();

    /**
     * Clear all state and reset the workspace.
     */
    void Reset();

    /**
     * Set callback for conflict notifications.
     */
    void SetConflictCallback( CONFLICT_CALLBACK aCallback ) { m_conflictCallback = aCallback; }

    /**
     * Set callback for transaction state changes.
     */
    void SetTransactionCallback( TRANSACTION_CALLBACK aCallback )
    {
        m_transactionCallback = aCallback;
    }

    /**
     * Notify that a conflict was detected (triggers callback).
     */
    void NotifyConflict( const KIID& aItemId, const CONFLICT_INFO& aInfo );

private:
    /// Send message to editors about agent working set changes
    void notifyWorkingSetChanged();

    /// Send message about transaction state changes
    void notifyTransactionStateChanged();

private:
    KIWAY*                  m_kiway;
    KIID                    m_targetSheet;
    bool                    m_transactionActive;
    AGENT_COMMIT            m_agentCommit;

    // Callbacks
    CONFLICT_CALLBACK       m_conflictCallback;
    TRANSACTION_CALLBACK    m_transactionCallback;
};
