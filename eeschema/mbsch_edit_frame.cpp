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

#include "mbsch_edit_frame.h"
#include "toolbars_mbsch_editor.h"

#include "api/api_handler_mbs_sch.h"

#include <api/api_server.h>
#include <connection_graph.h>
#include <fmt/format.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <mail_type.h>
#include <multi_board_net_extractor.h>
#include <project/project_file.h>
#include <sch_module_block.h>
#include <sch_module_pin.h>
#include <schematic.h>
#include <sch_screen.h>
#include <kiface_base.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tool/common_control.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <widgets/wx_menubar.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>


MBSCH_EDIT_FRAME::MBSCH_EDIT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        SCH_EDIT_FRAME( aKiway, aParent, FRAME_MBSCH )
{
    m_aboutTitle = _HKI( "Zeo Multi-Board Schematic Editor" );

    // Replace the toolbar config the SCH_EDIT_FRAME ctor installed with
    // the trimmed MBS variant. Re-run the toolbar configure + recreate
    // pipeline so the visible bars reflect the new config. Cheaper than
    // adding a virtual hook in the base class for a single subclass.
    m_toolbarSettings = GetToolbarSettings<MBSCH_EDIT_TOOLBAR_SETTINGS>( "mbsch-toolbars" );
    configureToolbars();
    RecreateToolbars();

    // Swap the menu bar for the trimmed MBS menu.
    ReCreateMenuBar();

    // Hide the hierarchy pane — an MBS is a single flat sheet by design,
    // so a sheet-hierarchy tree has no content to show and just takes
    // space from the schematic canvas.
    if( wxAuiPaneInfo& hierarchyPane = m_auimgr.GetPane( SchematicHierarchyPaneName() );
        hierarchyPane.IsOk() )
    {
        hierarchyPane.Show( false );
        m_auimgr.Update();
    }

#ifdef KICAD_IPC_API
    // Replace the inherited API_HANDLER_SCH (which claims DOCTYPE_SCHEMATIC)
    // with API_HANDLER_MBS_SCH (which claims DOCTYPE_MBS_SCHEMATIC). Lets
    // the API server route MBS-specific kipy commands here deterministically
    // even when a regular schematic editor is open in the same process.
    if( m_apiHandler )
    {
        Pgm().GetApiServer().DeregisterHandler( m_apiHandler.get() );
        m_apiHandler.reset();
    }

    m_apiHandler = std::make_unique<API_HANDLER_MBS_SCH>( this );
    Pgm().GetApiServer().RegisterHandler( m_apiHandler.get() );
#endif
}


MBSCH_EDIT_FRAME::~MBSCH_EDIT_FRAME()
{
}


wxString MBSCH_EDIT_FRAME::windowTitleSuffix() const
{
    return _( "Multi-Board Schematic Editor" );
}


