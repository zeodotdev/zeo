/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "dialog_mbs_sync_pcb.h"

#include <sch_edit_frame.h>
#include <project.h>
#include <reporter.h>
#include <widgets/wx_html_report_panel.h>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>


namespace
{

SEVERITY toReporterSeverity( MB_SYNC_PREVIEW_LINE::SEVERITY aSeverity )
{
    switch( aSeverity )
    {
    case MB_SYNC_PREVIEW_LINE::SEVERITY::INFO:    return RPT_SEVERITY_INFO;
    case MB_SYNC_PREVIEW_LINE::SEVERITY::WARNING: return RPT_SEVERITY_WARNING;
    case MB_SYNC_PREVIEW_LINE::SEVERITY::ERR:     return RPT_SEVERITY_ERROR;
    }

    return RPT_SEVERITY_INFO;
}

} // namespace


DIALOG_MBS_SYNC_PCB::DIALOG_MBS_SYNC_PCB( SCH_EDIT_FRAME* aFrame,
                                          const MB_CROSS_BOARD_SYNC_RESULT& aPreview,
                                          ApplyCallback aApply ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Sync Cross-Board Nets to PCBs" ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_preview( aPreview ),
        m_applyFn( std::move( aApply ) ),
        m_applied( false ),
        m_summaryText( nullptr ),
        m_messagePanel( nullptr ),
        m_applyButton( nullptr ),
        m_cancelButton( nullptr )
{
    buildUI();

    // Pre-flight summary at the top + dry-run details in the report panel.
    m_summaryText->SetLabel(
            wxString::Format(
                    _( "Preview: %d sub-board(s) would be modified · "
                       "%d pad assignment(s) · %d missing target(s) · "
                       "%zu naming conflict(s)" ),
                    m_preview.subProjectsTouched,
                    m_preview.endpointsApplied,
                    m_preview.endpointsMissing,
                    m_preview.conflicts.size() ) );

    renderLines( m_preview.previewLines );

    // Apply has nothing to commit if every endpoint is missing or the net
    // list is empty; reflect that on the button.
    bool hasWork = m_preview.endpointsApplied > 0
                   || m_preview.netsRenamed > 0
                   || !m_preview.previewLines.empty();
    m_applyButton->Enable( hasWork );

    SetMinSize( wxSize( 720, 540 ) );
    finishDialogSettings();
}


void DIALOG_MBS_SYNC_PCB::buildUI()
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // ---- Summary header ----
    m_summaryText = new wxStaticText( this, wxID_ANY, wxEmptyString );
    wxFont bold = m_summaryText->GetFont();
    bold.SetWeight( wxFONTWEIGHT_BOLD );
    m_summaryText->SetFont( bold );
    mainSizer->Add( m_summaryText, 0, wxALL | wxEXPAND, 8 );

    // ---- Cautionary note ----
    //
    // Net rename in a sub-board PCB is a whole-file string replace —
    // pads beyond the connector can be affected if their net name
    // happens to match. Surface that here so the user reads it before
    // hitting Apply rather than after.
    wxStaticText* note = new wxStaticText(
            this, wxID_ANY,
            _( "Sync edits sub-board .kicad_pcb files directly. Review the "
               "items below carefully before applying — bulk renames affect "
               "every matching pad in the file." ) );
    note->Wrap( 700 );
    mainSizer->Add( note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    // ---- Report panel ----
    m_messagePanel = new WX_HTML_REPORT_PANEL( this, wxID_ANY );
    m_messagePanel->SetLabel( _( "Changes To Be Applied" ) );
    m_messagePanel->SetFileName( Prj().GetProjectPath() + wxT( "mbs_sync_report.txt" ) );
    m_messagePanel->SetLazyUpdate( true );

    mainSizer->Add( m_messagePanel, 1, wxEXPAND | wxALL, 5 );

    // Standard Apply / Cancel pair. Build via wxStdDialogButtonSizer so
    // platform conventions (button order, default-action wiring) follow
    // DIALOG_SHIM's existing handling. Use wxID_OK + relabel so the
    // dialog's keyboard defaults still hit Apply.
    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    m_applyButton  = new wxButton( this, wxID_OK );
    m_cancelButton = new wxButton( this, wxID_CANCEL );
    sdb->AddButton( m_applyButton );
    sdb->AddButton( m_cancelButton );
    sdb->Realize();

    mainSizer->Add( sdb, 0, wxEXPAND | wxALL, 5 );

    SetSizer( mainSizer );

    m_messagePanel->GetSizer()->SetSizeHints( this );
    m_messagePanel->Layout();

    SetupStandardButtons( { { wxID_OK,     _( "Apply" ) },
                            { wxID_CANCEL, _( "Cancel" ) } } );

    m_applyButton->Bind( wxEVT_BUTTON, &DIALOG_MBS_SYNC_PCB::onApply, this );
}


void DIALOG_MBS_SYNC_PCB::renderLines( const std::vector<MB_SYNC_PREVIEW_LINE>& aLines )
{
    REPORTER& rep = m_messagePanel->Reporter();

    if( aLines.empty() )
    {
        rep.Report( _( "No changes to apply." ), RPT_SEVERITY_INFO );
        m_messagePanel->Flush( false );
        return;
    }

    wxString currentGroup;

    for( const MB_SYNC_PREVIEW_LINE& line : aLines )
    {
        // Emit a section header whenever the sub-project changes. Lines
        // with an empty subProjectDisplayName are project-level (e.g.
        // naming conflicts) and don't carry a group header.
        if( !line.subProjectDisplayName.IsEmpty()
            && line.subProjectDisplayName != currentGroup )
        {
            currentGroup = line.subProjectDisplayName;
            rep.Report( wxString::Format( wxT( "── %s ──" ), currentGroup ),
                        RPT_SEVERITY_ACTION );
        }
        else if( line.subProjectDisplayName.IsEmpty() && !currentGroup.IsEmpty() )
        {
            // Returning to project-level after a board section: reset so
            // the next board group is re-headed.
            currentGroup.clear();
        }

        rep.Report( line.text, toReporterSeverity( line.severity ) );
    }

    m_messagePanel->Flush( false );
}


void DIALOG_MBS_SYNC_PCB::onApply( wxCommandEvent& aEvent )
{
    if( m_applied )
    {
        // After a successful apply, the OK button has been relabeled
        // "Close"; honour that.
        EndModal( wxID_OK );
        return;
    }

    if( !m_applyFn )
    {
        EndModal( wxID_CANCEL );
        return;
    }

    m_messagePanel->Clear();
    m_messagePanel->Reporter().Report( _( "Applying sync…" ), RPT_SEVERITY_ACTION );
    m_messagePanel->Flush( false );

    m_result = m_applyFn();

    // Trailing summary stays visible even if the user filters the
    // report panel down to errors-only.
    renderLines( m_result.previewLines );
    m_messagePanel->Reporter().Report( m_result.summary, RPT_SEVERITY_ACTION );
    m_messagePanel->Flush( false );

    m_summaryText->SetLabel(
            wxString::Format(
                    _( "Applied: %d sub-board(s) modified · "
                       "%d pad assignment(s) · %d missing · "
                       "%d net(s) renamed · %zu naming conflict(s)" ),
                    m_result.subProjectsTouched,
                    m_result.endpointsApplied,
                    m_result.endpointsMissing,
                    m_result.netsRenamed,
                    m_result.conflicts.size() ) );

    m_applied = true;
    m_applyButton->SetLabel( _( "Close" ) );
    m_cancelButton->Hide();
    Layout();
}
