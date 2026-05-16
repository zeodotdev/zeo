/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

#include <pcb_edit_frame.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_tool_base.h>
#include <tools/zone_filler_tool.h>
#include <tools/pcb_selection_tool.h>
#include <tools/drc_tool.h>
#include <kiface_base.h>
#include <dialog_drc.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <pgm_base.h>
#include <progress_reporter.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <reporter.h>
#include <settings/settings_manager.h>
#include <drc/drc_engine.h>
#include <drc/drc_engine_cross_board.h>
#include <drc/drc_item.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <netlist_reader/pcb_netlist.h>
#include <macros.h>
#include <dialog_exchange_footprints.h>
#include <drc/rule_editor/dialog_drc_rule_editor.h>


DRC_TOOL::DRC_TOOL() :
        PCB_TOOL_BASE( "pcbnew.DRCTool" ),
        m_editFrame( nullptr ),
        m_pcb( nullptr ),
        m_drcDialog( nullptr ),
        m_designRuleEditorDlg( nullptr ),
        m_drcRunning( false )
{
}


DRC_TOOL::~DRC_TOOL()
{
}


void DRC_TOOL::Reset( RESET_REASON aReason )
{
    m_editFrame = getEditFrame<PCB_EDIT_FRAME>();

    if( m_pcb != m_editFrame->GetBoard() )
    {
        if( m_drcDialog )
            DestroyDRCDialog();

        m_pcb = m_editFrame->GetBoard();
        m_drcEngine = m_pcb->GetDesignSettings().m_DRCEngine;
    }
}


void DRC_TOOL::ShowDRCDialog( wxWindow* aParent )
{
    bool show_dlg_modal = true;

    // the dialog needs a parent frame. if it is not specified, this is the PCB editor frame
    // specified in DRC_TOOL class.
    if( !aParent )
    {
        // if any parent is specified, the dialog is modal.
        // if this is the default PCB editor frame, it is not modal
        show_dlg_modal = false;
        aParent = m_editFrame;
    }

    Activate();
    m_toolMgr->RunAction( ACTIONS::selectionClear );

    if( !m_drcDialog )
    {
        m_drcDialog = new DIALOG_DRC( m_editFrame, aParent );
        updatePointers( false );

        if( show_dlg_modal )
            m_drcDialog->ShowModal();
        else
            m_drcDialog->Show( true );
    }
    else // The dialog is just not visible (because the user has double clicked on an error item)
    {
        updatePointers( false );
        m_drcDialog->Show( true );
    }
}


int DRC_TOOL::ShowDRCDialog( const TOOL_EVENT& aEvent )
{
    ShowDRCDialog( nullptr );
    return 0;
}


bool DRC_TOOL::IsDRCDialogShown()
{
    if( m_drcDialog )
        return m_drcDialog->IsShownOnScreen();

    return false;
}


void DRC_TOOL::DestroyDRCDialog()
{
    if( m_drcDialog )
    {
        m_drcDialog->Destroy();
        m_drcDialog = nullptr;
    }
}