void MBSCH_EDIT_FRAME::doReCreateMenuBar()
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
    COMMON_CONTROL*     commonControl = m_toolManager->GetTool<COMMON_CONTROL>();

    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    // -- File menu --
    ACTION_MENU* fileMenu = new ACTION_MENU( false, selTool );
    fileMenu->Add( ACTIONS::save );
    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );
    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "Multi-Board Schematic Editor" ) );

    // -- Edit menu --
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );
    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::paste );
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    // -- View menu --
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    // Panels submenu — mirrors regular SCH "View > Panels". MBSCH
    // doesn't expose Hierarchy / Design Blocks / Remote Symbols (none
    // make sense on a flat cross-board schematic), but Properties and
    // Net Navigator are both useful and structured the same way as
    // the regular SCH for muscle-memory consistency.
    ACTION_MENU* panelsMenu = new ACTION_MENU( false, selTool );
    panelsMenu->SetTitle( _( "Panels" ) );
    panelsMenu->Add( ACTIONS::showProperties,       ACTION_MENU::CHECK );
    panelsMenu->Add( SCH_ACTIONS::showNetNavigator, ACTION_MENU::CHECK );
    viewMenu->Add( panelsMenu );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::toggleGrid,     ACTION_MENU::CHECK );
    viewMenu->Add( ACTIONS::gridProperties );

    // -- Place menu -- (restricted to what makes sense on an MBS)
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );
    placeMenu->Add( SCH_ACTIONS::drawWire );
    placeMenu->Add( SCH_ACTIONS::drawBus );
    placeMenu->Add( SCH_ACTIONS::placeBusWireEntry );
    placeMenu->Add( SCH_ACTIONS::placeJunction );
    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeLabel );
    placeMenu->Add( SCH_ACTIONS::placeGlobalLabel );
    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSchematicText );
    placeMenu->Add( SCH_ACTIONS::drawTextBox );
    placeMenu->Add( SCH_ACTIONS::drawTable );
    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawLines );
    placeMenu->Add( SCH_ACTIONS::placeImage );

    // -- Tools menu -- (MBS-specific)
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );
    toolsMenu->Add( SCH_ACTIONS::refreshMbsFromSubProjects );

    // -- Preferences menu --
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );
    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::showDesignBlockLibTable );
    prefsMenu->AppendSeparator();
    prefsMenu->Add( ACTIONS::openPreferences );

    // -- Help menu (standard) --
    ACTION_MENU* helpMenu = new ACTION_MENU( false, commonControl );
    helpMenu->Add( ACTIONS::help );
    helpMenu->Add( ACTIONS::reportBug );
    helpMenu->AppendSeparator();
    helpMenu->Add( ACTIONS::about );

    menuBar->Append( fileMenu,   _( "&File" ) );
    menuBar->Append( editMenu,   _( "&Edit" ) );
    menuBar->Append( viewMenu,   _( "&View" ) );
    menuBar->Append( placeMenu,  _( "&Place" ) );
    menuBar->Append( toolsMenu,  _( "&Tools" ) );
    menuBar->Append( prefsMenu,  _( "P&references" ) );
    menuBar->Append( helpMenu,   _( "&Help" ) );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}


void MBSCH_EDIT_FRAME::onSchematicSaved()
{
    SCH_SCREEN* rootScreen = Schematic().RootScreen();

    if( !rootScreen )
        return;

    // Backfill empty mbs_reference values. Blocks added via the legacy code
    // path (or imported from a project that pre-dates the auto-assign in
    // mbs_refresh's ADD_BLOCK case) can carry empty mbs_references, which
    // makes them indistinguishable from sibling blocks that share the same
    // component_ref (e.g. CN1 in two sub-projects). With no unique handle,
    // the agent's sch_label silently retargets the wrong block. Assign the
    // next free B<N> to any empty ref. The schematic is marked modified so
    // the next save persists; we don't re-save here to avoid recursing into
    // onSchematicSaved.
    {
        std::set<int>                  usedRefs;
        std::vector<SCH_MODULE_BLOCK*> needsBackfill;

        for( SCH_ITEM* item : rootScreen->Items().OfType( SCH_MODULE_BLOCK_T ) )
        {
            SCH_MODULE_BLOCK* b   = static_cast<SCH_MODULE_BLOCK*>( item );
            const wxString&   ref = b->GetMbsReference();

            if( ref.IsEmpty() )
            {
                needsBackfill.push_back( b );
                continue;
            }

            if( !ref.StartsWith( wxT( "B" ) ) )
                continue;

            long n = 0;

            if( ref.Mid( 1 ).ToLong( &n ) && n > 0 )
                usedRefs.insert( (int) n );
        }

        if( !needsBackfill.empty() )
        {
            int candidate = 1;

            for( SCH_MODULE_BLOCK* b : needsBackfill )
            {
                while( usedRefs.count( candidate ) )
                    candidate++;

                b->SetMbsReference( wxString::Format( wxT( "B%d" ), candidate ) );
                usedRefs.insert( candidate );
            }

            OnModify();

            wxString msg = wxString::Format(
                    _( "Multi-board: backfilled %zu empty block reference(s); "
                       "save again to persist." ),
                    needsBackfill.size() );
            SetStatusText( msg, 0 );
            wxLogInfo( wxT( "MBSCH: %s" ), msg );
        }
    }

    wxFileName schFn( rootScreen->GetFileName() );

    if( !schFn.IsAbsolute() )
        schFn.MakeAbsolute( Prj().GetProjectPath() );

    // Walk upward from the MBS's directory for a `.kicad_pro` with
    // `multi_board.container = true` whose `mbs_file` points at this
    // schematic. The container/MBS pairing isn't stored inside the
    // `.kicad_mbs` itself yet, so a targeted walk remains the link.
    wxFileName multiFile;
    wxFileName searchDir( schFn );
    searchDir.SetFullName( wxEmptyString );

    for( int depth = 0; depth < 6 && searchDir.GetPath().Length() > 1; ++depth )
    {
        wxArrayString files;
        wxDir::GetAllFiles( searchDir.GetPath(), &files, wxT( "*.kicad_pro" ),
                            wxDIR_FILES );

        for( const wxString& candidate : files )
        {
            PROJECT_FILE probe( candidate );

            if( !probe.LoadFromFile() )
                continue;

            if( !probe.IsMultiBoardContainer() )
                continue;

            wxFileName expectedMbs = probe.ResolveMbsPath();

            if( expectedMbs.IsOk()
                && expectedMbs.GetFullPath() == schFn.GetFullPath() )
            {
                multiFile = wxFileName( candidate );
                break;
            }
        }

        if( multiFile.IsOk() )
            break;

        searchDir.RemoveLastDir();
    }

    if( !multiFile.IsOk() || !multiFile.FileExists() )
        return;

    PROJECT_FILE multi( multiFile.GetFullPath() );

    if( !multi.LoadFromFile() )
        return;

    std::vector<MB_CROSS_BOARD_NET> nets = ExtractCrossBoardNets( Schematic(), multi );
    multi.SetCrossBoardNets( nets );
    // aForce=true: SetCrossBoardNets is a plain member assignment that bypasses
    // the JSON_SETTINGS modified-tracking, so SaveToFile would otherwise be a
    // silent no-op and the cross-board net list would never persist.
    multi.SaveToFile( wxString(), /* aForce */ true );

    // Surface extraction outcomes in the status bar so a user wiring the
    // MBS can immediately tell whether Sync will have anything to do —
    // without opening the project manager or inspecting the .kicad_pro.
    wxString msg = wxString::Format( _( "Multi-board: extracted %zu cross-board net(s)" ),
                                     nets.size() );
    SetStatusText( msg, 0 );
    wxLogTrace( wxT( "MULTI_BOARD" ), wxS( "%s → %s" ), msg, multiFile.GetFullPath() );
}


