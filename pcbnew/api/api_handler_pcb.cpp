/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Jon Evans <jon@craftyjon.com>
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

#include <magic_enum.hpp>
#include <memory>
#include <atomic>
#include <properties/property.h>

#include <common.h>
#include <api/api_handler_pcb.h>
#include <api/api_pcb_utils.h>
#include <api/api_enums.h>
#include <api/api_utils.h>
#include <diff_manager.h>
#include <gal/graphics_abstraction_layer.h>
#include <pcb_draw_panel_gal.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <kicad_clipboard.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_group.h>
#include <pcb_reference_image.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <pcbnew_id.h>
#include <pcb_marker.h>
#include <drc/drc_item.h>
#include <layer_ids.h>
#include <project.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <tools/drc_tool.h>
#include <drc/drc_engine.h>
#include <zone.h>
#include <zone_settings.h>
#include <pcb_dimension.h>
#include <teardrop/teardrop_parameters.h>
#include <router/pns_meander.h>
#include <project/tuning_profiles.h>
#include <project/project_file.h>
#include <project/component_class_settings.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/board_netlist_updater.h>
#include <kiface_base.h>
#include <mail_type.h>
#include <richio.h>
#include <ki_exception.h>
#include <wx/regex.h>
#include <lib_id.h>
#include <cli_progress_reporter.h>
#include <connectivity/connectivity_data.h>
#include <ratsnest/ratsnest_data.h>
#include <connectivity/connectivity_items.h>
#include <board_connected_item.h>
#include <project_pcb.h>
#include <footprint_library_adapter.h>
#include <footprint_info.h>
#include <pgm_base.h>

#include <api/common/types/base_types.pb.h>
#include <widgets/appearance_controls.h>
#include <wx/wfstream.h>
#include <wx/textfile.h>
#include <wx/file.h>
#include <widgets/report_severity.h>

using namespace kiapi::common::commands;
using types::CommandStatus;
using types::DocumentType;
using types::ItemRequestStatus;


API_HANDLER_PCB::API_HANDLER_PCB( PCB_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    wxLogMessage( "API_HANDLER_PCB: Registering handlers..." );

    registerHandler<RunAction, RunActionResponse>( &API_HANDLER_PCB::handleRunAction );
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_PCB::handleGetOpenDocuments );
    registerHandler<SaveDocument, Empty>( &API_HANDLER_PCB::handleSaveDocument );
    registerHandler<SaveCopyOfDocument, Empty>( &API_HANDLER_PCB::handleSaveCopyOfDocument );
    registerHandler<RevertDocument, Empty>( &API_HANDLER_PCB::handleRevertDocument );

    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_PCB::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_PCB::handleGetItemsById );

    registerHandler<GetSelection, SelectionResponse>( &API_HANDLER_PCB::handleGetSelection );
    registerHandler<ClearSelection, Empty>( &API_HANDLER_PCB::handleClearSelection );
    registerHandler<AddToSelection, SelectionResponse>( &API_HANDLER_PCB::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>(
            &API_HANDLER_PCB::handleRemoveFromSelection );

    registerHandler<GetBoardStackup, BoardStackupResponse>( &API_HANDLER_PCB::handleGetStackup );
    registerHandler<GetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleGetBoardEnabledLayers );
    registerHandler<SetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleSetBoardEnabledLayers );
    registerHandler<GetBoardLayersInfo, BoardLayersInfoResponse>(
        &API_HANDLER_PCB::handleGetBoardLayersInfo );
    registerHandler<SetLayerName, Empty>( &API_HANDLER_PCB::handleSetLayerName );
    registerHandler<SetLayerType, Empty>( &API_HANDLER_PCB::handleSetLayerType );
    registerHandler<GetGraphicsDefaults, GraphicsDefaultsResponse>(
            &API_HANDLER_PCB::handleGetGraphicsDefaults );
    registerHandler<GetBoundingBox, GetBoundingBoxResponse>(
            &API_HANDLER_PCB::handleGetBoundingBox );
    registerHandler<GetPadShapeAsPolygon, PadShapeAsPolygonResponse>(
            &API_HANDLER_PCB::handleGetPadShapeAsPolygon );
    registerHandler<CheckPadstackPresenceOnLayers, PadstackPresenceResponse>(
            &API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers );
    // Use fully qualified names to ensure correct proto types are registered
    registerHandler<kiapi::common::commands::GetTitleBlockInfo, kiapi::common::types::TitleBlockInfo>(
            &API_HANDLER_PCB::handleGetTitleBlockInfo );
    registerHandler<kiapi::common::commands::SetTitleBlockInfo, Empty>(
            &API_HANDLER_PCB::handleSetTitleBlockInfo );
    wxLogMessage( "API_HANDLER_PCB: Registered TitleBlock handlers. Type name: %s",
                  std::string( kiapi::common::commands::GetTitleBlockInfo().GetTypeName() ).c_str() );

    registerHandler<ExpandTextVariables, ExpandTextVariablesResponse>(
            &API_HANDLER_PCB::handleExpandTextVariables );
    registerHandler<kiapi::board::commands::GetBoardOrigin, kiapi::common::types::Vector2>(
            &API_HANDLER_PCB::handleGetBoardOrigin );
    registerHandler<kiapi::board::commands::SetBoardOrigin, Empty>(
            &API_HANDLER_PCB::handleSetBoardOrigin );
    wxLogMessage( "API_HANDLER_PCB: Registered BoardOrigin handlers. Type name: %s",
                  std::string( kiapi::board::commands::GetBoardOrigin().GetTypeName() ).c_str() );

    registerHandler<InteractiveMoveItems, Empty>( &API_HANDLER_PCB::handleInteractiveMoveItems );
    registerHandler<ShowDiffOverlay, Empty>( &API_HANDLER_PCB::handleShowDiffOverlay );
    registerHandler<GetNets, NetsResponse>( &API_HANDLER_PCB::handleGetNets );
    registerHandler<GetNetClassForNets, NetClassForNetsResponse>(
            &API_HANDLER_PCB::handleGetNetClassForNets );
    registerHandler<RefillZones, Empty>( &API_HANDLER_PCB::handleRefillZones );

    registerHandler<SaveDocumentToString, SavedDocumentResponse>(
            &API_HANDLER_PCB::handleSaveDocumentToString );
    registerHandler<SaveSelectionToString, SavedSelectionResponse>(
            &API_HANDLER_PCB::handleSaveSelectionToString );
    registerHandler<ParseAndCreateItemsFromString, CreateItemsResponse>(
            &API_HANDLER_PCB::handleParseAndCreateItemsFromString );
    registerHandler<GetVisibleLayers, BoardLayers>( &API_HANDLER_PCB::handleGetVisibleLayers );
    registerHandler<SetVisibleLayers, Empty>( &API_HANDLER_PCB::handleSetVisibleLayers );
    registerHandler<GetActiveLayer, BoardLayerResponse>( &API_HANDLER_PCB::handleGetActiveLayer );
    registerHandler<SetActiveLayer, Empty>( &API_HANDLER_PCB::handleSetActiveLayer );
    registerHandler<GetBoardEditorAppearanceSettings, BoardEditorAppearanceSettings>(
            &API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings );
    registerHandler<SetBoardEditorAppearanceSettings, Empty>(
            &API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings );
    registerHandler<InjectDrcError, InjectDrcErrorResponse>(
            &API_HANDLER_PCB::handleInjectDrcError );

    // Board stackup update
    registerHandler<UpdateBoardStackup, BoardStackupResponse>(
            &API_HANDLER_PCB::handleUpdateBoardStackup );

    // DRC handlers
    registerHandler<RunDRC, RunDRCResponse>( &API_HANDLER_PCB::handleRunDRC );
    registerHandler<GetDRCViolations, DRCViolationsResponse>(
            &API_HANDLER_PCB::handleGetDRCViolations );
    registerHandler<ClearDRCMarkers, Empty>( &API_HANDLER_PCB::handleClearDRCMarkers );

    // Design rules handlers
    registerHandler<GetDesignRules, DesignRulesResponse>( &API_HANDLER_PCB::handleGetDesignRules );
    registerHandler<SetDesignRules, DesignRulesResponse>( &API_HANDLER_PCB::handleSetDesignRules );

    // DRC settings handlers
    registerHandler<GetDRCSettings, DRCSettingsResponse>( &API_HANDLER_PCB::handleGetDRCSettings );
    registerHandler<SetDRCSettings, Empty>( &API_HANDLER_PCB::handleSetDRCSettings );

    // Grid settings handlers
    registerHandler<GetPCBGridSettings, PCBGridSettingsResponse>(
            &API_HANDLER_PCB::handleGetPCBGridSettings );
    registerHandler<SetPCBGridSettings, Empty>( &API_HANDLER_PCB::handleSetPCBGridSettings );

    // Graphics defaults setter
    registerHandler<SetGraphicsDefaults, GraphicsDefaultsResponse>(
            &API_HANDLER_PCB::handleSetGraphicsDefaults );

    // Update PCB from Schematic
    registerHandler<UpdatePCBFromSchematic, UpdatePCBFromSchematicResponse>(
            &API_HANDLER_PCB::handleUpdatePCBFromSchematic );

    // Footprint library browsing
    registerHandler<GetLibraryFootprints, GetLibraryFootprintsResponse>(
            &API_HANDLER_PCB::handleGetLibraryFootprints );
    registerHandler<SearchLibraryFootprints, SearchLibraryFootprintsResponse>(
            &API_HANDLER_PCB::handleSearchLibraryFootprints );
    registerHandler<GetFootprintInfo, GetFootprintInfoResponse>(
            &API_HANDLER_PCB::handleGetFootprintInfo );

    // Ratsnest query
    registerHandler<GetRatsnest, GetRatsnestResponse>( &API_HANDLER_PCB::handleGetRatsnest );
    registerHandler<GetUnroutedNets, GetUnroutedNetsResponse>(
            &API_HANDLER_PCB::handleGetUnroutedNets );
    registerHandler<GetConnectivityStatus, GetConnectivityStatusResponse>(
            &API_HANDLER_PCB::handleGetConnectivityStatus );

    // Group operations
    registerHandler<GetGroups, GetGroupsResponse>( &API_HANDLER_PCB::handleGetGroups );
    registerHandler<CreateGroup, CreateGroupResponse>( &API_HANDLER_PCB::handleCreateGroup );
    registerHandler<DeleteGroup, DeleteGroupResponse>( &API_HANDLER_PCB::handleDeleteGroup );
    registerHandler<AddToGroup, AddToGroupResponse>( &API_HANDLER_PCB::handleAddToGroup );
    registerHandler<RemoveFromGroup, RemoveFromGroupResponse>(
            &API_HANDLER_PCB::handleRemoveFromGroup );
    registerHandler<GetGroupMembers, GetGroupMembersResponse>(
            &API_HANDLER_PCB::handleGetGroupMembers );

    // Zone hatch offsets handlers
    registerHandler<GetZoneHatchOffsets, ZoneHatchOffsetsResponse>(
            &API_HANDLER_PCB::handleGetZoneHatchOffsets );
    registerHandler<SetZoneHatchOffsets, ZoneHatchOffsetsResponse>(
            &API_HANDLER_PCB::handleSetZoneHatchOffsets );

    // Dimension defaults handlers
    registerHandler<GetDimensionDefaults, DimensionDefaultsResponse>(
            &API_HANDLER_PCB::handleGetDimensionDefaults );
    registerHandler<SetDimensionDefaults, DimensionDefaultsResponse>(
            &API_HANDLER_PCB::handleSetDimensionDefaults );

    // Zone defaults handlers
    registerHandler<GetZoneDefaults, ZoneDefaultsResponse>(
            &API_HANDLER_PCB::handleGetZoneDefaults );
    registerHandler<SetZoneDefaults, ZoneDefaultsResponse>(
            &API_HANDLER_PCB::handleSetZoneDefaults );

    // Pre-defined sizes handlers
    registerHandler<GetPreDefinedSizes, PreDefinedSizesResponse>(
            &API_HANDLER_PCB::handleGetPreDefinedSizes );
    registerHandler<SetPreDefinedSizes, PreDefinedSizesResponse>(
            &API_HANDLER_PCB::handleSetPreDefinedSizes );

    // Teardrop settings handlers
    registerHandler<GetTeardropSettings, TeardropSettingsResponse>(
            &API_HANDLER_PCB::handleGetTeardropSettings );
    registerHandler<SetTeardropSettings, TeardropSettingsResponse>(
            &API_HANDLER_PCB::handleSetTeardropSettings );

    // Length-tuning pattern settings handlers
    registerHandler<GetLengthTuningPatternSettings, LengthTuningPatternSettingsResponse>(
            &API_HANDLER_PCB::handleGetLengthTuningPatternSettings );
    registerHandler<SetLengthTuningPatternSettings, LengthTuningPatternSettingsResponse>(
            &API_HANDLER_PCB::handleSetLengthTuningPatternSettings );

    // Tuning profiles handlers
    registerHandler<GetTuningProfiles, TuningProfilesResponse>(
            &API_HANDLER_PCB::handleGetTuningProfiles );
    registerHandler<SetTuningProfiles, TuningProfilesResponse>(
            &API_HANDLER_PCB::handleSetTuningProfiles );

    // Component class settings handlers
    registerHandler<GetComponentClassSettings, ComponentClassSettingsResponse>(
            &API_HANDLER_PCB::handleGetComponentClassSettings );
    registerHandler<SetComponentClassSettings, ComponentClassSettingsResponse>(
            &API_HANDLER_PCB::handleSetComponentClassSettings );

    // Custom DRC rules handlers
    registerHandler<GetCustomRules, CustomRulesResponse>(
            &API_HANDLER_PCB::handleGetCustomRules );
    registerHandler<SetCustomRules, CustomRulesResponse>(
            &API_HANDLER_PCB::handleSetCustomRules );

    // Document management handlers
    registerHandler<CreateDocument, CreateDocumentResponse>( &API_HANDLER_PCB::handleCreateDocument );
    registerHandler<OpenDocument, OpenDocumentResponse>( &API_HANDLER_PCB::handleOpenDocument );
    registerHandler<CloseDocument, Empty>( &API_HANDLER_PCB::handleCloseDocument );

    wxLogMessage( "API_HANDLER_PCB: Constructor complete. Total handlers registered: %zu", m_handlers.size() );
}


PCB_EDIT_FRAME* API_HANDLER_PCB::frame() const
{
    return static_cast<PCB_EDIT_FRAME*>( m_frame );
}


