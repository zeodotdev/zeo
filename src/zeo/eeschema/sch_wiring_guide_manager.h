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

#ifndef SCH_WIRING_GUIDE_MANAGER_H
#define SCH_WIRING_GUIDE_MANAGER_H

#include <wx/string.h>
#include <math/vector2d.h>
#include <kiid.h>
#include <vector>
#include <map>

class SCH_EDIT_FRAME;
class SCH_SYMBOL;
class SCH_SCREEN;
class AGENT_CHANGE_TRACKER;

/**
 * @class SCH_WIRING_GUIDE_MANAGER
 * @brief Manages wiring guide display and interaction for the sch_draft_circuit tool.
 *
 * This class scans symbols for Agent_Wiring fields, tracks guide states,
 * compares against actual connectivity, and handles guide dismissal.
 *
 * Guides are only shown for symbols that have been approved (not pending in diff).
 * During diff review, guide previews are shown by DIFF_OVERLAY_ITEM instead.
 */
class SCH_WIRING_GUIDE_MANAGER
{
public:
    /**
     * A single wiring guide representing a recommended connection.
     */
    struct WIRING_GUIDE
    {
        KIID     sourceSymbolId;    ///< KIID of the symbol containing the recommendation
        wxString sourceRef;         ///< Reference designator of source symbol (e.g., "R1")
        wxString sourcePin;         ///< Pin number/name on source symbol
        VECTOR2I sourcePos;         ///< Screen position of source pin

        wxString targetRef;         ///< Target: "U1:PA0" or net name like "VCC"
        VECTOR2I targetPos;         ///< Screen position of target (if resolved)
        bool     targetResolved;    ///< True if target position was successfully resolved

        bool     isComplete;        ///< True if connection exists (wired)
        bool     isVisible;         ///< User visibility toggle (per-guide)
    };

    SCH_WIRING_GUIDE_MANAGER( SCH_EDIT_FRAME* aFrame );
    ~SCH_WIRING_GUIDE_MANAGER();

    /**
     * Scan all symbols on the current sheet for Agent_Wiring fields.
     * Builds the list of guides and resolves target positions.
     * Should be called after approval or when sheet changes.
     */
    void ScanSymbolsForWiring();

    /**
     * Refresh guide completion states by checking actual connectivity.
     * Call this after wires are added/removed.
     */
    void RefreshGuideStates();

    /**
     * Dismiss a specific wiring recommendation.
     * This modifies the symbol's Agent_Wiring field and pushes to undo.
     *
     * @param aSymbolId KIID of the symbol
     * @param aPin Pin number/name to dismiss
     */
    void DismissGuide( const KIID& aSymbolId, const wxString& aPin );

    /**
     * Dismiss all wiring recommendations for a symbol.
     * Clears the entire Agent_Wiring field.
     *
     * @param aSymbolId KIID of the symbol
     */
    void DismissAllForSymbol( const KIID& aSymbolId );

    /**
     * Toggle visibility of a specific guide.
     *
     * @param aSymbolId KIID of the symbol
     * @param aPin Pin number/name
     * @param aVisible New visibility state
     */
    void SetGuideVisible( const KIID& aSymbolId, const wxString& aPin, bool aVisible );

    /**
     * Set global visibility for all guides.
     */
    void SetAllGuidesVisible( bool aVisible );

    /**
     * Get all active (incomplete, visible) guides for rendering.
     * Filters out completed and hidden guides.
     *
     * @return Vector of active WIRING_GUIDE structs
     */
    std::vector<WIRING_GUIDE> GetActiveGuides() const;

    /**
     * Get all guides (including completed and hidden) for UI display.
     *
     * @return Vector of all WIRING_GUIDE structs
     */
    const std::vector<WIRING_GUIDE>& GetAllGuides() const { return m_guides; }

    /**
     * Get progress statistics.
     *
     * @param aTotal Output: total number of recommendations
     * @param aCompleted Output: number of completed connections
     */
    void GetProgress( int& aTotal, int& aCompleted ) const;

    /**
     * Check if a specific guide is pending (not approved yet).
     * Guides for symbols in the change tracker are considered pending.
     *
     * @param aSymbolId KIID of the symbol
     * @return true if the symbol is still pending approval
     */
    bool IsGuidePending( const KIID& aSymbolId ) const;

    /**
     * Called when schematic is modified.
     * Updates guide states based on new connectivity.
     */
    void OnSchematicChanged();

    /**
     * Clear all guides.
     */
    void Clear();

    /**
     * Check if there are any guides to display.
     */
    bool HasGuides() const { return !m_guides.empty(); }

    /**
     * Get the number of incomplete guides.
     */
    int GetIncompleteCount() const;

    /**
     * Refresh guide positions from current symbol locations.
     * This is a lightweight update that doesn't rescan for Agent_Wiring fields,
     * it just updates sourcePos and targetPos based on current symbol positions.
     * Call this during drag operations for real-time position tracking.
     */
    void RefreshGuidePositions();

    /**
     * Set the active wiring position.
     * When the user starts drawing a wire, call this with the start position.
     * The guide whose endpoint is closest to this position will be highlighted,
     * and all other guides will be dimmed.
     *
     * @param aPos Position where wiring started (typically a pin position)
     */
    void SetActiveWiringPosition( const VECTOR2I& aPos );

    /**
     * Clear the active wiring position.
     * Call this when wiring ends (committed or cancelled).
     * All guides will return to normal appearance.
     */
    void ClearActiveWiringPosition();

    /**
     * Check if there is an active wiring position set.
     */
    bool HasActiveWiringPosition() const { return m_hasActiveWiring; }

    /**
     * Get the index of the guide that should be highlighted based on active wiring position.
     * Returns -1 if no guide matches or no active wiring position is set.
     *
     * @param aActiveGuides The vector of active guides (from GetActiveGuides())
     * @return Index into the vector, or -1 if none should be highlighted
     */
    int GetActiveGuideIndex( const std::vector<WIRING_GUIDE>& aActiveGuides ) const;

private:
    /**
     * Parse Agent_Wiring field from a symbol and add guides.
     */
    void ParseSymbolWiring( SCH_SYMBOL* aSymbol );

    /**
     * Resolve target position for a guide.
     * For pin references (U1:PA0), finds the pin position.
     * For net names (VCC), tries to find a nearby power symbol.
     */
    bool ResolveTargetPosition( WIRING_GUIDE& aGuide );

    /**
     * Check if a connection exists between two positions using connectivity graph.
     */
    bool CheckConnectionExists( const VECTOR2I& aStart, const VECTOR2I& aEnd );

    /**
     * Find a symbol by reference designator on the current screen.
     */
    SCH_SYMBOL* FindSymbolByRef( const wxString& aRef );

    /**
     * Get pin position for a symbol.
     */
    bool GetPinPosition( SCH_SYMBOL* aSymbol, const wxString& aPin, VECTOR2I& aPos );

private:
    SCH_EDIT_FRAME*           m_frame;
    std::vector<WIRING_GUIDE> m_guides;
    bool                      m_globalVisible;

    // Active wiring position tracking (for guide highlighting)
    VECTOR2I                  m_activeWiringPos;
    bool                      m_hasActiveWiring;

    // Cache for symbol ref to KIID mapping (rebuilt on scan)
    std::map<wxString, KIID>  m_refToKiid;
};

#endif // SCH_WIRING_GUIDE_MANAGER_H
