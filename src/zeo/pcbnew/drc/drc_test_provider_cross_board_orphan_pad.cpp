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

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <project.h>
#include <project/multi_board_scan.h>

#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>

#include <wx/filename.h>

#include <set>
#include <utility>


/*
    Cross-board orphan connector pad test provider.

    Fires only when the active project is part of a multi-board container.
    Iterates pads that the user has explicitly marked as connector pads
    (BOARD::GetConnectorPads) and verifies each is referenced by at least
    one cross-board net endpoint declared on this sub-project.

    Inverse of the M8.4 binding check: M8.4 iterates MBS bindings and
    verifies the local pads exist with the right net. This check iterates
    the local connector pads and verifies the MBS knows about them. Catches
    the "marked the pad as connector but never refreshed the MBS / never
    wired the pin" workflow gap.

    Errors generated:
    - DRCE_CROSS_BOARD_ORPHAN_PAD — connector pad not part of any cross-board net
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_ORPHAN_PAD : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_ORPHAN_PAD() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_ORPHAN_PAD() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_orphan_pad" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_ORPHAN_PAD::Run()
{
    BOARD* board = m_drcEngine->GetBoard();

    if( !board )
        return true;

    PROJECT* prj = board->GetProject();

    if( !prj )
        return true;

    wxString proPath = prj->GetProjectFullName();

    if( proPath.IsEmpty() )
        return true;

    const std::set<KIID>& connectorPads = board->GetConnectorPads();

    if( connectorPads.empty() )
        return true;   // user hasn't marked any pads as connector pads

    MULTI_BOARD_CONTAINER_VIEW view = MultiBoardBuildContainerView( wxFileName( proPath ) );

    if( view.containerProAbsPath.IsEmpty() )
        return true;   // not a multi-board sub-project

    if( !reportPhase( _( "Checking cross-board orphan connector pads..." ) ) )
        return false;

    // Build a set of (componentRef, pinNumber) tuples this sub-project
    // wires through cross-board nets. A pad orphaned from this set is
    // marked as a connector pad locally but the MBS doesn't know about it.
    std::set<std::pair<wxString, wxString>> wiredEndpoints;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
    {
        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : netView.myEndpoints )
            wiredEndpoints.emplace( ep.componentRef, ep.pinNumber );
    }

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( connectorPads.size() );

    for( const KIID& padUuid : connectorPads )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_ORPHAN_PAD ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        // Resolve the pad. We can't keep KIID-keyed lookups across the
        // BOARD's footprint list, so just walk; the marked-pad set is
        // small in practice.
        PAD*       pad      = nullptr;
        FOOTPRINT* parentFp = nullptr;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* p : fp->Pads() )
            {
                if( p->m_Uuid == padUuid )
                {
                    pad      = p;
                    parentFp = fp;
                    break;
                }
            }

            if( pad )
                break;
        }

        // Pad was marked but the footprint or pad has since been deleted.
        // The marker stays in m_connectorPads — surface that as a violation
        // so the user notices the dangling reference.
        if( !pad )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_ORPHAN_PAD );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "Connector-pad marker references a pad (uuid %s) that is no longer "
                       "on this board. Re-mark a current pad or clean up the marker." ),
                    padUuid.AsString() ) );
            reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
            continue;
        }

        const wxString ref       = parentFp ? parentFp->GetReference() : wxString( wxT( "?" ) );
        const wxString pinNumber = pad->GetNumber();

        if( wiredEndpoints.count( { ref, pinNumber } ) )
            continue;   // pad is wired through some cross-board net — fine

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_ORPHAN_PAD );
        drcItem->SetItems( pad );
        drcItem->SetErrorMessage( wxString::Format(
                _( "Connector pad %s/%s is marked as cross-board but no cross-board net on "
                   "the multi-board schematic references it. Refresh the MBS or wire the pin." ),
                ref, pinNumber ) );
        reportViolation( drcItem, pad->GetPosition(), pad->GetLayer() );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_ORPHAN_PAD> dummy;
}