HANDLER_RESULT<RunActionResponse> API_HANDLER_PCB::handleRunAction(
        const HANDLER_CONTEXT<RunAction>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    RunActionResponse response;

    if( frame()->GetToolManager()->RunAction( aCtx.Request.action(), true ) )
        response.set_status( RunActionStatus::RAS_OK );
    else
        response.set_status( RunActionStatus::RAS_INVALID );

    return response;
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_PCB::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_PCB )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( frame()->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_PCB );
    doc.set_board_filename( fn.GetFullName() );

    doc.mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
    doc.mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );

    response.mutable_documents()->Add( std::move( doc ) );
    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSaveDocument(
        const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    frame()->SaveBoard();
    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSaveCopyOfDocument(
        const HANDLER_CONTEXT<SaveCopyOfDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxFileName boardPath( frame()->Prj().AbsolutePath( wxString::FromUTF8( aCtx.Request.path() ) ) );

    if( !boardPath.IsOk() || !boardPath.IsDirWritable() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' could not be opened",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( boardPath.FileExists()
        && ( !boardPath.IsFileWritable() || !aCtx.Request.options().overwrite() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' exists and cannot be overwritten",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( boardPath.GetExt() != FILEEXT::KiCadPcbFileExtension )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' must have a kicad_pcb extension",
                                          boardPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();

    if( board->GetFileName().Matches( boardPath.GetFullPath() ) )
    {
        frame()->SaveBoard();
        return Empty();
    }

    bool includeProject = true;

    if( aCtx.Request.has_options() )
        includeProject = aCtx.Request.options().include_project();

    frame()->SavePcbCopy( boardPath.GetFullPath(), includeProject, /* aHeadless = */ true );

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRevertDocument(
        const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxFileName fn = frame()->Prj().AbsolutePath( frame()->GetBoard()->GetFileName() );

    frame()->GetScreen()->SetContentModified( false );
    frame()->ReleaseFile();
    frame()->OpenProjectFiles( std::vector<wxString>( 1, fn.GetFullPath() ), KICTL_REVERT );

    return Empty();
}


void API_HANDLER_PCB::pushCurrentCommit( const std::string& aClientName, const wxString& aMessage )
{
    API_HANDLER_EDITOR::pushCurrentCommit( aClientName, aMessage );

    // Note: We intentionally don't call Refresh() here because the commit->Push()
    // already triggers view updates via PostEvent(TA_MODEL_CHANGE) and OnModify().
    // Calling Refresh() during API handler execution (which happens during wxYield)
    // can cause issues on macOS with nested event processing.
}


std::unique_ptr<COMMIT> API_HANDLER_PCB::createCommit()
{
    wxASSERT( frame() != nullptr );

    if( !frame() )
        return nullptr;

    return std::make_unique<BOARD_COMMIT>( frame() );
}


std::optional<BOARD_ITEM*> API_HANDLER_PCB::getItemById( const KIID& aId ) const
{
    if( !frame() || !frame()->GetBoard() )
        return std::nullopt;

    BOARD_ITEM* item = frame()->GetBoard()->ResolveItem( aId, true );

    if( !item )
        return std::nullopt;

    return item;
}


bool API_HANDLER_PCB::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_PCB )
        return false;

    if( !frame() )
        return false;

    wxFileName fn( frame()->GetCurrentFileName() );
    return 0 == aDocument.board_filename().compare( fn.GetFullName() );
}


HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> API_HANDLER_PCB::createItemForType( KICAD_T aType,
        BOARD_ITEM_CONTAINER* aContainer )
{
    if( !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == PCB_PAD_T && !dynamic_cast<FOOTPRINT*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pad in {}, which is not a footprint",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == PCB_FOOTPRINT_T && !dynamic_cast<BOARD*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a footprint in {}, which is not a board",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }

    std::unique_ptr<BOARD_ITEM> created = CreateItemForType( aType, aContainer );

    if( !created )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create an item of type {}, which is unhandled",
                                          magic_enum::enum_name( aType ) ) );
        return tl::unexpected( e );
    }

    return created;
}


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_PCB::handleCreateUpdateItemsInternal( bool aCreate,
        const std::string& aClientName,
        const types::ItemHeader &aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )> aItemHandler )
{
    ApiResponseStatus e;

    auto containerResult = validateItemHeaderDocument( aHeader );

    if( !containerResult && containerResult.error().status() == ApiStatusCode::AS_UNHANDLED )
    {
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }
    else if( !containerResult )
    {
        e.CopyFrom( containerResult.error() );
        return tl::unexpected( e );
    }

    if( !frame() || !frame()->GetBoard() )
    {
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Frame or board is not available" );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();
    BOARD_ITEM_CONTAINER* container = board;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;
        std::optional<BOARD_ITEM*> optItem = getItemById( containerId );

        if( optItem )
        {
            container = dynamic_cast<BOARD_ITEM_CONTAINER*>( *optItem );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid board item container",
                        containerId.AsStdString() ) );
                return tl::unexpected( e );
            }
        }
        else
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format(
                    "The requested container {} does not exist in this document",
                    containerId.AsStdString() ) );
            return tl::unexpected( e );
        }
    }

    BOARD_COMMIT* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( aClientName ) );

    if( !commit )
    {
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to create commit - frame may be invalid" );
        return tl::unexpected( e );
    }

    for( const google::protobuf::Any& anyItem : aItems )
    {
        ItemStatus status;
        std::optional<KICAD_T> type = TypeNameFromAny( anyItem );

        if( !type )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( fmt::format( "Could not decode a valid type from {}",
                                                   anyItem.type_url() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        if( type == PCB_DIMENSION_T )
        {
            board::types::Dimension dimension;
            anyItem.UnpackTo( &dimension );

            switch( dimension.dimension_style_case() )
            {
            case board::types::Dimension::kAligned:    type = PCB_DIM_ALIGNED_T;    break;
            case board::types::Dimension::kOrthogonal: type = PCB_DIM_ORTHOGONAL_T; break;
            case board::types::Dimension::kRadial:     type = PCB_DIM_RADIAL_T;     break;
            case board::types::Dimension::kLeader:     type = PCB_DIM_LEADER_T;     break;
            case board::types::Dimension::kCenter:     type = PCB_DIM_CENTER_T;     break;
            case board::types::Dimension::DIMENSION_STYLE_NOT_SET: break;
            }
        }

        HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<BOARD_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        std::optional<BOARD_ITEM*> optItem = getItemById( item->m_Uuid );

        if( aCreate && optItem )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !optItem )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        if( aCreate && !( board->GetEnabledLayers() & item->GetLayerSet() ).any() )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA );
            status.set_error_message(
                "attempted to add item with no overlapping layers with the board" );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            if( item->Type() == PCB_FOOTPRINT_T )
            {
                // Ensure children have unique identifiers; in case the API client created this new
                // footprint by cloning an existing one and only changing the parent UUID.
                item->RunOnChildren(
                        []( BOARD_ITEM* aChild )
                        {
                            const_cast<KIID&>( aChild->m_Uuid ) = KIID();
                        },
                        RECURSE );
            }

            item->Serialize( newItem );
            commit->Add( item.release() );
        }
        else
        {
            BOARD_ITEM* boardItem = *optItem;

            // Footprints can't be modified by CopyFrom at the moment because the commit system
            // doesn't currently know what to do with a footprint that has had its children
            // replaced with other children; which results in things like the view not having its
            // cached geometry for footprint children updated when you move a footprint around.
            // And also, groups are special because they can contain any item type, so we
            // can't use CopyFrom on them either.
            if( boardItem->Type() == PCB_FOOTPRINT_T  || boardItem->Type() == PCB_GROUP_T )
            {
                // Save group membership before removal, since Remove() severs the relationship
                PCB_GROUP* parentGroup = dynamic_cast<PCB_GROUP*>( boardItem->GetParentGroup() );

                commit->Remove( boardItem );
                item->Serialize( newItem );

                BOARD_ITEM* newBoardItem = item.release();
                commit->Add( newBoardItem );

                // Restore group membership for the newly added item
                if( parentGroup )
                    parentGroup->AddItem( newBoardItem );
            }
            else
            {
                commit->Modify( boardItem );
                boardItem->CopyFrom( item.get() );
                boardItem->Serialize( newItem );
            }
        }

        aItemHandler( status, newItem );
    }

    if( !m_activeClients.count( aClientName ) )
    {
        pushCurrentCommit( aClientName, aCreate ? _( "Created items via API" )
                                                : _( "Modified items via API" ) );
    }


    return ItemRequestStatus::IRS_OK;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItems( const HANDLER_CONTEXT<GetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> items;
    std::set<KICAD_T> typesRequested, typesInserted;
    bool handledAnything = false;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type == TYPE_NOT_INIT )
            continue;

        typesRequested.emplace( type );

        if( typesInserted.count( type ) )
            continue;

        switch( type )
        {
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
            handledAnything = true;
            std::copy( board->Tracks().begin(), board->Tracks().end(),
                       std::back_inserter( items ) );
            typesInserted.insert( { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T } );
            break;

        case PCB_PAD_T:
        {
            handledAnything = true;

            for( FOOTPRINT* fp : board->Footprints() )
            {
                std::copy( fp->Pads().begin(), fp->Pads().end(),
                           std::back_inserter( items ) );
            }

            typesInserted.insert( PCB_PAD_T );
            break;
        }

        case PCB_FOOTPRINT_T:
        {
            handledAnything = true;

            std::copy( board->Footprints().begin(), board->Footprints().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_FOOTPRINT_T );
            break;
        }

        case PCB_SHAPE_T:
        case PCB_TEXT_T:
        case PCB_TEXTBOX_T:
        case PCB_BARCODE_T:
        case PCB_DIMENSION_T:
        {
            handledAnything = true;
            bool inserted = false;

            for( BOARD_ITEM* item : board->Drawings() )
            {
                if( item->Type() == type )
                {
                    items.emplace_back( item );
                    inserted = true;
                }
            }

            if( inserted )
                typesInserted.insert( type );

            break;
        }

        case PCB_ZONE_T:
        {
            handledAnything = true;

            std::copy( board->Zones().begin(), board->Zones().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_ZONE_T );
            break;
        }

        case PCB_GROUP_T:
        {
            handledAnything = true;

            std::copy( board->Groups().begin(), board->Groups().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_GROUP_T );
            break;
        }
        default:
            break;
        }
    }

    if( !handledAnything )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        if( !typesRequested.count( item->Type() ) )
            continue;

        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItemsById(
        const HANDLER_CONTEXT<GetItemsById>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    std::vector<BOARD_ITEM*> items;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            items.emplace_back( *item );
    }

    if( items.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested IDs were found or valid" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}

void API_HANDLER_PCB::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    if( !frame() || !frame()->GetBoard() )
        return;

    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> validatedItems;

    for( std::pair<const KIID, ItemDeletionStatus> pair : aItemsToDelete )
    {
        if( BOARD_ITEM* item = board->ResolveItem( pair.first, true ) )
        {
            validatedItems.push_back( item );
            aItemsToDelete[pair.first] = ItemDeletionStatus::IDS_OK;
        }

        // Note: we don't currently support locking items from API modification, but here is where
        // to add it in the future (and return IDS_IMMUTABLE)
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    if( !commit )
        return;

    for( BOARD_ITEM* item : validatedItems )
        commit->Remove( item );

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_PCB::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    return getItemById( aId );
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleGetSelection(
            const HANDLER_CONTEXT<GetSelection>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::set<KICAD_T> filter;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type == TYPE_NOT_INIT )
            continue;

        filter.insert( type );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
    {
        if( filter.empty() || filter.contains( item->Type() ) )
            item->Serialize( *response.add_items() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleClearSelection(
        const HANDLER_CONTEXT<ClearSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    mgr->RunAction( ACTIONS::selectionClear );

    // Note: The selection tool handles UI updates via events.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    return Empty();
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleAddToSelection(
        const HANDLER_CONTEXT<AddToSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toAdd;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toAdd.emplace_back( *item );
    }

    selectionTool->AddItemsToSel( &toAdd );

    // Note: The selection tool handles UI updates via events.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleRemoveFromSelection(
        const HANDLER_CONTEXT<RemoveFromSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toRemove;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toRemove.emplace_back( *item );
    }

    selectionTool->RemoveItemsFromSel( &toRemove );

    // Note: The selection tool handles UI updates via events.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<BoardStackupResponse> API_HANDLER_PCB::handleGetStackup(
        const HANDLER_CONTEXT<GetBoardStackup>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardStackupResponse  response;
    google::protobuf::Any any;

    frame()->GetBoard()->GetStackupOrDefault().Serialize( any );

    any.UnpackTo( response.mutable_stackup() );

    // User-settable layer names are not stored in BOARD_STACKUP at the moment
    for( board::BoardStackupLayer& layer : *response.mutable_stackup()->mutable_layers() )
    {
        if( layer.type() == board::BoardStackupLayerType::BSLT_DIELECTRIC )
            continue;

        PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( layer.layer() );
        wxCHECK2( id != UNDEFINED_LAYER, continue );

        layer.set_user_name( frame()->GetBoard()->GetLayerName( id ) );
    }

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleGetBoardEnabledLayers(
        const HANDLER_CONTEXT<GetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardEnabledLayersResponse response;

    BOARD* board = frame()->GetBoard();
    int copperLayerCount = board->GetCopperLayerCount();

    response.set_copper_layer_count( copperLayerCount );

    LSET enabled = board->GetEnabledLayers();

    // The Rescue layer is an internal detail and should be hidden from the API
    enabled.reset( Rescue );

    // Just in case this is out of sync; the API should always return the expected copper layers
    enabled |= LSET::AllCuMask( copperLayerCount );

    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleSetBoardEnabledLayers(
        const HANDLER_CONTEXT<SetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.copper_layer_count() % 2 != 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "copper_layer_count must be an even number" );
        return tl::unexpected( e );
    }

    if( aCtx.Request.copper_layer_count() > MAX_CU_LAYERS )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "copper_layer_count must be below %d", MAX_CU_LAYERS ) );
        return tl::unexpected( e );
    }

    int copperLayerCount = static_cast<int>( aCtx.Request.copper_layer_count() );
    LSET enabled = board::UnpackLayerSet( aCtx.Request.layers() );

    // Sanitize the input
    enabled |= LSET( { Edge_Cuts, Margin, F_CrtYd, B_CrtYd } );
    enabled &= ~LSET::AllCuMask();
    enabled |= LSET::AllCuMask( copperLayerCount );

    BOARD* board = frame()->GetBoard();

    LSET previousEnabled = board->GetEnabledLayers();
    LSET changedLayers = enabled ^ previousEnabled;

    board->SetEnabledLayers( enabled );
    board->SetVisibleLayers( board->GetVisibleLayers() | changedLayers );

    LSEQ removedLayers;

    for( PCB_LAYER_ID layer_id : previousEnabled )
    {
        if( !enabled[layer_id] && board->HasItemsOnLayer( layer_id ) )
            removedLayers.push_back( layer_id );
    }

    bool modified = false;

    if( !removedLayers.empty() )
    {
        m_frame->GetToolManager()->RunAction( PCB_ACTIONS::selectionClear );

        for( PCB_LAYER_ID layer_id : removedLayers )
            modified |= board->RemoveAllItemsOnLayer( layer_id );
    }

    if( enabled != previousEnabled )
        frame()->UpdateUserInterface();

    if( modified )
        frame()->OnModify();

    BoardEnabledLayersResponse response;

    response.set_copper_layer_count( copperLayerCount );
    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


namespace
{

BoardLayerType ToProtoBoardLayerType( LAYER_T aType )
{
    switch( aType )
    {
    case LT_UNDEFINED: return BoardLayerType::BLT_UNDEFINED;
    case LT_SIGNAL:    return BoardLayerType::BLT_SIGNAL;
    case LT_POWER:     return BoardLayerType::BLT_POWER;
    case LT_MIXED:     return BoardLayerType::BLT_MIXED;
    case LT_JUMPER:    return BoardLayerType::BLT_JUMPER;
    case LT_AUX:       return BoardLayerType::BLT_AUX;
    case LT_FRONT:     return BoardLayerType::BLT_FRONT;
    case LT_BACK:      return BoardLayerType::BLT_BACK;
    default:           return BoardLayerType::BLT_UNDEFINED;
    }
}


LAYER_T FromProtoBoardLayerType( BoardLayerType aType )
{
    switch( aType )
    {
    case BoardLayerType::BLT_UNDEFINED: return LT_UNDEFINED;
    case BoardLayerType::BLT_SIGNAL:    return LT_SIGNAL;
    case BoardLayerType::BLT_POWER:     return LT_POWER;
    case BoardLayerType::BLT_MIXED:     return LT_MIXED;
    case BoardLayerType::BLT_JUMPER:    return LT_JUMPER;
    case BoardLayerType::BLT_AUX:       return LT_AUX;
    case BoardLayerType::BLT_FRONT:     return LT_FRONT;
    case BoardLayerType::BLT_BACK:      return LT_BACK;
    default:                            return LT_UNDEFINED;
    }
}

} // anonymous namespace


HANDLER_RESULT<BoardLayersInfoResponse> API_HANDLER_PCB::handleGetBoardLayersInfo(
        const HANDLER_CONTEXT<GetBoardLayersInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayersInfoResponse response;

    BOARD* board = frame()->GetBoard();
    int copperLayerCount = board->GetCopperLayerCount();

    response.set_copper_layer_count( copperLayerCount );

    LSET enabled = board->GetEnabledLayers();
    LSET visible = board->GetVisibleLayers();

    // Remove Rescue layer from visible set
    enabled.reset( Rescue );

    for( PCB_LAYER_ID layer_id : enabled.Seq() )
    {
        BoardLayerInfo* layerInfo = response.add_layers();

        layerInfo->set_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( layer_id ) );
        layerInfo->set_name( board->GetStandardLayerName( layer_id ).ToStdString() );
        layerInfo->set_user_name( board->GetLayerName( layer_id ).ToStdString() );
        layerInfo->set_type( ToProtoBoardLayerType( board->GetLayerType( layer_id ) ) );
        layerInfo->set_enabled( true );
        layerInfo->set_visible( visible[layer_id] );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetLayerName(
        const HANDLER_CONTEXT<SetLayerName>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PCB_LAYER_ID layer_id = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    if( layer_id == UNDEFINED_LAYER )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Invalid layer specified" );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();

    if( !board->GetEnabledLayers()[layer_id] )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Layer is not enabled" );
        return tl::unexpected( e );
    }

    wxString newName = wxString::FromUTF8( aCtx.Request.name() );

    if( !board->SetLayerName( layer_id, newName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to set layer name" );
        return tl::unexpected( e );
    }

    frame()->UpdateUserInterface();
    frame()->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetLayerType(
        const HANDLER_CONTEXT<SetLayerType>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PCB_LAYER_ID layer_id = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    if( layer_id == UNDEFINED_LAYER )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Invalid layer specified" );
        return tl::unexpected( e );
    }

    // Layer type can only be set for copper layers
    if( !IsCopperLayer( layer_id ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Layer type can only be set for copper layers" );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();

    if( !board->GetEnabledLayers()[layer_id] )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Layer is not enabled" );
        return tl::unexpected( e );
    }

    LAYER_T newType = FromProtoBoardLayerType( aCtx.Request.type() );

    if( !board->SetLayerType( layer_id, newType ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to set layer type" );
        return tl::unexpected( e );
    }

    frame()->UpdateUserInterface();
    frame()->OnModify();

    return Empty();
}


HANDLER_RESULT<GraphicsDefaultsResponse> API_HANDLER_PCB::handleGetGraphicsDefaults(
        const HANDLER_CONTEXT<GetGraphicsDefaults>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    const BOARD_DESIGN_SETTINGS& bds = frame()->GetBoard()->GetDesignSettings();
    GraphicsDefaultsResponse response;

    // TODO: This should change to be an enum class
    constexpr std::array<kiapi::board::BoardLayerClass, LAYER_CLASS_COUNT> classOrder = {
        kiapi::board::BLC_SILKSCREEN,
        kiapi::board::BLC_COPPER,
        kiapi::board::BLC_EDGES,
        kiapi::board::BLC_COURTYARD,
        kiapi::board::BLC_FABRICATION,
        kiapi::board::BLC_OTHER
    };

    for( int i = 0; i < LAYER_CLASS_COUNT; ++i )
    {
        kiapi::board::BoardLayerGraphicsDefaults* l = response.mutable_defaults()->add_layers();

        l->set_layer( classOrder[i] );
        l->mutable_line_thickness()->set_value_nm( bds.m_LineThickness[i] );

        kiapi::common::types::TextAttributes* text = l->mutable_text();
        text->mutable_size()->set_x_nm( bds.m_TextSize[i].x );
        text->mutable_size()->set_y_nm( bds.m_TextSize[i].y );
        text->mutable_stroke_width()->set_value_nm( bds.m_TextThickness[i] );
        text->set_italic( bds.m_TextItalic[i] );
        text->set_keep_upright( bds.m_TextUpright[i] );
    }

    return response;
}


HANDLER_RESULT<types::Vector2> API_HANDLER_PCB::handleGetBoardOrigin(
        const HANDLER_CONTEXT<GetBoardOrigin>& aCtx )
{
    wxLogMessage( "API_HANDLER_PCB::handleGetBoardOrigin called" );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        wxLogMessage( "API_HANDLER_PCB::handleGetBoardOrigin - validateDocument failed" );
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin;
    const BOARD_DESIGN_SETTINGS& settings = frame()->GetBoard()->GetDesignSettings();

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
        origin = settings.GetGridOrigin();
        break;

    case BOT_DRILL:
        origin = settings.GetAuxOrigin();
        break;

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    types::Vector2 reply;
    PackVector2( reply, origin );
    return reply;
}

HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardOrigin(
        const HANDLER_CONTEXT<SetBoardOrigin>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin = UnpackVector2( aCtx.Request.origin() );

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                // gridSetOrigin takes ownership and frees this
                                VECTOR2D* dorigin = new VECTOR2D( origin );
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::gridSetOrigin, dorigin );
                                f->Refresh();
                            } );
        break;
    }

    case BOT_DRILL:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::drillSetOrigin, origin );
                                f->Refresh();
                            } );
        break;
    }

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    return Empty();
}


HANDLER_RESULT<GetBoundingBoxResponse> API_HANDLER_PCB::handleGetBoundingBox(
        const HANDLER_CONTEXT<GetBoundingBox>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetBoundingBoxResponse response;
    bool includeText = aCtx.Request.mode() == BoundingBoxMode::BBM_ITEM_AND_CHILD_TEXT;

    for( const types::KIID& idMsg : aCtx.Request.items() )
    {
        KIID id( idMsg.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        BOARD_ITEM* item = *optItem;
        BOX2I bbox;

        if( item->Type() == PCB_FOOTPRINT_T )
            bbox = static_cast<FOOTPRINT*>( item )->GetBoundingBox( includeText );
        else
            bbox = item->GetBoundingBox();

        response.add_items()->set_value( idMsg.value() );
        PackBox2( *response.add_boxes(), bbox );
    }

    return response;
}


HANDLER_RESULT<PadShapeAsPolygonResponse> API_HANDLER_PCB::handleGetPadShapeAsPolygon(
        const HANDLER_CONTEXT<GetPadShapeAsPolygon>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadShapeAsPolygonResponse response;
    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aCtx.Request.layer() );

    for( const types::KIID& padRequest : aCtx.Request.pads() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optPad = getItemById( id );

        if( !optPad || ( *optPad )->Type() != PCB_PAD_T )
            continue;

        response.add_pads()->set_value( padRequest.value() );

        PAD* pad = static_cast<PAD*>( *optPad );
        SHAPE_POLY_SET poly;
        pad->TransformShapeToPolygon( poly, pad->Padstack().EffectiveLayerFor( layer ), 0,
                                      pad->GetMaxError(), ERROR_INSIDE );

        types::PolygonWithHoles* polyMsg = response.mutable_polygons()->Add();
        PackPolyLine( *polyMsg->mutable_outline(), poly.COutline( 0 ) );
    }

    return response;
}


