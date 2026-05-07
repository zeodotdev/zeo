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

#ifndef PANEL_3D_ASSEMBLY_H
#define PANEL_3D_ASSEMBLY_H

#include <wx/panel.h>
#include <kiid.h>

#include <map>
#include <vector>

class ASSEMBLY_3D_MANAGER;
class EDA_3D_VIEWER_FRAME;

class wxButton;
class wxCheckBox;
class wxCheckListBox;
class wxChoice;
class wxListCtrl;
class wxListEvent;
class wxScrolledWindow;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxTreeEvent;


/**
 * Panel for controlling multi-board assembly view in the 3D viewer.
 *
 * Provides:
 * - Board list with visibility toggles
 * - Position/rotation controls for selected board
 * - Layout presets (flat, stacked, custom)
 * - Connector mating toggle
 * - Collision detection
 * - Assembly STEP export
 */
class PANEL_3D_ASSEMBLY : public wxPanel
{
public:
    PANEL_3D_ASSEMBLY( EDA_3D_VIEWER_FRAME* aParent, ASSEMBLY_3D_MANAGER* aManager );
    ~PANEL_3D_ASSEMBLY();

    /**
     * Refresh the board list from the assembly manager.
     */
    void RefreshBoardList();

    /**
     * Update controls to reflect selected board's state.
     */
    void UpdateSelectedBoardControls();

    /**
     * Get the currently selected board UUID.
     */
    KIID GetSelectedBoardUuid() const;

private:
    void createControls();
    void bindEvents();

    // Event handlers
    void onBoardSelected( wxCommandEvent& aEvent );
    void onBoardVisibilityChanged( wxCommandEvent& aEvent );
    void onLayoutModeChanged( wxCommandEvent& aEvent );
    void onPositionChanged( wxCommandEvent& aEvent );
    void onRotationChanged( wxCommandEvent& aEvent );
    void onResetPositions( wxCommandEvent& aEvent );
    void onMateConnectors( wxCommandEvent& aEvent );
    void onExportSTEP( wxCommandEvent& aEvent );
    void onShowAllBoards( wxCommandEvent& aEvent );
    void onHideAllBoards( wxCommandEvent& aEvent );
    void onTransparencyChanged( wxCommandEvent& aEvent );

    // M6.D-phase-2 custom mate handlers
    void onAddCustomMate( wxCommandEvent& aEvent );
    void onEditCustomMate( wxCommandEvent& aEvent );
    void onMoveMateUp( wxCommandEvent& aEvent );
    void onMoveMateDown( wxCommandEvent& aEvent );
    void onDisableMate( wxCommandEvent& aEvent );
    void onDeleteCustomMate( wxCommandEvent& aEvent );
    void onMateTreeSelectionChanged( wxTreeEvent& aEvent );
    void onMateTreeActivated( wxTreeEvent& aEvent );    ///< double-click → edit
    void onShowMatesToggled( wxCommandEvent& aEvent );
    void onShowPinPairsToggled( wxCommandEvent& aEvent );
    void onShowCollisionsToggled( wxCommandEvent& aEvent );
    void onShowContactsToggled( wxCommandEvent& aEvent );
    void onCollisionThresholdChanged( wxCommandEvent& aEvent );

    /**
     * Position section's own board picker — independent of the
     * Boards visibility list. Switching the picker just repaints
     * the X/Y/Z + rotation fields with that board's current state.
     */
    void onPositionBoardChoice( wxCommandEvent& aEvent );

    /**
     * Run collision detection and refresh the gizmo + status label.
     * Called from every position/rotation/visibility change so the
     * red collision markers stay live without a manual button.
     */
    void autoRunCollisionCheck();

    /**
     * Rebuild the mates tree from the manager's current mate graph
     * + custom mates list. Called after any mutation.
     */
    void RefreshMatesTree();

