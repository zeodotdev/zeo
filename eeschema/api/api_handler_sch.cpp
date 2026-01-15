/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
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

#include <api/api_handler_sch.h>
#include <api/api_sch_utils.h>
#include <api/api_utils.h>
#include <api/api_enums.h>
#include <magic_enum.hpp>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_symbol.h>
#include <lib_symbol.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_junction.h>
#include <sch_no_connect.h>
#include <sch_bus_entry.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <sch_shape.h>
#include <sch_bitmap.h>
#include <sch_table.h>
#include <wx/filename.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tools/sch_selection_tool.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <pgm_base.h>

#include <api/common/types/base_types.pb.h>

using namespace kiapi::common::commands;
using kiapi::common::types::CommandStatus;
using kiapi::common::types::DocumentType;
using kiapi::common::types::ItemRequestStatus;


API_HANDLER_SCH::API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame ),
        m_frame( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>( &API_HANDLER_SCH::handleGetOpenDocuments );
    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_SCH::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_SCH::handleGetItemsById );

    // Selection handlers
    registerHandler<GetSelection, SelectionResponse>( &API_HANDLER_SCH::handleGetSelection );
    registerHandler<AddToSelection, SelectionResponse>( &API_HANDLER_SCH::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>( &API_HANDLER_SCH::handleRemoveFromSelection );
    registerHandler<ClearSelection, Empty>( &API_HANDLER_SCH::handleClearSelection );

    // Library management handlers
    registerHandler<GetLibraries, GetLibrariesResponse>( &API_HANDLER_SCH::handleGetLibraries );
    registerHandler<AddLibrary, AddLibraryResponse>( &API_HANDLER_SCH::handleAddLibrary );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    wxASSERT( m_frame != nullptr );

    if( !m_frame )
        return nullptr;

    return std::make_unique<SCH_COMMIT>( m_frame );
}


bool API_HANDLER_SCH::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_SCHEMATIC )
        return false;

    // TODO(JE) need serdes for SCH_SHEET_PATH <> SheetPath
    return true;

    //wxString currentPath = m_frame->GetCurrentSheet().PathAsString();
    //return 0 == aDocument.sheet_path().compare( currentPath.ToStdString() );
}


