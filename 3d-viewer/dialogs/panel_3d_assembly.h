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

#include <vector>

class ASSEMBLY_3D_MANAGER;
class EDA_3D_VIEWER_FRAME;

class wxButton;
class wxCheckBox;
class wxCheckListBox;
class wxChoice;
class wxStaticText;
class wxTextCtrl;


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
    void onRunCollisionCheck( wxCommandEvent& aEvent );
    void onExportSTEP( wxCommandEvent& aEvent );
    void onShowAllBoards( wxCommandEvent& aEvent );
    void onHideAllBoards( wxCommandEvent& aEvent );
    void onTransparencyChanged( wxCommandEvent& aEvent );

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

    // Board list
    wxCheckListBox*         m_boardListBox;
    wxButton*               m_showAllButton;
    wxButton*               m_hideAllButton;

    // Layout
    wxChoice*               m_layoutModeChoice;

    // Position controls (for selected board)
    wxStaticText*           m_selectedBoardLabel;
    wxTextCtrl*             m_posXCtrl;
    wxTextCtrl*             m_posYCtrl;
    wxTextCtrl*             m_posZCtrl;
    wxTextCtrl*             m_rotXCtrl;
    wxTextCtrl*             m_rotYCtrl;
    wxTextCtrl*             m_rotZCtrl;
    wxButton*               m_resetPositionsButton;

    // Assembly options
    wxCheckBox*             m_mateConnectorsCheck;
    wxCheckBox*             m_transparentCheck;

    // Validation
    wxButton*               m_collisionCheckButton;
    wxStaticText*           m_collisionStatusLabel;

    // Export
    wxButton*               m_exportSTEPButton;

    // State
    int                     m_selectedBoardIndex;
    std::vector<KIID>       m_boardUuids;  // Maps list indices to UUIDs
};

#endif // PANEL_3D_ASSEMBLY_H
