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

#ifndef MULTI_BOARD_NETLIST_UPDATER_H
#define MULTI_BOARD_NETLIST_UPDATER_H

#include <kiid.h>
#include <wx/string.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

class BOARD;
class COMPONENT;
class COMPONENT_ASSIGNMENT_MANAGER;
class NETLIST;
class PCB_EDIT_FRAME;
class PROJECT;
class REPORTER;


/**
 * Sync status for a single board in a multi-board project.
 */
struct BOARD_SYNC_STATUS
{
    KIID                    boardUuid;
    wxString                boardName;
    int                     componentsToAdd = 0;
    int                     componentsToRemove = 0;
    int                     componentsToUpdate = 0;
    int                     netsChanged = 0;
    std::vector<wxString>   conflicts;
    std::vector<wxString>   warnings;

    bool HasChanges() const
    {
        return componentsToAdd > 0 || componentsToRemove > 0 ||
               componentsToUpdate > 0 || netsChanged > 0;
    }

    bool HasIssues() const
    {
        return !conflicts.empty() || !warnings.empty();
    }
};


/**
 * Cross-board connection validation result.
 */
struct CROSS_BOARD_VALIDATION_RESULT
{
    enum class Severity { INFO, WARNING, ERROR };

    Severity    severity;
    wxString    message;
    KIID        board1Uuid;
    KIID        board2Uuid;
    wxString    connectorRef;   // e.g., "J1"
    wxString    pinNumber;      // e.g., "3"
};


/**
 * Handles netlist synchronization for multi-board projects.
 *
 * This class orchestrates the update of multiple boards from a single schematic
 * netlist. Components are assigned to specific boards via the COMPONENT_ASSIGNMENT_MANAGER,
 * and the netlist is partitioned accordingly before updating each board.
 *
 * Key features:
 * - Partitions netlist by board based on component assignments
 * - Validates cross-board connector pin matching
 * - Reports per-board sync status
 * - Supports dry-run mode for preview
 */
class MULTI_BOARD_NETLIST_UPDATER
{
public:
    /**
     * Construct a multi-board netlist updater.
     *
     * @param aFrame The PCB editor frame
     * @param aProject The current project (contains board list)
     */
    MULTI_BOARD_NETLIST_UPDATER( PCB_EDIT_FRAME* aFrame, PROJECT* aProject );

    ~MULTI_BOARD_NETLIST_UPDATER() = default;

    /**
     * Set the component assignment manager.
     * Must be set before calling any update methods.
     */
    void SetAssignmentManager( COMPONENT_ASSIGNMENT_MANAGER* aManager )
    {
        m_assignmentManager = aManager;
    }

    /**
     * Set the reporter for status messages.
     */
    void SetReporter( REPORTER* aReporter ) { m_reporter = aReporter; }

    /**
     * Enable dry run mode (preview changes without applying).
     */
    void SetIsDryRun( bool aEnabled ) { m_isDryRun = aEnabled; }

    /**
     * Set whether to replace footprints with library versions.
     */
    void SetReplaceFootprints( bool aEnabled ) { m_replaceFootprints = aEnabled; }

    /**
     * Set whether to delete footprints not in the netlist.
     */
    void SetDeleteUnusedFootprints( bool aEnabled ) { m_deleteUnusedFootprints = aEnabled; }

    /**
     * Set whether to use timestamps for component matching.
     */
    void SetLookupByTimestamp( bool aEnabled ) { m_lookupByTimestamp = aEnabled; }

    /**
     * Partition the netlist by board based on component assignments.
     *
     * Creates separate netlists for each board containing only the components
     * assigned to that board. Components assigned to multiple boards (connectors)
     * appear in all relevant board netlists.
     *
     * @param aNetlist The full schematic netlist
     * @return Map of board UUID to partitioned netlist
     */
    std::map<KIID, std::unique_ptr<NETLIST>> PartitionNetlist( const NETLIST& aNetlist );

    /**
     * Get sync status for all boards without making changes.
     *
     * @param aNetlist The schematic netlist
     * @return Map of board UUID to sync status
     */
    std::map<KIID, BOARD_SYNC_STATUS> GetSyncStatus( const NETLIST& aNetlist );

    /**
     * Update a specific board from a partitioned netlist.
     *
     * @param aBoardUuid UUID of the board to update
     * @param aNetlist The partitioned netlist for this board
     * @return true if update was successful
     */
    bool UpdateBoard( const KIID& aBoardUuid, NETLIST& aNetlist );

    /**
     * Update all boards from the full netlist.
     *
     * Automatically partitions the netlist and updates each board.
     *
     * @param aNetlist The full schematic netlist
     * @return true if all updates were successful
     */
    bool UpdateAllBoards( NETLIST& aNetlist );

    /**
     * Validate cross-board connector connections.
     *
     * Checks that:
     * - Connector pin counts match between boards
     * - Net names are consistent across connector pins
     * - No orphan connectors (connector on one board without match)
     *
     * @return Vector of validation results
     */
    std::vector<CROSS_BOARD_VALIDATION_RESULT> ValidateCrossBoardConnectors();

    /**
     * Get the list of components that need board assignment.
     *
     * @param aNetlist The netlist to check
     * @return Set of component references without assignments
     */
    std::set<wxString> GetUnassignedComponents( const NETLIST& aNetlist ) const;

    /**
     * Get components assigned to multiple boards (connectors).
     */
    std::set<wxString> GetMultiBoardComponents() const;

private:
    /**
     * Load a board by UUID from the project.
     */
    BOARD* GetBoardByUuid( const KIID& aBoardUuid );

    /**
     * Analyze netlist to compute sync status for a board.
     */
    BOARD_SYNC_STATUS ComputeSyncStatus( const KIID& aBoardUuid, const NETLIST& aNetlist );

    /**
     * Check if a component should be included in a board's partitioned netlist.
     */
    bool ShouldIncludeComponent( const COMPONENT* aComponent, const KIID& aBoardUuid ) const;

    PCB_EDIT_FRAME*                 m_frame;
    PROJECT*                        m_project;
    COMPONENT_ASSIGNMENT_MANAGER*   m_assignmentManager;
    REPORTER*                       m_reporter;

    // Settings
    bool m_isDryRun;
    bool m_replaceFootprints;
    bool m_deleteUnusedFootprints;
    bool m_lookupByTimestamp;

    // Cached board pointers
    std::map<KIID, BOARD*>          m_boardCache;
};

#endif // MULTI_BOARD_NETLIST_UPDATER_H
