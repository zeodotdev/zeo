/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include <board.h>
#include <footprint.h>
#include <io/kicad/kicad_io_utils.h>
#include <pad.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <string_utils.h>

#include <drc/drc_item.h>
#include <drc/drc_test_provider.h>

#include <wx/filename.h>
#include <wx/regex.h>

#include <map>
#include <memory>
#include <vector>


/*
    Cross-board net consistency test provider.

    Fires only when the active project is part of a multi-board container.
    Complements the per-board cross-board binding check (M8.4):

      M8.4 (binding):
        Verifies THIS board's connector pads carry the MBS-declared net.
        Catches local mistakes (renamed pad, missing connector, wrong net
        on the local pad).

      THIS provider (consistency):
        For each cross-board net touching this board, lazy-loads each
        SIBLING sub-project's BOARD and verifies the corresponding
        sibling pad carries a net name compatible with this board's pad.
        Catches sibling-board drift the user wouldn't see without
        running DRC on every sub-project independently.

    Errors generated:
    - DRCE_CROSS_BOARD_CONSISTENCY — sibling pad missing or carrying a different net
*/

class DRC_TEST_PROVIDER_CROSS_BOARD_CONSISTENCY : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_CROSS_BOARD_CONSISTENCY() = default;
    virtual ~DRC_TEST_PROVIDER_CROSS_BOARD_CONSISTENCY() = default;

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return wxT( "cross_board_consistency" );
    }
};


// Same normalisation as the binding check — keep them in sync.
static wxString normaliseNetName( wxString aName )
{
    aName = UnescapeString( aName );

    if( aName.StartsWith( wxT( "/" ) ) )
        aName = aName.AfterFirst( '/' );

    static wxRegEx re( wxT( "_[0-9]+$" ) );

    if( re.IsValid() )
        re.Replace( &aName, wxEmptyString );

    return aName;
}


static PAD* findConnectorPad( BOARD* aBoard, const wxString& aRef, const wxString& aPinNumber )
{
    if( !aBoard )
        return nullptr;

    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        if( fp->GetReference() != aRef )
            continue;

        for( PAD* pad : fp->Pads() )
        {
            if( pad->GetNumber() == aPinNumber )
                return pad;
        }
    }

    return nullptr;
}


bool DRC_TEST_PROVIDER_CROSS_BOARD_CONSISTENCY::Run()
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

    if( view.containerProAbsPath.IsEmpty() || view.crossBoardNets.empty() )
        return true;   // not a multi-board sub-project — nothing to check

    if( !reportPhase( _( "Checking cross-board net consistency..." ) ) )
        return false;

    // Lazily load each sibling sub-project's BOARD by path. We don't need a
    // PROJECT here — PCB_IO_KICAD_SEXPR::LoadBoard works on a raw .kicad_pcb
    // file. Sibling boards live for the duration of this DRC run.
    std::map<wxString, std::unique_ptr<BOARD>> siblingBoards;

    auto loadSiblingByPro = [&]( const wxString& aSubProAbsPath ) -> BOARD*
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
    int total = static_cast<int>( view.crossBoardNets.size() );

    for( const MULTI_BOARD_CROSS_BOARD_NET_VIEW& netView : view.crossBoardNets )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CROSS_BOARD_CONSISTENCY ) )
            return true;

        if( !reportProgress( ii++, total, progressDelta ) )
            return false;

        // Resolve our local pads. If a local endpoint can't find a pad on
        // this board, the M8.4 binding check has already (or will) flag
        // it — skip silently here to avoid double-reporting.
        struct Resolved
        {
            const MULTI_BOARD_NET_ENDPOINT_VIEW* endpoint;
            PAD*                                 pad;
        };

        std::vector<Resolved> myResolved;

        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& myEp : netView.myEndpoints )
        {
            PAD* pad = findConnectorPad( board, myEp.componentRef, myEp.pinNumber );

            if( pad )
                myResolved.push_back( { &myEp, pad } );
        }

        if( myResolved.empty() )
            continue;

        // For every sibling endpoint, load its board and check the matching pad.
        for( const MULTI_BOARD_NET_ENDPOINT_VIEW& sibEp : netView.siblingEndpoints )
        {
            BOARD* sibBoard = loadSiblingByPro( sibEp.subProjectAbsPath );

            if( !sibBoard )
            {
                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_CONSISTENCY );
                drcItem->SetItems( myResolved.front().pad );
                drcItem->SetErrorMessage( wxString::Format(
                        _( "Cross-board net '%s' references sibling board '%s' "
                           "which could not be loaded." ),
                        netView.netName,
                        sibEp.subProjectName.IsEmpty() ? sibEp.subProjectAbsPath
                                                       : sibEp.subProjectName ) );
                reportViolation( drcItem, myResolved.front().pad->GetPosition(),
                                 myResolved.front().pad->GetLayer() );
                continue;
            }

            PAD* sibPad = findConnectorPad( sibBoard, sibEp.componentRef, sibEp.pinNumber );

            if( !sibPad )
            {
                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_CONSISTENCY );
                drcItem->SetItems( myResolved.front().pad );
                drcItem->SetErrorMessage( wxString::Format(
                        _( "Cross-board net '%s' endpoint %s/%s missing on sibling board '%s'." ),
                        netView.netName, sibEp.componentRef, sibEp.pinNumber,
                        sibEp.subProjectName ) );
                reportViolation( drcItem, myResolved.front().pad->GetPosition(),
                                 myResolved.front().pad->GetLayer() );
                continue;
            }

            // Compare the sibling pad's net name to each of our local pads.
            // Mismatch ⇒ sibling drift. Use the same normalisation as M8.4
            // so {slash}/auto-disambig don't false-positive.
            wxString sibNet       = sibPad->GetNetname();
            wxString sibNormal    = normaliseNetName( sibNet );

            for( const Resolved& mine : myResolved )
            {
                wxString myNet     = mine.pad->GetNetname();
                wxString myNormal  = normaliseNetName( myNet );

                bool matches = ( myNormal == sibNormal ) || ( myNet == sibNet );

                if( !matches )
                {
                    std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_CROSS_BOARD_CONSISTENCY );
                    drcItem->SetItems( mine.pad );
                    drcItem->SetErrorMessage( wxString::Format(
                            _( "Cross-board net '%s': pin %s/%s on this board carries '%s' "
                               "but sibling board '%s' pin %s/%s carries '%s'." ),
                            netView.netName,
                            mine.endpoint->componentRef, mine.endpoint->pinNumber,
                            UnescapeString( myNet ),
                            sibEp.subProjectName,
                            sibEp.componentRef, sibEp.pinNumber,
                            UnescapeString( sibNet ) ) );
                    reportViolation( drcItem, mine.pad->GetPosition(), mine.pad->GetLayer() );
                }
            }
        }
    }

    return !m_drcEngine->IsCancelled();
}


namespace detail
{
static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_CROSS_BOARD_CONSISTENCY> dummy;
}
