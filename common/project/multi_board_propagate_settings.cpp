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

#include <netclass.h>
#include <pgm_base.h>
#include <project.h>
#include <project/net_settings.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>

#include <wx/filename.h>

#include <set>
#include <unordered_map>


// Match the same path-comparison semantics SETTINGS_MANAGER uses when
// keying its open-projects map: case-insensitive on macOS / Windows,
// case-sensitive on Linux. Avoids "couldn't find sub-project" bugs from
// mixed-case .kicad_pro filenames.
static bool absPathsEqual( const wxString& a, const wxString& b )
{
    if( a.IsEmpty() || b.IsEmpty() )
        return false;

#if defined( __WXMSW__ ) || defined( __WXMAC__ )
    return a.IsSameAs( b, false );
#else
    return a == b;
#endif
}


// NETCLASS deletes its copy constructor, so std::make_shared<NETCLASS>(src)
// won't compile. Mirror the field set serialised by NET_SETTINGS's local
// `saveNetclass` lambda (common/project/net_settings.cpp) so a clone holds
// every field that ever round-trips through the .kicad_pro JSON.
static std::shared_ptr<NETCLASS> cloneNetclass( const NETCLASS& aSrc )
{
    auto dst = std::make_shared<NETCLASS>( aSrc.GetName(), false );

    dst->SetPriority( aSrc.GetPriority() );
    dst->SetTuningProfile( aSrc.GetTuningProfile() );
    dst->SetPcbColor( aSrc.GetPcbColor( true ) );
    dst->SetSchematicColor( aSrc.GetSchematicColor( true ) );

    if( aSrc.HasClearance() )      dst->SetClearance( aSrc.GetClearance() );
    if( aSrc.HasTrackWidth() )     dst->SetTrackWidth( aSrc.GetTrackWidth() );
    if( aSrc.HasViaDiameter() )    dst->SetViaDiameter( aSrc.GetViaDiameter() );
    if( aSrc.HasViaDrill() )       dst->SetViaDrill( aSrc.GetViaDrill() );
    if( aSrc.HasuViaDiameter() )   dst->SetuViaDiameter( aSrc.GetuViaDiameter() );
    if( aSrc.HasuViaDrill() )      dst->SetuViaDrill( aSrc.GetuViaDrill() );
    if( aSrc.HasDiffPairWidth() )  dst->SetDiffPairWidth( aSrc.GetDiffPairWidth() );
    if( aSrc.HasDiffPairGap() )    dst->SetDiffPairGap( aSrc.GetDiffPairGap() );
    if( aSrc.HasDiffPairViaGap() ) dst->SetDiffPairViaGap( aSrc.GetDiffPairViaGap() );
    if( aSrc.HasWireWidth() )      dst->SetWireWidth( aSrc.GetWireWidth() );
    if( aSrc.HasBusWidth() )       dst->SetBusWidth( aSrc.GetBusWidth() );
    if( aSrc.HasLineStyle() )      dst->SetLineStyle( aSrc.GetLineStyle() );

    return dst;
}


