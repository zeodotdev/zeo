/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_multi_board_sync.h"

#include <usage_sync.h>

#include <multi_board/component_assignment.h>
#include <netlist_reader/pcb_netlist.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <widgets/wx_html_report_box.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


//-----------------------------------------------------------------------------
// BOARD_SYNC_PANEL implementation
//-----------------------------------------------------------------------------

BOARD_SYNC_PANEL::BOARD_SYNC_PANEL( wxWindow* aParent, const KIID& aBoardUuid,
                                     const wxString& aBoardName ) :
        wxPanel( aParent ),
        m_boardUuid( aBoardUuid ),
        m_boardName( aBoardName ),
        m_statusLabel( nullptr ),
        m_addLabel( nullptr ),
        m_removeLabel( nullptr ),
        m_updateLabel( nullptr ),
        m_componentList( nullptr )
{
    createControls();
}


void BOARD_SYNC_PANEL::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Status summary
    wxStaticBoxSizer* statusBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Status" ) );

    m_statusLabel = new wxStaticText( statusBox->GetStaticBox(), wxID_ANY,
                                       _( "No changes detected" ) );
    m_statusLabel->SetFont( m_statusLabel->GetFont().Bold() );
    statusBox->Add( m_statusLabel, 0, wxALL, 5 );

    wxFlexGridSizer* statsGrid = new wxFlexGridSizer( 3, 2, 5, 15 );

    statsGrid->Add( new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, _( "Add:" ) ) );
    m_addLabel = new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, wxT( "0" ) );
    statsGrid->Add( m_addLabel );

    statsGrid->Add( new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, _( "Remove:" ) ) );
    m_removeLabel = new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, wxT( "0" ) );
    statsGrid->Add( m_removeLabel );

    statsGrid->Add( new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, _( "Update:" ) ) );
    m_updateLabel = new wxStaticText( statusBox->GetStaticBox(), wxID_ANY, wxT( "0" ) );
    statsGrid->Add( m_updateLabel );

    statusBox->Add( statsGrid, 0, wxALL, 5 );
    mainSizer->Add( statusBox, 0, wxEXPAND | wxALL, 5 );

    // Component list
    wxStaticBoxSizer* componentBox = new wxStaticBoxSizer( wxVERTICAL, this, _( "Components" ) );

    m_componentList = new wxDataViewCtrl( componentBox->GetStaticBox(), wxID_ANY,
                                           wxDefaultPosition, wxSize( -1, 200 ) );

    // Add columns
    m_componentList->AppendTextColumn( _( "Reference" ), 0, wxDATAVIEW_CELL_INERT, 80 );
    m_componentList->AppendTextColumn( _( "Action" ), 1, wxDATAVIEW_CELL_INERT, 80 );
    m_componentList->AppendTextColumn( _( "Footprint" ), 2, wxDATAVIEW_CELL_INERT, 150 );
    m_componentList->AppendTextColumn( _( "Value" ), 3, wxDATAVIEW_CELL_INERT, 100 );

    componentBox->Add( m_componentList, 1, wxEXPAND | wxALL, 5 );
    mainSizer->Add( componentBox, 1, wxEXPAND | wxALL, 5 );

    SetSizer( mainSizer );
}


void BOARD_SYNC_PANEL::UpdateStatus( const BOARD_SYNC_STATUS& aStatus )
{
    // Update labels
    m_addLabel->SetLabel( wxString::Format( wxT( "%d" ), aStatus.componentsToAdd ) );
    m_removeLabel->SetLabel( wxString::Format( wxT( "%d" ), aStatus.componentsToRemove ) );
    m_updateLabel->SetLabel( wxString::Format( wxT( "%d" ), aStatus.componentsToUpdate ) );

    // Update status text
    if( aStatus.HasChanges() )
    {
        m_statusLabel->SetLabel( _( "Changes pending" ) );
        m_statusLabel->SetForegroundColour( wxColour( 200, 100, 0 ) );  // Orange
    }
    else if( aStatus.HasIssues() )
    {
        m_statusLabel->SetLabel( _( "Issues detected" ) );
        m_statusLabel->SetForegroundColour( *wxRED );
    }
    else
    {
        m_statusLabel->SetLabel( _( "Up to date" ) );
        m_statusLabel->SetForegroundColour( wxColour( 0, 150, 0 ) );  // Green
    }

    Refresh();
}


//-----------------------------------------------------------------------------
// DIALOG_MULTI_BOARD_SYNC implementation
//-----------------------------------------------------------------------------

