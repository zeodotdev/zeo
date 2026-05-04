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

#include "dialog_multi_board_rules.h"

#include <pgm_base.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>

#include <wx/button.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


// Lengths are persisted in nanometers but authored in millimeters here —
// less typing for the user, matches DRC violation output. Conversion is
// 1 mm = 1e6 nm.
namespace
{
constexpr double NM_PER_MM = 1.0e6;


// Read a grid cell, trim whitespace, return as wxString.
wxString cellTrim( wxGrid* aGrid, int aRow, int aCol )
{
    wxString v = aGrid->GetCellValue( aRow, aCol );
    v.Trim( true ).Trim( false );
    return v;
}


// Read a grid cell as a long int. Returns false on parse failure or if
// the value is below `aMin`. On failure, populates `*aErrMsg` with a
// descriptive error.
bool cellToLong( wxGrid* aGrid, int aRow, int aCol, long aMin, long* aOut, wxString* aErrMsg )
{
    wxString v = cellTrim( aGrid, aRow, aCol );

    if( v.IsEmpty() )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d is empty." ), aRow + 1, aCol + 1 );
        return false;
    }

    if( !v.ToLong( aOut ) )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: '%s' is not a whole number." ),
                                      aRow + 1, aCol + 1, v );
        return false;
    }

    if( *aOut < aMin )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: %ld is below the minimum (%ld)." ),
                                      aRow + 1, aCol + 1, *aOut, aMin );
        return false;
    }

    return true;
}


// Read a grid cell as a double. Returns false on parse failure or below
// `aMin`. On failure, populates `*aErrMsg`.
bool cellToDouble( wxGrid* aGrid, int aRow, int aCol, double aMin, double* aOut,
                   wxString* aErrMsg )
{
    wxString v = cellTrim( aGrid, aRow, aCol );

    if( v.IsEmpty() )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d is empty." ), aRow + 1, aCol + 1 );
        return false;
    }

    if( !v.ToDouble( aOut ) )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: '%s' is not a number." ),
                                      aRow + 1, aCol + 1, v );
        return false;
    }

    if( *aOut < aMin )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: %.3f is below the minimum (%.3f)." ),
                                      aRow + 1, aCol + 1, *aOut, aMin );
        return false;
    }

    return true;
}


// Optional double — empty cell maps to 0 (which the rule treats as "use default").
// Returns false only on parse failure of a non-empty value.
bool cellToOptionalDouble( wxGrid* aGrid, int aRow, int aCol, double* aOut, wxString* aErrMsg )
{
    wxString v = cellTrim( aGrid, aRow, aCol );

    if( v.IsEmpty() )
    {
        *aOut = 0.0;
        return true;
    }

    if( !v.ToDouble( aOut ) )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: '%s' is not a number." ),
                                      aRow + 1, aCol + 1, v );
        return false;
    }

    if( *aOut < 0.0 )
    {
        *aErrMsg = wxString::Format( _( "Row %d column %d: %.3f cannot be negative." ),
                                      aRow + 1, aCol + 1, *aOut );
        return false;
    }

    return true;
}
}   // namespace


