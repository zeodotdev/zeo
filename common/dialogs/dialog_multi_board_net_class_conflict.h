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

#ifndef DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT_H
#define DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT_H

#include <dialog_shim.h>
#include <project/multi_board_propagate_settings.h>


class wxCheckBox;


/**
 * Modal conflict-resolution dialog shown by `MultiBoardPropagateNetSettings`
 * when a sub-project has a net class with the same name as the container
 * but with different settings. One dialog per conflict — the "Apply choice
 * to all remaining conflicts" checkbox lets the user batch the rest after
 * they've inspected one.
 *
 * Returns the user's choice via `GetResolution()`. `ShouldApplyToAll()`
 * tells the caller whether to skip the dialog for subsequent conflicts in
 * the same propagation pass and reuse this resolution.
 *
 * Construct on demand inside the propagator's resolver lambda — the
 * dialog has no project state of its own; it's a pure presenter.
 */
class DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT : public DIALOG_SHIM
{
public:
    DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT( wxWindow* aParent,
                                           const MULTI_BOARD_NET_CLASS_CONFLICT& aConflict );

    ~DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT() override = default;

    MULTI_BOARD_NET_CLASS_RESOLUTION GetResolution() const { return m_resolution; }

    bool ShouldApplyToAll() const { return m_applyToAll; }

private:
    /// One readable text block for a single side of the comparison.
    /// Renders the subset of NETCLASS fields that NET_SETTINGS round-trips
    /// to JSON (clearance, track width, via diameter, …) so the user sees
    /// the same fields they edited.
    wxString formatNetclassDescription( const std::shared_ptr<class NETCLASS>& aNc ) const;

    void onUseContainer( wxCommandEvent& aEvent );
    void onKeepSubProject( wxCommandEvent& aEvent );
    void onSkip( wxCommandEvent& aEvent );
    void onMerge( wxCommandEvent& aEvent );

    wxCheckBox*                      m_applyToAllCheckbox = nullptr;
    MULTI_BOARD_NET_CLASS_RESOLUTION m_resolution = MULTI_BOARD_NET_CLASS_RESOLUTION::USE_CONTAINER;
    bool                             m_applyToAll = false;
};


#endif // DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT_H