void DRC_TOOL::RunTests( PROGRESS_REPORTER* aProgressReporter, bool aRefillZones,
                         bool aReportAllTrackErrors, bool aTestFootprints )
{
    // One at a time, please.
    // Note that the main GUI entry points to get here are blocked, so this is really an
    // insurance policy and as such we make no attempts to queue up the DRC run or anything.
    if( m_drcRunning )
        return;

    ZONE_FILLER_TOOL* zoneFiller = m_toolMgr->GetTool<ZONE_FILLER_TOOL>();
    BOARD_COMMIT      commit( m_editFrame );
    NETLIST           netlist;
    bool              netlistFetched = false;

    if( m_drcDialog )
        wxWindowDisabler  disabler( /* disable everything except: */ m_drcDialog );

    m_drcRunning = true;

    if( aRefillZones && aProgressReporter )
    {
        aProgressReporter->AdvancePhase( _( "Refilling all zones..." ) );

        zoneFiller->FillAllZones( m_drcDialog, aProgressReporter );
    }

    if( m_editFrame->GetCanvas() && m_editFrame->GetCanvas()->GetDrawingSheet() )
        m_drcEngine->SetDrawingSheet( m_editFrame->GetCanvas()->GetDrawingSheet() );

    if( aTestFootprints && !Kiface().IsSingle() )
    {
        if( m_editFrame->FetchNetlistFromSchematic( netlist,
                                                    _( "Schematic parity tests require a "
                                                       "fully annotated schematic." ) ) )
        {
            netlistFetched = true;
        }

        if( m_drcDialog )
            m_drcDialog->Raise();

        m_drcEngine->SetSchematicNetlist( &netlist );
    }

    m_drcEngine->SetProgressReporter( aProgressReporter );

    m_drcEngine->SetViolationHandler(
            [&]( const std::shared_ptr<DRC_ITEM>& aItem, const VECTOR2I& aPos, int aLayer,
                 const std::function<void( PCB_MARKER* )>& aPathGenerator )
            {
                PCB_MARKER* marker = new PCB_MARKER( aItem, aPos, aLayer );
                aPathGenerator( marker );
                commit.Add( marker );
            } );

    m_drcEngine->RunTests( m_editFrame->GetUserUnits(), aReportAllTrackErrors, aTestFootprints,
                           &commit );

    m_drcEngine->SetProgressReporter( nullptr );
    m_drcEngine->ClearViolationHandler();

    if( m_drcDialog )
    {
        m_drcDialog->SetDrcRun();

        if( aTestFootprints && netlistFetched )
            m_drcDialog->SetFootprintTestsRun();
    }

    commit.Push( _( "DRC" ), SKIP_UNDO | SKIP_SET_DIRTY );

    m_drcRunning = false;

    m_editFrame->ShowSolderMask();

    // update the m_drcDialog listboxes
    updatePointers( aProgressReporter->IsCancelled() );
}


void DRC_TOOL::updatePointers( bool aDRCWasCancelled )
{
    // update my pointers, m_editFrame is the only unchangeable one
    m_pcb = m_editFrame->GetBoard();

    m_editFrame->ResolveDRCExclusions( aDRCWasCancelled );

    if( m_drcDialog )
        m_drcDialog->UpdateData();
}


int DRC_TOOL::PrevMarker( const TOOL_EVENT& aEvent )
{
    if( m_drcDialog )
    {
        m_drcDialog->Show( true );
        m_drcDialog->Raise();
        m_drcDialog->PrevMarker();
    }
    else
    {
        ShowDRCDialog( nullptr );
    }

    return 0;
}


int DRC_TOOL::NextMarker( const TOOL_EVENT& aEvent )
{
    if( m_drcDialog )
    {
        m_drcDialog->Show( true );
        m_drcDialog->Raise();
        m_drcDialog->NextMarker();
    }
    else
    {
        ShowDRCDialog( nullptr );
    }

    return 0;
}


int DRC_TOOL::CrossProbe( const TOOL_EVENT& aEvent )
{
    if( m_drcDialog && m_drcDialog->IsShownOnScreen() )
    {
        PCB_SELECTION_TOOL* selectionTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
        PCB_SELECTION&      selection = selectionTool->GetSelection();

        if( selection.GetSize() == 1 && selection.Front()->Type() == PCB_MARKER_T )
            m_drcDialog->SelectMarker( static_cast<PCB_MARKER*>( selection.Front() ) );
    }

    return 0;
}


void DRC_TOOL::CrossProbe( const PCB_MARKER* aMarker )
{
    if( !IsDRCDialogShown() )
        ShowDRCDialog( nullptr );

    m_drcDialog->SelectMarker( aMarker );
}


int DRC_TOOL::ExcludeMarker( const TOOL_EVENT& aEvent )
{
    if( m_drcDialog )
        m_drcDialog->ExcludeMarker();

    return 0;
}


