/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#ifndef PANEL_SETUP_IMPEDANCE_H
#define PANEL_SETUP_IMPEDANCE_H

#include <wx/panel.h>

class PCB_EDIT_FRAME;
class BOARD;
class BOARD_DESIGN_SETTINGS;
class wxTextCtrl;
class wxChoice;


/**
 * Board Setup → Board Stackup → Impedance.
 *
 * Edits the board-level signal-integrity analysis parameters that feed the impedance /
 * transmission-line calculation (BOARD_DESIGN_SETTINGS::m_SI_*).  This is the KiCad analog
 * of Altium's Layer Stack Manager "Impedance" tab: a single board-wide reference frequency
 * plus conductor material properties.  Per-net control of the impedance *target* lives with
 * the impedance_target DRC rule / tuning profiles, not here.
 *
 * Hand-coded (no wxFormBuilder base) — the control set is small and likely to grow.
 */
class PANEL_SETUP_IMPEDANCE : public wxPanel
{
public:
    PANEL_SETUP_IMPEDANCE( wxWindow* aParentWindow, PCB_EDIT_FRAME* aFrame );
    ~PANEL_SETUP_IMPEDANCE() override = default;

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void ImportSettingsFrom( BOARD* aBoard );

private:
    /// Push the four m_SI_* values into the controls (frequency unit auto-chosen as GHz).
    void loadFromSettings( const BOARD_DESIGN_SETTINGS& aBds );

    PCB_EDIT_FRAME*        m_frame;
    BOARD_DESIGN_SETTINGS* m_bds;

    wxTextCtrl* m_refFreqCtrl    = nullptr;
    wxChoice*   m_refFreqUnit    = nullptr;
    wxTextCtrl* m_dkFreqCtrl     = nullptr;
    wxChoice*   m_dkFreqUnit     = nullptr;
    wxTextCtrl* m_resistivityCtrl = nullptr;   // µΩ·cm
    wxTextCtrl* m_roughnessCtrl   = nullptr;   // µm
};

#endif  // PANEL_SETUP_IMPEDANCE_H
