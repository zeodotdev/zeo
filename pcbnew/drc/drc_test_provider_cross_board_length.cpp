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
#include <length_delay_calculation/length_delay_calculation.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <pcb_track.h>
#include <project.h>
#include <project/multi_board_scan.h>

#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>

#include <wx/filename.h>

#include <map>
#include <memory>
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


// Sum trace length for the named net on aBoard. Walks tracks + pads and
// feeds them through the board's LENGTH_DELAY_CALCULATION (same engine
// the matched-length DRC uses). Returns 0 if the net doesn't exist on
// this board.
static int64_t computeNetLengthOnBoard( BOARD& aBoard, const wxString& aNetName )
{
    NETINFO_ITEM* net = aBoard.GetNetInfo().GetNetItem( aNetName );

    if( !net )
        return 0;

    int netCode = net->GetNetCode();

    if( netCode <= 0 )
        return 0;

    LENGTH_DELAY_CALCULATION* calc = aBoard.GetLengthCalculation();

    if( !calc )
        return 0;

    std::vector<LENGTH_DELAY_CALCULATION_ITEM> items;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( track->GetNetCode() != netCode )
            continue;

        LENGTH_DELAY_CALCULATION_ITEM li = calc->GetLengthCalculationItem( track );

        if( li.Type() != LENGTH_DELAY_CALCULATION_ITEM::TYPE::UNKNOWN )
            items.push_back( li );
    }

    for( FOOTPRINT* fp : aBoard.Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            if( pad->GetNetCode() != netCode )
                continue;

            LENGTH_DELAY_CALCULATION_ITEM li = calc->GetLengthCalculationItem( pad );

            if( li.Type() != LENGTH_DELAY_CALCULATION_ITEM::TYPE::UNKNOWN )
                items.push_back( li );
        }
    }

    if( items.empty() )
        return 0;

    constexpr PATH_OPTIMISATIONS opts = { .OptimiseViaLayers     = true,
                                          .MergeTracks           = true,
                                          .OptimiseTracesInPads  = true,
                                          .InferViaInPad         = false };

    return calc->CalculateLength( items, opts );
}


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

    // Index nets by name so we can look up which sibling sub-projects
    // each rule's net touches without re-scanning.
    std::map<wxString, const MULTI_BOARD_CROSS_BOARD_NET_VIEW*> netByName;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
        netByName[netView.netName] = &netView;

    // Lazy sibling-board cache. Each load is expensive (parse .kicad_pcb)
    // so the same sibling shouldn't be re-loaded for each rule.
    std::map<wxString, std::unique_ptr<BOARD>> siblingBoards;

    auto loadSibling = [&]( const wxString& aSubProAbsPath ) -> BOARD*
    {
        if( aSubProAbsPath.IsEmpty() )
            return nullptr;

        auto it = siblingBoards.find( aSubProAbsPath );

        if( it != siblingBoards.end() )
            return it->second.get();

        wxFileName pcbFile = MultiBoardMainPcb( wxFileName( aSubProAbsPath ) );

        if( !pcbFile.FileExists() )
            return nullptr;

        PCB_IO_KICAD_SEXPR pi;
        BOARD*             rawBoard = nullptr;

        try
        {
            rawBoard = pi.LoadBoard( pcbFile.GetFullPath(), nullptr, nullptr, nullptr );
        }
        catch( const IO_ERROR& )
        {
            return nullptr;
        }

        if( !rawBoard )
            return nullptr;

        siblingBoards[aSubProAbsPath] = std::unique_ptr<BOARD>( rawBoard );

        return siblingBoards[aSubProAbsPath].get();
    };

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( view.maxLengthNm.size() );

    for( const auto& [netName, maxNm] : view.maxLengthNm )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_LENGTH ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        auto netIt = netByName.find( netName );

        // Net is in the rule but doesn't touch this board's
        // sub-project — skip; the sibling board's DRC run will check it.
        if( netIt == netByName.end() )
            continue;

        const MULTI_BOARD_CROSS_BOARD_NET_VIEW* netView = netIt->second;

        // Local contribution. Same calc the standard matched-length
        // DRC uses, just on a per-net basis.
        int64_t total_nm = computeNetLengthOnBoard( *board, netName );

        // Sibling contributions. Track which siblings contributed so
        // the violation message can be detailed.
        std::set<wxString> siblingsTouched;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& sibEp : netView->siblingEndpoints )
        {
            BOARD* sibBoard = loadSibling( sibEp.subProjectAbsPath );

            if( !sibBoard )
                continue;

            // Guard against double-counting if the same sub-project
            // appears as multiple endpoints on the same net.
            if( !siblingsTouched.insert( sibEp.subProjectAbsPath ).second )
                continue;

            total_nm += computeNetLengthOnBoard( *sibBoard, netName );
        }

        if( total_nm <= maxNm )
            continue;

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_LENGTH );

        // Render in mm for readability — IU is nanometers internally.
        double total_mm = static_cast<double>( total_nm ) / 1e6;
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