// ── Cross-probe from sub-project editors ───────────────────────────────────
//
// Sub-project SCH/PCB editors broadcast MAIL_CROSS_PROBE / MAIL_SELECTION
// packets when the user clicks a symbol, footprint, or net. The MBSCH
// editor is a distinct FRAME_T, so those broadcasts reach it only because
// the SCH/PCB senders explicitly add an ExpressMail(FRAME_MBSCH, ...) call
// alongside their FRAME_SCH/FRAME_PCB_EDITOR broadcasts. We intercept the
// mails here and map payload tokens to SCH_MODULE_BLOCK / CONNECTION_SUBGRAPH
// highlights. The base SCH_EDIT_FRAME::KiwayMailIn handles other mail types
// we still want (library reload, ngspice, etc.) so we fall through when we
// don't handle a specific command.

namespace
{

/**
 * Extract the quoted string immediately following a `$KEY:` token.
 * Returns wxEmptyString if the key is absent. Used for the legacy DDE
 * payload format shared with SCH ↔ PCB cross-probing (`$PART: "J1"`,
 * `$NET: "BATTERY"`, etc.).
 */
wxString extractQuotedParam( const wxString& aPayload, const wxString& aKey )
{
    int key = aPayload.Find( aKey );

    if( key == wxNOT_FOUND )
        return wxEmptyString;

    int open = aPayload.find( '"', key + aKey.Length() );

    if( open == wxNOT_FOUND )
        return wxEmptyString;

    int close = aPayload.find( '"', open + 1 );

    if( close == wxNOT_FOUND )
        return wxEmptyString;

    return aPayload.Mid( open + 1, close - open - 1 );
}

}   // anonymous namespace


