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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "panel_3d_assembly.h"

#include <3d_viewer/3d_viewer_assembly.h>
#include <3d_viewer/eda_3d_viewer_frame.h>
#include <board.h>
#include <footprint.h>
#include <project.h>
#include <project/project_file.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
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
}


PANEL_3D_ASSEMBLY::~PANEL_3D_ASSEMBLY()
{
}


void PANEL_3D_ASSEMBLY::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Board list section
    wxStaticBoxSizer* boardBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Boards" ) );

    m_boardListBox = new wxCheckListBox( this, wxID_ANY, wxDefaultPosition,
                                          wxSize( -1, 120 ) );
    boardBox->Add( m_boardListBox, 1, wxEXPAND | wxBOTTOM, 5 );

    wxBoxSizer* visButtonSizer = new wxBoxSizer( wxHORIZONTAL );
    m_showAllButton = new wxButton( this, wxID_ANY, _( "Show All" ) );
    m_hideAllButton = new wxButton( this, wxID_ANY, _( "Hide All" ) );
    visButtonSizer->Add( m_showAllButton, 1, wxRIGHT, 3 );
    visButtonSizer->Add( m_hideAllButton, 1 );
    boardBox->Add( visButtonSizer, 0, wxEXPAND );

    mainSizer->Add( boardBox, 0, wxEXPAND | wxALL, 5 );

    // Layout section
    wxStaticBoxSizer* layoutBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Layout" ) );

    wxBoxSizer* layoutModeSizer = new wxBoxSizer( wxHORIZONTAL );
    layoutModeSizer->Add( new wxStaticText( this, wxID_ANY, _( "Mode:" ) ), 0,
                          wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_layoutModeChoice = new wxChoice( this, wxID_ANY );
    m_layoutModeChoice->Append( _( "Flat (side by side)" ) );
    m_layoutModeChoice->Append( _( "Stacked" ) );
    m_layoutModeChoice->Append( _( "Custom" ) );
    m_layoutModeChoice->SetSelection( 0 );
    layoutModeSizer->Add( m_layoutModeChoice, 1 );
    layoutBox->Add( layoutModeSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    mainSizer->Add( layoutBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // Position section (for selected board)
    wxStaticBoxSizer* posBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Position" ) );

    m_selectedBoardLabel = new wxStaticText( this, wxID_ANY, _( "Select a board" ) );
    wxFont boldFont = m_selectedBoardLabel->GetFont();
    boldFont.SetWeight( wxFONTWEIGHT_BOLD );
    m_selectedBoardLabel->SetFont( boldFont );
    posBox->Add( m_selectedBoardLabel, 0, wxBOTTOM, 5 );

    // Position XYZ
    wxFlexGridSizer* posSizer = new wxFlexGridSizer( 3, 3, 3, 5 );
    posSizer->AddGrowableCol( 1, 1 );

    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "X:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_posXCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    posSizer->Add( m_posXCtrl, 0 );
    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0, wxALIGN_CENTER_VERTICAL );

    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "Y:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_posYCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    posSizer->Add( m_posYCtrl, 0 );
    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0, wxALIGN_CENTER_VERTICAL );

    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "Z:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_posZCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    posSizer->Add( m_posZCtrl, 0 );
    posSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0, wxALIGN_CENTER_VERTICAL );

    posBox->Add( posSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    // Rotation XYZ
    posBox->Add( new wxStaticText( this, wxID_ANY, _( "Rotation:" ) ), 0, wxTOP | wxBOTTOM, 3 );

    wxFlexGridSizer* rotSizer = new wxFlexGridSizer( 3, 3, 3, 5 );
    rotSizer->AddGrowableCol( 1, 1 );

    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "X:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_rotXCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    rotSizer->Add( m_rotXCtrl, 0 );
    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "°" ) ), 0, wxALIGN_CENTER_VERTICAL );

    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "Y:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_rotYCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    rotSizer->Add( m_rotYCtrl, 0 );
    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "°" ) ), 0, wxALIGN_CENTER_VERTICAL );

    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "Z:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_rotZCtrl = new wxTextCtrl( this, wxID_ANY, "0", wxDefaultPosition, wxSize( 60, -1 ) );
    rotSizer->Add( m_rotZCtrl, 0 );
    rotSizer->Add( new wxStaticText( this, wxID_ANY, _( "°" ) ), 0, wxALIGN_CENTER_VERTICAL );

    posBox->Add( rotSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    m_resetPositionsButton = new wxButton( this, wxID_ANY, _( "Reset Positions" ) );
    posBox->Add( m_resetPositionsButton, 0, wxEXPAND );

    mainSizer->Add( posBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // Assembly options
    wxStaticBoxSizer* optionsBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Assembly" ) );

    m_mateConnectorsCheck = new wxCheckBox( this, wxID_ANY, _( "Mate connectors" ) );
    m_mateConnectorsCheck->SetToolTip( _( "Auto-align boards at connector pairs" ) );
    optionsBox->Add( m_mateConnectorsCheck, 0, wxBOTTOM, 3 );

    m_transparentCheck = new wxCheckBox( this, wxID_ANY, _( "Transparent mode" ) );
    m_transparentCheck->SetToolTip( _( "Make selected board semi-transparent" ) );
    optionsBox->Add( m_transparentCheck, 0 );

    mainSizer->Add( optionsBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // M6.D-phase-2 Mates section: tree of mate edges + per-pair
    // actions. Auto-derived rows are read-only; only CUSTOM rows
    // accept Mark primary / Disable / Delete.
    wxStaticBoxSizer* matesBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Mates" ) );

    m_matesTree = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, wxSize( -1, 140 ),
                                   wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE );
    matesBox->Add( m_matesTree, 1, wxEXPAND | wxBOTTOM, 5 );

    wxBoxSizer* matesButtonSizer = new wxBoxSizer( wxHORIZONTAL );
    m_addMateButton     = new wxButton( this, wxID_ANY, _( "+ Add" ) );
    m_markPrimaryButton = new wxButton( this, wxID_ANY, _( "Primary" ) );
    m_disableMateButton = new wxButton( this, wxID_ANY, _( "Disable" ) );
    m_deleteMateButton  = new wxButton( this, wxID_ANY, _( "Delete" ) );

    m_addMateButton->SetToolTip( _( "Add a custom mate (mounting hole, override, etc.)" ) );
    m_markPrimaryButton->SetToolTip( _( "Force this pair as primary on its board edge" ) );
    m_disableMateButton->SetToolTip( _( "Disable an auto-derived mate" ) );
    m_deleteMateButton->SetToolTip( _( "Delete the selected custom mate" ) );

    matesButtonSizer->Add( m_addMateButton,     1, wxRIGHT, 3 );
    matesButtonSizer->Add( m_markPrimaryButton, 1, wxRIGHT, 3 );
    matesButtonSizer->Add( m_disableMateButton, 1, wxRIGHT, 3 );
    matesButtonSizer->Add( m_deleteMateButton,  1 );

    matesBox->Add( matesButtonSizer, 0, wxEXPAND );

    mainSizer->Add( matesBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // Validation section
    wxStaticBoxSizer* validationBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Validation" ) );

    m_collisionCheckButton = new wxButton( this, wxID_ANY, _( "Run Collision Check" ) );
    validationBox->Add( m_collisionCheckButton, 0, wxEXPAND | wxBOTTOM, 3 );

    m_collisionStatusLabel = new wxStaticText( this, wxID_ANY, _( "Status: --" ) );
    validationBox->Add( m_collisionStatusLabel, 0 );

    mainSizer->Add( validationBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    // Export section
    wxStaticBoxSizer* exportBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Export" ) );

    m_exportSTEPButton = new wxButton( this, wxID_ANY, _( "Export Assembly STEP..." ) );
    exportBox->Add( m_exportSTEPButton, 0, wxEXPAND );

    mainSizer->Add( exportBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

    SetSizer( mainSizer );
}


void PANEL_3D_ASSEMBLY::bindEvents()
{
    m_boardListBox->Bind( wxEVT_LISTBOX, &PANEL_3D_ASSEMBLY::onBoardSelected, this );
    m_boardListBox->Bind( wxEVT_CHECKLISTBOX, &PANEL_3D_ASSEMBLY::onBoardVisibilityChanged, this );

    m_showAllButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onShowAllBoards, this );
    m_hideAllButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onHideAllBoards, this );

    m_layoutModeChoice->Bind( wxEVT_CHOICE, &PANEL_3D_ASSEMBLY::onLayoutModeChanged, this );

    m_posXCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onPositionChanged, this );
    m_posYCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onPositionChanged, this );
    m_posZCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onPositionChanged, this );

    m_rotXCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onRotationChanged, this );
    m_rotYCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onRotationChanged, this );
    m_rotZCtrl->Bind( wxEVT_TEXT_ENTER, &PANEL_3D_ASSEMBLY::onRotationChanged, this );

    m_resetPositionsButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onResetPositions, this );

    m_mateConnectorsCheck->Bind( wxEVT_CHECKBOX, &PANEL_3D_ASSEMBLY::onMateConnectors, this );
    m_transparentCheck->Bind( wxEVT_CHECKBOX, &PANEL_3D_ASSEMBLY::onTransparencyChanged, this );

    m_collisionCheckButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onRunCollisionCheck, this );
    m_exportSTEPButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onExportSTEP, this );

    // M6.D-phase-2 mates UI bindings
    m_matesTree->Bind( wxEVT_TREE_SEL_CHANGED,
                       &PANEL_3D_ASSEMBLY::onMateTreeSelectionChanged, this );
    m_addMateButton->Bind(     wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onAddCustomMate,    this );
    m_markPrimaryButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onMarkMatePrimary,  this );
    m_disableMateButton->Bind( wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onDisableMate,      this );
    m_deleteMateButton->Bind(  wxEVT_BUTTON, &PANEL_3D_ASSEMBLY::onDeleteCustomMate, this );
}