HANDLER_RESULT<PadstackPresenceResponse> API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers(
        const HANDLER_CONTEXT<CheckPadstackPresenceOnLayers>& aCtx )
{
    using board::types::BoardLayer;

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadstackPresenceResponse response;

    LSET layers;

    for( const int layer : aCtx.Request.layers() )
        layers.set( FromProtoEnum<PCB_LAYER_ID, BoardLayer>( static_cast<BoardLayer>( layer ) ) );

    for( const types::KIID& padRequest : aCtx.Request.items() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        switch( ( *optItem )->Type() )
        {
        case PCB_PAD_T:
        {
            PAD* pad = static_cast<PAD*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( pad->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( pad->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        case PCB_VIA_T:
        {
            PCB_VIA* via = static_cast<PCB_VIA*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( via->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( via->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        default:
            break;
        }
    }

    return response;
}


HANDLER_RESULT<types::TitleBlockInfo> API_HANDLER_PCB::handleGetTitleBlockInfo(
        const HANDLER_CONTEXT<GetTitleBlockInfo>& aCtx )
{
    wxLogMessage( "API_HANDLER_PCB::handleGetTitleBlockInfo called" );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
    {
        wxLogMessage( "API_HANDLER_PCB::handleGetTitleBlockInfo - validateDocument failed" );
        return tl::unexpected( documentValidation.error() );
    }

    BOARD* board = frame()->GetBoard();
    const TITLE_BLOCK& block = board->GetTitleBlock();

    types::TitleBlockInfo response;

    response.set_title( block.GetTitle().ToUTF8() );
    response.set_date( block.GetDate().ToUTF8() );
    response.set_revision( block.GetRevision().ToUTF8() );
    response.set_company( block.GetCompany().ToUTF8() );
    response.set_comment1( block.GetComment( 0 ).ToUTF8() );
    response.set_comment2( block.GetComment( 1 ).ToUTF8() );
    response.set_comment3( block.GetComment( 2 ).ToUTF8() );
    response.set_comment4( block.GetComment( 3 ).ToUTF8() );
    response.set_comment5( block.GetComment( 4 ).ToUTF8() );
    response.set_comment6( block.GetComment( 5 ).ToUTF8() );
    response.set_comment7( block.GetComment( 6 ).ToUTF8() );
    response.set_comment8( block.GetComment( 7 ).ToUTF8() );
    response.set_comment9( block.GetComment( 8 ).ToUTF8() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetTitleBlockInfo(
        const HANDLER_CONTEXT<SetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    BOARD* board = frame()->GetBoard();
    TITLE_BLOCK block = board->GetTitleBlock();

    const types::TitleBlockInfo& info = aCtx.Request.title_block();

    if( !info.title().empty() )
        block.SetTitle( wxString::FromUTF8( info.title() ) );
    if( !info.date().empty() )
        block.SetDate( wxString::FromUTF8( info.date() ) );
    if( !info.revision().empty() )
        block.SetRevision( wxString::FromUTF8( info.revision() ) );
    if( !info.company().empty() )
        block.SetCompany( wxString::FromUTF8( info.company() ) );
    if( !info.comment1().empty() )
        block.SetComment( 0, wxString::FromUTF8( info.comment1() ) );
    if( !info.comment2().empty() )
        block.SetComment( 1, wxString::FromUTF8( info.comment2() ) );
    if( !info.comment3().empty() )
        block.SetComment( 2, wxString::FromUTF8( info.comment3() ) );
    if( !info.comment4().empty() )
        block.SetComment( 3, wxString::FromUTF8( info.comment4() ) );
    if( !info.comment5().empty() )
        block.SetComment( 4, wxString::FromUTF8( info.comment5() ) );
    if( !info.comment6().empty() )
        block.SetComment( 5, wxString::FromUTF8( info.comment6() ) );
    if( !info.comment7().empty() )
        block.SetComment( 6, wxString::FromUTF8( info.comment7() ) );
    if( !info.comment8().empty() )
        block.SetComment( 7, wxString::FromUTF8( info.comment8() ) );
    if( !info.comment9().empty() )
        block.SetComment( 8, wxString::FromUTF8( info.comment9() ) );

    board->SetTitleBlock( block );
    frame()->OnModify();

    return Empty();
}


HANDLER_RESULT<ExpandTextVariablesResponse> API_HANDLER_PCB::handleExpandTextVariables(
    const HANDLER_CONTEXT<ExpandTextVariables>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    ExpandTextVariablesResponse reply;
    BOARD* board = frame()->GetBoard();

    std::function<bool( wxString* )> textResolver =
            [&]( wxString* token ) -> bool
            {
                // Handles m_board->GetTitleBlock() *and* m_board->GetProject()
                return board->ResolveTextVar( token, 0 );
            };

    for( const std::string& textMsg : aCtx.Request.text() )
    {
        wxString text = ExpandTextVars( wxString::FromUTF8( textMsg ), &textResolver );
        reply.add_text( text.ToUTF8() );
    }

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleInteractiveMoveItems(
        const HANDLER_CONTEXT<InteractiveMoveItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    std::vector<EDA_ITEM*> toSelect;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toSelect.emplace_back( static_cast<EDA_ITEM*>( *item ) );
    }

    if( toSelect.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "None of the given items exist on the board",
                                          aCtx.Request.board().board_filename() ) );
        return tl::unexpected( e );
    }

    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    selectionTool->GetSelection().SetReferencePoint( toSelect[0]->GetPosition() );

    mgr->RunAction( ACTIONS::selectionClear );
    mgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &toSelect );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    mgr->PostAPIAction( PCB_ACTIONS::move, commit );

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleShowDiffOverlay(
        const HANDLER_CONTEXT<ShowDiffOverlay>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOX2I bbox;

    // Check if bounding_box was provided and has non-zero size
    if( aCtx.Request.has_bounding_box() )
    {
        const auto& bb = aCtx.Request.bounding_box();
        bbox = BOX2I( VECTOR2I( bb.position().x_nm(), bb.position().y_nm() ),
                      VECTOR2I( bb.size().x_nm(), bb.size().y_nm() ) );
    }

    // If no bounding box provided or it has zero size, use board's bounding box
    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
    {
        BOARD* board = frame()->GetBoard();
        bbox = board->GetBoundingBox();

        // If board is empty, use a default reasonable size
        if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
            bbox = BOX2I( VECTOR2I( 0, 0 ), VECTOR2I( 100000000, 100000000 ) );
    }

    DIFF_CALLBACKS callbacks;
    callbacks.onUndo = []() { /* Test: no-op */ };
    callbacks.onRedo = []() { /* Test: no-op */ };

    DIFF_MANAGER::GetInstance().RegisterOverlay( frame()->GetCanvas()->GetView(), callbacks );
    DIFF_MANAGER::GetInstance().ShowDiff( bbox );

    return Empty();
}


HANDLER_RESULT<NetsResponse> API_HANDLER_PCB::handleGetNets( const HANDLER_CONTEXT<GetNets>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    NetsResponse response;
    BOARD* board = frame()->GetBoard();

    std::set<wxString> netclassFilter;

    for( const std::string& nc : aCtx.Request.netclass_filter() )
        netclassFilter.insert( wxString( nc.c_str(), wxConvUTF8 ) );

    for( NETINFO_ITEM* net : board->GetNetInfo() )
    {
        NETCLASS* nc = net->GetNetClass();

        if( !netclassFilter.empty() && nc && !netclassFilter.count( nc->GetName() ) )
            continue;

        board::types::Net* netProto = response.add_nets();
        netProto->set_name( net->GetNetname() );
        netProto->mutable_code()->set_value( net->GetNetCode() );
    }

    return response;
}


HANDLER_RESULT<NetClassForNetsResponse> API_HANDLER_PCB::handleGetNetClassForNets(
            const HANDLER_CONTEXT<GetNetClassForNets>& aCtx )
{
    NetClassForNetsResponse response;

    BOARD* board = frame()->GetBoard();
    const NETINFO_LIST& nets = board->GetNetInfo();
    google::protobuf::Any any;

    for( const board::types::Net& net : aCtx.Request.net() )
    {
        NETINFO_ITEM* netInfo = nets.GetNetItem( wxString::FromUTF8( net.name() ) );

        if( !netInfo )
            continue;

        netInfo->GetNetClass()->Serialize( any );
        auto [pair, rc] = response.mutable_classes()->insert( { net.name(), {} } );
        any.UnpackTo( &pair->second );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRefillZones( const HANDLER_CONTEXT<RefillZones>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.zones().empty() )
    {
        TOOL_MANAGER* mgr = frame()->GetToolManager();
        frame()->CallAfter( [mgr]()
                            {
                                mgr->RunAction( PCB_ACTIONS::zoneFillAll );
                            } );
    }
    else
    {
        // TODO
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_PCB::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SavedDocumentResponse response;
    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SaveBoard( wxEmptyString, frame()->GetBoard(), nullptr );

    return response;
}


HANDLER_RESULT<SavedSelectionResponse> API_HANDLER_PCB::handleSaveSelectionToString(
        const HANDLER_CONTEXT<SaveSelectionToString>& aCtx )
{
    SavedSelectionResponse response;

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_SELECTION& selection = selectionTool->GetSelection();

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SetBoard( frame()->GetBoard() );
    io.SaveSelection( selection, false );

    return response;
}


HANDLER_RESULT<CreateItemsResponse> API_HANDLER_PCB::handleParseAndCreateItemsFromString(
        const HANDLER_CONTEXT<ParseAndCreateItemsFromString>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxString contents = wxString::FromUTF8( aCtx.Request.contents() );

    if( contents.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Empty contents string provided" );
        return tl::unexpected( e );
    }

    // Set up clipboard IO with custom reader
    CLIPBOARD_IO clipboardIO;
    clipboardIO.SetReader( [&contents]() { return contents; } );
    clipboardIO.SetBoard( frame()->GetBoard() );

    BOARD_ITEM* clipItem = nullptr;

    try
    {
        clipItem = clipboardIO.Parse();
    }
    catch( const IO_ERROR& e )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( wxString::Format( "Failed to parse contents: %s",
                                                  e.What() ).ToStdString() );
        return tl::unexpected( err );
    }

    if( !clipItem )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to parse contents - invalid format" );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();
    CreateItemsResponse response;
    COMMIT* commit = getCurrentCommit( aCtx.ClientName );

    std::vector<BOARD_ITEM*> addedItems;

    auto addItemToResponse = [&response]( BOARD_ITEM* item )
    {
        ItemCreationResult* itemResult = response.add_created_items();
        itemResult->mutable_status()->set_code( ItemStatusCode::ISC_OK );
        item->Serialize( *itemResult->mutable_item() );
    };

    if( clipItem->Type() == PCB_T )
    {
        // Parsed a full board - extract its contents
        BOARD* clipBoard = static_cast<BOARD*>( clipItem );

        // Map nets from clipboard board to current board
        clipBoard->MapNets( board );

        // Add footprints
        for( FOOTPRINT* fp : clipBoard->Footprints() )
        {
            fp->SetParent( board );
            commit->Add( fp );
            addedItems.push_back( fp );
            addItemToResponse( fp );
        }
        clipBoard->RemoveAll( { PCB_FOOTPRINT_T } );

        // Add tracks
        for( PCB_TRACK* track : clipBoard->Tracks() )
        {
            track->SetParent( board );
            commit->Add( track );
            addedItems.push_back( track );
            addItemToResponse( track );
        }
        clipBoard->RemoveAll( { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T } );

        // Add zones
        for( ZONE* zone : clipBoard->Zones() )
        {
            zone->SetParent( board );
            commit->Add( zone );
            addedItems.push_back( zone );
            addItemToResponse( zone );
        }
        clipBoard->RemoveAll( { PCB_ZONE_T } );

        // Add drawings
        for( BOARD_ITEM* drawing : clipBoard->Drawings() )
        {
            drawing->SetParent( board );
            commit->Add( drawing );
            addedItems.push_back( drawing );
            addItemToResponse( drawing );
        }
        clipBoard->RemoveAll( { PCB_SHAPE_T, PCB_TEXT_T, PCB_TEXTBOX_T, PCB_TABLE_T,
                                PCB_DIMENSION_T, PCB_DIM_ALIGNED_T, PCB_DIM_LEADER_T,
                                PCB_DIM_CENTER_T, PCB_DIM_RADIAL_T, PCB_DIM_ORTHOGONAL_T,
                                PCB_TARGET_T, PCB_REFERENCE_IMAGE_T } );

        // Add groups
        for( PCB_GROUP* group : clipBoard->Groups() )
        {
            group->SetParent( board );
            commit->Add( group );
            addedItems.push_back( group );
            addItemToResponse( group );
        }
        clipBoard->RemoveAll( { PCB_GROUP_T } );

        delete clipBoard;
    }
    else if( clipItem->Type() == PCB_FOOTPRINT_T )
    {
        // Parsed a single footprint
        FOOTPRINT* fp = static_cast<FOOTPRINT*>( clipItem );
        fp->SetParent( board );
        commit->Add( fp );
        addedItems.push_back( fp );
        addItemToResponse( fp );
    }
    else
    {
        // Other single item types
        clipItem->SetParent( board );
        commit->Add( clipItem );
        addedItems.push_back( clipItem );
        addItemToResponse( clipItem );
    }

    // Rebuild connectivity for the new items
    if( !addedItems.empty() )
    {
        board->BuildListOfNets();
        board->BuildConnectivity();
    }

    pushCurrentCommit( aCtx.ClientName, _( "API: Parse and create items from string" ) );

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<BoardLayers> API_HANDLER_PCB::handleGetVisibleLayers(
        const HANDLER_CONTEXT<GetVisibleLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayers response;

    for( PCB_LAYER_ID layer : frame()->GetBoard()->GetVisibleLayers() )
        response.add_layers( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( layer ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetVisibleLayers(
        const HANDLER_CONTEXT<SetVisibleLayers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    LSET visible;
    LSET enabled = frame()->GetBoard()->GetEnabledLayers();

    for( int layerIdx : aCtx.Request.layers() )
    {
        PCB_LAYER_ID layer =
                FromProtoEnum<PCB_LAYER_ID>( static_cast<board::types::BoardLayer>( layerIdx ) );

        if( enabled.Contains( layer ) )
            visible.set( layer );
    }

    frame()->GetBoard()->SetVisibleLayers( visible );
    frame()->GetAppearancePanel()->OnBoardChanged();
    frame()->GetCanvas()->SyncLayersVisibility( frame()->GetBoard() );

    // Note: OnBoardChanged() and SyncLayersVisibility() handle UI updates.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    return Empty();
}


HANDLER_RESULT<BoardLayerResponse> API_HANDLER_PCB::handleGetActiveLayer(
        const HANDLER_CONTEXT<GetActiveLayer>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayerResponse response;
    response.set_layer(
            ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( frame()->GetActiveLayer() ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetActiveLayer(
        const HANDLER_CONTEXT<SetActiveLayer>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    if( !frame()->GetBoard()->GetEnabledLayers().Contains( layer ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Layer {} is not a valid layer for the given board",
                                            magic_enum::enum_name( layer ) ) );
        return tl::unexpected( err );
    }

    frame()->SetActiveLayer( layer );
    return Empty();
}


HANDLER_RESULT<BoardEditorAppearanceSettings> API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<GetBoardEditorAppearanceSettings>& aCtx )
{
    BoardEditorAppearanceSettings reply;

    // TODO: might be nice to put all these things in one place and have it derive SERIALIZABLE

    const PCB_DISPLAY_OPTIONS& displayOptions = frame()->GetDisplayOptions();

    reply.set_inactive_layer_display( ToProtoEnum<HIGH_CONTRAST_MODE, InactiveLayerDisplayMode>(
            displayOptions.m_ContrastModeDisplay ) );
    reply.set_net_color_display(
            ToProtoEnum<NET_COLOR_MODE, NetColorDisplayMode>( displayOptions.m_NetColorMode ) );

    reply.set_board_flip( frame()->GetCanvas()->GetView()->IsMirroredX()
                                  ? BoardFlipMode::BFM_FLIPPED_X
                                  : BoardFlipMode::BFM_NORMAL );

    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();

    reply.set_ratsnest_display( ToProtoEnum<RATSNEST_MODE, RatsnestDisplayMode>(
            editorSettings->m_Display.m_RatsnestMode ) );

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<SetBoardEditorAppearanceSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    PCB_DISPLAY_OPTIONS options = frame()->GetDisplayOptions();
    KIGFX::PCB_VIEW* view = frame()->GetCanvas()->GetView();
    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();
    const BoardEditorAppearanceSettings& newSettings = aCtx.Request.settings();

    options.m_ContrastModeDisplay =
            FromProtoEnum<HIGH_CONTRAST_MODE>( newSettings.inactive_layer_display() );
    options.m_NetColorMode =
            FromProtoEnum<NET_COLOR_MODE>( newSettings.net_color_display() );

    bool flip = newSettings.board_flip() == BoardFlipMode::BFM_FLIPPED_X;

    if( flip != view->IsMirroredX() )
    {
        view->SetMirror( !view->IsMirroredX(), view->IsMirroredY() );
        view->RecacheAllItems();
    }

    editorSettings->m_Display.m_RatsnestMode =
            FromProtoEnum<RATSNEST_MODE>( newSettings.ratsnest_display() );

    frame()->SetDisplayOptions( options );
    frame()->GetCanvas()->GetView()->UpdateAllLayersColor();

    // Note: UpdateAllLayersColor() should trigger necessary repaints.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    return Empty();
}


HANDLER_RESULT<InjectDrcErrorResponse> API_HANDLER_PCB::handleInjectDrcError(
        const HANDLER_CONTEXT<InjectDrcError>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SEVERITY severity = FromProtoEnum<SEVERITY>( aCtx.Request.severity() );
    int      layer = severity == RPT_SEVERITY_WARNING ? LAYER_DRC_WARNING : LAYER_DRC_ERROR;
    int      code = severity == RPT_SEVERITY_WARNING ? DRCE_GENERIC_WARNING : DRCE_GENERIC_ERROR;

    std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( code );

    drcItem->SetErrorMessage( wxString::FromUTF8( aCtx.Request.message() ) );

    RC_ITEM::KIIDS ids;

    for( const auto& id : aCtx.Request.items() )
        ids.emplace_back( KIID( id.value() ) );

    if( !ids.empty() )
        drcItem->SetItems( ids );

    const auto& pos = aCtx.Request.position();
    VECTOR2I    position( static_cast<int>( pos.x_nm() ), static_cast<int>( pos.y_nm() ) );

    PCB_MARKER* marker = new PCB_MARKER( drcItem, position, layer );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    commit->Add( marker );
    commit->Push( wxS( "API injected DRC marker" ) );

    InjectDrcErrorResponse response;
    response.mutable_marker()->set_value( marker->GetUUID().AsStdString() );

    return response;
}


HANDLER_RESULT<BoardStackupResponse> API_HANDLER_PCB::handleUpdateBoardStackup(
        const HANDLER_CONTEXT<UpdateBoardStackup>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    // Pack the incoming stackup into an Any for deserialization
    google::protobuf::Any stackupAny;
    stackupAny.PackFrom( aCtx.Request.stackup() );

    // Deserialize into the board's stackup
    BOARD_STACKUP& stackup = board->GetDesignSettings().GetStackupDescriptor();

    if( !stackup.Deserialize( stackupAny ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to deserialize board stackup" );
        return tl::unexpected( e );
    }

    // Synchronize with board settings
    board->GetDesignSettings().m_HasStackup = true;

    // Mark board as modified
    board->SetModified();
    frame()->OnModify();

    // Return the updated stackup (re-serialized for normalization)
    BoardStackupResponse response;
    google::protobuf::Any any;
    stackup.Serialize( any );
    any.UnpackTo( response.mutable_stackup() );

    // Add user-settable layer names
    for( board::BoardStackupLayer& layer : *response.mutable_stackup()->mutable_layers() )
    {
        if( layer.type() == board::BoardStackupLayerType::BSLT_DIELECTRIC )
            continue;

        PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( layer.layer() );
        wxCHECK2( id != UNDEFINED_LAYER, continue );

        layer.set_user_name( board->GetLayerName( id ) );
    }

    return response;
}


HANDLER_RESULT<RunDRCResponse> API_HANDLER_PCB::handleRunDRC(
        const HANDLER_CONTEXT<RunDRC>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    DRC_TOOL* drcTool = frame()->GetToolManager()->GetTool<DRC_TOOL>();

    if( !drcTool )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "DRC tool not available" );
        return tl::unexpected( e );
    }

    // Check if DRC is already running
    if( drcTool->IsDRCRunning() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "DRC is already running" );
        return tl::unexpected( e );
    }

    BOARD* board = frame()->GetBoard();

    // Clear existing markers before running DRC
    board->DeleteMARKERs( true, false );

    // Run DRC (blocking)
    drcTool->RunTests( &CLI_PROGRESS_REPORTER::GetInstance(),
                       aCtx.Request.refill_zones(),
                       aCtx.Request.report_all_track_errors(),
                       aCtx.Request.test_footprints() );

    // Count results
    RunDRCResponse response;
    int errorCount = 0;
    int warningCount = 0;
    int exclusionCount = 0;

    for( PCB_MARKER* marker : board->Markers() )
    {
        switch( marker->GetSeverity() )
        {
        case RPT_SEVERITY_ERROR:     errorCount++;     break;
        case RPT_SEVERITY_WARNING:   warningCount++;   break;
        case RPT_SEVERITY_EXCLUSION: exclusionCount++; break;
        default:                                       break;
        }
    }

    response.set_error_count( errorCount );
    response.set_warning_count( warningCount );
    response.set_exclusion_count( exclusionCount );

    return response;
}


HANDLER_RESULT<DRCViolationsResponse> API_HANDLER_PCB::handleGetDRCViolations(
        const HANDLER_CONTEXT<GetDRCViolations>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    DRCViolationsResponse response;

    // Build set of severity filters (if any specified)
    std::set<SEVERITY> severityFilter;

    for( int i = 0; i < aCtx.Request.severities_size(); i++ )
    {
        severityFilter.insert( FromProtoEnum<SEVERITY>( aCtx.Request.severities( i ) ) );
    }

    bool filterBySeverity = !severityFilter.empty();

    for( PCB_MARKER* marker : board->Markers() )
    {
        SEVERITY severity = marker->GetSeverity();

        // Apply severity filter if specified
        if( filterBySeverity && severityFilter.find( severity ) == severityFilter.end() )
            continue;

        DRCViolation* violation = response.add_violations();

        violation->mutable_id()->set_value( marker->GetUUID().AsStdString() );
        violation->set_severity( ToProtoEnum<SEVERITY, DrcSeverity>( severity ) );

        std::shared_ptr<RC_ITEM> rcItem = marker->GetRCItem();

        if( rcItem )
        {
            violation->set_error_code( rcItem->GetErrorCode() );
            violation->set_error_type( rcItem->GetErrorText( false ).ToStdString() );
            violation->set_message( rcItem->GetErrorMessage( false ).ToStdString() );

            // Add involved items
            for( const KIID& id : rcItem->GetIDs() )
            {
                if( id != niluuid )
                    violation->add_items()->set_value( id.AsStdString() );
            }
        }

        // Set position
        VECTOR2I pos = marker->GetPosition();
        violation->mutable_position()->set_x_nm( pos.x );
        violation->mutable_position()->set_y_nm( pos.y );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleClearDRCMarkers(
        const HANDLER_CONTEXT<ClearDRCMarkers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    // DeleteMARKERs( warnings_and_errors, exclusions )
    board->DeleteMARKERs( aCtx.Request.clear_violations(), aCtx.Request.clear_exclusions() );

    // Refresh the view
    frame()->GetCanvas()->Refresh();

    return Empty();
}


HANDLER_RESULT<DesignRulesResponse> API_HANDLER_PCB::handleGetDesignRules(
        const HANDLER_CONTEXT<GetDesignRules>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    const BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    DesignRulesResponse response;
    BoardDesignRules* rules = response.mutable_rules();

    // Copper clearances
    rules->set_min_clearance( bds.m_MinClearance );
    rules->set_min_track_width( bds.m_TrackMinWidth );
    rules->set_min_connection( bds.m_MinConn );

    // Via constraints
    rules->set_min_via_diameter( bds.m_ViasMinSize );
    rules->set_min_via_drill( bds.m_MinThroughDrill );
    rules->set_min_via_annular_width( bds.m_ViasMinAnnularWidth );

    // Microvia constraints
    rules->set_min_microvia_diameter( bds.m_MicroViasMinSize );
    rules->set_min_microvia_drill( bds.m_MicroViasMinDrill );

    // Hole constraints
    rules->set_min_through_hole( bds.m_MinThroughDrill );
    rules->set_min_hole_to_hole( bds.m_HoleToHoleMin );
    rules->set_hole_to_copper_clearance( bds.m_HoleClearance );

    // Silkscreen
    rules->set_min_silk_clearance( bds.m_SilkClearance );
    rules->set_min_silk_text_height( bds.m_MinSilkTextHeight );
    rules->set_min_silk_text_thickness( bds.m_MinSilkTextThickness );

    // Board edge
    rules->set_copper_edge_clearance( bds.m_CopperEdgeClearance );

    // Solder mask
    rules->set_solder_mask_expansion( bds.m_SolderMaskExpansion );
    rules->set_solder_mask_min_width( bds.m_SolderMaskMinWidth );
    rules->set_solder_mask_to_copper_clearance( bds.m_SolderMaskToCopperClearance );
    rules->set_allow_soldermask_bridges_in_fps( bds.m_AllowSoldermaskBridgesInFPs );
    rules->set_tent_vias_front( bds.m_TentViasFront );
    rules->set_tent_vias_back( bds.m_TentViasBack );

    // Solder paste
    rules->set_solder_paste_margin( bds.m_SolderPasteMargin );
    rules->set_solder_paste_margin_ratio( bds.m_SolderPasteMarginRatio );

    // Minimum resolved spokes
    rules->set_min_resolved_spokes( bds.m_MinResolvedSpokes );

    // Arc/Circle Approximations
    rules->set_max_error( bds.m_MaxError );

    // Zone Fill Strategy
    rules->set_allow_external_fillets( bds.m_ZoneKeepExternalFillets );

    // Length Tuning
    rules->set_include_stackup_in_length( bds.m_UseHeightForLengthCalcs );

    return response;
}


HANDLER_RESULT<DesignRulesResponse> API_HANDLER_PCB::handleSetDesignRules(
        const HANDLER_CONTEXT<SetDesignRules>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    const BoardDesignRules& rules = aCtx.Request.rules();

    // DEBUG: Log all incoming min values
    wxLogMessage( "SetDesignRules INCOMING: min_via_drill=%lld, min_through_hole=%lld, current m_MinThroughDrill=%d",
                  (long long)rules.min_via_drill(), (long long)rules.min_through_hole(), bds.m_MinThroughDrill );

    // Copper clearances
    if( rules.min_clearance() > 0 )
        bds.m_MinClearance = rules.min_clearance();
    if( rules.min_track_width() > 0 )
        bds.m_TrackMinWidth = rules.min_track_width();
    if( rules.min_connection() > 0 )
        bds.m_MinConn = rules.min_connection();

    // Via constraints
    if( rules.min_via_diameter() > 0 )
        bds.m_ViasMinSize = rules.min_via_diameter();
    if( rules.min_via_annular_width() > 0 )
        bds.m_ViasMinAnnularWidth = rules.min_via_annular_width();

    // Microvia constraints
    if( rules.min_microvia_diameter() > 0 )
        bds.m_MicroViasMinSize = rules.min_microvia_diameter();
    if( rules.min_microvia_drill() > 0 )
        bds.m_MicroViasMinDrill = rules.min_microvia_drill();

    // Hole constraints - min_via_drill and min_through_hole both map to m_MinThroughDrill
    // Prioritize min_via_drill if set, otherwise use min_through_hole
    if( rules.min_via_drill() > 0 )
    {
        wxLogMessage( "SetDesignRules: Setting m_MinThroughDrill from min_via_drill=%lld", (long long)rules.min_via_drill() );
        bds.m_MinThroughDrill = rules.min_via_drill();
    }
    else if( rules.min_through_hole() > 0 )
    {
        wxLogMessage( "SetDesignRules: Setting m_MinThroughDrill from min_through_hole=%lld", (long long)rules.min_through_hole() );
        bds.m_MinThroughDrill = rules.min_through_hole();
    }
    wxLogMessage( "SetDesignRules AFTER: m_MinThroughDrill=%d", bds.m_MinThroughDrill );
    if( rules.min_hole_to_hole() > 0 )
        bds.m_HoleToHoleMin = rules.min_hole_to_hole();
    if( rules.hole_to_copper_clearance() > 0 )
        bds.m_HoleClearance = rules.hole_to_copper_clearance();

    // Silkscreen
    if( rules.min_silk_clearance() >= 0 )
        bds.m_SilkClearance = rules.min_silk_clearance();
    if( rules.min_silk_text_height() > 0 )
        bds.m_MinSilkTextHeight = rules.min_silk_text_height();
    if( rules.min_silk_text_thickness() > 0 )
        bds.m_MinSilkTextThickness = rules.min_silk_text_thickness();

    // Board edge
    if( rules.copper_edge_clearance() >= 0 )
        bds.m_CopperEdgeClearance = rules.copper_edge_clearance();

    // Solder mask
    bds.m_SolderMaskExpansion = rules.solder_mask_expansion();
    if( rules.solder_mask_min_width() >= 0 )
        bds.m_SolderMaskMinWidth = rules.solder_mask_min_width();
    if( rules.solder_mask_to_copper_clearance() >= 0 )
        bds.m_SolderMaskToCopperClearance = rules.solder_mask_to_copper_clearance();
    bds.m_AllowSoldermaskBridgesInFPs = rules.allow_soldermask_bridges_in_fps();
    bds.m_TentViasFront = rules.tent_vias_front();
    bds.m_TentViasBack = rules.tent_vias_back();

    // Solder paste
    bds.m_SolderPasteMargin = rules.solder_paste_margin();
    bds.m_SolderPasteMarginRatio = rules.solder_paste_margin_ratio();

    // Minimum resolved spokes
    if( rules.min_resolved_spokes() > 0 )
        bds.m_MinResolvedSpokes = rules.min_resolved_spokes();

    // Arc/Circle Approximations
    if( rules.max_error() > 0 )
        bds.m_MaxError = rules.max_error();

    // Zone Fill Strategy
    bds.m_ZoneKeepExternalFillets = rules.allow_external_fillets();

    // Length Tuning
    bds.m_UseHeightForLengthCalcs = rules.include_stackup_in_length();

    board->SetModified();
    frame()->OnModify();

    // Return the updated rules (inline to avoid context type mismatch)
    DesignRulesResponse response;
    BoardDesignRules* updatedRules = response.mutable_rules();

    updatedRules->set_min_clearance( bds.m_MinClearance );
    updatedRules->set_min_track_width( bds.m_TrackMinWidth );
    updatedRules->set_min_connection( bds.m_MinConn );
    updatedRules->set_min_via_diameter( bds.m_ViasMinSize );
    updatedRules->set_min_via_drill( bds.m_MinThroughDrill );
    updatedRules->set_min_via_annular_width( bds.m_ViasMinAnnularWidth );
    updatedRules->set_min_microvia_diameter( bds.m_MicroViasMinSize );
    updatedRules->set_min_microvia_drill( bds.m_MicroViasMinDrill );
    updatedRules->set_min_through_hole( bds.m_MinThroughDrill );
    updatedRules->set_min_hole_to_hole( bds.m_HoleToHoleMin );
    updatedRules->set_hole_to_copper_clearance( bds.m_HoleClearance );
    updatedRules->set_min_silk_clearance( bds.m_SilkClearance );
    updatedRules->set_min_silk_text_height( bds.m_MinSilkTextHeight );
    updatedRules->set_min_silk_text_thickness( bds.m_MinSilkTextThickness );
    updatedRules->set_copper_edge_clearance( bds.m_CopperEdgeClearance );
    updatedRules->set_solder_mask_expansion( bds.m_SolderMaskExpansion );
    updatedRules->set_solder_mask_min_width( bds.m_SolderMaskMinWidth );
    updatedRules->set_solder_mask_to_copper_clearance( bds.m_SolderMaskToCopperClearance );
    updatedRules->set_allow_soldermask_bridges_in_fps( bds.m_AllowSoldermaskBridgesInFPs );
    updatedRules->set_tent_vias_front( bds.m_TentViasFront );
    updatedRules->set_tent_vias_back( bds.m_TentViasBack );
    updatedRules->set_solder_paste_margin( bds.m_SolderPasteMargin );
    updatedRules->set_solder_paste_margin_ratio( bds.m_SolderPasteMarginRatio );
    updatedRules->set_min_resolved_spokes( bds.m_MinResolvedSpokes );
    updatedRules->set_max_error( bds.m_MaxError );
    updatedRules->set_allow_external_fillets( bds.m_ZoneKeepExternalFillets );
    updatedRules->set_include_stackup_in_length( bds.m_UseHeightForLengthCalcs );

    return response;
}


HANDLER_RESULT<DRCSettingsResponse> API_HANDLER_PCB::handleGetDRCSettings(
        const HANDLER_CONTEXT<GetDRCSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    const BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    DRCSettingsResponse response;
    DRCSettingsData* settings = response.mutable_settings();

    // Export DRC severities using settings keys (human-readable names)
    for( const RC_ITEM& item : DRC_ITEM::GetItemsWithSeverities() )
    {
        wxString settingsKey = item.GetSettingsKey();

        // Skip heading items (empty settings key)
        if( settingsKey.IsEmpty() )
            continue;

        int errorCode = item.GetErrorCode();

        // Get severity from board settings, or use default RPT_SEVERITY_ERROR
        SEVERITY severity = RPT_SEVERITY_ERROR;
        auto it = bds.m_DRCSeverities.find( errorCode );
        if( it != bds.m_DRCSeverities.end() )
            severity = it->second;

        DRCCheckSeverity* checkSeverity = settings->add_check_severities();
        checkSeverity->set_check_name( settingsKey.ToStdString() );
        checkSeverity->set_severity( ToProtoEnum<SEVERITY, DrcSeverity>( severity ) );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetDRCSettings(
        const HANDLER_CONTEXT<SetDRCSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    const DRCSettingsData& settings = aCtx.Request.settings();

    // Apply DRC severity settings using settings keys
    for( const DRCCheckSeverity& checkSeverity : settings.check_severities() )
    {
        wxString settingsKey = wxString::FromUTF8( checkSeverity.check_name() );

        // Look up the DRC item by settings key to get its error code
        std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( settingsKey );

        if( drcItem )
        {
            int errorCode = drcItem->GetErrorCode();
            SEVERITY severity = FromProtoEnum<SEVERITY>( checkSeverity.severity() );
            bds.m_DRCSeverities[errorCode] = severity;
        }
        // Skip unknown settings keys silently
    }

    board->SetModified();
    frame()->OnModify();

    return Empty();
}


HANDLER_RESULT<PCBGridSettingsResponse> API_HANDLER_PCB::handleGetPCBGridSettings(
        const HANDLER_CONTEXT<GetPCBGridSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    const GRID_SETTINGS& gridSettings = frame()->GetWindowSettings( frame()->config() )->grid;

    PCBGridSettingsResponse response;
    PCBGridSettings* settings = response.mutable_settings();

    // Get current grid size - prefer reading from GAL for accuracy
    bool gotGridFromGAL = false;

    if( frame()->GetCanvas() && frame()->GetCanvas()->GetGAL() )
    {
        VECTOR2D gridSize = frame()->GetCanvas()->GetGAL()->GetGridSize();
        settings->set_grid_size_x_nm( static_cast<int64_t>( gridSize.x ) );
        settings->set_grid_size_y_nm( static_cast<int64_t>( gridSize.y ) );
        gotGridFromGAL = true;
    }

    // Fallback to settings if GAL not available
    if( !gotGridFromGAL )
    {
        int currentIdx = gridSettings.last_size_idx;

        if( currentIdx >= 0 && currentIdx < static_cast<int>( gridSettings.grids.size() ) )
        {
            const GRID& grid = gridSettings.grids[currentIdx];
            VECTOR2D gridSize = grid.ToDouble( pcbIUScale );

            settings->set_grid_size_x_nm( static_cast<int64_t>( gridSize.x ) );
            settings->set_grid_size_y_nm( static_cast<int64_t>( gridSize.y ) );
        }
    }

    settings->set_show_grid( gridSettings.show );

    // Map grid style
    switch( gridSettings.style )
    {
    case 0:  settings->set_style( GridStyle::GS_DOTS ); break;
    case 1:  settings->set_style( GridStyle::GS_LINES ); break;
    case 2:  settings->set_style( GridStyle::GS_SMALL_CROSS ); break;
    default: settings->set_style( GridStyle::GS_UNKNOWN ); break;
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetPCBGridSettings(
        const HANDLER_CONTEXT<SetPCBGridSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    GRID_SETTINGS& gridSettings = frame()->GetWindowSettings( frame()->config() )->grid;

    // Set grid visibility
    if( aCtx.Request.has_show_grid() )
        gridSettings.show = aCtx.Request.show_grid();

    // Set grid style
    if( aCtx.Request.has_style() )
    {
        switch( aCtx.Request.style() )
        {
        case GridStyle::GS_DOTS:        gridSettings.style = 0; break;
        case GridStyle::GS_LINES:       gridSettings.style = 1; break;
        case GridStyle::GS_SMALL_CROSS: gridSettings.style = 2; break;
        default: break;
        }
    }

    // Set custom grid size if specified
    if( aCtx.Request.has_grid_size_x_nm() || aCtx.Request.has_grid_size_y_nm() )
    {
        // Convert nm to appropriate unit string for grid
        double gridX = aCtx.Request.has_grid_size_x_nm()
                           ? aCtx.Request.grid_size_x_nm() / 1e6  // nm to mm
                           : 1.0;
        double gridY = aCtx.Request.has_grid_size_y_nm()
                           ? aCtx.Request.grid_size_y_nm() / 1e6  // nm to mm
                           : gridX;  // Default to square grid

        // Store as user grid for persistence
        gridSettings.user_grid_x = wxString::Format( wxT( "%g mm" ), gridX );
        gridSettings.user_grid_y = wxString::Format( wxT( "%g mm" ), gridY );

        // Create a GRID entry with the new size
        GRID newGrid;
        newGrid.name = wxT( "API" );
        newGrid.x = wxString::Format( wxT( "%g mm" ), gridX );
        newGrid.y = wxString::Format( wxT( "%g mm" ), gridY );

        // Find or add the "API" grid entry
        int apiGridIdx = -1;
        for( size_t i = 0; i < gridSettings.grids.size(); ++i )
        {
            if( gridSettings.grids[i].name == wxT( "API" ) )
            {
                apiGridIdx = static_cast<int>( i );
                break;
            }
        }

        if( apiGridIdx >= 0 )
        {
            // Update existing API grid
            gridSettings.grids[apiGridIdx] = newGrid;
        }
        else
        {
            // Add new API grid at the end
            apiGridIdx = static_cast<int>( gridSettings.grids.size() );
            gridSettings.grids.push_back( newGrid );
        }

        // Select the API grid
        gridSettings.last_size_idx = apiGridIdx;

        // Apply the grid size directly to the GAL and canvas
        // The gridPreset action uses a cached grid list (m_grids) that doesn't include
        // our runtime-added "API" grid entry, so we set the size directly
        if( frame()->GetCanvas() )
        {
            KIGFX::GAL* gal = frame()->GetCanvas()->GetGAL();
            KIGFX::VIEW* view = frame()->GetCanvas()->GetView();

            if( gal )
            {
                VECTOR2D gridSize( gridX * pcbIUScale.IU_PER_MM, gridY * pcbIUScale.IU_PER_MM );
                gal->SetGridSize( gridSize );
            }

            if( view )
            {
                view->MarkTargetDirty( KIGFX::TARGET_NONCACHED );
            }
        }
    }

    // Update grid visibility in GAL
    if( aCtx.Request.has_show_grid() && frame()->GetCanvas() && frame()->GetCanvas()->GetGAL() )
    {
        frame()->GetCanvas()->GetGAL()->SetGridVisibility( aCtx.Request.show_grid() );
    }

    // Refresh the view
    if( frame()->GetCanvas() )
    {
        if( frame()->GetCanvas()->GetView() )
            frame()->GetCanvas()->GetView()->MarkTargetDirty( KIGFX::TARGET_NONCACHED );
        frame()->GetCanvas()->Refresh();
    }

    return Empty();
}


HANDLER_RESULT<GraphicsDefaultsResponse> API_HANDLER_PCB::handleSetGraphicsDefaults(
        const HANDLER_CONTEXT<SetGraphicsDefaults>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    const kiapi::board::GraphicsDefaults& defaults = aCtx.Request.defaults();

    // Helper to map BoardLayerClass to LAYER_CLASS index
    auto mapLayerClass = []( kiapi::board::BoardLayerClass aClass ) -> int
    {
        switch( aClass )
        {
        case kiapi::board::BLC_SILKSCREEN:  return LAYER_CLASS_SILK;
        case kiapi::board::BLC_COPPER:      return LAYER_CLASS_COPPER;
        case kiapi::board::BLC_EDGES:       return LAYER_CLASS_EDGES;
        case kiapi::board::BLC_COURTYARD:   return LAYER_CLASS_COURTYARD;
        case kiapi::board::BLC_FABRICATION: return LAYER_CLASS_FAB;
        case kiapi::board::BLC_OTHER:       return LAYER_CLASS_OTHERS;
        default:                            return -1;
        }
    };

    // Apply layer defaults from the request
    for( const auto& layerDefaults : defaults.layers() )
    {
        int layerClassIdx = mapLayerClass( layerDefaults.layer() );

        if( layerClassIdx < 0 || layerClassIdx >= LAYER_CLASS_COUNT )
            continue;

        // Apply line thickness
        if( layerDefaults.has_line_thickness() )
            bds.m_LineThickness[layerClassIdx] = layerDefaults.line_thickness().value_nm();

        // Apply text attributes
        if( layerDefaults.has_text() )
        {
            const auto& text = layerDefaults.text();

            if( text.has_size() )
            {
                bds.m_TextSize[layerClassIdx].x = text.size().x_nm();
                bds.m_TextSize[layerClassIdx].y = text.size().y_nm();
            }

            if( text.has_stroke_width() )
                bds.m_TextThickness[layerClassIdx] = text.stroke_width().value_nm();

            bds.m_TextItalic[layerClassIdx] = text.italic();
            bds.m_TextUpright[layerClassIdx] = text.keep_upright();
        }
    }

    board->SetModified();
    frame()->OnModify();

    // Return the updated graphics defaults
    return handleGetGraphicsDefaults(
            HANDLER_CONTEXT<GetGraphicsDefaults>{ aCtx.ClientName,
                                                   GetGraphicsDefaults() } );
}


//
// Document Management Handlers
//

HANDLER_RESULT<CreateDocumentResponse> API_HANDLER_PCB::handleCreateDocument(
        const HANDLER_CONTEXT<CreateDocument>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_PCB )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    wxString path = wxString::FromUTF8( aCtx.Request.path() );

    if( path.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Path cannot be empty" );
        return tl::unexpected( e );
    }

    // Check if file already exists
    if( wxFileName::FileExists( path ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "File already exists: " + path.ToStdString() );
        return tl::unexpected( e );
    }

    // Create directories if needed
    wxFileName fn( path );
    fn.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    // If a template is provided, copy it
    if( !aCtx.Request.template_path().empty() )
    {
        wxString templatePath = wxString::FromUTF8( aCtx.Request.template_path() );

        if( !wxFileName::FileExists( templatePath ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Template file not found: " + templatePath.ToStdString() );
            return tl::unexpected( e );
        }

        if( !wxCopyFile( templatePath, path ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to copy template file" );
            return tl::unexpected( e );
        }
    }
    else
    {
        // Create a minimal empty board file
        wxFileOutputStream output( path );

        if( !output.IsOk() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to create file: " + path.ToStdString() );
            return tl::unexpected( e );
        }

        wxString content = wxT( "(kicad_pcb (version 20231014) (generator \"api\"))\n" );
        output.Write( content.c_str(), content.length() );
    }

    CreateDocumentResponse response;
    response.mutable_document()->set_type( DocumentType::DOCTYPE_PCB );

    // Open the document if requested
    if( aCtx.Request.open_after_create() )
    {
        if( !frame()->OpenProjectFiles( std::vector<wxString>{ path } ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Created file but failed to open it" );
            return tl::unexpected( e );
        }

        wxFileName openedFn( path );
        response.mutable_document()->set_board_filename( openedFn.GetFullName().ToStdString() );
    }

    return response;
}


HANDLER_RESULT<OpenDocumentResponse> API_HANDLER_PCB::handleOpenDocument(
        const HANDLER_CONTEXT<OpenDocument>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_PCB )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    wxString path = wxString::FromUTF8( aCtx.Request.path() );

    if( path.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Path cannot be empty" );
        return tl::unexpected( e );
    }

    if( !wxFileName::FileExists( path ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "File not found: " + path.ToStdString() );
        return tl::unexpected( e );
    }

    if( !frame()->OpenProjectFiles( std::vector<wxString>{ path } ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to open board: " + path.ToStdString() );
        return tl::unexpected( e );
    }

    OpenDocumentResponse response;
    response.mutable_document()->set_type( DocumentType::DOCTYPE_PCB );

    wxFileName fn( path );
    response.mutable_document()->set_board_filename( fn.GetFullName().ToStdString() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleCloseDocument(
        const HANDLER_CONTEXT<CloseDocument>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    // Save first if requested
    if( aCtx.Request.save_changes() && !aCtx.Request.force() )
    {
        if( !frame()->SaveBoard() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to save document before closing" );
            return tl::unexpected( e );
        }
    }

    // Clear the board - doAskAboutUnsavedChanges=false since we handle that above
    frame()->Clear_Pcb( false );

    return Empty();
}


//
// API_UPDATE_REPORTER implementation
//

REPORTER& API_UPDATE_REPORTER::Report( const wxString& aText, SEVERITY aSeverity )
{
    API_UPDATE_CHANGE change;
    change.message = aText;

    // Determine type from message content
    if( aText.StartsWith( wxT( "Add " ) ) )
    {
        change.type = PCBUpdateChange::CT_FOOTPRINT_ADDED;
        m_addedCount++;
    }
    else if( aText.Contains( wxT( "footprint from" ) ) || aText.Contains( wxT( "Changed footprint" ) ) )
    {
        change.type = PCBUpdateChange::CT_FOOTPRINT_REPLACED;
        m_replacedCount++;
    }
    else if( aText.StartsWith( wxT( "Remove " ) ) || aText.StartsWith( wxT( "Delete " ) ) )
    {
        change.type = PCBUpdateChange::CT_FOOTPRINT_DELETED;
        m_deletedCount++;
    }
    else if( aText.Contains( wxT( "net " ) ) || aText.Contains( wxT( " net to " ) ) )
    {
        change.type = PCBUpdateChange::CT_NET_CHANGED;
        m_netsChangedCount++;
    }
    else if( aText.Contains( wxT( "pad " ) ) && aText.Contains( wxT( "net" ) ) )
    {
        change.type = PCBUpdateChange::CT_PAD_NET_CHANGED;
        m_netsChangedCount++;
    }
    else if( aSeverity == RPT_SEVERITY_WARNING )
    {
        change.type = PCBUpdateChange::CT_WARNING;
        m_warningCount++;
    }
    else if( aSeverity == RPT_SEVERITY_ERROR )
    {
        change.type = PCBUpdateChange::CT_ERROR;
        m_errorCount++;
    }
    else
    {
        change.type = PCBUpdateChange::CT_FOOTPRINT_UPDATED;
        m_updatedCount++;
    }

    // Extract reference from message (e.g., "Add component R1...")
    // Matches patterns like R1, U1, C10, D2, etc.
    wxRegEx refRegex( wxT( "\\b([A-Z]+[0-9]+)\\b" ) );
    if( refRegex.Matches( aText ) )
        change.reference = refRegex.GetMatch( aText, 1 );

    m_changes.push_back( change );
    return *this;
}


//
// Update PCB from Schematic
//

HANDLER_RESULT<UpdatePCBFromSchematicResponse> API_HANDLER_PCB::handleUpdatePCBFromSchematic(
        const HANDLER_CONTEXT<UpdatePCBFromSchematic>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    // Fetch netlist from schematic - API version (no modal dialogs)
    // Use heap allocation to avoid stack corruption issues when called via nested event processing
    // (e.g., when invoked from agent via wxYield). Stack-allocated NETLIST was getting corrupted.
    std::unique_ptr<NETLIST> netlist = std::make_unique<NETLIST>();

    wxLogInfo( "API: handleUpdatePCBFromSchematic - starting" );

    // Check for standalone mode (PCB editor opened without project manager)
    if( Kiface().IsSingle() )
    {
        wxLogInfo( "API: handleUpdatePCBFromSchematic - standalone mode, returning error" );
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Cannot update PCB from schematic in standalone mode. "
                             "Open the project from KiCad project manager." );
        return tl::unexpected( e );
    }

    wxLogInfo( "API: handleUpdatePCBFromSchematic - not standalone, checking schematic frame" );

    // Check if schematic editor frame exists (don't create it, just check)
    KIWAY_PLAYER* schFrame = frame()->Kiway().Player( FRAME_SCH, false );
    if( !schFrame )
    {
        wxLogInfo( "API: handleUpdatePCBFromSchematic - no schematic frame, returning error" );
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Schematic editor is not open. "
                             "Please open the schematic before updating PCB." );
        return tl::unexpected( e );
    }

    wxLogInfo( "API: handleUpdatePCBFromSchematic - schematic frame exists, sending ExpressMail" );

    // Request netlist from schematic via ExpressMail (non-interactive)
    // Using empty string as payload - schematic will fill it with netlist data
    std::string netlistPayload;
    frame()->Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_GET_NETLIST, netlistPayload, frame() );

    wxLogInfo( "API: handleUpdatePCBFromSchematic - ExpressMail returned, payload size: %zu", netlistPayload.size() );

    // Check if we received a netlist (payload should be modified by schematic editor)
    if( netlistPayload.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to fetch netlist from schematic. "
                             "Ensure the schematic is open and fully annotated." );
        return tl::unexpected( e );
    }

    // Log first line of netlist to verify format
    {
        size_t firstNewline = netlistPayload.find( '\n' );
        std::string firstLine = ( firstNewline != std::string::npos )
                                    ? netlistPayload.substr( 0, firstNewline )
                                    : netlistPayload.substr( 0, std::min( size_t( 100 ), netlistPayload.size() ) );
        wxLogInfo( "API: handleUpdatePCBFromSchematic - netlist first line: %s", firstLine.c_str() );
    }

    // Parse the netlist
    wxLogInfo( "API: handleUpdatePCBFromSchematic - parsing netlist" );
    try
    {
        wxLogInfo( "API: handleUpdatePCBFromSchematic - creating STRING_LINE_READER" );
        auto lineReader = new STRING_LINE_READER( netlistPayload, _( "Eeschema netlist" ) );

        wxLogInfo( "API: handleUpdatePCBFromSchematic - creating KICAD_NETLIST_READER" );
        KICAD_NETLIST_READER netlistReader( lineReader, netlist.get() );

        wxLogInfo( "API: handleUpdatePCBFromSchematic - calling LoadNetlist()" );
        netlistReader.LoadNetlist();
    }
    catch( const IO_ERROR& e )
    {
        wxLogError( "API: handleUpdatePCBFromSchematic - IO_ERROR during netlist parse: %s", e.What() );
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error parsing netlist from schematic: {}",
                                            e.What().ToStdString() ) );
        return tl::unexpected( err );
    }
    catch( const std::out_of_range& e )
    {
        wxLogError( "API: handleUpdatePCBFromSchematic - std::out_of_range during netlist parse: %s", e.what() );
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error parsing netlist (out_of_range): {}", e.what() ) );
        return tl::unexpected( err );
    }
    catch( const std::exception& e )
    {
        wxLogError( "API: handleUpdatePCBFromSchematic - std::exception during netlist parse: %s", e.what() );
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error parsing netlist: {}", e.what() ) );
        return tl::unexpected( err );
    }

    wxLogInfo( "API: handleUpdatePCBFromSchematic - netlist parsed, component count: %zu", netlist->GetCount() );

    // Create reporter to capture changes
    API_UPDATE_REPORTER reporter;

    // Configure updater
    wxLogInfo( "API: handleUpdatePCBFromSchematic - configuring updater" );

    // Ensure project footprint library is loaded synchronously before the netlist updater runs.
    // When generate_footprint or sch_import_symbol runs while the PCB editor is closed,
    // MAIL_RELOAD_LIB is silently dropped. When the PCB editor later opens, background preload
    // may still be in progress (LOADING state) when pcb_sync_schematic is called, causing
    // fetchIfLoaded("project") to return nullopt and footprint lookup to fail.
    // Calling LoadProjectTables + LoadOne here guarantees the library is in LOADED state.
    wxLogInfo( "API: handleUpdatePCBFromSchematic - ensuring project footprint library is loaded" );
    Pgm().GetLibraryManager().LoadProjectTables( { LIBRARY_TABLE_TYPE::FOOTPRINT } );
    FOOTPRINT_LIBRARY_ADAPTER* fpAdapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    if( fpAdapter )
    {
        fpAdapter->LoadOne( wxT( "project" ) );
        wxLogInfo( "API: handleUpdatePCBFromSchematic - project footprint library load complete" );
    }

    BOARD_NETLIST_UPDATER updater( frame(), board );
    updater.SetReporter( &reporter );

    const auto& opts = aCtx.Request.options();
    updater.SetIsDryRun( aCtx.Request.dry_run() );
    updater.SetLookupByTimestamp( opts.lookup_by_timestamp() );
    updater.SetReplaceFootprints( opts.replace_footprints() );
    updater.SetDeleteUnusedFootprints( opts.delete_unused_footprints() );
    updater.SetOverrideLocks( opts.override_locks() );
    updater.SetUpdateFields( opts.update_fields() );
    updater.SetRemoveExtraFields( opts.remove_extra_fields() );
    updater.SetTransferGroups( opts.transfer_groups() );

    // Execute update
    wxLogInfo( "API: handleUpdatePCBFromSchematic - executing UpdateNetlist" );
    bool success = false;
    try
    {
        success = updater.UpdateNetlist( *netlist );
    }
    catch( const std::exception& e )
    {
        wxLogError( "API: handleUpdatePCBFromSchematic - exception in UpdateNetlist: %s", e.what() );
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error updating PCB from netlist: {}", e.what() ) );
        return tl::unexpected( err );
    }
    wxLogInfo( "API: handleUpdatePCBFromSchematic - UpdateNetlist completed, success: %d", success );

    // Populate response
    UpdatePCBFromSchematicResponse response;

    response.set_changes_applied( !aCtx.Request.dry_run() && success );
    response.set_footprints_added( reporter.GetAddedCount() );
    response.set_footprints_replaced( reporter.GetReplacedCount() );
    response.set_footprints_deleted( reporter.GetDeletedCount() );
    response.set_footprints_updated( reporter.GetUpdatedCount() );
    response.set_nets_changed( reporter.GetNetsChangedCount() );
    response.set_warnings( reporter.GetWarningCount() );
    response.set_errors( reporter.GetErrorCount() );

    // Copy detailed changes
    for( const auto& change : reporter.GetChanges() )
    {
        auto* protoChange = response.add_changes();
        protoChange->set_type( change.type );
        protoChange->set_reference( change.reference.ToStdString() );
        protoChange->set_message( change.message.ToStdString() );

        // Note: itemId is not currently populated by the reporter.
        // Future enhancement could track item IDs from the updater.
    }

    // Post-update handling (if not dry run)
    if( !aCtx.Request.dry_run() && success )
    {
        bool dummy = false;
        frame()->OnNetlistChanged( updater, &dummy );
        frame()->Refresh();
    }

    return response;
}


//
// Footprint Library Browsing
//

HANDLER_RESULT<GetLibraryFootprintsResponse> API_HANDLER_PCB::handleGetLibraryFootprints(
        const HANDLER_CONTEXT<GetLibraryFootprints>& aCtx )
{
    GetLibraryFootprintsResponse response;

    // Get the footprint library adapter
    FOOTPRINT_LIBRARY_ADAPTER* fpAdapter = nullptr;

    if( frame() )
        fpAdapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );

    if( !fpAdapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No footprint library adapter available" );
        return tl::unexpected( e );
    }

    wxString libName = wxString::FromUTF8( aCtx.Request.library_name() );

    // Check library exists
    if( !fpAdapter->HasLibrary( libName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Library '{}' not found", aCtx.Request.library_name() ) );
        return tl::unexpected( e );
    }

    try
    {
        // Enumerate footprints in library
        std::vector<wxString> fpNames = fpAdapter->GetFootprintNames( libName, true /* best efforts */ );

        for( const wxString& fpName : fpNames )
        {
            FootprintInfo* info = response.add_footprints();

            info->set_name( fpName.ToUTF8().data() );
            info->set_lib_id( fmt::format( "{}:{}", aCtx.Request.library_name(),
                                           fpName.ToUTF8().data() ) );
            // Note: Extended footprint info (description, keywords, pad counts) requires
            // loading the full footprint, which is expensive. Omitted for list operations.
        }
    }
    catch( const IO_ERROR& e )
    {
        ApiResponseStatus status;
        status.set_status( ApiStatusCode::AS_BAD_REQUEST );
        status.set_error_message( e.What().ToUTF8().data() );
        return tl::unexpected( status );
    }

    return response;
}


HANDLER_RESULT<SearchLibraryFootprintsResponse> API_HANDLER_PCB::handleSearchLibraryFootprints(
        const HANDLER_CONTEXT<SearchLibraryFootprints>& aCtx )
{
    // TODO: Implement footprint search using FP_LIB_TABLE directly
    // The FOOTPRINT_LIST singleton pattern was removed in KiCad 10
    ApiResponseStatus e;
    e.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
    e.set_error_message( "SearchLibraryFootprints is not yet implemented" );
    return tl::unexpected( e );
}


HANDLER_RESULT<GetFootprintInfoResponse> API_HANDLER_PCB::handleGetFootprintInfo(
        const HANDLER_CONTEXT<GetFootprintInfo>& aCtx )
{
    GetFootprintInfoResponse response;

    LIB_ID libId;

    if( libId.Parse( aCtx.Request.lib_id() ) != 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Invalid lib_id format. Expected 'Library:Footprint'" );
        return tl::unexpected( e );
    }

    // Get the footprint library adapter
    FOOTPRINT_LIBRARY_ADAPTER* fpAdapter = nullptr;

    if( frame() )
        fpAdapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );

    if( !fpAdapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No footprint library adapter available" );
        return tl::unexpected( e );
    }

    try
    {
        // Load the actual footprint to get full details
        FOOTPRINT* footprint = fpAdapter->LoadFootprint(
            libId.GetLibNickname(), libId.GetLibItemName(), true /* keep UUID */ );

        if( !footprint )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "Footprint '{}' not found", aCtx.Request.lib_id() ) );
            return tl::unexpected( e );
        }

        // Fill basic info
        FootprintInfo* info = response.mutable_info();
        info->set_name( libId.GetLibItemName().c_str() );
        info->set_lib_id( aCtx.Request.lib_id() );
        info->set_description( footprint->GetLibDescription().ToUTF8().data() );
        info->set_keywords( footprint->GetKeywords().ToUTF8().data() );
        info->set_pad_count( footprint->GetPadCount() );

        // Add pad details
        for( PAD* pad : footprint->Pads() )
        {
            PadInfo* padInfo = response.add_pads();
            padInfo->set_number( pad->GetNumber().ToUTF8().data() );

            kiapi::common::PackVector2( *padInfo->mutable_position(), pad->GetPosition() );

            VECTOR2I size = pad->GetSize( PADSTACK::ALL_LAYERS );
            kiapi::common::PackVector2( *padInfo->mutable_size(), size );

            padInfo->set_shape( static_cast<int>( pad->GetShape( PADSTACK::ALL_LAYERS ) ) );
        }

        // Bounding box
        BOX2I bbox = footprint->GetBoundingBox( false /* no invisible text */ );
        kiapi::common::PackBox2( *response.mutable_bounding_box(), bbox );
    }
    catch( const IO_ERROR& e )
    {
        ApiResponseStatus status;
        status.set_status( ApiStatusCode::AS_BAD_REQUEST );
        status.set_error_message( e.What().ToUTF8().data() );
        return tl::unexpected( status );
    }

    return response;
}


//
// Ratsnest Query
//

HANDLER_RESULT<GetRatsnestResponse> API_HANDLER_PCB::handleGetRatsnest(
        const HANDLER_CONTEXT<GetRatsnest>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    GetRatsnestResponse response;

    // Get connectivity data (calculates ratsnest)
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();

    if( !connectivity )
    {
        board->BuildConnectivity();
        connectivity = board->GetConnectivity();
    }

    // Build set of net codes to include
    std::set<int> targetNets;

    for( int netCode : aCtx.Request.net_codes() )
        targetNets.insert( netCode );

    bool filterByNet = !targetNets.empty();
    int totalUnrouted = 0;

    // Iterate through all nets
    for( unsigned int netCode = 1; netCode < board->GetNetCount(); netCode++ )
    {
        if( filterByNet && targetNets.find( static_cast<int>( netCode ) ) == targetNets.end() )
            continue;

        NETINFO_ITEM* netInfo = board->FindNet( static_cast<int>( netCode ) );

        if( !netInfo || netInfo->GetNetname().IsEmpty() )
            continue;

        // Get ratsnest for this net
        RN_NET* rnNet = connectivity->GetRatsnestForNet( netCode );

        if( !rnNet )
            continue;

        // Get unconnected edges (ratsnest lines)
        const std::vector<CN_EDGE>& edges = rnNet->GetEdges();

        for( const CN_EDGE& edge : edges )
        {
            // Skip if this is a routed connection (not visible = connected)
            if( !edge.IsVisible() )
                continue;

            std::shared_ptr<const CN_ANCHOR> source = edge.GetSourceNode();
            std::shared_ptr<const CN_ANCHOR> target = edge.GetTargetNode();

            if( !source || !target )
                continue;

            // Skip zone-to-zone if not requested
            if( !aCtx.Request.include_zones() )
            {
                if( source->Parent()->Type() == PCB_ZONE_T &&
                    target->Parent()->Type() == PCB_ZONE_T )
                    continue;
            }

            RatsnestLine* line = response.add_lines();

            line->set_net_code( netCode );
            line->set_net_name( netInfo->GetNetname().ToUTF8().data() );

            // Set item IDs
            if( source->Parent() )
            {
                line->mutable_pad1_id()->set_value( source->Parent()->m_Uuid.AsStdString() );
            }

            if( target->Parent() )
            {
                line->mutable_pad2_id()->set_value( target->Parent()->m_Uuid.AsStdString() );
            }

            // Set positions
            VECTOR2I startPos = source->Pos();
            VECTOR2I endPos = target->Pos();

            kiapi::common::PackVector2( *line->mutable_start(), startPos );
            kiapi::common::PackVector2( *line->mutable_end(), endPos );

            // Calculate length (Manhattan distance)
            int64_t length = std::abs( endPos.x - startPos.x ) +
                             std::abs( endPos.y - startPos.y );
            line->set_length( length );

            totalUnrouted++;
        }
    }

    response.set_total_unrouted( totalUnrouted );

    return response;
}


HANDLER_RESULT<GetUnroutedNetsResponse> API_HANDLER_PCB::handleGetUnroutedNets(
        const HANDLER_CONTEXT<GetUnroutedNets>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    GetUnroutedNetsResponse response;

    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();

    if( !connectivity )
    {
        board->BuildConnectivity();
        connectivity = board->GetConnectivity();
    }

    // Iterate through all nets
    for( unsigned int netCode = 1; netCode < board->GetNetCount(); netCode++ )
    {
        NETINFO_ITEM* netInfo = board->FindNet( static_cast<int>( netCode ) );

        if( !netInfo || netInfo->GetNetname().IsEmpty() )
            continue;

        // Count pads on this net
        int padCount = 0;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->GetNetCode() == static_cast<int>( netCode ) )
                    padCount++;
            }
        }

        // Skip single-pad nets (nothing to route)
        if( padCount < 2 )
            continue;

        // Get ratsnest to count unrouted connections
        RN_NET* rnNet = connectivity->GetRatsnestForNet( netCode );

        int unroutedCount = 0;

        if( rnNet )
        {
            for( const CN_EDGE& edge : rnNet->GetEdges() )
            {
                if( edge.IsVisible() )  // Visible = unrouted
                    unroutedCount++;
            }
        }

        // A fully connected net with N pads needs N-1 connections
        int totalConnections = padCount - 1;
        int routedConnections = totalConnections - unroutedCount;

        // Only include nets that have unrouted connections
        if( unroutedCount > 0 )
        {
            UnroutedNetInfo* netProto = response.add_nets();

            netProto->set_net_code( netCode );
            netProto->set_net_name( netInfo->GetNetname().ToUTF8().data() );
            netProto->set_total_pads( padCount );
            netProto->set_routed_connections( routedConnections );
            netProto->set_unrouted_connections( unroutedCount );
            netProto->set_is_complete( false );
        }
    }

    return response;
}


HANDLER_RESULT<GetConnectivityStatusResponse> API_HANDLER_PCB::handleGetConnectivityStatus(
        const HANDLER_CONTEXT<GetConnectivityStatus>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    GetConnectivityStatusResponse response;

    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();

    if( !connectivity )
    {
        board->BuildConnectivity();
        connectivity = board->GetConnectivity();
    }

    const auto& itemCache = board->GetItemByIdCache();

    for( const auto& kiidProto : aCtx.Request.item_ids() )
    {
        KIID kiid( kiidProto.value() );
        auto it = itemCache.find( kiid );
        BOARD_ITEM* item = ( it != itemCache.end() ) ? it->second : nullptr;

        if( !item )
            continue;

        // Only process connectable items
        BOARD_CONNECTED_ITEM* connItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( item );

        if( !connItem )
            continue;

        ItemConnectivity* itemStatus = response.add_items();

        itemStatus->mutable_item_id()->set_value( kiid.AsStdString() );
        itemStatus->set_net_code( connItem->GetNetCode() );

        NETINFO_ITEM* netInfo = connItem->GetNet();

        if( netInfo )
            itemStatus->set_net_name( netInfo->GetNetname().ToUTF8().data() );

        // Get items connected to this one
        const std::vector<BOARD_CONNECTED_ITEM*> connected =
            connectivity->GetConnectedItems( connItem );

        for( BOARD_CONNECTED_ITEM* conn : connected )
        {
            itemStatus->add_connected_items()->set_value( conn->m_Uuid.AsStdString() );
        }

        // Find items on same net but not connected (through ratsnest)
        if( netInfo && connItem->GetNetCode() > 0 )
        {
            RN_NET* rnNet = connectivity->GetRatsnestForNet( connItem->GetNetCode() );

            if( rnNet )
            {
                // Find edges involving this item
                for( const CN_EDGE& edge : rnNet->GetEdges() )
                {
                    if( !edge.IsVisible() )
                        continue;

                    std::shared_ptr<const CN_ANCHOR> source = edge.GetSourceNode();
                    std::shared_ptr<const CN_ANCHOR> target = edge.GetTargetNode();

                    if( source && source->Parent() == connItem && target && target->Parent() )
                    {
                        itemStatus->add_unconnected_items()->set_value(
                            target->Parent()->m_Uuid.AsStdString() );
                    }
                    else if( target && target->Parent() == connItem && source && source->Parent() )
                    {
                        itemStatus->add_unconnected_items()->set_value(
                            source->Parent()->m_Uuid.AsStdString() );
                    }
                }
            }
        }
    }

    return response;
}


//
// Group Operations
//

HANDLER_RESULT<GetGroupsResponse> API_HANDLER_PCB::handleGetGroups(
        const HANDLER_CONTEXT<GetGroups>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    GetGroupsResponse response;

    for( PCB_GROUP* group : board->Groups() )
    {
        GroupInfo* info = response.add_groups();

        info->mutable_id()->set_value( group->m_Uuid.AsStdString() );
        info->set_name( group->GetName().ToUTF8().data() );
        info->set_locked( group->IsLocked() );

        // Add member IDs
        for( EDA_ITEM* member : group->GetItems() )
        {
            info->add_member_ids()->set_value( member->m_Uuid.AsStdString() );
        }
    }

    return response;
}


HANDLER_RESULT<CreateGroupResponse> API_HANDLER_PCB::handleCreateGroup(
        const HANDLER_CONTEXT<CreateGroup>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    CreateGroupResponse response;

    // Create the group
    PCB_GROUP* group = new PCB_GROUP( board );
    group->SetName( wxString::FromUTF8( aCtx.Request.name() ) );

    // Collect items to add
    std::vector<BOARD_ITEM*> itemsToAdd;
    const auto& itemCache = board->GetItemByIdCache();

    for( const auto& kiidProto : aCtx.Request.member_ids() )
    {
        KIID kiid( kiidProto.value() );
        auto it = itemCache.find( kiid );
        BOARD_ITEM* item = ( it != itemCache.end() ) ? it->second : nullptr;

        if( item && item != board && item->Type() != PCB_GROUP_T )
        {
            // Check item isn't already in another group
            if( item->GetParentGroup() == nullptr )
            {
                itemsToAdd.push_back( item );
            }
        }
    }

    // Use a commit for undo support
    BOARD_COMMIT commit( frame() );

    commit.Add( group );

    // Add items to the group
    for( BOARD_ITEM* item : itemsToAdd )
    {
        commit.Modify( item );
        group->AddItem( item );
    }

    commit.Push( wxString::Format( _( "Create group '%s'" ), group->GetName() ) );

    // Build response
    response.mutable_group_id()->set_value( group->m_Uuid.AsStdString() );

    GroupInfo* info = response.mutable_group();
    info->mutable_id()->set_value( group->m_Uuid.AsStdString() );
    info->set_name( group->GetName().ToUTF8().data() );
    info->set_locked( group->IsLocked() );

    for( EDA_ITEM* member : group->GetItems() )
    {
        info->add_member_ids()->set_value( member->m_Uuid.AsStdString() );
    }

    return response;
}


HANDLER_RESULT<DeleteGroupResponse> API_HANDLER_PCB::handleDeleteGroup(
        const HANDLER_CONTEXT<DeleteGroup>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    KIID groupId( aCtx.Request.group_id().value() );
    const auto& itemCache = board->GetItemByIdCache();
    auto it = itemCache.find( groupId );
    BOARD_ITEM* item = ( it != itemCache.end() ) ? it->second : nullptr;

    if( !item || item->Type() != PCB_GROUP_T )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Group not found" );
        return tl::unexpected( e );
    }

    PCB_GROUP* group = static_cast<PCB_GROUP*>( item );

    BOARD_COMMIT commit( frame() );

    if( aCtx.Request.ungroup_members() )
    {
        // Remove all items from group first
        for( EDA_ITEM* member : group->GetItems() )
        {
            commit.Modify( static_cast<BOARD_ITEM*>( member ) );
        }

        group->RemoveAll();
    }

    commit.Remove( group );
    commit.Push( wxString::Format( _( "Delete group '%s'" ), group->GetName() ) );

    DeleteGroupResponse response;
    response.set_success( true );

    return response;
}


HANDLER_RESULT<AddToGroupResponse> API_HANDLER_PCB::handleAddToGroup(
        const HANDLER_CONTEXT<AddToGroup>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    KIID groupId( aCtx.Request.group_id().value() );
    const auto& itemCache = board->GetItemByIdCache();
    auto groupIt = itemCache.find( groupId );
    BOARD_ITEM* item = ( groupIt != itemCache.end() ) ? groupIt->second : nullptr;

    if( !item || item->Type() != PCB_GROUP_T )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Group not found" );
        return tl::unexpected( e );
    }

    PCB_GROUP* group = static_cast<PCB_GROUP*>( item );

    AddToGroupResponse response;

    BOARD_COMMIT commit( frame() );
    commit.Modify( group );

    int addedCount = 0;

    for( const auto& kiidProto : aCtx.Request.item_ids() )
    {
        KIID kiid( kiidProto.value() );
        auto it = itemCache.find( kiid );
        BOARD_ITEM* itemToAdd = ( it != itemCache.end() ) ? it->second : nullptr;

        if( !itemToAdd || itemToAdd == board || itemToAdd->Type() == PCB_GROUP_T )
        {
            response.add_failed_ids()->set_value( kiid.AsStdString() );
            continue;
        }

        // Can't add if already in a group
        if( itemToAdd->GetParentGroup() != nullptr )
        {
            response.add_failed_ids()->set_value( kiid.AsStdString() );
            continue;
        }

        commit.Modify( itemToAdd );
        group->AddItem( itemToAdd );
        addedCount++;
    }

    commit.Push( wxString::Format( _( "Add items to group '%s'" ), group->GetName() ) );

    response.set_items_added( addedCount );

    return response;
}


HANDLER_RESULT<RemoveFromGroupResponse> API_HANDLER_PCB::handleRemoveFromGroup(
        const HANDLER_CONTEXT<RemoveFromGroup>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    KIID groupId( aCtx.Request.group_id().value() );
    const auto& itemCache = board->GetItemByIdCache();
    auto groupIt = itemCache.find( groupId );
    BOARD_ITEM* item = ( groupIt != itemCache.end() ) ? groupIt->second : nullptr;

    if( !item || item->Type() != PCB_GROUP_T )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Group not found" );
        return tl::unexpected( e );
    }

    PCB_GROUP* group = static_cast<PCB_GROUP*>( item );

    RemoveFromGroupResponse response;

    BOARD_COMMIT commit( frame() );
    commit.Modify( group );

    int removedCount = 0;

    for( const auto& kiidProto : aCtx.Request.item_ids() )
    {
        KIID kiid( kiidProto.value() );
        auto it = itemCache.find( kiid );
        BOARD_ITEM* itemToRemove = ( it != itemCache.end() ) ? it->second : nullptr;

        if( !itemToRemove )
            continue;

        // Check item is actually in this group
        if( itemToRemove->GetParentGroup() != group )
            continue;

        commit.Modify( itemToRemove );
        group->RemoveItem( itemToRemove );
        removedCount++;
    }

    commit.Push( wxString::Format( _( "Remove items from group '%s'" ), group->GetName() ) );

    response.set_items_removed( removedCount );

    return response;
}


HANDLER_RESULT<GetGroupMembersResponse> API_HANDLER_PCB::handleGetGroupMembers(
        const HANDLER_CONTEXT<GetGroupMembers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    KIID groupId( aCtx.Request.group_id().value() );
    const auto& itemCache = board->GetItemByIdCache();
    auto it = itemCache.find( groupId );
    BOARD_ITEM* item = ( it != itemCache.end() ) ? it->second : nullptr;

    if( !item || item->Type() != PCB_GROUP_T )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Group not found" );
        return tl::unexpected( e );
    }

    PCB_GROUP* group = static_cast<PCB_GROUP*>( item );

    GetGroupMembersResponse response;

    for( EDA_ITEM* member : group->GetItems() )
    {
        BOARD_ITEM* boardMember = static_cast<BOARD_ITEM*>( member );

        // Serialize each item using the BOARD_ITEM::Serialize method
        google::protobuf::Any itemAny;
        boardMember->Serialize( itemAny );
        response.add_items()->CopyFrom( itemAny );
    }

    return response;
}


HANDLER_RESULT<ZoneHatchOffsetsResponse> API_HANDLER_PCB::handleGetZoneHatchOffsets(
        const HANDLER_CONTEXT<GetZoneHatchOffsets>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    ZoneHatchOffsetsResponse response;

    // Iterate through all enabled copper layers
    for( PCB_LAYER_ID layer : LSET::AllCuMask().UIOrder() )
    {
        if( !bds.IsLayerEnabled( layer ) )
            continue;

        ZoneHatchOffset* offset = response.add_layer_offsets();
        offset->set_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( layer ) );

        if( bds.m_ZoneLayerProperties.contains( layer ) )
        {
            const ZONE_LAYER_PROPERTIES& props = bds.m_ZoneLayerProperties.at( layer );
            if( props.hatching_offset.has_value() )
            {
                offset->mutable_offset()->set_x_nm( props.hatching_offset->x );
                offset->mutable_offset()->set_y_nm( props.hatching_offset->y );
            }
            else
            {
                offset->mutable_offset()->set_x_nm( 0 );
                offset->mutable_offset()->set_y_nm( 0 );
            }
        }
        else
        {
            offset->mutable_offset()->set_x_nm( 0 );
            offset->mutable_offset()->set_y_nm( 0 );
        }
    }

    return response;
}


HANDLER_RESULT<ZoneHatchOffsetsResponse> API_HANDLER_PCB::handleSetZoneHatchOffsets(
        const HANDLER_CONTEXT<SetZoneHatchOffsets>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    // Update only the specified layers
    for( const ZoneHatchOffset& layerOffset : aCtx.Request.layer_offsets() )
    {
        PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>(
                layerOffset.layer() );

        if( !IsCopperLayer( layer ) )
            continue;

        ZONE_LAYER_PROPERTIES& props = bds.m_ZoneLayerProperties[layer];
        props.hatching_offset = VECTOR2I( layerOffset.offset().x_nm(), layerOffset.offset().y_nm() );
    }

    frame()->OnModify();

    // Return updated offsets
    return handleGetZoneHatchOffsets(
            HANDLER_CONTEXT<GetZoneHatchOffsets>{ aCtx.ClientName, GetZoneHatchOffsets() } );
}


HANDLER_RESULT<DimensionDefaultsResponse> API_HANDLER_PCB::handleGetDimensionDefaults(
        const HANDLER_CONTEXT<GetDimensionDefaults>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    DimensionDefaultsResponse response;

    // Map DIM_UNITS_MODE to proto enum
    switch( bds.m_DimensionUnitsMode )
    {
    case DIM_UNITS_MODE::INCH:      response.set_units_mode( DimensionUnitsMode::DUM_INCHES ); break;
    case DIM_UNITS_MODE::MILS:      response.set_units_mode( DimensionUnitsMode::DUM_MILS ); break;
    case DIM_UNITS_MODE::MM:        response.set_units_mode( DimensionUnitsMode::DUM_MILLIMETERS ); break;
    case DIM_UNITS_MODE::AUTOMATIC: response.set_units_mode( DimensionUnitsMode::DUM_AUTOMATIC ); break;
    }

    // Map DIM_UNITS_FORMAT to proto enum
    switch( bds.m_DimensionUnitsFormat )
    {
    case DIM_UNITS_FORMAT::NO_SUFFIX:    response.set_units_format( DimensionUnitsFormat::DUF_NO_SUFFIX ); break;
    case DIM_UNITS_FORMAT::BARE_SUFFIX:  response.set_units_format( DimensionUnitsFormat::DUF_BARE_SUFFIX ); break;
    case DIM_UNITS_FORMAT::PAREN_SUFFIX: response.set_units_format( DimensionUnitsFormat::DUF_PAREN_SUFFIX ); break;
    }

    // Map DIM_PRECISION to proto enum
    response.set_precision( static_cast<DimensionPrecision>( static_cast<int>( bds.m_DimensionPrecision ) ) );

    response.set_suppress_zeroes( bds.m_DimensionSuppressZeroes );

    // Map DIM_TEXT_POSITION to proto enum
    switch( bds.m_DimensionTextPosition )
    {
    case DIM_TEXT_POSITION::OUTSIDE: response.set_text_position( DimensionTextPosition::DTP_OUTSIDE ); break;
    case DIM_TEXT_POSITION::INLINE:  response.set_text_position( DimensionTextPosition::DTP_INLINE ); break;
    case DIM_TEXT_POSITION::MANUAL:  response.set_text_position( DimensionTextPosition::DTP_MANUAL ); break;
    }

    response.set_keep_text_aligned( bds.m_DimensionKeepTextAligned );
    response.mutable_arrow_length()->set_value_nm( bds.m_DimensionArrowLength );
    response.mutable_extension_offset()->set_value_nm( bds.m_DimensionExtensionOffset );

    return response;
}


HANDLER_RESULT<DimensionDefaultsResponse> API_HANDLER_PCB::handleSetDimensionDefaults(
        const HANDLER_CONTEXT<SetDimensionDefaults>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    if( aCtx.Request.has_units_mode() )
    {
        switch( aCtx.Request.units_mode() )
        {
        case DimensionUnitsMode::DUM_INCHES:      bds.m_DimensionUnitsMode = DIM_UNITS_MODE::INCH; break;
        case DimensionUnitsMode::DUM_MILS:        bds.m_DimensionUnitsMode = DIM_UNITS_MODE::MILS; break;
        case DimensionUnitsMode::DUM_MILLIMETERS: bds.m_DimensionUnitsMode = DIM_UNITS_MODE::MM; break;
        case DimensionUnitsMode::DUM_AUTOMATIC:   bds.m_DimensionUnitsMode = DIM_UNITS_MODE::AUTOMATIC; break;
        }
    }

    if( aCtx.Request.has_units_format() )
    {
        switch( aCtx.Request.units_format() )
        {
        case DimensionUnitsFormat::DUF_NO_SUFFIX:    bds.m_DimensionUnitsFormat = DIM_UNITS_FORMAT::NO_SUFFIX; break;
        case DimensionUnitsFormat::DUF_BARE_SUFFIX:  bds.m_DimensionUnitsFormat = DIM_UNITS_FORMAT::BARE_SUFFIX; break;
        case DimensionUnitsFormat::DUF_PAREN_SUFFIX: bds.m_DimensionUnitsFormat = DIM_UNITS_FORMAT::PAREN_SUFFIX; break;
        }
    }

    if( aCtx.Request.has_precision() )
    {
        bds.m_DimensionPrecision = static_cast<DIM_PRECISION>( aCtx.Request.precision() );
    }

    if( aCtx.Request.has_suppress_zeroes() )
    {
        bds.m_DimensionSuppressZeroes = aCtx.Request.suppress_zeroes();
    }

    if( aCtx.Request.has_text_position() )
    {
        switch( aCtx.Request.text_position() )
        {
        case DimensionTextPosition::DTP_OUTSIDE: bds.m_DimensionTextPosition = DIM_TEXT_POSITION::OUTSIDE; break;
        case DimensionTextPosition::DTP_INLINE:  bds.m_DimensionTextPosition = DIM_TEXT_POSITION::INLINE; break;
        case DimensionTextPosition::DTP_MANUAL:  bds.m_DimensionTextPosition = DIM_TEXT_POSITION::MANUAL; break;
        }
    }

    if( aCtx.Request.has_keep_text_aligned() )
    {
        bds.m_DimensionKeepTextAligned = aCtx.Request.keep_text_aligned();
    }

    if( aCtx.Request.has_arrow_length() )
    {
        bds.m_DimensionArrowLength = aCtx.Request.arrow_length().value_nm();
    }

    if( aCtx.Request.has_extension_offset() )
    {
        bds.m_DimensionExtensionOffset = aCtx.Request.extension_offset().value_nm();
    }

    frame()->OnModify();

    // Return updated defaults
    return handleGetDimensionDefaults(
            HANDLER_CONTEXT<GetDimensionDefaults>{ aCtx.ClientName, GetDimensionDefaults() } );
}


HANDLER_RESULT<ZoneDefaultsResponse> API_HANDLER_PCB::handleGetZoneDefaults(
        const HANDLER_CONTEXT<GetZoneDefaults>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    const ZONE_SETTINGS& zs = bds.GetDefaultZoneSettings();
    ZoneDefaultsResponse response;

    response.set_name( zs.m_Name.ToStdString() );
    response.set_locked( zs.m_Locked );
    response.set_priority( zs.m_ZonePriority );

    // Map corner smoothing type
    switch( zs.GetCornerSmoothingType() )
    {
    case ZONE_SETTINGS::SMOOTHING_NONE:    response.set_corner_smoothing( ZoneCornerSmoothing::ZCS_NONE ); break;
    case ZONE_SETTINGS::SMOOTHING_CHAMFER: response.set_corner_smoothing( ZoneCornerSmoothing::ZCS_CHAMFER ); break;
    case ZONE_SETTINGS::SMOOTHING_FILLET:  response.set_corner_smoothing( ZoneCornerSmoothing::ZCS_FILLET ); break;
    default:                               response.set_corner_smoothing( ZoneCornerSmoothing::ZCS_NONE ); break;
    }

    response.mutable_corner_radius()->set_value_nm( zs.GetCornerRadius() );
    response.mutable_clearance()->set_value_nm( zs.m_ZoneClearance );
    response.mutable_min_thickness()->set_value_nm( zs.m_ZoneMinThickness );

    // Map pad connection type
    switch( zs.GetPadConnection() )
    {
    case ZONE_CONNECTION::INHERITED:   response.set_pad_connection( ZonePadConnection::ZPC_INHERITED ); break;
    case ZONE_CONNECTION::NONE:        response.set_pad_connection( ZonePadConnection::ZPC_NONE ); break;
    case ZONE_CONNECTION::THERMAL:     response.set_pad_connection( ZonePadConnection::ZPC_THERMAL ); break;
    case ZONE_CONNECTION::FULL:        response.set_pad_connection( ZonePadConnection::ZPC_SOLID ); break;
    case ZONE_CONNECTION::THT_THERMAL: response.set_pad_connection( ZonePadConnection::ZPC_THT_THERMAL ); break;
    }

    response.mutable_thermal_gap()->set_value_nm( zs.m_ThermalReliefGap );
    response.mutable_thermal_spoke_width()->set_value_nm( zs.m_ThermalReliefSpokeWidth );

    // Map island removal mode
    switch( zs.GetIslandRemovalMode() )
    {
    case ISLAND_REMOVAL_MODE::ALWAYS: response.set_island_removal( ZoneIslandRemoval::ZIR_ALWAYS ); break;
    case ISLAND_REMOVAL_MODE::NEVER:  response.set_island_removal( ZoneIslandRemoval::ZIR_NEVER ); break;
    case ISLAND_REMOVAL_MODE::AREA:   response.set_island_removal( ZoneIslandRemoval::ZIR_AREA ); break;
    }

    response.set_min_island_area_nm2( zs.GetMinIslandArea() );

    return response;
}


HANDLER_RESULT<ZoneDefaultsResponse> API_HANDLER_PCB::handleSetZoneDefaults(
        const HANDLER_CONTEXT<SetZoneDefaults>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    ZONE_SETTINGS zs = bds.GetDefaultZoneSettings();

    if( aCtx.Request.has_name() )
    {
        zs.m_Name = wxString::FromUTF8( aCtx.Request.name() );
    }

    if( aCtx.Request.has_locked() )
    {
        zs.m_Locked = aCtx.Request.locked();
    }

    if( aCtx.Request.has_priority() )
    {
        zs.m_ZonePriority = aCtx.Request.priority();
    }

    if( aCtx.Request.has_corner_smoothing() )
    {
        switch( aCtx.Request.corner_smoothing() )
        {
        case ZoneCornerSmoothing::ZCS_NONE:    zs.SetCornerSmoothingType( ZONE_SETTINGS::SMOOTHING_NONE ); break;
        case ZoneCornerSmoothing::ZCS_CHAMFER: zs.SetCornerSmoothingType( ZONE_SETTINGS::SMOOTHING_CHAMFER ); break;
        case ZoneCornerSmoothing::ZCS_FILLET:  zs.SetCornerSmoothingType( ZONE_SETTINGS::SMOOTHING_FILLET ); break;
        }
    }

    if( aCtx.Request.has_corner_radius() )
    {
        zs.SetCornerRadius( aCtx.Request.corner_radius().value_nm() );
    }

    if( aCtx.Request.has_clearance() )
    {
        zs.m_ZoneClearance = aCtx.Request.clearance().value_nm();
    }

    if( aCtx.Request.has_min_thickness() )
    {
        zs.m_ZoneMinThickness = aCtx.Request.min_thickness().value_nm();
    }

    if( aCtx.Request.has_pad_connection() )
    {
        switch( aCtx.Request.pad_connection() )
        {
        case ZonePadConnection::ZPC_INHERITED:   zs.SetPadConnection( ZONE_CONNECTION::INHERITED ); break;
        case ZonePadConnection::ZPC_NONE:        zs.SetPadConnection( ZONE_CONNECTION::NONE ); break;
        case ZonePadConnection::ZPC_THERMAL:     zs.SetPadConnection( ZONE_CONNECTION::THERMAL ); break;
        case ZonePadConnection::ZPC_SOLID:       zs.SetPadConnection( ZONE_CONNECTION::FULL ); break;
        case ZonePadConnection::ZPC_THT_THERMAL: zs.SetPadConnection( ZONE_CONNECTION::THT_THERMAL ); break;
        }
    }

    if( aCtx.Request.has_thermal_gap() )
    {
        zs.m_ThermalReliefGap = aCtx.Request.thermal_gap().value_nm();
    }

    if( aCtx.Request.has_thermal_spoke_width() )
    {
        zs.m_ThermalReliefSpokeWidth = aCtx.Request.thermal_spoke_width().value_nm();
    }

    if( aCtx.Request.has_island_removal() )
    {
        switch( aCtx.Request.island_removal() )
        {
        case ZoneIslandRemoval::ZIR_ALWAYS: zs.SetIslandRemovalMode( ISLAND_REMOVAL_MODE::ALWAYS ); break;
        case ZoneIslandRemoval::ZIR_NEVER:  zs.SetIslandRemovalMode( ISLAND_REMOVAL_MODE::NEVER ); break;
        case ZoneIslandRemoval::ZIR_AREA:   zs.SetIslandRemovalMode( ISLAND_REMOVAL_MODE::AREA ); break;
        }
    }

    if( aCtx.Request.has_min_island_area_nm2() )
    {
        zs.SetMinIslandArea( aCtx.Request.min_island_area_nm2() );
    }

    bds.SetDefaultZoneSettings( zs );
    frame()->OnModify();

    // Return updated defaults
    return handleGetZoneDefaults(
            HANDLER_CONTEXT<GetZoneDefaults>{ aCtx.ClientName, GetZoneDefaults() } );
}


HANDLER_RESULT<PreDefinedSizesResponse> API_HANDLER_PCB::handleGetPreDefinedSizes(
        const HANDLER_CONTEXT<GetPreDefinedSizes>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    PreDefinedSizesResponse response;
    PreDefinedSizes* sizes = response.mutable_sizes();

    // Track widths (skip index 0 which is the netclass default)
    for( size_t i = 1; i < bds.m_TrackWidthList.size(); ++i )
    {
        sizes->add_track_widths_nm( bds.m_TrackWidthList[i] );
    }

    // Via sizes (skip index 0 which is the netclass default)
    for( size_t i = 1; i < bds.m_ViasDimensionsList.size(); ++i )
    {
        ViaDimension* via = sizes->add_via_sizes();
        via->set_diameter_nm( bds.m_ViasDimensionsList[i].m_Diameter );
        via->set_drill_nm( bds.m_ViasDimensionsList[i].m_Drill );
    }

    // Diff pair dimensions (skip index 0 which is the netclass default)
    for( size_t i = 1; i < bds.m_DiffPairDimensionsList.size(); ++i )
    {
        DiffPairDimension* dp = sizes->add_diff_pairs();
        dp->set_width_nm( bds.m_DiffPairDimensionsList[i].m_Width );
        dp->set_gap_nm( bds.m_DiffPairDimensionsList[i].m_Gap );
        dp->set_via_gap_nm( bds.m_DiffPairDimensionsList[i].m_ViaGap );
    }

    return response;
}


HANDLER_RESULT<PreDefinedSizesResponse> API_HANDLER_PCB::handleSetPreDefinedSizes(
        const HANDLER_CONTEXT<SetPreDefinedSizes>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    const PreDefinedSizes& sizes = aCtx.Request.sizes();

    // Update track widths - preserve index 0 (netclass default)
    // Note: We always update if the sizes message is present (even if empty arrays).
    // Empty arrays mean "clear to just the netclass default".
    // Keep the first entry (netclass default)
    int defaultWidth = bds.m_TrackWidthList.empty() ? 0 : bds.m_TrackWidthList[0];
    bds.m_TrackWidthList.clear();
    bds.m_TrackWidthList.push_back( defaultWidth );

    for( int i = 0; i < sizes.track_widths_nm_size(); ++i )
    {
        bds.m_TrackWidthList.push_back( sizes.track_widths_nm( i ) );
    }

    // Update via sizes - preserve index 0 (netclass default)
    VIA_DIMENSION defaultVia = bds.m_ViasDimensionsList.empty() ?
                               VIA_DIMENSION() : bds.m_ViasDimensionsList[0];
    bds.m_ViasDimensionsList.clear();
    bds.m_ViasDimensionsList.push_back( defaultVia );

    for( int i = 0; i < sizes.via_sizes_size(); ++i )
    {
        const ViaDimension& via = sizes.via_sizes( i );
        bds.m_ViasDimensionsList.push_back(
                VIA_DIMENSION( via.diameter_nm(), via.drill_nm() ) );
    }

    // Update diff pair dimensions - preserve index 0 (netclass default)
    DIFF_PAIR_DIMENSION defaultDp = bds.m_DiffPairDimensionsList.empty() ?
                                    DIFF_PAIR_DIMENSION() : bds.m_DiffPairDimensionsList[0];
    bds.m_DiffPairDimensionsList.clear();
    bds.m_DiffPairDimensionsList.push_back( defaultDp );

    for( int i = 0; i < sizes.diff_pairs_size(); ++i )
    {
        const DiffPairDimension& dp = sizes.diff_pairs( i );
        bds.m_DiffPairDimensionsList.push_back(
                DIFF_PAIR_DIMENSION( dp.width_nm(), dp.gap_nm(), dp.via_gap_nm() ) );
    }

    frame()->OnModify();

    // Return updated sizes
    return handleGetPreDefinedSizes(
            HANDLER_CONTEXT<GetPreDefinedSizes>{ aCtx.ClientName, GetPreDefinedSizes() } );
}


// Helper to populate TeardropParameters proto from TEARDROP_PARAMETERS
static void populateTeardropParams( TeardropParameters* aProto, const TEARDROP_PARAMETERS* aParams )
{
    aProto->set_best_length_ratio( aParams->m_BestLengthRatio );
    aProto->set_max_length_nm( aParams->m_TdMaxLen );
    aProto->set_best_width_ratio( aParams->m_BestWidthRatio );
    aProto->set_max_width_nm( aParams->m_TdMaxWidth );
    aProto->set_curved_edges( aParams->m_CurvedEdges );
    aProto->set_allow_two_segments( aParams->m_AllowUseTwoTracks );
    aProto->set_prefer_zone_connection( aParams->m_TdOnPadsInZones );
    aProto->set_track_width_limit_ratio( aParams->m_WidthtoSizeFilterRatio );
}


// Helper to apply TeardropParameters proto to TEARDROP_PARAMETERS
// Only applies fields that were explicitly set in the proto (partial update support)
static void applyTeardropParams( TEARDROP_PARAMETERS* aParams, const TeardropParameters& aProto )
{
    if( aProto.has_best_length_ratio() )
        aParams->m_BestLengthRatio = aProto.best_length_ratio();
    if( aProto.has_max_length_nm() )
        aParams->m_TdMaxLen = aProto.max_length_nm();
    if( aProto.has_best_width_ratio() )
        aParams->m_BestWidthRatio = aProto.best_width_ratio();
    if( aProto.has_max_width_nm() )
        aParams->m_TdMaxWidth = aProto.max_width_nm();
    if( aProto.has_curved_edges() )
        aParams->m_CurvedEdges = aProto.curved_edges();
    if( aProto.has_allow_two_segments() )
        aParams->m_AllowUseTwoTracks = aProto.allow_two_segments();
    if( aProto.has_prefer_zone_connection() )
        aParams->m_TdOnPadsInZones = aProto.prefer_zone_connection();
    if( aProto.has_track_width_limit_ratio() )
        aParams->m_WidthtoSizeFilterRatio = aProto.track_width_limit_ratio();
}


HANDLER_RESULT<TeardropSettingsResponse> API_HANDLER_PCB::handleGetTeardropSettings(
        const HANDLER_CONTEXT<GetTeardropSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    TEARDROP_PARAMETERS_LIST* tdParams = bds.GetTeadropParamsList();

    TeardropSettingsResponse response;
    TeardropSettings* settings = response.mutable_settings();

    // Global flags
    settings->set_target_vias( tdParams->m_TargetVias );
    settings->set_target_pth_pads( tdParams->m_TargetPTHPads );
    settings->set_target_smd_pads( tdParams->m_TargetSMDPads );
    settings->set_target_track_to_track( tdParams->m_TargetTrack2Track );
    settings->set_round_shapes_only( tdParams->m_UseRoundShapesOnly );

    // Per-type parameters
    populateTeardropParams( settings->mutable_round_shapes(),
                            tdParams->GetParameters( TARGET_ROUND ) );
    populateTeardropParams( settings->mutable_rect_shapes(),
                            tdParams->GetParameters( TARGET_RECT ) );
    populateTeardropParams( settings->mutable_track_to_track(),
                            tdParams->GetParameters( TARGET_TRACK ) );

    return response;
}


HANDLER_RESULT<TeardropSettingsResponse> API_HANDLER_PCB::handleSetTeardropSettings(
        const HANDLER_CONTEXT<SetTeardropSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    TEARDROP_PARAMETERS_LIST* tdParams = bds.GetTeadropParamsList();

    // Update global flags (only if provided)
    if( aCtx.Request.has_target_vias() )
        tdParams->m_TargetVias = aCtx.Request.target_vias();

    if( aCtx.Request.has_target_pth_pads() )
        tdParams->m_TargetPTHPads = aCtx.Request.target_pth_pads();

    if( aCtx.Request.has_target_smd_pads() )
        tdParams->m_TargetSMDPads = aCtx.Request.target_smd_pads();

    if( aCtx.Request.has_target_track_to_track() )
        tdParams->m_TargetTrack2Track = aCtx.Request.target_track_to_track();

    if( aCtx.Request.has_round_shapes_only() )
        tdParams->m_UseRoundShapesOnly = aCtx.Request.round_shapes_only();

    // Update per-type parameters (only if provided)
    if( aCtx.Request.has_round_shapes() )
        applyTeardropParams( tdParams->GetParameters( TARGET_ROUND ), aCtx.Request.round_shapes() );

    if( aCtx.Request.has_rect_shapes() )
        applyTeardropParams( tdParams->GetParameters( TARGET_RECT ), aCtx.Request.rect_shapes() );

    if( aCtx.Request.has_track_to_track() )
        applyTeardropParams( tdParams->GetParameters( TARGET_TRACK ), aCtx.Request.track_to_track() );

    frame()->OnModify();

    // Return updated settings
    return handleGetTeardropSettings(
            HANDLER_CONTEXT<GetTeardropSettings>{ aCtx.ClientName, GetTeardropSettings() } );
}


// Helper to populate MeanderPatternSettings proto from PNS::MEANDER_SETTINGS
static void populateMeanderSettings( MeanderPatternSettings* aProto, const PNS::MEANDER_SETTINGS& aSettings )
{
    aProto->set_min_amplitude_nm( aSettings.m_minAmplitude );
    aProto->set_max_amplitude_nm( aSettings.m_maxAmplitude );
    aProto->set_spacing_nm( aSettings.m_spacing );

    // Map KiCad MEANDER_STYLE to proto MeanderCornerStyle
    switch( aSettings.m_cornerStyle )
    {
    case PNS::MEANDER_STYLE_ROUND:   aProto->set_corner_style( MeanderCornerStyle::MCS_ROUND ); break;
    case PNS::MEANDER_STYLE_CHAMFER: aProto->set_corner_style( MeanderCornerStyle::MCS_CHAMFER ); break;
    default:                         aProto->set_corner_style( MeanderCornerStyle::MCS_ROUND ); break;
    }

    aProto->set_corner_radius_percent( aSettings.m_cornerRadiusPercentage );
    aProto->set_single_sided( aSettings.m_singleSided );
}


// Helper to apply MeanderPatternSettings proto to PNS::MEANDER_SETTINGS
// Only applies fields that were explicitly set in the proto (partial update support)
static void applyMeanderSettings( PNS::MEANDER_SETTINGS* aSettings, const MeanderPatternSettings& aProto )
{
    if( aProto.has_min_amplitude_nm() )
        aSettings->m_minAmplitude = aProto.min_amplitude_nm();
    if( aProto.has_max_amplitude_nm() )
        aSettings->m_maxAmplitude = aProto.max_amplitude_nm();
    if( aProto.has_spacing_nm() )
        aSettings->m_spacing = aProto.spacing_nm();

    // Map proto MeanderCornerStyle to KiCad MEANDER_STYLE
    if( aProto.has_corner_style() )
    {
        switch( aProto.corner_style() )
        {
        case MeanderCornerStyle::MCS_ROUND:   aSettings->m_cornerStyle = PNS::MEANDER_STYLE_ROUND; break;
        case MeanderCornerStyle::MCS_CHAMFER: aSettings->m_cornerStyle = PNS::MEANDER_STYLE_CHAMFER; break;
        default:                              aSettings->m_cornerStyle = PNS::MEANDER_STYLE_ROUND; break;
        }
    }

    if( aProto.has_corner_radius_percent() )
        aSettings->m_cornerRadiusPercentage = aProto.corner_radius_percent();
    if( aProto.has_single_sided() )
        aSettings->m_singleSided = aProto.single_sided();
}


HANDLER_RESULT<LengthTuningPatternSettingsResponse> API_HANDLER_PCB::handleGetLengthTuningPatternSettings(
        const HANDLER_CONTEXT<GetLengthTuningPatternSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    LengthTuningPatternSettingsResponse response;
    LengthTuningPatternSettings* settings = response.mutable_settings();

    // Single track meander settings
    populateMeanderSettings( settings->mutable_single_track(), bds.m_SingleTrackMeanderSettings );

    // Differential pair meander settings
    populateMeanderSettings( settings->mutable_diff_pair(), bds.m_DiffPairMeanderSettings );

    // Differential pair skew meander settings
    populateMeanderSettings( settings->mutable_diff_pair_skew(), bds.m_SkewMeanderSettings );

    return response;
}


HANDLER_RESULT<LengthTuningPatternSettingsResponse> API_HANDLER_PCB::handleSetLengthTuningPatternSettings(
        const HANDLER_CONTEXT<SetLengthTuningPatternSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( !board )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No board loaded" );
        return tl::unexpected( e );
    }

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    // Update single track meander settings (only if provided)
    if( aCtx.Request.has_single_track() )
        applyMeanderSettings( &bds.m_SingleTrackMeanderSettings, aCtx.Request.single_track() );

    // Update differential pair meander settings (only if provided)
    if( aCtx.Request.has_diff_pair() )
        applyMeanderSettings( &bds.m_DiffPairMeanderSettings, aCtx.Request.diff_pair() );

    // Update differential pair skew meander settings (only if provided)
    if( aCtx.Request.has_diff_pair_skew() )
        applyMeanderSettings( &bds.m_SkewMeanderSettings, aCtx.Request.diff_pair_skew() );

    frame()->OnModify();

    // Return updated settings
    return handleGetLengthTuningPatternSettings(
            HANDLER_CONTEXT<GetLengthTuningPatternSettings>{ aCtx.ClientName, GetLengthTuningPatternSettings() } );
}


// Helper to populate a protobuf TrackPropagationEntry from a KiCad entry
static void populateTrackPropagationEntry( TrackPropagationEntry* aProto,
                                           const DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& aEntry )
{
    aProto->set_signal_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.GetSignalLayer() ) );
    aProto->set_top_reference_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.GetTopReferenceLayer() ) );
    aProto->set_bottom_reference_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.GetBottomReferenceLayer() ) );
    aProto->set_width_nm( aEntry.GetWidth() );
    aProto->set_diff_pair_gap_nm( aEntry.GetDiffPairGap() );
    aProto->set_delay_ps_per_mm( aEntry.GetDelay( true ) ); // Force get delay even if time domain disabled
    // Note: The entry's enable state is per-entry in KiCad
}