DIALOG_MULTI_BOARD_SYNC::DIALOG_MULTI_BOARD_SYNC( PCB_EDIT_FRAME* aParent, NETLIST* aNetlist ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Sync Boards from Schematic" ),
                     wxDefaultPosition, wxSize( 700, 600 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_project( &aParent->Prj() ),
        m_netlist( aNetlist ),
        m_assignmentManager( nullptr ),
        m_boardNotebook( nullptr ),
        m_crossBoardPanel( nullptr ),
        m_crossBoardView( nullptr ),
        m_optionsPanel( nullptr ),
        m_summaryLabel( nullptr ),
        m_reportBox( nullptr ),
        m_deleteUnusedCheck( nullptr ),
        m_replaceFootprintsCheck( nullptr ),
        m_updateFieldsCheck( nullptr ),
        m_testSyncButton( nullptr ),
        m_applySelectedButton( nullptr ),
        m_applyAllButton( nullptr ),
        m_closeButton( nullptr )
{
    m_updater = std::make_unique<MULTI_BOARD_NETLIST_UPDATER>( m_frame, m_project );

    createControls();

    SetMinSize( wxSize( 600, 500 ) );
    Layout();
    Centre();
}


DIALOG_MULTI_BOARD_SYNC::~DIALOG_MULTI_BOARD_SYNC()
{
}


void DIALOG_MULTI_BOARD_SYNC::SetAssignmentManager( COMPONENT_ASSIGNMENT_MANAGER* aManager )
{
    m_assignmentManager = aManager;
    m_updater->SetAssignmentManager( aManager );
}


void DIALOG_MULTI_BOARD_SYNC::createControls()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Summary label at top
    m_summaryLabel = new wxStaticText( this, wxID_ANY, _( "Select a board to view sync status" ) );
    m_summaryLabel->SetFont( m_summaryLabel->GetFont().Bold() );
    mainSizer->Add( m_summaryLabel, 0, wxALL, 10 );

    // Notebook with board tabs
    m_boardNotebook = new wxNotebook( this, wxID_ANY );
    createBoardTabs();
    mainSizer->Add( m_boardNotebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    // Options panel
    createOptionsPanel();
    mainSizer->Add( m_optionsPanel, 0, wxEXPAND | wxALL, 10 );

    // Report box
    m_reportBox = new WX_HTML_REPORT_BOX( this, wxID_ANY, wxDefaultPosition, wxSize( -1, 100 ) );
    mainSizer->Add( m_reportBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    // Button sizer
    wxBoxSizer* buttonSizer = new wxBoxSizer( wxHORIZONTAL );

    m_testSyncButton = new wxButton( this, wxID_ANY, _( "Test Sync" ) );
    m_applySelectedButton = new wxButton( this, wxID_ANY, _( "Apply to Selected" ) );
    m_applyAllButton = new wxButton( this, wxID_ANY, _( "Apply to All" ) );
    m_closeButton = new wxButton( this, wxID_CANCEL, _( "Close" ) );

    buttonSizer->Add( m_testSyncButton, 0, wxRIGHT, 5 );
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add( m_applySelectedButton, 0, wxRIGHT, 5 );
    buttonSizer->Add( m_applyAllButton, 0, wxRIGHT, 5 );
    buttonSizer->Add( m_closeButton, 0 );

    mainSizer->Add( buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    SetSizer( mainSizer );

    // Bind events
    m_testSyncButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SYNC::onTestSync, this );
    m_applySelectedButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SYNC::onApplySelected, this );
    m_applyAllButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SYNC::onApplyAll, this );
    m_closeButton->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_SYNC::onClose, this );
    m_boardNotebook->Bind( wxEVT_NOTEBOOK_PAGE_CHANGED,
                            &DIALOG_MULTI_BOARD_SYNC::onNotebookPageChanged, this );
}


void DIALOG_MULTI_BOARD_SYNC::createBoardTabs()
{
    if( !m_project )
        return;

    PROJECT_FILE& projectFile = m_project->GetProjectFile();
    const auto& boardInfos = projectFile.GetBoardInfos();

    for( const BOARD_INFO& boardInfo : boardInfos )
    {
        BOARD_SYNC_PANEL* panel = new BOARD_SYNC_PANEL( m_boardNotebook, boardInfo.uuid,
                                                         boardInfo.displayName );
        m_boardNotebook->AddPage( panel, boardInfo.displayName );
        m_boardPanels[boardInfo.uuid] = panel;
    }

    // Add cross-board tab
    createCrossBoardPanel();
    m_boardNotebook->AddPage( m_crossBoardPanel, _( "Cross-Board Connections" ) );
}


