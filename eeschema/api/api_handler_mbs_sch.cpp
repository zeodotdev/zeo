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
#include <pgm_base.h>
#include <project/project_file.h>
#include <project.h>
#include <project/cross_board_pcb_sync.h>
#include <settings/settings_manager.h>
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


/// Inverse of packPinType — convert proto enum back to C++ ELECTRICAL_PINTYPE.
static ELECTRICAL_PINTYPE unpackPinType( kiapi::common::types::ElectricalPinType aType )
{
    switch( aType )
    {
    case kiapi::common::types::EPT_INPUT:           return ELECTRICAL_PINTYPE::PT_INPUT;
    case kiapi::common::types::EPT_OUTPUT:          return ELECTRICAL_PINTYPE::PT_OUTPUT;
    case kiapi::common::types::EPT_BIDIRECTIONAL:   return ELECTRICAL_PINTYPE::PT_BIDI;
    case kiapi::common::types::EPT_TRISTATE:        return ELECTRICAL_PINTYPE::PT_TRISTATE;
    case kiapi::common::types::EPT_PASSIVE:         return ELECTRICAL_PINTYPE::PT_PASSIVE;
    case kiapi::common::types::EPT_FREE:            return ELECTRICAL_PINTYPE::PT_NIC;
    case kiapi::common::types::EPT_UNSPECIFIED:     return ELECTRICAL_PINTYPE::PT_UNSPECIFIED;
    case kiapi::common::types::EPT_POWER_INPUT:     return ELECTRICAL_PINTYPE::PT_POWER_IN;
    case kiapi::common::types::EPT_POWER_OUTPUT:    return ELECTRICAL_PINTYPE::PT_POWER_OUT;
    case kiapi::common::types::EPT_OPEN_COLLECTOR:  return ELECTRICAL_PINTYPE::PT_OPENCOLLECTOR;
    case kiapi::common::types::EPT_OPEN_EMITTER:    return ELECTRICAL_PINTYPE::PT_OPENEMITTER;
    case kiapi::common::types::EPT_NO_CONNECT:      return ELECTRICAL_PINTYPE::PT_NC;
    default:                                        return ELECTRICAL_PINTYPE::PT_UNSPECIFIED;
    }
}


static SHEET_SIDE unpackSide( kiapi::schematic::types::SheetPinSide aSide )
{
    switch( aSide )
    {
    case kiapi::schematic::types::SPS_LEFT:   return SHEET_SIDE::LEFT;
    case kiapi::schematic::types::SPS_RIGHT:  return SHEET_SIDE::RIGHT;
    case kiapi::schematic::types::SPS_TOP:    return SHEET_SIDE::TOP;
    case kiapi::schematic::types::SPS_BOTTOM: return SHEET_SIDE::BOTTOM;
    default:                                  return SHEET_SIDE::LEFT;
    }
}


/// Find a SCH_MODULE_BLOCK on the active sheet by UUID.
static SCH_MODULE_BLOCK* findModuleBlock( SCH_EDIT_FRAME* aFrame, const KIID& aId )
{
    SCH_SCREEN* screen = aFrame->GetCurrentSheet().LastScreen();

    if( !screen )
        return nullptr;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );

        if( b->m_Uuid == aId )
            return b;
    }

    return nullptr;
}


