/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "panel_3d_assembly.h"

#include <3d_viewer/3d_viewer_assembly.h>
#include <3d_viewer/eda_3d_viewer_frame.h>
#include <board.h>
#include <footprint.h>
#include <project.h>
#include <project/project_file.h>

#include <wx/busyinfo.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/utils.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <wx/valnum.h>


PANEL_3D_ASSEMBLY::PANEL_3D_ASSEMBLY( EDA_3D_VIEWER_FRAME* aParent,
                                       ASSEMBLY_3D_MANAGER* aManager ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aParent ),
        m_manager( aManager ),
        m_selectedBoardIndex( wxNOT_FOUND )
{
    createControls();
    bindEvents();
    RefreshBoardList();
    RefreshMatesTree();
    updateMateButtons();

    // Run collision check on the FIRST idle event after construction —
    // by which point the canvas has painted at least once and the
    // manager's `InitRenderers` has populated `m_instanceAdapters`.
    // Calling earlier (in this ctor) saw null adapters and produced
    // an empty result, leaving collision/contact highlights blank
    // until the user nudged a board.
    Bind( wxEVT_IDLE,
          [this]( wxIdleEvent& aEvent )
          {
              if( m_initialCollisionCheckDone )
                  return;

              // Adapters are populated by the canvas's first paint
              // (which calls ASSEMBLY_3D_MANAGER::InitRenderers). The
              // manager-side `RedrawAll` clears the pending flag once
              // it has run RunCollisionCheck against ready adapters.
              // Mirror that here so the panel's status label refreshes
              // with the correct count, not the empty placeholder.
              if( m_manager && m_manager->IsInitialCollisionCheckPending() )
                  return;

              autoRunCollisionCheck();
              m_initialCollisionCheckDone = true;
          } );
}


PANEL_3D_ASSEMBLY::~PANEL_3D_ASSEMBLY()
{
}


