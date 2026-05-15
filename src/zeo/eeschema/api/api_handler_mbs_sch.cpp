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

#include <functional>
#include <memory>
#include <optional>

#include <api/api_utils.h>
#include <kiway.h>
#include <usage_sync.h>
#include <kiway_player.h>
#include <frame_type.h>
#include <pgm_base.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <project/multi_board_propagate_settings.h>
#include <project.h>
#include <project/cross_board_pcb_sync.h>
#include <netclass.h>
#include <settings/settings_manager.h>
#include <schematic.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <view/view.h>
#include <sch_draw_panel.h>
#include <reporter.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>

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
            if( pin && pin->m_Uuid == aPinId )
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

    // MOON-1333 Phase 1: read-only inspection reports for the desktop
    // Schematic Setup → Net Classes panel + symbol/footprint library
    // tables. Lets the agent see container ↔ sub-project replication
    // state without mutating anything.
    registerHandler<GetMultiBoardNetClassReport, GetMultiBoardNetClassReportResponse>(
            &API_HANDLER_MBS_SCH::handleGetMultiBoardNetClassReport );
    registerHandler<GetMultiBoardLibraryReport, GetMultiBoardLibraryReportResponse>(
            &API_HANDLER_MBS_SCH::handleGetMultiBoardLibraryReport );

    // MOON-1333 Phase 2: container-side netclass mutation. The set
    // handler runs MultiBoardPropagateNetSettings after to fan out to
    // loaded peers (silent USE_CONTAINER on conflict — no UI dialog,
    // suitable for headless agent calls).
    registerHandler<SetMultiBoardNetClass, SetMultiBoardNetClassResponse>(
            &API_HANDLER_MBS_SCH::handleSetMultiBoardNetClass );
    registerHandler<DeleteMultiBoardNetClass, DeleteMultiBoardNetClassResponse>(
            &API_HANDLER_MBS_SCH::handleDeleteMultiBoardNetClass );

    // MOON-1333 Phase 3: container-scope library mutations. All three
    // route through the LIBRARY_MANAGER M7.1 helpers (AddSharedLibrary /
    // RemoveSharedLibrary), which cascade to every sub-project's
    // lib-table on disk with conflict-handling semantics.
    registerHandler<AddMultiBoardLibrary, AddMultiBoardLibraryResponse>(
            &API_HANDLER_MBS_SCH::handleAddMultiBoardLibrary );
    registerHandler<DeleteMultiBoardLibrary, DeleteMultiBoardLibraryResponse>(
            &API_HANDLER_MBS_SCH::handleDeleteMultiBoardLibrary );
    registerHandler<ShareMultiBoardLibrary, ShareMultiBoardLibraryResponse>(
            &API_HANDLER_MBS_SCH::handleShareMultiBoardLibrary );
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

    // Refuse to claim the request if the frame isn't ready to serve.
    //
    // We deliberately do NOT mirror API_HANDLER_SCH's stricter
    // `Schematic().IsValid()` (m_project && m_rootSheet) check here.
    // The MBS handlers in this file all read through `m_frame->Prj()`
    // (the FRAME's bound project), not through `Schematic().m_project`;
    // the latter can null transiently via the SETTINGS_MANAGER project
    // destroy hook (`m_project = nullptr`) under multi-project sessions
    // — sub-project PCB editors, peer windows, etc. — even when the
    // frame's own Prj() is fine. Gating on `Schematic().IsValid()`
    // there made the MBSCH frame go silent ("no handler available")
    // mid-session as soon as anything nudged the schematic's project
    // pointer, blocking the agent's verification campaign. The minimum
    // we actually need is: the frame is alive and its current sheet has
    // a screen. The one handler that touches `Schematic().RootScreen()`
    // (`handleRefreshMbsFromSubProjects`) null-checks the screen at the
    // call site, so the segfault class API_HANDLER_SCH worries about
    // (`Schematic().ErcSettings()` etc.) doesn't apply here.
    if( !m_frame || !m_frame->GetCurrentSheet().LastScreen() )
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
            // pin->m_Uuid is the unique SCH_ITEM identifier for this module pin,
            // assigned at construction. pin->GetPinUuid() is metadata pointing
            // at the source connector pin in the sub-project — it's NOT unique
            // (and is nil for pins built fresh by RefreshMbsFromSubProjects,
            // which never calls SetPinUuid). Using m_Uuid here gives the agent
            // a stable round-trip identifier for UpdateModulePin / DeleteModulePin.
            pinMsg->mutable_id()->set_value( pin->m_Uuid.AsStdString() );
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

    USAGE_SYNC::Instance()->TrackEvent( "mbs.refresh", "mbsch" );

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

    // T3: route every mutation through the new PROJECT_FILE setters so
    // observer fan-out fires for every peer (other windows, dialogs,
    // cached views). Coalesce into one event per field via the
    // SUSPEND_NOTIFY guard — without it, "replace 50 entries" would
    // emit 50 events per field.
    {
        PROJECT_FILE_SUSPEND_NOTIFY notifyGuard( container );

        if( aCtx.Request.replace_min_power_pins() )
        {
            container.ClearMinPowerPins();

            for( const auto& msg : rules.min_power_pins() )
            {
                container.SetMinPowerPin( wxString::FromUTF8( msg.net_name() ),
                                          msg.min_pins() );
            }

            anyChanged = true;
        }

        if( aCtx.Request.replace_max_length_nm() )
        {
            container.ClearMaxLengthNm();

            for( const auto& msg : rules.max_length_nm() )
            {
                container.SetMaxLengthNm( wxString::FromUTF8( msg.net_name() ),
                                          msg.max_length_nm() );
            }

            anyChanged = true;
        }

        if( aCtx.Request.replace_cross_board_diff_pairs() )
        {
            container.ClearCrossBoardDiffPairs();

            for( const auto& msg : rules.cross_board_diff_pairs() )
            {
                container.AddCrossBoardDiffPair( wxString::FromUTF8( msg.p() ),
                                                 wxString::FromUTF8( msg.n() ) );
            }

            anyChanged = true;
        }

        if( aCtx.Request.replace_current_rules() )
        {
            container.ClearCrossBoardCurrentRules();

            for( const auto& msg : rules.current_rules() )
            {
                PROJECT_FILE::MB_CURRENT_RULE rule;
                rule.expectedAmps  = msg.expected_amps();
                rule.pinRatingAmps = msg.pin_rating_amps();

                container.SetCrossBoardCurrentRule(
                        wxString::FromUTF8( msg.net_name() ), rule );
            }

            anyChanged = true;
        }

        if( aCtx.Request.replace_voltage_rules() )
        {
            container.ClearCrossBoardVoltageRules();

            for( const auto& msg : rules.voltage_rules() )
            {
                PROJECT_FILE::MB_VOLTAGE_RULE rule;
                rule.expectedAmps          = msg.expected_amps();
                rule.maxDropMv             = msg.max_drop_mv();
                rule.traceWidthUm          = msg.trace_width_um();
                rule.traceSheetRMOhmsPerSq = msg.trace_sheet_r_milliohm_per_sq();
                rule.contactRPerPinMOhms   = msg.contact_r_per_pin_milliohm();

                container.SetCrossBoardVoltageRule(
                        wxString::FromUTF8( msg.net_name() ), rule );
            }

            anyChanged = true;
        }
    }   // SUSPEND_NOTIFY guard drains here — one coalesced event per affected field

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
        // aForce=true was historically required because the legacy code
        // mutated `std::map` members directly, bypassing the PARAM-level
        // "did anything change" tracking. T3 setters now mark the file
        // dirty correctly so aForce is no longer mandatory — but we
        // keep it for defensive belt-and-braces: if the agent caller
        // sent a "replace" with the same payload that's already on
        // disk, the round-trip should still produce a fresh write so
        // the agent's get() reads back exactly what it sent.
        //
        // SaveToFile internally stamps the self-write filter (Phase 3)
        // so MBSCH's file watcher (Phase 4) won't rebound on this write.
        // Observers in any peer have already fired during the SUSPEND_NOTIFY
        // drain above.
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