void DIALOG_MULTI_BOARD_SYNC::createCrossBoardPanel()
{
    m_crossBoardPanel = new wxPanel( m_boardNotebook );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* label = new wxStaticText( m_crossBoardPanel, wxID_ANY,
            _( "Cross-board connector connections:" ) );
    sizer->Add( label, 0, wxALL, 5 );

    m_crossBoardView = new wxDataViewCtrl( m_crossBoardPanel, wxID_ANY,
                                            wxDefaultPosition, wxSize( -1, 200 ) );

    m_crossBoardView->AppendTextColumn( _( "Net" ), 0, wxDATAVIEW_CELL_INERT, 100 );
    m_crossBoardView->AppendTextColumn( _( "Board 1" ), 1, wxDATAVIEW_CELL_INERT, 120 );
    m_crossBoardView->AppendTextColumn( _( "Connector 1" ), 2, wxDATAVIEW_CELL_INERT, 80 );
    m_crossBoardView->AppendTextColumn( _( "Board 2" ), 3, wxDATAVIEW_CELL_INERT, 120 );
    m_crossBoardView->AppendTextColumn( _( "Connector 2" ), 4, wxDATAVIEW_CELL_INERT, 80 );
    m_crossBoardView->AppendTextColumn( _( "Status" ), 5, wxDATAVIEW_CELL_INERT, 80 );

    sizer->Add( m_crossBoardView, 1, wxEXPAND | wxALL, 5 );

    m_crossBoardPanel->SetSizer( sizer );
}


void DIALOG_MULTI_BOARD_SYNC::createOptionsPanel()
{
    m_optionsPanel = new wxPanel( this );

    wxStaticBoxSizer* optionsSizer = new wxStaticBoxSizer( wxVERTICAL, m_optionsPanel,
                                                            _( "Options" ) );

    m_deleteUnusedCheck = new wxCheckBox( optionsSizer->GetStaticBox(), wxID_ANY,
                                           _( "Delete footprints not in netlist" ) );
    m_deleteUnusedCheck->SetValue( true );
    optionsSizer->Add( m_deleteUnusedCheck, 0, wxALL, 3 );

    m_replaceFootprintsCheck = new wxCheckBox( optionsSizer->GetStaticBox(), wxID_ANY,
                                                _( "Replace footprints with mismatched library references" ) );
    m_replaceFootprintsCheck->SetValue( true );
    optionsSizer->Add( m_replaceFootprintsCheck, 0, wxALL, 3 );

    m_updateFieldsCheck = new wxCheckBox( optionsSizer->GetStaticBox(), wxID_ANY,
                                           _( "Update footprint fields from schematic" ) );
    m_updateFieldsCheck->SetValue( true );
    optionsSizer->Add( m_updateFieldsCheck, 0, wxALL, 3 );

    m_optionsPanel->SetSizer( optionsSizer );
}


void DIALOG_MULTI_BOARD_SYNC::RunTestSync()
{
    if( !m_netlist || !m_assignmentManager )
    {
        m_reportBox->Report( _( "Error: Netlist or assignment manager not set" ),
                              RPT_SEVERITY_ERROR );
        return;
    }

    m_reportBox->Clear();
    m_reportBox->Report( _( "Running test sync..." ), RPT_SEVERITY_INFO );

    // Configure updater
    m_updater->SetIsDryRun( true );
    m_updater->SetDeleteUnusedFootprints( m_deleteUnusedCheck->GetValue() );
    m_updater->SetReplaceFootprints( m_replaceFootprintsCheck->GetValue() );
    m_updater->SetReporter( m_reportBox );

    // Get sync status for all boards
    m_syncStatus = m_updater->GetSyncStatus( *m_netlist );

    // Update panels
    refreshBoardPanels();
    updateStatusSummary();

    // Validate cross-board connections
    auto validationResults = m_updater->ValidateCrossBoardConnectors();
    for( const auto& result : validationResults )
    {
        SEVERITY severity = RPT_SEVERITY_INFO;
        if( result.severity == CROSS_BOARD_VALIDATION_RESULT::Severity::WARNING )
            severity = RPT_SEVERITY_WARNING;
        else if( result.severity == CROSS_BOARD_VALIDATION_RESULT::Severity::ERROR )
            severity = RPT_SEVERITY_ERROR;

        m_reportBox->Report( result.message, severity );
    }

    m_reportBox->Report( _( "Test sync complete." ), RPT_SEVERITY_INFO );
}


