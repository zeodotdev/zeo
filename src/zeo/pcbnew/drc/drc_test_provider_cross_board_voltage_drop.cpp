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
    Cross-board voltage drop test provider.

    Estimates DC voltage drop across a cross-board power net using a
    simple lumped-resistance model. Two contributors:

      1. Trace resistance (per board, summed):
         R_trace = (length_mm / width_mm) × sheet_R_mΩ_per_sq

      2. Connector contact resistance (per crossing, parallel pins):
         R_contact = contact_R_per_pin / num_parallel_pins

      Total drop = (sum R_trace + sum R_contact) × expected_amps

    Defaults (all overridable per rule):
      - trace width    = 250 μm  (typical signal-class trace)
      - sheet R        = 0.5 mΩ/sq  (1oz copper)
      - per-pin R      = 20 mΩ  (typical 0.1" header)

    The model is a coarse approximation — assumes uniform trace width
    along the net and ignores parasitic via resistance. Suitable for a
    sanity-check rule, not a full SPICE analysis.

    Errors generated:
    - DRCE_CROSS_BOARD_VOLTAGE_DROP — estimated drop exceeds rule
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_VOLTAGE_DROP : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_VOLTAGE_DROP() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_VOLTAGE_DROP() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_voltage_drop" );
    }
};


// Mirrors the helper in drc_test_provider_cross_board_length.cpp. Kept
// as a separate copy so the two providers don't have to share a header
// for one helper. Returns total trace length on aBoard for the given
// net name, in nanometers.
static int64_t computeNetLengthOnBoardLocal( BOARD& aBoard, const wxString& aNetName )
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


bool DRC_TEST_PROVIDER_CROSS_BOARD_VOLTAGE_DROP::Run()
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

    if( view.containerProAbsPath.IsEmpty() || view.voltageRules.empty() )
        return true;

    if( !reportPhase( _( "Checking cross-board voltage drop..." ) ) )
        return false;

    std::map<wxString, const MULTI_BOARD_CROSS_BOARD_NET_VIEW*> netByName;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
        netByName[netView.netName] = &netView;

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
    int total = static_cast<int>( view.voltageRules.size() );

    for( const auto& [netName, rule] : view.voltageRules )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_VOLTAGE_DROP ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        if( rule.expectedAmps <= 0.0 || rule.maxDropMv <= 0.0 )
            continue;

        auto netIt = netByName.find( netName );

        if( netIt == netByName.end() )
            continue;   // net doesn't touch this board — sibling DRC will report

        const MULTI_BOARD_CROSS_BOARD_NET_VIEW* netView = netIt->second;

        // Apply documented defaults for any rule field left at zero.
        const double widthUm        = ( rule.traceWidthUm > 0.0 ) ? rule.traceWidthUm : 250.0;
        const double sheetRMOhm     = ( rule.traceSheetRMOhmsPerSq > 0.0 )
                                              ? rule.traceSheetRMOhmsPerSq
                                              : 0.5;
        const double contactRMOhm   = ( rule.contactRPerPinMOhms > 0.0 )
                                              ? rule.contactRPerPinMOhms
                                              : 20.0;

        // ---- Trace resistance ----
        // R = (length_mm / width_mm) × sheet_R_mΩ_per_sq
        auto traceR = [&]( int64_t lengthNm ) -> double
        {
            if( lengthNm <= 0 || widthUm <= 0.0 )
                return 0.0;

            double lengthMm = static_cast<double>( lengthNm ) / 1e6;
            double widthMm  = widthUm / 1000.0;
            double squares  = lengthMm / widthMm;

            return squares * sheetRMOhm;
        };

        double rTrace_mOhm = 0.0;

        rTrace_mOhm += traceR( computeNetLengthOnBoardLocal( *board, netName ) );

        std::set<wxString> siblingsTouched;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& sibEp : netView->siblingEndpoints )
        {
            BOARD* sibBoard = loadSibling( sibEp.subProjectAbsPath );

            if( !sibBoard )
                continue;

            if( !siblingsTouched.insert( sibEp.subProjectAbsPath ).second )
                continue;

            rTrace_mOhm += traceR( computeNetLengthOnBoardLocal( *sibBoard, netName ) );
        }

        // ---- Connector contact resistance ----
        // For each crossing (this board ↔ each sibling), parallel pin
        // count is min of this side's pin count and that sibling's pin
        // count on the net. Total contact R += per-pin / parallel pins.
        std::set<std::pair<wxString, wxString>> myPins;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : netView->myEndpoints )
            myPins.emplace( ep.componentRef, ep.pinNumber );

        std::map<KIID, std::set<std::pair<wxString, wxString>>> sibPinsByUuid;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : netView->siblingEndpoints )
            sibPinsByUuid[ep.subProjectUuid].emplace( ep.componentRef, ep.pinNumber );

        double rContact_mOhm = 0.0;

        for( const auto& [sibUuid, sibPins] : sibPinsByUuid )
        {
            int parallelPins =
                    static_cast<int>( std::min( myPins.size(), sibPins.size() ) );

            if( parallelPins <= 0 )
                continue;

            rContact_mOhm += contactRMOhm / parallelPins;
        }

        double totalR_mOhm = rTrace_mOhm + rContact_mOhm;
        double drop_mV     = rule.expectedAmps * totalR_mOhm;

        if( drop_mV <= rule.maxDropMv )
            continue;

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_VOLTAGE_DROP );
        drcItem->SetErrorMessage( wxString::Format(
                _( "Cross-board net '%s': estimated voltage drop %.1f mV (rule max "
                   "%.1f mV). Trace R %.2f mΩ + contact R %.2f mΩ at %.2f A. Widen "
                   "the trace, add connector pins, or relax the rule." ),
                netName, drop_mV, rule.maxDropMv, rTrace_mOhm, rContact_mOhm,
                rule.expectedAmps ) );
        reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_VOLTAGE_DROP> dummy;
}