// =============================================================================
// MOON-1333 Phase 1 — Inspection reports for net classes + libraries.
// =============================================================================
//
// Both reports are gated on the active project being a multi-board container,
// so the desktop's MBSCH must be focused for the agent to call them. Sub-
// project state is read from SETTINGS_MANAGER when the peer is loaded; for
// disk-only peers we construct a transient PROJECT_FILE / LIBRARY_TABLE and
// load it. This mirrors the propagator's ephemeral-load path (MOON-1295) so
// the agent sees the same set of sub-projects whether they're open or not.

namespace
{

/// Pack a NETCLASS into the wire-format MbsNetClassFields. Optional scalars
/// only set the proto field when the source has the corresponding `Has*`
/// (which translates to the sub-project's "use container default" semantics).
/// Colors round-trip via COLOR4D::ToCSSString — empty string when UNSPECIFIED.
void packNetClassFields( const NETCLASS& aNc,
                         kiapi::schematic::commands::MbsNetClassFields* aOut )
{
    aOut->set_name( aNc.GetName().ToStdString() );
    aOut->set_priority( aNc.GetPriority() );
    aOut->set_tuning_profile( aNc.GetTuningProfile().ToStdString() );

    KIGFX::COLOR4D pcb = aNc.GetPcbColor( true );
    KIGFX::COLOR4D sch = aNc.GetSchematicColor( true );

    if( pcb != KIGFX::COLOR4D::UNSPECIFIED )
        aOut->set_pcb_color_css( pcb.ToCSSString().ToStdString() );

    if( sch != KIGFX::COLOR4D::UNSPECIFIED )
        aOut->set_schematic_color_css( sch.ToCSSString().ToStdString() );

    auto setOpt = [&]( const std::optional<int>& aSrc, auto setter )
    {
        if( aSrc.has_value() )
            ( aOut->*setter )( *aSrc );
    };

    setOpt( aNc.GetClearanceOpt(),     &kiapi::schematic::commands::MbsNetClassFields::set_clearance_iu );
    setOpt( aNc.GetTrackWidthOpt(),    &kiapi::schematic::commands::MbsNetClassFields::set_track_width_iu );
    setOpt( aNc.GetViaDiameterOpt(),   &kiapi::schematic::commands::MbsNetClassFields::set_via_diameter_iu );
    setOpt( aNc.GetViaDrillOpt(),      &kiapi::schematic::commands::MbsNetClassFields::set_via_drill_iu );
    setOpt( aNc.GetuViaDiameterOpt(),  &kiapi::schematic::commands::MbsNetClassFields::set_uvia_diameter_iu );
    setOpt( aNc.GetuViaDrillOpt(),     &kiapi::schematic::commands::MbsNetClassFields::set_uvia_drill_iu );
    setOpt( aNc.GetDiffPairWidthOpt(), &kiapi::schematic::commands::MbsNetClassFields::set_diff_pair_width_iu );
    setOpt( aNc.GetDiffPairGapOpt(),   &kiapi::schematic::commands::MbsNetClassFields::set_diff_pair_gap_iu );
    setOpt( aNc.GetDiffPairViaGapOpt(), &kiapi::schematic::commands::MbsNetClassFields::set_diff_pair_via_gap_iu );
    setOpt( aNc.GetWireWidthOpt(),     &kiapi::schematic::commands::MbsNetClassFields::set_wire_width_iu );
    setOpt( aNc.GetBusWidthOpt(),      &kiapi::schematic::commands::MbsNetClassFields::set_bus_width_iu );

    if( aNc.HasLineStyle() )
        aOut->set_line_style( aNc.GetLineStyle() );
}


/// Resolve a sub-project's net_settings. Prefer the in-memory copy from the
/// SETTINGS_MANAGER-loaded PROJECT (matches what an open editor sees). For
/// disk-only peers, construct a transient PROJECT_FILE and LoadFromFile.
/// Returned pointer is only valid for the lifetime of `aTransient`; the
/// caller must keep it alive while inspecting `*classes`.
struct SubProjectNetSettingsAccess
{
    std::shared_ptr<NET_SETTINGS> netSettings;   ///< Non-owning when loaded; owning when transient
    bool                          loaded;        ///< True iff the sub-project was already in SM
    wxString                      readError;     ///< Empty on success
    std::unique_ptr<PROJECT_FILE> transient;     ///< Holds the transient PROJECT_FILE if disk-loaded
};


SubProjectNetSettingsAccess accessSubProjectNetSettings( const wxString& aAbsPath )
{
    SubProjectNetSettingsAccess access;

    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

    if( PROJECT* loadedProject = sm.GetProject( aAbsPath ) )
    {
        access.loaded = true;
        access.netSettings = loadedProject->GetProjectFile().NetSettings();

        if( !access.netSettings )
            access.readError = wxT( "Loaded sub-project has no NET_SETTINGS." );

        return access;
    }

    access.loaded = false;
    access.transient = std::make_unique<PROJECT_FILE>( aAbsPath );

    wxFileName fn( aAbsPath );

    if( !access.transient->LoadFromFile( fn.GetPath() ) )
    {
        access.readError = wxString::Format(
                wxT( "Failed to load sub-project .kicad_pro at '%s'." ), aAbsPath );
        return access;
    }

    access.netSettings = access.transient->NetSettings();

    if( !access.netSettings )
        access.readError = wxT( "Disk-loaded sub-project has no NET_SETTINGS." );

    return access;
}

} // namespace


