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


/*
    Cross-board power pin count test provider.

    Fires only when the active project is a multi-board sub-project AND
    the container declares one or more rules in `multi_board.min_power_pins`.
    For each declared (powerNetName, minPins) rule, count how many of THIS
    board's endpoints are wired through a cross-board net whose canonical
    name matches the rule key. Flag when the count is below the minimum.

    Counting: an "endpoint" here is one (componentRef, pinNumber) on this
    sub-project that participates in a cross-board net of the named power.
    The same physical pad is counted once even if multiple cross-board
    nets happen to bind to it (which shouldn't happen by construction).

    Errors generated:
    - DRCE_CROSS_BOARD_POWER_PINS — pin count below the rule minimum
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_POWER_PINS : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_POWER_PINS() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_POWER_PINS() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_power_pins" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_POWER_PINS::Run()
{
    BOARD* board = m_drcEngine->GetBoard();

    wxLogMessage( "[DRC_CB_POWER] Run() entry, board=%p", board );

    if( !board )
        return true;

    PROJECT* prj = board->GetProject();

    wxLogMessage( "[DRC_CB_POWER] prj=%p", prj );

    if( !prj )
        return true;

    wxString proPath = prj->GetProjectFullName();

    wxLogMessage( "[DRC_CB_POWER] proPath='%s'", proPath );

    if( proPath.IsEmpty() )
        return true;

    MULTI_BOARD_CONTAINER_VIEW view = MultiBoardBuildContainerView( wxFileName( proPath ) );

    wxLogMessage( "[DRC_CB_POWER] view: containerProAbsPath='%s' minPowerPins.size=%zu "
                  "crossBoardNets.size=%zu mySubProjectUuid='%s'",
                  view.containerProAbsPath, view.minPowerPins.size(),
                  view.crossBoardNets.size(), view.mySubProjectUuid.AsString() );

    for( const auto& [netName, minPins] : view.minPowerPins )
        wxLogMessage( "[DRC_CB_POWER]   rule: %s -> %d", netName, minPins );

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& nv : view.crossBoardNets )
        wxLogMessage( "[DRC_CB_POWER]   net: %s myEndpoints=%zu siblings=%zu",
                      nv.netName, nv.myEndpoints.size(), nv.siblingEndpoints.size() );

    if( view.containerProAbsPath.IsEmpty() || view.minPowerPins.empty() )
    {
        wxLogMessage( "[DRC_CB_POWER] EARLY RETURN: containerEmpty=%d minPowerPinsEmpty=%d",
                      view.containerProAbsPath.IsEmpty(), view.minPowerPins.empty() );
        return true;   // not a multi-board sub-project, or no rules declared
    }

    if( !reportPhase( _( "Checking cross-board power-pin counts..." ) ) )
        return false;

    // Per-power-net pin count on THIS board. Each unique
    // (componentRef, pinNumber) pair on the local side of a matching
    // cross-board net counts once. A net is "matching" when its
    // canonical name equals a rule key — exact string match, no
    // normalisation. The rule is authored by the user on the
    // container, so it's expected to use the same canonical form the
    // MBS extractor produces.
    std::map<wxString, std::set<std::pair<wxString, wxString>>> pinsPerNet;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
    {
        if( !view.minPowerPins.count( netView.netName ) )
            continue;

        auto& pins = pinsPerNet[netView.netName];

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : netView.myEndpoints )
            pins.emplace( ep.componentRef, ep.pinNumber );
    }

    for( const auto& [netName, minPins] : view.minPowerPins )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_POWER_PINS ) )
        {
            wxLogMessage( "[DRC_CB_POWER]   '%s': error limit exceeded, bailing", netName );
            return true;
        }

        auto it = pinsPerNet.find( netName );
        int  count = ( it != pinsPerNet.end() ) ? static_cast<int>( it->second.size() ) : 0;

        wxLogMessage( "[DRC_CB_POWER]   evaluate '%s': count=%d minPins=%d", netName, count, minPins );

        // Skip nets that don't touch this board at all — that's an
        // "unrelated board" case, not a violation.
        if( count == 0 )
        {
            wxLogMessage( "[DRC_CB_POWER]   '%s': SKIP (count==0, unrelated board)", netName );
            continue;
        }

        if( count >= minPins )
        {
            wxLogMessage( "[DRC_CB_POWER]   '%s': SKIP (count>=minPins, satisfied)", netName );
            continue;
        }

        wxLogMessage( "[DRC_CB_POWER]   '%s': FIRING violation (count=%d < minPins=%d)",
                      netName, count, minPins );

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_POWER_PINS );

        if( !drcItem )
        {
            wxLogMessage( "[DRC_CB_POWER]   '%s': DRC_ITEM::Create returned NULL!", netName );
            continue;
        }

        drcItem->SetErrorMessage( wxString::Format(
                _( "Cross-board power net '%s' has %d connector pin(s) on this board "
                   "(rule requires at least %d). Add more connector pads carrying this "
                   "net or relax the rule on the multi-board container." ),
                netName, count, minPins ) );

        wxLogMessage( "[DRC_CB_POWER]   '%s': calling reportViolation", netName );
        reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
        wxLogMessage( "[DRC_CB_POWER]   '%s': reportViolation returned", netName );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_POWER_PINS> dummy;
}