DIALOG_MULTI_BOARD_RULES::DIALOG_MULTI_BOARD_RULES( wxWindow* aParent, PROJECT_FILE* aProject ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Cross-Board ERC/DRC Rules" ),
                     wxDefaultPosition, wxSize( 720, 540 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_project( aProject ),
        m_minPowerGrid( nullptr ),
        m_maxLengthGrid( nullptr ),
        m_diffPairGrid( nullptr ),
        m_currentGrid( nullptr ),
        m_voltageGrid( nullptr )
{
    wxASSERT_MSG( m_project != nullptr, wxT( "DIALOG_MULTI_BOARD_RULES requires a project" ) );

    buildUI();
    populateGridsFromProject();
    Centre();
}


DIALOG_MULTI_BOARD_RULES::~DIALOG_MULTI_BOARD_RULES() = default;


void DIALOG_MULTI_BOARD_RULES::buildUI()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* header = new wxStaticText( this, wxID_ANY,
            _( "Cross-board ERC/DRC rules apply to nets that span multiple sub-projects "
               "via the multi-board schematic. Each tab configures one rule type." ) );
    header->Wrap( 680 );
    mainSizer->Add( header, 0, wxALL, 8 );

    wxNotebook* notebook = new wxNotebook( this, wxID_ANY );

    buildMinPowerPinsTab( notebook );
    buildMaxLengthTab( notebook );
    buildDiffPairsTab( notebook );
    buildCurrentTab( notebook );
    buildVoltageTab( notebook );

    mainSizer->Add( notebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    // Standard OK / Apply / Cancel button row at the bottom.
    wxBoxSizer* buttonRow = new wxBoxSizer( wxHORIZONTAL );
    buttonRow->AddStretchSpacer( 1 );

    wxButton* applyBtn = new wxButton( this, wxID_APPLY, _( "Apply" ) );
    wxButton* okBtn    = new wxButton( this, wxID_OK,    _( "OK" ) );
    wxButton* cancelBtn = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );

    buttonRow->Add( applyBtn, 0, wxALL, 4 );
    buttonRow->Add( okBtn,    0, wxALL, 4 );
    buttonRow->Add( cancelBtn, 0, wxALL, 4 );

    okBtn->SetDefault();

    mainSizer->Add( buttonRow, 0, wxEXPAND | wxALL, 4 );

    SetSizer( mainSizer );

    applyBtn->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_RULES::onApply, this );
    okBtn->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_RULES::onOK, this );
    cancelBtn->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_RULES::onCancel, this );
}


wxGrid* DIALOG_MULTI_BOARD_RULES::buildGridPanel( wxWindow* aParent,
                                                  const wxArrayString& aColumnLabels,
                                                  const wxArrayInt& aColumnWidths,
                                                  wxButton** aAddBtnOut,
                                                  wxButton** aRemoveBtnOut )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxGrid* grid = new wxGrid( aParent, wxID_ANY );
    grid->CreateGrid( 0, static_cast<int>( aColumnLabels.size() ) );
    grid->EnableEditing( true );
    grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    grid->HideRowLabels();

    for( int i = 0; i < static_cast<int>( aColumnLabels.size() ); ++i )
    {
        grid->SetColLabelValue( i, aColumnLabels[i] );

        if( i < static_cast<int>( aColumnWidths.size() ) && aColumnWidths[i] > 0 )
            grid->SetColSize( i, aColumnWidths[i] );
    }

    sizer->Add( grid, 1, wxEXPAND | wxALL, 4 );

    wxBoxSizer* btnRow = new wxBoxSizer( wxHORIZONTAL );

    wxButton* addBtn    = new wxButton( aParent, wxID_ANY, _( "Add Row" ) );
    wxButton* removeBtn = new wxButton( aParent, wxID_ANY, _( "Remove Selected" ) );

    btnRow->Add( addBtn,    0, wxALL, 4 );
    btnRow->Add( removeBtn, 0, wxALL, 4 );
    btnRow->AddStretchSpacer( 1 );

    sizer->Add( btnRow, 0, wxEXPAND );

    aParent->SetSizer( sizer );

    // Map the button to its grid so the single shared handler can route
    // correctly. The wxClientData approach (storing the grid* on the
    // button) doesn't survive Bind without raw void* casts; an explicit
    // map is clearer.
    m_buttonToGrid[addBtn->GetId()]    = grid;
    m_buttonToGrid[removeBtn->GetId()] = grid;

    addBtn->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_RULES::onAddRow, this );
    removeBtn->Bind( wxEVT_BUTTON, &DIALOG_MULTI_BOARD_RULES::onRemoveSelected, this );

    if( aAddBtnOut )
        *aAddBtnOut = addBtn;

    if( aRemoveBtnOut )
        *aRemoveBtnOut = removeBtn;

    return grid;
}