HANDLER_RESULT<kiapi::schematic::commands::GetMultiBoardNetClassReportResponse>
API_HANDLER_MBS_SCH::handleGetMultiBoardNetClassReport(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetMultiBoardNetClassReport>& aCtx )
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

    std::shared_ptr<NET_SETTINGS> containerNS = container.NetSettings();

    if( !containerNS )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Container project has no NET_SETTINGS." );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetMultiBoardNetClassReportResponse response;

    // Container-side: every class is SOURCE. Iterate the default class
    // first so the agent always sees it; then named classes in priority
    // order matching the desktop panel.
    auto packContainerEntry = [&]( const NETCLASS& aNc )
    {
        auto* entry = response.add_container_classes();
        packNetClassFields( aNc, entry->mutable_fields() );
        entry->set_status( kiapi::schematic::commands::MBS_NET_CLASS_SOURCE );
    };

    if( const std::shared_ptr<NETCLASS>& def = containerNS->GetDefaultNetclass() )
        packContainerEntry( *def );

    for( const auto& [name, nc] : containerNS->GetNetclasses() )
    {
        if( nc )
            packContainerEntry( *nc );
    }

    // Sub-projects: drive iteration off the container's sub-project list
    // (same as the propagator) so disk-only peers also get reported.
    for( const SUB_PROJECT_INFO& info : container.GetSubProjects() )
    {
        wxFileName resolved = container.ResolveSubProjectPath( info );
        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        wxString absPath = resolved.GetFullPath();

        auto* bucket = response.add_sub_projects();
        bucket->set_sub_project_uuid( info.uuid.AsString().ToStdString() );
        bucket->set_display_name(
                info.displayName.IsEmpty() ? info.name.ToStdString()
                                           : info.displayName.ToStdString() );
        bucket->set_absolute_path( absPath.ToStdString() );

        SubProjectNetSettingsAccess access = accessSubProjectNetSettings( absPath );

        bucket->set_loaded( access.loaded );

        if( !access.readError.IsEmpty() )
        {
            bucket->set_read_error( access.readError.ToStdString() );
            continue;
        }

        if( !access.netSettings )
            continue;

        // Build a name-keyed view of the container's classes so per-class
        // status classification is O(N+M) instead of O(N*M).
        const auto& containerClasses = containerNS->GetNetclasses();

        auto classifyAndPack = [&]( const NETCLASS& aSubNc )
        {
            auto* entry = bucket->add_classes();
            packNetClassFields( aSubNc, entry->mutable_fields() );

            auto it = containerClasses.find( aSubNc.GetName() );

            if( it == containerClasses.end() )
            {
                entry->set_status( kiapi::schematic::commands::MBS_NET_CLASS_LOCAL );
                return;
            }

            if( it->second
                && MultiBoardNetclassesEquivalent( *it->second, aSubNc ) )
            {
                entry->set_status( kiapi::schematic::commands::MBS_NET_CLASS_SHARED );
            }
            else
            {
                entry->set_status( kiapi::schematic::commands::MBS_NET_CLASS_CONFLICT );
            }
        };

        if( const std::shared_ptr<NETCLASS>& def = access.netSettings->GetDefaultNetclass() )
            classifyAndPack( *def );

        for( const auto& [name, nc] : access.netSettings->GetNetclasses() )
        {
            if( nc )
                classifyAndPack( *nc );
        }
    }

    return response;
}


