/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_mbs_refresh.h"

#include <sch_edit_frame.h>
#include <sch_draw_panel.h>
#include <project.h>
#include <reporter.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
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
    // Layout mirrors `pcbnew/dialogs/dialog_update_pcb` — single
    // top-level vertical sizer, one checkbox per row in the operations
    // group, WX_HTML_REPORT_PANEL filling the rest of the dialog, and
    // the standard buttons via DIALOG_SHIM::SetupStandardButtons. This
    // keeps the look + feel consistent with the canonical "sync" dialog.
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // ---- Operations to perform ----
    wxStaticBoxSizer* sbCategories = new wxStaticBoxSizer(
            new wxStaticBox( this, wxID_ANY, _( "Include in update" ) ),
            wxVERTICAL );

    auto makeCheckbox = [&]( const wxString& aLabel ) -> wxCheckBox*
    {
        wxCheckBox* cb = new wxCheckBox( sbCategories->GetStaticBox(), wxID_ANY, aLabel );
        cb->SetValue( true );
        cb->Bind( wxEVT_CHECKBOX, &DIALOG_MBS_REFRESH::onCategoryToggled, this );
        sbCategories->Add( cb, 0, wxLEFT | wxRIGHT | wxTOP, 5 );
        return cb;
    };

    m_cbAddBlocks    = makeCheckbox( _( "Add new module blocks" ) );
    m_cbAddPins      = makeCheckbox( _( "Add new pins" ) );
    m_cbRenamePins   = makeCheckbox( _( "Rename pin labels" ) );
    m_cbUpdatePaths  = makeCheckbox( _( "Update sub-project paths" ) );
    m_cbRemoveBlocks = makeCheckbox( _( "Remove obsolete blocks (destructive)" ) );
    m_cbRemovePins   = makeCheckbox( _( "Remove obsolete pins (destructive)" ) );
    m_cbUpgradeUuids = makeCheckbox( _( "Adopt sub-project UUIDs (legacy upgrade)" ) );

    // Trailing margin so the bottom checkbox isn't flush with the box.
    sbCategories->AddSpacer( 5 );

    mainSizer->Add( sbCategories, 0, wxEXPAND | wxALL, 5 );

    // ---- Report panel ----
    m_messagePanel = new WX_HTML_REPORT_PANEL( this, wxID_ANY );
    m_messagePanel->SetLabel( _( "Changes To Be Applied" ) );
    m_messagePanel->SetFileName( Prj().GetProjectPath() + wxT( "mbs_refresh_report.txt" ) );
    m_messagePanel->SetLazyUpdate( true );

    mainSizer->Add( m_messagePanel, 1, wxEXPAND | wxALL, 5 );

    // Standard wxStdDialogButtonSizer with OK + Cancel. Hand-coded
    // dialogs without an .fbp twin must create the buttons explicitly
    // (DIALOG_SHIM::SetupStandardButtons only re-labels existing
    // buttons; it doesn't create them). Add the sizer to the main
    // layout *before* calling SetupStandardButtons so the relabel walk
    // can find them.
    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    m_updateButton = new wxButton( this, wxID_OK );
    m_cancelButton = new wxButton( this, wxID_CANCEL );
    sdb->AddButton( m_updateButton );
    sdb->AddButton( m_cancelButton );
    sdb->Realize();

    mainSizer->Add( sdb, 0, wxEXPAND | wxALL, 5 );

    SetSizer( mainSizer );

    // Bind the report panel's natural size into the dialog's size
    // hints — same trick `dialog_update_pcb` uses so the dialog
    // resizes gracefully across platforms with different default fonts.
    m_messagePanel->GetSizer()->SetSizeHints( this );
    m_messagePanel->Layout();

    // Apply custom labels to the standard buttons (mirrors
    // dialog_update_pcb's "Update PCB" / "Close" pair).
    SetupStandardButtons( { { wxID_OK,     _( "Update MBS" ) },
                            { wxID_CANCEL, _( "Close" ) } } );

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

    // Clear the canvas selection before mutating the screen. The previous
    // RefreshMbsFromSubProjects controller auto-selects newly-added blocks
    // and posts a move action — those blocks remain in the selection
    // VIEW_GROUP after the move ends. If a subsequent refresh deletes any
    // of those blocks (REMOVE_BLOCK / empty-block sweep / placeholder pin
    // teardown that empties the block) the SELECTION VIEW_GROUP is left
    // with dangling pointers and crashes on the next ViewDraw walk.
    if( TOOL_MANAGER* toolMgr = m_frame->GetToolManager() )
        toolMgr->RunAction( ACTIONS::selectionClear );

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