HANDLER_RESULT<GetOpenDocumentsResponse>
API_HANDLER_SCH::handleGetOpenDocuments( const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
    {
        ApiResponseStatus e;

        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse         response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( m_frame->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_SCHEMATIC );
    // Use sheet_path for schematic documents, not board_filename
    // Must set the path field (with at least root sheet) for oneof to be recognized
    types::SheetPath* sheetPath = doc.mutable_sheet_path();
    sheetPath->set_path_human_readable( "/" );

    // Add a placeholder KIID for the root sheet path
    // This ensures the sheet_path oneof field is properly serialized
    sheetPath->add_path()->set_value( m_frame->GetCurrentSheet().Last()->m_Uuid.AsStdString() );

    *response.mutable_documents()->Add() = doc;
    return response;
}


HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> API_HANDLER_SCH::createItemForType( KICAD_T aType, EDA_ITEM* aContainer )
{
    // Some item types require a container - validate those specifically
    // Top-level items (wires, junctions, labels, etc.) don't need a container
    if( aType == SCH_PIN_T )
    {
        if( !aContainer || !dynamic_cast<SCH_SYMBOL*>( aContainer ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( aContainer
                ? fmt::format( "Tried to create a pin in {}, which is not a symbol",
                               aContainer->GetFriendlyName().ToStdString() )
                : "Tried to create a pin without a symbol container" );
            return tl::unexpected( e );
        }
    }
    else if( aType == SCH_SHEET_PIN_T )
    {
        if( !aContainer || !dynamic_cast<SCH_SHEET*>( aContainer ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( aContainer
                ? fmt::format( "Tried to create a sheet pin in {}, which is not a sheet",
                               aContainer->GetFriendlyName().ToStdString() )
                : "Tried to create a sheet pin without a sheet container" );
            return tl::unexpected( e );
        }
    }

    std::unique_ptr<EDA_ITEM> created = CreateItemForType( aType, aContainer );

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


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_SCH::handleCreateUpdateItemsInternal(
        bool aCreate, const std::string& aClientName, const types::ItemHeader& aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )>         aItemHandler )
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

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
    {
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No active schematic screen" );
        return tl::unexpected( e );
    }

    EE_RTREE& screenItems = screen->Items();

    std::map<KIID, EDA_ITEM*> itemUuidMap;

    std::for_each( screenItems.begin(), screenItems.end(),
                   [&]( EDA_ITEM* aItem )
                   {
                       itemUuidMap[aItem->m_Uuid] = aItem;
                   } );

    EDA_ITEM* container = nullptr;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;

        if( itemUuidMap.count( containerId ) )
        {
            container = itemUuidMap.at( containerId );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format( "The requested container {} is not a valid schematic item container",
                                                  containerId.AsStdString() ) );
                return tl::unexpected( e );
            }
        }
        else
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "The requested container {} does not exist in this document",
                                              containerId.AsStdString() ) );
            return tl::unexpected( e );
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    if( !commit )
    {
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to create commit - frame may be invalid" );
        return tl::unexpected( e );
    }

    for( const google::protobuf::Any& anyItem : aItems )
    {
        ItemStatus             status;
        std::optional<KICAD_T> type = TypeNameFromAny( anyItem );

        if( !type )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( fmt::format( "Could not decode a valid type from {}", anyItem.type_url() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> creationResult = createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<EDA_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request", item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        // For SCH_SYMBOL, we need to resolve the library symbol from the lib_id
        if( aCreate && item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item.get() );
            LIB_ID libId = symbol->GetLibId();

            if( !libId.IsValid() )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
                status.set_error_message( "Symbol has invalid or empty lib_id" );
                aItemHandler( status, anyItem );
                continue;
            }

            LIB_SYMBOL* libSymbol = m_frame->GetLibSymbol( libId );

            if( !libSymbol )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
                status.set_error_message( fmt::format( "Library symbol '{}' not found",
                                                       libId.GetUniStringLibId().ToStdString() ) );
                aItemHandler( status, anyItem );
                continue;
            }

            // Flatten the library symbol (handles inheritance) and set it on the symbol
            std::unique_ptr<LIB_SYMBOL> flattenedSymbol = libSymbol->Flatten();
            flattenedSymbol->SetParent();
            symbol->SetLibSymbol( flattenedSymbol.release() );

            // Update fields from library symbol
            symbol->UpdateFields( &m_frame->GetCurrentSheet(),
                                  true,   // updateStyle
                                  false,  // updateRef
                                  false,  // updateOtherFields
                                  true,   // resetRef
                                  true ); // resetOtherFields
        }

        if( aCreate && itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message(
                    fmt::format( "an item with UUID {} already exists", item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message(
                    fmt::format( "an item with UUID {} does not exist", item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            item->Serialize( newItem );
            commit->Add( item.release(), screen );
        }
        else
        {
            EDA_ITEM* edaItem = itemUuidMap[item->m_Uuid];

            if( SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( edaItem ) )
            {
                schItem->SwapItemData( static_cast<SCH_ITEM*>( item.get() ) );
                schItem->Serialize( newItem );
                commit->Modify( schItem, screen );
            }
            else
            {
                wxASSERT( false );
            }
        }

        aItemHandler( status, newItem );
    }

    // Push the commit AFTER all items are processed (not inside the loop!)
    // This was causing use-after-free when processing multiple items in one request
    // Only auto-push if client hasn't called begin_commit (i.e., not in m_activeClients)
    //
    // NOTE: Due to API routing, BeginCommit (which has no document field) may be
    // handled by a different handler instance than CreateItems. This means
    // m_activeClients may not contain the client even if begin_commit() was called.
    // This is a known limitation - fixing it requires adding a document field to
    // the BeginCommit protobuf message.
    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, aCreate ? _( "Added items via API" ) : _( "Updated items via API" ) );

    return ItemRequestStatus::IRS_OK;
}


void API_HANDLER_SCH::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string&                  aClientName )
{
    if( !m_frame )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
        return;

    COMMIT* commit = getCurrentCommit( aClientName );

    if( !commit )
        return;

    for( auto& [id, status] : aItemsToDelete )
    {
        SCH_ITEM* item = nullptr;

        for( SCH_ITEM* candidate : screen->Items() )
        {
            if( candidate->m_Uuid == id )
            {
                item = candidate;
                break;
            }
        }

        if( item )
        {
            commit->Remove( item, screen );
            status = ItemDeletionStatus::IDS_OK;
        }
        else
        {
            status = ItemDeletionStatus::IDS_NONEXISTENT;
        }
    }

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_SCH::getItemFromDocument( const DocumentSpecifier& aDocument, const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
        return std::nullopt;

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->m_Uuid == aId )
            return item;
    }

    return std::nullopt;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItems( const HANDLER_CONTEXT<GetItems>& aCtx )
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

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "no screen available" );
        return tl::unexpected( e );
    }

    std::vector<SCH_ITEM*> items;
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
        case SCH_SYMBOL_T:
        {
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( item->Type() == SCH_SYMBOL_T )
                    items.emplace_back( item );
            }

            typesInserted.insert( SCH_SYMBOL_T );
            break;
        }

        case SCH_PIN_T:
        {
            // Pins are nested inside symbols
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                {
                    for( SCH_PIN* pin : symbol->GetPins() )
                        items.emplace_back( pin );
                }
            }

            typesInserted.insert( SCH_PIN_T );
            break;
        }

        case SCH_FIELD_T:
        {
            // Fields are nested inside symbols
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                {
                    for( SCH_FIELD& field : symbol->GetFields() )
                        items.emplace_back( &field );
                }
            }

            typesInserted.insert( SCH_FIELD_T );
            break;
        }

        case SCH_SHEET_PIN_T:
        {
            // Sheet pins are nested inside sheets
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( item ) )
                {
                    for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                        items.emplace_back( pin );
                }
            }

            typesInserted.insert( SCH_SHEET_PIN_T );
            break;
        }

        // Handle all top-level schematic item types
        case SCH_JUNCTION_T:
        case SCH_NO_CONNECT_T:
        case SCH_BUS_WIRE_ENTRY_T:
        case SCH_BUS_BUS_ENTRY_T:
        case SCH_LINE_T:
        case SCH_SHAPE_T:
        case SCH_BITMAP_T:
        case SCH_TEXTBOX_T:
        case SCH_TEXT_T:
        case SCH_TABLE_T:
        case SCH_TABLECELL_T:
        case SCH_LABEL_T:
        case SCH_GLOBAL_LABEL_T:
        case SCH_HIER_LABEL_T:
        case SCH_DIRECTIVE_LABEL_T:
        case SCH_SHEET_T:
        {
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( item->Type() == type )
                    items.emplace_back( item );
            }

            typesInserted.insert( type );
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
        e.set_error_message( "none of the requested types are valid for a Schematic object" );
        return tl::unexpected( e );
    }

    for( const SCH_ITEM* item : items )
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


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItemsById(
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

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "no screen available" );
        return tl::unexpected( e );
    }

    // Build map of all items including nested ones
    std::map<KIID, SCH_ITEM*> itemMap;

    for( SCH_ITEM* item : screen->Items() )
    {
        itemMap[item->m_Uuid] = item;

        // Add nested items from symbols
        if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
        {
            for( SCH_PIN* pin : symbol->GetPins() )
                itemMap[pin->m_Uuid] = pin;

            for( SCH_FIELD& field : symbol->GetFields() )
                itemMap[field.m_Uuid] = &field;
        }

        // Add nested items from sheets
        if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( item ) )
        {
            for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                itemMap[pin->m_Uuid] = pin;
        }
    }

    std::vector<SCH_ITEM*> items;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        KIID kiid( id.value() );

        if( itemMap.count( kiid ) )
            items.emplace_back( itemMap.at( kiid ) );
    }

    if( items.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested IDs were found or valid" );
        return tl::unexpected( e );
    }

    for( const SCH_ITEM* item : items )
    {
        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


std::optional<SCH_ITEM*> API_HANDLER_SCH::getItemById( const KIID& aId )
{
    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
        return std::nullopt;

    // Search top-level items
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->m_Uuid == aId )
            return item;

        // Search nested items in symbols
        if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
        {
            for( SCH_PIN* pin : symbol->GetPins() )
            {
                if( pin->m_Uuid == aId )
                    return pin;
            }

            for( SCH_FIELD& field : symbol->GetFields() )
            {
                if( field.m_Uuid == aId )
                    return &field;
            }
        }

        // Search nested items in sheets
        if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( item ) )
        {
            for( SCH_SHEET_PIN* pin : sheet->GetPins() )
            {
                if( pin->m_Uuid == aId )
                    return pin;
            }
        }
    }

    return std::nullopt;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleGetSelection(
        const HANDLER_CONTEXT<GetSelection>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
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

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    if( !mgr )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tool manager not available" );
        return tl::unexpected( e );
    }

    SCH_SELECTION_TOOL* selectionTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    if( !selectionTool )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Selection tool not available" );
        return tl::unexpected( e );
    }

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
    {
        if( filter.empty() || filter.contains( item->Type() ) )
            item->Serialize( *response.add_items() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleClearSelection(
        const HANDLER_CONTEXT<ClearSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    if( !mgr )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tool manager not available" );
        return tl::unexpected( e );
    }

    mgr->RunAction( ACTIONS::selectionClear );

    // Note: The selection tool handles UI updates via events.
    // We don't call Refresh() here to avoid issues with nested event processing on macOS.

    return Empty();
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleAddToSelection(
        const HANDLER_CONTEXT<AddToSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    if( !mgr )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tool manager not available" );
        return tl::unexpected( e );
    }

    SCH_SELECTION_TOOL* selectionTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    if( !selectionTool )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Selection tool not available" );
        return tl::unexpected( e );
    }

    std::vector<EDA_ITEM*> toAdd;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<SCH_ITEM*> item = getItemById( KIID( id.value() ) ) )
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