wxString DRC_TOOL::FixDRCErrorMenuText( const std::shared_ptr<RC_ITEM>& aDRCItem )
{
    if( aDRCItem->GetErrorCode() == DRCE_LIB_FOOTPRINT_ISSUES )
    {
        return frame()->GetRunMenuCommandDescription( PCB_ACTIONS::showFootprintLibTable );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_LIB_FOOTPRINT_MISMATCH )
    {
        return frame()->GetRunMenuCommandDescription( PCB_ACTIONS::updateFootprint );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_FOOTPRINT_FILTERS )
    {
        return frame()->GetRunMenuCommandDescription( PCB_ACTIONS::changeFootprint );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_SCHEMATIC_PARITY
                || aDRCItem->GetErrorCode() == DRCE_SCHEMATIC_FIELDS_PARITY
                || aDRCItem->GetErrorCode() == DRCE_MISSING_FOOTPRINT
                || aDRCItem->GetErrorCode() == DRCE_DUPLICATE_FOOTPRINT
                || aDRCItem->GetErrorCode() == DRCE_EXTRA_FOOTPRINT )
    {
        return frame()->GetRunMenuCommandDescription( PCB_ACTIONS::updatePcbFromSchematic );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_FOOTPRINT_TYPE_MISMATCH
                || aDRCItem->GetErrorCode() == DRCE_FOOTPRINT )
    {
        return _( "Edit Footprint Properties..." );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_PADSTACK
                || aDRCItem->GetErrorCode() == DRCE_PADSTACK_INVALID )
    {
        return _( "Edit Pad Properties..." );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_TEXT_HEIGHT
                || aDRCItem->GetErrorCode() == DRCE_TEXT_THICKNESS
                || aDRCItem->GetErrorCode() == DRCE_MIRRORED_TEXT_ON_FRONT_LAYER
                || aDRCItem->GetErrorCode() == DRCE_NONMIRRORED_TEXT_ON_BACK_LAYER )
    {
        BOARD_ITEM* item = m_pcb->ResolveItem( aDRCItem->GetMainItemID() );

        if( item && BaseType( item->Type() ) == PCB_DIMENSION_T )
            return _( "Edit Dimension Properties..." );
        else if( item && item->Type() == PCB_FIELD_T )
            return _( "Edit Field Properties..." );
        else
            return _( "Edit Text Properties..." );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_DANGLING_TRACK
                || aDRCItem->GetErrorCode() == DRCE_DANGLING_VIA )
    {
        return frame()->GetRunMenuCommandDescription( PCB_ACTIONS::cleanupTracksAndVias );
    }

    return wxEmptyString;
}


void DRC_TOOL::FixDRCError( const std::shared_ptr<RC_ITEM>& aDRCItem )
{
    if( aDRCItem->GetErrorCode() == DRCE_LIB_FOOTPRINT_ISSUES )
    {
        m_toolMgr->RunAction( PCB_ACTIONS::showFootprintLibTable );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_LIB_FOOTPRINT_MISMATCH
            || aDRCItem->GetErrorCode() == DRCE_FOOTPRINT_FILTERS )
    {
        bool        updateMode = aDRCItem->GetErrorCode() == DRCE_LIB_FOOTPRINT_MISMATCH;
        BOARD_ITEM* item = m_pcb->ResolveItem( aDRCItem->GetMainItemID() );

        if( FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( item ) )
        {
            DIALOG_EXCHANGE_FOOTPRINTS dialog( m_editFrame, footprint, updateMode, true );
            dialog.ShowQuasiModal();
        }
    }
    else if( aDRCItem->GetErrorCode() == DRCE_SCHEMATIC_PARITY
                || aDRCItem->GetErrorCode() == DRCE_SCHEMATIC_FIELDS_PARITY
                || aDRCItem->GetErrorCode() == DRCE_MISSING_FOOTPRINT
                || aDRCItem->GetErrorCode() == DRCE_DUPLICATE_FOOTPRINT
                || aDRCItem->GetErrorCode() == DRCE_EXTRA_FOOTPRINT )
    {
        m_toolMgr->RunAction( PCB_ACTIONS::updatePcbFromSchematic );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_FOOTPRINT_TYPE_MISMATCH
                || aDRCItem->GetErrorCode() == DRCE_FOOTPRINT
                || aDRCItem->GetErrorCode() == DRCE_PADSTACK
                || aDRCItem->GetErrorCode() == DRCE_PADSTACK_INVALID
                || aDRCItem->GetErrorCode() == DRCE_TEXT_HEIGHT
                || aDRCItem->GetErrorCode() == DRCE_TEXT_THICKNESS
                || aDRCItem->GetErrorCode() == DRCE_MIRRORED_TEXT_ON_FRONT_LAYER
                || aDRCItem->GetErrorCode() == DRCE_NONMIRRORED_TEXT_ON_BACK_LAYER)

    {
        BOARD_ITEM* item = m_pcb->ResolveItem( aDRCItem->GetMainItemID() );

        m_editFrame->OnEditItemRequest( item );
    }
    else if( aDRCItem->GetErrorCode() == DRCE_DANGLING_TRACK
                || aDRCItem->GetErrorCode() == DRCE_DANGLING_VIA )
    {
        m_toolMgr->RunAction( PCB_ACTIONS::cleanupTracksAndVias );
    }
}


