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
#include <project.h>
#include <project/multi_board_scan.h>

#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>

#include <wx/filename.h>

#include <map>
#include <set>


/*
    Cross-board current capacity test provider.

    For each declared current rule (cross-board net → expected_amps,
    pin_rating_amps), counts the number of unique connector endpoints
    on this board for that net, multiplies by pin_rating_amps to get
    this board's safe carry capacity, and flags when expected_amps
    exceeds it.

    Errors generated:
    - DRCE_CROSS_BOARD_CURRENT — capacity insufficient for expected current
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_CURRENT : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_CURRENT() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_CURRENT() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_current" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_CURRENT::Run()
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

    MULTI_BOARD_CONTAINER_VIEW view = MultiBoardBuildContainerView( wxFileName( proPath ) );

    if( view.containerProAbsPath.IsEmpty() || view.currentRules.empty() )
        return true;   // not a multi-board sub-project, or no rules declared

    if( !reportPhase( _( "Checking cross-board current capacity..." ) ) )
        return false;

    // Count this board's distinct connector endpoints per net. Same
    // approach as the power-pin DRC: dedup on (componentRef, pinNumber).
    std::map<wxString, std::set<std::pair<wxString, wxString>>> pinsPerNet;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
    {
        if( !view.currentRules.count( netView.netName ) )
            continue;

        auto& pins = pinsPerNet[netView.netName];

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : netView.myEndpoints )
            pins.emplace( ep.componentRef, ep.pinNumber );
    }

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( view.currentRules.size() );

    for( const auto& [netName, rule] : view.currentRules )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_CURRENT ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        if( rule.expectedAmps <= 0.0 || rule.pinRatingAmps <= 0.0 )
            continue;   // rule incomplete, skip silently

        auto it = pinsPerNet.find( netName );

        if( it == pinsPerNet.end() )
            continue;   // net doesn't touch this board

        int    numPins  = static_cast<int>( it->second.size() );
        double capacity = numPins * rule.pinRatingAmps;

        if( capacity >= rule.expectedAmps )
            continue;

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_CURRENT );
        drcItem->SetErrorMessage( wxString::Format(
                _( "Cross-board net '%s': %d connector pin(s) on this board × %.2f A "
                   "rating = %.2f A capacity, but rule expects %.2f A. Add more "
                   "connector pads carrying this net or update the rule." ),
                netName, numPins, rule.pinRatingAmps, capacity, rule.expectedAmps ) );
        reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_CURRENT> dummy;
}