/// Find an SCH_MODULE_PIN on the active sheet by pin UUID, returning the pin and its parent block.
static std::pair<SCH_MODULE_PIN*, SCH_MODULE_BLOCK*>
findModulePin( SCH_EDIT_FRAME* aFrame, const KIID& aPinId )
{
    SCH_SCREEN* screen = aFrame->GetCurrentSheet().LastScreen();

    if( !screen )
        return { nullptr, nullptr };

    for( SCH_ITEM* item : screen->Items().OfType( SCH_MODULE_BLOCK_T ) )
    {
        SCH_MODULE_BLOCK* b = static_cast<SCH_MODULE_BLOCK*>( item );

        for( SCH_MODULE_PIN* pin : b->GetPins() )
        {
            if( pin && pin->GetPinUuid() == aPinId )
                return { pin, b };
        }
    }

    return { nullptr, nullptr };
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

    // MBS module-pin / module-block edits.
    registerHandler<UpdateModulePin, UpdateModulePinResponse>(
            &API_HANDLER_MBS_SCH::handleUpdateModulePin );
    registerHandler<DeleteModulePin, DeleteModulePinResponse>(
            &API_HANDLER_MBS_SCH::handleDeleteModulePin );
    registerHandler<UpdateModuleBlock, UpdateModuleBlockResponse>(
            &API_HANDLER_MBS_SCH::handleUpdateModuleBlock );

    // Container-level multi_board rule sets (read/write the .kicad_pro JSON).
    registerHandler<GetMbsRules, GetMbsRulesResponse>(
            &API_HANDLER_MBS_SCH::handleGetMbsRules );
    registerHandler<SetMbsRules, SetMbsRulesResponse>(
            &API_HANDLER_MBS_SCH::handleSetMbsRules );
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

    // Refuse to claim the request if our MBSCH schematic isn't fully
    // bound to a project. Same crash class as API_HANDLER_SCH —
    // SCHEMATIC::m_project nulls during project unload and on PROJECT
    // destroy-hook fire; downstream methods (ErcSettings(), Settings(),
    // etc.) dereference it unconditionally and segfault. AS_UNHANDLED
    // here lets the API server fall through to a peer handler (see
    // common/api/api_server.cpp:501); if none is ready the agent gets
    // a clean "no handler available" error rather than a process kill.
    if( !m_frame || !m_frame->Schematic().IsValid() )
        return false;

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
    // resolve relative to CWD. Resolve via SETTINGS_MANAGER (safe across
    // unload/reload) rather than the PROJECT_FILE back-pointer (raw
    // pointer, can dangle).
    SETTINGS_MANAGER& projSm = Pgm().GetSettingsManager();
    wxString          projContainerDir;

    for( const wxString& proPath : projSm.GetOpenProjects() )
    {
        PROJECT* prj = projSm.GetProject( proPath );

        if( prj && &prj->GetProjectFile() == &projectFile )
        {
            projContainerDir = wxFileName( proPath ).GetPath();
            break;
        }
    }

    if( projContainerDir.IsEmpty() )
        projContainerDir = wxFileName( m_frame->Prj().GetProjectFullName() ).GetPath();

    projectFile.LoadFromFile( projContainerDir );

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
    // Resolve via SETTINGS_MANAGER's open-projects list (authoritative,
    // safe across project unload/reload cycles) rather than the
    // PROJECT_FILE back-pointer (raw pointer, can dangle).
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();
    wxString          containerDir;

    for( const wxString& proPath : sm.GetOpenProjects() )
    {
        PROJECT* prj = sm.GetProject( proPath );

        if( prj && &prj->GetProjectFile() == &container )
        {
            containerDir = wxFileName( proPath ).GetPath();
            break;
        }
    }

    if( containerDir.IsEmpty() )
        containerDir = wxFileName( m_frame->Prj().GetProjectFullName() ).GetPath();

    // Reload to pick up cross-board nets written by the MBSCH save hook;
    // the in-memory PROJECT_FILE won't reflect a recent save otherwise.
    container.LoadFromFile( containerDir );

    MB_CROSS_BOARD_SYNC_RESULT result = ApplyCrossBoardNetsToSubProjectPCBs( container );

    // aForce=true: the sync mutates m_crossBoardNets via SetCrossBoardNets which
    // bypasses the JSON_SETTINGS modified-tracking, so SaveToFile would otherwise
    // skip the write and re-runs would see stale data.
    container.SaveToFile( containerDir, /* aForce */ true );

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


HANDLER_RESULT<kiapi::schematic::commands::UpdateModulePinResponse>
API_HANDLER_MBS_SCH::handleUpdateModulePin(
        const HANDLER_CONTEXT<kiapi::schematic::commands::UpdateModulePin>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID pinId( aCtx.Request.pin_id().value() );
    auto [pin, block] = findModulePin( m_frame, pinId );

    kiapi::schematic::commands::UpdateModulePinResponse response;

    if( !pin || !block )
    {
        response.set_updated( false );
        return response;
    }

    bool changed = false;

    if( aCtx.Request.has_text() )
    {
        pin->SetText( wxString::FromUTF8( aCtx.Request.text() ) );
        changed = true;
    }

    if( aCtx.Request.has_electrical_type() )
    {
        pin->SetType( unpackPinType( aCtx.Request.electrical_type() ) );
        changed = true;
    }

    if( aCtx.Request.has_side() )
    {
        pin->SetSide( unpackSide( aCtx.Request.side() ) );
        changed = true;
    }

    if( aCtx.Request.has_position() )
    {
        VECTOR2I newPos( aCtx.Request.position().x_nm(),
                         aCtx.Request.position().y_nm() );
        pin->SetPosition( newPos );
        pin->ConstrainOnEdge( newPos, true );
        changed = true;
    }

    if( changed )
    {
        m_frame->OnModify();

        if( m_frame->GetCanvas() )
            m_frame->GetCanvas()->Refresh();
    }

    response.set_updated( changed );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::DeleteModulePinResponse>
API_HANDLER_MBS_SCH::handleDeleteModulePin(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteModulePin>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID pinId( aCtx.Request.pin_id().value() );
    auto [pin, block] = findModulePin( m_frame, pinId );

    kiapi::schematic::commands::DeleteModulePinResponse response;

    if( !pin || !block )
    {
        response.set_deleted( false );
        return response;
    }

    block->RemovePin( pin );
    m_frame->OnModify();

    if( m_frame->GetCanvas() )
        m_frame->GetCanvas()->Refresh();

    response.set_deleted( true );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::UpdateModuleBlockResponse>
API_HANDLER_MBS_SCH::handleUpdateModuleBlock(
        const HANDLER_CONTEXT<kiapi::schematic::commands::UpdateModuleBlock>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID blockId( aCtx.Request.block_id().value() );
    SCH_MODULE_BLOCK* block = findModuleBlock( m_frame, blockId );

    kiapi::schematic::commands::UpdateModuleBlockResponse response;

    if( !block )
    {
        response.set_updated( false );
        return response;
    }

    bool changed = false;

    if( aCtx.Request.has_position() )
    {
        VECTOR2I oldPos = block->GetPosition();
        VECTOR2I newPos( aCtx.Request.position().x_nm(),
                         aCtx.Request.position().y_nm() );
        VECTOR2I delta = newPos - oldPos;

        if( delta != VECTOR2I( 0, 0 ) )
        {
            block->Move( delta );
            changed = true;
        }
    }

    if( aCtx.Request.has_mbs_reference() )
    {
        block->SetMbsReference( wxString::FromUTF8( aCtx.Request.mbs_reference() ) );
        changed = true;
    }

    if( changed )
    {
        m_frame->OnModify();

        if( m_frame->GetCanvas() )
            m_frame->GetCanvas()->Refresh();
    }

    response.set_updated( changed );
    return response;
}


/// Resolve the on-disk container directory for the active MBSCH frame.
/// Mirrors the idiom in handleSyncCrossBoardNetsToPcb: PROJECT_FILE has only
/// the basename in m_filename for live projects, so Save/Load needs an
/// explicit directory argument. Falls back to the project's full-name
/// directory when the SETTINGS_MANAGER lookup misses.
static wxString resolveContainerDir( SCH_EDIT_FRAME* aFrame, PROJECT_FILE& aContainer )
{
    wxString containerDir;

    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();

    std::vector<wxString> openProjects = mgr.GetOpenProjects();

    wxLogMessage( "resolveContainerDir: aContainer=%p aFrame=%p openProjects.size()=%zu",
                  &aContainer, aFrame, openProjects.size() );

    for( const wxString& proPath : openProjects )
    {
        PROJECT*      prj  = mgr.GetProject( proPath );
        PROJECT_FILE* pf   = prj ? &prj->GetProjectFile() : nullptr;
        bool          hit  = ( pf == &aContainer );

        wxLogMessage( "  proPath='%s' prj=%p pf=%p hit=%s",
                      proPath, prj, pf, hit ? "true" : "false" );

        if( hit )
        {
            containerDir = wxFileName( proPath ).GetPath();
            break;
        }
    }

    wxString frameProjectFull;
    try
    {
        frameProjectFull = aFrame->Prj().GetProjectFullName();
    }
    catch( ... )
    {
    }

    wxLogMessage( "  frameProjectFull='%s' (fallback)", frameProjectFull );

    if( containerDir.IsEmpty() )
        containerDir = wxFileName( frameProjectFull ).GetPath();

    // Last-ditch fallback: SCH_EDIT_FRAME tracks its own current file path
    // (the .kicad_mbs we're editing). Take its directory when the upstream
    // PROJECT lookups all miss — better to write to the right folder than
    // to write nothing.
    if( containerDir.IsEmpty() && aFrame )
    {
        wxString currentFile = aFrame->GetCurrentFileName();
        wxLogMessage( "  currentFile='%s' (last-ditch)", currentFile );

        if( !currentFile.IsEmpty() )
            containerDir = wxFileName( currentFile ).GetPath();
    }

    return containerDir;
}


HANDLER_RESULT<kiapi::schematic::commands::GetMbsRulesResponse>
API_HANDLER_MBS_SCH::handleGetMbsRules(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetMbsRules>& aCtx )
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
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    // Reload from disk so we see any rule edits made out-of-band (or via
    // a previous SetMbsRules call that hasn't propagated to the in-memory
    // PROJECT_FILE yet — see the same pattern in handleGetCrossBoardNets).
    wxString containerDir = resolveContainerDir( m_frame, container );

    wxLogMessage( "MBS_GET[%p]: containerDir='%s' container=%p file='%s'",
                  this, containerDir, &container, container.GetFilename() );

    bool loadOk = container.LoadFromFile( containerDir );

    wxLogMessage( "MBS_GET[%p]: LoadFromFile returned %s; map sizes: minPins=%zu "
                  "maxLen=%zu diffPairs=%zu current=%zu voltage=%zu",
                  this, loadOk ? "true" : "false",
                  container.GetMinPowerPins().size(),
                  container.GetMaxLengthNm().size(),
                  container.GetCrossBoardDiffPairs().size(),
                  container.GetCrossBoardCurrentRules().size(),
                  container.GetCrossBoardVoltageRules().size() );

    kiapi::schematic::commands::GetMbsRulesResponse response;
    auto* rules = response.mutable_rules();

    for( const auto& [name, minPins] : container.GetMinPowerPins() )
    {
        auto* msg = rules->add_min_power_pins();
        msg->set_net_name( name.ToStdString() );
        msg->set_min_pins( minPins );
    }

    for( const auto& [name, maxLen] : container.GetMaxLengthNm() )
    {
        auto* msg = rules->add_max_length_nm();
        msg->set_net_name( name.ToStdString() );
        msg->set_max_length_nm( maxLen );
    }

    for( const auto& [p, n] : container.GetCrossBoardDiffPairs() )
    {
        auto* msg = rules->add_cross_board_diff_pairs();
        msg->set_p( p.ToStdString() );
        msg->set_n( n.ToStdString() );
    }

    for( const auto& [name, rule] : container.GetCrossBoardCurrentRules() )
    {
        auto* msg = rules->add_current_rules();
        msg->set_net_name( name.ToStdString() );
        msg->set_expected_amps( rule.expectedAmps );
        msg->set_pin_rating_amps( rule.pinRatingAmps );
    }

    for( const auto& [name, rule] : container.GetCrossBoardVoltageRules() )
    {
        auto* msg = rules->add_voltage_rules();
        msg->set_net_name( name.ToStdString() );
        msg->set_expected_amps( rule.expectedAmps );
        msg->set_max_drop_mv( rule.maxDropMv );
        msg->set_trace_width_um( rule.traceWidthUm );
        msg->set_trace_sheet_r_milliohm_per_sq( rule.traceSheetRMOhmsPerSq );
        msg->set_contact_r_per_pin_milliohm( rule.contactRPerPinMOhms );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SetMbsRulesResponse>
API_HANDLER_MBS_SCH::handleSetMbsRules(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SetMbsRules>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

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
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    wxString containerDir = resolveContainerDir( m_frame, container );

    wxLogMessage( "MBS_SET[%p]: containerDir='%s' container=%p file='%s'",
                  this, containerDir, &container, container.GetFilename() );

    // Reload first so we don't clobber out-of-band edits to other rule sets.
    bool loadOk = container.LoadFromFile( containerDir );

    wxLogMessage( "MBS_SET[%p]: LoadFromFile returned %s; pre-mutate map sizes: "
                  "minPins=%zu maxLen=%zu diffPairs=%zu current=%zu voltage=%zu",
                  this, loadOk ? "true" : "false",
                  container.GetMinPowerPins().size(),
                  container.GetMaxLengthNm().size(),
                  container.GetCrossBoardDiffPairs().size(),
                  container.GetCrossBoardCurrentRules().size(),
                  container.GetCrossBoardVoltageRules().size() );

    const auto& rules = aCtx.Request.rules();
    bool anyChanged = false;

    if( aCtx.Request.replace_min_power_pins() )
    {
        auto& dest = container.GetMinPowerPins();
        dest.clear();

        for( const auto& msg : rules.min_power_pins() )
            dest[wxString::FromUTF8( msg.net_name() )] = msg.min_pins();

        anyChanged = true;
    }

    if( aCtx.Request.replace_max_length_nm() )
    {
        auto& dest = container.GetMaxLengthNm();
        dest.clear();

        for( const auto& msg : rules.max_length_nm() )
            dest[wxString::FromUTF8( msg.net_name() )] = msg.max_length_nm();

        anyChanged = true;
    }

    if( aCtx.Request.replace_cross_board_diff_pairs() )
    {
        auto& dest = container.GetCrossBoardDiffPairs();
        dest.clear();

        for( const auto& msg : rules.cross_board_diff_pairs() )
        {
            dest.push_back( { wxString::FromUTF8( msg.p() ),
                              wxString::FromUTF8( msg.n() ) } );
        }

        anyChanged = true;
    }

    if( aCtx.Request.replace_current_rules() )
    {
        auto& dest = container.GetCrossBoardCurrentRules();
        dest.clear();

        for( const auto& msg : rules.current_rules() )
        {
            PROJECT_FILE::MB_CURRENT_RULE rule;
            rule.expectedAmps  = msg.expected_amps();
            rule.pinRatingAmps = msg.pin_rating_amps();
            dest[wxString::FromUTF8( msg.net_name() )] = rule;
        }

        anyChanged = true;
    }

    if( aCtx.Request.replace_voltage_rules() )
    {
        auto& dest = container.GetCrossBoardVoltageRules();
        dest.clear();

        for( const auto& msg : rules.voltage_rules() )
        {
            PROJECT_FILE::MB_VOLTAGE_RULE rule;
            rule.expectedAmps          = msg.expected_amps();
            rule.maxDropMv             = msg.max_drop_mv();
            rule.traceWidthUm          = msg.trace_width_um();
            rule.traceSheetRMOhmsPerSq = msg.trace_sheet_r_milliohm_per_sq();
            rule.contactRPerPinMOhms   = msg.contact_r_per_pin_milliohm();
            dest[wxString::FromUTF8( msg.net_name() )] = rule;
        }

        anyChanged = true;
    }

    wxLogMessage( "MBS_SET[%p]: post-mutate map sizes: minPins=%zu maxLen=%zu "
                  "diffPairs=%zu current=%zu voltage=%zu  anyChanged=%s "
                  "writeFile=%s readOnly=%s",
                  this,
                  container.GetMinPowerPins().size(),
                  container.GetMaxLengthNm().size(),
                  container.GetCrossBoardDiffPairs().size(),
                  container.GetCrossBoardCurrentRules().size(),
                  container.GetCrossBoardVoltageRules().size(),
                  anyChanged ? "true" : "false",
                  container.IsReadOnly() ? "false" : "true",
                  container.IsReadOnly() ? "true" : "false" );

    bool saveOk = false;

    if( anyChanged )
    {
        // aForce=true is mandatory: the framework's "did anything change"
        // bookkeeping watches its own PARAM Set() calls, but we mutate
        // the underlying std::map members directly. Without force, the
        // save is a silent no-op and the next get() returns whatever's
        // still on disk (i.e. the pre-set values) — round-trip broken.
        saveOk = container.SaveToFile( containerDir, /* aForce */ true );

        wxLogMessage( "MBS_SET[%p]: SaveToFile('%s', aForce=true) returned %s",
                      this, containerDir, saveOk ? "true" : "false" );
    }

    kiapi::schematic::commands::SetMbsRulesResponse response;
    // Only report updated=true when the disk write actually succeeded.
    // Otherwise the agent sees "updated:true" then a get() returns empty
    // and the round-trip looks broken when really the save silently
    // failed (read-only file, missing path, m_writeFile=false, etc.).
    response.set_updated( anyChanged && saveOk );
    return response;
}
