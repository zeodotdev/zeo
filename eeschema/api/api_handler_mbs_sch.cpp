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
#include <project/project_file.h>
#include <project.h>
#include <schematic.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>

#include "../sch_module_block.h"
#include "../sch_module_pin.h"

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
}


bool API_HANDLER_MBS_SCH::validateDocumentInternal(
        const DocumentSpecifier& aDocument ) const
{
    return aDocument.type() == DocumentType::DOCTYPE_MBS_SCHEMATIC;
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

    GetOpenDocumentsResponse         response;
    kiapi::common::types::DocumentSpecifier doc;
    doc.set_type( DocumentType::DOCTYPE_MBS_SCHEMATIC );

    // MBS schematics are flat — no sub-sheets — but we still emit a SheetPath
    // with the root sheet UUID so the wire format is symmetric with regular
    // schematic responses (kipy.Schematic doesn't care about the contents).
    kiapi::common::types::SheetPath* sheetPath = doc.mutable_sheet_path();
    SCH_SHEET_PATH                   currentPath = m_frame->GetCurrentSheet();
    sheetPath->set_path_human_readable(
            currentPath.PathHumanReadable().ToStdString() );

    for( size_t i = 0; i < currentPath.size(); ++i )
        sheetPath->add_path()->set_value( currentPath.at( i )->m_Uuid.AsStdString() );

    // Project metadata so kipy can resolve relative paths.
    PROJECT* prj = &m_frame->Prj();

    if( prj )
    {
        kiapi::common::types::ProjectSpecifier* projSpec = doc.mutable_project();
        projSpec->set_name( prj->GetProjectName().ToStdString() );
        projSpec->set_path( prj->GetProjectPath().ToStdString() );
    }

    *response.add_documents() = doc;
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