// Helper to populate a protobuf ViaPropagationOverride from a KiCad entry
static void populateViaPropagationOverride( ViaPropagationOverride* aProto,
                                            const DELAY_PROFILE_VIA_OVERRIDE_ENTRY& aEntry )
{
    aProto->set_signal_layer_from( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.m_SignalLayerFrom ) );
    aProto->set_signal_layer_to( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.m_SignalLayerTo ) );
    aProto->set_via_layer_from( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.m_ViaLayerFrom ) );
    aProto->set_via_layer_to( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aEntry.m_ViaLayerTo ) );
    aProto->set_delay_ps( aEntry.m_Delay );
}


// Helper to populate a protobuf TuningProfile from a KiCad profile
static void populateTuningProfile( TuningProfile* aProto, const TUNING_PROFILE& aProfile )
{
    aProto->set_name( aProfile.m_ProfileName.ToStdString() );

    switch( aProfile.m_Type )
    {
    case TUNING_PROFILE::PROFILE_TYPE::SINGLE:
        aProto->set_type( TuningProfileType::TPT_SINGLE );
        break;
    case TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL:
        aProto->set_type( TuningProfileType::TPT_DIFFERENTIAL );
        break;
    }

    aProto->set_target_impedance_ohms( aProfile.m_TargetImpedance );
    aProto->set_enable_time_domain_tuning( aProfile.m_EnableTimeDomainTuning );
    aProto->set_via_propagation_delay_ps( aProfile.m_ViaPropagationDelay );

    for( const auto& entry : aProfile.m_TrackPropagationEntries )
    {
        TrackPropagationEntry* protoEntry = aProto->add_track_entries();
        populateTrackPropagationEntry( protoEntry, entry );
    }

    for( const auto& override : aProfile.m_ViaOverrides )
    {
        ViaPropagationOverride* protoOverride = aProto->add_via_overrides();
        populateViaPropagationOverride( protoOverride, override );
    }
}