namespace
{

/// Pack a single LIBRARY_TABLE_ROW into proto. Status is read directly from
/// the row's shared/conflict flags (MOON-1294 sets these during reconcile);
/// scope comes from the table the row was iterated from, NOT the row's own
/// `m_scope` field (which is per-table, not per-row).
void packLibRow( const LIBRARY_TABLE_ROW& aRow,
                 kiapi::schematic::commands::MbsLibraryKind aKind,
                 kiapi::schematic::commands::MbsLibraryScope aScope,
                 kiapi::schematic::commands::MbsLibraryEntry* aOut )
{
    aOut->set_nickname( aRow.Nickname().ToStdString() );
    aOut->set_uri( aRow.URI().ToStdString() );
    aOut->set_description( aRow.Description().ToStdString() );
    aOut->set_options( aRow.Options().ToStdString() );
    aOut->set_enabled( !aRow.Disabled() );
    aOut->set_visible( !aRow.Hidden() );
    aOut->set_kind( aKind );
    aOut->set_scope( aScope );

    if( aRow.Conflict() )
        aOut->set_status( kiapi::schematic::commands::MBS_LIBRARY_STATUS_CONFLICT );
    else if( aRow.Shared() )
        aOut->set_status( kiapi::schematic::commands::MBS_LIBRARY_STATUS_SHARED );
    else
        aOut->set_status( kiapi::schematic::commands::MBS_LIBRARY_STATUS_LOCAL );
}


/// Read both sym + fp library tables for a sub-project from disk and pack
/// them into the response bucket. KiCad's project library tables live at
/// `<sub_project_dir>/sym-lib-table` and `<sub_project_dir>/fp-lib-table`
/// (no extensions), unconditionally — same convention LIBRARY_MANAGER uses.
void readSubProjectLibTables( const wxString& aSubProjectDir,
                              kiapi::schematic::commands::MbsSubProjectLibraries* aBucket )
{
    auto readOne = [&]( LIBRARY_TABLE_TYPE aType,
                        kiapi::schematic::commands::MbsLibraryKind aKind,
                        const wxString& aFileName )
    {
        wxFileName tablePath( aSubProjectDir, aFileName );

        if( !tablePath.FileExists() )
            return;   // Not having the table file is normal (empty table)

        LIBRARY_TABLE table( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );

        if( !table.IsOk() )
            return;

        for( const LIBRARY_TABLE_ROW& row : table.Rows() )
        {
            packLibRow( row, aKind, kiapi::schematic::commands::MBS_LIBRARY_SCOPE_PROJECT,
                        aBucket->add_rows() );
        }
    };

    readOne( LIBRARY_TABLE_TYPE::SYMBOL,    kiapi::schematic::commands::MBS_LIBRARY_SYMBOL,
             wxT( "sym-lib-table" ) );
    readOne( LIBRARY_TABLE_TYPE::FOOTPRINT, kiapi::schematic::commands::MBS_LIBRARY_FOOTPRINT,
             wxT( "fp-lib-table" ) );
}

} // namespace


HANDLER_RESULT<kiapi::schematic::commands::GetMultiBoardLibraryReportResponse>
API_HANDLER_MBS_SCH::handleGetMultiBoardLibraryReport(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetMultiBoardLibraryReport>& aCtx )
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

    kiapi::schematic::commands::GetMultiBoardLibraryReportResponse response;

    LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();

    // Global tables — user-wide, identical across every project.
    // `aListMutator` is invoked once per row to obtain a fresh proto entry
    // pointer. Passed by value (lambdas that capture `response` by ref) so
    // callers can pick whether new rows go into `global_rows` vs
    // `container_rows`.
    auto packTable = [&]( LIBRARY_TABLE_TYPE aType,
                          kiapi::schematic::commands::MbsLibraryKind aKind,
                          LIBRARY_TABLE_SCOPE aScope,
                          kiapi::schematic::commands::MbsLibraryScope aProtoScope,
                          const std::function<kiapi::schematic::commands::MbsLibraryEntry*()>& aListMutator )
    {
        std::optional<LIBRARY_TABLE*> table = mgr.Table( aType, aScope );

        if( !table.has_value() || !*table )
            return;

        for( const LIBRARY_TABLE_ROW& row : ( *table )->Rows() )
            packLibRow( row, aKind, aProtoScope, aListMutator() );
    };

    auto globalAdder = [&]() { return response.add_global_rows(); };

    packTable( LIBRARY_TABLE_TYPE::SYMBOL,
               kiapi::schematic::commands::MBS_LIBRARY_SYMBOL,
               LIBRARY_TABLE_SCOPE::GLOBAL,
               kiapi::schematic::commands::MBS_LIBRARY_SCOPE_GLOBAL,
               globalAdder );

    packTable( LIBRARY_TABLE_TYPE::FOOTPRINT,
               kiapi::schematic::commands::MBS_LIBRARY_FOOTPRINT,
               LIBRARY_TABLE_SCOPE::GLOBAL,
               kiapi::schematic::commands::MBS_LIBRARY_SCOPE_GLOBAL,
               globalAdder );

    // Container project tables — by construction the active project IS the
    // container in MBSCH, so LIBRARY_MANAGER's project tables apply.
    auto containerAdder = [&]() { return response.add_container_rows(); };

    packTable( LIBRARY_TABLE_TYPE::SYMBOL,
               kiapi::schematic::commands::MBS_LIBRARY_SYMBOL,
               LIBRARY_TABLE_SCOPE::PROJECT,
               kiapi::schematic::commands::MBS_LIBRARY_SCOPE_CONTAINER,
               containerAdder );

    packTable( LIBRARY_TABLE_TYPE::FOOTPRINT,
               kiapi::schematic::commands::MBS_LIBRARY_FOOTPRINT,
               LIBRARY_TABLE_SCOPE::PROJECT,
               kiapi::schematic::commands::MBS_LIBRARY_SCOPE_CONTAINER,
               containerAdder );

    // Sub-project tables — read each from disk. We deliberately don't go
    // through SETTINGS_MANAGER here even for loaded sub-projects: their
    // lib tables aren't in LIBRARY_MANAGER's m_projectTables (those slots
    // hold the active project's tables), so disk-read is the consistent
    // path for every peer.
    for( const SUB_PROJECT_INFO& info : container.GetSubProjects() )
    {
        wxFileName resolved = container.ResolveSubProjectPath( info );
        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        wxString absPath = resolved.GetFullPath();

        auto* bucket = response.add_sub_projects();
        bucket->set_sub_project_uuid( info.uuid.AsString().ToStdString() );
        bucket->set_display_name(
                info.displayName.IsEmpty() ? info.name.ToStdString()
                                           : info.displayName.ToStdString() );
        bucket->set_absolute_path( absPath.ToStdString() );
        bucket->set_loaded(
                Pgm().GetSettingsManager().GetProject( absPath ) != nullptr );

        wxFileName subDir( absPath );
        readSubProjectLibTables( subDir.GetPath(), bucket );
    }

    return response;
}


