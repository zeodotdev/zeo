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

#ifndef DIALOG_CREATE_PANEL_H
#define DIALOG_CREATE_PANEL_H

#include <dialog_shim.h>
#include <multi_board/panel_board.h>

#include <memory>
#include <vector>

class PCB_EDIT_FRAME;
class PROJECT;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxDataViewListCtrl;
class wxRadioButton;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;


/**
 * Entry for a board in the panel board list.
 */
struct PANEL_BOARD_ENTRY
{
    KIID        boardUuid;
    wxString    boardName;
    int         copies;
    double      rotationDeg;
    bool        mirror;

    PANEL_BOARD_ENTRY() :
            copies( 1 ),
            rotationDeg( 0.0 ),
            mirror( false )
    {}
};


/**
 * Dialog for creating a manufacturing panel from project boards.
 *
 * This dialog allows users to:
 * - Select which boards to include in the panel
 * - Specify number of copies and rotation for each board
 * - Choose layout strategy (grid, auto-optimize, custom)
 * - Configure tab type and parameters
 * - Add manufacturing features (rails, tooling holes, fiducials)
 */
class DIALOG_CREATE_PANEL : public DIALOG_SHIM
{
public:
    DIALOG_CREATE_PANEL( PCB_EDIT_FRAME* aParent );
    ~DIALOG_CREATE_PANEL();

    /**
     * Get the generated panel board.
     * @return The panel, or nullptr if creation failed
     */
    std::unique_ptr<PANEL_BOARD> GetPanel();

    /**
     * Get the panel name entered by the user.
     */
    wxString GetPanelName() const;

private:
    void createControls();
    void populateBoardList();
    void updatePreview();
    void updatePanelSizeDisplay();

    // Event handlers
    void onAddBoard( wxCommandEvent& aEvent );
    void onRemoveBoard( wxCommandEvent& aEvent );
    void onBoardSelected( wxDataViewEvent& aEvent );
    void onLayoutChanged( wxCommandEvent& aEvent );
    void onTabTypeChanged( wxCommandEvent& aEvent );
    void onSettingChanged( wxCommandEvent& aEvent );
    void onSpinChanged( wxSpinEvent& aEvent );
    void onOK( wxCommandEvent& aEvent );

    /**
     * Build the panel based on current settings.
     */
    bool buildPanel();

    /**
     * Validate all inputs.
     */
    bool validateInputs();

    PCB_EDIT_FRAME*             m_frame;
    PROJECT*                    m_project;
    std::unique_ptr<PANEL_BOARD> m_panel;

    // Board selection
    wxDataViewListCtrl*         m_boardListCtrl;
    wxButton*                   m_addBoardButton;
    wxButton*                   m_removeBoardButton;
    wxChoice*                   m_boardChoice;
    wxSpinCtrl*                 m_copiesSpin;
    wxChoice*                   m_rotationChoice;

    // Panel settings
    wxTextCtrl*                 m_panelNameCtrl;

    // Layout
    wxRadioButton*              m_gridLayoutRadio;
    wxRadioButton*              m_autoLayoutRadio;
    wxRadioButton*              m_customLayoutRadio;
    wxSpinCtrl*                 m_rowsSpin;
    wxSpinCtrl*                 m_colsSpin;
    wxTextCtrl*                 m_spacingCtrl;
    wxCheckBox*                 m_alternateRotationCheck;

    // Tabs
    wxRadioButton*              m_noTabsRadio;
    wxRadioButton*              m_mousebiteRadio;
    wxRadioButton*              m_vGrooveRadio;
    wxRadioButton*              m_solidTabsRadio;
    wxTextCtrl*                 m_tabWidthCtrl;
    wxTextCtrl*                 m_tabSpacingCtrl;
    wxTextCtrl*                 m_mousebiteHoleDiaCtrl;
    wxTextCtrl*                 m_mousebiteHoleSpacingCtrl;

    // Frame/Rails
    wxCheckBox*                 m_addRailsCheck;
    wxTextCtrl*                 m_railWidthCtrl;
    wxCheckBox*                 m_railsTopCheck;
    wxCheckBox*                 m_railsBottomCheck;
    wxCheckBox*                 m_railsLeftCheck;
    wxCheckBox*                 m_railsRightCheck;

    // Tooling
    wxCheckBox*                 m_addToolingHolesCheck;
    wxChoice*                   m_toolingPatternChoice;
    wxCheckBox*                 m_addFiducialsCheck;

    // Preview / size display
    wxStaticText*               m_panelSizeLabel;

    // Data
    std::vector<PANEL_BOARD_ENTRY> m_boardEntries;
};

#endif // DIALOG_CREATE_PANEL_H