HANDLER_RESULT<SelectionResponse> API_HANDLER_SCH::handleRemoveFromSelection(
        const HANDLER_CONTEXT<RemoveFromSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    if( !mgr )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tool manager not available" );
        return tl::unexpected( e );
    }

    SCH_SELECTION_TOOL* selectionTool = mgr->GetTool<SCH_SELECTION_TOOL>();

    if( !selectionTool )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Selection tool not available" );
        return tl::unexpected( e );
    }

    std::vector<EDA_ITEM*> toRemove;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<SCH_ITEM*> item = getItemById( KIID( id.value() ) ) )
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


HANDLER_RESULT<GetLibrariesResponse> API_HANDLER_SCH::handleGetLibraries(
        const HANDLER_CONTEXT<GetLibraries>& aCtx )
{
    GetLibrariesResponse response;

    SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );

    if( !adapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Symbol library adapter not available" );
        return tl::unexpected( e );
    }

    // Determine which scopes to query
    LIBRARY_TABLE_SCOPE queryScope = LIBRARY_TABLE_SCOPE::BOTH;

    switch( aCtx.Request.scope() )
    {
    case LibraryTableScope::LTS_GLOBAL:
        queryScope = LIBRARY_TABLE_SCOPE::GLOBAL;
        break;
    case LibraryTableScope::LTS_PROJECT:
        queryScope = LIBRARY_TABLE_SCOPE::PROJECT;
        break;
    case LibraryTableScope::LTS_BOTH:
    case LibraryTableScope::LTS_UNKNOWN:
    default:
        queryScope = LIBRARY_TABLE_SCOPE::BOTH;
        break;
    }

    // Get all library rows
    std::vector<LIBRARY_TABLE_ROW*> rows = adapter->Rows( queryScope, true /* include invalid */ );

    for( LIBRARY_TABLE_ROW* row : rows )
    {
        LibraryInfo* libInfo = response.add_libraries();
        libInfo->set_nickname( row->Nickname().ToStdString() );
        libInfo->set_uri( row->URI().ToStdString() );
        libInfo->set_type( row->Type().ToStdString() );
        libInfo->set_description( row->Description().ToStdString() );

        // Check if library is loaded
        std::optional<LIB_STATUS> status = adapter->GetLibraryStatus( row->Nickname() );
        libInfo->set_is_loaded( status.has_value() && status->load_status == LOAD_STATUS::LOADED );

        // Check if library is read-only
        libInfo->set_is_read_only( !adapter->IsWritable( row->Nickname() ) );

        // Determine scope of this row based on the row's scope property
        if( row->Scope() == LIBRARY_TABLE_SCOPE::GLOBAL )
        {
            libInfo->set_scope( LibraryTableScope::LTS_GLOBAL );
        }
        else if( row->Scope() == LIBRARY_TABLE_SCOPE::PROJECT )
        {
            libInfo->set_scope( LibraryTableScope::LTS_PROJECT );
        }
        else
        {
            // Check global table as fallback
            LIBRARY_TABLE* globalTable = adapter->GlobalTable();
            if( globalTable && globalTable->HasRow( row->Nickname() ) )
            {
                libInfo->set_scope( LibraryTableScope::LTS_GLOBAL );
            }
            else
            {
                libInfo->set_scope( LibraryTableScope::LTS_PROJECT );
            }
        }
    }

    return response;
}


