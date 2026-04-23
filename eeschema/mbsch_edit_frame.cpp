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

#include <connection_graph.h>
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
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::toggleGrid,     ACTION_MENU::CHECK );
    viewMenu->Add( ACTIONS::gridProperties );
    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showProperties, ACTION_MENU::CHECK );

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
    multi.SaveToFile();

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
                                                const wxString& aPadOrPin )
{
    if( aRef.IsEmpty() )
        return;

    SCH_SCREEN* screen = Schematic().RootScreen();

    if( !screen )
        return;

    SCH_MODULE_BLOCK* matchedBlock = nullptr;
    SCH_MODULE_PIN*   matchedPin   = nullptr;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_MODULE_BLOCK_T )
            continue;

        SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

        if( block->GetComponentRef() != aRef )
            continue;

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

    m_toolManager->RunAction( ACTIONS::selectionClear );

    EDA_ITEM* target = matchedPin ? static_cast<EDA_ITEM*>( matchedPin )
                                  : static_cast<EDA_ITEM*>( matchedBlock );
    selTool->AddItemToSel( target );

    // Re-center the canvas on the highlighted block so the user can see
    // the context immediately; otherwise the highlight may sit off-screen.
    FocusOnItem( target );
    GetCanvas()->Refresh();
}


void MBSCH_EDIT_FRAME::crossProbeHighlightNet( const wxString& aNetName )
{
    // SetHighlightedConnection invokes the standard SCH painter
    // highlight pipeline (including m_highlightedConnChanged flag so
    // the next Refresh repaints items whose resolved net matches).
    // An empty net name clears the highlight.
    SetHighlightedConnection( aNetName );
    GetCanvas()->Refresh();
}


void MBSCH_EDIT_FRAME::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent )
{
    const std::string& payloadStd = aEvent.GetPayload();
    wxString           payload( payloadStd.c_str(), wxConvUTF8 );

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

        if( !part.IsEmpty() )
            crossProbeHighlightPart( part, pad );

        if( !net.IsEmpty() )
            crossProbeHighlightNet( net );

        break;
    }

    case MAIL_SELECTION:
    case MAIL_SELECTION_FORCE:
    {
        // $SELECT: <focus>,F<ref>,P<ref>/<pin>,... — for MBS we care only
        // about the F<ref> entries (component references). Each is mapped
        // to a SCH_MODULE_BLOCK with matching componentRef.
        size_t start = payload.find( wxT( "$SELECT:" ) );

        if( start == wxString::npos )
            break;

        wxString body = payload.Mid( start + 8 );   // strlen("$SELECT:")
        wxStringTokenizer tok( body, wxT( "," ) );

        // First token is the focus flag (0 or 1) — ignore.
        if( tok.HasMoreTokens() )
            tok.GetNextToken();

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
                crossProbeHighlightPart( spec.Mid( 1 ), wxEmptyString );
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
                                         spec.Mid( slash + 1 ) );
            }
        }

        break;
    }

    default:
        SCH_EDIT_FRAME::KiwayMailIn( aEvent );
        break;
    }
}
