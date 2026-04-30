/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "api_handler_mbs_sch.h"

#include <wx/filename.h>
#include <wx/log.h>

#include <api/api_utils.h>
#include <kiway.h>
#include <kiway_player.h>
#include <frame_type.h>
#include <project/project_file.h>
#include <project.h>
#include <project/cross_board_pcb_sync.h>
#include <schematic.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <view/view.h>
#include <sch_draw_panel.h>
#include <reporter.h>

#include "../sch_module_block.h"
#include "../sch_module_pin.h"
#include "../multi_board_mbs_refresh.h"

#include <api/common/types/base_types.pb.h>
#include <api/schematic/schematic_commands.pb.h>
#include <api/schematic/schematic_types.pb.h>

using namespace kiapi::common::commands;
using kiapi::common::types::DocumentType;


/// Map a C++ ELECTRICAL_PINTYPE to the proto ElectricalPinType enum.
/// Mirrors the conversion the regular pin handlers in api_handler_sch.cpp use.
static kiapi::common::types::ElectricalPinType packPinType( ELECTRICAL_PINTYPE aType )
{
    switch( aType )
    {
    case ELECTRICAL_PINTYPE::PT_INPUT:          return kiapi::common::types::EPT_INPUT;
    case ELECTRICAL_PINTYPE::PT_OUTPUT:         return kiapi::common::types::EPT_OUTPUT;
    case ELECTRICAL_PINTYPE::PT_BIDI:           return kiapi::common::types::EPT_BIDIRECTIONAL;
    case ELECTRICAL_PINTYPE::PT_TRISTATE:       return kiapi::common::types::EPT_TRISTATE;
    case ELECTRICAL_PINTYPE::PT_PASSIVE:        return kiapi::common::types::EPT_PASSIVE;
    case ELECTRICAL_PINTYPE::PT_NIC:            return kiapi::common::types::EPT_FREE;
    case ELECTRICAL_PINTYPE::PT_UNSPECIFIED:    return kiapi::common::types::EPT_UNSPECIFIED;
    case ELECTRICAL_PINTYPE::PT_POWER_IN:       return kiapi::common::types::EPT_POWER_INPUT;
    case ELECTRICAL_PINTYPE::PT_POWER_OUT:      return kiapi::common::types::EPT_POWER_OUTPUT;
    case ELECTRICAL_PINTYPE::PT_OPENCOLLECTOR:  return kiapi::common::types::EPT_OPEN_COLLECTOR;
    case ELECTRICAL_PINTYPE::PT_OPENEMITTER:    return kiapi::common::types::EPT_OPEN_EMITTER;
    case ELECTRICAL_PINTYPE::PT_NC:             return kiapi::common::types::EPT_NO_CONNECT;
    default:                                    return kiapi::common::types::EPT_UNKNOWN;
    }
}


static kiapi::schematic::types::SheetPinSide packSide( SHEET_SIDE aSide )
{
    switch( aSide )
    {
    case SHEET_SIDE::LEFT:   return kiapi::schematic::types::SPS_LEFT;
    case SHEET_SIDE::RIGHT:  return kiapi::schematic::types::SPS_RIGHT;
    case SHEET_SIDE::TOP:    return kiapi::schematic::types::SPS_TOP;
    case SHEET_SIDE::BOTTOM: return kiapi::schematic::types::SPS_BOTTOM;
    default:                 return kiapi::schematic::types::SPS_UNKNOWN;
    }
}