// Helper to apply a protobuf TrackPropagationEntry to a KiCad entry
static DELAY_PROFILE_TRACK_PROPAGATION_ENTRY applyTrackPropagationEntry(
        const TrackPropagationEntry& aProto )
{
    DELAY_PROFILE_TRACK_PROPAGATION_ENTRY entry;

    entry.SetSignalLayer( FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.signal_layer() ) );
    entry.SetTopReferenceLayer( FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.top_reference_layer() ) );
    entry.SetBottomReferenceLayer( FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.bottom_reference_layer() ) );
    entry.SetWidth( aProto.width_nm() );
    entry.SetDiffPairGap( aProto.diff_pair_gap_nm() );
    entry.SetDelay( aProto.delay_ps_per_mm() );
    entry.SetEnableTimeDomainTuning( aProto.enable_time_domain() );

    return entry;
}


// Helper to apply a protobuf ViaPropagationOverride to a KiCad entry
static DELAY_PROFILE_VIA_OVERRIDE_ENTRY applyViaPropagationOverride(
        const ViaPropagationOverride& aProto )
{
    DELAY_PROFILE_VIA_OVERRIDE_ENTRY entry;

    entry.m_SignalLayerFrom = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.signal_layer_from() );
    entry.m_SignalLayerTo = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.signal_layer_to() );
    entry.m_ViaLayerFrom = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.via_layer_from() );
    entry.m_ViaLayerTo = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aProto.via_layer_to() );
    entry.m_Delay = aProto.delay_ps();

    return entry;
}


