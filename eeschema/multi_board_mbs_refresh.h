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

#ifndef MULTI_BOARD_MBS_REFRESH_H
#define MULTI_BOARD_MBS_REFRESH_H

#include <kiid.h>
#include <project/multi_board_scan.h>
#include <wx/string.h>

#include <vector>


class SCH_SCREEN;
class SCH_MODULE_BLOCK;
class SCH_MODULE_PIN;
class PROJECT_FILE;


/**
 * One planned change produced by `ComputeMbsRefreshDiff`.
 *
 * Each variant kind captures "what would differ between the current MBS
 * and a fresh scan of the sub-projects." The UI picks a subset of the
 * list (via the `checked` flag) and passes it to `ApplyMbsRefreshChanges`.
 */
struct MBS_CHANGE
{
    enum class KIND
    {
        ADD_BLOCK,     ///< A sub-project has a connector with no matching block on the MBS
        REMOVE_BLOCK,  ///< Block's (sub-project, ref) no longer exists in the scan
        ADD_PIN,       ///< Connector has a pad with no matching pin on the block
        REMOVE_PIN,    ///< Pin's pad number is no longer present on the connector
        RENAME_PIN,    ///< Pin's text (label) differs from the scan's canonical label
        PATH_DRIFT,    ///< Sub-project path changed but the UUID still matches
        UPGRADE_UUID   ///< Legacy block without a persisted UUID — stamp it
    };

    KIND     kind;
    KIID     subProjectUuid;
    wxString subProjectPath;     ///< Current (in-scan) relative path to the sub-project
    wxString subProjectDisplayName;
    wxString componentRef;       ///< Connector ref on the sub-project (e.g. "J2")
    wxString pinNumber;          ///< For pin-level kinds
    wxString oldLabel;           ///< For RENAME_PIN
    wxString newLabel;           ///< For RENAME_PIN, ADD_PIN
    wxString oldPath;            ///< For PATH_DRIFT
    wxString newPath;            ///< For PATH_DRIFT

    /// Opaque pointers — populated by the compute pass, consumed by apply.
    /// Never exposed through Describe() or the UI row labels.
    SCH_MODULE_BLOCK* existingBlock = nullptr;
    SCH_MODULE_PIN*   existingPin   = nullptr;

    /// Scan-side context for ADD_BLOCK: the full pad layout we want to
    /// lay out when creating the new block.
    std::vector<MULTI_BOARD_PAD_INFO> blockAllPads;

    /// Scan-side context for ADD_PIN: the single pad info that should
    /// drive label + number on the new pin.
    MULTI_BOARD_PAD_INFO padInfo;

    /// Dialog checkbox state. Defaults to "apply" so accepting the
    /// dialog with no tweaks runs a full refresh.
    bool checked = true;

    /// Human-readable one-line summary for the dialog row.
    wxString Describe() const;
};


/**
 * Stats returned by `ApplyMbsRefreshChanges`.
 */
struct MBS_REFRESH_RESULT
{
    int blocksAdded   = 0;
    int blocksRemoved = 0;
    int pinsAdded     = 0;
    int pinsRemoved   = 0;
    int pinsRenamed   = 0;
    int pathsUpdated  = 0;
    int uuidsStamped  = 0;
    wxString summary;
};


/**
 * Compute the set of changes required to bring the MBS screen in sync
 * with the current state of `aMultiBoard`'s sub-projects.
 *
 * Pure read — nothing on `aMbsScreen` is mutated. Call
 * `ApplyMbsRefreshChanges` with a (possibly filtered) subset of the
 * returned list to actually apply.
 */
std::vector<MBS_CHANGE> ComputeMbsRefreshDiff( SCH_SCREEN& aMbsScreen,
                                               const PROJECT_FILE& aMultiBoard );


/**
 * Apply the given (pre-filtered) list of changes to the MBS screen.
 * Changes with `checked == false` are skipped.
 */
MBS_REFRESH_RESULT ApplyMbsRefreshChanges( SCH_SCREEN& aMbsScreen,
                                           const std::vector<MBS_CHANGE>& aChanges );


/**
 * Backwards-compatible one-shot: compute every change, mark all as
 * checked, apply them. Used by headless code paths and tests that
 * don't want to spin up the UI.
 */
MBS_REFRESH_RESULT RefreshMbsFromSubProjects( SCH_SCREEN& aMbsScreen,
                                              const PROJECT_FILE& aMultiBoard );

#endif // MULTI_BOARD_MBS_REFRESH_H
