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

#include <map>
#include <vector>


/*
    Cross-board net binding test provider.

    Fires only when the active project is part of a multi-board container.
    For each cross-board net the MBS declares as having an endpoint on
    THIS sub-project, verify the corresponding connector pad on this
    board carries the expected net.

    Catches local mistakes that the MBS-level ERC can't see:
    - Connector footprint with the right ref but the wrong pin count
      (declared pin number not present on the footprint)
    - Pad on the connector assigned to a different net than what the MBS
      contract says
    - Connector ref renamed locally but the rename hasn't flowed back
      to the MBS

    Errors generated:
    - DRCE_GENERIC_ERROR — the binding contract isn't satisfied
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_BINDING : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_BINDING() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_BINDING() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_binding" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_BINDING::Run()
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

    std::vector<MULTI_BOARD_CROSS_BOARD_BINDING> bindings =
            MultiBoardCollectCrossBoardBindingsForSubProject( wxFileName( proPath ) );

    if( bindings.empty() )
        return true;   // not a multi-board sub-project — nothing to check

    if( !reportPhase( _( "Checking cross-board net bindings..." ) ) )
        return false;

    // Index footprints by reference for quick lookup. Reference is
    // case-sensitive in KiCad practice (J1 != j1) so just use direct
    // string comparison.
    std::map<wxString, FOOTPRINT*> fpByRef;

    for( FOOTPRINT* fp : board->Footprints() )
        fpByRef[fp->GetReference()] = fp;

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( bindings.size() );

    for( const MULTI_BOARD_CROSS_BOARD_BINDING& binding : bindings )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_GENERIC_ERROR ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        // 1. Connector footprint must exist on this board.
        auto it = fpByRef.find( binding.componentRef );

        if( it == fpByRef.end() )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_GENERIC_ERROR );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "MBS cross-board net '%s' references connector '%s' "
                       "which is not present on this board." ),
                    binding.netName, binding.componentRef ) );
            reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
            continue;
        }

        FOOTPRINT* fp = it->second;

        // 2. Pad with the declared pin number must exist on the footprint.
        PAD* pad = nullptr;

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
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_GENERIC_ERROR );
            drcItem->SetItems( fp );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "MBS cross-board net '%s' references pin %s/%s "
                       "but the connector has no pad with that number." ),
                    binding.netName, binding.componentRef, binding.pinNumber ) );
            reportViolation( drcItem, fp->GetPosition(), UNDEFINED_LAYER );
            continue;
        }

        // 3. Pad must have a net assigned.
        if( pad->GetNetCode() <= 0 )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_GENERIC_ERROR );
            drcItem->SetItems( pad );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "Connector pin %s/%s should carry cross-board net '%s' "
                       "but has no net assigned." ),
                    binding.componentRef, binding.pinNumber, binding.netName ) );
            reportViolation( drcItem, pad->GetPosition(), pad->GetLayer() );
            continue;
        }

        // 4. Pad's local net name should match the MBS-declared name.
        wxString padNet = pad->GetNetname();

        // Strip any leading "/" — sub-project schematic labels carry the
        // sheet path prefix; the MBS-side name doesn't.
        wxString padNetLocal = padNet;

        if( padNetLocal.StartsWith( wxT( "/" ) ) )
            padNetLocal = padNetLocal.AfterFirst( '/' );

        if( padNetLocal != binding.netName && padNet != binding.netName )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_GENERIC_ERROR );
            drcItem->SetItems( pad );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "Connector pin %s/%s carries net '%s' but the MBS "
                       "declares cross-board net '%s' for this pin." ),
                    binding.componentRef, binding.pinNumber, padNet, binding.netName ) );
            reportViolation( drcItem, pad->GetPosition(), pad->GetLayer() );
        }
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_BINDING> dummy;
}