void MBSCH_EDIT_FRAME::crossProbeHighlightPart( const wxString& aRef,
                                                const wxString& aPadOrPin,
                                                const wxString& aSenderProjectPath )
{
    if( aRef.IsEmpty() )
        return;

    SCH_SCREEN* screen = Schematic().RootScreen();

    if( !screen )
        return;

    // If the caller told us which sub-project originated this probe,
    // resolve its .kicad_pro absolute path so we can match blocks that
    // live on THAT sub-project only. Multiple blocks may carry the
    // same local componentRef (e.g. both boards have a "J2"); without
    // the scope filter, we'd pick whichever iterated first.
    wxString   scopeAbsPath;
    wxFileName scopeFn;

    if( !aSenderProjectPath.IsEmpty() )
    {
        scopeFn.Assign( aSenderProjectPath );
        scopeFn.Normalize( FN_NORMALIZE_FLAGS );
        scopeAbsPath = scopeFn.GetFullPath();
    }

    auto blockSubProjectAbsPath = [this]( SCH_MODULE_BLOCK* aBlock ) -> wxString
    {
        if( aBlock->GetSubProjectPath().IsEmpty() )
            return wxEmptyString;

        wxFileName fn( aBlock->GetSubProjectPath() );

        if( !fn.IsAbsolute() )
            fn.MakeAbsolute( Prj().GetProjectPath() );

        fn.Normalize( FN_NORMALIZE_FLAGS );
        return fn.GetFullPath();
    };

    SCH_MODULE_BLOCK* matchedBlock = nullptr;
    SCH_MODULE_PIN*   matchedPin   = nullptr;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_MODULE_BLOCK_T )
            continue;

        SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

        if( block->GetComponentRef() != aRef )
            continue;

        // When a sender scope is available, require the block's
        // sub-project absolute path to match. Blocks with no path
        // info (e.g. hand-created placeholders) always match so
        // legacy workflows still work unscoped.
        if( !scopeAbsPath.IsEmpty() )
        {
            wxString blockAbsPath = blockSubProjectAbsPath( block );

            if( !blockAbsPath.IsEmpty() && blockAbsPath != scopeAbsPath )
                continue;
        }

        matchedBlock = block;

        if( !aPadOrPin.IsEmpty() )
        {
            for( SCH_MODULE_PIN* pin : block->GetPins() )
            {
                if( pin->GetPinNumber() == aPadOrPin )
                {
                    matchedPin = pin;
                    break;
                }
            }
        }

        break;
    }

    if( !matchedBlock )
        return;

    // Select via the standard selection tool so the painter + properties
    // pane + whole selection machinery (copy, delete) stay consistent
    // with clicks originating from the user.
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    if( !selTool )
        return;

    // No selectionClear here: callers (KiwayMailIn) batch multiple specs
    // and clear once at the start. Clearing per-spec would leave only
    // the last item in the selection, breaking lasso / shift-click which
    // both round-trip a multi-item $SELECT through this handler.
    EDA_ITEM* target = matchedPin ? static_cast<EDA_ITEM*>( matchedPin )
                                  : static_cast<EDA_ITEM*>( matchedBlock );
    selTool->AddItemToSel( target );

    FocusOnItem( target );
    GetCanvas()->Refresh();
}