void DIALOG_MULTI_BOARD_RULES::buildMinPowerPinsTab( wxNotebook* aNotebook )
{
    wxPanel* panel = new wxPanel( aNotebook );

    wxArrayString labels;
    labels.Add( _( "Net Name" ) );
    labels.Add( _( "Min Pins" ) );

    wxArrayInt widths;
    widths.Add( 320 );
    widths.Add( 100 );

    m_minPowerGrid = buildGridPanel( panel, labels, widths, nullptr, nullptr );

    aNotebook->AddPage( panel, _( "Min Power Pins" ) );
}


void DIALOG_MULTI_BOARD_RULES::buildMaxLengthTab( wxNotebook* aNotebook )
{
    wxPanel* panel = new wxPanel( aNotebook );

    wxArrayString labels;
    labels.Add( _( "Net Name" ) );
    labels.Add( _( "Max Length (mm)" ) );

    wxArrayInt widths;
    widths.Add( 320 );
    widths.Add( 140 );

    m_maxLengthGrid = buildGridPanel( panel, labels, widths, nullptr, nullptr );

    aNotebook->AddPage( panel, _( "Max Length" ) );
}


void DIALOG_MULTI_BOARD_RULES::buildDiffPairsTab( wxNotebook* aNotebook )
{
    wxPanel* panel = new wxPanel( aNotebook );

    wxArrayString labels;
    labels.Add( _( "Positive Net" ) );
    labels.Add( _( "Negative Net" ) );

    wxArrayInt widths;
    widths.Add( 220 );
    widths.Add( 220 );

    m_diffPairGrid = buildGridPanel( panel, labels, widths, nullptr, nullptr );

    aNotebook->AddPage( panel, _( "Diff Pairs" ) );
}


void DIALOG_MULTI_BOARD_RULES::buildCurrentTab( wxNotebook* aNotebook )
{
    wxPanel* panel = new wxPanel( aNotebook );

    wxArrayString labels;
    labels.Add( _( "Net Name" ) );
    labels.Add( _( "Expected (A)" ) );
    labels.Add( _( "Per-Pin Rating (A)" ) );

    wxArrayInt widths;
    widths.Add( 240 );
    widths.Add( 120 );
    widths.Add( 140 );

    m_currentGrid = buildGridPanel( panel, labels, widths, nullptr, nullptr );

    aNotebook->AddPage( panel, _( "Current" ) );
}


void DIALOG_MULTI_BOARD_RULES::buildVoltageTab( wxNotebook* aNotebook )
{
    wxPanel* panel = new wxPanel( aNotebook );

    wxBoxSizer* outerSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* hint = new wxStaticText( panel, wxID_ANY,
            _( "Trace Width / Sheet R / Pin R are optional overrides — leave blank to use "
               "documented defaults (250 µm width, 0.5 mΩ/sq for 1 oz copper, 20 mΩ per pin)." ) );
    hint->Wrap( 660 );
    outerSizer->Add( hint, 0, wxALL, 4 );

    // Build the grid panel inside its own sub-panel so the hint sits
    // above and the grid still gets the standard buttons row.
    wxPanel* innerPanel = new wxPanel( panel );

    wxArrayString labels;
    labels.Add( _( "Net Name" ) );
    labels.Add( _( "Expected (A)" ) );
    labels.Add( _( "Max Drop (mV)" ) );
    labels.Add( _( "Trace Width (µm)" ) );
    labels.Add( _( "Sheet R (mΩ/sq)" ) );
    labels.Add( _( "Pin R (mΩ)" ) );

    wxArrayInt widths;
    widths.Add( 140 );
    widths.Add(  90 );
    widths.Add( 100 );
    widths.Add( 110 );
    widths.Add( 110 );
    widths.Add(  90 );

    m_voltageGrid = buildGridPanel( innerPanel, labels, widths, nullptr, nullptr );

    outerSizer->Add( innerPanel, 1, wxEXPAND );
    panel->SetSizer( outerSizer );

    aNotebook->AddPage( panel, _( "Voltage Drop" ) );
}


