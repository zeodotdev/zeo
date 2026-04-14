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

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


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
