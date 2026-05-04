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

#include <project/multi_board_propagate_settings.h>

#include "dialog_multi_board_net_class_conflict.h"

#include <pgm_base.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>

#include <wx/filename.h>
#include <wx/window.h>

#include <optional>


// Why this file lives in libcommon (not kicommon, where the rest of
// the propagator code lives): DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT
// derives from DIALOG_SHIM, and DIALOG_SHIM lives in libcommon. kicommon
// can't depend on libcommon (libcommon depends on kicommon, not the
// reverse), so the dialog-aware wrapper has to be on the libcommon side
// of that boundary. The headless propagator stays in kicommon for use
// by IPC handlers, CLI, and tests.
MULTI_BOARD_PROPAGATE_RESULT MultiBoardPropagateNetSettingsWithDialog(
        PROJECT& aContainer, wxWindow* aDialogParent )
{
    // Sticky resolution captured by the "Apply to all remaining" checkbox.
    // std::optional empty until the user ticks the box; when present, the
    // resolver short-circuits the dialog for every subsequent conflict in
    // this propagation pass.
    std::optional<MULTI_BOARD_NET_CLASS_RESOLUTION> sticky;

    auto resolver = [&]( const MULTI_BOARD_NET_CLASS_CONFLICT& aConflict )
            -> MULTI_BOARD_NET_CLASS_RESOLUTION
    {
        if( sticky.has_value() )
            return *sticky;

        DIALOG_MULTI_BOARD_NET_CLASS_CONFLICT dlg( aDialogParent, aConflict );
        dlg.ShowModal();

        MULTI_BOARD_NET_CLASS_RESOLUTION choice = dlg.GetResolution();

        if( dlg.ShouldApplyToAll() )
            sticky = choice;

        return choice;
    };

    MULTI_BOARD_PROPAGATE_RESULT result =
            MultiBoardPropagateNetSettings( aContainer, resolver );

    // Flush each mutated sub-project to disk. The propagator only mutates
    // in-memory NET_SETTINGS; persistence is the caller's job. Wrapping
    // each save in PROJECT_FILE_SUSPEND_NOTIFY would coalesce observers,
    // but we don't have that fan-out for net_settings yet — the per-save
    // cost is one .kicad_pro JSON write per affected sub-project, and we
    // expect the count to be small (one per peer window).
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

    for( const wxString& subPath : result.mutatedSubProjectPaths )
    {
        PROJECT* sub = sm.GetProject( subPath );

        if( !sub )
        {
            wxLogWarning( wxT( "[M7.2-PROPAGATE] sm.GetProject('%s') returned null — "
                               "skipping save for this sub-project" ), subPath );
            continue;
        }

        // Derive the save directory from the PROJECT itself, then sanity-check
        // it's absolute. If the project's full name was set with a basename
        // (a known legacy gotcha — see PROJECT::setProjectFullName defensive
        // recovery), the derived path can come back relative or empty, which
        // makes JSON_SETTINGS::SaveToFile write to the current working
        // directory — typically the container's directory, producing stray
        // <sub-project>.kicad_pro files at the project root. Refuse to save
        // in that case.
        wxFileName subDirFn( sub->GetProjectFullName() );
        subDirFn.MakeAbsolute();
        wxString subDir = subDirFn.GetPath();

        wxLogMessage( wxT( "[M7.2-PROPAGATE] saving sub-project: subPath='%s' "
                           "projGetFullName='%s' projGetPath='%s' resolvedDir='%s' "
                           "fileGetFilename='%s'" ),
                      subPath, sub->GetProjectFullName(), sub->GetProjectPath(),
                      subDir, sub->GetProjectFile().GetFilename() );

        if( subDir.IsEmpty() || !wxFileName::DirExists( subDir ) )
        {
            wxLogWarning( wxT( "[M7.2-PROPAGATE] derived subDir='%s' is empty or "
                               "doesn't exist — refusing to save '%s' (would create "
                               "stray file at cwd)" ), subDir, subPath );
            continue;
        }

        sub->GetProjectFile().SaveToFile( subDir, /* aForce */ true );
    }

    // Unload sub-projects the propagator loaded itself. We pass aSave=false
    // because the explicit loop above already wrote any mutated state — and
    // un-mutated ephemerals shouldn't write anything (they were just opened
    // for inspection). Failing to unload would leave them lingering as
    // phantom open projects in SETTINGS_MANAGER, which messes up Prj() and
    // the active-project switch on the next user-driven open.
    for( const wxString& ephemeralPath : result.ephemerallyLoadedSubProjectPaths )
    {
        PROJECT* ephemeral = sm.GetProject( ephemeralPath );

        if( !ephemeral )
            continue;

        sm.UnloadProject( ephemeral, /* aSave */ false );
    }

    return result;
}