API_HANDLER_MBS_SCH::API_HANDLER_MBS_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_SCH( aFrame )
{
    using namespace kiapi::schematic::commands;

    // Override GetOpenDocuments — base class registered its own handler keyed
    // on the same proto type; re-registering replaces the entry in the map so
    // our MBS-aware version wins.
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_MBS_SCH::handleGetOpenDocuments );

    // MBS-specific read commands.
    registerHandler<GetModuleBlocks, GetModuleBlocksResponse>(
            &API_HANDLER_MBS_SCH::handleGetModuleBlocks );
    registerHandler<GetCrossBoardNets, GetCrossBoardNetsResponse>(
            &API_HANDLER_MBS_SCH::handleGetCrossBoardNets );
    registerHandler<GetMultiBoardContainerInfo, GetMultiBoardContainerInfoResponse>(
            &API_HANDLER_MBS_SCH::handleGetMultiBoardContainerInfo );

    // MBS-specific edit/workflow commands.
    // Qualify the proto class explicitly — `RefreshMbsFromSubProjects` is
    // also the name of the headless one-shot helper from multi_board_mbs_refresh.h.
    registerHandler<kiapi::schematic::commands::RefreshMbsFromSubProjects,
                    kiapi::schematic::commands::RefreshMbsFromSubProjectsResponse>(
            &API_HANDLER_MBS_SCH::handleRefreshMbsFromSubProjects );
    registerHandler<SyncCrossBoardNetsToPcb, SyncCrossBoardNetsToPcbResponse>(
            &API_HANDLER_MBS_SCH::handleSyncCrossBoardNetsToPcb );
}


/**
 * Same project-path resolution as the SCH handler: derive absolute pro
 * path from the frame's GetCurrentFileName() because PROJECT::GetProjectPath()
 * returns empty for projects loaded via the multi-board path.
 */
static wxString mbsFrameAbsProjectPath( SCH_EDIT_FRAME* aFrame )
{
    if( !aFrame )
        return wxEmptyString;

    wxString filePath = aFrame->GetCurrentFileName();
    wxString projName;

    try
    {
        projName = aFrame->Prj().GetProjectName();
    }
    catch( ... )
    {
    }

    if( filePath.IsEmpty() || projName.IsEmpty() )
        return wxEmptyString;

    wxFileName fn( filePath );
    fn.SetFullName( projName + wxT( ".kicad_pro" ) );
    return fn.GetFullPath();
}


bool API_HANDLER_MBS_SCH::validateDocumentInternal(
        const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_MBS_SCHEMATIC )
        return false;

    // Filter by project.path when set so the right MBSCH frame's handler
    // claims the request (deterministic with multiple containers — rare
    // but supported by the M4 peer-player infrastructure).
    const std::string& reqProjPath = aDocument.project().path();

    if( !reqProjPath.empty() )
    {
        wxString myPath = mbsFrameAbsProjectPath( m_frame );

#if defined( __WXMSW__ ) || defined( __WXMAC__ )
        if( !myPath.IsSameAs( wxString::FromUTF8( reqProjPath ), false ) )
            return false;
#else
        if( myPath != wxString::FromUTF8( reqProjPath ) )
            return false;
#endif
    }

    return true;
}