    /**
     * Update the per-row action buttons (Move up / Move down / Disable
     * / Delete) based on the currently-selected mate row.
     */
    void updateMateButtons();

    /**
     * Re-select the mate-tree row whose canonical pair-id matches
     * `aPairId`. Called after RefreshMatesTree (which clears the
     * selection) so the user keeps focus on the row they just acted
     * on. No-op if the row no longer exists.
     */
    void selectMatePairRow( const wxString& aPairId );

    /**
     * Update the 3D view after a change.
     */
    void refresh3DView();

    /**
     * Update position text controls from selected board.
     */
    void updatePositionControls();

    /**
     * Update collision status display.
     */
    void updateCollisionStatus();

    EDA_3D_VIEWER_FRAME*    m_frame;
    ASSEMBLY_3D_MANAGER*    m_manager;

    // Scrolled inner panel hosting all sections (so the panel scrolls
    // when the AUI pane is shorter than the section column).
    wxScrolledWindow*       m_scrolled;

    // Board list (visibility only; selection for Position is now a
    // separate wxChoice in the Position section).
    wxCheckListBox*         m_boardListBox;
    wxButton*               m_showAllButton;
    wxButton*               m_hideAllButton;

    // Layout
    wxChoice*               m_layoutModeChoice;

    // Position controls — own board picker, decoupled from the Boards
    // visibility list.
    wxChoice*               m_positionBoardChoice;
    wxTextCtrl*             m_posXCtrl;
    wxTextCtrl*             m_posYCtrl;
    wxTextCtrl*             m_posZCtrl;
    wxTextCtrl*             m_rotXCtrl;
    wxTextCtrl*             m_rotYCtrl;
    wxTextCtrl*             m_rotZCtrl;
    wxButton*               m_resetPositionsButton;
    wxCheckBox*             m_transparentCheck;

    // M6.D-phase-2 mates UI. "Mate connectors" toggle moved here so
    // the mate-related controls live together. View toggles below
    // (mate gizmos / collision / contact highlights) are independent
    // of the mate solver state.
    wxCheckBox*             m_mateConnectorsCheck;
    wxTreeCtrl*             m_matesTree;
    wxButton*               m_addMateButton;
    wxButton*               m_editMateButton;
    wxButton*               m_moveMateUpButton;
    wxButton*               m_moveMateDownButton;
    wxButton*               m_disableMateButton;
    wxButton*               m_deleteMateButton;

    // View / overlay toggles. Independent of the underlying solver
    // and collision state — controls *what* the gizmo pass renders.
    wxCheckBox*             m_showMatesCheck;
    wxCheckBox*             m_showPinPairsCheck;
    wxCheckBox*             m_showCollisionsCheck;
    wxCheckBox*             m_showContactsCheck;
    wxTextCtrl*             m_collisionThresholdCtrl;

    // Validation. Auto-runs on every position/visibility change; the
    // status label reflects the latest result.
    wxStaticText*           m_collisionStatusLabel;

    // Export
    wxButton*               m_exportSTEPButton;

    /// Per-row metadata attached to mate-tree leaves so the action
    /// buttons can map a tree selection back to the underlying
    /// MATE_PAIR / CUSTOM_MATE.
    struct MATE_TREE_ROW
    {
        bool      isEdgeNode;        ///< true for parent rows (board edges)
        bool      isAuto;            ///< auto-derived (no customMateUuid)
        KIID      customMateUuid;    ///< null when isAuto
        KIID      instanceA;
        KIID      instanceB;
        wxString  footprintRefA;
        wxString  footprintRefB;
    };

    /// Map wxTreeItemIdValue → row metadata. Rebuilt by RefreshMatesTree.
    std::map<void*, MATE_TREE_ROW> m_mateTreeRows;

    // State
    int                     m_selectedBoardIndex;
    std::vector<KIID>       m_boardUuids;  // Maps list indices to UUIDs
};

#endif // PANEL_3D_ASSEMBLY_H
