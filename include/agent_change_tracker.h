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

#ifndef AGENT_CHANGE_TRACKER_H
#define AGENT_CHANGE_TRACKER_H

#include <kiid.h>
#include <set>
#include <map>
#include <wx/string.h>

/**
 * AGENT_CHANGE_TRACKER provides item-based tracking of agent changes.
 *
 * Instead of storing a static bounding box, this class tracks individual items
 * by their KIID. The bounding box is computed dynamically from the current
 * positions of tracked items, allowing the diff overlay to follow items as
 * they are moved by the user.
 *
 * For schematic editing, items are associated with their sheet path to support
 * multi-sheet diff overlays.
 *
 * Note: ComputeBBox methods are implemented in the frame classes (SCH_EDIT_FRAME,
 * PCB_EDIT_FRAME) that have access to SCHEMATIC and BOARD objects.
 */
class AGENT_CHANGE_TRACKER
{
public:
    AGENT_CHANGE_TRACKER();
    ~AGENT_CHANGE_TRACKER();

    /**
     * Track an item by KIID with its sheet path (for schematic).
     * @param aItemId The KIID of the item to track.
     * @param aSheetPath The sheet path where the item exists (as a string).
     */
    void TrackItem( const KIID& aItemId, const wxString& aSheetPath );

    /**
     * Track an item by KIID without sheet path (for PCB).
     * @param aItemId The KIID of the item to track.
     */
    void TrackItem( const KIID& aItemId );

    /**
     * Stop tracking an item.
     * @param aItemId The KIID of the item to untrack.
     */
    void UntrackItem( const KIID& aItemId );

    /**
     * Check if an item is being tracked.
     * @param aItemId The KIID to check.
     * @return true if the item is tracked.
     */
    bool IsTracked( const KIID& aItemId ) const;

    /**
     * Clear all tracked items.
     */
    void ClearTrackedItems();

    /**
     * Get the set of sheet paths that have tracked items.
     * @return Set of sheet path strings.
     */
    std::set<wxString> GetAffectedSheets() const;

    /**
     * Get all tracked items on a specific sheet.
     * @param aSheetPath The sheet path to query.
     * @return Set of KIIDs on that sheet.
     */
    std::set<KIID> GetTrackedItemsOnSheet( const wxString& aSheetPath ) const;

    /**
     * Untrack all items on a sheet and any nested subsheets.
     * When a sheet symbol is deleted, this untracks items on that sheet
     * and recursively on any sheets that start with the given path.
     * @param aSheetPath The sheet path prefix to untrack.
     * @return Number of items untracked.
     */
    size_t UntrackItemsOnSheetAndNested( const wxString& aSheetPath );

    /**
     * Get all tracked items regardless of sheet.
     * @return Set of all tracked KIIDs.
     */
    std::set<KIID> GetAllTrackedItems() const;

    /**
     * Get the sheet path for a tracked item.
     * @param aItemId The KIID to query.
     * @return The sheet path string, or empty if not tracked.
     */
    wxString GetSheetPathForItem( const KIID& aItemId ) const;

    /**
     * Set the undo stack baseline for this tracking session.
     * @param aUndoCount The undo stack count at session start.
     */
    void SetUndoBaseline( int aUndoCount );

    /**
     * Get the undo stack baseline.
     * @return The undo count at session start.
     */
    int GetUndoBaseline() const;

    /**
     * Check if there are any tracked changes.
     * @return true if at least one item is tracked.
     */
    bool HasChanges() const;

    /**
     * Get the number of tracked items.
     * @return The count of tracked items.
     */
    size_t GetTrackedItemCount() const;

private:
    // Map of KIID -> sheet path (empty string for PCB items)
    std::map<KIID, wxString> m_trackedItems;

    // Undo stack count at the start of agent session
    int m_undoBaseline = 0;
};

#endif // AGENT_CHANGE_TRACKER_H