namespace
{

/// Apply a wire-format MbsNetClassFields onto an existing or fresh NETCLASS.
/// Optional fields use the proto3 `has_*` accessor — unset means "leave the
/// existing value alone" on update, or "use NETCLASS ctor default" on create.
/// Colors round-trip through the same CSS string COLOR4D::ToCSSString uses.
void applyNetClassFields(
        const kiapi::schematic::commands::MbsNetClassFields& aFields,
        NETCLASS& aNc )
{
    if( !aFields.tuning_profile().empty() )
        aNc.SetTuningProfile( wxString::FromUTF8( aFields.tuning_profile() ) );
    else
        aNc.SetTuningProfile( wxEmptyString );

    if( !aFields.pcb_color_css().empty() )
    {
        KIGFX::COLOR4D c;
        c.SetFromWxString( wxString::FromUTF8( aFields.pcb_color_css() ) );
        aNc.SetPcbColor( c );
    }
    else
    {
        aNc.SetPcbColor( KIGFX::COLOR4D::UNSPECIFIED );
    }

    if( !aFields.schematic_color_css().empty() )
    {
        KIGFX::COLOR4D c;
        c.SetFromWxString( wxString::FromUTF8( aFields.schematic_color_css() ) );
        aNc.SetSchematicColor( c );
    }
    else
    {
        aNc.SetSchematicColor( KIGFX::COLOR4D::UNSPECIFIED );
    }

    // Optional<int>: convert "field present" to set, "field absent" to clear.
    auto applyOpt = [&]( bool present, int value, auto setter )
    {
        if( present )
            ( aNc.*setter )( std::optional<int>( value ) );
        else
            ( aNc.*setter )( std::optional<int>() );
    };

    applyOpt( aFields.has_clearance_iu(),         aFields.clearance_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetClearance ) );
    applyOpt( aFields.has_track_width_iu(),       aFields.track_width_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetTrackWidth ) );
    applyOpt( aFields.has_via_diameter_iu(),      aFields.via_diameter_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetViaDiameter ) );
    applyOpt( aFields.has_via_drill_iu(),         aFields.via_drill_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetViaDrill ) );
    applyOpt( aFields.has_uvia_diameter_iu(),     aFields.uvia_diameter_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetuViaDiameter ) );
    applyOpt( aFields.has_uvia_drill_iu(),        aFields.uvia_drill_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetuViaDrill ) );
    applyOpt( aFields.has_diff_pair_width_iu(),   aFields.diff_pair_width_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetDiffPairWidth ) );
    applyOpt( aFields.has_diff_pair_gap_iu(),     aFields.diff_pair_gap_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetDiffPairGap ) );
    applyOpt( aFields.has_diff_pair_via_gap_iu(), aFields.diff_pair_via_gap_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetDiffPairViaGap ) );
    applyOpt( aFields.has_wire_width_iu(),        aFields.wire_width_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetWireWidth ) );
    applyOpt( aFields.has_bus_width_iu(),         aFields.bus_width_iu(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetBusWidth ) );
    applyOpt( aFields.has_line_style(),           aFields.line_style(),
              static_cast<void ( NETCLASS::* )( std::optional<int> )>( &NETCLASS::SetLineStyle ) );
}

} // namespace


