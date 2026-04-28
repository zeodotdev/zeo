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

#include "cross_board_apply_to_board.h"

#include <board.h>
#include <footprint.h>
#include <netinfo.h>
#include <pad.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <reporter.h>

#include <wx/filename.h>

#include <map>
#include <vector>


CROSS_BOARD_APPLY_RESULT ApplyCrossBoardNetsToBoard( BOARD& aBoard, REPORTER* aReporter )
{
    CROSS_BOARD_APPLY_RESULT result;

    PROJECT* prj = aBoard.GetProject();

    if( !prj )
        return result;

    wxString proPath = prj->GetProjectFullName();

    if( proPath.IsEmpty() )
        return result;

    std::vector<MULTI_BOARD_CROSS_BOARD_BINDING> bindings =
            MultiBoardCollectCrossBoardBindingsForSubProject( wxFileName( proPath ) );

    if( bindings.empty() )
        return result;   // not a multi-board sub-project, or no bindings

    result.isMultiBoard = true;

    // Index footprints by reference for quick lookup.
    std::map<wxString, FOOTPRINT*> fpByRef;

    for( FOOTPRINT* fp : aBoard.Footprints() )
        fpByRef[fp->GetReference()] = fp;

    auto report = [&]( const wxString& aMsg, SEVERITY aSev = RPT_SEVERITY_INFO )
    {
        if( aReporter )
            aReporter->Report( aMsg, aSev );
    };

    for( const MULTI_BOARD_CROSS_BOARD_BINDING& binding : bindings )
    {
        auto fpIt = fpByRef.find( binding.componentRef );

        if( fpIt == fpByRef.end() )
        {
            // Connector declared in MBS but not present on this board.
            // M8.4 binding DRC also surfaces this; we report here as a
            // warning so the user sees it during sync too.
            result.padsMissing++;
            report( wxString::Format(
                            _( "Cross-board net '%s' references connector '%s' "
                               "but no such footprint is on this board." ),
                            binding.netName, binding.componentRef ),
                     RPT_SEVERITY_WARNING );
            continue;
        }

        FOOTPRINT* fp  = fpIt->second;
        PAD*       pad = nullptr;

        for( PAD* p : fp->Pads() )
        {
            if( p->GetNumber() == binding.pinNumber )
            {
                pad = p;
                break;
            }
        }

        if( !pad )
        {
            result.padsMissing++;
            report( wxString::Format(
                            _( "Cross-board net '%s' references pin %s/%s but the "
                               "connector has no pad with that number." ),
                            binding.netName, binding.componentRef, binding.pinNumber ),
                     RPT_SEVERITY_WARNING );
            continue;
        }

        // Find or create the target NETINFO_ITEM. If the schematic-side
        // sync hasn't introduced this net yet (e.g., the user hasn't
        // labelled a wire for it on the local schematic) we add it
        // here so the cross-board contract is preserved.
        NETINFO_ITEM* targetNet = aBoard.FindNet( binding.netName );

        if( !targetNet )
        {
            targetNet = new NETINFO_ITEM( &aBoard, binding.netName );
            aBoard.Add( targetNet );
            result.netsAdded++;
        }

        if( pad->GetNet() == targetNet )
        {
            result.padsAlreadyMatch++;
            continue;
        }

        wxString prevNet = pad->GetNetname();

        pad->SetNet( targetNet );
        result.padsUpdated++;

        if( prevNet.IsEmpty() )
        {
            report( wxString::Format(
                            _( "Assigned %s/%s to cross-board net '%s'." ),
                            binding.componentRef, binding.pinNumber, binding.netName ),
                     RPT_SEVERITY_ACTION );
        }
        else
        {
            // MBS-wins-on-conflict path. Promote to ACTION so the user
            // notices when their local label is being overridden.
            report( wxString::Format(
                            _( "Reassigned %s/%s from '%s' to MBS cross-board net '%s'." ),
                            binding.componentRef, binding.pinNumber, prevNet,
                            binding.netName ),
                     RPT_SEVERITY_ACTION );
        }
    }

    return result;
}
