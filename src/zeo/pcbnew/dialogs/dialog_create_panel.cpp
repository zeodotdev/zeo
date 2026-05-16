/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_create_panel.h"

#include <board.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <widgets/std_bitmap_button.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


DIALOG_CREATE_PANEL::DIALOG_CREATE_PANEL( PCB_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Create Panel" ), wxDefaultPosition,
                     wxSize( 700, 600 ), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_project( &aParent->Prj() )
{
    createControls();
    populateBoardList();
    updatePreview();

    SetMinSize( wxSize( 600, 500 ) );
    Centre();
}


DIALOG_CREATE_PANEL::~DIALOG_CREATE_PANEL()
{
}


void DIALOG_CREATE_PANEL::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Panel name
    wxBoxSizer* nameSizer = new wxBoxSizer( wxHORIZONTAL );
    nameSizer->Add( new wxStaticText( this, wxID_ANY, _( "Panel Name:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_panelNameCtrl = new wxTextCtrl( this, wxID_ANY, _( "Production Panel" ) );
    nameSizer->Add( m_panelNameCtrl, 1, wxEXPAND );
    mainSizer->Add( nameSizer, 0, wxEXPAND | wxALL, 10 );

    // Main content in horizontal layout
    wxBoxSizer* contentSizer = new wxBoxSizer( wxHORIZONTAL );

    // Left side: Board selection
    wxStaticBoxSizer* boardBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Source Boards" ) );

    m_boardListCtrl = new wxDataViewListCtrl( this, wxID_ANY, wxDefaultPosition,
                                               wxSize( 280, 150 ) );
    m_boardListCtrl->AppendTextColumn( _( "Board" ), wxDATAVIEW_CELL_INERT, 120 );
    m_boardListCtrl->AppendTextColumn( _( "Copies" ), wxDATAVIEW_CELL_INERT, 50 );
    m_boardListCtrl->AppendTextColumn( _( "Rotation" ), wxDATAVIEW_CELL_INERT, 60 );
    m_boardListCtrl->AppendTextColumn( _( "Mirror" ), wxDATAVIEW_CELL_INERT, 50 );
    boardBox->Add( m_boardListCtrl, 1, wxEXPAND | wxBOTTOM, 5 );

    // Add board controls
    wxBoxSizer* addBoardSizer = new wxBoxSizer( wxHORIZONTAL );
    m_boardChoice = new wxChoice( this, wxID_ANY );
    addBoardSizer->Add( m_boardChoice, 1, wxRIGHT, 5 );

    addBoardSizer->Add( new wxStaticText( this, wxID_ANY, _( "×" ) ), 0,
                        wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5 );

    m_copiesSpin = new wxSpinCtrl( this, wxID_ANY, wxT( "1" ), wxDefaultPosition,
                                   wxSize( 50, -1 ), wxSP_ARROW_KEYS, 1, 100, 1 );
    addBoardSizer->Add( m_copiesSpin, 0, wxRIGHT, 5 );

    m_rotationChoice = new wxChoice( this, wxID_ANY );
    m_rotationChoice->Append( _( "0°" ) );
    m_rotationChoice->Append( _( "90°" ) );
    m_rotationChoice->Append( _( "180°" ) );
    m_rotationChoice->Append( _( "270°" ) );
    m_rotationChoice->SetSelection( 0 );
    addBoardSizer->Add( m_rotationChoice, 0, wxRIGHT, 5 );

    boardBox->Add( addBoardSizer, 0, wxEXPAND | wxBOTTOM, 5 );

    wxBoxSizer* boardButtonSizer = new wxBoxSizer( wxHORIZONTAL );
    m_addBoardButton = new wxButton( this, wxID_ANY, _( "Add" ) );
    m_removeBoardButton = new wxButton( this, wxID_ANY, _( "Remove" ) );
    boardButtonSizer->Add( m_addBoardButton, 0, wxRIGHT, 5 );
    boardButtonSizer->Add( m_removeBoardButton, 0 );
    boardBox->Add( boardButtonSizer, 0, wxALIGN_RIGHT );

    contentSizer->Add( boardBox, 1, wxEXPAND | wxRIGHT, 10 );

    // Right side: Settings
    wxBoxSizer* settingsSizer = new wxBoxSizer( wxVERTICAL );

    // Layout settings
    wxStaticBoxSizer* layoutBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Panel Layout" ) );

    m_gridLayoutRadio = new wxRadioButton( this, wxID_ANY, _( "Grid (rows × cols)" ),
                                           wxDefaultPosition, wxDefaultSize, wxRB_GROUP );
    layoutBox->Add( m_gridLayoutRadio, 0, wxBOTTOM, 3 );

    wxBoxSizer* gridSizer = new wxBoxSizer( wxHORIZONTAL );
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Rows:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxLEFT, 20 );
    m_rowsSpin = new wxSpinCtrl( this, wxID_ANY, wxT( "2" ), wxDefaultPosition,
                                  wxSize( 50, -1 ), wxSP_ARROW_KEYS, 1, 20, 2 );
    gridSizer->Add( m_rowsSpin, 0, wxLEFT | wxRIGHT, 5 );
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Cols:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );
    m_colsSpin = new wxSpinCtrl( this, wxID_ANY, wxT( "3" ), wxDefaultPosition,
                                  wxSize( 50, -1 ), wxSP_ARROW_KEYS, 1, 20, 3 );
    gridSizer->Add( m_colsSpin, 0, wxLEFT, 5 );
    layoutBox->Add( gridSizer, 0, wxBOTTOM, 3 );

    m_autoLayoutRadio = new wxRadioButton( this, wxID_ANY, _( "Auto-optimize" ) );
    layoutBox->Add( m_autoLayoutRadio, 0, wxBOTTOM, 3 );

    m_customLayoutRadio = new wxRadioButton( this, wxID_ANY, _( "Custom placement" ) );
    layoutBox->Add( m_customLayoutRadio, 0, wxBOTTOM, 5 );

    wxBoxSizer* spacingSizer = new wxBoxSizer( wxHORIZONTAL );
    spacingSizer->Add( new wxStaticText( this, wxID_ANY, _( "Board Spacing:" ) ), 0,
                       wxALIGN_CENTER_VERTICAL );
    m_spacingCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "3.0" ), wxDefaultPosition,
                                     wxSize( 60, -1 ) );
    spacingSizer->Add( m_spacingCtrl, 0, wxLEFT, 5 );
    spacingSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0,
                       wxALIGN_CENTER_VERTICAL | wxLEFT, 3 );
    layoutBox->Add( spacingSizer, 0, wxBOTTOM, 3 );

    m_alternateRotationCheck = new wxCheckBox( this, wxID_ANY, _( "Alternate rotation for nesting" ) );
    layoutBox->Add( m_alternateRotationCheck, 0 );

    settingsSizer->Add( layoutBox, 0, wxEXPAND | wxBOTTOM, 10 );

    // Tab settings
    wxStaticBoxSizer* tabBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Tabs" ) );

    m_noTabsRadio = new wxRadioButton( this, wxID_ANY, _( "None" ),
                                        wxDefaultPosition, wxDefaultSize, wxRB_GROUP );
    tabBox->Add( m_noTabsRadio, 0, wxBOTTOM, 2 );

    m_mousebiteRadio = new wxRadioButton( this, wxID_ANY, _( "Mousebites" ) );
    m_mousebiteRadio->SetValue( true );
    tabBox->Add( m_mousebiteRadio, 0, wxBOTTOM, 2 );

    wxBoxSizer* mousebiteSettings = new wxBoxSizer( wxHORIZONTAL );
    mousebiteSettings->Add( new wxStaticText( this, wxID_ANY, _( "Hole Ø:" ) ), 0,
                            wxALIGN_CENTER_VERTICAL | wxLEFT, 20 );
    m_mousebiteHoleDiaCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "0.5" ), wxDefaultPosition,
                                              wxSize( 50, -1 ) );
    mousebiteSettings->Add( m_mousebiteHoleDiaCtrl, 0, wxLEFT, 3 );
    mousebiteSettings->Add( new wxStaticText( this, wxID_ANY, _( "mm  Spacing:" ) ), 0,
                            wxALIGN_CENTER_VERTICAL | wxLEFT, 5 );
    m_mousebiteHoleSpacingCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "0.8" ), wxDefaultPosition,
                                                  wxSize( 50, -1 ) );
    mousebiteSettings->Add( m_mousebiteHoleSpacingCtrl, 0, wxLEFT, 3 );
    mousebiteSettings->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0,
                            wxALIGN_CENTER_VERTICAL | wxLEFT, 3 );
    tabBox->Add( mousebiteSettings, 0, wxBOTTOM, 2 );

    m_vGrooveRadio = new wxRadioButton( this, wxID_ANY, _( "V-Groove" ) );
    tabBox->Add( m_vGrooveRadio, 0, wxBOTTOM, 2 );

    m_solidTabsRadio = new wxRadioButton( this, wxID_ANY, _( "Solid tabs" ) );
    tabBox->Add( m_solidTabsRadio, 0, wxBOTTOM, 5 );

    wxBoxSizer* tabDimSizer = new wxBoxSizer( wxHORIZONTAL );
    tabDimSizer->Add( new wxStaticText( this, wxID_ANY, _( "Tab width:" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    m_tabWidthCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "3.0" ), wxDefaultPosition,
                                      wxSize( 50, -1 ) );
    tabDimSizer->Add( m_tabWidthCtrl, 0, wxLEFT, 5 );
    tabDimSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm  Spacing:" ) ), 0,
                      wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );
    m_tabSpacingCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "50" ), wxDefaultPosition,
                                        wxSize( 50, -1 ) );
    tabDimSizer->Add( m_tabSpacingCtrl, 0, wxLEFT, 5 );
    tabDimSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0,
                      wxALIGN_CENTER_VERTICAL | wxLEFT, 3 );
    tabBox->Add( tabDimSizer, 0 );

    settingsSizer->Add( tabBox, 0, wxEXPAND | wxBOTTOM, 10 );

    // Frame/Rails settings
    wxStaticBoxSizer* frameBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Frame/Rails" ) );

    m_addRailsCheck = new wxCheckBox( this, wxID_ANY, _( "Add manufacturing rails" ) );
    m_addRailsCheck->SetValue( true );
    frameBox->Add( m_addRailsCheck, 0, wxBOTTOM, 3 );

    wxBoxSizer* railWidthSizer = new wxBoxSizer( wxHORIZONTAL );
    railWidthSizer->Add( new wxStaticText( this, wxID_ANY, _( "Rail width:" ) ), 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT, 20 );
    m_railWidthCtrl = new wxTextCtrl( this, wxID_ANY, wxT( "5.0" ), wxDefaultPosition,
                                       wxSize( 50, -1 ) );
    railWidthSizer->Add( m_railWidthCtrl, 0, wxLEFT, 5 );
    railWidthSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT, 3 );
    frameBox->Add( railWidthSizer, 0, wxBOTTOM, 3 );

    wxBoxSizer* railSidesSizer = new wxBoxSizer( wxHORIZONTAL );
    m_railsTopCheck = new wxCheckBox( this, wxID_ANY, _( "Top" ) );
    m_railsTopCheck->SetValue( true );
    m_railsBottomCheck = new wxCheckBox( this, wxID_ANY, _( "Bottom" ) );
    m_railsBottomCheck->SetValue( true );
    m_railsLeftCheck = new wxCheckBox( this, wxID_ANY, _( "Left" ) );
    m_railsRightCheck = new wxCheckBox( this, wxID_ANY, _( "Right" ) );
    railSidesSizer->Add( m_railsTopCheck, 0, wxRIGHT, 10 );
    railSidesSizer->Add( m_railsBottomCheck, 0, wxRIGHT, 10 );
    railSidesSizer->Add( m_railsLeftCheck, 0, wxRIGHT, 10 );
    railSidesSizer->Add( m_railsRightCheck, 0 );
    frameBox->Add( railSidesSizer, 0, wxLEFT, 20 );

    settingsSizer->Add( frameBox, 0, wxEXPAND | wxBOTTOM, 10 );

    // Tooling features
    wxStaticBoxSizer* toolingBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Tooling Features" ) );

    m_addToolingHolesCheck = new wxCheckBox( this, wxID_ANY, _( "Add tooling holes" ) );
    m_addToolingHolesCheck->SetValue( true );
    toolingBox->Add( m_addToolingHolesCheck, 0, wxBOTTOM, 3 );

    wxBoxSizer* toolingPatternSizer = new wxBoxSizer( wxHORIZONTAL );
    toolingPatternSizer->Add( new wxStaticText( this, wxID_ANY, _( "Pattern:" ) ), 0,
                              wxALIGN_CENTER_VERTICAL | wxLEFT, 20 );
    m_toolingPatternChoice = new wxChoice( this, wxID_ANY );
    m_toolingPatternChoice->Append( _( "Corners" ) );
    m_toolingPatternChoice->Append( _( "Edges" ) );
    m_toolingPatternChoice->SetSelection( 0 );
    toolingPatternSizer->Add( m_toolingPatternChoice, 0, wxLEFT, 5 );
    toolingBox->Add( toolingPatternSizer, 0, wxBOTTOM, 3 );

    m_addFiducialsCheck = new wxCheckBox( this, wxID_ANY, _( "Add panel fiducials" ) );
    m_addFiducialsCheck->SetValue( true );
    toolingBox->Add( m_addFiducialsCheck, 0 );

    settingsSizer->Add( toolingBox, 0, wxEXPAND );

    contentSizer->Add( settingsSizer, 0, wxEXPAND );

    mainSizer->Add( contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    // Panel size display
    wxBoxSizer* sizeSizer = new wxBoxSizer( wxHORIZONTAL );
    sizeSizer->Add( new wxStaticText( this, wxID_ANY, _( "Panel Size:" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_panelSizeLabel = new wxStaticText( this, wxID_ANY, _( "-- × -- mm" ) );
    wxFont boldFont = m_panelSizeLabel->GetFont();
    boldFont.SetWeight( wxFONTWEIGHT_BOLD );
    m_panelSizeLabel->SetFont( boldFont );
    sizeSizer->Add( m_panelSizeLabel, 0, wxALIGN_CENTER_VERTICAL );
    mainSizer->Add( sizeSizer, 0, wxALL, 10 );

    // Dialog buttons
    wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
    wxButton* okButton = new wxButton( this, wxID_OK, _( "Create Panel" ) );
    wxButton* cancelButton = new wxButton( this, wxID_CANCEL );
    buttonSizer->AddButton( okButton );
    buttonSizer->AddButton( cancelButton );
    buttonSizer->Realize();
    mainSizer->Add( buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10 );

    SetSizer( mainSizer );

    // Bind events
    m_addBoardButton->Bind( wxEVT_BUTTON, &DIALOG_CREATE_PANEL::onAddBoard, this );
    m_removeBoardButton->Bind( wxEVT_BUTTON, &DIALOG_CREATE_PANEL::onRemoveBoard, this );
    m_boardListCtrl->Bind( wxEVT_DATAVIEW_SELECTION_CHANGED,
                           &DIALOG_CREATE_PANEL::onBoardSelected, this );
    m_gridLayoutRadio->Bind( wxEVT_RADIOBUTTON, &DIALOG_CREATE_PANEL::onLayoutChanged, this );
    m_autoLayoutRadio->Bind( wxEVT_RADIOBUTTON, &DIALOG_CREATE_PANEL::onLayoutChanged, this );
    m_customLayoutRadio->Bind( wxEVT_RADIOBUTTON, &DIALOG_CREATE_PANEL::onLayoutChanged, this );
    m_rowsSpin->Bind( wxEVT_SPINCTRL, &DIALOG_CREATE_PANEL::onSpinChanged, this );
    m_colsSpin->Bind( wxEVT_SPINCTRL, &DIALOG_CREATE_PANEL::onSpinChanged, this );

    okButton->Bind( wxEVT_BUTTON, &DIALOG_CREATE_PANEL::onOK, this );
}


void DIALOG_CREATE_PANEL::populateBoardList()
{
    m_boardChoice->Clear();

    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    for( const BOARD_INFO& info : boardInfos )
    {
        m_boardChoice->Append( info.displayName );
    }

    if( m_boardChoice->GetCount() > 0 )
        m_boardChoice->SetSelection( 0 );
}


void DIALOG_CREATE_PANEL::updatePreview()
{
    // Build a temporary panel to calculate size
    if( buildPanel() && m_panel )
    {
        updatePanelSizeDisplay();
    }
}


void DIALOG_CREATE_PANEL::updatePanelSizeDisplay()
{
    if( !m_panel )
    {
        m_panelSizeLabel->SetLabel( _( "-- × -- mm" ) );
        return;
    }

    double widthMm = m_panel->GetWidthMm();
    double heightMm = m_panel->GetHeightMm();

    m_panelSizeLabel->SetLabel( wxString::Format( "%.1f × %.1f mm", widthMm, heightMm ) );
}


void DIALOG_CREATE_PANEL::onAddBoard( wxCommandEvent& aEvent )
{
    int selection = m_boardChoice->GetSelection();
    if( selection == wxNOT_FOUND )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    if( selection >= static_cast<int>( boardInfos.size() ) )
        return;

    const BOARD_INFO& info = boardInfos[selection];

    PANEL_BOARD_ENTRY entry;
    entry.boardUuid = info.uuid;
    entry.boardName = info.displayName;
    entry.copies = m_copiesSpin->GetValue();

    int rotationSel = m_rotationChoice->GetSelection();
    entry.rotationDeg = rotationSel * 90.0;

    m_boardEntries.push_back( entry );

    // Update list control
    wxVector<wxVariant> data;
    data.push_back( wxVariant( entry.boardName ) );
    data.push_back( wxVariant( wxString::Format( "%d", entry.copies ) ) );
    data.push_back( wxVariant( wxString::Format( "%.0f°", entry.rotationDeg ) ) );
    data.push_back( wxVariant( entry.mirror ? _( "Yes" ) : _( "No" ) ) );
    m_boardListCtrl->AppendItem( data );

    updatePreview();
}


void DIALOG_CREATE_PANEL::onRemoveBoard( wxCommandEvent& aEvent )
{
    int selection = m_boardListCtrl->GetSelectedRow();
    if( selection == wxNOT_FOUND )
        return;

    m_boardListCtrl->DeleteItem( selection );
    m_boardEntries.erase( m_boardEntries.begin() + selection );

    updatePreview();
}


void DIALOG_CREATE_PANEL::onBoardSelected( wxDataViewEvent& aEvent )
{
    int selection = m_boardListCtrl->GetSelectedRow();
    m_removeBoardButton->Enable( selection != wxNOT_FOUND );
}


void DIALOG_CREATE_PANEL::onLayoutChanged( wxCommandEvent& aEvent )
{
    bool isGrid = m_gridLayoutRadio->GetValue();
    m_rowsSpin->Enable( isGrid );
    m_colsSpin->Enable( isGrid );

    updatePreview();
}


void DIALOG_CREATE_PANEL::onTabTypeChanged( wxCommandEvent& aEvent )
{
    bool isMousebite = m_mousebiteRadio->GetValue();
    m_mousebiteHoleDiaCtrl->Enable( isMousebite );
    m_mousebiteHoleSpacingCtrl->Enable( isMousebite );

    updatePreview();
}


void DIALOG_CREATE_PANEL::onSettingChanged( wxCommandEvent& aEvent )
{
    updatePreview();
}


void DIALOG_CREATE_PANEL::onSpinChanged( wxSpinEvent& aEvent )
{
    updatePreview();
}


void DIALOG_CREATE_PANEL::onOK( wxCommandEvent& aEvent )
{
    if( !validateInputs() )
        return;

    if( !buildPanel() )
    {
        wxMessageBox( _( "Failed to create panel." ), _( "Error" ),
                      wxOK | wxICON_ERROR, this );
        return;
    }

    EndModal( wxID_OK );
}


bool DIALOG_CREATE_PANEL::validateInputs()
{
    if( m_boardEntries.empty() )
    {
        wxMessageBox( _( "Please add at least one board to the panel." ), _( "Validation Error" ),
                      wxOK | wxICON_WARNING, this );
        return false;
    }

    if( m_panelNameCtrl->GetValue().IsEmpty() )
    {
        wxMessageBox( _( "Please enter a panel name." ), _( "Validation Error" ),
                      wxOK | wxICON_WARNING, this );
        return false;
    }

    return true;
}


bool DIALOG_CREATE_PANEL::buildPanel()
{
    m_panel = std::make_unique<PANEL_BOARD>();
    m_panel->SetName( m_panelNameCtrl->GetValue() );

    // Add board instances
    // For now, we just add placeholders since we may not have loaded boards
    int instanceIndex = 0;
    for( const auto& entry : m_boardEntries )
    {
        for( int i = 0; i < entry.copies; i++ )
        {
            wxString instanceName = wxString::Format( "%s_%c",
                    entry.boardName, 'A' + static_cast<char>( instanceIndex++ ) );

            m_panel->AddBoardInstance( entry.boardUuid, VECTOR2I( 0, 0 ),
                                        entry.rotationDeg, instanceName );
        }
    }

    // Apply layout
    if( m_gridLayoutRadio->GetValue() )
    {
        PANEL_GRID_SETTINGS gridSettings;
        gridSettings.rows = m_rowsSpin->GetValue();
        gridSettings.cols = m_colsSpin->GetValue();

        double spacingMm = 3.0;
        m_spacingCtrl->GetValue().ToDouble( &spacingMm );
        gridSettings.spacingXNm = static_cast<int>( spacingMm * 1000000 );
        gridSettings.spacingYNm = gridSettings.spacingXNm;

        gridSettings.alternateRotation = m_alternateRotationCheck->GetValue();

        m_panel->ArrangeGrid( gridSettings );
    }
    else if( m_autoLayoutRadio->GetValue() )
    {
        double spacingMm = 3.0;
        m_spacingCtrl->GetValue().ToDouble( &spacingMm );
        m_panel->ArrangeOptimized( static_cast<int>( spacingMm * 1000000 ) );
    }
    // Custom layout: positions set manually (not implemented here)

    // Generate tabs
    PANEL_TAB_SETTINGS tabSettings;
    if( m_noTabsRadio->GetValue() )
    {
        tabSettings.type = PANEL_TAB_TYPE::NONE;
    }
    else if( m_mousebiteRadio->GetValue() )
    {
        tabSettings.type = PANEL_TAB_TYPE::MOUSEBITE;

        double holeDia = 0.5;
        m_mousebiteHoleDiaCtrl->GetValue().ToDouble( &holeDia );
        tabSettings.mousebiteHoleDiaNm = static_cast<int>( holeDia * 1000000 );

        double holeSpacing = 0.8;
        m_mousebiteHoleSpacingCtrl->GetValue().ToDouble( &holeSpacing );
        tabSettings.mousebiteHoleSpacingNm = static_cast<int>( holeSpacing * 1000000 );
    }
    else if( m_vGrooveRadio->GetValue() )
    {
        tabSettings.type = PANEL_TAB_TYPE::V_GROOVE;
    }
    else if( m_solidTabsRadio->GetValue() )
    {
        tabSettings.type = PANEL_TAB_TYPE::SOLID;
    }

    double tabWidth = 3.0;
    m_tabWidthCtrl->GetValue().ToDouble( &tabWidth );
    tabSettings.widthNm = static_cast<int>( tabWidth * 1000000 );

    double tabSpacing = 50.0;
    m_tabSpacingCtrl->GetValue().ToDouble( &tabSpacing );
    tabSettings.spacingNm = static_cast<int>( tabSpacing * 1000000 );

    if( tabSettings.type != PANEL_TAB_TYPE::NONE )
        m_panel->GenerateTabs( tabSettings );

    // Generate rails
    PANEL_FRAME_SETTINGS frameSettings;
    frameSettings.addRails = m_addRailsCheck->GetValue();

    double railWidth = 5.0;
    m_railWidthCtrl->GetValue().ToDouble( &railWidth );
    frameSettings.railWidthNm = static_cast<int>( railWidth * 1000000 );

    frameSettings.railsOnTop = m_railsTopCheck->GetValue();
    frameSettings.railsOnBottom = m_railsBottomCheck->GetValue();
    frameSettings.railsOnLeft = m_railsLeftCheck->GetValue();
    frameSettings.railsOnRight = m_railsRightCheck->GetValue();

    m_panel->GenerateRails( frameSettings );

    // Generate tooling features
    PANEL_TOOLING_SETTINGS toolingSettings;
    toolingSettings.addToolingHoles = m_addToolingHolesCheck->GetValue();

    int patternSel = m_toolingPatternChoice->GetSelection();
    toolingSettings.pattern = patternSel == 0 ? TOOLING_PATTERN::CORNERS : TOOLING_PATTERN::EDGES;

    toolingSettings.addFiducials = m_addFiducialsCheck->GetValue();

    m_panel->GenerateToolingHoles( toolingSettings );
    m_panel->GenerateFiducials( toolingSettings );

    return true;
}


std::unique_ptr<PANEL_BOARD> DIALOG_CREATE_PANEL::GetPanel()
{
    return std::move( m_panel );
}


wxString DIALOG_CREATE_PANEL::GetPanelName() const
{
    return m_panelNameCtrl->GetValue();
}
