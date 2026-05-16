/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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
    void onMateConnectors( wxCommandEvent& aEvent );
    void onExportSTEP( wxCommandEvent& aEvent );
    void onShowAllBoards( wxCommandEvent& aEvent );
    void onHideAllBoards( wxCommandEvent& aEvent );

    // M6.D-phase-2 custom mate handlers
    void onAddCustomMate( wxCommandEvent& aEvent );
    void onEditCustomMate( wxCommandEvent& aEvent );
    void onMoveMateUp( wxCommandEvent& aEvent );
    void onMoveMateDown( wxCommandEvent& aEvent );
    void onDisableMate( wxCommandEvent& aEvent );
    void onDeleteCustomMate( wxCommandEvent& aEvent );
    void onMateTreeSelectionChanged( wxTreeEvent& aEvent );
    void onMateTreeActivated( wxTreeEvent& aEvent );    ///< double-click → edit

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

    /// Latched once we've successfully run autoRunCollisionCheck
    /// against ready instance adapters. Gates the wxIdle one-shot
    /// hook in the constructor so it fires exactly once.
    bool m_initialCollisionCheckDone = false;

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

    // M6.D-phase-2 mates UI. "Mate connectors" toggle moved here so
    // the mate-related controls live together. View toggles below
    // (mate gizmos / collision / contact highlights) are independent
    // of the mate solver state.
    wxButton*               m_mateConnectorsButton;
    wxTreeCtrl*             m_matesTree;
    wxButton*               m_addMateButton;
    wxButton*               m_editMateButton;
    wxButton*               m_moveMateUpButton;
    wxButton*               m_moveMateDownButton;
    wxButton*               m_disableMateButton;
    wxButton*               m_deleteMateButton;

    // View / overlay toggles. Independent of the underlying solver
    // and collision state — controls *what* the gizmo pass renders.
    // Mate-rendering controls moved to RHS APPEARANCE_CONTROLS_3D's
    // "Mate Rendering" section in MBS mode. The members below are
    // kept as nullptr so any leftover access guarded by null-checks
    // doesn't crash; they're never instantiated.
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
        bool      isEdgeNode = false;  ///< true for parent rows (board edges)
        bool      isAuto = false;      ///< auto-derived (no customMateUuid)
        bool      disabled = false;    ///< suppressed by a CUSTOM_MATE override
        KIID      customMateUuid = KIID( 0 );  ///< null when isAuto AND not disabled
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
