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
 */

#ifndef PANEL_SETUP_CROSS_BOARD_RULES_H
#define PANEL_SETUP_CROSS_BOARD_RULES_H

#include <kicommon.h>

#include <wx/panel.h>

#include <map>


class PROJECT_FILE;
class wxButton;
class wxGrid;
class wxNotebook;


/**
 * Setup-dialog panel for cross-board ERC/DRC rules persisted on a
 * multi-board container `.kicad_pro` under `multi_board.*`.
 *
 * Reuses the same five-tab notebook layout as the standalone
 * DIALOG_MULTI_BOARD_RULES (min power pins / max length / diff pairs /
 * current / voltage drop). Embeds cleanly inside DIALOG_SCHEMATIC_SETUP
 * via PAGED_DIALOG's standard TransferDataToWindow / TransferDataFromWindow
 * lifecycle. When opened on a non-multi-board project, the panel is
 * harmless: populate sees empty rule maps and apply writes back empty
 * maps, leaving the project untouched.
 */
class KICOMMON_API PANEL_SETUP_CROSS_BOARD_RULES : public wxPanel
{
public:
    /**
     * @param aParent  parent window (typically the treebook page placeholder).
     * @param aProject the container PROJECT_FILE to edit. Must be non-null.
     */
    PANEL_SETUP_CROSS_BOARD_RULES( wxWindow* aParent, PROJECT_FILE* aProject );

    ~PANEL_SETUP_CROSS_BOARD_RULES() override;

    /// Pull current rule maps off the project and populate each grid.
    bool TransferDataToWindow() override;

    /// Validate every grid; on success commit through the T3 setters
    /// wrapped in PROJECT_FILE_SUSPEND_NOTIFY (one observer event per
    /// rule type). Does NOT call SaveToFile — caller (the parent
    /// dialog) is responsible for persistence.
    bool TransferDataFromWindow() override;

private:
    void buildUI();
    void buildMinPowerPinsTab( wxNotebook* aNotebook );
    void buildMaxLengthTab( wxNotebook* aNotebook );
    void buildDiffPairsTab( wxNotebook* aNotebook );
    void buildCurrentTab( wxNotebook* aNotebook );
    void buildVoltageTab( wxNotebook* aNotebook );

    wxGrid* buildGridPanel( wxWindow* aParent, const wxArrayString& aColumnLabels,
                            const wxArrayInt& aColumnWidths );

    void onAddRow( wxCommandEvent& aEvent );
    void onRemoveSelected( wxCommandEvent& aEvent );

private:
    PROJECT_FILE* m_project;

    wxGrid* m_minPowerGrid;
    wxGrid* m_maxLengthGrid;
    wxGrid* m_diffPairGrid;
    wxGrid* m_currentGrid;
    wxGrid* m_voltageGrid;

    /// Add Row / Remove Selected buttons on each tab share one handler
    /// via this id → grid map.
    std::map<int, wxGrid*> m_buttonToGrid;
};


#endif // PANEL_SETUP_CROSS_BOARD_RULES_H
