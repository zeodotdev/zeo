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

#ifndef DIALOG_MBS_SYNC_PCB_H
#define DIALOG_MBS_SYNC_PCB_H

#include <dialog_shim.h>
#include <project/cross_board_pcb_sync.h>

#include <functional>

class wxButton;
class wxStaticText;
class WX_HTML_REPORT_PANEL;
class SCH_EDIT_FRAME;


/**
 * "Sync Cross-Board Nets to Sub-Project PCBs" preview + apply dialog.
 *
 * Layout mirrors `DIALOG_MBS_REFRESH`:
 *   - Top: a one-line summary derived from the dry-run preview.
 *   - Middle: `WX_HTML_REPORT_PANEL` listing every pending change
 *     grouped by sub-project. Bulk net renames render as warnings
 *     because they're whole-file string replaces in `.kicad_pcb` —
 *     they could touch unrelated pads if the local net name happens
 *     to appear elsewhere in the file, so the user must visually
 *     confirm the blast radius before committing.
 *   - Bottom: Apply / Cancel.
 *
 * The dialog is constructed with a precomputed dry-run result and an
 * apply callback. On Apply the callback runs (live mutation, file
 * writes, container save), the result lines are streamed into the
 * report panel, and Apply morphs into Close.
 *
 * Returns `wxID_OK` once an apply has run; `wxID_CANCEL` if the user
 * closed without committing.
 */
class DIALOG_MBS_SYNC_PCB : public DIALOG_SHIM
{
public:
    /// Runs the live (non-dry-run) sync, saves the container, and returns
    /// the post-apply result. Provided by the caller so the dialog stays
    /// agnostic of project-resolution mechanics (the call site already
    /// resolves the container dir via SETTINGS_MANAGER).
    using ApplyCallback = std::function<MB_CROSS_BOARD_SYNC_RESULT()>;

    DIALOG_MBS_SYNC_PCB( SCH_EDIT_FRAME* aFrame,
                         const MB_CROSS_BOARD_SYNC_RESULT& aPreview,
                         ApplyCallback aApply );

    ~DIALOG_MBS_SYNC_PCB() override = default;

    /// Apply outcome (valid only when an apply ran and `ShowModal` returned `wxID_OK`).
    const MB_CROSS_BOARD_SYNC_RESULT& GetResult() const { return m_result; }

private:
    void buildUI();

    /// Stream a vector of preview lines into the report panel, grouping
    /// consecutive lines by sub-project name with a section header.
    void renderLines( const std::vector<MB_SYNC_PREVIEW_LINE>& aLines );

    void onApply( wxCommandEvent& aEvent );

    SCH_EDIT_FRAME*            m_frame;
    MB_CROSS_BOARD_SYNC_RESULT m_preview;
    MB_CROSS_BOARD_SYNC_RESULT m_result;
    ApplyCallback              m_applyFn;
    bool                       m_applied;

    wxStaticText*         m_summaryText;
    WX_HTML_REPORT_PANEL* m_messagePanel;
    wxButton*             m_applyButton;
    wxButton*             m_cancelButton;
};

#endif // DIALOG_MBS_SYNC_PCB_H