// Helper to apply a protobuf TuningProfile to a KiCad profile
static TUNING_PROFILE applyTuningProfile( const TuningProfile& aProto )
{
    TUNING_PROFILE profile;

    profile.m_ProfileName = wxString::FromUTF8( aProto.name() );

    switch( aProto.type() )
    {
    case TuningProfileType::TPT_DIFFERENTIAL:
        profile.m_Type = TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL;
        break;
    case TuningProfileType::TPT_SINGLE:
    default:
        profile.m_Type = TUNING_PROFILE::PROFILE_TYPE::SINGLE;
        break;
    }

    profile.m_TargetImpedance = aProto.target_impedance_ohms();
    profile.m_EnableTimeDomainTuning = aProto.enable_time_domain_tuning();
    profile.m_ViaPropagationDelay = aProto.via_propagation_delay_ps();

    for( const auto& protoEntry : aProto.track_entries() )
    {
        profile.m_TrackPropagationEntries.push_back( applyTrackPropagationEntry( protoEntry ) );
    }

    for( const auto& protoOverride : aProto.via_overrides() )
    {
        profile.m_ViaOverrides.push_back( applyViaPropagationOverride( protoOverride ) );
    }

    return profile;
}


HANDLER_RESULT<TuningProfilesResponse> API_HANDLER_PCB::handleGetTuningProfiles(
        const HANDLER_CONTEXT<GetTuningProfiles>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PROJECT& project = frame()->Prj();
    std::shared_ptr<TUNING_PROFILES> tuningProfiles = project.GetProjectFile().TuningProfileParameters();

    TuningProfilesResponse response;
    TuningProfiles* profiles = response.mutable_profiles();

    if( tuningProfiles )
    {
        for( const auto& profile : tuningProfiles->GetTuningProfiles() )
        {
            TuningProfile* protoProfile = profiles->add_profiles();
            populateTuningProfile( protoProfile, profile );
        }
    }

    return response;
}