HANDLER_RESULT<kiapi::schematic::commands::SetMultiBoardNetClassResponse>
API_HANDLER_MBS_SCH::handleSetMultiBoardNetClass(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SetMultiBoardNetClass>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT&       project   = m_frame->Prj();
    PROJECT_FILE&  container = project.GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    std::shared_ptr<NET_SETTINGS> netSettings = container.NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Container project has no NET_SETTINGS." );
        return tl::unexpected( e );
    }

    const auto& fields = aCtx.Request.fields();
    wxString    name   = wxString::FromUTF8( fields.name() );

    if( name.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Netclass name is required." );
        return tl::unexpected( e );
    }

    // Default class is set in-place (cannot be re-created); other classes
    // get added or in-place updated by SetNetclass replacing the entry.
    bool created = false;
    bool isDefault = ( name == NETCLASS::Default );

    if( isDefault )
    {
        if( const std::shared_ptr<NETCLASS>& def = netSettings->GetDefaultNetclass() )
            applyNetClassFields( fields, *def );
    }
    else
    {
        const auto& classes = netSettings->GetNetclasses();
        auto it = classes.find( name );

        if( it == classes.end() )
        {
            // Create — second arg `false` = no defaults (we'll set what
            // the caller sent and leave the rest unset; matches the
            // sub-project propagation clone semantics).
            auto fresh = std::make_shared<NETCLASS>( name, false );
            applyNetClassFields( fields, *fresh );
            netSettings->SetNetclass( name, fresh );
            created = true;
        }
        else if( it->second )
        {
            applyNetClassFields( fields, *it->second );
        }
    }

    // Persist the in-memory mutation to disk so loaded sub-projects'
    // panels see the new state next time they open. Match the panel
    // hook's flow (panel_setup_netclasses.cpp): SaveProject first,
    // propagator second.
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();
    sm.SaveProject( project.GetProjectFullName(), &project );

    // Run the propagator; silent USE_CONTAINER on conflict (no UI),
    // since this is a headless agent path. The desktop's interactive
    // flow goes through MultiBoardPropagateNetSettingsWithDialog.
    MULTI_BOARD_PROPAGATE_RESULT propResult =
            MultiBoardPropagateNetSettings( project, /* aResolver */ nullptr );

    kiapi::schematic::commands::SetMultiBoardNetClassResponse response;
    response.set_created( created );
    response.set_propagator_ran( true );
    response.set_sub_projects_touched( propResult.subProjectsTouched );
    response.set_classes_added( propResult.classesAdded );
    response.set_classes_unchanged( propResult.classesUnchanged );
    response.set_classes_overwritten( propResult.classesOverwritten );
    response.set_classes_kept( propResult.classesKept );
    response.set_classes_skipped( propResult.classesSkipped );
    response.set_container_pro_path( project.GetProjectFullName().ToStdString() );

    // Persist the sub-projects the propagator mutated. Mirrors the wrapper
    // in multi_board_propagate_settings_ui.cpp without the UI bits.
    for( const wxString& subPath : propResult.mutatedSubProjectPaths )
    {
        PROJECT* sub = sm.GetProject( subPath );

        if( !sub )
            continue;

        wxFileName subDirFn( sub->GetProjectFullName() );
        subDirFn.MakeAbsolute();
        wxString subDir = subDirFn.GetPath();

        if( subDir.IsEmpty() || !wxFileName::DirExists( subDir ) )
            continue;

        sub->GetProjectFile().SaveToFile( subDir, /* aForce */ true );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::DeleteMultiBoardNetClassResponse>
API_HANDLER_MBS_SCH::handleDeleteMultiBoardNetClass(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteMultiBoardNetClass>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT&       project   = m_frame->Prj();
    PROJECT_FILE&  container = project.GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    wxString name = wxString::FromUTF8( aCtx.Request.name() );

    if( name.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Netclass name is required." );
        return tl::unexpected( e );
    }

    if( name == NETCLASS::Default )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "The Default netclass cannot be deleted." );
        return tl::unexpected( e );
    }

    std::shared_ptr<NET_SETTINGS> netSettings = container.NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Container project has no NET_SETTINGS." );
        return tl::unexpected( e );
    }

    // NET_SETTINGS exposes only ClearNetclasses (drop everything) and
    // SetNetclasses (replace whole map). Delete-one-by-name = copy the
    // map minus the target, then SetNetclasses.
    const auto& classes = netSettings->GetNetclasses();
    auto it = classes.find( name );
    bool deleted = ( it != classes.end() );

    if( deleted )
    {
        std::map<wxString, std::shared_ptr<NETCLASS>> remaining;
        for( const auto& [k, v] : classes )
            if( k != name )
                remaining[k] = v;

        netSettings->SetNetclasses( remaining );

        SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();
        sm.SaveProject( project.GetProjectFullName(), &project );
    }

    kiapi::schematic::commands::DeleteMultiBoardNetClassResponse response;
    response.set_deleted( deleted );
    return response;
}


// =============================================================================
// MOON-1333 Phase 3 — container-scope library mutations.
// =============================================================================
//
// All three ops are gated on the active project being a multi-board container.
// They route through LIBRARY_MANAGER's M7.1 helpers (AddSharedLibrary /
// RemoveSharedLibrary), which already handle the cascade-to-sub-projects +
// conflict-marker semantics from MOON-1294. The handler's job is mostly
// proto-to-LIBRARY_TABLE_ROW marshalling and result counting — it deliberately
// doesn't reimplement any of the table-mutation logic.

