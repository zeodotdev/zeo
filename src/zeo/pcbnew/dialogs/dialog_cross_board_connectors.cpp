/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_cross_board_connectors.h"

#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>


//-----------------------------------------------------------------------------
// DIALOG_CROSS_BOARD_CONNECTORS implementation
//-----------------------------------------------------------------------------

DIALOG_CROSS_BOARD_CONNECTORS::DIALOG_CROSS_BOARD_CONNECTORS( PCB_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Cross-Board Connectors" ),
                     wxDefaultPosition, wxSize( 700, 500 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_project( &aParent->Prj() ),
        m_connectorListBox( nullptr ),
        m_pinMappingView( nullptr ),
        m_statusLabel( nullptr ),
        m_addButton( nullptr ),
        m_removeButton( nullptr ),
        m_autoDetectButton( nullptr ),
        m_saveButton( nullptr ),
        m_closeButton( nullptr ),
        m_selectedPairIndex( -1 )
{
    createControls();
    populateConnectorList();

    SetMinSize( wxSize( 600, 400 ) );
    Layout();
    Centre();
}


DIALOG_CROSS_BOARD_CONNECTORS::~DIALOG_CROSS_BOARD_CONNECTORS()
{
}


void DIALOG_CROSS_BOARD_CONNECTORS::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Top panel: connector list and pin mapping side by side
    wxBoxSizer* topSizer = new wxBoxSizer( wxHORIZONTAL );

    // Left: Connector pair list
    wxStaticBoxSizer* listBox = new wxStaticBoxSizer( wxVERTICAL, this,
                                                       _( "Connector Pairs" ) );

    m_connectorListBox = new wxListBox( listBox->GetStaticBox(), wxID_ANY,
                                         wxDefaultPosition, wxSize( 200, -1 ) );
    listBox->Add( m_connectorListBox, 1, wxEXPAND | wxALL, 5 );

    wxBoxSizer* listButtonSizer = new wxBoxSizer( wxHORIZONTAL );
    m_addButton = new wxButton( listBox->GetStaticBox(), wxID_ANY, _( "Add..." ) );
    m_removeButton = new wxButton( listBox->GetStaticBox(), wxID_ANY, _( "Remove" ) );
    listButtonSizer->Add( m_addButton, 0, wxRIGHT, 5 );
    listButtonSizer->Add( m_removeButton, 0 );
    listBox->Add( listButtonSizer, 0, wxALL, 5 );

    topSizer->Add( listBox, 0, wxEXPAND | wxALL, 5 );

    // Right: Pin mapping view
    wxStaticBoxSizer* mappingBox = new wxStaticBoxSizer( wxVERTICAL, this,
                                                          _( "Pin Mapping" ) );

    m_pinMappingView = new wxDataViewListCtrl( mappingBox->GetStaticBox(), wxID_ANY,
                                                wxDefaultPosition, wxSize( -1, 200 ) );

    m_pinMappingView->AppendTextColumn( _( "Pin" ), wxDATAVIEW_CELL_INERT, 60 );
    m_pinMappingView->AppendTextColumn( _( "Board 1 Net" ), wxDATAVIEW_CELL_INERT, 120 );
    m_pinMappingView->AppendTextColumn( _( "Board 2 Net" ), wxDATAVIEW_CELL_INERT, 120 );
    m_pinMappingView->AppendTextColumn( _( "Status" ), wxDATAVIEW_CELL_INERT, 80 );

    mappingBox->Add( m_pinMappingView, 1, wxEXPAND | wxALL, 5 );

    topSizer->Add( mappingBox, 1, wxEXPAND | wxALL, 5 );

    mainSizer->Add( topSizer, 1, wxEXPAND );

    // Status bar
    wxStaticBoxSizer* statusBox = new wxStaticBoxSizer( wxHORIZONTAL, this, _( "Status" ) );
    m_statusLabel = new wxStaticText( statusBox->GetStaticBox(), wxID_ANY,
                                       _( "No connector pair selected" ) );
    statusBox->Add( m_statusLabel, 1, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    m_autoDetectButton = new wxButton( statusBox->GetStaticBox(), wxID_ANY,
                                        _( "Auto-Detect from Schematic" ) );
    statusBox->Add( m_autoDetectButton, 0, wxALL, 5 );

    mainSizer->Add( statusBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

    // Bottom buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer( wxHORIZONTAL );
    buttonSizer->AddStretchSpacer();

    m_saveButton = new wxButton( this, wxID_OK, _( "Save" ) );
    m_closeButton = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );

    buttonSizer->Add( m_saveButton, 0, wxRIGHT, 5 );
    buttonSizer->Add( m_closeButton, 0 );

    mainSizer->Add( buttonSizer, 0, wxEXPAND | wxALL, 10 );

    SetSizer( mainSizer );

    // Bind events
    m_connectorListBox->Bind( wxEVT_LISTBOX, &DIALOG_CROSS_BOARD_CONNECTORS::onConnectorSelected,
                               this );
    m_addButton->Bind( wxEVT_BUTTON, &DIALOG_CROSS_BOARD_CONNECTORS::onAddConnectorPair, this );
    m_removeButton->Bind( wxEVT_BUTTON, &DIALOG_CROSS_BOARD_CONNECTORS::onRemoveConnectorPair,
                           this );
    m_autoDetectButton->Bind( wxEVT_BUTTON, &DIALOG_CROSS_BOARD_CONNECTORS::onAutoDetect, this );
    m_saveButton->Bind( wxEVT_BUTTON, &DIALOG_CROSS_BOARD_CONNECTORS::onSave, this );
    m_closeButton->Bind( wxEVT_BUTTON, &DIALOG_CROSS_BOARD_CONNECTORS::onClose, this );
}


void DIALOG_CROSS_BOARD_CONNECTORS::RefreshConnectorList()
{
    populateConnectorList();
}


void DIALOG_CROSS_BOARD_CONNECTORS::populateConnectorList()
{
    m_connectorListBox->Clear();
    m_connectorPairs.clear();

    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& connections = projectFile.GetCrossBoardConnections();
    const auto& boardInfos = projectFile.GetBoardInfos();

    // Build a map of board UUIDs to names for display
    std::map<KIID, wxString> boardNames;
    for( const BOARD_INFO& info : boardInfos )
        boardNames[info.uuid] = info.displayName;

    // Group connections by connector pairs
    // For now, we'll show individual pad connections
    // TODO: Group by footprint reference for a cleaner display

    for( const CROSS_BOARD_CONNECTION& conn : connections )
    {
        CONNECTOR_PAIR_INFO pair;
        pair.board1Uuid = conn.board1Uuid;
        pair.board2Uuid = conn.board2Uuid;
        pair.board1Name = boardNames.count( conn.board1Uuid ) ?
                          boardNames[conn.board1Uuid] : _( "Unknown" );
        pair.board2Name = boardNames.count( conn.board2Uuid ) ?
                          boardNames[conn.board2Uuid] : _( "Unknown" );
        pair.connector1Ref = wxT( "Pad" );  // TODO: Get actual footprint reference
        pair.connector2Ref = wxT( "Pad" );
        pair.pinCount = 1;
        pair.matchedPins = 0;
        pair.mismatchedPins = 0;

        m_connectorPairs.push_back( pair );

        wxString displayText = wxString::Format( wxT( "%s (%s) <-> %s (%s)" ),
                                                  pair.connector1Ref, pair.board1Name,
                                                  pair.connector2Ref, pair.board2Name );
        m_connectorListBox->Append( displayText );
    }

    updateStatusDisplay();
}


void DIALOG_CROSS_BOARD_CONNECTORS::populatePinMappingView( int aConnectorPairIndex )
{
    m_pinMappingView->DeleteAllItems();

    if( aConnectorPairIndex < 0 ||
        aConnectorPairIndex >= static_cast<int>( m_connectorPairs.size() ) )
        return;

    // TODO: Populate with actual pin mapping data
    // For now, show placeholder data
    wxVector<wxVariant> row;
    row.push_back( wxVariant( wxT( "1" ) ) );
    row.push_back( wxVariant( wxT( "VCC" ) ) );
    row.push_back( wxVariant( wxT( "VCC" ) ) );
    row.push_back( wxVariant( wxT( "Match" ) ) );
    m_pinMappingView->AppendItem( row );
}


void DIALOG_CROSS_BOARD_CONNECTORS::updateStatusDisplay()
{
    if( m_selectedPairIndex < 0 )
    {
        m_statusLabel->SetLabel( _( "No connector pair selected" ) );
        m_removeButton->Enable( false );
    }
    else
    {
        const CONNECTOR_PAIR_INFO& pair = m_connectorPairs[m_selectedPairIndex];
        wxString status = wxString::Format(
                _( "Pins: %d total, %d matched, %d mismatched" ),
                pair.pinCount, pair.matchedPins, pair.mismatchedPins );
        m_statusLabel->SetLabel( status );
        m_removeButton->Enable( true );
    }
}


int DIALOG_CROSS_BOARD_CONNECTORS::GetSelectedConnectorPair() const
{
    return m_selectedPairIndex;
}


void DIALOG_CROSS_BOARD_CONNECTORS::onConnectorSelected( wxCommandEvent& aEvent )
{
    m_selectedPairIndex = m_connectorListBox->GetSelection();

    if( m_selectedPairIndex >= 0 )
        populatePinMappingView( m_selectedPairIndex );

    updateStatusDisplay();
}


void DIALOG_CROSS_BOARD_CONNECTORS::onAddConnectorPair( wxCommandEvent& aEvent )
{
    if( showAddConnectorDialog() )
        populateConnectorList();
}


void DIALOG_CROSS_BOARD_CONNECTORS::onRemoveConnectorPair( wxCommandEvent& aEvent )
{
    if( m_selectedPairIndex < 0 )
        return;

    int result = wxMessageBox( _( "Remove this connector pair?" ), _( "Confirm Removal" ),
                                wxYES_NO | wxICON_QUESTION, this );

    if( result == wxYES )
    {
        // TODO: Remove the connection from PROJECT_FILE
        m_connectorPairs.erase( m_connectorPairs.begin() + m_selectedPairIndex );
        m_selectedPairIndex = -1;
        populateConnectorList();
    }
}


void DIALOG_CROSS_BOARD_CONNECTORS::onAutoDetect( wxCommandEvent& aEvent )
{
    // TODO: Implement auto-detection from schematic
    wxMessageBox( _( "Auto-detection from schematic not yet implemented." ),
                   _( "Auto-Detect" ), wxOK | wxICON_INFORMATION, this );
}


void DIALOG_CROSS_BOARD_CONNECTORS::onClose( wxCommandEvent& aEvent )
{
    EndModal( wxID_CANCEL );
}


void DIALOG_CROSS_BOARD_CONNECTORS::onSave( wxCommandEvent& aEvent )
{
    // TODO: Save changes to PROJECT_FILE
    EndModal( wxID_OK );
}


bool DIALOG_CROSS_BOARD_CONNECTORS::showAddConnectorDialog()
{
    DIALOG_ADD_CONNECTOR_PAIR dlg( this, m_project );

    if( dlg.ShowModal() == wxID_OK )
    {
        // TODO: Add the new connection to PROJECT_FILE
        return true;
    }

    return false;
}


void DIALOG_CROSS_BOARD_CONNECTORS::validateConnectorPair( CONNECTOR_PAIR_INFO& aPair )
{
    // TODO: Implement validation logic
    aPair.matchedPins = 0;
    aPair.mismatchedPins = 0;
}


//-----------------------------------------------------------------------------
// DIALOG_ADD_CONNECTOR_PAIR implementation
//-----------------------------------------------------------------------------

DIALOG_ADD_CONNECTOR_PAIR::DIALOG_ADD_CONNECTOR_PAIR( wxWindow* aParent, PROJECT* aProject ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Add Connector Pair" ),
                     wxDefaultPosition, wxSize( 400, 300 ),
                     wxDEFAULT_DIALOG_STYLE ),
        m_project( aProject ),
        m_board1Choice( nullptr ),
        m_connector1Choice( nullptr ),
        m_board2Choice( nullptr ),
        m_connector2Choice( nullptr )
{
    createControls();
    populateBoardChoices();

    Layout();
    Centre();
}


void DIALOG_ADD_CONNECTOR_PAIR::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxFlexGridSizer* gridSizer = new wxFlexGridSizer( 4, 2, 10, 10 );
    gridSizer->AddGrowableCol( 1, 1 );

    // Board 1
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Board 1:" ) ),
                    0, wxALIGN_CENTER_VERTICAL );
    m_board1Choice = new wxChoice( this, wxID_ANY );
    gridSizer->Add( m_board1Choice, 1, wxEXPAND );

    // Connector 1
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Connector 1:" ) ),
                    0, wxALIGN_CENTER_VERTICAL );
    m_connector1Choice = new wxChoice( this, wxID_ANY );
    gridSizer->Add( m_connector1Choice, 1, wxEXPAND );

    // Board 2
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Board 2:" ) ),
                    0, wxALIGN_CENTER_VERTICAL );
    m_board2Choice = new wxChoice( this, wxID_ANY );
    gridSizer->Add( m_board2Choice, 1, wxEXPAND );

    // Connector 2
    gridSizer->Add( new wxStaticText( this, wxID_ANY, _( "Connector 2:" ) ),
                    0, wxALIGN_CENTER_VERTICAL );
    m_connector2Choice = new wxChoice( this, wxID_ANY );
    gridSizer->Add( m_connector2Choice, 1, wxEXPAND );

    mainSizer->Add( gridSizer, 1, wxEXPAND | wxALL, 15 );

    // Buttons
    wxSizer* buttonSizer = CreateButtonSizer( wxOK | wxCANCEL );
    mainSizer->Add( buttonSizer, 0, wxEXPAND | wxALL, 10 );

    SetSizer( mainSizer );

    // Bind events
    m_board1Choice->Bind( wxEVT_CHOICE, &DIALOG_ADD_CONNECTOR_PAIR::onBoard1Changed, this );
    m_board2Choice->Bind( wxEVT_CHOICE, &DIALOG_ADD_CONNECTOR_PAIR::onBoard2Changed, this );
}