void MBSCH_EDIT_FRAME::crossProbeHighlightNet( const wxString& aNetName )
{
    // Reject placeholder names ("<NO NET>", bare "/", empty) — a net
    // name broadcast that matches every unnamed subgraph's display
    // would brighten every orphan wire on the MBS sheet. Treat as a
    // clear instead.
    auto isPlaceholderName = []( const wxString& aName ) -> bool
    {
        return aName.IsEmpty() || aName == wxT( "/" )
               || aName.Contains( wxT( "<NO NET>" ) );
    };

    if( isPlaceholderName( aNetName ) )
    {
        SetHighlightedConnection( wxEmptyString );
        m_toolManager->RunAction( SCH_ACTIONS::updateNetHighlighting );
        return;
    }

    // The standard SCH painter only brightens items whose cached
    // SCH_CONNECTION name matches m_highlightedConn. That name is
    // produced by CONNECTION_GRAPH during RecalculateConnections — if
    // the graph hasn't been rebuilt since the last edit, Connection()
    // pointers may be stale or null and every item falls through to
    // the "brighten when unconnected" fallback in UpdateNetHighlighting.
    // Force a rebuild when anything is dirty so the match set is
    // well-defined before we decide who gets brightened.
    if( SCH_SCREEN* rootScreen = Schematic().RootScreen() )
    {
        for( SCH_ITEM* item : rootScreen->Items() )
        {
            if( item->IsConnectivityDirty() )
            {
                RecalculateConnections( nullptr, NO_CLEANUP );
                break;
            }
        }
    }

    // Resolve the incoming name to whatever the local MBS connection
    // graph actually calls the matching subgraph.
    //
    // Three lookups, in order:
    //
    //   1. Exact match — works when the sub-project net name happens
    //      to align with the MBS driver (e.g. both called "GND").
    //
    //   2. Slash toggle — sub-project PCB/SCH broadcast `/FOO` with a
    //      sheet-path prefix; the MBS driver typically doesn't have
    //      it. Try both forms.
    //
    //   3. Pin-text match — the reverse-direction case: sub-project
    //      broadcasts its local net name (e.g. "/VBAT") but the MBS
    //      subgraph is driven by a LABEL (e.g. "BATTERY") that
    //      outranks module pins in driver priority. The pin whose
    //      text equals "/VBAT" lives on that BATTERY subgraph, so we
    //      resolve via the pin to the subgraph, then adopt the
    //      subgraph's actual name as the local highlight target.
    //
    // Pin-text match also captures `target` as a side effect so the
    // cross-board fan-out below doesn't need a second search.
    CONNECTION_GRAPH*    graph  = Schematic().ConnectionGraph();
    wxString             localName = aNetName;
    CONNECTION_SUBGRAPH* target = nullptr;
    SCH_SCREEN*          screen = Schematic().RootScreen();

    if( graph && !aNetName.IsEmpty() )
    {
        auto nameResolves = [&]( const wxString& aCandidate ) -> bool
        {
            return graph->FindFirstSubgraphByName( aCandidate ) != nullptr;
        };

        if( !nameResolves( localName ) )
        {
            if( localName.StartsWith( wxT( "/" ) )
                && nameResolves( localName.AfterFirst( '/' ) ) )
            {
                localName = localName.AfterFirst( '/' );
            }
            else if( !localName.StartsWith( wxT( "/" ) )
                     && nameResolves( wxT( "/" ) + localName ) )
            {
                localName = wxT( "/" ) + localName;
            }
            else if( screen )
            {
                // Pin-text fallback: find any module pin whose displayed
                // text matches the requested name, then use its
                // subgraph's resolved name as the local target.
                wxString withoutSlash = localName.StartsWith( wxT( "/" ) )
                                                ? localName.AfterFirst( '/' )
                                                : localName;
                wxString withSlash    = localName.StartsWith( wxT( "/" ) )
                                                ? localName
                                                : wxT( "/" ) + localName;

                for( SCH_ITEM* item : screen->Items() )
                {
                    if( item->Type() != SCH_MODULE_BLOCK_T )
                        continue;

                    for( SCH_MODULE_PIN* pin :
                         static_cast<SCH_MODULE_BLOCK*>( item )->GetPins() )
                    {
                        wxString pinText = pin->GetText();

                        if( pinText != localName && pinText != withoutSlash
                            && pinText != withSlash )
                        {
                            continue;
                        }

                        if( CONNECTION_SUBGRAPH* sg = graph->GetSubgraphForItem( pin ) )
                        {
                            target    = sg;
                            localName = graph->GetResolvedSubgraphName( sg );
                            break;
                        }
                    }

                    if( target )
                        break;
                }
            }
        }
    }

    SetHighlightedConnection( localName );

    // SetHighlightedConnection only stores the net name; the painter
    // still needs an explicit UpdateNetHighlighting pass to flip
    // IsBrightened() on matching items. Without this the MBS canvas
    // keeps its previous brightness state — including the "all wires
    // lit" state when the previous highlight was "" (match everything).
    m_toolManager->RunAction( SCH_ACTIONS::updateNetHighlighting );

    if( aNetName.IsEmpty() )
        return;

    // Cross-board net propagation. Find the subgraph for this net in the
    // MBS connection graph; its SCH_MODULE_PINs identify every connector
    // endpoint that the user's cross-board wiring ties together. Emit a
    // targeted `$PART: "ref" $PAD: "pin"` cross-probe per endpoint so
    // each sub-project's PCB and schematic editor highlight their local
    // connector regardless of whether the net names have been aligned
    // by Sync yet — post-sync the standard $NET broadcast would also
    // work, but pad-level probes remain correct pre-sync.
    if( !graph )
        return;

    // Re-search by name if the pin-text fallback didn't set target.
    if( !target && screen )
    {
        for( SCH_ITEM* item : screen->Items() )
        {
            if( item->Type() != SCH_MODULE_BLOCK_T )
                continue;

            for( SCH_MODULE_PIN* pin : static_cast<SCH_MODULE_BLOCK*>( item )->GetPins() )
            {
                CONNECTION_SUBGRAPH* sg = graph->GetSubgraphForItem( pin );

                if( sg && ( graph->GetResolvedSubgraphName( sg ) == aNetName
                            || graph->GetResolvedSubgraphName( sg ) == localName ) )
                {
                    target = sg;
                    break;
                }
            }

            if( target )
                break;
        }
    }

    if( !target )
        return;

    // Collect (ref, pin-number, sub-project-abs-path) endpoints.
    // Sub-project scope is the sub-project's .kicad_pro absolute path
    // (resolved from the block's relative sub-project path against the
    // MBS container's project directory). Sub-project PCB/SCH frames
    // compare their own Prj().GetProjectFullName() against this scope
    // and ignore probes that don't target them — without this,
    // FRAME_PCB_EDITOR broadcasts would hit every sibling board that
    // happens to also have a footprint named e.g. "J2" on it.
    struct Endpoint
    {
        wxString ref;
        wxString pinNum;
        wxString subProjAbsPath;   // empty if block has no sub-project path
    };

    std::vector<Endpoint> endpoints;
    wxString              mbsProjectDir = Prj().GetProjectPath();

    for( SCH_ITEM* item : target->GetItems() )
    {
        if( item->Type() != SCH_MODULE_PIN_T )
            continue;

        SCH_MODULE_PIN*   pin   = static_cast<SCH_MODULE_PIN*>( item );
        SCH_MODULE_BLOCK* block = pin->GetParent();

        if( !block )
            continue;

        wxString ref = block->GetComponentRef();

        if( ref.IsEmpty() )
            ref = pin->GetComponentRef();

        if( ref.IsEmpty() || pin->GetPinNumber().IsEmpty() )
            continue;

        wxString absPath;

        if( !block->GetSubProjectPath().IsEmpty() )
        {
            wxFileName fn( block->GetSubProjectPath() );

            if( !fn.IsAbsolute() )
                fn.MakeAbsolute( mbsProjectDir );

            absPath = fn.GetFullPath();
        }

        endpoints.push_back( { ref, pin->GetPinNumber(), absPath } );
    }

    // Fire one cross-probe per endpoint, scoped to a single sub-project.
    // The $PROJECT: token tells each sub-project PCB/SCH whether this
    // probe is for them; non-matches skip silently without touching
    // their existing highlight state.
    for( const Endpoint& ep : endpoints )
    {
        std::string packet;

        if( !ep.subProjAbsPath.IsEmpty() )
        {
            packet = fmt::format( "$PART: \"{}\" $PAD: \"{}\" $PROJECT: \"{}\"",
                                  TO_UTF8( ep.ref ), TO_UTF8( ep.pinNum ),
                                  TO_UTF8( ep.subProjAbsPath ) );
        }
        else
        {
            packet = fmt::format( "$PART: \"{}\" $PAD: \"{}\"",
                                  TO_UTF8( ep.ref ), TO_UTF8( ep.pinNum ) );
        }

        Kiway().ExpressMail( FRAME_PCB_EDITOR,   MAIL_CROSS_PROBE, packet, this );
        Kiway().ExpressMail( FRAME_SCH,          MAIL_CROSS_PROBE, packet, this );
        // The 3D assembly viewer (when open alongside this MBSCH)
        // registers itself as a FRAME_PCB_DISPLAY3D peer player.
        // Single-board 3D viewers don't override KiwayMailIn, so they
        // silently ignore this broadcast — only the assembly viewer
        // acts on it.
        Kiway().ExpressMail( FRAME_PCB_DISPLAY3D, MAIL_CROSS_PROBE, packet, this );
    }
}