void PANEL_3D_ASSEMBLY::RefreshBoardList()
{
    m_boardListBox->Clear();
    m_boardUuids.clear();
    m_selectedBoardIndex = wxNOT_FOUND;

    if( !m_manager )
        return;

    const auto& instances = m_manager->GetBoardInstances();

    for( const auto& inst : instances )
    {
        int index = m_boardListBox->Append( inst.displayName );
        m_boardListBox->Check( index, inst.visible );
        m_boardUuids.push_back( inst.uuid );
    }

    UpdateSelectedBoardControls();
}


void PANEL_3D_ASSEMBLY::UpdateSelectedBoardControls()
{
    if( m_selectedBoardIndex == wxNOT_FOUND || !m_manager )
    {
        m_selectedBoardLabel->SetLabel( _( "Select a board" ) );
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

    m_selectedBoardLabel->SetLabel( inst->displayName );

    m_posXCtrl->SetValue( wxString::Format( "%.2f", inst->position.x ) );
    m_posYCtrl->SetValue( wxString::Format( "%.2f", inst->position.y ) );
    m_posZCtrl->SetValue( wxString::Format( "%.2f", inst->position.z ) );

    m_rotXCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.x ) );
    m_rotYCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.y ) );
    m_rotZCtrl->SetValue( wxString::Format( "%.1f", inst->rotation.z ) );

    m_transparentCheck->SetValue( inst->transparent );
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
    m_selectedBoardIndex = m_boardListBox->GetSelection();
    UpdateSelectedBoardControls();

    // Repoint the frame's 3D adapter at the newly-selected sub-board.
    // Pre-M6.C: only one instance renders at a time; selection drives
    // which. Panel still tracks every instance for layout/mating.
    if( m_frame && m_selectedBoardIndex != wxNOT_FOUND
        && m_selectedBoardIndex < static_cast<int>( m_boardUuids.size() ) )
    {
        m_frame->SetActiveAssemblyInstance( m_boardUuids[m_selectedBoardIndex] );
    }
}