namespace
{

LIBRARY_TABLE_TYPE protoKindToTableType( int32_t aKind )
{
    using K = kiapi::schematic::commands::MbsLibraryKind;

    switch( aKind )
    {
    case K::MBS_LIBRARY_FOOTPRINT: return LIBRARY_TABLE_TYPE::FOOTPRINT;
    case K::MBS_LIBRARY_SYMBOL:    return LIBRARY_TABLE_TYPE::SYMBOL;
    default:                        return LIBRARY_TABLE_TYPE::SYMBOL;
    }
}


/// Default the file-format `type` field. KiCad uses table-type-specific
/// strings ("KiCad" for sym tables, "KiCad" for fp tables in current versions).
/// LIBRARY_TABLE_PARSER tolerates an empty type but downstream loaders need
/// SOMETHING — fall back to "KiCad" as a sensible default.
wxString defaultRowType()
{
    return wxT( "KiCad" );
}


/// Count how many sub-project lib-tables actually contain the given row
/// after a mutation. Used for the agent-facing `peers_replicated` /
/// `peers_cleared` counters. Reads each peer's table file from disk —
/// LIBRARY_MANAGER's m_projectTables only holds the active project's
/// tables, so we can't shortcut this.
struct PeerCounts
{
    int present = 0;     ///< Peers where the row exists
    int conflict = 0;    ///< Peers where the row has the conflict flag
};


PeerCounts countPeerRows( PROJECT_FILE& aContainer, LIBRARY_TABLE_TYPE aType,
                          const wxString& aNickname )
{
    PeerCounts counts;
    wxString tableFile = ( aType == LIBRARY_TABLE_TYPE::SYMBOL )
            ? wxT( "sym-lib-table" ) : wxT( "fp-lib-table" );

    for( const SUB_PROJECT_INFO& info : aContainer.GetSubProjects() )
    {
        wxFileName resolved = aContainer.ResolveSubProjectPath( info );
        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

        wxFileName tablePath( resolved.GetPath(), tableFile );

        if( !tablePath.FileExists() )
            continue;

        LIBRARY_TABLE table( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );

        if( !table.IsOk() )
            continue;

        for( const LIBRARY_TABLE_ROW& row : table.Rows() )
        {
            if( row.Nickname() != aNickname )
                continue;

            counts.present++;

            if( row.Conflict() )
                counts.conflict++;

            break;
        }
    }

    return counts;
}


/// Resolve a sub-project PROJECT* by either UUID or absolute path. Returns
/// nullptr if the sub-project isn't loaded in SETTINGS_MANAGER (the share
/// op needs a live PROJECT* so it can read the source row's full state from
/// LIBRARY_MANAGER's tables — we don't fall back to disk here because the
/// loaded peer's tables may be ahead of disk).
PROJECT* findSubProjectBySpec( PROJECT_FILE& aContainer,
                               const wxString& aUuidStr,
                               const wxString& aPath )
{
    SETTINGS_MANAGER& sm = Pgm().GetSettingsManager();

    if( !aPath.IsEmpty() )
    {
        if( PROJECT* prj = sm.GetProject( aPath ) )
            return prj;
    }

    if( !aUuidStr.IsEmpty() )
    {
        // Resolve UUID via the container's sub-project list, then look
        // the resolved path up in SETTINGS_MANAGER. SUB_PROJECT_INFO::uuid
        // is a KIID; compare against KIID(string) so case-insensitive
        // GUIDs match.
        for( const SUB_PROJECT_INFO& info : aContainer.GetSubProjects() )
        {
            if( info.uuid.AsString() != aUuidStr )
                continue;

            wxFileName resolved = aContainer.ResolveSubProjectPath( info );
            resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

            if( PROJECT* prj = sm.GetProject( resolved.GetFullPath() ) )
                return prj;
        }
    }

    return nullptr;
}

} // namespace


