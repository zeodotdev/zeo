/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_module_block_properties.h"

#include <sch_module_block.h>
#include <sch_module_pin.h>
#include <sch_edit_frame.h>
#include <base_units.h>
#include <kiway.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <multi_board_peer_open.h>
#include <widgets/wx_grid.h>

#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


// Field-grid layout (General tab)
enum
{
    FIELD_ROW_DISPLAY_NAME = 0,
    FIELD_ROW_MBS_REF,
    FIELD_ROW_LOCAL_REF,
    FIELD_ROW_SUB_PROJECT,
    FIELD_ROW_COUNT
};

enum
{
    FIELD_COL_NAME = 0,
    FIELD_COL_VALUE,
    FIELD_COL_COUNT
};


// Pin-grid layout (Pin Functions tab)
enum
{
    PIN_COL_NUMBER = 0,
    PIN_COL_LABEL,
    PIN_COL_REF,
    PIN_COL_COUNT
};


DIALOG_MODULE_BLOCK_PROPERTIES::DIALOG_MODULE_BLOCK_PROPERTIES( SCH_EDIT_FRAME* aFrame,
                                                                SCH_MODULE_BLOCK* aBlock ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Module Block Properties" ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_block( aBlock )
{
    // Force natural-size on every open instead of restoring the
    // (possibly oversized) saved dialog geometry. DIALOG_SHIM persists
    // window size keyed by dialog title; this dialog historically
    // opened screen-tall, and that bad size got stuck in user prefs.
    // Mirrors how content-driven dialogs (e.g. rule editors) opt out
    // of the save/restore so the natural sizer fit is used every time.
    // The user can still resize (wxRESIZE_BORDER is set) — the size
    // just isn't persisted.
    m_useCalculatedSize = true;

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    m_notebook = new wxNotebook( this, wxID_ANY );

    // ============================================================
    // General tab
    // ============================================================
    wxPanel*    generalPage = new wxPanel( m_notebook );
    wxBoxSizer* generalSizer = new wxBoxSizer( wxVERTICAL );

    // ---- Fields box ----
    wxStaticBoxSizer* sbFields = new wxStaticBoxSizer(
            new wxStaticBox( generalPage, wxID_ANY, _( "Fields" ) ), wxVERTICAL );

    m_fieldsGrid = new WX_GRID( sbFields->GetStaticBox(), wxID_ANY );
    m_fieldsGrid->CreateGrid( FIELD_ROW_COUNT, FIELD_COL_COUNT );
    m_fieldsGrid->EnableDragRowSize( false );
    m_fieldsGrid->EnableDragColMove( false );
    m_fieldsGrid->SetRowLabelSize( 0 );
    m_fieldsGrid->SetColLabelValue( FIELD_COL_NAME, _( "Name" ) );
    m_fieldsGrid->SetColLabelValue( FIELD_COL_VALUE, _( "Value" ) );
    m_fieldsGrid->SetColLabelAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
    m_fieldsGrid->SetColSize( FIELD_COL_NAME, 160 );
    m_fieldsGrid->SetColSize( FIELD_COL_VALUE, 320 );
    m_fieldsGrid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_fieldsGrid->SetMinSize( wxSize( -1, 140 ) );

    sbFields->Add( m_fieldsGrid, 1, wxEXPAND | wxALL, 5 );
    generalSizer->Add( sbFields, 1, wxEXPAND | wxALL, 5 );

    // ---- Source box ----
    wxStaticBoxSizer* sbSource = new wxStaticBoxSizer(
            new wxStaticBox( generalPage, wxID_ANY, _( "Source schematic" ) ), wxHORIZONTAL );

    m_sourceSchPath = new wxTextCtrl( sbSource->GetStaticBox(), wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_READONLY | wxBORDER_NONE );
    sbSource->Add( m_sourceSchPath, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_openSourceBtn = new wxButton( sbSource->GetStaticBox(), wxID_ANY, _( "Open…" ) );
    sbSource->Add( m_openSourceBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, 5 );

    generalSizer->Add( sbSource, 0, wxEXPAND | wxALL, 5 );

    // ---- Geometry box ----
    // Use the UNIT_BINDER triplet pattern (label + ctrl + units) so the
    // user's chosen display units (mm / mils / inches) and validation
    // come for free. Mirrors DIALOG_PAD_PROPERTIES / DIALOG_SYMBOL_PROPERTIES
    // styling — keeps every property dialog consistent.
    wxStaticBoxSizer* sbGeom = new wxStaticBoxSizer(
            new wxStaticBox( generalPage, wxID_ANY, _( "Geometry" ) ), wxVERTICAL );

    wxFlexGridSizer* geomGrid = new wxFlexGridSizer( 0, 3, 6, 8 );
    geomGrid->AddGrowableCol( 1, 1 );

    m_widthLabel = new wxStaticText( sbGeom->GetStaticBox(), wxID_ANY, _( "Width:" ) );
    m_widthCtrl  = new wxTextCtrl( sbGeom->GetStaticBox(), wxID_ANY, wxEmptyString );
    m_widthUnits = new wxStaticText( sbGeom->GetStaticBox(), wxID_ANY, wxEmptyString );

    geomGrid->Add( m_widthLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    geomGrid->Add( m_widthCtrl,  1, wxEXPAND );
    geomGrid->Add( m_widthUnits, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );

    m_heightLabel = new wxStaticText( sbGeom->GetStaticBox(), wxID_ANY, _( "Height:" ) );
    m_heightCtrl  = new wxTextCtrl( sbGeom->GetStaticBox(), wxID_ANY, wxEmptyString );
    m_heightUnits = new wxStaticText( sbGeom->GetStaticBox(), wxID_ANY, wxEmptyString );

    geomGrid->Add( m_heightLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    geomGrid->Add( m_heightCtrl,  1, wxEXPAND );
    geomGrid->Add( m_heightUnits, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );

    sbGeom->Add( geomGrid, 0, wxEXPAND | wxALL, 5 );
    generalSizer->Add( sbGeom, 0, wxEXPAND | wxALL, 5 );

    m_width  = std::make_unique<UNIT_BINDER>( aFrame, m_widthLabel,  m_widthCtrl,  m_widthUnits );
    m_height = std::make_unique<UNIT_BINDER>( aFrame, m_heightLabel, m_heightCtrl, m_heightUnits );

    generalPage->SetSizer( generalSizer );
    m_notebook->AddPage( generalPage, _( "General" ), true );

    // ============================================================
    // Pin Functions tab (read-only)
    // ============================================================
    wxPanel*    pinPage = new wxPanel( m_notebook );
    wxBoxSizer* pinSizer = new wxBoxSizer( wxVERTICAL );

    m_pinGrid = new WX_GRID( pinPage, wxID_ANY );
    m_pinGrid->CreateGrid( 0, PIN_COL_COUNT );
    m_pinGrid->EnableEditing( false );
    m_pinGrid->EnableDragRowSize( false );
    m_pinGrid->EnableDragColMove( false );
    m_pinGrid->SetRowLabelSize( 0 );
    m_pinGrid->SetColLabelValue( PIN_COL_NUMBER, _( "Pin Number" ) );
    m_pinGrid->SetColLabelValue( PIN_COL_LABEL,  _( "Pin Label" ) );
    m_pinGrid->SetColLabelValue( PIN_COL_REF,    _( "Connector Ref" ) );
    m_pinGrid->SetColLabelAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
    m_pinGrid->SetColSize( PIN_COL_NUMBER, 100 );
    m_pinGrid->SetColSize( PIN_COL_LABEL,  300 );
    m_pinGrid->SetColSize( PIN_COL_REF,    100 );
    m_pinGrid->SetSelectionMode( wxGrid::wxGridSelectRows );

    pinSizer->Add( m_pinGrid, 1, wxEXPAND | wxALL, 5 );

    pinPage->SetSizer( pinSizer );
    m_notebook->AddPage( pinPage, _( "Pin Functions" ), false );

    mainSizer->Add( m_notebook, 1, wxEXPAND | wxALL, 10 );

    // Standard wxStdDialogButtonSizer with OK + Cancel. Hand-coded
    // dialogs without an .fbp twin must create the buttons explicitly
    // (DIALOG_SHIM::SetupStandardButtons only re-labels existing
    // buttons; it doesn't create them). Add the sizer to the main
    // layout *before* calling SetupStandardButtons so the relabel walk
    // can find them.
    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    wxButton* okBtn     = new wxButton( this, wxID_OK );
    wxButton* cancelBtn = new wxButton( this, wxID_CANCEL );
    sdb->AddButton( okBtn );
    sdb->AddButton( cancelBtn );
    sdb->Realize();

    mainSizer->Add( sdb, 0, wxEXPAND | wxALL, 8 );

    SetSizer( mainSizer );

    // Lay out children + size dialog to natural sizer size BEFORE
    // finishDialogSettings(). DIALOG_SHIM::finishDialogSettings calls
    // GetSizer()->SetSizeHints(this) which locks in the dialog's
    // current size as its minimum — so the dialog must already be at
    // its natural (compact) size at that point. Without an explicit
    // Layout()+Fit() pair the dialog stays at the ctor's wxDefaultSize
    // (which can be screen-tall on macOS), and SetSizeHints then makes
    // that the floor — user can't shrink it. Mirrors the pattern in
    // dialog_symbol_properties.cpp:410-416.
    Layout();
    mainSizer->Fit( this );

    // Apply platform-conventional button ordering / default-button
    // handling on top of the now-laid-out sizer hierarchy.
    SetupStandardButtons();

    m_openSourceBtn->Bind( wxEVT_BUTTON,
                           &DIALOG_MODULE_BLOCK_PROPERTIES::onOpenSourceSchematic, this );

    finishDialogSettings();
}


DIALOG_MODULE_BLOCK_PROPERTIES::~DIALOG_MODULE_BLOCK_PROPERTIES()
{
}


bool DIALOG_MODULE_BLOCK_PROPERTIES::TransferDataToWindow()
{
    if( !m_block )
        return false;

    // ---- Fields grid ----
    auto setField = [&]( int aRow, const wxString& aName, const wxString& aValue,
                         bool aReadOnly )
    {
        m_fieldsGrid->SetCellValue( aRow, FIELD_COL_NAME, aName );
        m_fieldsGrid->SetCellValue( aRow, FIELD_COL_VALUE, aValue );

        // Field name column is always read-only — only Value is user-editable.
        m_fieldsGrid->SetReadOnly( aRow, FIELD_COL_NAME, true );
        m_fieldsGrid->SetReadOnly( aRow, FIELD_COL_VALUE, aReadOnly );
    };

    setField( FIELD_ROW_DISPLAY_NAME, _( "Display Name" ),  m_block->GetDisplayName(),   false );
    setField( FIELD_ROW_MBS_REF,      _( "MBS Annotation" ), m_block->GetMbsReference(),  false );
    setField( FIELD_ROW_LOCAL_REF,    _( "Local Annotation" ), m_block->GetComponentRef(), true );
    setField( FIELD_ROW_SUB_PROJECT,  _( "Sub-project" ),    m_block->GetSubProjectPath(), true );

    // ---- Source schematic path ----
    wxString schDisplay;

    if( !m_block->GetSubProjectPath().IsEmpty() && m_frame )
    {
        wxFileName subPro( m_block->GetSubProjectPath() );

        if( !subPro.IsAbsolute() )
            subPro.MakeAbsolute( m_frame->Prj().GetProjectPath() );

        wxFileName mainSch = MultiBoardMainSchematic( subPro );
        schDisplay = mainSch.GetFullPath();
    }
    else
    {
        schDisplay = m_block->GetSubProjectPath();
    }

    m_sourceSchPath->SetValue( schDisplay );
    m_openSourceBtn->Enable( !schDisplay.IsEmpty() && wxFileName::FileExists( schDisplay ) );

    // ---- Geometry ----
    m_width->SetValue( m_block->GetSize().x );
    m_height->SetValue( m_block->GetSize().y );

    // ---- Pin Functions grid ----
    const auto& pins = m_block->GetPins();

    if( m_pinGrid->GetNumberRows() > 0 )
        m_pinGrid->DeleteRows( 0, m_pinGrid->GetNumberRows() );

    m_pinGrid->AppendRows( static_cast<int>( pins.size() ) );

    int row = 0;

    for( const SCH_MODULE_PIN* pin : pins )
    {
        m_pinGrid->SetCellValue( row, PIN_COL_NUMBER, pin->GetPinNumber() );
        m_pinGrid->SetCellValue( row, PIN_COL_LABEL,  pin->GetText() );
        m_pinGrid->SetCellValue( row, PIN_COL_REF,    pin->GetComponentRef() );
        ++row;
    }

    return true;
}


bool DIALOG_MODULE_BLOCK_PROPERTIES::TransferDataFromWindow()
{
    if( !m_block )
        return false;

    if( !m_fieldsGrid->CommitPendingChanges() )
        return false;

    m_block->SetDisplayName(
            m_fieldsGrid->GetCellValue( FIELD_ROW_DISPLAY_NAME, FIELD_COL_VALUE ) );
    m_block->SetMbsReference(
            m_fieldsGrid->GetCellValue( FIELD_ROW_MBS_REF, FIELD_COL_VALUE ) );

    // UNIT_BINDER returns IU directly, already validated and unit-
    // converted from the user's display preference. Guard against a
    // zero size from an empty / invalid input by falling back to the
    // current block size.
    int newWidth  = m_width->GetIntValue();
    int newHeight = m_height->GetIntValue();

    if( newWidth  <= 0 ) newWidth  = m_block->GetSize().x;
    if( newHeight <= 0 ) newHeight = m_block->GetSize().y;

    m_block->SetSize( VECTOR2I( newWidth, newHeight ) );

    return true;
}


void DIALOG_MODULE_BLOCK_PROPERTIES::onOpenSourceSchematic( wxCommandEvent& aEvent )
{
    if( !m_frame || !m_block )
        return;

    // Use the dedicated peer-spawn helper rather than OpenProjectFiles.
    // OpenProjectFiles replaces the active project — it would unload the
    // multi-board container and switch the frame into single-board mode,
    // breaking subsequent attempts to reopen the MBS. The peer helper
    // loads the sub-project as a sibling PROJECT and spawns a new
    // SCH_EDIT_FRAME pinned to it via SetPrjOverride, leaving the MBSCH
    // frame untouched.
    if( !OpenSubProjectInPeerEditor( m_frame, m_block->GetSubProjectUuid(),
                                     /*aWantPcb=*/false ) )
    {
        wxMessageBox( _( "Could not open the source schematic — sub-project "
                         "missing or container project not found." ),
                      _( "Open Failed" ), wxOK | wxICON_ERROR, this );
    }
}
