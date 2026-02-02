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
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <tools/drc_tool.h>
#include <drc/drc_engine.h>
#include <zone.h>

#include <api/common/types/base_types.pb.h>
#include <widgets/appearance_controls.h>
#include <wx/wfstream.h>
#include <widgets/report_severity.h>

using namespace kiapi::common::commands;
using types::CommandStatus;
using types::DocumentType;
using types::ItemRequestStatus;


API_HANDLER_PCB::API_HANDLER_PCB( PCB_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
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
    registerHandler<GetGraphicsDefaults, GraphicsDefaultsResponse>(
            &API_HANDLER_PCB::handleGetGraphicsDefaults );
    registerHandler<GetBoundingBox, GetBoundingBoxResponse>(
            &API_HANDLER_PCB::handleGetBoundingBox );
    registerHandler<GetPadShapeAsPolygon, PadShapeAsPolygonResponse>(
            &API_HANDLER_PCB::handleGetPadShapeAsPolygon );
    registerHandler<CheckPadstackPresenceOnLayers, PadstackPresenceResponse>(
            &API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers );
    registerHandler<GetTitleBlockInfo, types::TitleBlockInfo>(
            &API_HANDLER_PCB::handleGetTitleBlockInfo );
    registerHandler<ExpandTextVariables, ExpandTextVariablesResponse>(
            &API_HANDLER_PCB::handleExpandTextVariables );
    registerHandler<GetBoardOrigin, types::Vector2>( &API_HANDLER_PCB::handleGetBoardOrigin );
    registerHandler<SetBoardOrigin, Empty>( &API_HANDLER_PCB::handleSetBoardOrigin );

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

    // Document management handlers
    registerHandler<CreateDocument, CreateDocumentResponse>( &API_HANDLER_PCB::handleCreateDocument );
    registerHandler<OpenDocument, OpenDocumentResponse>( &API_HANDLER_PCB::handleOpenDocument );
    registerHandler<CloseDocument, Empty>( &API_HANDLER_PCB::handleCloseDocument );
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
                commit->Remove( boardItem );
                item->Serialize( newItem );
                commit->Add( item.release() );
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
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
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
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

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
    drcTool->RunTests( nullptr,  // No progress reporter for API
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

    // Solder paste
    rules->set_solder_paste_margin( bds.m_SolderPasteMargin );
    rules->set_solder_paste_margin_ratio( bds.m_SolderPasteMarginRatio );

    // Minimum resolved spokes
    rules->set_min_resolved_spokes( bds.m_MinResolvedSpokes );

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
    if( rules.min_via_drill() > 0 )
        bds.m_MinThroughDrill = rules.min_via_drill();
    if( rules.min_via_annular_width() > 0 )
        bds.m_ViasMinAnnularWidth = rules.min_via_annular_width();

    // Microvia constraints
    if( rules.min_microvia_diameter() > 0 )
        bds.m_MicroViasMinSize = rules.min_microvia_diameter();
    if( rules.min_microvia_drill() > 0 )
        bds.m_MicroViasMinDrill = rules.min_microvia_drill();

    // Hole constraints
    if( rules.min_through_hole() > 0 )
        bds.m_MinThroughDrill = rules.min_through_hole();
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

    // Solder paste
    bds.m_SolderPasteMargin = rules.solder_paste_margin();
    bds.m_SolderPasteMarginRatio = rules.solder_paste_margin_ratio();

    // Minimum resolved spokes
    if( rules.min_resolved_spokes() > 0 )
        bds.m_MinResolvedSpokes = rules.min_resolved_spokes();

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
    updatedRules->set_solder_paste_margin( bds.m_SolderPasteMargin );
    updatedRules->set_solder_paste_margin_ratio( bds.m_SolderPasteMarginRatio );
    updatedRules->set_min_resolved_spokes( bds.m_MinResolvedSpokes );

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

    // Export DRC severities
    for( const auto& [errorCode, severity] : bds.m_DRCSeverities )
    {
        DRCCheckSeverity* checkSeverity = settings->add_check_severities();
        checkSeverity->set_check_name( std::to_string( errorCode ) );
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

    // Apply DRC severity settings
    for( const DRCCheckSeverity& checkSeverity : settings.check_severities() )
    {
        try
        {
            int errorCode = std::stoi( checkSeverity.check_name() );
            SEVERITY severity = FromProtoEnum<SEVERITY>( checkSeverity.severity() );
            bds.m_DRCSeverities[errorCode] = severity;
        }
        catch( ... )
        {
            // Skip invalid check names
        }
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

    // Get current grid size
    int currentIdx = gridSettings.last_size_idx;

    if( currentIdx >= 0 && currentIdx < static_cast<int>( gridSettings.grids.size() ) )
    {
        const GRID& grid = gridSettings.grids[currentIdx];
        VECTOR2D gridSize = grid.ToDouble( pcbIUScale );

        settings->set_grid_size_x_nm( static_cast<int64_t>( gridSize.x ) );
        settings->set_grid_size_y_nm( static_cast<int64_t>( gridSize.y ) );
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
        // Convert nm to appropriate unit string for user grid
        double gridX = aCtx.Request.has_grid_size_x_nm()
                           ? aCtx.Request.grid_size_x_nm() / 1e6  // nm to mm
                           : 1.0;
        double gridY = aCtx.Request.has_grid_size_y_nm()
                           ? aCtx.Request.grid_size_y_nm() / 1e6  // nm to mm
                           : 1.0;

        gridSettings.user_grid_x = wxString::Format( wxT( "%g mm" ), gridX );
        gridSettings.user_grid_y = wxString::Format( wxT( "%g mm" ), gridY );
    }

    // Refresh the view
    frame()->GetCanvas()->Refresh();

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