void DRC_TOOL::ShowDesignRuleEditorDialog( wxWindow* aParent )
{
    bool show_dlg_modal = true;

    // the dialog needs a parent frame. if it is not specified, this is the PCB editor frame
    // specified in DRC_TOOL class.
    if( !aParent )
    {
        // if any parent is specified, the dialog is modal.
        // if this is the default PCB editor frame, it is not modal
        show_dlg_modal = false;
        aParent = m_editFrame;
    }

    Activate();
    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear );

    if( !m_designRuleEditorDlg )
    {
        m_designRuleEditorDlg = new DIALOG_DRC_RULE_EDITOR( m_editFrame, aParent );
        updatePointers( false );

        if( show_dlg_modal )
            m_designRuleEditorDlg->ShowModal();
        else
            m_designRuleEditorDlg->Show( true );
    }
    else // The dialog is just not visible (because the user has double clicked on an error item)
    {
        updatePointers( false );
        m_designRuleEditorDlg->Show( true );
    }
}


int DRC_TOOL::ShowDesignRuleEditorDialog( const TOOL_EVENT& aEvent )
{
    ShowDesignRuleEditorDialog( nullptr );
    return 0;
}


void DRC_TOOL::DestroyDesignRuleEditorDialog()
{
    if( m_designRuleEditorDlg )
    {
        m_designRuleEditorDlg->Destroy();
        m_designRuleEditorDlg = nullptr;
    }
}