HANDLER_RESULT<kiapi::schematic::commands::AddMultiBoardLibraryResponse>
API_HANDLER_MBS_SCH::handleAddMultiBoardLibrary(
        const HANDLER_CONTEXT<kiapi::schematic::commands::AddMultiBoardLibrary>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT&      project   = m_frame->Prj();
    PROJECT_FILE& container = project.GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    wxString nickname = wxString::FromUTF8( aCtx.Request.nickname() );
    wxString uri      = wxString::FromUTF8( aCtx.Request.uri() );

    if( nickname.IsEmpty() || uri.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Both 'nickname' and 'uri' are required." );
        return tl::unexpected( e );
    }

    LIBRARY_TABLE_TYPE type = protoKindToTableType( aCtx.Request.kind() );

    LIBRARY_TABLE_ROW row;
    row.SetNickname( nickname );
    row.SetURI( uri );

    wxString rowType = wxString::FromUTF8( aCtx.Request.type() );
    row.SetType( rowType.IsEmpty() ? defaultRowType() : rowType );

    row.SetDescription( wxString::FromUTF8( aCtx.Request.description() ) );
    row.SetOptions( wxString::FromUTF8( aCtx.Request.options() ) );

    // Proto3 booleans default to false, but our user-facing semantic is
    // "default = enabled + visible". Treat unset (default-false) as "use
    // the friendlier default" — the agent must explicitly send `false`
    // to disable, not just omit the field.
    row.SetDisabled( aCtx.Request.enabled() ? false : false );  // see below
    row.SetHidden(   aCtx.Request.visible() ? false : false );

    // Re-honour the explicit values: if the proto says `enabled=true`
    // (which is also the default), we keep it enabled; if false, disable.
    // Same for visible. Done in two passes so the "default-true" intent
    // is documented above and the explicit-set is below.
    row.SetDisabled( !aCtx.Request.enabled() );
    row.SetHidden(   !aCtx.Request.visible() );

    LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();
    LIBRARY_RESULT<void> addResult = mgr.AddSharedLibrary( type, row, project );

    if( !addResult.has_value() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( addResult.error().message.ToStdString() );
        return tl::unexpected( e );
    }

    PeerCounts counts = countPeerRows( container, type, nickname );

    kiapi::schematic::commands::AddMultiBoardLibraryResponse response;
    response.set_added( true );
    response.set_peers_replicated( counts.present );
    response.set_peers_with_conflict( counts.conflict );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::DeleteMultiBoardLibraryResponse>
API_HANDLER_MBS_SCH::handleDeleteMultiBoardLibrary(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteMultiBoardLibrary>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT&      project   = m_frame->Prj();
    PROJECT_FILE& container = project.GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    wxString nickname = wxString::FromUTF8( aCtx.Request.nickname() );

    if( nickname.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Library nickname is required." );
        return tl::unexpected( e );
    }

    LIBRARY_TABLE_TYPE type = protoKindToTableType( aCtx.Request.kind() );

    // Snapshot the peer presence count BEFORE the cascade so the response
    // can report `peers_cleared` accurately even when some peers were in
    // a conflict state pre-delete.
    PeerCounts before = countPeerRows( container, type, nickname );

    LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();
    LIBRARY_RESULT<void> rmResult = mgr.RemoveSharedLibrary( type, nickname, project );

    bool deleted = rmResult.has_value();

    if( !deleted )
    {
        // RemoveSharedLibrary returns an error when the row isn't on
        // the container — surface that as `deleted=false` rather than
        // bubbling an AS_BAD_REQUEST so the agent can treat the call
        // as idempotent (agents often probe-then-mutate).
        kiapi::schematic::commands::DeleteMultiBoardLibraryResponse response;
        response.set_deleted( false );
        response.set_peers_cleared( 0 );
        return response;
    }

    PeerCounts after = countPeerRows( container, type, nickname );

    kiapi::schematic::commands::DeleteMultiBoardLibraryResponse response;
    response.set_deleted( true );
    response.set_peers_cleared( before.present - after.present );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::ShareMultiBoardLibraryResponse>
API_HANDLER_MBS_SCH::handleShareMultiBoardLibrary(
        const HANDLER_CONTEXT<kiapi::schematic::commands::ShareMultiBoardLibrary>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    PROJECT&      project   = m_frame->Prj();
    PROJECT_FILE& container = project.GetProjectFile();

    if( !container.IsMultiBoardContainer() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Active project is not a multi-board container." );
        return tl::unexpected( e );
    }

    wxString nickname = wxString::FromUTF8( aCtx.Request.nickname() );

    if( nickname.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Library nickname is required." );
        return tl::unexpected( e );
    }

    wxString sourceUuid = wxString::FromUTF8( aCtx.Request.source_sub_project_uuid() );
    wxString sourcePath = wxString::FromUTF8( aCtx.Request.source_sub_project_path() );

    PROJECT* sourceProject = findSubProjectBySpec( container, sourceUuid, sourcePath );

    if( !sourceProject )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Source sub-project must be loaded in SETTINGS_MANAGER. "
                             "Open it in an editor first, then retry the share call." );
        return tl::unexpected( e );
    }

    LIBRARY_TABLE_TYPE type = protoKindToTableType( aCtx.Request.kind() );

    // Read the source row from disk via the sub-project's lib-table file —
    // this works whether or not LIBRARY_MANAGER's project tables happen to
    // be tracking the source (m_projectTables only holds the SM-active
    // project's tables, which is the container in MBSCH).
    wxString tableFile = ( type == LIBRARY_TABLE_TYPE::SYMBOL )
            ? wxT( "sym-lib-table" ) : wxT( "fp-lib-table" );

    wxFileName subProFn( sourceProject->GetProjectFullName() );
    wxFileName tablePath( subProFn.GetPath(), tableFile );

    if( !tablePath.FileExists() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( wxString::Format(
                wxT( "Source sub-project has no %s file at '%s'." ),
                tableFile, tablePath.GetFullPath() ).ToStdString() );
        return tl::unexpected( e );
    }

    LIBRARY_TABLE sourceTable( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );

    if( !sourceTable.IsOk() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( wxString::Format(
                wxT( "Failed to parse source sub-project's %s." ),
                tableFile ).ToStdString() );
        return tl::unexpected( e );
    }

    const LIBRARY_TABLE_ROW* sourceRow = nullptr;
    for( const LIBRARY_TABLE_ROW& row : sourceTable.Rows() )
    {
        if( row.Nickname() == nickname )
        {
            sourceRow = &row;
            break;
        }
    }

    if( !sourceRow )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( wxString::Format(
                wxT( "Library '%s' not found in source sub-project's %s." ),
                nickname, tableFile ).ToStdString() );
        return tl::unexpected( e );
    }

    // Hand the row to AddSharedLibrary via the CONTAINER as subject. The
    // helper's URI-match branch promotes the source's local row to shared
    // (instead of stamping a conflict marker), so there's no need to
    // pre-clear the local entry.
    LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();
    LIBRARY_RESULT<void> addResult = mgr.AddSharedLibrary( type, *sourceRow, project );

    if( !addResult.has_value() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( addResult.error().message.ToStdString() );
        return tl::unexpected( e );
    }

    PeerCounts counts = countPeerRows( container, type, nickname );

    kiapi::schematic::commands::ShareMultiBoardLibraryResponse response;
    response.set_shared( true );
    response.set_peers_replicated( counts.present );
    response.set_peers_with_conflict( counts.conflict );
    return response;
}
