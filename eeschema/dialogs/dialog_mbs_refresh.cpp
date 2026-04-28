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

#include "dialog_mbs_refresh.h"

#include <sch_edit_frame.h>
#include <sch_draw_panel.h>
#include <project.h>
#include <reporter.h>
#include <view/view.h>
#include <widgets/wx_html_report_panel.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>


DIALOG_MBS_REFRESH::DIALOG_MBS_REFRESH( SCH_EDIT_FRAME* aFrame,
                                        std::vector<MBS_CHANGE>& aChanges ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Refresh Module Blocks" ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_changes( aChanges ),
        m_applied( false ),
        m_cbAddBlocks( nullptr ),
        m_cbRemoveBlocks( nullptr ),
        m_cbAddPins( nullptr ),
        m_cbRemovePins( nullptr ),
        m_cbRenamePins( nullptr ),
        m_cbUpdatePaths( nullptr ),
        m_cbUpgradeUuids( nullptr ),
        m_messagePanel( nullptr ),
        m_updateButton( nullptr ),
        m_cancelButton( nullptr )
{
    buildUI();

    // Default: every category enabled. Caller's diff already pre-sets
    // each change's `checked = true`; the category toggles narrow that
    // set, they don't widen it.
    syncCheckedFlagsFromSettings();
    renderPreview();

    SetMinSize( wxSize( 640, 500 ) );
    finishDialogSettings();
}


void DIALOG_MBS_REFRESH::buildUI()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // ---- Operations to perform ----
    wxStaticBoxSizer* sbCategories = new wxStaticBoxSizer(
            new wxStaticBox( this, wxID_ANY, _( "Operations to perform" ) ),
            wxVERTICAL );

    wxBoxSizer* row1 = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* row2 = new wxBoxSizer( wxHORIZONTAL );

    auto makeCheckbox = [&]( const wxString& aLabel, wxBoxSizer* aRow ) -> wxCheckBox*
    {
        wxCheckBox* cb = new wxCheckBox( sbCategories->GetStaticBox(), wxID_ANY, aLabel );
        cb->SetValue( true );
        cb->Bind( wxEVT_CHECKBOX, &DIALOG_MBS_REFRESH::onCategoryToggled, this );
        aRow->Add( cb, 1, wxALL, 4 );
        return cb;
    };

    m_cbAddBlocks    = makeCheckbox( _( "Add new module blocks" ),                    row1 );
    m_cbAddPins      = makeCheckbox( _( "Add new pins" ),                              row1 );
    m_cbRenamePins   = makeCheckbox( _( "Rename pin labels" ),                         row1 );
    m_cbUpdatePaths  = makeCheckbox( _( "Update sub-project paths" ),                  row1 );

    m_cbRemoveBlocks = makeCheckbox( _( "Remove obsolete blocks (destructive)" ),      row2 );
    m_cbRemovePins   = makeCheckbox( _( "Remove obsolete pins (destructive)" ),        row2 );
    m_cbUpgradeUuids = makeCheckbox( _( "Adopt sub-project UUIDs (legacy upgrade)" ),  row2 );
    row2->AddStretchSpacer();   // keep widths consistent across the two rows

    sbCategories->Add( row1, 0, wxEXPAND | wxALL, 4 );
    sbCategories->Add( row2, 0, wxEXPAND | wxALL, 4 );

    mainSizer->Add( sbCategories, 0, wxEXPAND | wxALL, 8 );

    // ---- Report panel ----
    m_messagePanel = new WX_HTML_REPORT_PANEL( this, wxID_ANY );
    m_messagePanel->SetLabel( _( "Changes" ) );
    m_messagePanel->SetFileName( Prj().GetProjectPath() + wxT( "mbs_refresh_report.txt" ) );
    m_messagePanel->SetLazyUpdate( true );

    mainSizer->Add( m_messagePanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    // ---- Buttons ----
    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    m_updateButton = new wxButton( this, wxID_OK, _( "Update MBS" ) );
    m_cancelButton = new wxButton( this, wxID_CANCEL, _( "Close" ) );
    sdb->AddButton( m_updateButton );
    sdb->AddButton( m_cancelButton );
    sdb->Realize();

    mainSizer->Add( sdb, 0, wxEXPAND | wxALL, 8 );

    SetSizer( mainSizer );
    Layout();

    m_updateButton->Bind( wxEVT_BUTTON, &DIALOG_MBS_REFRESH::onUpdate, this );
}


void DIALOG_MBS_REFRESH::syncCheckedFlagsFromSettings()
{
    auto categoryEnabled = [&]( MBS_CHANGE::KIND aKind ) -> bool
    {
        switch( aKind )
        {
        case MBS_CHANGE::KIND::ADD_BLOCK:    return m_cbAddBlocks->GetValue();
        case MBS_CHANGE::KIND::REMOVE_BLOCK: return m_cbRemoveBlocks->GetValue();
        case MBS_CHANGE::KIND::ADD_PIN:      return m_cbAddPins->GetValue();
        case MBS_CHANGE::KIND::REMOVE_PIN:   return m_cbRemovePins->GetValue();
        case MBS_CHANGE::KIND::RENAME_PIN:   return m_cbRenamePins->GetValue();
        case MBS_CHANGE::KIND::PATH_DRIFT:   return m_cbUpdatePaths->GetValue();
        case MBS_CHANGE::KIND::UPGRADE_UUID: return m_cbUpgradeUuids->GetValue();
        }

        return true;
    };

    for( MBS_CHANGE& ch : m_changes )
        ch.checked = categoryEnabled( ch.kind );
}


void DIALOG_MBS_REFRESH::renderPreview()
{
    m_messagePanel->Clear();

    REPORTER& rep = m_messagePanel->Reporter();

    if( m_changes.empty() )
    {
        rep.Report( _( "The multi-board schematic is already in sync." ),
                    RPT_SEVERITY_INFO );
        m_messagePanel->Flush( false );
        m_updateButton->Enable( false );
        return;
    }

    int enabledCount = 0;

    for( const MBS_CHANGE& ch : m_changes )
    {
        if( !ch.checked )
            continue;

        ++enabledCount;

        // Destructive ops (REMOVE_*) get warning severity in the
        // preview so the user has visual confirmation of how many
        // blocks/pins are about to disappear.
        SEVERITY sev = ( ch.kind == MBS_CHANGE::KIND::REMOVE_BLOCK
                         || ch.kind == MBS_CHANGE::KIND::REMOVE_PIN )
                               ? RPT_SEVERITY_WARNING
                               : RPT_SEVERITY_INFO;

        rep.Report( ch.Describe(), sev );
    }

    if( enabledCount == 0 )
    {
        rep.Report( _( "No changes are enabled. Toggle a category above to "
                       "include changes in the update." ),
                    RPT_SEVERITY_INFO );
    }

    m_messagePanel->Flush( false );
    m_updateButton->Enable( enabledCount > 0 && !m_applied );
}


void DIALOG_MBS_REFRESH::onCategoryToggled( wxCommandEvent& aEvent )
{
    syncCheckedFlagsFromSettings();
    renderPreview();
}


void DIALOG_MBS_REFRESH::onUpdate( wxCommandEvent& aEvent )
{
    if( m_applied )
    {
        // Already applied; the OK button now means "close".
        EndModal( wxID_OK );
        return;
    }

    syncCheckedFlagsFromSettings();
    m_messagePanel->Clear();

    SCH_SCREEN*  screen = m_frame->Schematic().RootScreen();
    KIGFX::VIEW* view   = m_frame->GetCanvas() ? m_frame->GetCanvas()->GetView() : nullptr;

    if( !screen )
    {
        m_messagePanel->Reporter().Report(
                _( "Refresh failed: no MBS root screen available." ),
                RPT_SEVERITY_ERROR );
        m_messagePanel->Flush( false );
        return;
    }

    m_result = ApplyMbsRefreshChanges( *screen, m_changes, view,
                                        &m_messagePanel->Reporter() );

    // Trailing summary line: stays visible even if the user filters
    // the report panel down to errors-only.
    m_messagePanel->Reporter().Report( m_result.summary, RPT_SEVERITY_ACTION );
    m_messagePanel->Flush( false );

    m_frame->OnModify();
    m_frame->GetCanvas()->Refresh( true );

    m_applied = true;
    m_updateButton->SetLabel( _( "Close" ) );
    m_updateButton->Enable( true );
    m_cancelButton->Hide();
    Layout();

    // Don't auto-close: leave the dialog up so the user can read the
    // log. Either button now dismisses with wxID_OK so the caller can
    // detect "apply happened" and hand off newly-added blocks to the
    // move tool. (m_result.newlyAddedBlocks captured above.)
}