void DIALOG_ADD_CONNECTOR_PAIR::populateBoardChoices()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    m_boardInfos = projectFile.GetBoardInfos();

    for( const BOARD_INFO& info : m_boardInfos )
    {
        m_board1Choice->Append( info.displayName );
        m_board2Choice->Append( info.displayName );
    }

    if( m_boardInfos.size() >= 2 )
    {
        m_board1Choice->SetSelection( 0 );
        m_board2Choice->SetSelection( 1 );

        populateConnectorChoices( m_connector1Choice, m_boardInfos[0].uuid );
        populateConnectorChoices( m_connector2Choice, m_boardInfos[1].uuid );
    }
}


void DIALOG_ADD_CONNECTOR_PAIR::populateConnectorChoices( wxChoice* aChoice,
                                                           const KIID& aBoardUuid )
{
    aChoice->Clear();

    // TODO: Get actual connector footprints from the board
    // For now, add placeholder entries
    aChoice->Append( _( "J1" ) );
    aChoice->Append( _( "J2" ) );
    aChoice->Append( _( "P1" ) );

    if( aChoice->GetCount() > 0 )
        aChoice->SetSelection( 0 );
}


void DIALOG_ADD_CONNECTOR_PAIR::onBoard1Changed( wxCommandEvent& aEvent )
{
    int sel = m_board1Choice->GetSelection();
    if( sel >= 0 && sel < static_cast<int>( m_boardInfos.size() ) )
        populateConnectorChoices( m_connector1Choice, m_boardInfos[sel].uuid );
}