void PANEL_3D_ASSEMBLY::createControls()
{
    // Outer panel just holds a vertical scrolled window — without this
    // the whole stack of static-box sections gets clipped when the AUI
    // pane is shorter than ~600px.
    wxBoxSizer* outerSizer = new wxBoxSizer( wxVERTICAL );

    // Vertical scroll only — narrowing the AUI pane should compress
    // the section column (text fields shrink, labels wrap) rather than
    // clipping horizontally. HSCROLL was producing a horizontal bar
    // and cutting the right-hand side off when the pane went under
    // ~220 px wide.
    m_scrolled = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxVSCROLL );
    m_scrolled->SetScrollRate( 0, 12 );

    outerSizer->Add( m_scrolled, 1, wxEXPAND );
    SetSizer( outerSizer );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // ====================  Boards (visibility) ====================
    wxStaticBoxSizer* boardBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Boards" ) );

    m_boardListBox = new wxCheckListBox( m_scrolled, wxID_ANY, wxDefaultPosition,
                                          wxSize( -1, 120 ) );
    m_boardListBox->SetMinSize( wxSize( FromDIP( 60 ), FromDIP( 80 ) ) );
    m_boardListBox->SetToolTip( _( "Visibility checkboxes per sub-board." ) );
    boardBox->Add( m_boardListBox, 1, wxEXPAND | wxBOTTOM, 5 );

    // wxBU_EXACTFIT trims the platform-default padding so the buttons
    // hit a smaller min width — without it the macOS theme reserves
    // ~80 px even for short labels, forcing the panel to a wider min.
    wxBoxSizer* visButtonSizer = new wxBoxSizer( wxHORIZONTAL );
    m_showAllButton = new wxButton( m_scrolled, wxID_ANY, _( "Show" ),
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_hideAllButton = new wxButton( m_scrolled, wxID_ANY, _( "Hide" ),
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_showAllButton->SetToolTip( _( "Show all sub-boards" ) );
    m_hideAllButton->SetToolTip( _( "Hide all sub-boards" ) );
    visButtonSizer->Add( m_showAllButton, 1, wxRIGHT, 3 );
    visButtonSizer->Add( m_hideAllButton, 1 );
    boardBox->Add( visButtonSizer, 0, wxEXPAND );

    mainSizer->Add( boardBox, 0, wxEXPAND | wxALL, 5 );

    // ====================  Layout ====================
    wxStaticBoxSizer* layoutBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Layout" ) );

    wxBoxSizer* layoutModeSizer = new wxBoxSizer( wxHORIZONTAL );
    layoutModeSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Mode:" ) ), 0,
                          wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_layoutModeChoice = new wxChoice( m_scrolled, wxID_ANY );
    m_layoutModeChoice->Append( _( "Flat (side by side)" ) );
    m_layoutModeChoice->Append( _( "Stacked" ) );
    m_layoutModeChoice->Append( _( "Custom" ) );
    m_layoutModeChoice->SetSelection( 0 );
    layoutModeSizer->Add( m_layoutModeChoice, 1 );
    layoutBox->Add( layoutModeSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    mainSizer->Add( layoutBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // ====================  Position ====================
    // Has its own board picker — independent of the Boards visibility
    // list above. Switching boards in the picker just repaints the X/Y/Z
    // and rotation fields with that board's current state.
    wxStaticBoxSizer* posBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Position" ) );

    wxBoxSizer* posBoardSizer = new wxBoxSizer( wxHORIZONTAL );
    posBoardSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Board:" ) ), 0,
                        wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_positionBoardChoice = new wxChoice( m_scrolled, wxID_ANY );
    m_positionBoardChoice->SetToolTip(
            _( "Pick the sub-board the X/Y/Z and rotation fields below act on." ) );
    posBoardSizer->Add( m_positionBoardChoice, 1 );
    posBox->Add( posBoardSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    // Position XYZ. Text controls get a small explicit min size so the
    // FlexGridSizer can shrink the column when the AUI pane is narrow
    // — without this, wxTextCtrl's platform default (~80 px on macOS)
    // sets a floor the panel can't go below.
    auto makeNumCtrl = [&]( wxTextCtrl** aOut, const wxString& aInit )
    {
        // wxTE_PROCESS_ENTER  → Enter fires wxEVT_TEXT_ENTER (commit).
        // wxWANTS_CHARS       → tells wx to NOT route arrow keys
        //                       through tab-traversal / parent
        //                       handlers. Without this, the parent
        //                       wxScrolledWindow / canvas dispatcher
        //                       chain intercepts unmodified Left/Right
        //                       on macOS before the text widget sees
        //                       them, leaving the cursor stuck.
        *aOut = new wxTextCtrl( m_scrolled, wxID_ANY, aInit, wxDefaultPosition,
                                 wxDefaultSize,
                                 wxTE_PROCESS_ENTER | wxWANTS_CHARS );
        ( *aOut )->SetMinSize( wxSize( FromDIP( 40 ), -1 ) );
    };

    wxFlexGridSizer* posSizer = new wxFlexGridSizer( 3, 3, 3, 5 );
    posSizer->AddGrowableCol( 1, 1 );

    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "X:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_posXCtrl, "0" );
    posSizer->Add( m_posXCtrl, 0, wxEXPAND );
    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "mm" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Y:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_posYCtrl, "0" );
    posSizer->Add( m_posYCtrl, 0, wxEXPAND );
    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "mm" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Z:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_posZCtrl, "0" );
    posSizer->Add( m_posZCtrl, 0, wxEXPAND );
    posSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "mm" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    posBox->Add( posSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    // Rotation XYZ
    posBox->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Rotation:" ) ), 0,
                  wxTOP | wxBOTTOM, 3 );

    wxFlexGridSizer* rotSizer = new wxFlexGridSizer( 3, 3, 3, 5 );
    rotSizer->AddGrowableCol( 1, 1 );

    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "X:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_rotXCtrl, "0" );
    rotSizer->Add( m_rotXCtrl, 0, wxEXPAND );
    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "°" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Y:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_rotYCtrl, "0" );
    rotSizer->Add( m_rotYCtrl, 0, wxEXPAND );
    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "°" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "Z:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
    makeNumCtrl( &m_rotZCtrl, "0" );
    rotSizer->Add( m_rotZCtrl, 0, wxEXPAND );
    rotSizer->Add( new wxStaticText( m_scrolled, wxID_ANY, _( "°" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );

    posBox->Add( rotSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    mainSizer->Add( posBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // ====================  Mates ====================
    // "Mate connectors" toggle now lives here so all the mate state +
    // per-pair actions are grouped. The view toggles below control
    // gizmo visibility separately.
    wxStaticBoxSizer* matesBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Mates" ) );

    m_mateConnectorsButton = new wxButton( m_scrolled, wxID_ANY, _( "Mate connectors" ),
                                            wxDefaultPosition, wxDefaultSize,
                                            wxBU_EXACTFIT );
    m_mateConnectorsButton->SetToolTip(
            _( "Auto-align boards at connector pairs (one-shot)" ) );
    matesBox->Add( m_mateConnectorsButton, 0, wxEXPAND | wxBOTTOM, 4 );

    m_matesTree = new wxTreeCtrl( m_scrolled, wxID_ANY, wxDefaultPosition,
                                   wxSize( -1, 180 ),
                                   wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE );
    m_matesTree->SetMinSize( wxSize( FromDIP( 80 ), FromDIP( 160 ) ) );
    matesBox->Add( m_matesTree, 1, wxEXPAND | wxBOTTOM, 5 );

    // Two-row button layout — single-row would need ~340 px to display
    // all six labels, which collides with narrow AUI panes. Splitting
    // explicitly (rather than relying on wxWrapSizer's runtime wrap)
    // is more predictable across platforms / DPI settings.
    // wxBU_EXACTFIT on every button so the row's natural min-width
    // tracks label width + a few px of padding instead of the macOS
    // default ~80 px floor.
    m_addMateButton      = new wxButton( m_scrolled, wxID_ANY, _( "+ Add" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
    m_editMateButton     = new wxButton( m_scrolled, wxID_ANY, _( "Edit…" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
    // ↑ / ↓ replace the Primary button: top-of-edge pair is treated as
    // primary by the solver, so reorder = priority shift. Reordering an
    // AUTO row promotes it to a CUSTOM override transparently.
    m_moveMateUpButton   = new wxButton( m_scrolled, wxID_ANY, wxT( "↑" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
    m_moveMateDownButton = new wxButton( m_scrolled, wxID_ANY, wxT( "↓" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
    m_disableMateButton  = new wxButton( m_scrolled, wxID_ANY, _( "Disable" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
    m_deleteMateButton   = new wxButton( m_scrolled, wxID_ANY, _( "Delete" ),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );

    m_addMateButton->SetToolTip( _( "Add a custom mate (mounting hole, override, etc.)" ) );
    m_editMateButton->SetToolTip( _( "Edit the selected custom mate (or double-click)" ) );
    m_moveMateUpButton->SetToolTip(
            _( "Raise this pair's priority on its board edge "
               "(top-most pair becomes primary)." ) );
    m_moveMateDownButton->SetToolTip(
            _( "Lower this pair's priority on its board edge." ) );
    m_disableMateButton->SetToolTip( _( "Disable an auto-derived mate" ) );
    m_deleteMateButton->SetToolTip( _( "Delete the selected custom mate" ) );

    wxBoxSizer* matesRow1 = new wxBoxSizer( wxHORIZONTAL );
    matesRow1->Add( m_addMateButton,      1, wxRIGHT, 3 );
    matesRow1->Add( m_editMateButton,     1, wxRIGHT, 3 );
    matesRow1->Add( m_moveMateUpButton,   0, wxRIGHT, 1 );
    matesRow1->Add( m_moveMateDownButton, 0 );

    wxBoxSizer* matesRow2 = new wxBoxSizer( wxHORIZONTAL );
    matesRow2->Add( m_disableMateButton,  1, wxRIGHT, 3 );
    matesRow2->Add( m_deleteMateButton,   1 );

    wxBoxSizer* matesButtonSizer = new wxBoxSizer( wxVERTICAL );
    matesButtonSizer->Add( matesRow1, 0, wxEXPAND | wxBOTTOM, 3 );
    matesButtonSizer->Add( matesRow2, 0, wxEXPAND );

    matesBox->Add( matesButtonSizer, 0, wxEXPAND );

    mainSizer->Add( matesBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // The mate-rendering toggles + tolerance now live on the RHS
    // appearance panel (APPEARANCE_CONTROLS_3D) under the "Mate
    // Rendering" section in MBS mode. Keeping null pointers here so
    // any leftover references that don't reach the new controls
    // don't crash; the bind code below skips them when null.
    m_showMatesCheck         = nullptr;
    m_showPinPairsCheck      = nullptr;
    m_showCollisionsCheck    = nullptr;
    m_showContactsCheck      = nullptr;
    m_collisionThresholdCtrl = nullptr;

    // ====================  Validation ====================
    // Collision check auto-runs on every position change.
    wxStaticBoxSizer* validationBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Validation" ) );

    m_collisionStatusLabel = new wxStaticText( m_scrolled, wxID_ANY, _( "Status: --" ) );
    validationBox->Add( m_collisionStatusLabel, 0 );

    mainSizer->Add( validationBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // ====================  Export ====================
    wxStaticBoxSizer* exportBox =
            new wxStaticBoxSizer( wxVERTICAL, m_scrolled, _( "Export" ) );

    m_exportSTEPButton = new wxButton( m_scrolled, wxID_ANY, _( "Export STEP…" ),
                                        wxDefaultPosition, wxDefaultSize,
                                        wxBU_EXACTFIT );
    m_exportSTEPButton->SetToolTip( _( "Export the assembly to a STEP file" ) );
    exportBox->Add( m_exportSTEPButton, 0, wxEXPAND );

    mainSizer->Add( exportBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    m_scrolled->SetSizer( mainSizer );
    m_scrolled->FitInside();
}


void PANEL_3D_ASSEMBLY::bindEvents()
{
    // The Boards list is visibility-only now; click-to-select moved to
    // the Position section's wxChoice. wxEVT_LISTBOX is no longer
    // bound, only wxEVT_CHECKLISTBOX (which fires on checkbox toggle).
    m_boardListBox->Bind( wxEVT_CHECKLISTBOX, &PANEL_3D_ASSEMBLY::onBoardVisibilityChanged, this );

    m_showAllButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onShowAllBoards, this );
    m_hideAllButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onHideAllBoards, this );

    m_layoutModeChoice->Bind( wxEVT_CHOICE, &PANEL_3D_ASSEMBLY::onLayoutModeChanged, this );

    // Commit position / rotation on Enter OR when the field loses focus
    // (clicking away, tabbing). Without the focus binding, a user who
    // types a number then clicks the canvas never fires the handler.
    //
    // The CHAR_HOOK binding intercepts the keystroke before the 3D
    // canvas's GAL dispatcher swallows it (the dispatcher hooks
    // wxEVT_CHAR_HOOK to capture arrow keys etc., which would
    // otherwise prevent the user from cursor-navigating inside these
    // text fields). On Enter we trigger the handler directly because
    // wxEVT_TEXT_ENTER doesn't reliably fire on macOS when the
    // dispatcher has hooked CHAR_HOOK at the same time.
    auto bindCommit = [this]( wxTextCtrl* aCtrl, void (PANEL_3D_ASSEMBLY::*aHandler)( wxCommandEvent& ) )
    {
        aCtrl->Bind( wxEVT_TEXT_ENTER, aHandler, this );
        aCtrl->Bind( wxEVT_KILL_FOCUS,
                     [this, aHandler]( wxFocusEvent& aEvt )
                     {
                         wxCommandEvent dummy;
                         (this->*aHandler)( dummy );
                         aEvt.Skip();
                     } );
        // Catch Return on CHAR_HOOK so the commit fires even when
        // wxEVT_TEXT_ENTER doesn't propagate (macOS sometimes
        // converts Enter to a command event before the text ctrl's
        // standard handlers).
        aCtrl->Bind( wxEVT_CHAR_HOOK,
                     [this, aHandler]( wxKeyEvent& aEvt )
                     {
                         const int code = aEvt.GetKeyCode();

                         if( code == WXK_RETURN || code == WXK_NUMPAD_ENTER )
                         {
                             wxCommandEvent dummy;
                             (this->*aHandler)( dummy );
                             return;
                         }

                         aEvt.Skip();
                     } );

        // wxOSX delivers Left arrow only as wxEVT_CHAR (no CHAR_HOOK,
        // no KEY_DOWN), and the native cursor-movement command path
        // doesn't fire for it on this control combination — Right
        // moves natively via KEY_DOWN, Left needs an explicit handler
        // here to actually advance the insertion point. We drive the
        // cursor manually for unmodified arrow / Home / End on the
        // CHAR event and consume it so no double-move can happen.
        // Other characters (typing) still Skip() so the text ctrl's
        // native insert-text path runs.
        aCtrl->Bind( wxEVT_CHAR, [aCtrl]( wxKeyEvent& aEvt )
        {
            const int code = aEvt.GetKeyCode();

            if( !aEvt.HasModifiers() && !aEvt.ShiftDown() )
            {
                const long pos = aCtrl->GetInsertionPoint();
                const long len = aCtrl->GetLastPosition();

                if( code == WXK_LEFT )
                {
                    if( pos > 0 )
                        aCtrl->SetInsertionPoint( pos - 1 );
                    return;     // consumed
                }
                if( code == WXK_RIGHT )
                {
                    if( pos < len )
                        aCtrl->SetInsertionPoint( pos + 1 );
                    return;
                }
                if( code == WXK_HOME )
                {
                    aCtrl->SetInsertionPoint( 0 );
                    return;
                }
                if( code == WXK_END )
                {
                    aCtrl->SetInsertionPointEnd();
                    return;
                }
            }

            aEvt.Skip();
        } );
    };

    bindCommit( m_posXCtrl, &PANEL_3D_ASSEMBLY::onPositionChanged );
    bindCommit( m_posYCtrl, &PANEL_3D_ASSEMBLY::onPositionChanged );
    bindCommit( m_posZCtrl, &PANEL_3D_ASSEMBLY::onPositionChanged );

    bindCommit( m_rotXCtrl, &PANEL_3D_ASSEMBLY::onRotationChanged );
    bindCommit( m_rotYCtrl, &PANEL_3D_ASSEMBLY::onRotationChanged );
    bindCommit( m_rotZCtrl, &PANEL_3D_ASSEMBLY::onRotationChanged );

    m_positionBoardChoice->Bind( wxEVT_CHOICE,
                                 &PANEL_3D_ASSEMBLY::onPositionBoardChoice, this );

    m_mateConnectorsButton->Bind( wxEVT_BUTTON,
                                   &PANEL_3D_ASSEMBLY::onMateConnectors, this );
    m_exportSTEPButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onExportSTEP, this );

    // M6.D-phase-2 mates UI bindings
    m_matesTree->Bind( wxEVT_TREE_SEL_CHANGED,
                       &PANEL_3D_ASSEMBLY::onMateTreeSelectionChanged, this );
    m_matesTree->Bind( wxEVT_TREE_ITEM_ACTIVATED,
                       &PANEL_3D_ASSEMBLY::onMateTreeActivated, this );
    m_addMateButton->Bind(      wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onAddCustomMate,    this );
    m_editMateButton->Bind(     wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onEditCustomMate,   this );
    m_moveMateUpButton->Bind(   wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onMoveMateUp,       this );
    m_moveMateDownButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onMoveMateDown,     this );
    m_disableMateButton->Bind(  wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onDisableMate,      this );
    m_deleteMateButton->Bind(   wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onDeleteCustomMate, this );

    // Mate-rendering view toggles + tolerance moved to RHS appearance
    // panel; widgets are nullptr here so no bind necessary.
}


void PANEL_3D_ASSEMBLY::RefreshBoardList()
{
    m_boardListBox->Clear();
    m_positionBoardChoice->Clear();
    m_boardUuids.clear();

    if( !m_manager )
    {
        m_selectedBoardIndex = wxNOT_FOUND;
        UpdateSelectedBoardControls();
        return;
    }

    const auto& instances = m_manager->GetBoardInstances();

    for( const auto& inst : instances )
    {
        int index = m_boardListBox->Append( inst.displayName );
        m_boardListBox->Check( index, inst.visible );
        m_positionBoardChoice->Append( inst.displayName );
        m_boardUuids.push_back( inst.uuid );
    }

    // Default the Position picker to the first board (if any) so the
    // X/Y/Z + rotation fields show meaningful values immediately
    // instead of "Select a board" placeholder.
    if( !instances.empty() )
    {
        m_positionBoardChoice->SetSelection( 0 );
        m_selectedBoardIndex = 0;
    }
    else
    {
        m_selectedBoardIndex = wxNOT_FOUND;
    }

    // If the project has persisted per-instance poses, the layout is
    // necessarily a user-arranged "Custom" — not the FLAT default. Sync
    // the dropdown so reopening a saved project doesn't lie about the
    // current arrangement.
    if( m_manager && m_manager->HasPersistedInstanceStates() )
        m_layoutModeChoice->SetSelection( 2 );    // Custom

    UpdateSelectedBoardControls();
}


void PANEL_3D_ASSEMBLY::UpdateSelectedBoardControls()
{
    if( m_selectedBoardIndex == wxNOT_FOUND || !m_manager )
    {
        m_posXCtrl->SetValue( "0" );
        m_posYCtrl->SetValue( "0" );
        m_posZCtrl->SetValue( "0" );
        m_rotXCtrl->SetValue( "0" );
        m_rotYCtrl->SetValue( "0" );
        m_rotZCtrl->SetValue( "0" );
        return;
    }

    KIID uuid = m_boardUuids[m_selectedBoardIndex];
    const BOARD_3D_INSTANCE* inst = m_manager->GetBoardInstance( uuid );

    if( !inst )
        return;

    m_posXCtrl->SetValue( wxString::Format( "%.2f", inst->position.x ) );
    m_posYCtrl->SetValue( wxString::Format( "%.2f", inst->position.y ) );
    m_posZCtrl->SetValue( wxString::Format( "%.2f", inst->position.z ) );

    m_rotXCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.x ) );
    m_rotYCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.y ) );
    m_rotZCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.z ) );
}


KIID PANEL_3D_ASSEMBLY::GetSelectedBoardUuid() const
{
    if( m_selectedBoardIndex == wxNOT_FOUND ||
        m_selectedBoardIndex >= static_cast<int>( m_boardUuids.size() ) )
        return KIID();

    return m_boardUuids[m_selectedBoardIndex];
}


void PANEL_3D_ASSEMBLY::onBoardSelected( wxCommandEvent& aEvent )
{
    // Vestigial: m_boardListBox no longer fires wxEVT_LISTBOX (it's
    // visibility-only now). Selection moved to onPositionBoardChoice.
    (void) aEvent;
}


void PANEL_3D_ASSEMBLY::onPositionBoardChoice( wxCommandEvent& aEvent )
{
    int sel = m_positionBoardChoice->GetSelection();

    if( sel < 0 || sel >= static_cast<int>( m_boardUuids.size() ) )
        return;

    m_selectedBoardIndex = sel;
    UpdateSelectedBoardControls();

    // Drive the frame's "active instance" so cross-probe (selection
    // events from a sub-project schematic / pcbnew) still has something
    // to focus on.
    if( m_frame )
        m_frame->SetActiveAssemblyInstance( m_boardUuids[sel] );
}


void PANEL_3D_ASSEMBLY::onBoardVisibilityChanged( wxCommandEvent& aEvent )
{
    int index = aEvent.GetInt();
    if( index < 0 || index >= static_cast<int>( m_boardUuids.size() ) || !m_manager )
        return;

    bool visible = m_boardListBox->IsChecked( index );
    m_manager->SetBoardVisible( m_boardUuids[index], visible );

    autoRunCollisionCheck();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onLayoutModeChanged( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    int selection = m_layoutModeChoice->GetSelection();
    BOARD_LAYOUT_MODE mode = BOARD_LAYOUT_MODE::FLAT;

    if( selection == 1 )
        mode = BOARD_LAYOUT_MODE::STACKED;
    else if( selection == 2 )
        mode = BOARD_LAYOUT_MODE::CUSTOM;

    m_manager->ArrangeBoards( mode, 20.0f );
    m_manager->PersistAllInstances();
    UpdateSelectedBoardControls();
    autoRunCollisionCheck();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onPositionChanged( wxCommandEvent& aEvent )
{
    if( m_selectedBoardIndex == wxNOT_FOUND || !m_manager )
        return;

    double x, y, z;
    if( !m_posXCtrl->GetValue().ToDouble( &x ) ||
        !m_posYCtrl->GetValue().ToDouble( &y ) ||
        !m_posZCtrl->GetValue().ToDouble( &z ) )
        return;

    KIID uuid = m_boardUuids[m_selectedBoardIndex];
    m_manager->SetBoardPosition( uuid, SFVEC3F( static_cast<float>( x ),
                                                 static_cast<float>( y ),
                                                 static_cast<float>( z ) ) );
    autoRunCollisionCheck();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onRotationChanged( wxCommandEvent& aEvent )
{
    if( m_selectedBoardIndex == wxNOT_FOUND || !m_manager )
        return;

    double x, y, z;
    if( !m_rotXCtrl->GetValue().ToDouble( &x ) ||
        !m_rotYCtrl->GetValue().ToDouble( &y ) ||
        !m_rotZCtrl->GetValue().ToDouble( &z ) )
        return;

    KIID uuid = m_boardUuids[m_selectedBoardIndex];
    m_manager->SetBoardRotation( uuid, SFVEC3F( static_cast<float>( x ),
                                                 static_cast<float>( y ),
                                                 static_cast<float>( z ) ) );
    autoRunCollisionCheck();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onMateConnectors( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxLogMessage( wxT( "[MATE] onMateConnectors fired (button), manager=%p" ),
                  static_cast<void*>( m_manager ) );

    m_manager->MateConnectors();
    m_layoutModeChoice->SetSelection( 2 );  // Custom

    UpdateSelectedBoardControls();
    autoRunCollisionCheck();
    refresh3DView();
    RefreshMatesTree();
}


void PANEL_3D_ASSEMBLY::autoRunCollisionCheck()
{
    if( !m_manager )
        return;

    m_manager->RunCollisionCheck();
    updateCollisionStatus();
}




void PANEL_3D_ASSEMBLY::onExportSTEP( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxFileDialog dlg( this, _( "Export Assembly STEP" ), wxEmptyString,
                      "assembly.step",
                      _( "STEP files" ) + " (*.step;*.stp)|*.step;*.stp",
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return;

    bool ok;

    {
        // STEP export drives the per-board OCCT exporter once per visible
        // sub-board, then re-reads each output to compose the final
        // compound. Each per-board step takes seconds-to-minutes; the
        // operation is synchronous on the UI thread, so without an
        // explicit busy indicator the app appears frozen. wxBusyInfo
        // keeps a modal "Exporting..." panel up while the work runs and
        // changes the cursor; events fire normally inside the export
        // (the assembly_step exporter calls wxYield between boards) so
        // the busy panel remains responsive.
        wxBusyCursor busyCursor;
        wxBusyInfo   busyInfo(
                _( "Exporting assembly to STEP — this may take a few minutes..." ), this );
        wxYield();

        ok = m_manager->ExportAssemblySTEP( dlg.GetPath() );
    }

    if( ok )
    {
        wxMessageBox( _( "Assembly exported successfully." ), _( "Export" ),
                      wxOK | wxICON_INFORMATION, this );
    }
    else
    {
        wxMessageBox( _( "Failed to export assembly." ), _( "Export Error" ),
                      wxOK | wxICON_ERROR, this );
    }
}


void PANEL_3D_ASSEMBLY::onShowAllBoards( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    m_manager->ShowAllBoards();

    for( unsigned int i = 0; i < m_boardListBox->GetCount(); i++ )
        m_boardListBox->Check( i, true );

    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onHideAllBoards( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    m_manager->HideAllBoards();

    for( unsigned int i = 0; i < m_boardListBox->GetCount(); i++ )
        m_boardListBox->Check( i, false );

    refresh3DView();
}


void PANEL_3D_ASSEMBLY::refresh3DView()
{
    if( m_frame )
        m_frame->NewDisplay( true );
}


void PANEL_3D_ASSEMBLY::updatePositionControls()
{
    UpdateSelectedBoardControls();
}


void PANEL_3D_ASSEMBLY::updateCollisionStatus()
{
    if( !m_manager )
        return;

    const auto& collisions = m_manager->GetLastCollisions();
    const auto& residuals  = m_manager->GetMateResiduals();

    // Surface both unintended overlaps (collisions) and outlier pin
    // pairs (MOON-1364: dropped by the robust mate solver) in the
    // single status line so the user can spot either kind of issue at
    // a glance. Pin residuals are warnings — they don't block placement
    // (the mate placed using the inlier set) but they flag bad port
    // mappings that the user should fix in the schematic.
    wxString label;
    wxColour color;

    if( collisions.empty() && residuals.empty() )
    {
        label = _( "Status: No collisions" );
        color = wxColour( 0, 128, 0 );    // Green
    }
    else if( !collisions.empty() && residuals.empty() )
    {
        label = wxString::Format( _( "Status: %zu collision(s) found" ),
                                   collisions.size() );
        color = wxColour( 200, 0, 0 );    // Red
    }
    else if( collisions.empty() && !residuals.empty() )
    {
        label = wxString::Format( _( "Status: %zu pin alignment warning(s)" ),
                                   residuals.size() );
        color = wxColour( 200, 130, 0 );  // Orange
    }
    else
    {
        label = wxString::Format( _( "Status: %zu collision(s), %zu pin warning(s)" ),
                                   collisions.size(), residuals.size() );
        color = wxColour( 200, 0, 0 );    // Red dominates
    }

    m_collisionStatusLabel->SetLabel( label );
    m_collisionStatusLabel->SetForegroundColour( color );
}


// ========== M6.D-phase-2 mates UI ==========

namespace
{

const char* roleLabel( CUSTOM_MATE_ROLE aRole )
{
    switch( aRole )
    {
    case CUSTOM_MATE_ROLE::PRIMARY:   return "PRIMARY";
    case CUSTOM_MATE_ROLE::SECONDARY: return "SECONDARY";
    case CUSTOM_MATE_ROLE::DISABLED:  return "DISABLED";
    }
    return "?";
}


const char* typeLabel( CUSTOM_MATE_TYPE aType )
{
    switch( aType )
    {
    case CUSTOM_MATE_TYPE::CONNECTOR:     return "connector";
    case CUSTOM_MATE_TYPE::MOUNTING_HOLE: return "mounting hole";
    case CUSTOM_MATE_TYPE::ALIGNMENT:     return "alignment";
    }
    return "?";
}

} // namespace


void PANEL_3D_ASSEMBLY::RefreshMatesTree()
{
    m_mateTreeRows.clear();
    m_matesTree->DeleteAllItems();

    wxTreeItemId root = m_matesTree->AddRoot( "" );

    if( !m_manager )
        return;

    // Map instance UUID → display name once so the edge headers can
    // read "MAIN ↔ IO" without re-walking m_boardInstances per pair.
    std::map<KIID, wxString> nameByInstance;

    for( const BOARD_3D_INSTANCE& inst : m_manager->GetBoardInstances() )
        nameByInstance[inst.uuid] = inst.displayName;

    auto displayName = [&]( const KIID& uuid ) -> wxString
    {
        auto it = nameByInstance.find( uuid );
        return ( it != nameByInstance.end() ) ? it->second : uuid.AsString();
    };

    std::vector<MATE_EDGE> edges = m_manager->BuildMateGraph();

    wxLogMessage( wxT( "[PANEL] RefreshMatesTree: %zu edges from BuildMateGraph" ),
                  edges.size() );

    for( const MATE_EDGE& edge : edges )
    {
        wxString edgeLabel =
                wxString::Format( "%s  ↔  %s   [%d pin(s) total]",
                                  displayName( edge.instanceA ),
                                  displayName( edge.instanceB ),
                                  edge.totalWeight );

        wxTreeItemId edgeNode = m_matesTree->AppendItem( root, edgeLabel );

        MATE_TREE_ROW edgeMeta{};
        edgeMeta.isEdgeNode = true;
        edgeMeta.instanceA  = edge.instanceA;
        edgeMeta.instanceB  = edge.instanceB;
        m_mateTreeRows[edgeNode.GetID()] = edgeMeta;

        // BuildMateGraph sorted edge.pairs: head = primary,
        // alignmentOnly pairs sink to the bottom. The first non-
        // alignmentOnly pair is the primary the solver will use.
        const MATE_PAIR* primary = nullptr;

        for( const MATE_PAIR& p : edge.pairs )
        {
            if( !p.alignmentOnly )
            {
                primary = &p;
                break;
            }
        }

        for( const MATE_PAIR& pair : edge.pairs )
        {
            wxString badge;

            if( pair.disabled )
                badge << "[DISABLED]";
            else if( &pair == primary )
                badge << "[PRIMARY]";
            else if( pair.alignmentOnly )
                badge << "[SECONDARY]";

            // A pair tagged with a CUSTOM_MATE uuid is "custom" only
            // when its presence comes from that override (PRIMARY /
            // SECONDARY decoration on top of an auto pair, OR a pure
            // user-added mate). DISABLED rows decorate an auto pair —
            // the row should still read as AUTO (the override is just
            // a suppression flag, not a user-authored mate).
            const bool isCustom = !( pair.customMateUuid == KIID( 0 ) ) && !pair.disabled;

            if( pair.disabled )
                badge << " [AUTO]";
            else if( isCustom )
                badge << " [CUSTOM]";
            else
                badge << " [AUTO]";

            wxString label = wxString::Format( "%s — %s   %d pin(s)  %s",
                                               pair.footprintRefA,
                                               pair.footprintRefB,
                                               pair.pinCount,
                                               badge );

            wxTreeItemId pairNode = m_matesTree->AppendItem( edgeNode, label );

            // Visual cue: grey the disabled rows so the user can see
            // at a glance that they're suppressed without having to
            // read the badge.
            if( pair.disabled )
            {
                m_matesTree->SetItemTextColour( pairNode,
                        wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
            }

            MATE_TREE_ROW row{};
            row.isEdgeNode     = false;
            row.isAuto         = !isCustom;
            row.disabled       = pair.disabled;
            row.customMateUuid = pair.customMateUuid;
            row.instanceA      = pair.instanceA;
            row.instanceB      = pair.instanceB;
            row.footprintRefA  = pair.footprintRefA;
            row.footprintRefB  = pair.footprintRefB;
            m_mateTreeRows[pairNode.GetID()] = row;
        }

        m_matesTree->Expand( edgeNode );
    }
}


void PANEL_3D_ASSEMBLY::selectMatePairRow( const wxString& aPairId )
{
    if( aPairId.IsEmpty() || !m_matesTree )
        return;

    for( const auto& [id, row] : m_mateTreeRows )
    {
        if( row.isEdgeNode )
            continue;

        wxString thisId = ASSEMBLY_3D_MANAGER::MakeMatePairId(
                row.instanceA, row.footprintRefA,
                row.instanceB, row.footprintRefB );

        if( thisId == aPairId )
        {
            m_matesTree->SelectItem( wxTreeItemId( id ) );
            return;
        }
    }
}


void PANEL_3D_ASSEMBLY::updateMateButtons()
{
    bool haveSelection = false;
    bool isCustomLeaf  = false;
    bool isAutoLeaf    = false;

    wxTreeItemId sel = m_matesTree->GetSelection();

    if( sel.IsOk() )
    {
        auto it = m_mateTreeRows.find( sel.GetID() );

        if( it != m_mateTreeRows.end() && !it->second.isEdgeNode )
        {
            haveSelection = true;
            isCustomLeaf  = !it->second.isAuto;
            isAutoLeaf    = it->second.isAuto;
        }
    }

    bool isDisabled = false;

    if( sel.IsOk() )
    {
        auto it = m_mateTreeRows.find( sel.GetID() );

        if( it != m_mateTreeRows.end() && !it->second.isEdgeNode )
            isDisabled = it->second.disabled;
    }

    // Add is always available when a project is loaded.
    m_addMateButton->Enable( m_manager && m_manager->GetBoardInstances().size() >= 2 );

    // Edit only applies to existing CUSTOM rows — AUTO rows have no
    // CUSTOM_MATE record to edit (they're derived from netlist
    // topology). Greyed out on AUTO so the user isn't tempted to
    // expect a no-op edit dialog. (Use Disable to override an AUTO.)
    // Disabled rows are also non-editable — user re-enables first.
    m_editMateButton->Enable( isCustomLeaf && !isDisabled );

    // Up/Down reorder pairs within their edge — head of the edge is
    // the primary. Enabled on any leaf; the manager no-ops when at
    // top/bottom so we don't need to compute index here. Disabled
    // pairs sit out of primary selection so reordering is a no-op.
    m_moveMateUpButton->Enable( haveSelection && !isDisabled );
    m_moveMateDownButton->Enable( haveSelection && !isDisabled );

    // Disable doubles as Enable when the row is already disabled —
    // toggle semantics. Label flips so the user knows which way it
    // goes.
    m_disableMateButton->Enable( haveSelection );
    m_disableMateButton->SetLabel( isDisabled ? _( "Enable" ) : _( "Disable" ) );

    // Delete only on CUSTOM rows (AUTO can't be deleted, only disabled).
    m_deleteMateButton->Enable( isCustomLeaf && !isDisabled );

    (void) isAutoLeaf;   // currently no UI state branches on this; kept for clarity
}


void PANEL_3D_ASSEMBLY::onMateTreeSelectionChanged( wxTreeEvent& aEvent )
{
    updateMateButtons();

    // Drive the 3D mate-gizmo highlight. Both auto and custom rows
    // get a stable id (canonical-form string) that matches what
    // ASSEMBLY_3D_MANAGER computes for each gizmo entry — so either
    // kind highlights consistently.
    if( m_manager )
    {
        wxTreeItemId sel = m_matesTree->GetSelection();
        auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

        if( it != m_mateTreeRows.end() && !it->second.isEdgeNode )
        {
            m_manager->SetSelectedMatePair(
                    ASSEMBLY_3D_MANAGER::MakeMatePairId( it->second.instanceA,
                                                         it->second.footprintRefA,
                                                         it->second.instanceB,
                                                         it->second.footprintRefB ) );
        }
        else
        {
            m_manager->SetSelectedMatePair( wxEmptyString );
        }

        refresh3DView();
    }

    aEvent.Skip();
}


// Modal dialog: pick board A → footprint A → board B → footprint B,
// pick mate type, pick role. Returns the populated CUSTOM_MATE on OK.
namespace
{

class ADD_CUSTOM_MATE_DIALOG : public wxDialog
{
public:
    ADD_CUSTOM_MATE_DIALOG( wxWindow* aParent, ASSEMBLY_3D_MANAGER* aManager,
                            const CUSTOM_MATE* aPrefill = nullptr ) :
            wxDialog( aParent, wxID_ANY,
                      aPrefill ? _( "Edit Custom Mate" ) : _( "Add Custom Mate" ),
                      wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_manager( aManager )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        auto addRow = [&]( const wxString& aLabel, wxChoice*& aCtrl )
        {
            wxBoxSizer* row = new wxBoxSizer( wxHORIZONTAL );
            row->Add( new wxStaticText( this, wxID_ANY, aLabel,
                                        wxDefaultPosition, wxSize( 110, -1 ) ),
                      0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
            aCtrl = new wxChoice( this, wxID_ANY );
            row->Add( aCtrl, 1 );
            main->Add( row, 0, wxEXPAND | wxALL, 4 );
        };

        addRow( _( "Board A:" ),     m_boardA );
        addRow( _( "Footprint A:" ), m_fpA );
        addRow( _( "Board B:" ),     m_boardB );
        addRow( _( "Footprint B:" ), m_fpB );
        addRow( _( "Type:" ),        m_typeChoice );
        addRow( _( "Role:" ),        m_roleChoice );

        m_typeChoice->Append( _( "Connector" ) );
        m_typeChoice->Append( _( "Mounting hole" ) );
        m_typeChoice->Append( _( "Alignment" ) );
        m_typeChoice->SetSelection( 0 );

        m_roleChoice->Append( _( "Primary" ) );
        m_roleChoice->Append( _( "Secondary (alignment check)" ) );
        m_roleChoice->Append( _( "Disabled" ) );
        m_roleChoice->SetSelection( 0 );

        for( const BOARD_3D_INSTANCE& inst : m_manager->GetBoardInstances() )
        {
            m_boardA->Append( inst.displayName );
            m_boardB->Append( inst.displayName );
            m_instances.push_back( &inst );
        }

        if( m_instances.size() >= 1 )
            m_boardA->SetSelection( 0 );

        if( m_instances.size() >= 2 )
            m_boardB->SetSelection( 1 );

        // Edit mode: pre-select the boards / footprints / role / type
        // matching the existing custom mate so the user can modify a
        // single field instead of re-creating the row.
        if( aPrefill )
        {
            for( size_t i = 0; i < m_instances.size(); i++ )
            {
                if( m_instances[i]->subProjectUuid == aPrefill->endA.subProjectUuid )
                    m_boardA->SetSelection( static_cast<int>( i ) );

                if( m_instances[i]->subProjectUuid == aPrefill->endB.subProjectUuid )
                    m_boardB->SetSelection( static_cast<int>( i ) );
            }

            switch( aPrefill->type )
            {
            case CUSTOM_MATE_TYPE::MOUNTING_HOLE: m_typeChoice->SetSelection( 1 ); break;
            case CUSTOM_MATE_TYPE::ALIGNMENT:     m_typeChoice->SetSelection( 2 ); break;
            default:                              m_typeChoice->SetSelection( 0 ); break;
            }

            switch( aPrefill->role )
            {
            case CUSTOM_MATE_ROLE::SECONDARY: m_roleChoice->SetSelection( 1 ); break;
            case CUSTOM_MATE_ROLE::DISABLED:  m_roleChoice->SetSelection( 2 ); break;
            default:                          m_roleChoice->SetSelection( 0 ); break;
            }
        }

        rebuildFootprintList( m_boardA, m_fpA );
        rebuildFootprintList( m_boardB, m_fpB );

        if( aPrefill )
        {
            // Footprint refs need to be selected after the lists are
            // populated for each board's current selection.
            int idxFpA = m_fpA->FindString( aPrefill->endA.footprintRef );
            int idxFpB = m_fpB->FindString( aPrefill->endB.footprintRef );

            if( idxFpA != wxNOT_FOUND )
                m_fpA->SetSelection( idxFpA );

            if( idxFpB != wxNOT_FOUND )
                m_fpB->SetSelection( idxFpB );
        }

        m_boardA->Bind( wxEVT_CHOICE,
                        [this]( wxCommandEvent& )
                        { rebuildFootprintList( m_boardA, m_fpA ); } );

        m_boardB->Bind( wxEVT_CHOICE,
                        [this]( wxCommandEvent& )
                        { rebuildFootprintList( m_boardB, m_fpB ); } );

        wxSizer* btns = CreateButtonSizer( wxOK | wxCANCEL );

        if( btns )
            main->Add( btns, 0, wxEXPAND | wxALL, 8 );

        SetSizerAndFit( main );
        SetMinSize( wxSize( 360, -1 ) );
    }

    bool BuildResult( CUSTOM_MATE& aOut ) const
    {
        int aIdx = m_boardA->GetSelection();
        int bIdx = m_boardB->GetSelection();
        int fpAIdx = m_fpA->GetSelection();
        int fpBIdx = m_fpB->GetSelection();

        if( aIdx < 0 || bIdx < 0 || fpAIdx < 0 || fpBIdx < 0 )
            return false;

        if( aIdx == bIdx )
            return false;   // same board — not a cross-board mate

        const BOARD_3D_INSTANCE* iA = m_instances[aIdx];
        const BOARD_3D_INSTANCE* iB = m_instances[bIdx];

        aOut.endA.subProjectUuid = iA->subProjectUuid;
        aOut.endA.footprintRef   = m_fpA->GetString( fpAIdx );
        aOut.endB.subProjectUuid = iB->subProjectUuid;
        aOut.endB.footprintRef   = m_fpB->GetString( fpBIdx );

        switch( m_typeChoice->GetSelection() )
        {
        case 1:  aOut.type = CUSTOM_MATE_TYPE::MOUNTING_HOLE; break;
        case 2:  aOut.type = CUSTOM_MATE_TYPE::ALIGNMENT;     break;
        default: aOut.type = CUSTOM_MATE_TYPE::CONNECTOR;     break;
        }

        switch( m_roleChoice->GetSelection() )
        {
        case 1:  aOut.role = CUSTOM_MATE_ROLE::SECONDARY; break;
        case 2:  aOut.role = CUSTOM_MATE_ROLE::DISABLED;  break;
        default: aOut.role = CUSTOM_MATE_ROLE::PRIMARY;   break;
        }

        return true;
    }

private:
    void rebuildFootprintList( wxChoice* aBoardChoice, wxChoice* aFpChoice )
    {
        aFpChoice->Clear();

        int idx = aBoardChoice->GetSelection();

        if( idx < 0 || idx >= static_cast<int>( m_instances.size() ) )
            return;

        BOARD* board = m_instances[idx]->board.get();

        if( !board )
            return;

        // Footprint list is large; sort by reference for usability.
        std::vector<wxString> refs;

        for( FOOTPRINT* fp : board->Footprints() )
            refs.push_back( fp->GetReference() );

        std::sort( refs.begin(), refs.end() );

        for( const wxString& r : refs )
            aFpChoice->Append( r );

        if( !refs.empty() )
            aFpChoice->SetSelection( 0 );
    }

    ASSEMBLY_3D_MANAGER*                  m_manager;
    std::vector<const BOARD_3D_INSTANCE*> m_instances;

    wxChoice* m_boardA     = nullptr;
    wxChoice* m_fpA        = nullptr;
    wxChoice* m_boardB     = nullptr;
    wxChoice* m_fpB        = nullptr;
    wxChoice* m_typeChoice = nullptr;
    wxChoice* m_roleChoice = nullptr;
};

} // namespace


void PANEL_3D_ASSEMBLY::onAddCustomMate( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    ADD_CUSTOM_MATE_DIALOG dlg( this, m_manager );

    if( dlg.ShowModal() != wxID_OK )
        return;

    CUSTOM_MATE mate;

    if( !dlg.BuildResult( mate ) )
    {
        wxMessageBox( _( "Pick two distinct boards and a footprint on each." ),
                      _( "Add Custom Mate" ), wxOK | wxICON_WARNING, this );
        return;
    }

    KIID newUuid = m_manager->AddCustomMate( mate );

    if( newUuid == KIID( 0 ) )
    {
        wxMessageBox( _( "Couldn't add the custom mate. Is the project a multi-board container?" ),
                      _( "Add Custom Mate" ), wxOK | wxICON_ERROR, this );
        return;
    }

    // Re-run the mate solver so the new pose takes effect immediately
    // when the user already has "Mate connectors" enabled. Otherwise
    // the change just lands in the persisted list and applies on next
    // toggle — same as auto-derived behaviour.
    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    updateMateButtons();
    UpdateSelectedBoardControls();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onEditCustomMate( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    const MATE_TREE_ROW& row = it->second;

    // For auto-derived rows, build a prefill record on the fly from the
    // tree row metadata so the dialog opens with the same endpoints.
    // On OK we add it as a NEW custom mate (auto pairs aren't user
    // data, so editing them really means "create an override").
    CUSTOM_MATE        prefillStorage;
    const CUSTOM_MATE* prefill   = nullptr;
    KIID               targetUuid;

    if( row.isAuto )
    {
        prefillStorage.role = CUSTOM_MATE_ROLE::PRIMARY;
        prefillStorage.type = CUSTOM_MATE_TYPE::CONNECTOR;

        if( const BOARD_3D_INSTANCE* iA = m_manager->GetBoardInstance( row.instanceA ) )
            prefillStorage.endA.subProjectUuid = iA->subProjectUuid;

        if( const BOARD_3D_INSTANCE* iB = m_manager->GetBoardInstance( row.instanceB ) )
            prefillStorage.endB.subProjectUuid = iB->subProjectUuid;

        prefillStorage.endA.footprintRef = row.footprintRefA;
        prefillStorage.endB.footprintRef = row.footprintRefB;
        prefill = &prefillStorage;
    }
    else
    {
        targetUuid = row.customMateUuid;
        const std::vector<CUSTOM_MATE>& mates = m_manager->GetCustomMates();
        auto cm = std::find_if( mates.begin(), mates.end(),
                                [&]( const CUSTOM_MATE& m ) { return m.uuid == targetUuid; } );

        if( cm == mates.end() )
            return;

        prefillStorage = *cm;
        prefill        = &prefillStorage;
    }

    ADD_CUSTOM_MATE_DIALOG dlg( this, m_manager, prefill );

    if( dlg.ShowModal() != wxID_OK )
        return;

    CUSTOM_MATE updated;

    if( !dlg.BuildResult( updated ) )
    {
        wxMessageBox( _( "Pick two distinct boards and a footprint on each." ),
                      _( "Edit Custom Mate" ), wxOK | wxICON_WARNING, this );
        return;
    }

    if( row.isAuto )
    {
        // Promote auto → custom by adding a fresh override row. The
        // dialog returned a default-UUID record; AddCustomMate assigns
        // a new UUID and stores it.
        m_manager->AddCustomMate( updated );
    }
    else
    {
        // Preserve the original UUID so the mate stays the same row.
        updated.uuid = targetUuid;

        if( !m_manager->UpdateCustomMate( updated ) )
        {
            wxMessageBox( _( "Couldn't update the custom mate." ),
                          _( "Edit Custom Mate" ), wxOK | wxICON_ERROR, this );
            return;
        }
    }

    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    updateMateButtons();
    UpdateSelectedBoardControls();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onMateTreeActivated( wxTreeEvent& aEvent )
{
    // Double-click activate routes to Edit on any leaf row. Edge
    // headers ignore activation — there's nothing to edit on the
    // group node itself. AUTO rows promote to a fresh CUSTOM override
    // when the dialog returns.
    auto it = m_mateTreeRows.find( aEvent.GetItem().IsOk() ? aEvent.GetItem().GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    wxCommandEvent dummy;
    onEditCustomMate( dummy );
}


void PANEL_3D_ASSEMBLY::onMoveMateUp( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    const MATE_TREE_ROW& row = it->second;
    wxString             pairId = ASSEMBLY_3D_MANAGER::MakeMatePairId(
            row.instanceA, row.footprintRefA,
            row.instanceB, row.footprintRefB );

    m_manager->ShiftPairUp( pairId );

    // Re-solve so the head-of-edge change drives a new pose.
    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    selectMatePairRow( pairId );
    updateMateButtons();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onMoveMateDown( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    const MATE_TREE_ROW& row = it->second;
    wxString             pairId = ASSEMBLY_3D_MANAGER::MakeMatePairId(
            row.instanceA, row.footprintRefA,
            row.instanceB, row.footprintRefB );

    m_manager->ShiftPairDown( pairId );

    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    selectMatePairRow( pairId );
    updateMateButtons();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onDisableMate( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    const MATE_TREE_ROW& row = it->second;

    if( row.disabled )
    {
        // Re-enable: find the existing CUSTOM_MATE with role=DISABLED
        // for this pair and remove it. Without the override, the next
        // BuildMateGraph re-derives the auto pair as active.
        const std::vector<CUSTOM_MATE>& mates = m_manager->GetCustomMates();
        const BOARD_3D_INSTANCE*        iA    = m_manager->GetBoardInstance( row.instanceA );
        const BOARD_3D_INSTANCE*        iB    = m_manager->GetBoardInstance( row.instanceB );

        if( !iA || !iB )
            return;

        for( const CUSTOM_MATE& cm : mates )
        {
            if( cm.role != CUSTOM_MATE_ROLE::DISABLED )
                continue;

            const bool same =
                    ( cm.endA.subProjectUuid == iA->subProjectUuid
                      && cm.endA.footprintRef == row.footprintRefA
                      && cm.endB.subProjectUuid == iB->subProjectUuid
                      && cm.endB.footprintRef == row.footprintRefB );

            const bool swapped =
                    ( cm.endA.subProjectUuid == iB->subProjectUuid
                      && cm.endA.footprintRef == row.footprintRefB
                      && cm.endB.subProjectUuid == iA->subProjectUuid
                      && cm.endB.footprintRef == row.footprintRefA );

            if( same || swapped )
            {
                m_manager->RemoveCustomMate( cm.uuid );
                break;
            }
        }
    }
    else if( row.isAuto )
    {
        // Add a CUSTOM DISABLED override; next BuildMateGraph pass
        // marks the matching auto pair as disabled (still visible).
        CUSTOM_MATE override;
        override.role = CUSTOM_MATE_ROLE::DISABLED;
        override.type = CUSTOM_MATE_TYPE::CONNECTOR;

        if( const BOARD_3D_INSTANCE* iA = m_manager->GetBoardInstance( row.instanceA ) )
            override.endA.subProjectUuid = iA->subProjectUuid;

        if( const BOARD_3D_INSTANCE* iB = m_manager->GetBoardInstance( row.instanceB ) )
            override.endB.subProjectUuid = iB->subProjectUuid;

        override.endA.footprintRef = row.footprintRefA;
        override.endB.footprintRef = row.footprintRefB;

        m_manager->AddCustomMate( override );
    }
    else
    {
        const std::vector<CUSTOM_MATE>& mates = m_manager->GetCustomMates();
        auto cm = std::find_if( mates.begin(), mates.end(),
                                [&]( const CUSTOM_MATE& m ) { return m.uuid == row.customMateUuid; } );

        if( cm == mates.end() )
            return;

        CUSTOM_MATE updated = *cm;
        updated.role        = CUSTOM_MATE_ROLE::DISABLED;
        m_manager->UpdateCustomMate( updated );
    }

    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    updateMateButtons();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onDeleteCustomMate( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode || it->second.isAuto )
        return;

    m_manager->RemoveCustomMate( it->second.customMateUuid );

    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
    updateMateButtons();
    refresh3DView();
}