void PANEL_3D_ASSEMBLY::onBoardVisibilityChanged( wxCommandEvent& aEvent )
{
    int index = aEvent.GetInt();
    if( index < 0 || index >= static_cast<int>( m_boardUuids.size() ) || !m_manager )
        return;

    bool visible = m_boardListBox->IsChecked( index );
    m_manager->SetBoardVisible( m_boardUuids[index], visible );

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
    UpdateSelectedBoardControls();
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
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onResetPositions( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    m_manager->ResetPositions();
    UpdateSelectedBoardControls();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onMateConnectors( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    if( m_mateConnectorsCheck->GetValue() )
    {
        m_manager->MateConnectors();
        m_layoutModeChoice->SetSelection( 2 );  // Custom
    }

    UpdateSelectedBoardControls();
    refresh3DView();
}


void PANEL_3D_ASSEMBLY::onRunCollisionCheck( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    std::vector<COLLISION_RESULT> collisions = m_manager->RunCollisionCheck();
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

    if( dlg.ShowModal() == wxID_OK )
    {
        if( m_manager->ExportAssemblySTEP( dlg.GetPath() ) )
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


void PANEL_3D_ASSEMBLY::onTransparencyChanged( wxCommandEvent& aEvent )
{
    if( m_selectedBoardIndex == wxNOT_FOUND || !m_manager )
        return;

    KIID uuid = m_boardUuids[m_selectedBoardIndex];
    m_manager->SetBoardTransparent( uuid, m_transparentCheck->GetValue(), 0.5f );
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

    if( collisions.empty() )
    {
        m_collisionStatusLabel->SetLabel( _( "Status: No collisions" ) );
        m_collisionStatusLabel->SetForegroundColour( wxColour( 0, 128, 0 ) );  // Green
    }
    else
    {
        m_collisionStatusLabel->SetLabel(
            wxString::Format( _( "Status: %zu collision(s) found" ), collisions.size() ) );
        m_collisionStatusLabel->SetForegroundColour( wxColour( 200, 0, 0 ) );  // Red
    }
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

        for( const MATE_PAIR& pair : edge.pairs )
        {
            wxString badge;

            if( pair.forcedPrimary )
                badge << "[PRIMARY*]";
            else if( pair.alignmentOnly )
                badge << "[SECONDARY]";

            const bool isCustom = !( pair.customMateUuid == KIID( 0 ) );

            if( isCustom )
                badge << " [CUSTOM]";
            else
                badge << " [AUTO]";

            wxString label = wxString::Format( "%s — %s   %d pin(s)  %s",
                                               pair.footprintRefA,
                                               pair.footprintRefB,
                                               pair.pinCount,
                                               badge );

            wxTreeItemId pairNode = m_matesTree->AppendItem( edgeNode, label );

            MATE_TREE_ROW row{};
            row.isEdgeNode     = false;
            row.isAuto         = !isCustom;
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

    // Add is always available when a project is loaded.
    m_addMateButton->Enable( m_manager && m_manager->GetBoardInstances().size() >= 2 );

    // Mark primary works on any leaf — for AUTO it creates a new
    // CUSTOM PRIMARY override; for CUSTOM it bumps role to PRIMARY.
    m_markPrimaryButton->Enable( haveSelection );

    // Disable creates a CUSTOM DISABLED for an AUTO leaf, or flips a
    // CUSTOM mate's role to DISABLED.
    m_disableMateButton->Enable( haveSelection );

    // Delete only on CUSTOM rows (AUTO can't be deleted, only disabled).
    m_deleteMateButton->Enable( isCustomLeaf );

    (void) isAutoLeaf;   // currently no UI state branches on this; kept for clarity
}


void PANEL_3D_ASSEMBLY::onMateTreeSelectionChanged( wxTreeEvent& aEvent )
{
    updateMateButtons();
    aEvent.Skip();
}


// Modal dialog: pick board A → footprint A → board B → footprint B,
// pick mate type, pick role. Returns the populated CUSTOM_MATE on OK.
namespace
{

class ADD_CUSTOM_MATE_DIALOG : public wxDialog
{
public:
    ADD_CUSTOM_MATE_DIALOG( wxWindow* aParent, ASSEMBLY_3D_MANAGER* aManager ) :
            wxDialog( aParent, wxID_ANY, _( "Add Custom Mate" ),
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

        rebuildFootprintList( m_boardA, m_fpA );
        rebuildFootprintList( m_boardB, m_fpB );

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


void PANEL_3D_ASSEMBLY::onMarkMatePrimary( wxCommandEvent& aEvent )
{
    if( !m_manager )
        return;

    wxTreeItemId sel = m_matesTree->GetSelection();
    auto         it  = m_mateTreeRows.find( sel.IsOk() ? sel.GetID() : nullptr );

    if( it == m_mateTreeRows.end() || it->second.isEdgeNode )
        return;

    const MATE_TREE_ROW& row = it->second;

    if( row.isAuto )
    {
        // Auto mates can't be edited directly. Add a CUSTOM PRIMARY
        // override that aliases the same canonical pair.
        CUSTOM_MATE override;
        override.role = CUSTOM_MATE_ROLE::PRIMARY;
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
        // Update the existing CUSTOM mate's role.
        const std::vector<CUSTOM_MATE>& mates = m_manager->GetCustomMates();
        auto cm = std::find_if( mates.begin(), mates.end(),
                                [&]( const CUSTOM_MATE& m ) { return m.uuid == row.customMateUuid; } );

        if( cm == mates.end() )
            return;

        CUSTOM_MATE updated = *cm;
        updated.role        = CUSTOM_MATE_ROLE::PRIMARY;
        m_manager->UpdateCustomMate( updated );
    }

    if( m_manager->GetState().mateConnectors )
        m_manager->MateConnectors();

    RefreshMatesTree();
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

    if( row.isAuto )
    {
        // Add a CUSTOM DISABLED override. The next BuildMateGraph()
        // pass erases the matching auto pair.
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
