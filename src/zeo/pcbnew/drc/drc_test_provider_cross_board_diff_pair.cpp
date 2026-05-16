/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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
    Cross-board diff-pair coherence test provider.

    Fires only when the active project is a multi-board sub-project AND
    the container declares one or more entries in
    `multi_board.cross_board_diff_pairs`.

    For each declared pair (netA, netB), checks two invariants from this
    board's perspective:

      1. **Both nets are cross-board.** If netA is a cross-board net but
         netB isn't (e.g., user wired only one half through the MBS),
         the diff pair is broken on this side. Symmetric check.

      2. **Same set of touched sub-projects.** A diff pair routed on
         board A but only one half present on board B means the differential
         signal gets converted to single-ended at the connector — almost
         certainly a wiring mistake. Compare each pair member's set of
         endpoint sub-project UUIDs.

    Currently does not check whether the local PCB has the netclass /
    diff-pair partner correctly set — that's a per-board DRC concern
    handled by the existing diff-pair providers. This check is purely
    about cross-board *topology*: the pair must touch the same boards.

    Errors generated:
    - DRCE_CROSS_BOARD_DIFF_PAIR — pair invariant broken on this board's view
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_DIFF_PAIR : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_DIFF_PAIR() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_DIFF_PAIR() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_diff_pair" );
    }
};


bool DRC_TEST_PROVIDER_CROSS_BOARD_DIFF_PAIR::Run()
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

    if( view.containerProAbsPath.IsEmpty() || view.crossBoardDiffPairs.empty() )
        return true;   // not a multi-board sub-project, or no rules declared

    if( !reportPhase( _( "Checking cross-board diff-pair coherence..." ) ) )
        return false;

    // Index the cross-board nets by name for O(1) lookup. We need both
    // membership (is netA cross-board?) and sub-project coverage (which
    // sub-projects does netA touch?).
    std::map<wxString, const MULTI_BOARD_CROSS_BOARD_NET_VIEW*> netByName;

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
        netByName[netView.netName] = &netView;

    auto subProjectsTouched = []( const MULTI_BOARD_CROSS_BOARD_NET_VIEW* aNet ) -> std::set<KIID>
    {
        std::set<KIID> result;

        if( !aNet )
            return result;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : aNet->myEndpoints )
            result.insert( ep.subProjectUuid );

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& ep : aNet->siblingEndpoints )
            result.insert( ep.subProjectUuid );

        return result;
    };

    int progressDelta = 50;
    int ii = 0;
    int total = static_cast<int>( view.crossBoardDiffPairs.size() );

    for( const auto& [netA, netB] : view.crossBoardDiffPairs )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_DIFF_PAIR ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        auto aIt = netByName.find( netA );
        auto bIt = netByName.find( netB );

        const MULTI_BOARD_CROSS_BOARD_NET_VIEW* aNet = ( aIt != netByName.end() ) ? aIt->second
                                                                                  : nullptr;
        const MULTI_BOARD_CROSS_BOARD_NET_VIEW* bNet = ( bIt != netByName.end() ) ? bIt->second
                                                                                  : nullptr;

        // Skip pairs that don't involve this board at all — the
        // sibling's DRC will catch them.
        bool aTouchesUs = ( aNet != nullptr );
        bool bTouchesUs = ( bNet != nullptr );

        if( !aTouchesUs && !bTouchesUs )
            continue;

        // Invariant 1: if one half is cross-board (touches this board)
        // and the other isn't, the pair is broken.
        if( aTouchesUs && !bNet )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_DIFF_PAIR );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "Cross-board diff pair '%s' / '%s': '%s' is cross-board on this "
                       "board but '%s' is not declared as a cross-board net. Wire the "
                       "partner through the multi-board schematic." ),
                    netA, netB, netA, netB ) );
            reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
            continue;
        }

        if( bTouchesUs && !aNet )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_DIFF_PAIR );
            drcItem->SetErrorMessage( wxString::Format(
                    _( "Cross-board diff pair '%s' / '%s': '%s' is cross-board on this "
                       "board but '%s' is not declared as a cross-board net. Wire the "
                       "partner through the multi-board schematic." ),
                    netA, netB, netB, netA ) );
            reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
            continue;
        }

        // Invariant 2: both members must touch the same set of sub-projects.
        // Otherwise the differential signal gets converted to single-ended
        // at the connector on whichever board only has one half.
        std::set<KIID> aBoards = subProjectsTouched( aNet );
        std::set<KIID> bBoards = subProjectsTouched( bNet );

        if( aBoards == bBoards )
            continue;

        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_DIFF_PAIR );
        drcItem->SetErrorMessage( wxString::Format(
                _( "Cross-board diff pair '%s' / '%s': the two nets do not touch the "
                   "same set of sub-projects. The differential signal will be converted "
                   "to single-ended at the connector. Re-wire the missing half through "
                   "the multi-board schematic." ),
                netA, netB ) );
        reportViolation( drcItem, board->GetBoundingBox().GetCenter(), UNDEFINED_LAYER );
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_DIFF_PAIR> dummy;
}