int DRC_TOOL::RunCrossBoardValidation( const TOOL_EVENT& aEvent )
{
    BOARD* board = m_editFrame ? m_editFrame->GetBoard() : nullptr;

    if( !board )
        return 0;

    PROJECT* prj = board->GetProject();

    if( !prj )
    {
        wxMessageBox( _( "No project loaded." ),
                      _( "Cross-Board Validation" ), wxOK | wxICON_INFORMATION,
                      m_editFrame );
        return 0;
    }

    // Resolve the enclosing multi-board container. The engine takes a
    // PROJECT* (the container's PROJECT). When the active board is a
    // sub-project, that's not the same as `prj` — we need the container.
    // Look it up via the existing helper, then find / load the container's
    // PROJECT through SETTINGS_MANAGER (must already be loaded for any of
    // the multi_board.* fields to be addressable through observers).
    wxFileName containerPro =
            MultiBoardResolveContainerForSubProject( wxFileName( prj->GetProjectFullName() ) );

    if( !containerPro.IsOk() || !containerPro.FileExists() )
    {
        // Active project might itself BE the container.
        if( prj->GetProjectFile().IsMultiBoardContainer() )
        {
            containerPro = wxFileName( prj->GetProjectFullName() );
        }
        else
        {
            wxMessageBox( _( "This board is not part of a multi-board container project." ),
                          _( "Not a Multi-Board Sub-Project" ),
                          wxOK | wxICON_INFORMATION, m_editFrame );
            return 0;
        }
    }

    // Find the container PROJECT in SETTINGS_MANAGER. If it's not loaded,
    // engine ops would do disk I/O via the path-based fallback, but the
    // engine API takes a PROJECT*. For this v1 we require the container
    // to be loaded — typically true when the user is in a sub-project's
    // PCB while MBSCH is also open.
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();
    PROJECT*          containerProject = nullptr;

    for( const wxString& proPath : sm.GetOpenProjects() )
    {
        if( wxFileName( proPath ).GetFullPath() == containerPro.GetFullPath() )
        {
            containerProject = sm.GetProject( proPath );
            break;
        }
    }

    if( !containerProject )
    {
        wxMessageBox(
                wxString::Format(
                        _( "The multi-board container project '%s' is not currently loaded.\n\n"
                           "Open it (or its MBSCH editor) before running cross-board "
                           "validation, so the engine can resolve all sibling sub-projects." ),
                        containerPro.GetFullPath() ),
                _( "Container Not Loaded" ), wxOK | wxICON_INFORMATION, m_editFrame );
        return 0;
    }

    // Stream progress via wxLogMessage so the user has feedback during
    // the (potentially slow) sibling-board loads. Crude but functional;
    // a streaming-progress dialog is a v2 polish.
    WX_STRING_REPORTER reporter;
    DRC_ENGINE_CROSS_BOARD engine( containerProject );
    engine.SetReporter( &reporter );

    // Pre-seed the engine cache with the active in-memory BOARD if it
    // belongs to one of the container's sub-projects. Without this the
    // engine re-reads the .kicad_pcb from disk and silently misses any
    // unsaved edits in the active window — common during interactive
    // testing where the user edits a pad net then immediately runs DRC.
    {
        const PROJECT_FILE& cf = containerProject->GetProjectFile();
        const wxString activeProPath = wxFileName( prj->GetProjectFullName() ).GetFullPath();

        for( const SUB_PROJECT_INFO& sp : cf.GetSubProjects() )
        {
            if( cf.ResolveSubProjectPath( sp ).GetFullPath() == activeProPath )
            {
                engine.RegisterInMemoryBoard( sp.uuid, board );
                break;
            }
        }
    }

    engine.RunAllChecks();

    const auto& violations = engine.GetViolations();
    int errors    = engine.GetErrorCount();
    int warnings  = engine.GetWarningCount();
    int infos     = static_cast<int>( violations.size() ) - errors - warnings;

    // Summary in a wxMessageBox; full per-violation list logged via
    // wxLogMessage (visible in the active wxLog target — terminal stderr
    // when launched with WXTRACE, or wxLogWindow if registered).
    wxString summary = wxString::Format(
            _( "Cross-board validation complete.\n\n"
               "Errors:   %d\nWarnings: %d\nInfo:     %d\n\n%s" ),
            errors, warnings, infos,
            violations.empty() ? _( "No issues found." )
                                : _( "See log for per-violation details." ) );

    for( const auto& v : violations )
    {
        wxLogMessage( wxT( "[%s] %s — %s%s%s" ),
                      v.GetSeverityString(),
                      v.GetTypeString(),
                      v.message,
                      v.board1Name.IsEmpty() ? wxString( wxEmptyString )
                                              : wxString::Format( wxT( "  (board: %s" ),
                                                                  v.board1Name ),
                      v.board1Name.IsEmpty() ? wxString( wxEmptyString )
                                              : ( v.board2Name.IsEmpty()
                                                          ? wxString( wxT( ")" ) )
                                                          : wxString::Format(
                                                                  wxT( " ↔ %s)" ),
                                                                  v.board2Name ) ) );
    }

    wxMessageBox( summary, _( "Multi-Board Validation Results" ),
                  wxOK | ( errors > 0 ? wxICON_ERROR
                                       : warnings > 0 ? wxICON_WARNING
                                                       : wxICON_INFORMATION ),
                  m_editFrame );

    return 0;
}


void DRC_TOOL::setTransitions()
{
    Go( &DRC_TOOL::ShowDRCDialog,              PCB_ACTIONS::runDRC.MakeEvent() );
    Go( &DRC_TOOL::RunCrossBoardValidation,    PCB_ACTIONS::runCrossBoardValidation.MakeEvent() );
    Go( &DRC_TOOL::PrevMarker,                 ACTIONS::prevMarker.MakeEvent() );
    Go( &DRC_TOOL::NextMarker,                 ACTIONS::nextMarker.MakeEvent() );
    Go( &DRC_TOOL::ExcludeMarker,              ACTIONS::excludeMarker.MakeEvent() );
    Go( &DRC_TOOL::CrossProbe,                 EVENTS::PointSelectedEvent );
    Go( &DRC_TOOL::CrossProbe,                 EVENTS::SelectedEvent );
}
