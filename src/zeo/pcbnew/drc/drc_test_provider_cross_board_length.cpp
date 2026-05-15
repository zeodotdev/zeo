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
#include <length_delay_calculation/multi_board_length.h>
#include <project.h>
#include <project/multi_board_scan.h>

#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>

#include <wx/filename.h>

#include <set>


/*
    Cross-board total trace length test provider.

    Fires only when the active project is a multi-board sub-project AND
    the container declares one or more entries in `multi_board.max_length_nm`.
    For each rule (cross-board net name → max nm), the provider:

      1. Sums local trace length on this board for that net (using the
         standard LENGTH_DELAY_CALCULATION).
      2. Lazy-loads each sibling sub-project's BOARD by path; for each,
         finds the sibling pad that participates in the cross-board net
         and sums its trace-net length on the sibling board.
      3. Adds the totals; flags when the sum exceeds the rule.

    Catches the case the user explicitly called out: length-tuning a
    trace on board A is meaningless if the same signal is far longer
    on board B than expected — the rule is a TOTAL across both.

    Errors generated:
    - DRCE_CROSS_BOARD_LENGTH — total length exceeds rule maximum
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_LENGTH : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_LENGTH() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_LENGTH() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_length" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_LENGTH::Run()
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

    if( view.containerProAbsPath.IsEmpty() || view.maxLengthNm.empty() )
        return true;   // not a multi-board sub-project, or no rules declared

    if( !reportPhase( _( "Checking cross-board total trace length..." ) ) )
        return false;

    // Index cross-board net names this sub-project participates in. The
    // ComputeCrossBoardNetLength() primitive does the actual graph walk
    // + sibling-board load + summation; we just need to know which of
    // the rules' nets actually touch this board (rules whose net has no
    // local endpoint are checked by the sibling board's own DRC run).
    std::set<wxString> netsTouchingThisBoard;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
        netsTouchingThisBoard.insert( netView.netName );

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( view.maxLengthNm.size() );

    for( const auto& [netName, maxNm] : view.maxLengthNm )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_LENGTH ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        if( netsTouchingThisBoard.count( netName ) == 0 )
            continue;

        // Foundation primitive: returns this-board contribution + sibling
        // sum + isCrossBoard flag. Hides the sibling-board lazy-load from
        // every consumer.
        CROSS_BOARD_NET_LENGTH r = ComputeCrossBoardNetLength( *board, netName );

        if( r.totalNm <= maxNm )
            continue;

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_LENGTH );

        // Render in mm for readability — IU is nanometers internally.
        double total_mm = static_cast<double>( r.totalNm ) / 1e6;
        double max_mm   = static_cast<double>( maxNm ) / 1e6;

        drcItem->SetErrorMessage( wxString::Format(
                _( "Cross-board net '%s': total trace length across all boards is "
                   "%.3f mm (rule max %.3f mm)." ),
                netName, total_mm, max_mm ) );
        reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_LENGTH> dummy;
}