HANDLER_RESULT<TuningProfilesResponse> API_HANDLER_PCB::handleSetTuningProfiles(
        const HANDLER_CONTEXT<SetTuningProfiles>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PROJECT& project = frame()->Prj();
    std::shared_ptr<TUNING_PROFILES> tuningProfiles = project.GetProjectFile().TuningProfileParameters();

    if( !tuningProfiles )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tuning profiles not initialized" );
        return tl::unexpected( e );
    }

    // Clear existing profiles and add new ones
    tuningProfiles->ClearTuningProfiles();

    for( const auto& protoProfile : aCtx.Request.profiles().profiles() )
    {
        TUNING_PROFILE profile = applyTuningProfile( protoProfile );
        tuningProfiles->AddTuningProfile( std::move( profile ) );
    }

    frame()->OnModify();

    // Return updated profiles
    return handleGetTuningProfiles(
            HANDLER_CONTEXT<GetTuningProfiles>{ aCtx.ClientName, GetTuningProfiles() } );
}


// Helper to convert condition type from KiCad to protobuf
static ComponentClassConditionType toProtoConditionType(
        COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE aType )
{
    switch( aType )
    {
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::REFERENCE:
        return ComponentClassConditionType::CCCT_REFERENCE;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::FOOTPRINT:
        return ComponentClassConditionType::CCCT_FOOTPRINT;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::SIDE:
        return ComponentClassConditionType::CCCT_SIDE;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::ROTATION:
        return ComponentClassConditionType::CCCT_ROTATION;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::FOOTPRINT_FIELD:
        return ComponentClassConditionType::CCCT_FOOTPRINT_FIELD;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::CUSTOM:
        return ComponentClassConditionType::CCCT_CUSTOM;
    case COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::SHEET_NAME:
        return ComponentClassConditionType::CCCT_SHEET_NAME;
    default:
        return ComponentClassConditionType::CCCT_REFERENCE;
    }
}


