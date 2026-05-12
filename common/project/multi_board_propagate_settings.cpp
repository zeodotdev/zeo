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
//
// EXCEPT priority: PANEL_SETUP_NETCLASSES::TransferDataFromWindow assigns
// priority from the grid row index every time the user clicks OK, which
// means each board's priorities only make sense in that board's own grid.
// Propagating the container's priority would either fight the user's
// intentional sub-board ordering or trigger spurious "Conflict" status on
// every settings-panel open. Sub-projects keep their own priority order;
// the propagator only carries the actual netclass parameters.
static std::shared_ptr<NETCLASS> cloneNetclass( const NETCLASS& aSrc )
{
    auto dst = std::make_shared<NETCLASS>( aSrc.GetName(), false );

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


// NETCLASS::operator== only compares m_constituents — every NETCLASS
// instance pushes `this` into m_constituents in its constructor, so two
// distinct NETCLASS instances are NEVER equal regardless of their actual
// settings. That's useless for "did container's Heavy diverge from
// sub-project's Heavy?". Compare the user-meaningful fields directly —
// the same set we round-trip through JSON in cloneNetclass.
bool MultiBoardNetclassesEquivalent( const NETCLASS& a, const NETCLASS& b )
{
    if( a.GetName() != b.GetName() )                       return false;

    // Priority deliberately NOT compared. PANEL_SETUP_NETCLASSES rewrites
    // priority from grid row index on every OK, so a sub-board's priority
    // for a given class is whatever row the user has it at — completely
    // unrelated to the container's row. Comparing priority would surface
    // every cross-board class as "Conflict" the moment either panel was
    // OK'd, even when the actual netclass parameters are identical. See
    // also `cloneNetclass()` above for the matching omission.
    if( a.GetTuningProfile() != b.GetTuningProfile() )     return false;

    // Optional fields: equivalent when both are absent OR both are present
    // with the same value.
    auto sameOpt = [&]( const std::optional<int>& x, const std::optional<int>& y )
    { return x == y; };

    if( !sameOpt( a.GetClearanceOpt(),     b.GetClearanceOpt() ) )     return false;
    if( !sameOpt( a.GetTrackWidthOpt(),    b.GetTrackWidthOpt() ) )    return false;
    if( !sameOpt( a.GetViaDiameterOpt(),   b.GetViaDiameterOpt() ) )   return false;
    if( !sameOpt( a.GetViaDrillOpt(),      b.GetViaDrillOpt() ) )      return false;
    if( !sameOpt( a.GetuViaDiameterOpt(),  b.GetuViaDiameterOpt() ) )  return false;
    if( !sameOpt( a.GetuViaDrillOpt(),     b.GetuViaDrillOpt() ) )     return false;
    if( !sameOpt( a.GetDiffPairWidthOpt(), b.GetDiffPairWidthOpt() ) ) return false;
    if( !sameOpt( a.GetDiffPairGapOpt(),   b.GetDiffPairGapOpt() ) )   return false;
    if( !sameOpt( a.GetDiffPairViaGapOpt(), b.GetDiffPairViaGapOpt() ) ) return false;
    if( !sameOpt( a.GetWireWidthOpt(),     b.GetWireWidthOpt() ) )     return false;
    if( !sameOpt( a.GetBusWidthOpt(),      b.GetBusWidthOpt() ) )      return false;

    if( a.HasLineStyle() != b.HasLineStyle() )              return false;
    if( a.HasLineStyle() && a.GetLineStyle() != b.GetLineStyle() )
        return false;

    if( a.GetPcbColor( true ) != b.GetPcbColor( true ) )            return false;
    if( a.GetSchematicColor( true ) != b.GetSchematicColor( true ) ) return false;

    return true;
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

    // Build a normalized lookup of currently-open projects so we can
    // distinguish "already loaded" (mutate in place) from "disk-only"
    // (load ephemerally, mutate, caller must unload after save). Earlier
    // versions of this propagator only iterated open projects, which
    // meant saving the container with only MBSCH open silently skipped
    // every disk-only sub-project — and the new netclass never landed on
    // disk for them. Now we drive the iteration off the container's
    // sub-project list directly and load any that aren't already open.
    std::set<wxString> openAbsPaths;

    for( const wxString& openProPath : sm.GetOpenProjects() )
    {
        wxFileName fn( openProPath );
        fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        openAbsPaths.insert( fn.GetFullPath() );
    }

    auto isOpen = [&]( const wxString& aAbs ) -> bool
    {
        for( const wxString& p : openAbsPaths )
            if( absPathsEqual( p, aAbs ) )
                return true;
        return false;
    };

    for( const auto& [openAbs, displayName] : subProjAbsByPath )
    {
        bool wasOpen = isOpen( openAbs );

        if( !wasOpen )
        {
            // Load read-only into SETTINGS_MANAGER without changing the
            // active project (aSetActive=false skips cwd/env mutation,
            // library manager notifications, and active-project eviction).
            // We track it in ephemerallyLoadedSubProjectPaths so the
            // wrapper can save+unload after propagation.
            if( !sm.LoadProject( openAbs, /* aSetActive */ false ) )
            {
                wxLogWarning( wxT( "[M7.2-PROPAGATE] sm.LoadProject('%s') failed — "
                                   "skipping disk-only sub-project; container's "
                                   "net classes will NOT propagate to it on this save" ),
                              openAbs );
                continue;
            }
        }

        PROJECT* subProject = sm.GetProject( openAbs );

        if( !subProject )
            continue;

        if( !wasOpen )
            result.ephemerallyLoadedSubProjectPaths.push_back( openAbs );

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

            if( containerClass && existing
                && MultiBoardNetclassesEquivalent( *containerClass, *existing ) )
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


MULTI_BOARD_NETCLASS_VIEW BuildMultiBoardNetclassView( PROJECT_FILE& aContainer )
{
    MULTI_BOARD_NETCLASS_VIEW view;

    if( !aContainer.IsMultiBoardContainer() )
        return view;

    std::shared_ptr<NET_SETTINGS> containerNS = aContainer.NetSettings();

    if( !containerNS )
        return view;

    // Load every sub-project's NetSettings via a transient PROJECT_FILE.
    // Transients are scoped to this function: any sub-project class we need
    // to keep (the LOCAL_ONLY rows) is cloned before the transient dies.
    struct SubProjectAccess
    {
        wxString                       displayName;
        std::unique_ptr<PROJECT_FILE>  transient;
        std::shared_ptr<NET_SETTINGS>  netSettings;
    };

    std::vector<SubProjectAccess> subProjects;
    subProjects.reserve( aContainer.GetSubProjects().size() );

    for( const SUB_PROJECT_INFO& info : aContainer.GetSubProjects() )
    {
        wxFileName resolved = aContainer.ResolveSubProjectPath( info );
        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        wxString absPath = resolved.GetFullPath();

        SubProjectAccess access;
        access.displayName = info.displayName.IsEmpty() ? info.name : info.displayName;
        access.transient   = std::make_unique<PROJECT_FILE>( absPath );

        wxFileName fn( absPath );

        if( !access.transient->LoadFromFile( fn.GetPath() ) )
        {
            wxLogWarning( wxT( "BuildMultiBoardNetclassView: failed to load '%s' — "
                               "sub-project skipped from aggregate view" ),
                          absPath );
            continue;
        }

        access.netSettings = access.transient->NetSettings();

        if( !access.netSettings )
            continue;

        subProjects.push_back( std::move( access ) );
    }

    auto classifyContainerClass = [&]( const NETCLASS& aContainerNc )
            -> MULTI_BOARD_NETCLASS_VIEW_STATUS
    {
        const wxString& name = aContainerNc.GetName();
        bool seenInSub    = false;
        bool conflictSeen = false;

        for( const SubProjectAccess& sp : subProjects )
        {
            std::shared_ptr<NETCLASS> subNc;

            if( aContainerNc.IsDefault() )
            {
                subNc = sp.netSettings->GetDefaultNetclass();
            }
            else
            {
                const auto& subClasses = sp.netSettings->GetNetclasses();
                auto        it         = subClasses.find( name );

                if( it != subClasses.end() )
                    subNc = it->second;
            }

            if( !subNc )
                continue;

            seenInSub = true;

            if( !MultiBoardNetclassesEquivalent( aContainerNc, *subNc ) )
            {
                conflictSeen = true;
                break;
            }
        }

        if( !seenInSub )
            return MULTI_BOARD_NETCLASS_VIEW_STATUS::SOURCE;

        if( conflictSeen )
            return MULTI_BOARD_NETCLASS_VIEW_STATUS::CONFLICT;

        return MULTI_BOARD_NETCLASS_VIEW_STATUS::SHARED;
    };

    if( const std::shared_ptr<NETCLASS>& def = containerNS->GetDefaultNetclass() )
        view.containerStatusByName[def->GetName()] = classifyContainerClass( *def );

    for( const auto& [name, nc] : containerNS->GetNetclasses() )
    {
        if( nc )
            view.containerStatusByName[name] = classifyContainerClass( *nc );
    }

    // Collect classes that live ONLY on sub-projects (not on container).
    // One row per (sub-project, class) occurrence — if the same name shows
    // up on multiple sub-boards without being adopted into the container,
    // each board's copy gets its own row so the user can compare.
    const auto& containerClasses = containerNS->GetNetclasses();

    for( const SubProjectAccess& sp : subProjects )
    {
        for( const auto& [name, nc] : sp.netSettings->GetNetclasses() )
        {
            if( !nc )
                continue;

            if( containerClasses.find( name ) != containerClasses.end() )
                continue;

            MULTI_BOARD_NETCLASS_LOCAL_ROW row;
            row.netclass              = cloneNetclass( *nc );
            row.subProjectDisplayName = sp.displayName;
            view.localOnlyRows.push_back( std::move( row ) );
        }
    }

    return view;
}
