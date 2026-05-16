/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include <length_delay_calculation/multi_board_length.h>

#include <board.h>
#include <footprint.h>
#include <length_delay_calculation/length_delay_calculation.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <pcb_track.h>
#include <project.h>
#include <project/multi_board_scan.h>

#include <wx/filename.h>

#include <map>
#include <memory>
#include <set>


int64_t SumNetLengthOnBoard( BOARD& aBoard, const wxString& aNetName )
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


CROSS_BOARD_NET_LENGTH ComputeCrossBoardNetLength( BOARD& aThisBoard, const wxString& aNetName )
{
    CROSS_BOARD_NET_LENGTH result;

    // Per-board contribution. Always computed — even on a single-board
    // project this gives callers the per-board length to display.
    result.thisBoardNm = SumNetLengthOnBoard( aThisBoard, aNetName );
    result.totalNm     = result.thisBoardNm;

    if( aNetName.IsEmpty() )
        return result;

    PROJECT* prj = aThisBoard.GetProject();

    if( !prj )
        return result;

    wxString proPath = prj->GetProjectFullName();

    if( proPath.IsEmpty() )
        return result;

    // Resolve container view for this sub-project. Returns an empty
    // view (containerProAbsPath empty) when not part of a multi-board
    // container — handled below by leaving isCrossBoard=false.
    MULTI_BOARD_CONTAINER_VIEW view = MultiBoardBuildContainerView( wxFileName( proPath ) );

    if( view.containerProAbsPath.IsEmpty() )
        return result;

    const MULTI_BOARD_CROSS_BOARD_NET_VIEW* netView = nullptr;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& nv : view.crossBoardNets )
    {
        if( nv.netName == aNetName )
        {
            netView = &nv;
            break;
        }
    }

    // Net isn't declared as cross-board in the container — single-board
    // semantics apply, leave isCrossBoard=false.
    if( !netView )
        return result;

    result.isCrossBoard = true;

    // Sibling contributions. Lazy-load each sibling sub-project's .kicad_pcb
    // once per call. Guard against double-counting if the same sub-project
    // appears as multiple endpoints on the same net.
    std::map<wxString, std::unique_ptr<BOARD>> siblingBoards;
    std::set<wxString>                          siblingsTouched;

    for( const MULTI_BOARD_NET_ENDPOINT_VIEW& sibEp : netView->siblingEndpoints )
    {
        if( sibEp.subProjectAbsPath.IsEmpty() )
            continue;

        if( !siblingsTouched.insert( sibEp.subProjectAbsPath ).second )
            continue;

        auto it = siblingBoards.find( sibEp.subProjectAbsPath );
        BOARD* sibBoard = nullptr;

        if( it != siblingBoards.end() )
        {
            sibBoard = it->second.get();
        }
        else
        {
            wxFileName pcbFile = MultiBoardMainPcb( wxFileName( sibEp.subProjectAbsPath ) );

            if( !pcbFile.FileExists() )
                continue;

            PCB_IO_KICAD_SEXPR pi;

            try
            {
                if( BOARD* loaded = pi.LoadBoard( pcbFile.GetFullPath(), nullptr, nullptr,
                                                   nullptr ) )
                {
                    siblingBoards[sibEp.subProjectAbsPath] = std::unique_ptr<BOARD>( loaded );
                    sibBoard = loaded;
                }
            }
            catch( const IO_ERROR& )
            {
                continue;
            }
        }

        if( !sibBoard )
            continue;

        result.totalNm += SumNetLengthOnBoard( *sibBoard, aNetName );

        if( !sibEp.subProjectName.IsEmpty() )
            result.siblingNames.push_back( sibEp.subProjectName );
    }

    return result;
}


CROSS_BOARD_NET_LENGTH ComputeCrossBoardNetLength( BOARD& aThisBoard, const NETINFO_ITEM* aNet )
{
    if( !aNet )
        return CROSS_BOARD_NET_LENGTH{};

    return ComputeCrossBoardNetLength( aThisBoard, aNet->GetNetname() );
}
