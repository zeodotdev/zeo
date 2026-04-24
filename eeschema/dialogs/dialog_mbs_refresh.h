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

#ifndef DIALOG_MBS_REFRESH_H
#define DIALOG_MBS_REFRESH_H

#include <dialog_shim.h>
#include <multi_board_mbs_refresh.h>

#include <vector>

class wxButton;
class wxCheckListBox;
class wxStaticText;


/**
 * Diff preview for "Refresh Module Blocks from Sub-Projects".
 *
 * Constructs directly from a `std::vector<MBS_CHANGE>` — the caller has
 * already run `ComputeMbsRefreshDiff` against the current MBS. The
 * dialog mutates the vector's `checked` flags in place when the user
 * toggles rows; the caller then passes the same vector to
 * `ApplyMbsRefreshChanges` on accept.
 *
 * Empty-diff case: show an informational message and auto-dismiss.
 */
class DIALOG_MBS_REFRESH : public DIALOG_SHIM
{
public:
    DIALOG_MBS_REFRESH( wxWindow* aParent, std::vector<MBS_CHANGE>& aChanges );
    ~DIALOG_MBS_REFRESH() override = default;

private:
    void buildUI();
    void refreshCounts();

    void onCheckAll( wxCommandEvent& aEvent );
    void onCheckNone( wxCommandEvent& aEvent );
    void onItemToggled( wxCommandEvent& aEvent );
    void onApply( wxCommandEvent& aEvent );

    std::vector<MBS_CHANGE>& m_changes;

    wxCheckListBox* m_list;
    wxStaticText*   m_countsLabel;
    wxButton*       m_checkAllButton;
    wxButton*       m_checkNoneButton;
    wxButton*       m_applyButton;
    wxButton*       m_cancelButton;
};

#endif // DIALOG_MBS_REFRESH_H