// Helper to convert condition type from protobuf to KiCad
static COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE fromProtoConditionType(
        ComponentClassConditionType aType )
{
    switch( aType )
    {
    case ComponentClassConditionType::CCCT_REFERENCE:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::REFERENCE;
    case ComponentClassConditionType::CCCT_FOOTPRINT:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::FOOTPRINT;
    case ComponentClassConditionType::CCCT_SIDE:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::SIDE;
    case ComponentClassConditionType::CCCT_ROTATION:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::ROTATION;
    case ComponentClassConditionType::CCCT_FOOTPRINT_FIELD:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::FOOTPRINT_FIELD;
    case ComponentClassConditionType::CCCT_CUSTOM:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::CUSTOM;
    case ComponentClassConditionType::CCCT_SHEET_NAME:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::SHEET_NAME;
    default:
        return COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITION_TYPE::REFERENCE;
    }
}


HANDLER_RESULT<ComponentClassSettingsResponse> API_HANDLER_PCB::handleGetComponentClassSettings(
        const HANDLER_CONTEXT<GetComponentClassSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PROJECT& project = frame()->Prj();
    std::shared_ptr<COMPONENT_CLASS_SETTINGS> ccSettings =
            project.GetProjectFile().ComponentClassSettings();

    ComponentClassSettingsResponse response;
    ComponentClassSettings* settings = response.mutable_settings();

    if( ccSettings )
    {
        settings->set_enable_sheet_component_classes( ccSettings->GetEnableSheetComponentClasses() );

        for( const auto& assignment : ccSettings->GetComponentClassAssignments() )
        {
            ComponentClassAssignment* protoAssignment = settings->add_assignments();
            protoAssignment->set_component_class( assignment.GetComponentClass().ToStdString() );

            // Set operator
            if( assignment.GetConditionsOperator() ==
                COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITIONS_OPERATOR::ANY )
            {
                protoAssignment->set_operator_( ComponentClassConditionsOperator::CCCO_ANY );
            }
            else
            {
                protoAssignment->set_operator_( ComponentClassConditionsOperator::CCCO_ALL );
            }

            // Add conditions
            for( const auto& [condType, primaryData, secondaryData] : assignment.GetConditions() )
            {
                ComponentClassCondition* condition = protoAssignment->add_conditions();
                condition->set_type( toProtoConditionType( condType ) );
                condition->set_primary_data( primaryData.ToStdString() );
                condition->set_secondary_data( secondaryData.ToStdString() );
            }
        }
    }

    return response;
}


HANDLER_RESULT<ComponentClassSettingsResponse> API_HANDLER_PCB::handleSetComponentClassSettings(
        const HANDLER_CONTEXT<SetComponentClassSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PROJECT& project = frame()->Prj();
    std::shared_ptr<COMPONENT_CLASS_SETTINGS> ccSettings =
            project.GetProjectFile().ComponentClassSettings();

    if( !ccSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Component class settings not initialized" );
        return tl::unexpected( e );
    }

    const ComponentClassSettings& protoSettings = aCtx.Request.settings();

    // Update enable sheet component classes
    ccSettings->SetEnableSheetComponentClasses( protoSettings.enable_sheet_component_classes() );

    // Clear and rebuild assignments
    ccSettings->ClearComponentClassAssignments();

    for( const auto& protoAssignment : protoSettings.assignments() )
    {
        COMPONENT_CLASS_ASSIGNMENT_DATA assignment;
        assignment.SetComponentClass( wxString::FromUTF8( protoAssignment.component_class() ) );

        // Set operator
        if( protoAssignment.operator_() == ComponentClassConditionsOperator::CCCO_ANY )
        {
            assignment.SetConditionsOperation(
                    COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITIONS_OPERATOR::ANY );
        }
        else
        {
            assignment.SetConditionsOperation(
                    COMPONENT_CLASS_ASSIGNMENT_DATA::CONDITIONS_OPERATOR::ALL );
        }

        // Add conditions
        for( const auto& protoCondition : protoAssignment.conditions() )
        {
            assignment.AddCondition(
                    fromProtoConditionType( protoCondition.type() ),
                    wxString::FromUTF8( protoCondition.primary_data() ),
                    wxString::FromUTF8( protoCondition.secondary_data() ) );
        }

        ccSettings->AddComponentClassAssignment( assignment );
    }

    frame()->OnModify();

    // Return updated settings
    return handleGetComponentClassSettings(
            HANDLER_CONTEXT<GetComponentClassSettings>{ aCtx.ClientName, GetComponentClassSettings() } );
}


HANDLER_RESULT<CustomRulesResponse> API_HANDLER_PCB::handleGetCustomRules(
        const HANDLER_CONTEXT<GetCustomRules>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxString rulesPath = frame()->GetDesignRulesPath();
    CustomRulesResponse response;

    if( wxFileExists( rulesPath ) )
    {
        wxTextFile file( rulesPath );

        if( file.Open() )
        {
            wxString rulesText;

            // wxTextFile iteration: GetFirstLine()/GetNextLine() loop doesn't include
            // the last line when Eof() becomes true, so we need to handle it separately
            size_t lineCount = file.GetLineCount();

            for( size_t i = 0; i < lineCount; ++i )
            {
                rulesText += file.GetLine( i ) + wxT( "\n" );
            }

            response.set_rules_text( rulesText.ToStdString() );
        }
        else
        {
            // File exists but can't be opened - return empty with default version header
            response.set_rules_text( "(version 1)\n" );
        }
    }
    else
    {
        // No rules file exists yet - return default version header
        response.set_rules_text( "(version 1)\n" );
    }

    return response;
}


HANDLER_RESULT<CustomRulesResponse> API_HANDLER_PCB::handleSetCustomRules(
        const HANDLER_CONTEXT<SetCustomRules>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( frame()->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Cannot set custom rules without a project" );
        return tl::unexpected( e );
    }

    wxString rulesPath = frame()->GetDesignRulesPath();
    wxString rulesText = wxString::FromUTF8( aCtx.Request.rules_text() );

    // Write the rules to file
    wxFile file( rulesPath, wxFile::write );

    if( !file.IsOpened() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to open rules file for writing" );
        return tl::unexpected( e );
    }

    if( !file.Write( rulesText ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to write rules to file" );
        return tl::unexpected( e );
    }

    file.Close();

    // Reinitialize the DRC engine with the new rules
    try
    {
        frame()->GetBoard()->GetDesignSettings().m_DRCEngine->InitEngine( rulesPath );
    }
    catch( const PARSE_ERROR& e )
    {
        // Rules file has syntax errors but was still saved
        // Return the content but with an error indication
        CustomRulesResponse response;
        response.set_rules_text( rulesText.ToStdString() );
        return response;
    }

    frame()->OnModify();

    // Return updated rules
    return handleGetCustomRules(
            HANDLER_CONTEXT<GetCustomRules>{ aCtx.ClientName, GetCustomRules() } );
}