HANDLER_RESULT<GetOpenDocumentsResponse>
API_HANDLER_MBS_SCH::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_MBS_SCHEMATIC )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;

    // Enumerate every visible FRAME_MBSCH (M4 peer windows could in theory
    // host multiple containers in one process). Same shape as SCH handler.
    std::vector<KIWAY_PLAYER*> mbsPeers = m_frame->Kiway().GetAllPlayerFrames( FRAME_MBSCH );

    for( KIWAY_PLAYER* player : mbsPeers )
    {
        if( !player || !player->IsShown() )
            continue;

        SCH_EDIT_FRAME* peerFrame = dynamic_cast<SCH_EDIT_FRAME*>( player );

        if( !peerFrame )
            continue;

        kiapi::common::types::DocumentSpecifier doc;
        doc.set_type( DocumentType::DOCTYPE_MBS_SCHEMATIC );

        kiapi::common::types::SheetPath* sheetPath = doc.mutable_sheet_path();
        SCH_SHEET_PATH currentPath = peerFrame->GetCurrentSheet();
        sheetPath->set_path_human_readable(
                currentPath.PathHumanReadable().ToStdString() );

        for( size_t i = 0; i < currentPath.size(); ++i )
            sheetPath->add_path()->set_value( currentPath.at( i )->m_Uuid.AsStdString() );

        // Stable absolute project path so kipy can filter on it.
        wxString absProjectPath = mbsFrameAbsProjectPath( peerFrame );
        kiapi::common::types::ProjectSpecifier* projSpec = doc.mutable_project();

        try
        {
            projSpec->set_name( peerFrame->Prj().GetProjectName().ToStdString() );
        }
        catch( ... )
        {
        }

        if( !absProjectPath.IsEmpty() )
            projSpec->set_path( absProjectPath.ToStdString() );

        *response.add_documents() = std::move( doc );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetModuleBlocksResponse>
API_HANDLER_MBS_SCH::handleGetModuleBlocks(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetModuleBlocks>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetModuleBlocksResponse response;

    SCH_SCREEN* screen = m_frame->GetCurrentSheet().LastScreen();

    if( !screen )
        return response;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* block = static_cast<SCH_MODULE_BLOCK*>( item );

        kiapi::schematic::types::ModuleBlock* msg = response.add_blocks();
        msg->mutable_id()->set_value( block->m_Uuid.AsStdString() );
        msg->set_mbs_reference( block->GetMbsReference().ToStdString() );
        msg->set_component_ref( block->GetComponentRef().ToStdString() );
        msg->set_sub_project_uuid( block->GetSubProjectUuid().AsStdString() );
        msg->set_sub_project_path( block->GetSubProjectPath().ToStdString() );
        msg->set_display_name( block->GetDisplayName().ToStdString() );

        kiapi::common::PackVector2Sch( *msg->mutable_position(), block->GetPosition() );
        kiapi::common::PackVector2Sch( *msg->mutable_size(), block->GetSize() );

        for( SCH_MODULE_PIN* pin : block->GetPins() )
        {
            if( !pin )
                continue;

            kiapi::schematic::types::ModuleBlockPin* pinMsg = msg->add_pins();
            pinMsg->mutable_id()->set_value( pin->GetPinUuid().AsStdString() );
            pinMsg->set_component_ref( pin->GetComponentRef().ToStdString() );
            pinMsg->set_pin_number( pin->GetPinNumber().ToStdString() );
            pinMsg->set_text( pin->GetText().ToStdString() );
            kiapi::common::PackVector2Sch( *pinMsg->mutable_position(),
                                           pin->GetPosition() );
            pinMsg->set_side( packSide( pin->GetSide() ) );
            pinMsg->set_electrical_type( packPinType( pin->GetType() ) );
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetCrossBoardNetsResponse>
API_HANDLER_MBS_SCH::handleGetCrossBoardNets(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetCrossBoardNets>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetCrossBoardNetsResponse response;

    PROJECT_FILE& projectFile = m_frame->Prj().GetProjectFile();

    if( !projectFile.IsMultiBoardContainer() )
        return response;

    // onSchematicSaved() extracts cross-board nets into a freshly-loaded
    // PROJECT_FILE instance and saves it to disk; the MBS frame's own
    // in-memory PROJECT_FILE never sees those updates. Reload from disk
    // so callers always get the latest extraction (matches the same
    // pattern in handleSyncCrossBoardNetsToPcb below).
    //
    // Pass the project directory explicitly: m_filename on a live
    // PROJECT_FILE is just the basename, so an empty aDirectory would
    // resolve relative to CWD. Resolve via the back-pointer rather
    // than m_frame->Prj() to dodge the multi-project ambiguity.
    if( projectFile.GetProject() )
    {
        projectFile.LoadFromFile(
                wxFileName( projectFile.GetProject()->GetProjectFullName() ).GetPath() );
    }

    for( const MB_CROSS_BOARD_NET& net : projectFile.GetCrossBoardNets() )
    {
        kiapi::schematic::types::CrossBoardNet* netMsg = response.add_nets();
        netMsg->set_name( net.name.ToStdString() );

        for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
        {
            kiapi::schematic::types::CrossBoardNetEndpoint* epMsg = netMsg->add_endpoints();
            epMsg->set_sub_project_uuid( ep.subProjectUuid.AsStdString() );
            epMsg->set_component_ref( ep.componentRef.ToStdString() );
            epMsg->set_pin_number( ep.pinNumber.ToStdString() );
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetMultiBoardContainerInfoResponse>
API_HANDLER_MBS_SCH::handleGetMultiBoardContainerInfo(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetMultiBoardContainerInfo>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetMultiBoardContainerInfoResponse response;

    PROJECT&      project = m_frame->Prj();
    PROJECT_FILE& projectFile = project.GetProjectFile();

    if( !projectFile.IsMultiBoardContainer() )
    {
        // Empty response is still a valid reply — the caller can detect a
        // standalone schematic by an empty container_pro_path.
        return response;
    }

    response.set_container_pro_path( project.GetProjectFullName().ToStdString() );
    response.set_container_name( project.GetProjectName().ToStdString() );

    if( !projectFile.GetMbsFileName().IsEmpty() )
    {
        wxFileName mbsFn( project.GetProjectPath(), projectFile.GetMbsFileName() );
        response.set_mbs_file_path( mbsFn.GetFullPath().ToStdString() );
    }

    for( const SUB_PROJECT_INFO& info : projectFile.GetSubProjects() )
    {
        kiapi::schematic::types::SubProjectInfo* spMsg = response.add_sub_projects();
        spMsg->set_uuid( info.uuid.AsStdString() );
        spMsg->set_relative_path( info.relativePath.ToStdString() );
        spMsg->set_name( info.name.ToStdString() );

        wxFileName resolved = projectFile.ResolveSubProjectPath( info );
        spMsg->set_absolute_path( resolved.GetFullPath().ToStdString() );
    }

    return response;
}


/// Map an MBS_CHANGE::KIND to a stable string surfaced in the proto.
/// Kept in sync with the proto comment listing the valid `kind` values.
static const char* mbsChangeKindString( MBS_CHANGE::KIND aKind )
{
    switch( aKind )
    {
    case MBS_CHANGE::KIND::ADD_BLOCK:    return "ADD_BLOCK";
    case MBS_CHANGE::KIND::REMOVE_BLOCK: return "REMOVE_BLOCK";
    case MBS_CHANGE::KIND::ADD_PIN:      return "ADD_PIN";
    case MBS_CHANGE::KIND::REMOVE_PIN:   return "REMOVE_PIN";
    case MBS_CHANGE::KIND::RENAME_PIN:   return "RENAME_PIN";
    case MBS_CHANGE::KIND::PATH_DRIFT:   return "PATH_DRIFT";
    case MBS_CHANGE::KIND::UPGRADE_UUID: return "UPGRADE_UUID";
    }

    return "UNKNOWN";
}


/// Populate one proto MbsProposedChange from a C++ MBS_CHANGE entry.
static void packProposedChange( kiapi::schematic::commands::MbsProposedChange* aMsg,
                                int aIndex, const MBS_CHANGE& aCh )
{
    aMsg->set_index( aIndex );
    aMsg->set_kind( mbsChangeKindString( aCh.kind ) );
    aMsg->set_description( aCh.Describe().ToStdString() );
    aMsg->set_sub_project_uuid( aCh.subProjectUuid.AsStdString() );
    aMsg->set_sub_project_path( aCh.subProjectPath.ToStdString() );
    aMsg->set_sub_project_name( aCh.subProjectDisplayName.ToStdString() );
    aMsg->set_component_ref( aCh.componentRef.ToStdString() );
    aMsg->set_pin_number( aCh.pinNumber.ToStdString() );
    aMsg->set_old_label( aCh.oldLabel.ToStdString() );
    aMsg->set_new_label( aCh.newLabel.ToStdString() );
}


HANDLER_RESULT<kiapi::schematic::commands::RefreshMbsFromSubProjectsResponse>
API_HANDLER_MBS_SCH::handleRefreshMbsFromSubProjects(
        const HANDLER_CONTEXT<kiapi::schematic::commands::RefreshMbsFromSubProjects>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT_FILE& container = m_frame->Prj().GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container; "
                             "cannot refresh MBS from sub-projects." );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->Schematic().RootScreen();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "MBS schematic has no root screen; cannot refresh." );
        return tl::unexpected( e );
    }

    std::vector<MBS_CHANGE> changes = ComputeMbsRefreshDiff( *screen, container );

    kiapi::schematic::commands::RefreshMbsFromSubProjectsResponse response;
    response.set_dry_run( aCtx.Request.dry_run() );

    // Always emit the diff so the caller can show a preview / report
    // exactly what was applied.
    for( int i = 0; i < static_cast<int>( changes.size() ); ++i )
        packProposedChange( response.add_proposed_changes(), i, changes[i] );

    if( aCtx.Request.dry_run() )
    {
        // Nothing else to do — apply totals are zero in dry-run mode.
        response.set_summary( wxString::Format(
                                      _( "Preview only: %zu change(s) proposed." ),
                                      changes.size() )
                                      .ToStdString() );
        return response;
    }

    // Apply mode. When apply_indices is non-empty, mark only those
    // entries as `checked = true`; ApplyMbsRefreshChanges skips
    // unchecked entries. Empty list = apply all (the historical
    // behavior — `MBS_CHANGE::checked` defaults to true).
    if( aCtx.Request.apply_indices_size() > 0 )
    {
        std::set<int> selected;

        for( int idx : aCtx.Request.apply_indices() )
            selected.insert( idx );

        for( int i = 0; i < static_cast<int>( changes.size() ); ++i )
            changes[i].checked = ( selected.count( i ) > 0 );
    }

    KIGFX::VIEW*  view = m_frame->GetCanvas() ? m_frame->GetCanvas()->GetView() : nullptr;
    NULL_REPORTER reporter;

    MBS_REFRESH_RESULT res = ApplyMbsRefreshChanges( *screen, changes, view, &reporter );

    if( m_frame->GetCanvas() )
        m_frame->GetCanvas()->Refresh( true );

    response.set_blocks_added( res.blocksAdded );
    response.set_blocks_removed( res.blocksRemoved );
    response.set_pins_added( res.pinsAdded );
    response.set_pins_removed( res.pinsRemoved );
    response.set_pins_renamed( res.pinsRenamed );
    response.set_paths_updated( res.pathsUpdated );
    response.set_uuids_stamped( res.uuidsStamped );
    response.set_summary( res.summary.ToStdString() );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SyncCrossBoardNetsToPcbResponse>
API_HANDLER_MBS_SCH::handleSyncCrossBoardNetsToPcb(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SyncCrossBoardNetsToPcb>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT_FILE& container = m_frame->Prj().GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container; "
                             "cannot push cross-board nets." );
        return tl::unexpected( e );
    }

    // For live PROJECT_FILEs, m_filename is the basename only — Load /
    // SaveToFile with no aDirectory resolve relative to CWD and miss
    // the actual .kicad_pro on disk. Pass the project directory.
    // Resolve via the PROJECT_FILE back-pointer rather than
    // m_frame->Prj(): the back-pointer is unambiguous, while Prj()
    // can return the wrong project in multi-project sessions when
    // no per-frame override is set.
    wxString containerDir;

    if( container.GetProject() )
        containerDir = wxFileName( container.GetProject()->GetProjectFullName() ).GetPath();

    // Reload to pick up cross-board nets written by the MBSCH save hook;
    // the in-memory PROJECT_FILE won't reflect a recent save otherwise.
    container.LoadFromFile( containerDir );

    MB_CROSS_BOARD_SYNC_RESULT result = ApplyCrossBoardNetsToSubProjectPCBs( container );

    container.SaveToFile( containerDir );

    kiapi::schematic::commands::SyncCrossBoardNetsToPcbResponse response;
    response.set_sub_projects_touched( result.subProjectsTouched );
    response.set_endpoints_applied( result.endpointsApplied );
    response.set_endpoints_missing( result.endpointsMissing );
    response.set_nets_renamed( result.netsRenamed );
    response.set_summary( result.summary.ToStdString() );

    for( const MB_NET_NAME_CONFLICT& c : result.conflicts )
    {
        kiapi::schematic::commands::MbsNetNameConflict* msg = response.add_conflicts();
        msg->set_chosen( c.chosen.ToStdString() );

        for( const wxString& rej : c.rejected )
            msg->add_rejected( rej.ToStdString() );
    }

    return response;
}