void MBSCH_EDIT_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    const std::string& payloadStd = aEvent.GetPayload();
    wxString           payload( payloadStd.c_str(), wxConvUTF8 );

    // Two boards can legitimately share a connector ref (both have
    // "J2"), so just matching F<ref> against componentRef produces the
    // wrong block. The sender frame carries its own PROJECT — read it
    // here and pass down as scope so the match becomes unambiguous.
    // KIWAY_MAIL_EVENT stashes the sender wxWindow* via SetEventObject
    // on construction; GetEventObject() hands it back. Cast up to
    // EDA_BASE_FRAME so we can reach the sender's PROJECT binding.
    auto senderProjectPath = [&aEvent]() -> wxString
    {
        wxObject* source = aEvent.GetEventObject();

        if( auto* frame = dynamic_cast<EDA_BASE_FRAME*>( source ) )
            return frame->Prj().GetProjectFullName();

        return wxEmptyString;
    };

    switch( aEvent.Command() )
    {
    case MAIL_CROSS_PROBE:
    {
        if( payload.Contains( wxT( "$CLEAR" ) ) )
        {
            crossProbeHighlightNet( wxEmptyString );
            break;
        }

        wxString part = extractQuotedParam( payload, wxT( "$PART:" ) );
        wxString pad  = extractQuotedParam( payload, wxT( "$PAD:" ) );
        wxString net  = extractQuotedParam( payload, wxT( "$NET:" ) );

        // Packets we originate ourselves (MBSCH's own fan-out on net
        // highlight) carry an explicit $PROJECT: token. If present,
        // prefer it over the sender frame's PROJECT since the fan-out
        // targets a specific sub-project's path, not MBSCH's own.
        wxString explicitScope = extractQuotedParam( payload, wxT( "$PROJECT:" ) );
        wxString scope         = explicitScope.IsEmpty() ? senderProjectPath() : explicitScope;

        if( !part.IsEmpty() )
            crossProbeHighlightPart( part, pad, scope );

        if( !net.IsEmpty() )
            crossProbeHighlightNet( net );

        break;
    }

    case MAIL_SELECTION:
    case MAIL_SELECTION_FORCE:
    {
        // $SELECT: <focus>,F<ref>,P<ref>/<pin>,... — for MBS we care only
        // about the F<ref> entries (component references). Each is mapped
        // to a SCH_MODULE_BLOCK with matching componentRef + sub-project
        // path so colliding refs across boards don't pick the wrong one.
        size_t start = payload.find( wxT( "$SELECT:" ) );

        if( start == wxString::npos )
            break;

        wxString body = payload.Mid( start + 8 );   // strlen("$SELECT:")

        // Strip trailing `$PROJECT: "..."` scope token (MBSCH stamps
        // this on its own outbound packets) before comma tokenization.
        // We re-read it as the preferred scope source below — it's
        // authoritative when present since MBSCH-originated packets
        // target a specific sub-project by absolute path rather than
        // by sender Prj().
        size_t   scopeCut = body.find( wxT( " $PROJECT:" ) );
        wxString explicitScope;

        if( scopeCut != wxString::npos )
        {
            explicitScope = extractQuotedParam( body, wxT( "$PROJECT:" ) );
            body          = body.Mid( 0, scopeCut );
        }

        wxString scope = explicitScope.IsEmpty() ? senderProjectPath() : explicitScope;

        wxStringTokenizer tok( body, wxT( "," ) );

        // First token is the focus flag (0 or 1) — ignore.
        if( tok.HasMoreTokens() )
            tok.GetNextToken();

        // Recursion guard: selection changes we apply below fire
        // EVENTS::SelectedEvent, which would normally invoke
        // SCH_EDITOR_CONTROL::CrossProbeToPcb and re-broadcast. Setting
        // the sync flag matches what SCH_EDIT_FRAME::KiwayMailIn does
        // around its SyncSelection call. Restore in all exit paths.
        SetSyncingSelection( true );

        bool cleared = false;

        while( tok.HasMoreTokens() )
        {
            wxString spec = tok.GetNextToken().Trim().Trim( false );

            if( spec.IsEmpty() )
                continue;

            if( spec[0] == 'F' )
            {
                if( !cleared )
                {
                    m_toolManager->RunAction( ACTIONS::selectionClear );
                    cleared = true;
                }

                // F<ref> — symbol reference. No per-pin data in this
                // format; fall through to the part-only highlight path.
                crossProbeHighlightPart( spec.Mid( 1 ), wxEmptyString, scope );
            }
            else if( spec[0] == 'P' )
            {
                if( !cleared )
                {
                    m_toolManager->RunAction( ACTIONS::selectionClear );
                    cleared = true;
                }

                // P<ref>/<pinNumber>
                int slash = spec.Find( '/' );

                if( slash == wxNOT_FOUND )
                    continue;

                crossProbeHighlightPart( spec.Mid( 1, slash - 1 ),
                                         spec.Mid( slash + 1 ), scope );
            }
        }

        SetSyncingSelection( false );

        break;
    }

    default:
        SCH_EDIT_FRAME::KiwayMailIn( aEvent );
        break;
    }
}