void DIALOG_ADD_CONNECTOR_PAIR::onBoard2Changed( wxCommandEvent& aEvent )
{
    int sel = m_board2Choice->GetSelection();
    if( sel >= 0 && sel < static_cast<int>( m_boardInfos.size() ) )
        populateConnectorChoices( m_connector2Choice, m_boardInfos[sel].uuid );
}


wxString DIALOG_ADD_CONNECTOR_PAIR::GetBoard1Name() const
{
    return m_board1Choice->GetStringSelection();
}


KIID DIALOG_ADD_CONNECTOR_PAIR::GetBoard1Uuid() const
{
    int sel = m_board1Choice->GetSelection();
    if( sel >= 0 && sel < static_cast<int>( m_boardInfos.size() ) )
        return m_boardInfos[sel].uuid;
    return KIID();
}


wxString DIALOG_ADD_CONNECTOR_PAIR::GetConnector1Ref() const
{
    return m_connector1Choice->GetStringSelection();
}


wxString DIALOG_ADD_CONNECTOR_PAIR::GetBoard2Name() const
{
    return m_board2Choice->GetStringSelection();
}


KIID DIALOG_ADD_CONNECTOR_PAIR::GetBoard2Uuid() const
{
    int sel = m_board2Choice->GetSelection();
    if( sel >= 0 && sel < static_cast<int>( m_boardInfos.size() ) )
        return m_boardInfos[sel].uuid;
    return KIID();
}


wxString DIALOG_ADD_CONNECTOR_PAIR::GetConnector2Ref() const
{
    return m_connector2Choice->GetStringSelection();
}