void DIALOG_MULTI_BOARD_SYNC::refreshBoardPanels()
{
    for( auto& [uuid, panel] : m_boardPanels )
    {
        auto it = m_syncStatus.find( uuid );
        if( it != m_syncStatus.end() )
            panel->UpdateStatus( it->second );
    }
}


void DIALOG_MULTI_BOARD_SYNC::updateStatusSummary()
{
    int totalAdd = 0;
    int totalRemove = 0;
    int totalUpdate = 0;

    for( const auto& [uuid, status] : m_syncStatus )
    {
        totalAdd += status.componentsToAdd;
        totalRemove += status.componentsToRemove;
        totalUpdate += status.componentsToUpdate;
    }

    wxString summary = wxString::Format(
            _( "Total changes: %d to add, %d to remove, %d to update across %zu boards" ),
            totalAdd, totalRemove, totalUpdate, m_syncStatus.size() );

    m_summaryLabel->SetLabel( summary );
}


bool DIALOG_MULTI_BOARD_SYNC::ApplyToSelected()
{
    int pageIdx = m_boardNotebook->GetSelection();
    if( pageIdx < 0 || pageIdx >= (int) m_boardPanels.size() )
        return false;

    // Get the selected board panel
    wxWindow* page = m_boardNotebook->GetPage( pageIdx );
    BOARD_SYNC_PANEL* panel = dynamic_cast<BOARD_SYNC_PANEL*>( page );
    if( !panel )
        return false;

    KIID boardUuid = panel->GetBoardUuid();

    m_reportBox->Clear();
    m_reportBox->Report( wxString::Format( _( "Applying sync to board..." ) ), RPT_SEVERITY_INFO );

    // Configure updater for real changes
    m_updater->SetIsDryRun( false );
    m_updater->SetDeleteUnusedFootprints( m_deleteUnusedCheck->GetValue() );
    m_updater->SetReplaceFootprints( m_replaceFootprintsCheck->GetValue() );
    m_updater->SetReporter( m_reportBox );

    // Partition netlist and update selected board
    auto partitioned = m_updater->PartitionNetlist( *m_netlist );
    auto it = partitioned.find( boardUuid );
    if( it == partitioned.end() )
    {
        m_reportBox->Report( _( "Error: Board not found in partitioned netlist" ),
                              RPT_SEVERITY_ERROR );
        return false;
    }

    bool success = m_updater->UpdateBoard( boardUuid, *it->second );

    if( success )
        m_reportBox->Report( _( "Sync complete." ), RPT_SEVERITY_INFO );
    else
        m_reportBox->Report( _( "Sync completed with errors." ), RPT_SEVERITY_WARNING );

    // Refresh status
    RunTestSync();

    return success;
}


bool DIALOG_MULTI_BOARD_SYNC::ApplyToAll()
{
    m_reportBox->Clear();
    m_reportBox->Report( _( "Applying sync to all boards..." ), RPT_SEVERITY_INFO );

    // Configure updater for real changes
    m_updater->SetIsDryRun( false );
    m_updater->SetDeleteUnusedFootprints( m_deleteUnusedCheck->GetValue() );
    m_updater->SetReplaceFootprints( m_replaceFootprintsCheck->GetValue() );
    m_updater->SetReporter( m_reportBox );

    bool success = m_updater->UpdateAllBoards( *m_netlist );

    if( success )
    {
        m_reportBox->Report( _( "All boards synced successfully." ), RPT_SEVERITY_INFO );
        USAGE_SYNC::Instance()->TrackEvent( "mbs.sync_to_pcb", "pcbnew" );
    }
    else
    {
        m_reportBox->Report( _( "Sync completed with errors." ), RPT_SEVERITY_WARNING );
    }

    // Refresh status
    RunTestSync();

    return success;
}


void DIALOG_MULTI_BOARD_SYNC::onTestSync( wxCommandEvent& aEvent )
{
    RunTestSync();
}


void DIALOG_MULTI_BOARD_SYNC::onApplySelected( wxCommandEvent& aEvent )
{
    ApplyToSelected();
}


void DIALOG_MULTI_BOARD_SYNC::onApplyAll( wxCommandEvent& aEvent )
{
    ApplyToAll();
}


void DIALOG_MULTI_BOARD_SYNC::onClose( wxCommandEvent& aEvent )
{
    EndModal( wxID_CANCEL );
}


void DIALOG_MULTI_BOARD_SYNC::onNotebookPageChanged( wxBookCtrlEvent& aEvent )
{
    // Update UI based on selected tab
    int pageIdx = aEvent.GetSelection();
    bool isBoardTab = pageIdx < (int) m_boardPanels.size();

    m_applySelectedButton->Enable( isBoardTab );
}