HANDLER_RESULT<AddLibraryResponse> API_HANDLER_SCH::handleAddLibrary(
        const HANDLER_CONTEXT<AddLibrary>& aCtx )
{
    AddLibraryResponse response;

    wxString filePath( aCtx.Request.file_path() );

    // Validate that the file exists
    if( !wxFileName::FileExists( filePath ) && !wxFileName::DirExists( filePath ) )
    {
        response.set_status( AddLibraryStatus::ALS_FILE_NOT_FOUND );
        response.set_error_message( fmt::format( "File or directory not found: {}",
                                                  aCtx.Request.file_path() ) );
        return response;
    }

    // Determine the nickname
    wxString nickname;
    if( !aCtx.Request.nickname().empty() )
    {
        nickname = wxString( aCtx.Request.nickname() );
    }
    else
    {
        // Derive nickname from filename
        wxFileName fn( filePath );
        nickname = fn.GetName();
    }

    // Determine which table to add to
    LIBRARY_TABLE_SCOPE tableScope = LIBRARY_TABLE_SCOPE::GLOBAL;

    switch( aCtx.Request.scope() )
    {
    case LibraryTableScope::LTS_PROJECT:
        tableScope = LIBRARY_TABLE_SCOPE::PROJECT;
        break;
    case LibraryTableScope::LTS_GLOBAL:
    case LibraryTableScope::LTS_BOTH:
    case LibraryTableScope::LTS_UNKNOWN:
    default:
        tableScope = LIBRARY_TABLE_SCOPE::GLOBAL;
        break;
    }

    // Get the library manager
    LIBRARY_MANAGER& libMgr = Pgm().GetLibraryManager();
    std::optional<LIBRARY_TABLE*> table = libMgr.Table( LIBRARY_TABLE_TYPE::SYMBOL, tableScope );

    if( !table.has_value() || !table.value() )
    {
        response.set_status( AddLibraryStatus::ALS_TABLE_NOT_FOUND );
        response.set_error_message( tableScope == LIBRARY_TABLE_SCOPE::PROJECT
            ? "No project library table available. Is a project loaded?"
            : "Global library table not available" );
        return response;
    }

    LIBRARY_TABLE* libTable = table.value();

    // Check if a library with this nickname already exists
    if( libTable->HasRow( nickname ) )
    {
        response.set_status( AddLibraryStatus::ALS_ALREADY_EXISTS );
        response.set_nickname( nickname.ToStdString() );
        response.set_error_message( fmt::format( "Library '{}' already exists in the table",
                                                  nickname.ToStdString() ) );
        return response;
    }

    // Determine the plugin type based on file extension
    wxString pluginType = wxT( "KiCad" );  // Default to KiCad format
    wxFileName fn( filePath );
    wxString ext = fn.GetExt().Lower();

    if( ext == wxT( "lib" ) )
        pluginType = wxT( "Legacy" );
    else if( ext == wxT( "kicad_sym" ) )
        pluginType = wxT( "KiCad" );

    // Create a new row and add it to the table
    LIBRARY_TABLE_ROW& newRow = libTable->InsertRow();
    newRow.SetNickname( nickname );
    newRow.SetURI( filePath );
    newRow.SetType( pluginType );
    newRow.SetScope( tableScope );

    // Save the table
    LIBRARY_RESULT<void> saveResult = libTable->Save();

    if( !saveResult.has_value() )
    {
        response.set_status( AddLibraryStatus::ALS_INVALID_FORMAT );
        response.set_error_message( fmt::format( "Failed to save library table: {}",
                                                  saveResult.error().message.ToStdString() ) );
        return response;
    }

    // Notify the adapter that tables have changed
    SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
    if( adapter )
    {
        adapter->GlobalTablesChanged( { LIBRARY_TABLE_TYPE::SYMBOL } );
    }

    response.set_status( AddLibraryStatus::ALS_OK );
    response.set_nickname( nickname.ToStdString() );
    return response;
}