void DIALOG_MULTI_BOARD_RULES::populateGridsFromProject()
{
    // Min power pins
    {
        const auto& src = m_project->GetMinPowerPins();
        m_minPowerGrid->AppendRows( static_cast<int>( src.size() ) );
        int row = 0;

        for( const auto& [net, n] : src )
        {
            m_minPowerGrid->SetCellValue( row, 0, net );
            m_minPowerGrid->SetCellValue( row, 1, wxString::Format( wxT( "%d" ), n ) );
            ++row;
        }
    }

    // Max length: stored in nm, displayed in mm
    {
        const auto& src = m_project->GetMaxLengthNm();
        m_maxLengthGrid->AppendRows( static_cast<int>( src.size() ) );
        int row = 0;

        for( const auto& [net, nm] : src )
        {
            double mm = static_cast<double>( nm ) / NM_PER_MM;
            m_maxLengthGrid->SetCellValue( row, 0, net );
            m_maxLengthGrid->SetCellValue( row, 1, wxString::Format( wxT( "%.3f" ), mm ) );
            ++row;
        }
    }

    // Diff pairs
    {
        const auto& src = m_project->GetCrossBoardDiffPairs();
        m_diffPairGrid->AppendRows( static_cast<int>( src.size() ) );
        int row = 0;

        for( const auto& [a, b] : src )
        {
            m_diffPairGrid->SetCellValue( row, 0, a );
            m_diffPairGrid->SetCellValue( row, 1, b );
            ++row;
        }
    }

    // Current
    {
        const auto& src = m_project->GetCrossBoardCurrentRules();
        m_currentGrid->AppendRows( static_cast<int>( src.size() ) );
        int row = 0;

        for( const auto& [net, rule] : src )
        {
            m_currentGrid->SetCellValue( row, 0, net );
            m_currentGrid->SetCellValue( row, 1, wxString::Format( wxT( "%.3f" ), rule.expectedAmps ) );
            m_currentGrid->SetCellValue( row, 2, wxString::Format( wxT( "%.3f" ), rule.pinRatingAmps ) );
            ++row;
        }
    }

    // Voltage drop
    {
        const auto& src = m_project->GetCrossBoardVoltageRules();
        m_voltageGrid->AppendRows( static_cast<int>( src.size() ) );
        int row = 0;

        auto fmtOpt = []( double v ) -> wxString
        {
            return v > 0.0 ? wxString::Format( wxT( "%.3f" ), v ) : wxString();
        };

        for( const auto& [net, rule] : src )
        {
            m_voltageGrid->SetCellValue( row, 0, net );
            m_voltageGrid->SetCellValue( row, 1,
                    wxString::Format( wxT( "%.3f" ), rule.expectedAmps ) );
            m_voltageGrid->SetCellValue( row, 2,
                    wxString::Format( wxT( "%.3f" ), rule.maxDropMv ) );
            m_voltageGrid->SetCellValue( row, 3, fmtOpt( rule.traceWidthUm ) );
            m_voltageGrid->SetCellValue( row, 4, fmtOpt( rule.traceSheetRMOhmsPerSq ) );
            m_voltageGrid->SetCellValue( row, 5, fmtOpt( rule.contactRPerPinMOhms ) );
            ++row;
        }
    }
}