MULTI_BOARD_PROPAGATE_RESULT MultiBoardPropagateNetSettings(
        PROJECT&                                       aContainer,
        const MULTI_BOARD_NET_CLASS_CONFLICT_RESOLVER& aResolver )
{
    MULTI_BOARD_PROPAGATE_RESULT result;

    PROJECT_FILE& containerFile = aContainer.GetProjectFile();

    if( !containerFile.IsMultiBoardContainer() )
        return result;

    std::shared_ptr<NET_SETTINGS>& containerNS = containerFile.NetSettings();

    if( !containerNS )
        return result;

    const auto& containerClasses = containerNS->GetNetclasses();

    // Collect the container's sub-project absolute paths once. Match
    // each loaded SETTINGS_MANAGER project against this set to find the
    // sub-projects belonging to *this* container (vs sub-projects of a
    // sibling container the user might also have loaded).
    std::unordered_map<wxString, wxString> subProjAbsByPath; // absPath → display name

    for( const SUB_PROJECT_INFO& info : containerFile.GetSubProjects() )
    {
        wxFileName resolved = containerFile.ResolveSubProjectPath( info );
        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        wxString absPath = resolved.GetFullPath();

        wxString display = info.displayName.IsEmpty() ? info.name : info.displayName;
        subProjAbsByPath[absPath] = display;
    }

    if( subProjAbsByPath.empty() )
        return result;

    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

    for( const wxString& openProPath : sm.GetOpenProjects() )
    {
        wxFileName fn( openProPath );
        fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        wxString openAbs = fn.GetFullPath();

        // Find this project among container's sub-projects.
        wxString displayName;
        bool     matched = false;

        for( const auto& [subAbs, subDisplay] : subProjAbsByPath )
        {
            if( absPathsEqual( subAbs, openAbs ) )
            {
                displayName = subDisplay;
                matched     = true;
                break;
            }
        }

        if( !matched )
            continue;

        PROJECT* subProject = sm.GetProject( openProPath );

        if( !subProject )
            continue;

        PROJECT_FILE&                  subFile = subProject->GetProjectFile();
        std::shared_ptr<NET_SETTINGS>& subNS   = subFile.NetSettings();

        if( !subNS )
            continue;

        result.subProjectsTouched++;
        bool subMutated = false;

        for( const auto& [className, containerClass] : containerClasses )
        {
            if( !subNS->HasNetclass( className ) )
            {
                // Missing on sub-project — copy in silently.
                std::shared_ptr<NETCLASS> copy = cloneNetclass( *containerClass );
                subNS->SetNetclass( className, copy );
                result.classesAdded++;
                subMutated = true;
                continue;
            }

            const auto& subClassesMap = subNS->GetNetclasses();
            auto        existingIt    = subClassesMap.find( className );

            if( existingIt == subClassesMap.end() )
                continue;   // shouldn't happen given HasNetclass returned true

            const std::shared_ptr<NETCLASS>& existing = existingIt->second;

            if( containerClass && existing && *containerClass == *existing )
            {
                // Identical settings — no-op silently.
                result.classesUnchanged++;
                continue;
            }

            // Same name, different settings — conflict.
            MULTI_BOARD_NET_CLASS_CONFLICT conflict;
            conflict.subProjectDisplayName = displayName;
            conflict.subProjectAbsPath     = openAbs;
            conflict.netClassName          = className;
            conflict.containerNetClass     = containerClass;
            conflict.subProjectNetClass    = existing;

            // Defensive default when no resolver is supplied: silently
            // overwrite. Test paths and headless saves take this branch.
            MULTI_BOARD_NET_CLASS_RESOLUTION choice =
                    MULTI_BOARD_NET_CLASS_RESOLUTION::USE_CONTAINER;

            if( aResolver )
                choice = aResolver( conflict );

            switch( choice )
            {
            case MULTI_BOARD_NET_CLASS_RESOLUTION::USE_CONTAINER:
            {
                std::shared_ptr<NETCLASS> copy = cloneNetclass( *containerClass );
                subNS->SetNetclass( className, copy );
                result.classesOverwritten++;
                subMutated = true;
                break;
            }

            case MULTI_BOARD_NET_CLASS_RESOLUTION::KEEP_SUB_PROJECT:
                result.classesKept++;
                break;

            case MULTI_BOARD_NET_CLASS_RESOLUTION::SKIP:
                result.classesSkipped++;
                break;
            }
        }

        if( subMutated )
            result.mutatedSubProjectPaths.push_back( openAbs );
    }

    return result;
}

// MultiBoardPropagateNetSettingsWithDialog() lives in libcommon
// (common/dialogs/multi_board_propagate_settings_ui.cpp) because it
// depends on DIALOG_SHIM, which can't be in kicommon (layering).