bool DIALOG_MULTI_BOARD_RULES::applyChanges()
{
    // Validate every row in every grid first, accumulating any error.
    // We commit nothing if any row is invalid — partial commits would
    // be confusing and hard to undo.
    wxString err;

    auto requireNetName = [&]( wxGrid* grid, int row, int col ) -> bool
    {
        wxString name = cellTrim( grid, row, col );

        if( name.IsEmpty() )
        {
            err = wxString::Format( _( "Row %d column %d: net name is empty." ),
                                     row + 1, col + 1 );
            return false;
        }

        return true;
    };

    // ---- Min power pins validation ----
    std::map<wxString, int> newMinPowerPins;

    for( int row = 0; row < m_minPowerGrid->GetNumberRows(); ++row )
    {
        if( !requireNetName( m_minPowerGrid, row, 0 ) )
        {
            wxMessageBox( _( "Min Power Pins: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        long n = 0;

        if( !cellToLong( m_minPowerGrid, row, 1, 1, &n, &err ) )
        {
            wxMessageBox( _( "Min Power Pins: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        newMinPowerPins[cellTrim( m_minPowerGrid, row, 0 )] = static_cast<int>( n );
    }

    // ---- Max length validation ----
    std::map<wxString, int64_t> newMaxLengthNm;

    for( int row = 0; row < m_maxLengthGrid->GetNumberRows(); ++row )
    {
        if( !requireNetName( m_maxLengthGrid, row, 0 ) )
        {
            wxMessageBox( _( "Max Length: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        double mm = 0.0;

        if( !cellToDouble( m_maxLengthGrid, row, 1, 0.001, &mm, &err ) )
        {
            wxMessageBox( _( "Max Length: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        newMaxLengthNm[cellTrim( m_maxLengthGrid, row, 0 )] =
                static_cast<int64_t>( mm * NM_PER_MM );
    }

    // ---- Diff pairs validation ----
    std::vector<std::pair<wxString, wxString>> newDiffPairs;

    for( int row = 0; row < m_diffPairGrid->GetNumberRows(); ++row )
    {
        if( !requireNetName( m_diffPairGrid, row, 0 )
            || !requireNetName( m_diffPairGrid, row, 1 ) )
        {
            wxMessageBox( _( "Diff Pairs: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        newDiffPairs.emplace_back( cellTrim( m_diffPairGrid, row, 0 ),
                                    cellTrim( m_diffPairGrid, row, 1 ) );
    }

    // ---- Current rules validation ----
    std::map<wxString, PROJECT_FILE::MB_CURRENT_RULE> newCurrentRules;

    for( int row = 0; row < m_currentGrid->GetNumberRows(); ++row )
    {
        if( !requireNetName( m_currentGrid, row, 0 ) )
        {
            wxMessageBox( _( "Current: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        PROJECT_FILE::MB_CURRENT_RULE rule;

        if( !cellToDouble( m_currentGrid, row, 1, 0.0, &rule.expectedAmps, &err )
            || !cellToDouble( m_currentGrid, row, 2, 0.0, &rule.pinRatingAmps, &err ) )
        {
            wxMessageBox( _( "Current: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        newCurrentRules[cellTrim( m_currentGrid, row, 0 )] = rule;
    }

    // ---- Voltage rules validation ----
    std::map<wxString, PROJECT_FILE::MB_VOLTAGE_RULE> newVoltageRules;

    for( int row = 0; row < m_voltageGrid->GetNumberRows(); ++row )
    {
        if( !requireNetName( m_voltageGrid, row, 0 ) )
        {
            wxMessageBox( _( "Voltage Drop: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        PROJECT_FILE::MB_VOLTAGE_RULE rule;

        if( !cellToDouble( m_voltageGrid, row, 1, 0.0, &rule.expectedAmps, &err )
            || !cellToDouble( m_voltageGrid, row, 2, 0.0, &rule.maxDropMv, &err )
            || !cellToOptionalDouble( m_voltageGrid, row, 3, &rule.traceWidthUm, &err )
            || !cellToOptionalDouble( m_voltageGrid, row, 4, &rule.traceSheetRMOhmsPerSq, &err )
            || !cellToOptionalDouble( m_voltageGrid, row, 5, &rule.contactRPerPinMOhms, &err ) )
        {
            wxMessageBox( _( "Voltage Drop: " ) + err, _( "Invalid Rule" ),
                          wxOK | wxICON_WARNING, this );
            return false;
        }

        newVoltageRules[cellTrim( m_voltageGrid, row, 0 )] = rule;
    }

    // ---- Commit ----
    // Wrap all setter calls in SUSPEND_NOTIFY so each rule type fires
    // exactly one observer event regardless of row count.
    {
        PROJECT_FILE_SUSPEND_NOTIFY notifyGuard( *m_project );

        m_project->ClearMinPowerPins();
        for( const auto& [net, n] : newMinPowerPins )
            m_project->SetMinPowerPin( net, n );

        m_project->ClearMaxLengthNm();
        for( const auto& [net, nm] : newMaxLengthNm )
            m_project->SetMaxLengthNm( net, nm );

        m_project->ClearCrossBoardDiffPairs();
        for( const auto& [a, b] : newDiffPairs )
            m_project->AddCrossBoardDiffPair( a, b );

        m_project->ClearCrossBoardCurrentRules();
        for( const auto& [net, rule] : newCurrentRules )
            m_project->SetCrossBoardCurrentRule( net, rule );

        m_project->ClearCrossBoardVoltageRules();
        for( const auto& [net, rule] : newVoltageRules )
            m_project->SetCrossBoardVoltageRule( net, rule );
    }   // SUSPEND_NOTIFY drains here

    // Persist immediately. SaveToFile stamps the self-write filter so
    // peer windows' watchers don't false-positive on our own write.
    // Determine target dir via SETTINGS_MANAGER's open-projects list
    // (lifetime-safe, mirrors the pattern in dialog_multi_board_setup).
    wxString targetDir;

    if( PROJECT* prj = m_project->GetProject() )
        targetDir = wxFileName( prj->GetProjectFullName() ).GetPath();

    if( targetDir.IsEmpty() )
    {
        SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

        for( const wxString& proPath : sm.GetOpenProjects() )
        {
            PROJECT* prj = sm.GetProject( proPath );

            if( prj && &prj->GetProjectFile() == m_project )
            {
                targetDir = wxFileName( proPath ).GetPath();
                break;
            }
        }
    }

    if( !m_project->SaveToFile( targetDir, /* aForce */ true ) )
    {
        wxMessageBox( _( "Could not save the multi-board project file. Your changes are "
                         "in memory but not on disk." ),
                      _( "Save Failed" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    return true;
}


void DIALOG_MULTI_BOARD_RULES::onAddRow( wxCommandEvent& aEvent )
{
    auto it = m_buttonToGrid.find( aEvent.GetId() );

    if( it == m_buttonToGrid.end() || !it->second )
        return;

    it->second->AppendRows( 1 );

    int newRow = it->second->GetNumberRows() - 1;
    it->second->MakeCellVisible( newRow, 0 );
    it->second->SetGridCursor( newRow, 0 );
}


void DIALOG_MULTI_BOARD_RULES::onRemoveSelected( wxCommandEvent& aEvent )
{
    auto it = m_buttonToGrid.find( aEvent.GetId() );

    if( it == m_buttonToGrid.end() || !it->second )
        return;

    wxGrid* grid = it->second;

    // wxGrid row-selection mode populates wxArrayInt with the selected
    // rows. Delete from highest index downward so earlier indices stay
    // valid mid-erase.
    wxArrayInt selected = grid->GetSelectedRows();

    if( selected.IsEmpty() )
    {
        // Fall back to the cursor row when no full-row selection exists
        // (saves a click for the common single-row case).
        int cursor = grid->GetGridCursorRow();

        if( cursor < 0 )
            return;

        grid->DeleteRows( cursor, 1 );
        return;
    }

    std::sort( selected.begin(), selected.end(),
               []( int a, int b ) { return a > b; } );

    for( int row : selected )
        grid->DeleteRows( row, 1 );
}


void DIALOG_MULTI_BOARD_RULES::onApply( wxCommandEvent& aEvent )
{
    applyChanges();
}


void DIALOG_MULTI_BOARD_RULES::onOK( wxCommandEvent& aEvent )
{
    if( applyChanges() )
        EndModal( wxID_OK );
}


void DIALOG_MULTI_BOARD_RULES::onCancel( wxCommandEvent& aEvent )
{
    EndModal( wxID_CANCEL );
}
