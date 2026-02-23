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
#include <sch_sheet_path.h>
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
#include <sch_group.h>
#include <schematic.h>
#include <bus_alias.h>
#include <refdes_tracker.h>
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/sstream.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_navigate_tool.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <pgm_base.h>
#include <kiway.h>
#include <erc/erc.h>
#include <sch_marker.h>
#include <sch_reference_list.h>
#include <rc_item.h>
#include <reporter.h>
#include <connection_graph.h>
#include <sch_connection.h>
#include <project/net_settings.h>
#include <project/project_file.h>
#include <title_block.h>
#include <page_info.h>
#include <eeschema_settings.h>
#include <schematic_settings.h>
#include <settings/common_settings.h>
#include <sim/spice_settings.h>
#include <sim/simulator_frame.h>
#include <sim/spice_simulator.h>
#include <sim/spice_circuit_model.h>
#include <advanced_config.h>
#include <gal/graphics_abstraction_layer.h>
#include <view/view.h>
#include <undo_redo_container.h>
#include <lib_id.h>
#include <erc/erc_settings.h>
#include <pin_type.h>
#include <design_block_library_adapter.h>
#include <design_block.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <sch_io/sch_io_mgr.h>
#include <richio.h>
#include <io/kicad/kicad_io_utils.h>
#include <sch_screen.h>
#include <wildcards_and_files_ext.h>
#include <junction_helpers.h>
#include <trigo.h>

#include <api/common/types/base_types.pb.h>
#include <api/schematic/schematic_commands.pb.h>
#include <api/schematic/schematic_types.pb.h>

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
    registerHandler<GetBoundingBox, GetBoundingBoxResponse>( &API_HANDLER_SCH::handleGetBoundingBox );

    // Selection handlers
    registerHandler<GetSelection, SelectionResponse>( &API_HANDLER_SCH::handleGetSelection );
    registerHandler<AddToSelection, SelectionResponse>( &API_HANDLER_SCH::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>( &API_HANDLER_SCH::handleRemoveFromSelection );
    registerHandler<ClearSelection, Empty>( &API_HANDLER_SCH::handleClearSelection );

    // Library management handlers
    registerHandler<GetLibraries, GetLibrariesResponse>( &API_HANDLER_SCH::handleGetLibraries );
    registerHandler<AddLibrary, AddLibraryResponse>( &API_HANDLER_SCH::handleAddLibrary );
    registerHandler<RemoveLibrary, RemoveLibraryResponse>( &API_HANDLER_SCH::handleRemoveLibrary );

    // Document management handlers
    registerHandler<CreateDocument, CreateDocumentResponse>( &API_HANDLER_SCH::handleCreateDocument );
    registerHandler<OpenDocument, OpenDocumentResponse>( &API_HANDLER_SCH::handleOpenDocument );
    registerHandler<CloseDocument, Empty>( &API_HANDLER_SCH::handleCloseDocument );
    registerHandler<SetActiveDocument, Empty>( &API_HANDLER_SCH::handleSetActiveDocument );

    // Sheet hierarchy handlers
    using namespace kiapi::schematic::commands;
    registerHandler<GetSheetHierarchy, GetSheetHierarchyResponse>( &API_HANDLER_SCH::handleGetSheetHierarchy );
    registerHandler<GetCurrentSheet, GetCurrentSheetResponse>( &API_HANDLER_SCH::handleGetCurrentSheet );
    registerHandler<NavigateToSheet, Empty>( &API_HANDLER_SCH::handleNavigateToSheet );

    // Sheet CRUD handlers
    registerHandler<CreateSheet, CreateSheetResponse>( &API_HANDLER_SCH::handleCreateSheet );
    registerHandler<DeleteSheet, Empty>( &API_HANDLER_SCH::handleDeleteSheet );
    registerHandler<GetSheetProperties, GetSheetPropertiesResponse>( &API_HANDLER_SCH::handleGetSheetProperties );
    registerHandler<SetSheetProperties, Empty>( &API_HANDLER_SCH::handleSetSheetProperties );

    // Sheet pin handlers
    registerHandler<CreateSheetPin, CreateSheetPinResponse>( &API_HANDLER_SCH::handleCreateSheetPin );
    registerHandler<DeleteSheetPin, Empty>( &API_HANDLER_SCH::handleDeleteSheetPin );
    registerHandler<GetSheetPins, GetSheetPinsResponse>( &API_HANDLER_SCH::handleGetSheetPins );
    registerHandler<SyncSheetPins, SyncSheetPinsResponse>( &API_HANDLER_SCH::handleSyncSheetPins );

    // Annotation handlers
    registerHandler<AnnotateSymbols, AnnotateSymbolsResponse>( &API_HANDLER_SCH::handleAnnotateSymbols );
    registerHandler<ClearAnnotation, ClearAnnotationResponse>( &API_HANDLER_SCH::handleClearAnnotation );
    registerHandler<CheckAnnotation, CheckAnnotationResponse>( &API_HANDLER_SCH::handleCheckAnnotation );

    // ERC handlers
    registerHandler<RunERC, RunERCResponse>( &API_HANDLER_SCH::handleRunERC );
    registerHandler<GetERCViolations, GetERCViolationsResponse>( &API_HANDLER_SCH::handleGetERCViolations );
    registerHandler<ClearERCMarkers, ClearERCMarkersResponse>( &API_HANDLER_SCH::handleClearERCMarkers );
    registerHandler<ExcludeERCViolation, Empty>( &API_HANDLER_SCH::handleExcludeERCViolation );

    // Connectivity handlers
    registerHandler<GetNets, GetNetsResponse>( &API_HANDLER_SCH::handleGetNets );
    registerHandler<GetBuses, GetBusesResponse>( &API_HANDLER_SCH::handleGetBuses );
    registerHandler<GetNetForItem, GetNetForItemResponse>( &API_HANDLER_SCH::handleGetNetForItem );
    registerHandler<GetBusMembers, GetBusMembersResponse>( &API_HANDLER_SCH::handleGetBusMembers );
    registerHandler<GetNetItems, GetNetItemsResponse>( &API_HANDLER_SCH::handleGetNetItems );

    // Title block handlers
    registerHandler<GetTitleBlockInfo, types::TitleBlockInfo>( &API_HANDLER_SCH::handleGetTitleBlockInfo );
    registerHandler<SetTitleBlockInfo, Empty>( &API_HANDLER_SCH::handleSetTitleBlockInfo );

    // Page settings handlers
    registerHandler<GetPageSettings, types::PageInfo>( &API_HANDLER_SCH::handleGetPageSettings );
    registerHandler<SetPageSettings, Empty>( &API_HANDLER_SCH::handleSetPageSettings );

    // Document management handlers
    registerHandler<SaveDocument, Empty>( &API_HANDLER_SCH::handleSaveDocument );
    registerHandler<SaveDocumentToString, SavedDocumentResponse>( &API_HANDLER_SCH::handleSaveDocumentToString );
    registerHandler<RefreshEditor, Empty>( &API_HANDLER_SCH::handleRefreshEditor );
    registerHandler<SaveCopyOfDocument, Empty>( &API_HANDLER_SCH::handleSaveCopyOfDocument );
    registerHandler<RevertDocument, Empty>( &API_HANDLER_SCH::handleRevertDocument );
    registerHandler<SaveSelectionToString, SavedSelectionResponse>( &API_HANDLER_SCH::handleSaveSelectionToString );
    registerHandler<ParseAndCreateItemsFromString, CreateItemsResponse>( &API_HANDLER_SCH::handleParseAndCreateItemsFromString );

    // Grid settings handlers
    registerHandler<GetGridSettings, GetGridSettingsResponse>( &API_HANDLER_SCH::handleGetGridSettings );
    registerHandler<SetGridSettings, Empty>( &API_HANDLER_SCH::handleSetGridSettings );

    // ERC settings handlers
    registerHandler<GetERCSettings, GetERCSettingsResponse>( &API_HANDLER_SCH::handleGetERCSettings );
    registerHandler<SetERCSettings, Empty>( &API_HANDLER_SCH::handleSetERCSettings );

    // Net class handlers
    registerHandler<AssignNetToClass, Empty>( &API_HANDLER_SCH::handleAssignNetToClass );
    registerHandler<GetNetClasses, GetNetClassesResponse>( &API_HANDLER_SCH::handleGetNetClasses );
    registerHandler<SetNetClass, Empty>( &API_HANDLER_SCH::handleSetNetClass );
    registerHandler<DeleteNetClass, Empty>( &API_HANDLER_SCH::handleDeleteNetClass );
    registerHandler<GetNetClassAssignments, GetNetClassAssignmentsResponse>( &API_HANDLER_SCH::handleGetNetClassAssignments );
    registerHandler<SetNetClassAssignments, Empty>( &API_HANDLER_SCH::handleSetNetClassAssignments );
    registerHandler<AddNetClassAssignment, Empty>( &API_HANDLER_SCH::handleAddNetClassAssignment );
    registerHandler<RemoveNetClassAssignment, Empty>( &API_HANDLER_SCH::handleRemoveNetClassAssignment );

    // Bus alias handlers
    registerHandler<GetBusAliases, GetBusAliasesResponse>( &API_HANDLER_SCH::handleGetBusAliases );
    registerHandler<SetBusAlias, Empty>( &API_HANDLER_SCH::handleSetBusAlias );
    registerHandler<DeleteBusAlias, Empty>( &API_HANDLER_SCH::handleDeleteBusAlias );
    registerHandler<SetBusAliases, Empty>( &API_HANDLER_SCH::handleSetBusAliases );

    // Editor preferences handlers
    registerHandler<GetEditorPreferences, GetEditorPreferencesResponse>( &API_HANDLER_SCH::handleGetEditorPreferences );
    registerHandler<SetEditorPreferences, Empty>( &API_HANDLER_SCH::handleSetEditorPreferences );

    // Formatting settings handlers (project-level settings from Schematic Setup)
    registerHandler<GetFormattingSettings, GetFormattingSettingsResponse>( &API_HANDLER_SCH::handleGetFormattingSettings );
    registerHandler<SetFormattingSettings, Empty>( &API_HANDLER_SCH::handleSetFormattingSettings );

    // Field name templates handlers (project-level settings from Schematic Setup)
    registerHandler<GetFieldNameTemplates, GetFieldNameTemplatesResponse>( &API_HANDLER_SCH::handleGetFieldNameTemplates );
    registerHandler<SetFieldNameTemplates, Empty>( &API_HANDLER_SCH::handleSetFieldNameTemplates );

    // Annotation settings handlers (project-level settings from Schematic Setup)
    registerHandler<GetAnnotationSettings, GetAnnotationSettingsResponse>( &API_HANDLER_SCH::handleGetAnnotationSettings );
    registerHandler<SetAnnotationSettings, Empty>( &API_HANDLER_SCH::handleSetAnnotationSettings );

    // Simulation settings handlers
    registerHandler<GetSimulationSettings, GetSimulationSettingsResponse>( &API_HANDLER_SCH::handleGetSimulationSettings );
    registerHandler<SetSimulationSettings, Empty>( &API_HANDLER_SCH::handleSetSimulationSettings );

    // Library query handlers
    registerHandler<GetLibrarySymbols, GetLibrarySymbolsResponse>( &API_HANDLER_SCH::handleGetLibrarySymbols );
    registerHandler<SearchLibrarySymbols, SearchLibrarySymbolsResponse>( &API_HANDLER_SCH::handleSearchLibrarySymbols );
    registerHandler<GetSymbolInfo, GetSymbolInfoResponse>( &API_HANDLER_SCH::handleGetSymbolInfo );
    registerHandler<GetTransformedPinPosition, GetTransformedPinPositionResponse>( &API_HANDLER_SCH::handleGetTransformedPinPosition );
    registerHandler<GetNeededJunctions, GetNeededJunctionsResponse>( &API_HANDLER_SCH::handleGetNeededJunctions );

    // Simulation handlers
    registerHandler<RunSimulation, RunSimulationResponse>( &API_HANDLER_SCH::handleRunSimulation );
    registerHandler<GetSimulationResults, GetSimulationResultsResponse>( &API_HANDLER_SCH::handleGetSimulationResults );

    // Export handlers
    registerHandler<ExportNetlist, ExportNetlistResponse>( &API_HANDLER_SCH::handleExportNetlist );
    registerHandler<ExportBOM, ExportBOMResponse>( &API_HANDLER_SCH::handleExportBOM );
    registerHandler<ExportPlot, ExportPlotResponse>( &API_HANDLER_SCH::handleExportPlot );

    // Undo/Redo handlers
    registerHandler<GetUndoHistory, GetUndoHistoryResponse>( &API_HANDLER_SCH::handleGetUndoHistory );
    registerHandler<Undo, UndoResponse>( &API_HANDLER_SCH::handleUndo );
    registerHandler<Redo, RedoResponse>( &API_HANDLER_SCH::handleRedo );

    // Viewport handlers
    registerHandler<GetViewport, GetViewportResponse>( &API_HANDLER_SCH::handleGetViewport );
    registerHandler<SetViewport, Empty>( &API_HANDLER_SCH::handleSetViewport );
    registerHandler<ZoomToFit, Empty>( &API_HANDLER_SCH::handleZoomToFit );
    registerHandler<ZoomToItems, Empty>( &API_HANDLER_SCH::handleZoomToItems );

    // Highlighting handlers
    registerHandler<HighlightNet, Empty>( &API_HANDLER_SCH::handleHighlightNet );
    registerHandler<ClearHighlight, Empty>( &API_HANDLER_SCH::handleClearHighlight );

    // Cross-probe handlers
    registerHandler<CrossProbeToBoard, CrossProbeResponse>( &API_HANDLER_SCH::handleCrossProbeToBoard );
    registerHandler<CrossProbeFromBoard, CrossProbeFromBoardResponse>( &API_HANDLER_SCH::handleCrossProbeFromBoard );

    // ERC Pin Type Matrix handlers
    registerHandler<GetPinTypeMatrix, GetPinTypeMatrixResponse>( &API_HANDLER_SCH::handleGetPinTypeMatrix );
    registerHandler<SetPinTypeMatrix, Empty>( &API_HANDLER_SCH::handleSetPinTypeMatrix );

    // Design Block handlers
    registerHandler<GetDesignBlocks, GetDesignBlocksResponse>( &API_HANDLER_SCH::handleGetDesignBlocks );
    registerHandler<SearchDesignBlocks, SearchDesignBlocksResponse>( &API_HANDLER_SCH::handleSearchDesignBlocks );
    registerHandler<SaveSelectionAsDesignBlock, SaveDesignBlockResponse>( &API_HANDLER_SCH::handleSaveSelectionAsDesignBlock );
    registerHandler<SaveSheetAsDesignBlock, SaveDesignBlockResponse>( &API_HANDLER_SCH::handleSaveSheetAsDesignBlock );
    registerHandler<DeleteDesignBlock, DeleteDesignBlockResponse>( &API_HANDLER_SCH::handleDeleteDesignBlock );
    registerHandler<PlaceDesignBlock, PlaceDesignBlockResponse>( &API_HANDLER_SCH::handlePlaceDesignBlock );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    wxASSERT( m_frame != nullptr );

    if( !m_frame )
        return nullptr;

    return std::make_unique<SCH_COMMIT>( m_frame );
}


void API_HANDLER_SCH::pushCurrentCommit( const std::string& aClientName, const wxString& aMessage )
{
    auto it = m_commits.find( aClientName );

    if( it == m_commits.end() )
        return;

    // Use SKIP_CLEANUP to prevent the schematic cleanup from removing "unnecessary" junctions.
    // When items are created via the API, the user is in control of what gets created and
    // we should not second-guess their intentions.
    it->second.second->Push( aMessage.IsEmpty() ? m_defaultCommitMessage : aMessage, SKIP_CLEANUP );

    m_commits.erase( it );
    m_activeClients.erase( aClientName );
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
    SCH_SHEET* lastSheet = m_frame->GetCurrentSheet().Last();
    wxString lastSheetUuid = lastSheet ? lastSheet->m_Uuid.AsString() : wxString( "null" );

    wxLogTrace( "SCHEMATIC", "GetOpenDocuments: CurrentSheet size=%zu, Last UUID=%s, file=%s",
                m_frame->GetCurrentSheet().size(),
                lastSheetUuid,
                m_frame->GetCurrentFileName() );

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

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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

        // Special handling for BusEntry: check the type field to determine the actual C++ type
        if( *type == SCH_BUS_WIRE_ENTRY_T )
        {
            kiapi::schematic::types::BusEntry busEntry;
            if( anyItem.UnpackTo( &busEntry ) )
            {
                if( busEntry.type() == kiapi::schematic::types::BET_BUS_TO_BUS )
                    type = SCH_BUS_BUS_ENTRY_T;
            }
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

            // Hide non-essential fields to reduce visual clutter on the schematic sheet.
            // Only Reference and Value are shown; Description, Footprint, Datasheet, etc. are hidden.
            for( SCH_FIELD& field : symbol->GetFields() )
            {
                if( field.GetId() != FIELD_T::REFERENCE && field.GetId() != FIELD_T::VALUE )
                    field.SetVisible( false );
            }
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

                // For SCH_SYMBOL updates, we need to re-resolve the library symbol
                // because SwapItemData swaps m_part, and the temporary item has no library symbol
                if( schItem->Type() == SCH_SYMBOL_T )
                {
                    SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( schItem );
                    LIB_ID libId = symbol->GetLibId();

                    if( libId.IsValid() )
                    {
                        LIB_SYMBOL* libSymbol = m_frame->GetLibSymbol( libId );

                        if( libSymbol )
                        {
                            std::unique_ptr<LIB_SYMBOL> flattenedSymbol = libSymbol->Flatten();
                            flattenedSymbol->SetParent();
                            symbol->SetLibSymbol( flattenedSymbol.release() );
                        }
                    }
                }

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

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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
        case SCH_GROUP_T:
        {
            handledAnything = true;

            for( SCH_ITEM* item : screen->Items().OfType( type ) )
                items.emplace_back( item );

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

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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


HANDLER_RESULT<GetBoundingBoxResponse> API_HANDLER_SCH::handleGetBoundingBox(
        const HANDLER_CONTEXT<GetBoundingBox>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetBoundingBoxResponse response;
    bool includeText = aCtx.Request.mode() != BoundingBoxMode::BBM_ITEM_ONLY;

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "no screen available" );
        return tl::unexpected( e );
    }

    for( const types::KIID& idMsg : aCtx.Request.items() )
    {
        KIID id( idMsg.value() );

        for( SCH_ITEM* item : screen->Items() )
        {
            if( item->m_Uuid == id )
            {
                BOX2I bbox;

                if( item->Type() == SCH_SYMBOL_T )
                {
                    SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

                    if( includeText )
                        bbox = symbol->GetBoundingBox();
                    else
                        bbox = symbol->GetBodyAndPinsBoundingBox();
                }
                else
                {
                    bbox = item->GetBoundingBox();
                }

                response.add_items()->set_value( idMsg.value() );
                PackBox2Sch( *response.add_boxes(), bbox );
                break;
            }
        }
    }

    return response;
}


std::optional<SCH_ITEM*> API_HANDLER_SCH::getItemById( const KIID& aId )
{
    SCH_SCREEN* screen = m_frame->GetScreenForApi();

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


//
// Document Management Handlers
//

HANDLER_RESULT<CreateDocumentResponse> API_HANDLER_SCH::handleCreateDocument(
        const HANDLER_CONTEXT<CreateDocument>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
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

    // Create an empty schematic file
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
        // Create a minimal empty schematic file
        wxFileOutputStream output( path );

        if( !output.IsOk() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to create file: " + path.ToStdString() );
            return tl::unexpected( e );
        }

        wxString content = wxT( "(kicad_sch (version 20231120) (generator \"api\"))\n" );
        output.Write( content.c_str(), content.length() );
    }

    CreateDocumentResponse response;
    response.mutable_document()->set_type( DocumentType::DOCTYPE_SCHEMATIC );

    // Open the document if requested
    if( aCtx.Request.open_after_create() )
    {
        std::vector<wxString> files = { path };

        if( !m_frame->OpenProjectFiles( files ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Created file but failed to open it" );
            return tl::unexpected( e );
        }

        // Set sheet path in response
        types::SheetPath* sheetPath = response.mutable_document()->mutable_sheet_path();
        sheetPath->set_path_human_readable( "/" );
        sheetPath->add_path()->set_value( m_frame->GetCurrentSheet().Last()->m_Uuid.AsStdString() );
    }

    return response;
}


HANDLER_RESULT<OpenDocumentResponse> API_HANDLER_SCH::handleOpenDocument(
        const HANDLER_CONTEXT<OpenDocument>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
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

    std::vector<wxString> files = { path };

    if( !m_frame->OpenProjectFiles( files ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Failed to open schematic: " + path.ToStdString() );
        return tl::unexpected( e );
    }

    OpenDocumentResponse response;
    response.mutable_document()->set_type( DocumentType::DOCTYPE_SCHEMATIC );

    types::SheetPath* sheetPath = response.mutable_document()->mutable_sheet_path();
    sheetPath->set_path_human_readable( "/" );
    sheetPath->add_path()->set_value( m_frame->GetCurrentSheet().Last()->m_Uuid.AsStdString() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleCloseDocument(
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

    // Save the document if requested
    if( aCtx.Request.save_changes() && !aCtx.Request.force() )
    {
        if( !m_frame->SaveProject() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to save document before closing" );
            return tl::unexpected( e );
        }
    }

    // If force is set, clear the modify flags to prevent prompts
    if( aCtx.Request.force() )
    {
        SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();
        sheetList.ClearModifyStatus();
    }

    // Note: The schematic editor is a single-document application tied to a project.
    // A full "close" would require closing the project via the project manager.
    // For API purposes, we've saved if requested and cleared modify flags if forced.
    // The application itself remains open.

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetActiveDocument(
        const HANDLER_CONTEXT<SetActiveDocument>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    // The schematic editor is a single-document application (though with multiple sheets)
    // This handler is more relevant for multi-document scenarios
    // For now, just bring the frame to focus
    m_frame->Raise();
    m_frame->SetFocus();

    return Empty();
}


//
// Sheet Hierarchy Handlers
//

void API_HANDLER_SCH::buildSheetHierarchyNode( const SCH_SHEET_PATH& aPath,
                                               kiapi::schematic::commands::SheetHierarchyNode* aNode )
{
    SCH_SHEET* sheet = aPath.Last();

    aNode->mutable_id()->set_value( sheet->m_Uuid.AsStdString() );
    aNode->mutable_path()->set_path_human_readable( aPath.PathHumanReadable().ToStdString() );

    for( size_t i = 0; i < aPath.size(); i++ )
        aNode->mutable_path()->add_path()->set_value( aPath.GetSheet( i )->m_Uuid.AsStdString() );

    aNode->set_name( sheet->GetName().ToStdString() );
    aNode->set_filename( sheet->GetFileName().ToStdString() );
    aNode->set_page_number( aPath.GetPageNumber().ToStdString() );

    // Get direct children by iterating over sheets on this sheet's screen.
    // SCH_SHEET_LIST builds paths starting from the given sheet, so direct children
    // have size 2 (the starting sheet + the child), regardless of the full aPath depth.
    SCH_SHEET_LIST sheetList( sheet );

    for( const SCH_SHEET_PATH& childPath : sheetList )
    {
        // Direct children have size 2 (the starting sheet + the child)
        if( childPath.size() == 2 )
        {
            // Build the full path by appending the child to the current path
            SCH_SHEET_PATH fullChildPath = aPath;
            fullChildPath.push_back( childPath.Last() );
            buildSheetHierarchyNode( fullChildPath, aNode->add_children() );
        }
    }
}


SCH_SHEET* API_HANDLER_SCH::findSheetById( const KIID& aId )
{
    // First check the current screens directly (handles newly created sheets
    // that may not be fully linked in the hierarchy yet, e.g., when the parent
    // schematic is unsaved and the backing file path couldn't be resolved)
    SCH_SCREEN* currentScreen = m_frame->GetScreen();
    SCH_SCREEN* apiScreen = m_frame->GetScreenForApi();

    // Search current screen
    if( currentScreen )
    {
        for( SCH_ITEM* item : currentScreen->Items().OfType( SCH_SHEET_T ) )
        {
            if( item->m_Uuid == aId )
                return static_cast<SCH_SHEET*>( item );
        }
    }

    // Search API screen if different from current
    if( apiScreen && apiScreen != currentScreen )
    {
        for( SCH_ITEM* item : apiScreen->Items().OfType( SCH_SHEET_T ) )
        {
            if( item->m_Uuid == aId )
                return static_cast<SCH_SHEET*>( item );
        }
    }

    // Then search all screens in the schematic for sheet items
    // This ensures we find sheets regardless of which screen is currently active
    SCH_SCREENS screens( m_frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        // Skip screens we already searched
        if( screen == currentScreen || screen == apiScreen )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SHEET_T ) )
        {
            if( item->m_Uuid == aId )
                return static_cast<SCH_SHEET*>( item );
        }
    }

    // Also search through the hierarchy paths in case the sheet is found there
    SCH_SHEET_LIST sheetList = m_frame->Schematic().BuildUnorderedSheetList();

    for( const SCH_SHEET_PATH& path : sheetList )
    {
        if( path.Last()->m_Uuid == aId )
            return path.Last();
    }

    return nullptr;
}


HANDLER_RESULT<kiapi::schematic::commands::GetSheetHierarchyResponse>
API_HANDLER_SCH::handleGetSheetHierarchy(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetSheetHierarchy>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetSheetHierarchyResponse response;

    // Use top-level sheets for hierarchy building to support both old and new schematic formats.
    // Old schematics have a single root sheet; new ones use a virtual root with top-level sheets.
    std::vector<SCH_SHEET*> topLevelSheets = m_frame->Schematic().GetTopLevelSheets();

    if( topLevelSheets.empty() )
    {
        // Fall back to virtual root if no top-level sheets
        SCH_SHEET_PATH rootPath;
        rootPath.push_back( &m_frame->Schematic().Root() );
        buildSheetHierarchyNode( rootPath, response.mutable_root() );
    }
    else if( topLevelSheets.size() == 1 )
    {
        // Single top-level sheet - use it as the root
        SCH_SHEET_PATH rootPath;
        rootPath.push_back( topLevelSheets[0] );
        buildSheetHierarchyNode( rootPath, response.mutable_root() );
    }
    else
    {
        // Multiple top-level sheets - create virtual root node with children
        auto* rootNode = response.mutable_root();
        rootNode->mutable_id()->set_value( m_frame->Schematic().Root().m_Uuid.AsStdString() );
        rootNode->mutable_path()->set_path_human_readable( "/" );
        rootNode->set_name( "" );
        rootNode->set_filename( "" );
        rootNode->set_page_number( "" );

        for( SCH_SHEET* sheet : topLevelSheets )
        {
            SCH_SHEET_PATH sheetPath;
            sheetPath.push_back( sheet );
            buildSheetHierarchyNode( sheetPath, rootNode->add_children() );
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetCurrentSheetResponse>
API_HANDLER_SCH::handleGetCurrentSheet(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetCurrentSheet>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetCurrentSheetResponse response;

    const SCH_SHEET_PATH& currentPath = m_frame->GetCurrentSheet();

    response.mutable_current_sheet()->set_path_human_readable(
            currentPath.PathHumanReadable().ToStdString() );

    for( size_t i = 0; i < currentPath.size(); i++ )
        response.mutable_current_sheet()->add_path()->set_value( currentPath.GetSheet( i )->m_Uuid.AsStdString() );

    // Populate sheet_info
    SCH_SHEET* sheet = currentPath.Last();
    kiapi::schematic::types::Sheet* sheetInfo = response.mutable_sheet_info();

    sheetInfo->mutable_id()->set_value( sheet->m_Uuid.AsStdString() );
    sheetInfo->set_name( sheet->GetName().ToStdString() );
    sheetInfo->set_filename( sheet->GetFileName().ToStdString() );
    sheetInfo->set_page_number( currentPath.GetPageNumber().ToStdString() );

    // Position and size - convert from schematic IU to nanometers
    VECTOR2I pos = sheet->GetPosition();
    VECTOR2I size = sheet->GetSize();
    kiapi::common::PackVector2Sch( *sheetInfo->mutable_position(), pos );
    kiapi::common::PackVector2Sch( *sheetInfo->mutable_size(), size );

    // Add pins
    for( SCH_SHEET_PIN* pin : sheet->GetPins() )
    {
        kiapi::schematic::types::SheetPin* pinInfo = sheetInfo->add_pins();
        pinInfo->mutable_id()->set_value( pin->m_Uuid.AsStdString() );
        pinInfo->set_name( pin->GetText().ToStdString() );

        VECTOR2I pinPos = pin->GetPosition();
        kiapi::common::PackVector2Sch( *pinInfo->mutable_position(), pinPos );

        // Map sheet pin side
        switch( pin->GetSide() )
        {
        case SHEET_SIDE::LEFT:   pinInfo->set_side( kiapi::schematic::types::SPS_LEFT ); break;
        case SHEET_SIDE::RIGHT:  pinInfo->set_side( kiapi::schematic::types::SPS_RIGHT ); break;
        case SHEET_SIDE::TOP:    pinInfo->set_side( kiapi::schematic::types::SPS_TOP ); break;
        case SHEET_SIDE::BOTTOM: pinInfo->set_side( kiapi::schematic::types::SPS_BOTTOM ); break;
        default:                 pinInfo->set_side( kiapi::schematic::types::SPS_UNKNOWN ); break;
        }

        // Map label shape
        switch( pin->GetShape() )
        {
        case LABEL_FLAG_SHAPE::L_INPUT:       pinInfo->set_shape( kiapi::schematic::types::LS_INPUT ); break;
        case LABEL_FLAG_SHAPE::L_OUTPUT:      pinInfo->set_shape( kiapi::schematic::types::LS_OUTPUT ); break;
        case LABEL_FLAG_SHAPE::L_BIDI:        pinInfo->set_shape( kiapi::schematic::types::LS_BIDI ); break;
        case LABEL_FLAG_SHAPE::L_TRISTATE:    pinInfo->set_shape( kiapi::schematic::types::LS_TRISTATE ); break;
        case LABEL_FLAG_SHAPE::L_UNSPECIFIED: pinInfo->set_shape( kiapi::schematic::types::LS_UNSPECIFIED ); break;
        default:                              pinInfo->set_shape( kiapi::schematic::types::LS_UNKNOWN ); break;
        }
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleNavigateToSheet(
        const HANDLER_CONTEXT<kiapi::schematic::commands::NavigateToSheet>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    // Build the target sheet path from the request
    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    // Find the sheet path matching the request
    SCH_SHEET_PATH targetPath;
    bool found = false;

    // First, try to match by path KIIDs
    if( aCtx.Request.target_sheet().path_size() > 0 )
    {
        for( const SCH_SHEET_PATH& path : sheetList )
        {
            if( path.size() != static_cast<size_t>( aCtx.Request.target_sheet().path_size() ) )
                continue;

            bool matches = true;

            for( size_t i = 0; i < path.size(); i++ )
            {
                if( path.GetSheet( i )->m_Uuid.AsStdString() != aCtx.Request.target_sheet().path( i ).value() )
                {
                    matches = false;
                    break;
                }
            }

            if( matches )
            {
                targetPath = path;
                found = true;
                break;
            }
        }
    }

    // Fall back to matching by human-readable path
    if( !found && !aCtx.Request.target_sheet().path_human_readable().empty() )
    {
        wxString targetHumanPath = wxString::FromUTF8( aCtx.Request.target_sheet().path_human_readable() );

        for( const SCH_SHEET_PATH& path : sheetList )
        {
            if( path.PathHumanReadable() == targetHumanPath )
            {
                targetPath = path;
                found = true;
                break;
            }
        }
    }

    if( !found )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Target sheet path not found" );
        return tl::unexpected( e );
    }

    if( m_frame->IsAgentTransactionActive() && targetPath.size() > 0 )
    {
        // During agent transactions, only update the target sheet for API operations
        // without changing the user's visible view
        m_frame->SetAgentTargetSheet( targetPath.Last()->m_Uuid );
    }
    else
    {
        // User-initiated or non-transaction navigation — change the visible view
        m_frame->SetCurrentSheet( targetPath );
        m_frame->DisplayCurrentSheet();
    }

    return Empty();
}


//
// Sheet CRUD Handlers
//

HANDLER_RESULT<kiapi::schematic::commands::CreateSheetResponse>
API_HANDLER_SCH::handleCreateSheet(
        const HANDLER_CONTEXT<kiapi::schematic::commands::CreateSheet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    auto containerResult = validateItemHeaderDocument( aCtx.Request.header() );

    if( !containerResult )
    {
        ApiResponseStatus e;
        e.CopyFrom( containerResult.error() );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No active schematic screen" );
        return tl::unexpected( e );
    }

    // Get position and size from request - convert from nanometers to schematic IU
    VECTOR2I pos = kiapi::common::UnpackVector2Sch( aCtx.Request.position() );
    VECTOR2I size = kiapi::common::UnpackVector2Sch( aCtx.Request.size() );

    // Default size if not specified
    if( size.x == 0 || size.y == 0 )
    {
        size.x = schIUScale.MilsToIU( 500 );
        size.y = schIUScale.MilsToIU( 400 );
    }

    // Create the sheet and its backing screen
    SCH_SHEET* newSheet = new SCH_SHEET( m_frame->GetCurrentSheet().Last(), pos, size );
    SCH_SCREEN* newScreen = new SCH_SCREEN( &m_frame->Schematic() );
    newSheet->SetScreen( newScreen );
    newSheet->SetName( wxString::FromUTF8( aCtx.Request.name() ) );
    newSheet->SetFileName( wxString::FromUTF8( aCtx.Request.filename() ) );
    newScreen->SetFileName( wxString::FromUTF8( aCtx.Request.filename() ) );

    // Create the backing file if requested
    if( aCtx.Request.create_file() )
    {
        wxString filename = wxString::FromUTF8( aCtx.Request.filename() );
        wxFileName sheetFn( filename );

        // If not absolute, make it relative to the current schematic
        if( !sheetFn.IsAbsolute() )
        {
            wxFileName currentFn( m_frame->GetCurrentFileName() );
            sheetFn.MakeAbsolute( currentFn.GetPath() );
        }

        if( !wxFileName::FileExists( sheetFn.GetFullPath() ) )
        {
            // Create an empty schematic file
            wxFileOutputStream output( sheetFn.GetFullPath() );

            if( output.IsOk() )
            {
                wxString content = wxT( "(kicad_sch (version 20231120) (generator \"api\"))\n" );
                output.Write( content.c_str(), content.length() );
            }
        }
    }

    // Add the sheet to the screen first, then notify commit it's already added.
    // This is required because SCH_COMMIT::Stage() calls undoLevelItem() which returns
    // the parent sheet (since new sheet has parent set), causing CHT_ADD to become CHT_MODIFY.
    // Using AddToScreen() + Added() bypasses this issue (same pattern as sch_drawing_tools.cpp).
    m_frame->AddToScreen( newSheet );

    SCH_COMMIT commit( m_frame );
    commit.Added( newSheet, screen );
    commit.Push( _( "API: Create Sheet" ) );

    // Refresh the hierarchy cache so the new sheet can be found
    m_frame->Schematic().RefreshHierarchy();

    // Build response
    kiapi::schematic::commands::CreateSheetResponse response;
    response.mutable_sheet_id()->set_value( newSheet->m_Uuid.AsStdString() );

    // Build the new sheet path
    SCH_SHEET_PATH newPath = m_frame->GetCurrentSheet();
    newPath.push_back( newSheet );
    response.mutable_new_sheet_path()->set_path_human_readable( newPath.PathHumanReadable().ToStdString() );

    for( size_t i = 0; i < newPath.size(); i++ )
        response.mutable_new_sheet_path()->add_path()->set_value( newPath.GetSheet( i )->m_Uuid.AsStdString() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleDeleteSheet(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteSheet>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    KIID sheetId( aCtx.Request.sheet_id().value() );
    SCH_SHEET* sheet = findSheetById( sheetId );

    if( !sheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
        return tl::unexpected( e );
    }

    // Don't allow deleting the root sheet
    if( sheet == &m_frame->Schematic().Root() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Cannot delete the root sheet" );
        return tl::unexpected( e );
    }

    // Get the filename before deleting if we need to delete the file
    wxString filename;
    if( aCtx.Request.delete_file() )
    {
        wxFileName sheetFn( sheet->GetFileName() );
        wxFileName currentFn( m_frame->GetCurrentFileName() );
        sheetFn.MakeAbsolute( currentFn.GetPath() );
        filename = sheetFn.GetFullPath();
    }

    // Find the screen containing this sheet
    SCH_SCREEN* screen = nullptr;
    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    for( const SCH_SHEET_PATH& path : sheetList )
    {
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            if( item == sheet )
            {
                screen = path.LastScreen();
                break;
            }
        }
        if( screen )
            break;
    }

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Could not find screen containing sheet" );
        return tl::unexpected( e );
    }

    // Delete the sheet
    SCH_COMMIT commit( m_frame );
    commit.Remove( sheet, screen );
    commit.Push( _( "API: Delete Sheet" ) );

    // Refresh the hierarchy cache
    m_frame->Schematic().RefreshHierarchy();

    // Delete the file if requested
    if( aCtx.Request.delete_file() && !filename.IsEmpty() && wxFileName::FileExists( filename ) )
    {
        wxRemoveFile( filename );
    }

    return Empty();
}


HANDLER_RESULT<kiapi::schematic::commands::GetSheetPropertiesResponse>
API_HANDLER_SCH::handleGetSheetProperties(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetSheetProperties>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID sheetId( aCtx.Request.sheet_id().value() );
    SCH_SHEET* sheet = findSheetById( sheetId );

    if( !sheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetSheetPropertiesResponse response;
    kiapi::schematic::types::Sheet* sheetInfo = response.mutable_sheet();

    sheetInfo->mutable_id()->set_value( sheet->m_Uuid.AsStdString() );
    sheetInfo->set_name( sheet->GetName().ToStdString() );
    sheetInfo->set_filename( sheet->GetFileName().ToStdString() );

    // Convert from schematic IU to nanometers
    VECTOR2I pos = sheet->GetPosition();
    VECTOR2I size = sheet->GetSize();
    kiapi::common::PackVector2Sch( *sheetInfo->mutable_position(), pos );
    kiapi::common::PackVector2Sch( *sheetInfo->mutable_size(), size );

    // Find page number from hierarchy
    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();
    for( const SCH_SHEET_PATH& path : sheetList )
    {
        if( path.Last() == sheet )
        {
            sheetInfo->set_page_number( path.GetPageNumber().ToStdString() );
            break;
        }
    }

    // Add pins
    for( SCH_SHEET_PIN* pin : sheet->GetPins() )
    {
        kiapi::schematic::types::SheetPin* pinInfo = sheetInfo->add_pins();
        pinInfo->mutable_id()->set_value( pin->m_Uuid.AsStdString() );
        pinInfo->set_name( pin->GetText().ToStdString() );

        VECTOR2I pinPos = pin->GetPosition();
        kiapi::common::PackVector2Sch( *pinInfo->mutable_position(), pinPos );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetSheetProperties(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SetSheetProperties>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    KIID sheetId( aCtx.Request.sheet_id().value() );
    SCH_SHEET* sheet = findSheetById( sheetId );

    if( !sheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
        return tl::unexpected( e );
    }

    // Find the screen containing this sheet
    SCH_SCREEN* screen = nullptr;
    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    for( const SCH_SHEET_PATH& path : sheetList )
    {
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            if( item == sheet )
            {
                screen = path.LastScreen();
                break;
            }
        }
        if( screen )
            break;
    }

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Could not find screen containing sheet" );
        return tl::unexpected( e );
    }

    SCH_COMMIT commit( m_frame );
    commit.Modify( sheet, screen );

    // Update properties if provided
    if( aCtx.Request.has_name() )
        sheet->SetName( wxString::FromUTF8( aCtx.Request.name() ) );

    if( aCtx.Request.has_filename() )
        sheet->SetFileName( wxString::FromUTF8( aCtx.Request.filename() ) );

    // Page number is stored in the sheet path, not the sheet itself
    // This requires finding the path and updating it
    if( aCtx.Request.has_page_number() )
    {
        for( SCH_SHEET_PATH& path : sheetList )
        {
            if( path.Last() == sheet )
            {
                path.SetPageNumber( wxString::FromUTF8( aCtx.Request.page_number() ) );
                break;
            }
        }
    }

    commit.Push( _( "API: Update Sheet Properties" ) );

    return Empty();
}


//
// Sheet Pin Handlers
//

HANDLER_RESULT<kiapi::schematic::commands::CreateSheetPinResponse>
API_HANDLER_SCH::handleCreateSheetPin(
        const HANDLER_CONTEXT<kiapi::schematic::commands::CreateSheetPin>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    KIID sheetId( aCtx.Request.sheet_id().value() );
    SCH_SHEET* sheet = findSheetById( sheetId );

    if( !sheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
        return tl::unexpected( e );
    }

    // Find the screen containing this sheet
    SCH_SCREEN* screen = nullptr;
    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    for( const SCH_SHEET_PATH& path : sheetList )
    {
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            if( item == sheet )
            {
                screen = path.LastScreen();
                break;
            }
        }
        if( screen )
            break;
    }

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Could not find screen containing sheet" );
        return tl::unexpected( e );
    }

    // Create the pin
    SCH_SHEET_PIN* pin = new SCH_SHEET_PIN( sheet );
    pin->SetText( wxString::FromUTF8( aCtx.Request.name() ) );

    // Convert from nanometers to schematic IU
    VECTOR2I pos = kiapi::common::UnpackVector2Sch( aCtx.Request.position() );
    pin->SetPosition( pos );

    // Map side from protobuf
    switch( aCtx.Request.side() )
    {
    case kiapi::schematic::types::SPS_LEFT:   pin->SetSide( SHEET_SIDE::LEFT ); break;
    case kiapi::schematic::types::SPS_RIGHT:  pin->SetSide( SHEET_SIDE::RIGHT ); break;
    case kiapi::schematic::types::SPS_TOP:    pin->SetSide( SHEET_SIDE::TOP ); break;
    case kiapi::schematic::types::SPS_BOTTOM: pin->SetSide( SHEET_SIDE::BOTTOM ); break;
    default:                                  pin->SetSide( SHEET_SIDE::LEFT ); break;
    }

    // Map shape from protobuf
    switch( aCtx.Request.shape() )
    {
    case kiapi::schematic::types::LS_INPUT:       pin->SetShape( LABEL_FLAG_SHAPE::L_INPUT ); break;
    case kiapi::schematic::types::LS_OUTPUT:      pin->SetShape( LABEL_FLAG_SHAPE::L_OUTPUT ); break;
    case kiapi::schematic::types::LS_BIDI:        pin->SetShape( LABEL_FLAG_SHAPE::L_BIDI ); break;
    case kiapi::schematic::types::LS_TRISTATE:    pin->SetShape( LABEL_FLAG_SHAPE::L_TRISTATE ); break;
    case kiapi::schematic::types::LS_UNSPECIFIED: pin->SetShape( LABEL_FLAG_SHAPE::L_UNSPECIFIED ); break;
    default:                                      pin->SetShape( LABEL_FLAG_SHAPE::L_UNSPECIFIED ); break;
    }

    // Add pin to sheet
    SCH_COMMIT commit( m_frame );
    commit.Modify( sheet, screen );
    sheet->AddPin( pin );
    commit.Push( _( "API: Create Sheet Pin" ) );

    kiapi::schematic::commands::CreateSheetPinResponse response;
    response.mutable_pin_id()->set_value( pin->m_Uuid.AsStdString() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleDeleteSheetPin(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteSheetPin>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    KIID pinId( aCtx.Request.pin_id().value() );

    // Find the pin and its parent sheet
    SCH_SHEET* parentSheet = nullptr;
    SCH_SHEET_PIN* targetPin = nullptr;
    SCH_SCREEN* screen = nullptr;

    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    for( const SCH_SHEET_PATH& path : sheetList )
    {
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( item ) )
            {
                for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                {
                    if( pin->m_Uuid == pinId )
                    {
                        parentSheet = sheet;
                        targetPin = pin;
                        screen = path.LastScreen();
                        break;
                    }
                }
            }
            if( targetPin )
                break;
        }
        if( targetPin )
            break;
    }

    if( !targetPin || !parentSheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet pin not found with ID: " + pinId.AsStdString() );
        return tl::unexpected( e );
    }

    SCH_COMMIT commit( m_frame );
    commit.Modify( parentSheet, screen );
    parentSheet->RemovePin( targetPin );
    commit.Push( _( "API: Delete Sheet Pin" ) );

    return Empty();
}


HANDLER_RESULT<kiapi::schematic::commands::GetSheetPinsResponse>
API_HANDLER_SCH::handleGetSheetPins(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetSheetPins>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID sheetId( aCtx.Request.sheet_id().value() );
    SCH_SHEET* sheet = findSheetById( sheetId );

    if( !sheet )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetSheetPinsResponse response;

    for( SCH_SHEET_PIN* pin : sheet->GetPins() )
    {
        kiapi::schematic::types::SheetPin* pinInfo = response.add_pins();
        pinInfo->mutable_id()->set_value( pin->m_Uuid.AsStdString() );
        pinInfo->set_name( pin->GetText().ToStdString() );

        VECTOR2I pinPos = pin->GetPosition();
        kiapi::common::PackVector2Sch( *pinInfo->mutable_position(), pinPos );

        // Map side
        switch( pin->GetSide() )
        {
        case SHEET_SIDE::LEFT:   pinInfo->set_side( kiapi::schematic::types::SPS_LEFT ); break;
        case SHEET_SIDE::RIGHT:  pinInfo->set_side( kiapi::schematic::types::SPS_RIGHT ); break;
        case SHEET_SIDE::TOP:    pinInfo->set_side( kiapi::schematic::types::SPS_TOP ); break;
        case SHEET_SIDE::BOTTOM: pinInfo->set_side( kiapi::schematic::types::SPS_BOTTOM ); break;
        default:                 pinInfo->set_side( kiapi::schematic::types::SPS_UNKNOWN ); break;
        }

        // Map shape
        switch( pin->GetShape() )
        {
        case LABEL_FLAG_SHAPE::L_INPUT:       pinInfo->set_shape( kiapi::schematic::types::LS_INPUT ); break;
        case LABEL_FLAG_SHAPE::L_OUTPUT:      pinInfo->set_shape( kiapi::schematic::types::LS_OUTPUT ); break;
        case LABEL_FLAG_SHAPE::L_BIDI:        pinInfo->set_shape( kiapi::schematic::types::LS_BIDI ); break;
        case LABEL_FLAG_SHAPE::L_TRISTATE:    pinInfo->set_shape( kiapi::schematic::types::LS_TRISTATE ); break;
        case LABEL_FLAG_SHAPE::L_UNSPECIFIED: pinInfo->set_shape( kiapi::schematic::types::LS_UNSPECIFIED ); break;
        default:                              pinInfo->set_shape( kiapi::schematic::types::LS_UNKNOWN ); break;
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SyncSheetPinsResponse>
API_HANDLER_SCH::handleSyncSheetPins(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SyncSheetPins>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    int pinsAdded = 0;
    int pinsRemoved = 0;

    SCH_SHEET_LIST sheetList = m_frame->Schematic().Hierarchy();

    // If sheet_id is specified, sync only that sheet; otherwise sync all
    std::vector<SCH_SHEET*> sheetsToSync;

    if( !aCtx.Request.sheet_id().value().empty() )
    {
        KIID sheetId( aCtx.Request.sheet_id().value() );
        SCH_SHEET* sheet = findSheetById( sheetId );

        if( !sheet )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Sheet not found with ID: " + sheetId.AsStdString() );
            return tl::unexpected( e );
        }

        sheetsToSync.push_back( sheet );
    }
    else
    {
        // Sync all sheets (except root)
        for( const SCH_SHEET_PATH& path : sheetList )
        {
            SCH_SHEET* sheet = path.Last();
            if( sheet != &m_frame->Schematic().Root() )
                sheetsToSync.push_back( sheet );
        }
    }

    SCH_COMMIT commit( m_frame );

    for( SCH_SHEET* sheet : sheetsToSync )
    {
        // Find the screen containing this sheet and its child screen
        SCH_SCREEN* parentScreen = nullptr;
        SCH_SCREEN* childScreen = sheet->GetScreen();

        for( const SCH_SHEET_PATH& path : sheetList )
        {
            for( SCH_ITEM* item : path.LastScreen()->Items() )
            {
                if( item == sheet )
                {
                    parentScreen = path.LastScreen();
                    break;
                }
            }
            if( parentScreen )
                break;
        }

        if( !parentScreen || !childScreen )
            continue;

        commit.Modify( sheet, parentScreen );

        // Get existing pin names
        std::set<wxString> existingPinNames;
        for( SCH_SHEET_PIN* pin : sheet->GetPins() )
            existingPinNames.insert( pin->GetText() );

        // Get hierarchical labels from child sheet
        std::set<wxString> labelNames;
        for( SCH_ITEM* item : childScreen->Items() )
        {
            if( item->Type() == SCH_HIER_LABEL_T )
            {
                SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
                labelNames.insert( label->GetText() );
            }
        }

        // Add pins for labels that don't have pins, positioned along the left edge
        std::vector<wxString> newPinNames;
        for( const wxString& labelName : labelNames )
        {
            if( !existingPinNames.count( labelName ) )
                newPinNames.push_back( labelName );
        }

        if( !newPinNames.empty() )
        {
            // Find the lowest existing pin Y on the left edge to stack below it
            VECTOR2I sheetPos = sheet->GetPosition();
            VECTOR2I sheetSize = sheet->GetSize();
            int topY = sheetPos.y;
            int botY = sheetPos.y + sheetSize.y;
            int nextY = topY + schIUScale.MilsToIU( 100 );  // Start 100 mils from top

            for( SCH_SHEET_PIN* pin : sheet->GetPins() )
            {
                if( pin->GetSide() == SHEET_SIDE::LEFT && pin->GetTextPos().y >= nextY )
                    nextY = pin->GetTextPos().y + schIUScale.MilsToIU( 100 );
            }

            int pinSpacing = schIUScale.MilsToIU( 100 );  // 100 mils between pins

            for( const wxString& labelName : newPinNames )
            {
                SCH_SHEET_PIN* newPin = new SCH_SHEET_PIN( sheet );
                newPin->SetText( labelName );
                newPin->SetSide( SHEET_SIDE::LEFT );

                // Position on the left edge, clamped within the sheet box
                int pinY = std::min( nextY, botY - schIUScale.MilsToIU( 50 ) );
                newPin->SetTextPos( VECTOR2I( sheetPos.x, pinY ) );
                newPin->SetSide( SHEET_SIDE::LEFT );  // Re-set to fix X after SetTextPos
                nextY += pinSpacing;

                // Find the matching label to get its shape
                for( SCH_ITEM* item : childScreen->Items() )
                {
                    if( item->Type() == SCH_HIER_LABEL_T )
                    {
                        SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
                        if( label->GetText() == labelName )
                        {
                            newPin->SetShape( label->GetShape() );
                            break;
                        }
                    }
                }

                sheet->AddPin( newPin );
                pinsAdded++;
            }
        }

        // Remove pins for labels that no longer exist
        std::vector<SCH_SHEET_PIN*> pinsToRemove;
        for( SCH_SHEET_PIN* pin : sheet->GetPins() )
        {
            if( !labelNames.count( pin->GetText() ) )
                pinsToRemove.push_back( pin );
        }

        for( SCH_SHEET_PIN* pin : pinsToRemove )
        {
            sheet->RemovePin( pin );
            pinsRemoved++;
        }
    }

    commit.Push( _( "API: Sync Sheet Pins" ) );

    kiapi::schematic::commands::SyncSheetPinsResponse response;
    response.set_pins_added( pinsAdded );
    response.set_pins_removed( pinsRemoved );

    return response;
}


// =============================================================================
// Annotation Handlers
// =============================================================================

HANDLER_RESULT<kiapi::schematic::commands::AnnotateSymbolsResponse>
API_HANDLER_SCH::handleAnnotateSymbols(
        const HANDLER_CONTEXT<kiapi::schematic::commands::AnnotateSymbols>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::AnnotateSymbolsResponse response;

    // Map protobuf enums to KiCad enums
    ANNOTATE_SCOPE_T scope = ANNOTATE_ALL;
    switch( aCtx.Request.scope() )
    {
    case kiapi::schematic::commands::AS_CURRENT_SHEET: scope = ANNOTATE_CURRENT_SHEET; break;
    case kiapi::schematic::commands::AS_SELECTION:     scope = ANNOTATE_SELECTION; break;
    default:                                           scope = ANNOTATE_ALL; break;
    }

    ANNOTATE_ORDER_T order = SORT_BY_X_POSITION;
    switch( aCtx.Request.order() )
    {
    case kiapi::schematic::commands::AO_Y_X: order = SORT_BY_Y_POSITION; break;
    default:                                 order = SORT_BY_X_POSITION; break;
    }

    ANNOTATE_ALGO_T algo = INCREMENTAL_BY_REF;
    switch( aCtx.Request.algorithm() )
    {
    case kiapi::schematic::commands::AA_RESTART: algo = SHEET_NUMBER_X_100; break;
    default:                                     algo = INCREMENTAL_BY_REF; break;
    }

    int startNumber = aCtx.Request.start_number() > 0 ? aCtx.Request.start_number() : 1;

    // Create a reporter to capture annotation results
    class ApiAnnotationReporter : public REPORTER
    {
    public:
        REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
        {
            if( aSeverity == RPT_SEVERITY_ACTION )
                m_messages.push_back( aText.ToStdString() );
            return *this;
        }

        bool HasMessage() const override { return !m_messages.empty(); }
        std::vector<std::string> m_messages;
    };

    ApiAnnotationReporter reporter;

    SCH_COMMIT commit( m_frame );
    m_frame->AnnotateSymbols( &commit, scope, order, algo, aCtx.Request.recursive(),
                              startNumber, aCtx.Request.reset_existing(),
                              aCtx.Request.repair_timestamps(), reporter );
    commit.Push( _( "API: Annotate Symbols" ) );

    response.set_symbols_annotated( reporter.m_messages.size() );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::ClearAnnotationResponse>
API_HANDLER_SCH::handleClearAnnotation(
        const HANDLER_CONTEXT<kiapi::schematic::commands::ClearAnnotation>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::ClearAnnotationResponse response;

    ANNOTATE_SCOPE_T scope = ANNOTATE_ALL;
    switch( aCtx.Request.scope() )
    {
    case kiapi::schematic::commands::AS_CURRENT_SHEET: scope = ANNOTATE_CURRENT_SHEET; break;
    case kiapi::schematic::commands::AS_SELECTION:     scope = ANNOTATE_SELECTION; break;
    default:                                           scope = ANNOTATE_ALL; break;
    }

    // Count symbols before clearing
    SCH_REFERENCE_LIST refs;
    m_frame->Schematic().Hierarchy().GetSymbols( refs );
    int annotatedBefore = 0;
    for( size_t i = 0; i < refs.GetCount(); i++ )
    {
        if( refs[i].GetSymbol()->IsAnnotated( &refs[i].GetSheetPath() ) )
            annotatedBefore++;
    }

    class NullReporter : public REPORTER
    {
    public:
        REPORTER& Report( const wxString&, SEVERITY = RPT_SEVERITY_UNDEFINED ) override { return *this; }
        bool HasMessage() const override { return false; }
    };

    NullReporter reporter;
    m_frame->DeleteAnnotation( scope, aCtx.Request.recursive(), reporter );

    // Count symbols after clearing
    refs.Clear();
    m_frame->Schematic().Hierarchy().GetSymbols( refs );
    int annotatedAfter = 0;
    for( size_t i = 0; i < refs.GetCount(); i++ )
    {
        if( refs[i].GetSymbol()->IsAnnotated( &refs[i].GetSheetPath() ) )
            annotatedAfter++;
    }

    response.set_symbols_cleared( annotatedBefore - annotatedAfter );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::CheckAnnotationResponse>
API_HANDLER_SCH::handleCheckAnnotation(
        const HANDLER_CONTEXT<kiapi::schematic::commands::CheckAnnotation>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::CheckAnnotationResponse response;

    ANNOTATE_SCOPE_T scope = ANNOTATE_ALL;
    switch( aCtx.Request.scope() )
    {
    case kiapi::schematic::commands::AS_CURRENT_SHEET: scope = ANNOTATE_CURRENT_SHEET; break;
    case kiapi::schematic::commands::AS_SELECTION:     scope = ANNOTATE_SELECTION; break;
    default:                                           scope = ANNOTATE_ALL; break;
    }

    // Collect errors
    std::vector<std::tuple<ERCE_T, wxString, KIID, KIID>> errors;

    auto errorHandler = [&]( ERCE_T aType, const wxString& aMsg,
                             SCH_REFERENCE* aRef1, SCH_REFERENCE* aRef2 )
    {
        KIID id1 = aRef1 ? aRef1->GetSymbol()->m_Uuid : KIID();
        KIID id2 = aRef2 ? aRef2->GetSymbol()->m_Uuid : KIID();
        errors.push_back( { aType, aMsg, id1, id2 } );
    };

    int errorCount = m_frame->CheckAnnotate( errorHandler, scope, aCtx.Request.recursive() );

    response.set_error_count( errorCount );

    for( const auto& [errorType, msg, id1, id2] : errors )
    {
        auto* error = response.add_errors();

        // Map error type to string
        switch( errorType )
        {
        case ERCE_DUPLICATE_REFERENCE:    error->set_error_type( "ERCE_DUPLICATE_REFERENCE" ); break;
        case ERCE_UNANNOTATED:            error->set_error_type( "ERCE_UNANNOTATED" ); break;
        default:                          error->set_error_type( "ERCE_UNKNOWN" ); break;
        }

        error->set_message( msg.ToStdString() );

        if( !id1.AsString().IsEmpty() )
            error->add_symbol_ids()->set_value( id1.AsStdString() );
        if( !id2.AsString().IsEmpty() )
            error->add_symbol_ids()->set_value( id2.AsStdString() );
    }

    return response;
}


// =============================================================================
// ERC Handlers
// =============================================================================

HANDLER_RESULT<kiapi::schematic::commands::RunERCResponse>
API_HANDLER_SCH::handleRunERC( const HANDLER_CONTEXT<kiapi::schematic::commands::RunERC>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::RunERCResponse response;

    // Record existing exclusions before clearing markers (matches dialog_erc behavior)
    m_frame->Schematic().RecordERCExclusions();

    // Clear existing ERC markers before running new tests to prevent accumulation
    SCH_SCREENS allScreens( m_frame->Schematic().Root() );
    allScreens.DeleteAllMarkers( MARKER_BASE::MARKER_ERC, true );

    // Check for annotation errors (unannotated symbols, duplicate references)
    int annotationErrors = m_frame->CheckAnnotate(
            []( ERCE_T aType, const wxString& aMsg, SCH_REFERENCE* aItemA, SCH_REFERENCE* aItemB )
            {
                std::shared_ptr<ERC_ITEM> ercItem = ERC_ITEM::Create( aType );
                ercItem->SetErrorMessage( aMsg );

                if( aItemB )
                    ercItem->SetItems( aItemA->GetSymbol(), aItemB->GetSymbol() );
                else
                    ercItem->SetItems( aItemA->GetSymbol() );

                SCH_MARKER* marker = new SCH_MARKER( std::move( ercItem ),
                                                     aItemA->GetSymbol()->GetPosition() );
                aItemA->GetSheetPath().LastScreen()->Append( marker );
            } );

    wxLogTrace( "SCHEMATIC", "handleRunERC: CheckAnnotate found %d annotation errors",
                annotationErrors );

    // Run ERC tests
    ERC_TESTER tester( &m_frame->Schematic() );
    tester.RunTests( nullptr, m_frame, nullptr, &m_frame->Prj(), nullptr );

    // Collect markers from all screens
    int errorCount = 0;
    int warningCount = 0;

    SCH_SCREENS screens( m_frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );
            const std::shared_ptr<RC_ITEM>& rcItem = marker->GetRCItem();

            auto* violation = response.add_violations();
            violation->mutable_id()->set_value( marker->m_Uuid.AsStdString() );
            violation->set_error_code( std::to_string( rcItem->GetErrorCode() ) );
            violation->set_description( rcItem->GetErrorText( true ).ToStdString() );

            // Set severity
            switch( marker->GetSeverity() )
            {
            case RPT_SEVERITY_ERROR:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_ERROR );
                errorCount++;
                break;
            case RPT_SEVERITY_WARNING:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_WARNING );
                warningCount++;
                break;
            case RPT_SEVERITY_EXCLUSION:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_EXCLUDED );
                break;
            default:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_UNKNOWN );
                break;
            }

            // Position - convert from schematic IU to nanometers
            kiapi::common::PackVector2Sch( *violation->mutable_position(), marker->GetPosition() );

            // Item IDs
            if( rcItem->GetMainItemID() != niluuid )
                violation->add_item_ids()->set_value( rcItem->GetMainItemID().AsStdString() );
            if( rcItem->GetAuxItemID() != niluuid )
                violation->add_item_ids()->set_value( rcItem->GetAuxItemID().AsStdString() );
        }
    }

    response.set_error_count( errorCount );
    response.set_warning_count( warningCount );

    wxLogTrace( "SCHEMATIC", "handleRunERC: %d errors, %d warnings (%d annotation errors)",
                errorCount, warningCount, annotationErrors );

    m_frame->GetCanvas()->Refresh();

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetERCViolationsResponse>
API_HANDLER_SCH::handleGetERCViolations(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetERCViolations>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetERCViolationsResponse response;

    SCH_SCREENS screens( m_frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );
            const std::shared_ptr<RC_ITEM>& rcItem = marker->GetRCItem();

            // Apply severity filter if specified
            if( aCtx.Request.filter_severity() != kiapi::schematic::commands::ERC_SEV_UNKNOWN )
            {
                bool matches = false;
                switch( marker->GetSeverity() )
                {
                case RPT_SEVERITY_ERROR:
                    matches = aCtx.Request.filter_severity() == kiapi::schematic::commands::ERC_SEV_ERROR;
                    break;
                case RPT_SEVERITY_WARNING:
                    matches = aCtx.Request.filter_severity() == kiapi::schematic::commands::ERC_SEV_WARNING;
                    break;
                case RPT_SEVERITY_EXCLUSION:
                    matches = aCtx.Request.filter_severity() == kiapi::schematic::commands::ERC_SEV_EXCLUDED;
                    break;
                default:
                    break;
                }
                if( !matches )
                    continue;
            }

            auto* violation = response.add_violations();
            violation->mutable_id()->set_value( marker->m_Uuid.AsStdString() );
            violation->set_error_code( std::to_string( rcItem->GetErrorCode() ) );
            violation->set_description( rcItem->GetErrorText( true ).ToStdString() );

            switch( marker->GetSeverity() )
            {
            case RPT_SEVERITY_ERROR:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_ERROR );
                break;
            case RPT_SEVERITY_WARNING:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_WARNING );
                break;
            case RPT_SEVERITY_EXCLUSION:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_EXCLUDED );
                break;
            default:
                violation->set_severity( kiapi::schematic::commands::ERC_SEV_UNKNOWN );
                break;
            }

            // Convert from schematic IU to nanometers
            kiapi::common::PackVector2Sch( *violation->mutable_position(), marker->GetPosition() );

            if( rcItem->GetMainItemID() != niluuid )
                violation->add_item_ids()->set_value( rcItem->GetMainItemID().AsStdString() );
            if( rcItem->GetAuxItemID() != niluuid )
                violation->add_item_ids()->set_value( rcItem->GetAuxItemID().AsStdString() );
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::ClearERCMarkersResponse>
API_HANDLER_SCH::handleClearERCMarkers(
        const HANDLER_CONTEXT<kiapi::schematic::commands::ClearERCMarkers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::ClearERCMarkersResponse response;

    std::set<KIID> idsToRemove;
    for( const auto& id : aCtx.Request.marker_ids() )
        idsToRemove.insert( KIID( id.value() ) );

    int markersCleared = 0;
    SCH_SCREENS screens( m_frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        std::vector<SCH_MARKER*> toRemove;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );

            // If no specific IDs given, clear all. Otherwise, only clear specified IDs.
            if( idsToRemove.empty() || idsToRemove.count( marker->m_Uuid ) )
                toRemove.push_back( marker );
        }

        for( SCH_MARKER* marker : toRemove )
        {
            screen->Remove( marker );
            markersCleared++;
        }
    }

    response.set_markers_cleared( markersCleared );

    m_frame->GetCanvas()->Refresh();

    return response;
}


HANDLER_RESULT<Empty>
API_HANDLER_SCH::handleExcludeERCViolation(
        const HANDLER_CONTEXT<kiapi::schematic::commands::ExcludeERCViolation>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    KIID markerId( aCtx.Request.marker_id().value() );
    SCH_SCREENS screens( m_frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );

            if( marker->m_Uuid == markerId )
            {
                marker->SetExcluded( true );
                m_frame->GetCanvas()->Refresh();
                return Empty();
            }
        }
    }

    // Marker not found
    ApiResponseStatus e;
    e.set_status( ApiStatusCode::AS_BAD_REQUEST );
    e.set_error_message( "ERC marker not found" );
    return tl::unexpected( e );
}


HANDLER_RESULT<kiapi::schematic::commands::GetNetsResponse>
API_HANDLER_SCH::handleGetNets(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetNets>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetNetsResponse response;

    // Get the connection graph
    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( !graph )
    {
        // No connectivity data available, return empty list
        return response;
    }

    // Get net map (net codes to subgraphs)
    const auto& netMap = graph->GetNetMap();

    std::map<wxString, kiapi::schematic::commands::NetInfo*> netInfoMap;

    for( const auto& [netCode, subgraphs] : netMap )
    {
        if( subgraphs.empty() )
            continue;

        wxString netName = subgraphs[0]->GetNetName();

        if( netName.IsEmpty() )
            continue;

        // Skip bus connections, we only want nets
        if( subgraphs[0]->GetDriverConnection() &&
            subgraphs[0]->GetDriverConnection()->IsBus() )
            continue;

        // Get or create NetInfo for this net
        kiapi::schematic::commands::NetInfo* netInfo;
        auto it = netInfoMap.find( netName );

        if( it == netInfoMap.end() )
        {
            netInfo = response.add_nets();
            netInfo->set_name( netName.ToStdString() );
            netInfo->set_net_code( netCode.Netcode );
            netInfoMap[netName] = netInfo;
        }
        else
        {
            netInfo = it->second;
        }

        // Add items from this subgraph
        for( SCH_ITEM* item : subgraphs[0]->GetItems() )
        {
            netInfo->add_item_ids()->set_value( item->m_Uuid.AsStdString() );
        }

        netInfo->set_connection_count( netInfo->item_ids_size() );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetBusesResponse>
API_HANDLER_SCH::handleGetBuses(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetBuses>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetBusesResponse response;

    // Get the connection graph
    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( !graph )
        return response;

    const auto& netMap = graph->GetNetMap();

    std::map<wxString, kiapi::schematic::commands::BusInfo*> busInfoMap;

    for( const auto& [netCode, subgraphs] : netMap )
    {
        if( subgraphs.empty() )
            continue;

        // Only process bus connections
        if( !subgraphs[0]->GetDriverConnection() ||
            !subgraphs[0]->GetDriverConnection()->IsBus() )
            continue;

        const SCH_CONNECTION* conn = subgraphs[0]->GetDriverConnection();
        wxString busName = conn->GetNetName();

        if( busName.IsEmpty() )
            continue;

        // Check if we already have this bus
        if( busInfoMap.count( busName ) )
            continue;

        kiapi::schematic::commands::BusInfo* busInfo = response.add_buses();
        busInfo->set_name( busName.ToStdString() );
        busInfoMap[busName] = busInfo;

        // Check if it's a vector bus (e.g., D[0..7]) vs bus group (e.g., {A, B, C})
        if( conn->Type() == CONNECTION_TYPE::BUS )
        {
            busInfo->set_is_vector( true );

            // Get vector info
            wxString prefix = conn->VectorPrefix();
            busInfo->set_vector_prefix( prefix.ToStdString() );
            busInfo->set_vector_start( conn->VectorStart() );
            busInfo->set_vector_end( conn->VectorEnd() );

            // Generate member names
            for( int i = conn->VectorStart(); i <= conn->VectorEnd(); i++ )
            {
                busInfo->add_members( fmt::format( "{}{}", prefix.ToStdString(), i ) );
            }
        }
        else if( conn->Type() == CONNECTION_TYPE::BUS_GROUP )
        {
            busInfo->set_is_vector( false );

            // Get members from the bus group
            for( const auto& member : conn->Members() )
            {
                busInfo->add_members( member->GetNetName().ToStdString() );
            }
        }

        // Add bus line IDs
        for( SCH_ITEM* item : subgraphs[0]->GetItems() )
        {
            if( item->Type() == SCH_LINE_T &&
                static_cast<SCH_LINE*>( item )->GetLayer() == LAYER_BUS )
            {
                busInfo->add_bus_line_ids()->set_value( item->m_Uuid.AsStdString() );
            }
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetNetForItemResponse>
API_HANDLER_SCH::handleGetNetForItem(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetNetForItem>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetNetForItemResponse response;

    KIID itemId( aCtx.Request.item_id().value() );

    // Find the item
    std::optional<SCH_ITEM*> itemOpt = getItemById( itemId );

    if( !itemOpt || !*itemOpt )
    {
        response.set_is_connected( false );
        return response;
    }

    SCH_ITEM* item = *itemOpt;

    // Get connection info for the item
    SCH_CONNECTION* conn = item->Connection();

    if( !conn )
    {
        response.set_is_connected( false );
        return response;
    }

    response.set_is_connected( true );

    auto* connInfo = response.mutable_connection();

    // Set connection type
    switch( conn->Type() )
    {
    case CONNECTION_TYPE::NET:
        connInfo->set_type( kiapi::schematic::commands::CT_NET );
        break;
    case CONNECTION_TYPE::BUS:
        connInfo->set_type( kiapi::schematic::commands::CT_BUS );
        connInfo->set_vector_prefix( conn->VectorPrefix().ToStdString() );
        connInfo->set_vector_start( conn->VectorStart() );
        connInfo->set_vector_end( conn->VectorEnd() );
        break;
    case CONNECTION_TYPE::BUS_GROUP:
        connInfo->set_type( kiapi::schematic::commands::CT_BUS_GROUP );
        for( const auto& member : conn->Members() )
            connInfo->add_bus_members( member->GetNetName().ToStdString() );
        break;
    default:
        connInfo->set_type( kiapi::schematic::commands::CT_NONE );
        response.set_is_connected( false );
        break;
    }

    connInfo->set_name( conn->GetNetName().ToStdString() );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetBusMembersResponse>
API_HANDLER_SCH::handleGetBusMembers(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetBusMembers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetBusMembersResponse response;

    wxString busName = wxString::FromUTF8( aCtx.Request.bus_name() );
    wxString unescaped = UnescapeString( busName );

    wxString prefix;
    std::vector<wxString> members;

    // Try parsing as a vector bus (e.g., "D[0..7]")
    if( NET_SETTINGS::ParseBusVector( unescaped, &prefix, &members ) )
    {
        response.set_is_vector( true );
        response.set_vector_prefix( prefix.ToStdString() );

        // Extract start/end from member names
        if( !members.empty() )
        {
            // Members are already expanded like "D0", "D1", etc.
            // Try to extract the range
            long start = 0, end = 0;
            if( members.size() >= 1 )
            {
                wxString firstMember = members.front();
                wxString lastMember = members.back();

                // Extract number from end of member name
                size_t firstNumPos = firstMember.find_first_of( "0123456789" );
                size_t lastNumPos = lastMember.find_first_of( "0123456789" );

                if( firstNumPos != wxString::npos )
                    firstMember.Mid( firstNumPos ).ToLong( &start );
                if( lastNumPos != wxString::npos )
                    lastMember.Mid( lastNumPos ).ToLong( &end );
            }

            response.set_vector_start( static_cast<int>( start ) );
            response.set_vector_end( static_cast<int>( end ) );
        }

        for( const wxString& member : members )
            response.add_members( member.ToStdString() );
    }
    // Try parsing as a bus group (e.g., "{SDA, SCL}")
    else if( NET_SETTINGS::ParseBusGroup( unescaped, &prefix, &members ) )
    {
        response.set_is_vector( false );

        if( !prefix.IsEmpty() )
            response.set_vector_prefix( prefix.ToStdString() );

        for( const wxString& member : members )
            response.add_members( member.ToStdString() );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::GetNetItemsResponse>
API_HANDLER_SCH::handleGetNetItems(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetNetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetNetItemsResponse response;

    wxString netName = wxString::FromUTF8( aCtx.Request.net_name() );

    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( !graph )
        return response;

    const auto& netMap = graph->GetNetMap();

    for( const auto& [netCode, subgraphs] : netMap )
    {
        for( const CONNECTION_SUBGRAPH* subgraph : subgraphs )
        {
            if( subgraph->GetNetName() == netName )
            {
                for( SCH_ITEM* item : subgraph->GetItems() )
                {
                    response.add_item_ids()->set_value( item->m_Uuid.AsStdString() );

                    // Add connection points - convert from schematic IU to nanometers
                    for( const VECTOR2I& pt : item->GetConnectionPoints() )
                    {
                        auto* point = response.add_connection_points();
                        kiapi::common::PackVector2Sch( *point, pt );
                    }
                }
            }
        }
    }

    return response;
}


HANDLER_RESULT<types::TitleBlockInfo> API_HANDLER_SCH::handleGetTitleBlockInfo(
        const HANDLER_CONTEXT<GetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SCH_SCREEN* screen = m_frame->GetScreenForApi();
    const TITLE_BLOCK& block = screen->GetTitleBlock();

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


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetTitleBlockInfo(
        const HANDLER_CONTEXT<SetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCH_SCREEN* screen = m_frame->GetScreenForApi();
    TITLE_BLOCK block = screen->GetTitleBlock();

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

    screen->SetTitleBlock( block );

    // Mark the schematic as modified
    m_frame->OnModify();

    return Empty();
}


// Helper to convert PAGE_SIZE_TYPE to proto enum
static types::PageSizeType toProtoPageSize( PAGE_SIZE_TYPE aType )
{
    switch( aType )
    {
    case PAGE_SIZE_TYPE::A5:       return types::PST_A5;
    case PAGE_SIZE_TYPE::A4:       return types::PST_A4;
    case PAGE_SIZE_TYPE::A3:       return types::PST_A3;
    case PAGE_SIZE_TYPE::A2:       return types::PST_A2;
    case PAGE_SIZE_TYPE::A1:       return types::PST_A1;
    case PAGE_SIZE_TYPE::A0:       return types::PST_A0;
    case PAGE_SIZE_TYPE::A:        return types::PST_A;
    case PAGE_SIZE_TYPE::B:        return types::PST_B;
    case PAGE_SIZE_TYPE::C:        return types::PST_C;
    case PAGE_SIZE_TYPE::D:        return types::PST_D;
    case PAGE_SIZE_TYPE::E:        return types::PST_E;
    case PAGE_SIZE_TYPE::GERBER:   return types::PST_GERBER;
    case PAGE_SIZE_TYPE::USLetter: return types::PST_USLETTER;
    case PAGE_SIZE_TYPE::USLegal:  return types::PST_USLEGAL;
    case PAGE_SIZE_TYPE::USLedger: return types::PST_USLEDGER;
    case PAGE_SIZE_TYPE::User:     return types::PST_USER;
    default:                       return types::PST_UNKNOWN;
    }
}


// Helper to convert proto enum to PAGE_SIZE_TYPE
static PAGE_SIZE_TYPE fromProtoPageSize( types::PageSizeType aType )
{
    switch( aType )
    {
    case types::PST_A5:       return PAGE_SIZE_TYPE::A5;
    case types::PST_A4:       return PAGE_SIZE_TYPE::A4;
    case types::PST_A3:       return PAGE_SIZE_TYPE::A3;
    case types::PST_A2:       return PAGE_SIZE_TYPE::A2;
    case types::PST_A1:       return PAGE_SIZE_TYPE::A1;
    case types::PST_A0:       return PAGE_SIZE_TYPE::A0;
    case types::PST_A:        return PAGE_SIZE_TYPE::A;
    case types::PST_B:        return PAGE_SIZE_TYPE::B;
    case types::PST_C:        return PAGE_SIZE_TYPE::C;
    case types::PST_D:        return PAGE_SIZE_TYPE::D;
    case types::PST_E:        return PAGE_SIZE_TYPE::E;
    case types::PST_GERBER:   return PAGE_SIZE_TYPE::GERBER;
    case types::PST_USLETTER: return PAGE_SIZE_TYPE::USLetter;
    case types::PST_USLEGAL:  return PAGE_SIZE_TYPE::USLegal;
    case types::PST_USLEDGER: return PAGE_SIZE_TYPE::USLedger;
    case types::PST_USER:     return PAGE_SIZE_TYPE::User;
    default:                  return PAGE_SIZE_TYPE::A3;  // Default
    }
}


HANDLER_RESULT<types::PageInfo> API_HANDLER_SCH::handleGetPageSettings(
        const HANDLER_CONTEXT<GetPageSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SCH_SCREEN* screen = m_frame->GetScreenForApi();
    const PAGE_INFO& pageInfo = screen->GetPageSettings();

    types::PageInfo response;

    response.set_size_type( toProtoPageSize( pageInfo.GetType() ) );
    response.set_portrait( pageInfo.IsPortrait() );
    response.set_width_mm( pageInfo.GetWidthMM() );
    response.set_height_mm( pageInfo.GetHeightMM() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetPageSettings(
        const HANDLER_CONTEXT<SetPageSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCH_SCREEN* screen = m_frame->GetScreenForApi();
    const types::PageInfo& info = aCtx.Request.page_info();

    PAGE_SIZE_TYPE sizeType = fromProtoPageSize( info.size_type() );

    // For custom size, set the dimensions first
    if( sizeType == PAGE_SIZE_TYPE::User )
    {
        PAGE_INFO::SetCustomWidthMils( info.width_mm() * 1000.0 / 25.4 );
        PAGE_INFO::SetCustomHeightMils( info.height_mm() * 1000.0 / 25.4 );
    }

    PAGE_INFO pageInfo( sizeType, info.portrait() );

    screen->SetPageSettings( pageInfo );

    // Mark the schematic as modified
    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Grid Settings Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetGridSettingsResponse>
API_HANDLER_SCH::handleGetGridSettings(
        const HANDLER_CONTEXT<schematic::commands::GetGridSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetGridSettingsResponse response;

    // Get grid settings from the GAL
    KIGFX::GAL* gal = m_frame->GetCanvas()->GetGAL();
    VECTOR2D gridSize = gal->GetGridSize();
    VECTOR2I gridOrigin = m_frame->GetGridOrigin();

    kiapi::common::PackVector2Sch( *response.mutable_settings()->mutable_grid_size(),
                                   VECTOR2I( KiROUND( gridSize.x ), KiROUND( gridSize.y ) ) );
    kiapi::common::PackVector2Sch( *response.mutable_settings()->mutable_grid_origin(), gridOrigin );
    response.mutable_settings()->set_grid_visible( m_frame->IsGridVisible() );
    response.mutable_settings()->set_snap_to_grid( gal->GetGridSnapping() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetGridSettings(
        const HANDLER_CONTEXT<schematic::commands::SetGridSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( aCtx.Request.has_grid_visible() )
        m_frame->SetGridVisibility( aCtx.Request.grid_visible() );

    // Grid size changes - update via GAL
    KIGFX::GAL* gal = m_frame->GetCanvas()->GetGAL();

    if( aCtx.Request.has_grid_size_x_nm() || aCtx.Request.has_grid_size_y_nm() )
    {
        // API values are in nanometers; GAL grid size is in schematic IU (1 IU = 100nm)
        constexpr double NM_TO_SCH_IU = 1.0 / 100.0;
        VECTOR2D currentGrid = gal->GetGridSize();
        double newX = aCtx.Request.has_grid_size_x_nm()
                          ? aCtx.Request.grid_size_x_nm() * NM_TO_SCH_IU : currentGrid.x;
        double newY = aCtx.Request.has_grid_size_y_nm()
                          ? aCtx.Request.grid_size_y_nm() * NM_TO_SCH_IU : currentGrid.y;
        gal->SetGridSize( VECTOR2D( newX, newY ) );
    }

    m_frame->GetCanvas()->Refresh();

    return Empty();
}


// ============================================================================
// ERC Settings Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetERCSettingsResponse>
API_HANDLER_SCH::handleGetERCSettings(
        const HANDLER_CONTEXT<schematic::commands::GetERCSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetERCSettingsResponse response;

    SCHEMATIC* schematic = &m_frame->Schematic();
    const ERC_SETTINGS& ercSettings = schematic->ErcSettings();

    // Populate rule severities
    for( int i = ERCE_FIRST; i <= ERCE_LAST; i++ )
    {
        schematic::commands::ERCRuleSeverity* rule = response.mutable_settings()->add_rule_severities();

        // Convert error code to string
        std::shared_ptr<ERC_ITEM> ercItem = ERC_ITEM::Create( i );
        if( ercItem )
        {
            rule->set_error_code( ercItem->GetSettingsKey().ToStdString() );

            SEVERITY sev = ercSettings.GetSeverity( i );
            switch( sev )
            {
            case RPT_SEVERITY_ERROR:
                rule->set_severity( schematic::commands::ERC_SEV_ERROR );
                break;
            case RPT_SEVERITY_WARNING:
                rule->set_severity( schematic::commands::ERC_SEV_WARNING );
                break;
            case RPT_SEVERITY_IGNORE:
                rule->set_severity( schematic::commands::ERC_SEV_EXCLUDED );
                break;
            default:
                rule->set_severity( schematic::commands::ERC_SEV_UNKNOWN );
                break;
            }
        }
    }

    // Populate ERC check flags
    response.mutable_settings()->set_check_bus_driver_conflicts( ercSettings.IsTestEnabled( ERCE_DRIVER_CONFLICT ) );
    response.mutable_settings()->set_check_bus_to_net_conflicts( ercSettings.IsTestEnabled( ERCE_BUS_TO_NET_CONFLICT ) );
    response.mutable_settings()->set_check_bus_entry_conflicts( ercSettings.IsTestEnabled( ERCE_BUS_ENTRY_NEEDED ) );
    response.mutable_settings()->set_check_similar_labels( ercSettings.IsTestEnabled( ERCE_SIMILAR_LABELS ) );
    response.mutable_settings()->set_check_unique_global_labels( ercSettings.IsTestEnabled( ERCE_SINGLE_GLOBAL_LABEL ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetERCSettings(
        const HANDLER_CONTEXT<schematic::commands::SetERCSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCHEMATIC* schematic = &m_frame->Schematic();
    ERC_SETTINGS& ercSettings = schematic->ErcSettings();

    const schematic::commands::ERCSettingsData& settings = aCtx.Request.settings();

    // Update rule severities
    for( const schematic::commands::ERCRuleSeverity& rule : settings.rule_severities() )
    {
        // Find the ERC error code for this settings key
        for( int i = ERCE_FIRST; i <= ERCE_LAST; i++ )
        {
            std::shared_ptr<ERC_ITEM> ercItem = ERC_ITEM::Create( i );
            if( ercItem && ercItem->GetSettingsKey().ToStdString() == rule.error_code() )
            {
                SEVERITY sev = RPT_SEVERITY_WARNING;
                switch( rule.severity() )
                {
                case schematic::commands::ERC_SEV_ERROR:
                    sev = RPT_SEVERITY_ERROR;
                    break;
                case schematic::commands::ERC_SEV_WARNING:
                    sev = RPT_SEVERITY_WARNING;
                    break;
                case schematic::commands::ERC_SEV_EXCLUDED:
                    sev = RPT_SEVERITY_IGNORE;
                    break;
                default:
                    break;
                }
                ercSettings.SetSeverity( i, sev );
                break;
            }
        }
    }

    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Net Class Handlers
// ============================================================================

HANDLER_RESULT<Empty> API_HANDLER_SCH::handleAssignNetToClass(
        const HANDLER_CONTEXT<schematic::commands::AssignNetToClass>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const std::string& netName = aCtx.Request.net_name();
    const std::string& netClassName = aCtx.Request.net_class_name();

    // Get net settings from the project
    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    // Check if net class exists
    bool classExists = false;
    for( const auto& [name, netClass] : netSettings->GetNetclasses() )
    {
        if( name == netClassName )
        {
            classExists = true;
            break;
        }
    }

    if( !classExists && netClassName != "Default" )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Net class '{}' does not exist", netClassName ) );
        return tl::unexpected( e );
    }

    // Assign the net to the class
    netSettings->SetNetclassPatternAssignment( netName, netClassName );

    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<schematic::commands::GetNetClassesResponse>
API_HANDLER_SCH::handleGetNetClasses(
        const HANDLER_CONTEXT<schematic::commands::GetNetClasses>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetNetClassesResponse response;

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    // Helper lambda to populate netclass data
    auto populateNetClassData = []( schematic::commands::NetClassData* data,
                                    const std::shared_ptr<NETCLASS>& nc )
    {
        data->set_name( nc->GetName().ToStdString() );

        if( !nc->GetDescription().IsEmpty() )
            data->set_description( nc->GetDescription().ToStdString() );

        if( nc->HasWireWidth() )
            data->set_wire_width_mils( schIUScale.IUToMils( nc->GetWireWidth() ) );

        if( nc->HasBusWidth() )
            data->set_bus_width_mils( schIUScale.IUToMils( nc->GetBusWidth() ) );

        COLOR4D color = nc->GetSchematicColor();
        if( color != COLOR4D::UNSPECIFIED )
        {
            data->set_color( color.ToCSSString().ToStdString() );
        }
        else
        {
            data->set_color( "transparent" );
        }

        if( nc->HasLineStyle() )
        {
            switch( nc->GetLineStyle() )
            {
            case 0: data->set_line_style( schematic::commands::SLS_SOLID ); break;
            case 1: data->set_line_style( schematic::commands::SLS_DASH ); break;
            case 2: data->set_line_style( schematic::commands::SLS_DOT ); break;
            case 3: data->set_line_style( schematic::commands::SLS_DASH_DOT ); break;
            case 4: data->set_line_style( schematic::commands::SLS_DASH_DOT_DOT ); break;
            default: data->set_line_style( schematic::commands::SLS_SOLID ); break;
            }
        }

        data->set_priority( nc->GetPriority() );
    };

    // Get default netclass
    std::shared_ptr<NETCLASS> defaultNc = netSettings->GetDefaultNetclass();
    if( defaultNc )
    {
        populateNetClassData( response.mutable_default_netclass(), defaultNc );
    }

    // Get user-defined netclasses
    for( const auto& [name, nc] : netSettings->GetNetclasses() )
    {
        populateNetClassData( response.add_netclasses(), nc );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetNetClass(
        const HANDLER_CONTEXT<schematic::commands::SetNetClass>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    const auto& ncData = aCtx.Request.netclass();
    wxString name = wxString::FromUTF8( ncData.name() );

    if( name.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Netclass name is required" );
        return tl::unexpected( e );
    }

    // Check if this is the default netclass or a user-defined one
    std::shared_ptr<NETCLASS> nc;
    bool isDefault = ( name == NETCLASS::Default );

    if( isDefault )
    {
        nc = netSettings->GetDefaultNetclass();
    }
    else
    {
        // Check if netclass exists
        if( netSettings->HasNetclass( name ) )
        {
            nc = netSettings->GetNetClassByName( name );
        }
        else
        {
            // Create new netclass
            nc = std::make_shared<NETCLASS>( name );
        }
    }

    // Update netclass properties
    if( ncData.has_description() )
        nc->SetDescription( wxString::FromUTF8( ncData.description() ) );

    if( ncData.has_wire_width_mils() )
        nc->SetWireWidth( schIUScale.MilsToIU( ncData.wire_width_mils() ) );

    if( ncData.has_bus_width_mils() )
        nc->SetBusWidth( schIUScale.MilsToIU( ncData.bus_width_mils() ) );

    if( ncData.has_color() )
    {
        std::string colorStr = ncData.color();
        if( colorStr == "transparent" || colorStr.empty() )
        {
            nc->SetSchematicColor( COLOR4D::UNSPECIFIED );
        }
        else
        {
            COLOR4D color;
            color.SetFromHexString( wxString::FromUTF8( colorStr ) );
            nc->SetSchematicColor( color );
        }
    }

    if( ncData.has_line_style() )
    {
        int style = 0;
        switch( ncData.line_style() )
        {
        case schematic::commands::SLS_SOLID: style = 0; break;
        case schematic::commands::SLS_DASH: style = 1; break;
        case schematic::commands::SLS_DOT: style = 2; break;
        case schematic::commands::SLS_DASH_DOT: style = 3; break;
        case schematic::commands::SLS_DASH_DOT_DOT: style = 4; break;
        default: style = 0; break;
        }
        nc->SetLineStyle( style );
    }

    if( ncData.has_priority() )
        nc->SetPriority( ncData.priority() );

    // Save the netclass
    if( !isDefault )
    {
        netSettings->SetNetclass( name, nc );
    }

    netSettings->RecomputeEffectiveNetclasses();
    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleDeleteNetClass(
        const HANDLER_CONTEXT<schematic::commands::DeleteNetClass>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    wxString name = wxString::FromUTF8( aCtx.Request.name() );

    if( name == NETCLASS::Default )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Cannot delete the default netclass" );
        return tl::unexpected( e );
    }

    if( !netSettings->HasNetclass( name ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Netclass '{}' does not exist", name.ToStdString() ) );
        return tl::unexpected( e );
    }

    // Remove the netclass by getting all netclasses, removing this one, and setting them back
    std::map<wxString, std::shared_ptr<NETCLASS>> netclasses = netSettings->GetNetclasses();
    netclasses.erase( name );
    netSettings->SetNetclasses( netclasses );

    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<schematic::commands::GetNetClassAssignmentsResponse>
API_HANDLER_SCH::handleGetNetClassAssignments(
        const HANDLER_CONTEXT<schematic::commands::GetNetClassAssignments>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetNetClassAssignmentsResponse response;

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    // Get all pattern assignments
    auto& assignments = netSettings->GetNetclassPatternAssignments();
    for( const auto& [matcher, netclassName] : assignments )
    {
        auto* assignment = response.add_assignments();
        assignment->set_pattern( matcher->GetPattern().ToStdString() );
        assignment->set_netclass( netclassName.ToStdString() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetNetClassAssignments(
        const HANDLER_CONTEXT<schematic::commands::SetNetClassAssignments>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    // Clear existing assignments
    netSettings->ClearNetclassPatternAssignments();

    // Add new assignments
    for( const auto& assignment : aCtx.Request.assignments() )
    {
        wxString pattern = wxString::FromUTF8( assignment.pattern() );
        wxString netclass = wxString::FromUTF8( assignment.netclass() );
        netSettings->SetNetclassPatternAssignment( pattern, netclass );
    }

    netSettings->ClearAllCaches();
    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleAddNetClassAssignment(
        const HANDLER_CONTEXT<schematic::commands::AddNetClassAssignment>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    const auto& assignment = aCtx.Request.assignment();
    wxString pattern = wxString::FromUTF8( assignment.pattern() );
    wxString netclass = wxString::FromUTF8( assignment.netclass() );

    netSettings->SetNetclassPatternAssignment( pattern, netclass );
    netSettings->ClearAllCaches();
    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleRemoveNetClassAssignment(
        const HANDLER_CONTEXT<schematic::commands::RemoveNetClassAssignment>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

    if( !netSettings )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Net settings not available" );
        return tl::unexpected( e );
    }

    wxString patternToRemove = wxString::FromUTF8( aCtx.Request.pattern() );

    // Get current assignments, filter out the one to remove, and set them back
    auto& currentAssignments = netSettings->GetNetclassPatternAssignments();
    std::vector<std::pair<std::unique_ptr<EDA_COMBINED_MATCHER>, wxString>> newAssignments;

    for( auto& [matcher, netclassName] : currentAssignments )
    {
        if( matcher->GetPattern() != patternToRemove )
        {
            newAssignments.emplace_back(
                std::make_unique<EDA_COMBINED_MATCHER>( matcher->GetPattern(), CTX_NETCLASS ),
                netclassName );
        }
    }

    netSettings->SetNetclassPatternAssignments( std::move( newAssignments ) );
    netSettings->ClearAllCaches();
    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Bus Alias Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetBusAliasesResponse>
API_HANDLER_SCH::handleGetBusAliases(
        const HANDLER_CONTEXT<schematic::commands::GetBusAliases>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetBusAliasesResponse response;

    SCHEMATIC& schematic = m_frame->Schematic();
    const auto& aliases = schematic.GetAllBusAliases();

    for( const auto& alias : aliases )
    {
        if( !alias )
            continue;

        auto* aliasData = response.add_aliases();
        aliasData->set_name( alias->GetName().ToUTF8().data() );

        for( const wxString& member : alias->Members() )
        {
            aliasData->add_members( member.ToUTF8().data() );
        }
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetBusAlias(
        const HANDLER_CONTEXT<schematic::commands::SetBusAlias>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const auto& aliasData = aCtx.Request.alias();

    if( aliasData.name().empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Bus alias name is required" );
        return tl::unexpected( e );
    }

    SCHEMATIC& schematic = m_frame->Schematic();
    wxString aliasName = wxString::FromUTF8( aliasData.name() );

    // Get current aliases
    auto currentAliases = schematic.GetAllBusAliases();

    // Check if alias exists and remove it (to allow update)
    currentAliases.erase(
        std::remove_if( currentAliases.begin(), currentAliases.end(),
            [&aliasName]( const std::shared_ptr<BUS_ALIAS>& a ) {
                return a && a->GetName() == aliasName;
            }),
        currentAliases.end() );

    // Create new alias
    auto newAlias = std::make_shared<BUS_ALIAS>();
    newAlias->SetName( aliasName );

    for( const auto& member : aliasData.members() )
    {
        newAlias->Members().push_back( wxString::FromUTF8( member ) );
    }

    currentAliases.push_back( newAlias );

    // Set all aliases back
    schematic.SetBusAliases( currentAliases );
    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleDeleteBusAlias(
        const HANDLER_CONTEXT<schematic::commands::DeleteBusAlias>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    wxString nameToDelete = wxString::FromUTF8( aCtx.Request.name() );

    if( nameToDelete.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Bus alias name is required" );
        return tl::unexpected( e );
    }

    SCHEMATIC& schematic = m_frame->Schematic();

    // Get current aliases and filter out the one to delete
    auto currentAliases = schematic.GetAllBusAliases();
    bool found = false;

    currentAliases.erase(
        std::remove_if( currentAliases.begin(), currentAliases.end(),
            [&nameToDelete, &found]( const std::shared_ptr<BUS_ALIAS>& a ) {
                if( a && a->GetName() == nameToDelete )
                {
                    found = true;
                    return true;
                }
                return false;
            }),
        currentAliases.end() );

    if( !found )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Bus alias not found: " + aCtx.Request.name() );
        return tl::unexpected( e );
    }

    schematic.SetBusAliases( currentAliases );
    m_frame->OnModify();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetBusAliases(
        const HANDLER_CONTEXT<schematic::commands::SetBusAliases>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCHEMATIC& schematic = m_frame->Schematic();

    std::vector<std::shared_ptr<BUS_ALIAS>> newAliases;

    for( const auto& aliasData : aCtx.Request.aliases() )
    {
        if( aliasData.name().empty() )
            continue;

        auto alias = std::make_shared<BUS_ALIAS>();
        alias->SetName( wxString::FromUTF8( aliasData.name() ) );

        for( const auto& member : aliasData.members() )
        {
            alias->Members().push_back( wxString::FromUTF8( member ) );
        }

        newAliases.push_back( alias );
    }

    schematic.SetBusAliases( newAliases );
    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Editor Preferences Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetEditorPreferencesResponse>
API_HANDLER_SCH::handleGetEditorPreferences(
        const HANDLER_CONTEXT<schematic::commands::GetEditorPreferences>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetEditorPreferencesResponse response;
    schematic::commands::EditorPreferences* prefs = response.mutable_preferences();

    EESCHEMA_SETTINGS* eeSettings = m_frame->eeconfig();

    // Display settings
    prefs->set_show_hidden_pins( eeSettings->m_Appearance.show_hidden_pins );
    prefs->set_show_hidden_fields( eeSettings->m_Appearance.show_hidden_fields );
    prefs->set_show_pin_numbers( eeSettings->m_Appearance.show_erc_warnings );  // Placeholder
    prefs->set_show_pin_names( eeSettings->m_Appearance.show_erc_warnings );    // Placeholder
    prefs->set_show_erc_errors( eeSettings->m_Appearance.show_erc_errors );
    prefs->set_show_erc_warnings( eeSettings->m_Appearance.show_erc_warnings );
    prefs->set_show_erc_exclusions( eeSettings->m_Appearance.show_erc_exclusions );

    // Behavior settings - these are in COMMON_SETTINGS
    COMMON_SETTINGS* commonSettings = Pgm().GetCommonSettings();
    if( commonSettings )
    {
        prefs->set_auto_pan( commonSettings->m_Input.auto_pan );
        prefs->set_center_on_zoom( commonSettings->m_Input.center_on_zoom );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetEditorPreferences(
        const HANDLER_CONTEXT<schematic::commands::SetEditorPreferences>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const schematic::commands::EditorPreferences& prefs = aCtx.Request.preferences();
    EESCHEMA_SETTINGS* eeSettings = m_frame->eeconfig();

    // Display settings
    if( prefs.has_show_hidden_pins() )
        eeSettings->m_Appearance.show_hidden_pins = prefs.show_hidden_pins();
    if( prefs.has_show_hidden_fields() )
        eeSettings->m_Appearance.show_hidden_fields = prefs.show_hidden_fields();
    if( prefs.has_show_erc_errors() )
        eeSettings->m_Appearance.show_erc_errors = prefs.show_erc_errors();
    if( prefs.has_show_erc_warnings() )
        eeSettings->m_Appearance.show_erc_warnings = prefs.show_erc_warnings();
    if( prefs.has_show_erc_exclusions() )
        eeSettings->m_Appearance.show_erc_exclusions = prefs.show_erc_exclusions();

    // Behavior settings - these are in COMMON_SETTINGS
    COMMON_SETTINGS* commonSettings = Pgm().GetCommonSettings();
    if( commonSettings )
    {
        if( prefs.has_auto_pan() )
            commonSettings->m_Input.auto_pan = prefs.auto_pan();
        if( prefs.has_center_on_zoom() )
            commonSettings->m_Input.center_on_zoom = prefs.center_on_zoom();
    }

    m_frame->GetCanvas()->Refresh();

    return Empty();
}


// ============================================================================
// Formatting Settings Handlers (Project-level settings from Schematic Setup)
// ============================================================================

HANDLER_RESULT<schematic::commands::GetFormattingSettingsResponse>
API_HANDLER_SCH::handleGetFormattingSettings(
        const HANDLER_CONTEXT<schematic::commands::GetFormattingSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetFormattingSettingsResponse response;
    schematic::commands::FormattingSettings* settings = response.mutable_settings();

    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Text section (stored in IU internally, convert to mils)
    settings->set_default_text_size_mils( schSettings.m_DefaultTextSize / schIUScale.MilsToIU( 1 ) );
    settings->set_label_offset_ratio( schSettings.m_LabelSizeRatio * 100.0 );  // Convert to percentage
    settings->set_global_label_margin_ratio( schSettings.m_TextOffsetRatio * 100.0 );

    // Symbols section
    settings->set_default_line_width_mils( schSettings.m_DefaultLineWidth / schIUScale.MilsToIU( 1 ) );
    settings->set_pin_symbol_size_mils( schSettings.m_PinSymbolSize / schIUScale.MilsToIU( 1 ) );

    // Connections section
    settings->set_junction_size_choice( schSettings.m_JunctionSizeChoice );
    settings->set_hop_over_size_choice( schSettings.m_HopOverSizeChoice );
    settings->set_connection_grid_mils( schSettings.m_ConnectionGridSize / schIUScale.MilsToIU( 1 ) );

    // Inter-sheet References section
    settings->set_intersheet_refs_show( schSettings.m_IntersheetRefsShow );
    settings->set_intersheet_refs_list_own_page( schSettings.m_IntersheetRefsListOwnPage );
    settings->set_intersheet_refs_format_short( schSettings.m_IntersheetRefsFormatShort );
    settings->set_intersheet_refs_prefix( schSettings.m_IntersheetRefsPrefix.ToStdString() );
    settings->set_intersheet_refs_suffix( schSettings.m_IntersheetRefsSuffix.ToStdString() );

    // Dashed Lines section
    settings->set_dashed_line_dash_ratio( schSettings.m_DashedLineDashRatio );
    settings->set_dashed_line_gap_ratio( schSettings.m_DashedLineGapRatio );

    // Operating-point Overlay section
    settings->set_opo_voltage_precision( schSettings.m_OPO_VPrecision );
    settings->set_opo_voltage_range( schSettings.m_OPO_VRange.ToStdString() );
    settings->set_opo_current_precision( schSettings.m_OPO_IPrecision );
    settings->set_opo_current_range( schSettings.m_OPO_IRange.ToStdString() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetFormattingSettings(
        const HANDLER_CONTEXT<schematic::commands::SetFormattingSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const schematic::commands::FormattingSettings& settings = aCtx.Request.settings();
    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Text section (convert from mils to IU)
    if( settings.has_default_text_size_mils() )
        schSettings.m_DefaultTextSize = schIUScale.MilsToIU( settings.default_text_size_mils() );
    if( settings.has_label_offset_ratio() )
        schSettings.m_LabelSizeRatio = settings.label_offset_ratio() / 100.0;
    if( settings.has_global_label_margin_ratio() )
        schSettings.m_TextOffsetRatio = settings.global_label_margin_ratio() / 100.0;

    // Symbols section
    if( settings.has_default_line_width_mils() )
        schSettings.m_DefaultLineWidth = schIUScale.MilsToIU( settings.default_line_width_mils() );
    if( settings.has_pin_symbol_size_mils() )
        schSettings.m_PinSymbolSize = schIUScale.MilsToIU( settings.pin_symbol_size_mils() );

    // Connections section
    if( settings.has_junction_size_choice() )
        schSettings.m_JunctionSizeChoice = settings.junction_size_choice();
    if( settings.has_hop_over_size_choice() )
        schSettings.m_HopOverSizeChoice = settings.hop_over_size_choice();
    if( settings.has_connection_grid_mils() )
        schSettings.m_ConnectionGridSize = schIUScale.MilsToIU( settings.connection_grid_mils() );

    // Inter-sheet References section
    if( settings.has_intersheet_refs_show() )
        schSettings.m_IntersheetRefsShow = settings.intersheet_refs_show();
    if( settings.has_intersheet_refs_list_own_page() )
        schSettings.m_IntersheetRefsListOwnPage = settings.intersheet_refs_list_own_page();
    if( settings.has_intersheet_refs_format_short() )
        schSettings.m_IntersheetRefsFormatShort = settings.intersheet_refs_format_short();
    if( !settings.intersheet_refs_prefix().empty() || settings.has_intersheet_refs_prefix() )
        schSettings.m_IntersheetRefsPrefix = wxString::FromUTF8( settings.intersheet_refs_prefix() );
    if( !settings.intersheet_refs_suffix().empty() || settings.has_intersheet_refs_suffix() )
        schSettings.m_IntersheetRefsSuffix = wxString::FromUTF8( settings.intersheet_refs_suffix() );

    // Dashed Lines section
    if( settings.has_dashed_line_dash_ratio() )
        schSettings.m_DashedLineDashRatio = settings.dashed_line_dash_ratio();
    if( settings.has_dashed_line_gap_ratio() )
        schSettings.m_DashedLineGapRatio = settings.dashed_line_gap_ratio();

    // Operating-point Overlay section
    if( settings.has_opo_voltage_precision() )
        schSettings.m_OPO_VPrecision = settings.opo_voltage_precision();
    if( !settings.opo_voltage_range().empty() )
        schSettings.m_OPO_VRange = wxString::FromUTF8( settings.opo_voltage_range() );
    if( settings.has_opo_current_precision() )
        schSettings.m_OPO_IPrecision = settings.opo_current_precision();
    if( !settings.opo_current_range().empty() )
        schSettings.m_OPO_IRange = wxString::FromUTF8( settings.opo_current_range() );

    // Mark the schematic as modified and refresh
    m_frame->OnModify();
    m_frame->GetCanvas()->Refresh();

    return Empty();
}


// ============================================================================
// Field Name Templates Handlers (Project-level settings from Schematic Setup)
// ============================================================================

HANDLER_RESULT<schematic::commands::GetFieldNameTemplatesResponse>
API_HANDLER_SCH::handleGetFieldNameTemplates(
        const HANDLER_CONTEXT<schematic::commands::GetFieldNameTemplates>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetFieldNameTemplatesResponse response;

    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Get project-level template field names (aGlobal=false)
    const std::vector<TEMPLATE_FIELDNAME>& templates =
            schSettings.m_TemplateFieldNames.GetTemplateFieldNames( false );

    for( const TEMPLATE_FIELDNAME& field : templates )
    {
        schematic::commands::FieldNameTemplate* tmpl = response.add_templates();
        tmpl->set_name( field.m_Name.ToStdString() );
        tmpl->set_visible( field.m_Visible );
        tmpl->set_url( field.m_URL );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetFieldNameTemplates(
        const HANDLER_CONTEXT<schematic::commands::SetFieldNameTemplates>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Clear existing project-level templates
    schSettings.m_TemplateFieldNames.DeleteAllFieldNameTemplates( false );

    // Add new templates from the request
    for( const schematic::commands::FieldNameTemplate& tmpl : aCtx.Request.templates() )
    {
        TEMPLATE_FIELDNAME field( wxString::FromUTF8( tmpl.name() ) );
        field.m_Visible = tmpl.visible();
        field.m_URL = tmpl.url();
        schSettings.m_TemplateFieldNames.AddTemplateFieldName( field, false );
    }

    // Mark the schematic as modified
    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Annotation Settings Handlers (Project-level settings from Schematic Setup)
// ============================================================================

HANDLER_RESULT<schematic::commands::GetAnnotationSettingsResponse>
API_HANDLER_SCH::handleGetAnnotationSettings(
        const HANDLER_CONTEXT<schematic::commands::GetAnnotationSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetAnnotationSettingsResponse response;
    schematic::commands::AnnotationSettingsData* settings = response.mutable_settings();

    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Units section - convert from SubpartIdSeparator/SubpartFirstId to SymbolUnitNotation enum
    // m_SubpartFirstId: 'A' for letters, '1' for numbers
    // m_SubpartIdSeparator: 0 (none), '.' (dot), '-' (dash), '_' (underscore)
    schematic::commands::SymbolUnitNotation notation = schematic::commands::SUN_A;
    if( schSettings.m_SubpartFirstId == 'A' || schSettings.m_SubpartFirstId == 'a' )
    {
        switch( schSettings.m_SubpartIdSeparator )
        {
        case 0:   notation = schematic::commands::SUN_A;       break;
        case '.': notation = schematic::commands::SUN_DOT_A;   break;
        case '-': notation = schematic::commands::SUN_DASH_A;  break;
        case '_': notation = schematic::commands::SUN_UNDER_A; break;
        default:  notation = schematic::commands::SUN_A;       break;
        }
    }
    else // '1' for numbers
    {
        switch( schSettings.m_SubpartIdSeparator )
        {
        case '.': notation = schematic::commands::SUN_DOT_1;   break;
        case '-': notation = schematic::commands::SUN_DASH_1;  break;
        case '_': notation = schematic::commands::SUN_UNDER_1; break;
        default:  notation = schematic::commands::SUN_DOT_1;   break;
        }
    }
    settings->set_symbol_unit_notation( notation );

    // Order section - convert from ANNOTATE_ORDER_T
    settings->set_sort_order(
        schSettings.m_AnnotateSortOrder == ANNOTATE_ORDER_T::SORT_BY_Y_POSITION
            ? schematic::commands::ASO_Y_POSITION
            : schematic::commands::ASO_X_POSITION );

    // Numbering section - convert from ANNOTATE_ALGO_T
    switch( schSettings.m_AnnotateMethod )
    {
    case ANNOTATE_ALGO_T::SHEET_NUMBER_X_100:
        settings->set_numbering_method( schematic::commands::ANM_SHEET_X_100 );
        break;
    case ANNOTATE_ALGO_T::SHEET_NUMBER_X_1000:
        settings->set_numbering_method( schematic::commands::ANM_SHEET_X_1000 );
        break;
    default:
        settings->set_numbering_method( schematic::commands::ANM_FIRST_FREE );
        break;
    }

    settings->set_start_number( schSettings.m_AnnotateStartNum );

    // Allow reference reuse from the refdes tracker
    if( schSettings.m_refDesTracker )
        settings->set_allow_reference_reuse( schSettings.m_refDesTracker->GetReuseRefDes() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetAnnotationSettings(
        const HANDLER_CONTEXT<schematic::commands::SetAnnotationSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const schematic::commands::AnnotationSettingsData& settings = aCtx.Request.settings();
    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Units section - convert from SymbolUnitNotation enum to SubpartIdSeparator/SubpartFirstId
    if( settings.has_symbol_unit_notation() )
    {
        switch( settings.symbol_unit_notation() )
        {
        case schematic::commands::SUN_A:
            schSettings.m_SubpartFirstId = 'A';
            schSettings.m_SubpartIdSeparator = 0;
            break;
        case schematic::commands::SUN_DOT_A:
            schSettings.m_SubpartFirstId = 'A';
            schSettings.m_SubpartIdSeparator = '.';
            break;
        case schematic::commands::SUN_DASH_A:
            schSettings.m_SubpartFirstId = 'A';
            schSettings.m_SubpartIdSeparator = '-';
            break;
        case schematic::commands::SUN_UNDER_A:
            schSettings.m_SubpartFirstId = 'A';
            schSettings.m_SubpartIdSeparator = '_';
            break;
        case schematic::commands::SUN_DOT_1:
            schSettings.m_SubpartFirstId = '1';
            schSettings.m_SubpartIdSeparator = '.';
            break;
        case schematic::commands::SUN_DASH_1:
            schSettings.m_SubpartFirstId = '1';
            schSettings.m_SubpartIdSeparator = '-';
            break;
        case schematic::commands::SUN_UNDER_1:
            schSettings.m_SubpartFirstId = '1';
            schSettings.m_SubpartIdSeparator = '_';
            break;
        default:
            break;
        }
    }

    // Order section
    if( settings.has_sort_order() )
    {
        schSettings.m_AnnotateSortOrder =
            settings.sort_order() == schematic::commands::ASO_Y_POSITION
                ? ANNOTATE_ORDER_T::SORT_BY_Y_POSITION
                : ANNOTATE_ORDER_T::SORT_BY_X_POSITION;
    }

    // Numbering section
    if( settings.has_numbering_method() )
    {
        switch( settings.numbering_method() )
        {
        case schematic::commands::ANM_SHEET_X_100:
            schSettings.m_AnnotateMethod = ANNOTATE_ALGO_T::SHEET_NUMBER_X_100;
            break;
        case schematic::commands::ANM_SHEET_X_1000:
            schSettings.m_AnnotateMethod = ANNOTATE_ALGO_T::SHEET_NUMBER_X_1000;
            break;
        default:
            schSettings.m_AnnotateMethod = ANNOTATE_ALGO_T::INCREMENTAL_BY_REF;
            break;
        }
    }

    if( settings.has_start_number() )
        schSettings.m_AnnotateStartNum = settings.start_number();

    if( settings.has_allow_reference_reuse() && schSettings.m_refDesTracker )
        schSettings.m_refDesTracker->SetReuseRefDes( settings.allow_reference_reuse() );

    // Mark the schematic as modified
    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Simulation Settings Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetSimulationSettingsResponse>
API_HANDLER_SCH::handleGetSimulationSettings(
        const HANDLER_CONTEXT<schematic::commands::GetSimulationSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetSimulationSettingsResponse response;
    schematic::commands::SimulationSettings* settings = response.mutable_settings();

    // Get SPICE settings from schematic settings
    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    settings->set_simulator( "ngspice" );

    // Get compatibility mode from NGSPICE_SETTINGS if available
    if( schSettings.m_NgspiceSettings )
    {
        auto mode = schSettings.m_NgspiceSettings->GetCompatibilityMode();
        switch( mode )
        {
        case NGSPICE_COMPATIBILITY_MODE::PSPICE:
            settings->set_spice_command( "pspice" );
            break;
        case NGSPICE_COMPATIBILITY_MODE::LTSPICE:
            settings->set_spice_command( "ltspice" );
            break;
        case NGSPICE_COMPATIBILITY_MODE::HSPICE:
            settings->set_spice_command( "hspice" );
            break;
        default:
            settings->set_spice_command( "ngspice" );
            break;
        }
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetSimulationSettings(
        const HANDLER_CONTEXT<schematic::commands::SetSimulationSettings>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const schematic::commands::SimulationSettings& settings = aCtx.Request.settings();
    SCHEMATIC_SETTINGS& schSettings = m_frame->Schematic().Settings();

    // Set compatibility mode based on spice_command hint
    if( schSettings.m_NgspiceSettings && !settings.spice_command().empty() )
    {
        wxString mode = wxString::FromUTF8( settings.spice_command() ).Lower();
        if( mode == "pspice" )
            schSettings.m_NgspiceSettings->SetCompatibilityMode( NGSPICE_COMPATIBILITY_MODE::PSPICE );
        else if( mode == "ltspice" )
            schSettings.m_NgspiceSettings->SetCompatibilityMode( NGSPICE_COMPATIBILITY_MODE::LTSPICE );
        else if( mode == "hspice" )
            schSettings.m_NgspiceSettings->SetCompatibilityMode( NGSPICE_COMPATIBILITY_MODE::HSPICE );
        else
            schSettings.m_NgspiceSettings->SetCompatibilityMode( NGSPICE_COMPATIBILITY_MODE::NGSPICE );
    }

    m_frame->OnModify();

    return Empty();
}


// ============================================================================
// Library Query Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetLibrarySymbolsResponse>
API_HANDLER_SCH::handleGetLibrarySymbols(
        const HANDLER_CONTEXT<schematic::commands::GetLibrarySymbols>& aCtx )
{
    schematic::commands::GetLibrarySymbolsResponse response;

    wxString libName = wxString::FromUTF8( aCtx.Request.library_name() );

    // Get the symbol library adapter
    SYMBOL_LIBRARY_ADAPTER* libAdapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );

    if( !libAdapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Symbol library adapter not available" );
        return tl::unexpected( e );
    }

    if( !libAdapter->HasLibrary( libName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Library '{}' not found", libName.ToStdString() ) );
        return tl::unexpected( e );
    }

    try
    {
        std::vector<wxString> names = libAdapter->GetSymbolNames( libName );

        for( const wxString& name : names )
        {
            response.add_symbol_names( name.ToStdString() );
        }
    }
    catch( const IO_ERROR& e )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error reading library '{}': {}",
                                            libName.ToStdString(), e.What().ToStdString() ) );
        return tl::unexpected( err );
    }

    return response;
}


HANDLER_RESULT<schematic::commands::SearchLibrarySymbolsResponse>
API_HANDLER_SCH::handleSearchLibrarySymbols(
        const HANDLER_CONTEXT<schematic::commands::SearchLibrarySymbols>& aCtx )
{
    schematic::commands::SearchLibrarySymbolsResponse response;

    wxString query = wxString::FromUTF8( aCtx.Request.query() ).Lower();
    int maxResults = aCtx.Request.max_results() > 0 ? aCtx.Request.max_results() : 100;

    // Get the symbol library adapter
    SYMBOL_LIBRARY_ADAPTER* libAdapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );

    if( !libAdapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Symbol library adapter not available" );
        return tl::unexpected( e );
    }

    std::vector<wxString> libNames;

    if( aCtx.Request.libraries_size() > 0 )
    {
        for( const std::string& lib : aCtx.Request.libraries() )
            libNames.push_back( wxString::FromUTF8( lib ) );
    }
    else
    {
        libNames = libAdapter->GetLibraryNames();
    }

    // Match ranking: lower is better
    enum MatchRank
    {
        EXACT_MATCH = 0,     // name == query (highest priority)
        PREFIX_MATCH = 1,    // name starts with query
        SUBSTRING_MATCH = 2  // name contains query (lowest priority)
    };

    // Structure to hold match info for sorting
    struct MatchInfo
    {
        wxString    libName;
        wxString    symbolName;
        MatchRank   rank;
        LIB_SYMBOL* symbol;
    };

    std::vector<MatchInfo> matches;

    for( const wxString& libName : libNames )
    {
        if( !libAdapter->HasLibrary( libName ) )
            continue;

        try
        {
            std::vector<wxString> names = libAdapter->GetSymbolNames( libName );

            for( const wxString& name : names )
            {
                wxString nameLower = name.Lower();
                MatchRank rank;

                // Determine match rank
                if( nameLower == query )
                {
                    rank = EXACT_MATCH;
                }
                else if( nameLower.StartsWith( query ) )
                {
                    rank = PREFIX_MATCH;
                }
                else if( nameLower.Contains( query ) )
                {
                    rank = SUBSTRING_MATCH;
                }
                else
                {
                    continue; // No match
                }

                // Load symbol and add to matches
                LIB_SYMBOL* symbol = libAdapter->LoadSymbol( libName, name );
                if( symbol )
                {
                    matches.push_back( { libName, name, rank, symbol } );
                }
            }
        }
        catch( const IO_ERROR& )
        {
            // Skip libraries that can't be read
            continue;
        }
    }

    // Sort by rank (lower is better), then alphabetically by name for stability
    std::sort( matches.begin(), matches.end(),
        []( const MatchInfo& a, const MatchInfo& b )
        {
            if( a.rank != b.rank )
                return a.rank < b.rank;
            return a.symbolName.CmpNoCase( b.symbolName ) < 0;
        });

    // Build response with top maxResults
    int count = 0;
    for( const MatchInfo& match : matches )
    {
        if( count >= maxResults )
            break;

        schematic::commands::SymbolInfo* info = response.add_symbols();
        info->set_lib_id( fmt::format( "{}:{}", match.libName.ToStdString(),
                                                 match.symbolName.ToStdString() ) );
        info->set_name( match.symbolName.ToStdString() );
        info->set_description( match.symbol->GetDescription().ToStdString() );
        info->set_keywords( match.symbol->GetKeyWords().ToStdString() );
        info->set_unit_count( match.symbol->GetUnitCount() );
        info->set_is_power( match.symbol->IsPower() );

        // Add footprint filters
        for( const wxString& filter : match.symbol->GetFPFilters() )
            info->add_footprint_filters( filter.ToStdString() );

        // Add datasheet
        wxString ds = match.symbol->GetDatasheetProp();
        if( !ds.empty() )
            info->set_datasheet( ds.ToStdString() );

        // Populate pin information
        for( SCH_PIN* pin : match.symbol->GetPins() )
        {
            schematic::commands::PinInfo* pinInfo = info->add_pins();
            pinInfo->set_number( pin->GetNumber().ToStdString() );
            pinInfo->set_name( pin->GetName().ToStdString() );
            kiapi::common::PackVector2Sch( *pinInfo->mutable_position(), pin->GetPosition() );
            pinInfo->set_orientation( static_cast<int>( pin->GetOrientation() ) );
            pinInfo->set_electrical_type( magic_enum::enum_name( pin->GetType() ).data() );
            pinInfo->set_graphical_style( magic_enum::enum_name( pin->GetShape() ).data() );
            pinInfo->set_unit( pin->GetUnit() );
        }

        count++;
    }

    return response;
}


HANDLER_RESULT<schematic::commands::GetSymbolInfoResponse>
API_HANDLER_SCH::handleGetSymbolInfo(
        const HANDLER_CONTEXT<schematic::commands::GetSymbolInfo>& aCtx )
{
    schematic::commands::GetSymbolInfoResponse response;

    LIB_ID libId;
    // LIB_ID::Parse returns -1 on success, >= 0 (error offset) on failure
    if( libId.Parse( aCtx.Request.lib_id() ) >= 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Invalid lib_id: {}", aCtx.Request.lib_id() ) );
        return tl::unexpected( e );
    }

    SYMBOL_LIBRARY_ADAPTER* libAdapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );

    if( !libAdapter )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Symbol library adapter not available" );
        return tl::unexpected( e );
    }

    try
    {
        LIB_SYMBOL* symbol = libAdapter->LoadSymbol( libId );

        if( !symbol )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "Symbol not found: {}", aCtx.Request.lib_id() ) );
            return tl::unexpected( e );
        }

        schematic::commands::SymbolInfo* info = response.mutable_symbol();
        info->set_lib_id( aCtx.Request.lib_id() );
        info->set_name( symbol->GetName().ToStdString() );
        info->set_description( symbol->GetDescription().ToStdString() );
        info->set_keywords( symbol->GetKeyWords().ToStdString() );
        info->set_unit_count( symbol->GetUnitCount() );
        info->set_is_power( symbol->IsPower() );

        // Add footprint filters
        for( const wxString& filter : symbol->GetFPFilters() )
        {
            info->add_footprint_filters( filter.ToStdString() );
        }

        // Add datasheet
        wxString ds = symbol->GetDatasheetProp();
        if( !ds.empty() )
            info->set_datasheet( ds.ToStdString() );

        // Add pin information
        for( SCH_PIN* pin : symbol->GetPins() )
        {
            schematic::commands::PinInfo* pinInfo = info->add_pins();
            pinInfo->set_number( pin->GetNumber().ToStdString() );
            pinInfo->set_name( pin->GetName().ToStdString() );
            kiapi::common::PackVector2Sch( *pinInfo->mutable_position(), pin->GetPosition() );
            pinInfo->set_orientation( static_cast<int>( pin->GetOrientation() ) );
            pinInfo->set_electrical_type( magic_enum::enum_name( pin->GetType() ).data() );
            pinInfo->set_graphical_style( magic_enum::enum_name( pin->GetShape() ).data() );
            pinInfo->set_unit( pin->GetUnit() );
        }

        // Add bounding box (body + pins, all units)
        // BOX2I is in schematic IU; proto fields are in nanometers
        BOX2I bbox = symbol->GetBodyBoundingBox( 0, 0, true, false );

        if( bbox.GetWidth() > 0 || bbox.GetHeight() > 0 )
        {
            constexpr int64_t IU_TO_NM = 100;
            info->set_body_bbox_min_x_nm( static_cast<int64_t>( bbox.GetPosition().x ) * IU_TO_NM );
            info->set_body_bbox_min_y_nm( static_cast<int64_t>( bbox.GetPosition().y ) * IU_TO_NM );
            info->set_body_bbox_max_x_nm( static_cast<int64_t>( bbox.GetPosition().x + bbox.GetWidth() ) * IU_TO_NM );
            info->set_body_bbox_max_y_nm( static_cast<int64_t>( bbox.GetPosition().y + bbox.GetHeight() ) * IU_TO_NM );
        }
    }
    catch( const IO_ERROR& e )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error loading symbol: {}", e.What().ToStdString() ) );
        return tl::unexpected( err );
    }

    return response;
}


HANDLER_RESULT<schematic::commands::GetTransformedPinPositionResponse>
API_HANDLER_SCH::handleGetTransformedPinPosition(
        const HANDLER_CONTEXT<schematic::commands::GetTransformedPinPosition>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetTransformedPinPositionResponse response;

    KIID symbolId( aCtx.Request.symbol_id().value() );
    std::optional<SCH_ITEM*> itemOpt = getItemById( symbolId );

    if( !itemOpt || !*itemOpt )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Symbol not found" );
        return tl::unexpected( e );
    }

    SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( *itemOpt );

    if( !symbol )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Item is not a symbol" );
        return tl::unexpected( e );
    }

    wxString pinNumber = wxString::FromUTF8( aCtx.Request.pin_number() );
    SCH_PIN* pin = symbol->GetPin( pinNumber );

    if( !pin )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Pin '{}' not found on symbol", aCtx.Request.pin_number() ) );
        return tl::unexpected( e );
    }

    // Get the transformed position (applies symbol's position, rotation, mirror)
    VECTOR2I worldPos = pin->GetPosition();
    kiapi::common::PackVector2Sch( *response.mutable_position(), worldPos );
    response.set_orientation( static_cast<int>( pin->GetOrientation() ) );

    return response;
}


HANDLER_RESULT<schematic::commands::GetNeededJunctionsResponse>
API_HANDLER_SCH::handleGetNeededJunctions(
        const HANDLER_CONTEXT<schematic::commands::GetNeededJunctions>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetNeededJunctionsResponse response;

    SCH_SCREEN* screen = m_frame->GetScreenForApi();

    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No screen available" );
        return tl::unexpected( e );
    }

    // Get all existing connection points on the screen
    std::vector<VECTOR2I> connections = screen->GetConnections();

    // Collect candidate points from the specified items
    std::vector<VECTOR2I> pts;

    for( const auto& idProto : aCtx.Request.item_ids() )
    {
        KIID id( idProto.value() );
        std::optional<SCH_ITEM*> itemOpt = getItemById( id );

        if( !itemOpt || !*itemOpt )
            continue;

        SCH_ITEM* item = *itemOpt;

        // Connection points of the item itself
        std::vector<VECTOR2I> itemPts = item->GetConnectionPoints();
        pts.insert( pts.end(), itemPts.begin(), itemPts.end() );

        // For wire/bus lines, also check if existing connections lie on the segment interior
        if( item->Type() == SCH_LINE_T )
        {
            SCH_LINE* line = static_cast<SCH_LINE*>( item );

            for( const VECTOR2I& pt : connections )
            {
                if( IsPointOnSegment( line->GetStartPoint(), line->GetEndPoint(), pt ) )
                    pts.push_back( pt );
            }
        }
    }

    // Deduplicate
    std::sort( pts.begin(), pts.end(),
               []( const VECTOR2I& a, const VECTOR2I& b )
               {
                   return a.x < b.x || ( a.x == b.x && a.y < b.y );
               } );
    pts.erase( std::unique( pts.begin(), pts.end() ), pts.end() );

    // Analyze each candidate point using the screen's RTREE
    const EE_RTREE& rtree = screen->Items();

    for( const VECTOR2I& pt : pts )
    {
        JUNCTION_HELPERS::POINT_INFO info =
                JUNCTION_HELPERS::AnalyzePoint( rtree, pt, false );

        if( info.isJunction && !info.hasExplicitJunctionDot
            && ( !info.hasBusEntry || info.hasBusEntryToMultipleWires ) )
        {
            kiapi::common::PackVector2Sch( *response.add_positions(), pt );
        }
    }

    return response;
}


// ============================================================================
// Simulation Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::RunSimulationResponse>
API_HANDLER_SCH::handleRunSimulation(
        const HANDLER_CONTEXT<schematic::commands::RunSimulation>& aCtx )
{
    wxLogInfo( "SIM API: handleRunSimulation called" );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::RunSimulationResponse response;

    // Ensure schematic is annotated — auto-annotate if needed (no modal dialog)
    m_frame->Schematic().Hierarchy().AnnotatePowerSymbols();

    if( m_frame->CheckAnnotate(
            []( ERCE_T, const wxString&, SCH_REFERENCE*, SCH_REFERENCE* ) {} ) )
    {
        wxLogInfo( "SIM API: Schematic not annotated, auto-annotating" );

        NULL_REPORTER reporter;
        SCH_COMMIT    commit( m_frame );

        m_frame->AnnotateSymbols( &commit, ANNOTATE_ALL, SORT_BY_X_POSITION,
                                  INCREMENTAL_BY_REF, true, 1, false, false, reporter );
        commit.Push( _( "API: Auto-annotate for simulation" ) );

        // Verify annotation succeeded
        if( m_frame->CheckAnnotate(
                []( ERCE_T, const wxString&, SCH_REFERENCE*, SCH_REFERENCE* ) {} ) )
        {
            wxLogWarning( "SIM API: Auto-annotation failed" );
            response.set_success( false );
            response.set_error_message( "Schematic could not be auto-annotated. "
                                        "Please annotate manually before running simulation." );
            return response;
        }

        wxLogInfo( "SIM API: Auto-annotation succeeded" );
    }
    else
    {
        wxLogInfo( "SIM API: Annotation check passed" );
    }

    // Get or create the simulator frame (hidden — we only need the engine, not the GUI)
    SIMULATOR_FRAME* simFrame = nullptr;

    try
    {
        wxLogInfo( "SIM API: Getting simulator frame" );
        simFrame = static_cast<SIMULATOR_FRAME*>(
                m_frame->Kiway().Player( FRAME_SIMULATOR, true ) );

        wxLogInfo( "SIM API: Frame created, IsShown=%d", simFrame ? simFrame->IsShown() : -1 );

        if( simFrame && simFrame->IsShown() )
        {
            wxLogInfo( "SIM API: Hiding simulator frame" );
            simFrame->Hide();
        }
    }
    catch( const std::exception& e )
    {
        wxLogWarning( "SIM API: Failed to open simulator: %s", e.what() );
        response.set_success( false );
        response.set_error_message(
                fmt::format( "Failed to open simulator: {}", e.what() ) );
        return response;
    }

    if( !simFrame )
    {
        wxLogWarning( "SIM API: simFrame is null" );
        response.set_success( false );
        response.set_error_message( "Failed to open simulator frame" );
        return response;
    }

    auto simulator = simFrame->GetSimulator();
    auto circuitModel = simFrame->GetCircuitModel();

    if( !simulator )
    {
        wxLogWarning( "SIM API: Simulator engine is null" );
        response.set_success( false );
        response.set_error_message( "Simulator engine not available" );
        return response;
    }

    if( !circuitModel )
    {
        wxLogWarning( "SIM API: Circuit model is null" );
        response.set_success( false );
        response.set_error_message( "Circuit model not available" );
        return response;
    }

    // Determine the simulation command
    wxString simCommand;

    if( aCtx.Request.has_command_override() && !aCtx.Request.command_override().empty() )
    {
        simCommand = wxString::FromUTF8( aCtx.Request.command_override() );
    }
    else
    {
        simCommand = simFrame->GetCurrentSimCommand();
    }

    wxLogInfo( "SIM API: Command = '%s'", simCommand );

    if( simCommand.IsEmpty() )
    {
        response.set_success( false );
        response.set_error_message( "No simulation command specified and no default command set. "
                                    "Provide a command_override (e.g. '.tran 1u 10m')." );
        return response;
    }

    // Detect simulation type
    SIM_TYPE simType = SPICE_CIRCUIT_MODEL::CommandToSimType( simCommand );

    if( simType == ST_UNKNOWN )
    {
        response.set_success( false );
        response.set_error_message(
                fmt::format( "Unknown simulation command: '{}'",
                             simCommand.ToStdString() ) );
        return response;
    }

    wxLogInfo( "SIM API: SimType = %d, recalculating connections", (int) simType );

    // Recalculate connectivity (what LoadSimulator does after ReadyToNetlist)
    if( ADVANCED_CFG::GetCfg().m_IncrementalConnectivity )
        m_frame->RecalculateConnections( nullptr, GLOBAL_CLEANUP );

    // Attach the circuit model to the simulator (generates netlist, loads into ngspice)
    // This bypasses LoadSimulator's ReadyToNetlist() which shows modal dialogs
    WX_STRING_REPORTER reporter;

    wxLogInfo( "SIM API: Calling simulator->Attach()" );

    bool attached = simulator->Attach( circuitModel, simCommand, 0,
                                       m_frame->Prj().GetProjectPath(), reporter );

    if( !attached )
    {
        wxString reportText = reporter.GetMessages();
        wxLogWarning( "SIM API: Attach failed: %s", reportText );
        response.set_success( false );
        response.set_error_message(
                fmt::format( "Failed to load simulator netlist. {}",
                             reportText.ToStdString() ) );
        return response;
    }

    wxLogInfo( "SIM API: Attach succeeded, acquiring mutex" );

    // Acquire mutex - fail if another simulation is running
    std::unique_lock<std::mutex> lock( simulator->GetMutex(), std::try_to_lock );

    if( !lock.owns_lock() )
    {
        wxLogWarning( "SIM API: Could not acquire mutex - another sim running" );
        response.set_success( false );
        response.set_error_message( "Another simulation is currently running" );
        return response;
    }

    // Run the simulation
    wxLogInfo( "SIM API: Calling simulator->Run()" );

    if( !simulator->Run() )
    {
        wxLogWarning( "SIM API: Run() returned false" );
        response.set_success( false );
        response.set_error_message( "Failed to start simulation" );
        return response;
    }

    // Poll for completion with 30s timeout
    constexpr int POLL_MS = 50;
    constexpr int MAX_POLLS = 600;  // 30 seconds

    wxLogInfo( "SIM API: Polling for completion" );

    for( int i = 0; i < MAX_POLLS; ++i )
    {
        if( !simulator->IsRunning() )
        {
            wxLogInfo( "SIM API: Simulation finished after %d ms", i * POLL_MS );
            break;
        }

        wxMilliSleep( POLL_MS );

        if( i == MAX_POLLS - 1 )
        {
            wxLogWarning( "SIM API: Simulation timed out" );
            simulator->Stop();
            response.set_success( false );
            response.set_error_message( "Simulation timed out after 30 seconds" );
            return response;
        }
    }

    // Collect results
    auto vectors = simulator->AllVectors();

    wxLogInfo( "SIM API: Got %zu vectors", vectors.size() );

    if( vectors.empty() )
    {
        response.set_success( false );
        response.set_error_message( "Simulation completed but produced no output vectors" );
        return response;
    }

    wxString xAxisName = simulator->GetXAxis( simType );
    bool isComplex = ( simType == ST_AC || simType == ST_SP || simType == ST_NOISE );

    wxLogInfo( "SIM API: X-axis='%s', isComplex=%d", xAxisName, isComplex );

    // Get X-axis data
    std::vector<double> xData;

    if( !xAxisName.IsEmpty() )
    {
        xData = simulator->GetRealVector( xAxisName.ToStdString() );
        wxLogInfo( "SIM API: X-axis has %zu points", xData.size() );
    }

    for( const auto& vecName : vectors )
    {
        // Skip X-axis vector itself
        if( vecName == xAxisName.ToStdString() )
            continue;

        auto* trace = response.add_traces();
        trace->set_name( vecName );

        // Set X-axis values
        for( double val : xData )
            trace->add_time_values( val );

        // Get Y-axis data based on simulation type
        std::vector<double> yData;

        if( isComplex )
            yData = simulator->GetGainVector( vecName );
        else
            yData = simulator->GetRealVector( vecName );

        for( double val : yData )
            trace->add_data_values( val );
    }

    wxLogInfo( "SIM API: Returning %d traces", response.traces_size() );
    response.set_success( true );
    return response;
}


HANDLER_RESULT<schematic::commands::GetSimulationResultsResponse>
API_HANDLER_SCH::handleGetSimulationResults(
        const HANDLER_CONTEXT<schematic::commands::GetSimulationResults>& aCtx )
{
    wxLogInfo( "SIM API: handleGetSimulationResults called" );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetSimulationResultsResponse response;

    // Get existing simulator frame - don't create one
    SIMULATOR_FRAME* simFrame = static_cast<SIMULATOR_FRAME*>(
            m_frame->Kiway().Player( FRAME_SIMULATOR, false ) );

    if( !simFrame )
    {
        response.set_has_results( false );
        return response;
    }

    auto simulator = simFrame->GetSimulator();

    if( !simulator )
    {
        response.set_has_results( false );
        return response;
    }

    auto vectors = simulator->AllVectors();

    if( vectors.empty() )
    {
        response.set_has_results( false );
        return response;
    }

    response.set_has_results( true );

    SIM_TYPE simType = simFrame->GetCurrentSimType();
    wxString xAxisName = simulator->GetXAxis( simType );
    bool isComplex = ( simType == ST_AC || simType == ST_SP || simType == ST_NOISE );

    // Get X-axis data
    std::vector<double> xData;

    if( !xAxisName.IsEmpty() )
    {
        xData = simulator->GetRealVector( xAxisName.ToStdString() );
    }

    for( const auto& vecName : vectors )
    {
        if( vecName == xAxisName.ToStdString() )
            continue;

        auto* trace = response.add_traces();
        trace->set_name( vecName );

        for( double val : xData )
            trace->add_time_values( val );

        std::vector<double> yData;

        if( isComplex )
            yData = simulator->GetGainVector( vecName );
        else
            yData = simulator->GetRealVector( vecName );

        for( double val : yData )
            trace->add_data_values( val );
    }

    return response;
}


// ============================================================================
// Export Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::ExportNetlistResponse>
API_HANDLER_SCH::handleExportNetlist(
        const HANDLER_CONTEXT<schematic::commands::ExportNetlist>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::ExportNetlistResponse response;

    wxString outputPath = wxString::FromUTF8( aCtx.Request.output_path() );

    // Determine netlist format
    int formatType = 0;  // Default to KiCad format
    switch( aCtx.Request.format() )
    {
    case schematic::commands::NF_KICAD: formatType = 0; break;
    case schematic::commands::NF_SPICE: formatType = 1; break;
    case schematic::commands::NF_ORCAD: formatType = 2; break;
    case schematic::commands::NF_CADSTAR: formatType = 3; break;
    case schematic::commands::NF_PADS: formatType = 4; break;
    default: formatType = 0; break;
    }

    try
    {
        // Export netlist using the frame's export functionality
        bool success = m_frame->WriteNetListFile( formatType, outputPath, 0, nullptr );

        response.set_success( success );
        if( success )
            response.set_output_path( outputPath.ToStdString() );
        else
            response.set_error_message( "Failed to export netlist" );
    }
    catch( const std::exception& e )
    {
        response.set_success( false );
        response.set_error_message( e.what() );
    }

    return response;
}


HANDLER_RESULT<schematic::commands::ExportBOMResponse>
API_HANDLER_SCH::handleExportBOM(
        const HANDLER_CONTEXT<schematic::commands::ExportBOM>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::ExportBOMResponse response;

    // BOM export is typically done through Python plugins or external tools
    // This provides a basic implementation
    response.set_success( false );
    response.set_error_message( "BOM export via API is not fully implemented. "
                                "Use File > Fabrication Outputs > BOM for BOM generation." );

    return response;
}


HANDLER_RESULT<schematic::commands::ExportPlotResponse>
API_HANDLER_SCH::handleExportPlot(
        const HANDLER_CONTEXT<schematic::commands::ExportPlot>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::ExportPlotResponse response;

    // Plotting requires the plotter dialog or command-line tools
    // This is a placeholder for the API
    response.set_success( false );
    response.set_error_message( "Plot export via API is not fully implemented. "
                                "Use File > Plot for schematic plotting." );

    return response;
}


// ============================================================================
// Undo/Redo Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetUndoHistoryResponse>
API_HANDLER_SCH::handleGetUndoHistory(
        const HANDLER_CONTEXT<schematic::commands::GetUndoHistory>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetUndoHistoryResponse response;

    // Undo/redo counts from frame
    int undoCount = m_frame->GetUndoCommandCount();
    int redoCount = m_frame->GetRedoCommandCount();

    // We can only report counts, not detailed info without accessing private members
    for( int i = 0; i < undoCount; i++ )
    {
        schematic::commands::UndoRedoInfo* info = response.add_undo_stack();
        info->set_description( fmt::format( "Undo step {}", i + 1 ) );
        info->set_item_count( 1 );
    }

    for( int i = 0; i < redoCount; i++ )
    {
        schematic::commands::UndoRedoInfo* info = response.add_redo_stack();
        info->set_description( fmt::format( "Redo step {}", i + 1 ) );
        info->set_item_count( 1 );
    }

    return response;
}


HANDLER_RESULT<schematic::commands::UndoResponse>
API_HANDLER_SCH::handleUndo(
        const HANDLER_CONTEXT<schematic::commands::Undo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    schematic::commands::UndoResponse response;

    int count = aCtx.Request.count() > 0 ? aCtx.Request.count() : 1;
    int undone = 0;

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    for( int i = 0; i < count; i++ )
    {
        if( m_frame->GetUndoCommandCount() == 0 )
            break;

        mgr->RunAction( ACTIONS::undo );
        undone++;
    }

    response.set_steps_undone( undone );

    return response;
}


HANDLER_RESULT<schematic::commands::RedoResponse>
API_HANDLER_SCH::handleRedo(
        const HANDLER_CONTEXT<schematic::commands::Redo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    schematic::commands::RedoResponse response;

    int count = aCtx.Request.count() > 0 ? aCtx.Request.count() : 1;
    int redone = 0;

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    for( int i = 0; i < count; i++ )
    {
        if( m_frame->GetRedoCommandCount() == 0 )
            break;

        mgr->RunAction( ACTIONS::redo );
        redone++;
    }

    response.set_steps_redone( redone );

    return response;
}


// ============================================================================
// Viewport Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::GetViewportResponse>
API_HANDLER_SCH::handleGetViewport(
        const HANDLER_CONTEXT<schematic::commands::GetViewport>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::GetViewportResponse response;

    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();

    if( view )
    {
        schematic::commands::ViewportSettings* viewport = response.mutable_viewport();

        VECTOR2D center = view->GetCenter();
        VECTOR2I centerInt( static_cast<int64_t>( center.x ), static_cast<int64_t>( center.y ) );
        kiapi::common::PackVector2Sch( *viewport->mutable_center(), centerInt );

        viewport->set_scale( view->GetScale() );

        BOX2D viewBox = view->GetViewport();
        types::Box2* box = viewport->mutable_visible_area();
        kiapi::common::PackVector2Sch( *box->mutable_position(),
                                      VECTOR2I( static_cast<int64_t>( viewBox.GetPosition().x ),
                                                static_cast<int64_t>( viewBox.GetPosition().y ) ) );
        kiapi::common::PackVector2Sch( *box->mutable_size(),
                                      VECTOR2I( static_cast<int64_t>( viewBox.GetSize().x ),
                                                static_cast<int64_t>( viewBox.GetSize().y ) ) );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetViewport(
        const HANDLER_CONTEXT<schematic::commands::SetViewport>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();

    if( view )
    {
        if( aCtx.Request.has_center() )
        {
            VECTOR2I center = kiapi::common::UnpackVector2Sch( aCtx.Request.center() );
            view->SetCenter( VECTOR2D( center.x, center.y ) );
        }

        if( aCtx.Request.has_scale() )
        {
            view->SetScale( aCtx.Request.scale() );
        }

        m_frame->GetCanvas()->Refresh();
    }

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleZoomToFit(
        const HANDLER_CONTEXT<schematic::commands::ZoomToFit>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    TOOL_MANAGER* mgr = m_frame->GetToolManager();

    if( mgr )
    {
        mgr->RunAction( ACTIONS::zoomFitScreen );
    }

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleZoomToItems(
        const HANDLER_CONTEXT<schematic::commands::ZoomToItems>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.item_ids_size() == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No items specified" );
        return tl::unexpected( e );
    }

    // Calculate bounding box of all items
    BOX2I bbox;
    bool first = true;

    for( const types::KIID& id : aCtx.Request.item_ids() )
    {
        if( std::optional<SCH_ITEM*> itemOpt = getItemById( KIID( id.value() ) ) )
        {
            if( SCH_ITEM* item = *itemOpt )
            {
                if( first )
                {
                    bbox = item->GetBoundingBox();
                    first = false;
                }
                else
                {
                    bbox.Merge( item->GetBoundingBox() );
                }
            }
        }
    }

    if( first )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No valid items found" );
        return tl::unexpected( e );
    }

    // Apply margin
    double margin = aCtx.Request.margin_ratio() > 0 ? aCtx.Request.margin_ratio() : 0.1;
    int marginX = static_cast<int>( bbox.GetWidth() * margin );
    int marginY = static_cast<int>( bbox.GetHeight() * margin );
    bbox.Inflate( marginX, marginY );

    // Zoom to the bounding box
    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();
    if( view )
    {
        view->SetCenter( bbox.Centre() );

        BOX2D viewBox = view->GetViewport();
        double scaleX = viewBox.GetWidth() / bbox.GetWidth();
        double scaleY = viewBox.GetHeight() / bbox.GetHeight();
        double newScale = std::min( scaleX, scaleY ) * view->GetScale();
        view->SetScale( newScale );

        m_frame->GetCanvas()->Refresh();
    }

    return Empty();
}


// ============================================================================
// Highlighting Handlers
// ============================================================================

HANDLER_RESULT<Empty> API_HANDLER_SCH::handleHighlightNet(
        const HANDLER_CONTEXT<schematic::commands::HighlightNet>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxString netName = wxString::FromUTF8( aCtx.Request.net_name() );

    // Set the highlighted net
    m_frame->SetHighlightedConnection( netName, {} );
    m_frame->GetCanvas()->Refresh();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleClearHighlight(
        const HANDLER_CONTEXT<schematic::commands::ClearHighlight>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    // Clear any highlighted net
    m_frame->SetHighlightedConnection( wxEmptyString, {} );
    m_frame->GetCanvas()->Refresh();

    return Empty();
}


// ============================================================================
// Cross-Probe Handlers
// ============================================================================

HANDLER_RESULT<schematic::commands::CrossProbeResponse>
API_HANDLER_SCH::handleCrossProbeToBoard(
        const HANDLER_CONTEXT<schematic::commands::CrossProbeToBoard>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::CrossProbeResponse response;

    // Check if PCB editor is available via KIWAY
    KIWAY& kiway = m_frame->Kiway();
    KIWAY_PLAYER* pcbFrame = kiway.Player( FRAME_PCB_EDITOR, false );

    if( !pcbFrame )
    {
        response.set_board_found( false );
        response.set_items_found( 0 );
        return response;
    }

    response.set_board_found( true );

    // Collect items to cross-probe
    std::vector<SCH_ITEM*> items;

    for( const types::KIID& id : aCtx.Request.item_ids() )
    {
        if( std::optional<SCH_ITEM*> itemOpt = getItemById( KIID( id.value() ) ) )
        {
            if( *itemOpt )
                items.push_back( *itemOpt );
        }
    }

    response.set_items_found( static_cast<int>( items.size() ) );

    // Send cross-probe message to PCB editor
    if( !items.empty() && aCtx.Request.select_in_board() )
    {
        // Build a comma-separated list of reference designators for symbols
        wxString refs;

        for( SCH_ITEM* item : items )
        {
            if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
            {
                if( !refs.IsEmpty() )
                    refs += wxT( "," );
                refs += symbol->GetRef( &m_frame->GetCurrentSheet() );
            }
        }

        if( !refs.IsEmpty() )
        {
            // Send cross-probe command via KIWAY
            std::string crossProbeCmd = fmt::format( "$SELECT: {}", refs.ToStdString() );
            kiway.ExpressMail( FRAME_PCB_EDITOR, MAIL_CROSS_PROBE, crossProbeCmd );
        }
    }

    return response;
}


HANDLER_RESULT<schematic::commands::CrossProbeFromBoardResponse>
API_HANDLER_SCH::handleCrossProbeFromBoard(
        const HANDLER_CONTEXT<schematic::commands::CrossProbeFromBoard>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    schematic::commands::CrossProbeFromBoardResponse response;
    response.set_symbols_found( 0 );
    response.set_nets_found( 0 );

    // Get schematic and current sheet
    SCHEMATIC& schematic = m_frame->Schematic();
    SCH_SHEET_LIST sheetList = schematic.Hierarchy();

    // Clear existing selection if requested
    if( aCtx.Request.clear_existing_selection() )
    {
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
    }

    // Find symbols by reference designator
    std::vector<SCH_SYMBOL*> foundSymbols;

    for( const std::string& ref : aCtx.Request.references() )
    {
        wxString refStr( ref );

        // Search through all sheets for the symbol
        for( const SCH_SHEET_PATH& sheet : sheetList )
        {
            for( SCH_ITEM* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
            {
                SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

                if( symbol->GetRef( &sheet ) == refStr )
                {
                    foundSymbols.push_back( symbol );

                    // Add to response
                    types::KIID* kiid = response.add_found_symbol_ids();
                    kiid->set_value( symbol->m_Uuid.AsStdString() );
                    break;
                }
            }
        }
    }

    response.set_symbols_found( static_cast<int>( foundSymbols.size() ) );

    // Select items if requested
    if( aCtx.Request.select_items() && !foundSymbols.empty() )
    {
        SCH_SELECTION_TOOL* selTool = m_frame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();

        if( selTool )
        {
            for( SCH_SYMBOL* symbol : foundSymbols )
            {
                selTool->AddItemToSel( symbol );
            }
        }
    }

    // Highlight nets if requested
    if( aCtx.Request.highlight_nets() )
    {
        int netsFound = 0;

        for( const std::string& netName : aCtx.Request.net_names() )
        {
            wxString netStr( netName );

            // Find items on this net
            CONNECTION_GRAPH* graph = schematic.ConnectionGraph();

            if( graph )
            {
                CONNECTION_SUBGRAPH* subgraph = graph->FindFirstSubgraphByName( netStr );
                if( subgraph )
                {
                    netsFound++;
                }
            }
        }

        response.set_nets_found( netsFound );

        // If we have net names to highlight, trigger highlight mode
        if( !aCtx.Request.net_names().empty() )
        {
            // Highlight first net
            wxString firstNet( aCtx.Request.net_names( 0 ) );
            m_frame->SetHighlightedConnection( firstNet );
            m_frame->UpdateNetHighlightStatus();
        }
    }

    // Center view on found items if requested
    if( aCtx.Request.center_view() && !foundSymbols.empty() )
    {
        // Navigate to the sheet containing the first symbol
        SCH_SYMBOL* firstSymbol = foundSymbols.front();

        // Find which sheet contains this symbol
        for( const SCH_SHEET_PATH& sheet : sheetList )
        {
            for( SCH_ITEM* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
            {
                if( item == firstSymbol )
                {
                    // Navigate to this sheet if different from current
                    if( sheet != m_frame->GetCurrentSheet() )
                    {
                        m_frame->SetCurrentSheet( sheet );
                        m_frame->DisplayCurrentSheet();
                    }

                    // Center view on the symbol
                    BOX2I bbox = firstSymbol->GetBoundingBox();
                    m_frame->FocusOnLocation( bbox.Centre() );
                    break;
                }
            }
        }
    }

    m_frame->GetCanvas()->Refresh();

    return response;
}


//
// RemoveLibrary Handler
//

HANDLER_RESULT<RemoveLibraryResponse> API_HANDLER_SCH::handleRemoveLibrary(
        const HANDLER_CONTEXT<RemoveLibrary>& aCtx )
{
    RemoveLibraryResponse response;

    wxString nickname( aCtx.Request.nickname() );

    if( nickname.IsEmpty() )
    {
        response.set_status( RemoveLibraryStatus::RLS_NOT_FOUND );
        response.set_error_message( "Library nickname cannot be empty" );
        return response;
    }

    // Determine which table to remove from
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
        response.set_status( RemoveLibraryStatus::RLS_TABLE_NOT_FOUND );
        response.set_error_message( "Library table not found" );
        return response;
    }

    LIBRARY_TABLE* libTable = table.value();

    // Check if the library exists
    if( !libTable->HasRow( nickname ) )
    {
        response.set_status( RemoveLibraryStatus::RLS_NOT_FOUND );
        response.set_error_message( fmt::format( "Library '{}' not found in table",
                                                  nickname.ToStdString() ) );
        return response;
    }

    // Remove the library by finding and erasing from the rows vector
    std::vector<LIBRARY_TABLE_ROW>& rows = libTable->Rows();
    auto it = std::find_if( rows.begin(), rows.end(),
                           [&nickname]( const LIBRARY_TABLE_ROW& row )
                           {
                               return row.Nickname() == nickname;
                           } );

    if( it != rows.end() )
    {
        rows.erase( it );
    }

    // Save the library table
    LIBRARY_RESULT<void> saveResult = libTable->Save();
    if( !saveResult.has_value() )
    {
        // Library was removed from memory but save failed
        response.set_status( RemoveLibraryStatus::RLS_OK );
        response.set_error_message( fmt::format( "Library removed but table save failed: {}",
                                                  saveResult.error().message.ToStdString() ) );
        return response;
    }

    response.set_status( RemoveLibraryStatus::RLS_OK );
    return response;
}


//
// ERC Pin Type Matrix Handlers
//

HANDLER_RESULT<kiapi::schematic::commands::GetPinTypeMatrixResponse>
API_HANDLER_SCH::handleGetPinTypeMatrix(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetPinTypeMatrix>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    kiapi::schematic::commands::GetPinTypeMatrixResponse response;
    auto* matrix = response.mutable_matrix();

    SCHEMATIC& schematic = m_frame->Schematic();
    ERC_SETTINGS& ercSettings = schematic.ErcSettings();

    // Iterate through all pin type combinations
    for( int i = 0; i < ELECTRICAL_PINTYPES_TOTAL; i++ )
    {
        for( int j = 0; j < ELECTRICAL_PINTYPES_TOTAL; j++ )
        {
            PIN_ERROR error = ercSettings.GetPinMapValue( i, j );

            auto* entry = matrix->add_entries();
            entry->set_first_pin_type(
                    static_cast<kiapi::schematic::commands::ElectricalPinType>( i ) );
            entry->set_second_pin_type(
                    static_cast<kiapi::schematic::commands::ElectricalPinType>( j ) );

            switch( error )
            {
            case PIN_ERROR::OK:
                entry->set_error_type( kiapi::schematic::commands::PET_OK );
                break;
            case PIN_ERROR::WARNING:
                entry->set_error_type( kiapi::schematic::commands::PET_WARNING );
                break;
            case PIN_ERROR::PP_ERROR:
                entry->set_error_type( kiapi::schematic::commands::PET_ERROR );
                break;
            case PIN_ERROR::UNCONNECTED:
                entry->set_error_type( kiapi::schematic::commands::PET_UNCONNECTED );
                break;
            }
        }
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSetPinTypeMatrix(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SetPinTypeMatrix>& aCtx )
{
    if( !validateDocumentInternal( aCtx.Request.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    SCHEMATIC& schematic = m_frame->Schematic();
    ERC_SETTINGS& ercSettings = schematic.ErcSettings();

    // Reset to defaults if requested
    if( aCtx.Request.reset_to_defaults() )
    {
        ercSettings.ResetPinMap();
    }

    // Apply the specified entries
    for( const auto& entry : aCtx.Request.entries() )
    {
        int firstType = static_cast<int>( entry.first_pin_type() );
        int secondType = static_cast<int>( entry.second_pin_type() );

        if( firstType >= ELECTRICAL_PINTYPES_TOTAL || secondType >= ELECTRICAL_PINTYPES_TOTAL )
            continue;

        PIN_ERROR errorType;
        switch( entry.error_type() )
        {
        case kiapi::schematic::commands::PET_OK:
            errorType = PIN_ERROR::OK;
            break;
        case kiapi::schematic::commands::PET_WARNING:
            errorType = PIN_ERROR::WARNING;
            break;
        case kiapi::schematic::commands::PET_ERROR:
            errorType = PIN_ERROR::PP_ERROR;
            break;
        case kiapi::schematic::commands::PET_UNCONNECTED:
            errorType = PIN_ERROR::UNCONNECTED;
            break;
        default:
            errorType = PIN_ERROR::OK;
            break;
        }

        ercSettings.SetPinMapValue( firstType, secondType, errorType );
    }

    return Empty();
}


//
// Design Block Handlers
//

HANDLER_RESULT<kiapi::schematic::commands::GetDesignBlocksResponse>
API_HANDLER_SCH::handleGetDesignBlocks(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetDesignBlocks>& aCtx )
{
    kiapi::schematic::commands::GetDesignBlocksResponse response;

    DESIGN_BLOCK_LIBRARY_ADAPTER* libAdapter = m_frame->Prj().DesignBlockLibs();

    if( !libAdapter )
    {
        // No design block library adapter - return empty response
        return response;
    }

    std::vector<wxString> libraryNames;

    if( aCtx.Request.library_nickname().empty() )
    {
        // Get all libraries
        libraryNames = libAdapter->GetLibraryNames();
    }
    else
    {
        // Get specific library
        libraryNames.push_back( wxString::FromUTF8( aCtx.Request.library_nickname() ) );
    }

    for( const wxString& libName : libraryNames )
    {
        if( !libAdapter->HasLibrary( libName ) )
            continue;

        std::vector<wxString> blockNames;

        try
        {
            blockNames = libAdapter->GetDesignBlockNames( libName );
        }
        catch( const IO_ERROR& )
        {
            continue;
        }

        for( const wxString& blockName : blockNames )
        {
            auto* info = response.add_design_blocks();
            info->set_lib_id( fmt::format( "{}:{}", libName.ToStdString(), blockName.ToStdString() ) );
            info->set_name( blockName.ToStdString() );
            info->set_library_nickname( libName.ToStdString() );

            // Try to get description from the design block
            try
            {
                const DESIGN_BLOCK* block = libAdapter->GetEnumeratedDesignBlock( libName, blockName );
                if( block )
                {
                    info->set_description( block->GetLibDescription().ToStdString() );
                    info->set_keywords( block->GetKeywords().ToStdString() );
                }
            }
            catch( const IO_ERROR& )
            {
                // Ignore errors getting detailed info
            }
        }
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SearchDesignBlocksResponse>
API_HANDLER_SCH::handleSearchDesignBlocks(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SearchDesignBlocks>& aCtx )
{
    kiapi::schematic::commands::SearchDesignBlocksResponse response;

    DESIGN_BLOCK_LIBRARY_ADAPTER* libAdapter = m_frame->Prj().DesignBlockLibs();

    if( !libAdapter )
        return response;

    wxString query = wxString::FromUTF8( aCtx.Request.query() ).Lower();
    int maxResults = aCtx.Request.max_results();
    int count = 0;

    std::vector<wxString> libraryNames;

    if( aCtx.Request.libraries().empty() )
    {
        libraryNames = libAdapter->GetLibraryNames();
    }
    else
    {
        for( const auto& lib : aCtx.Request.libraries() )
            libraryNames.push_back( wxString::FromUTF8( lib ) );
    }

    for( const wxString& libName : libraryNames )
    {
        if( !libAdapter->HasLibrary( libName ) )
            continue;

        std::vector<wxString> blockNames;

        try
        {
            blockNames = libAdapter->GetDesignBlockNames( libName );
        }
        catch( const IO_ERROR& )
        {
            continue;
        }

        for( const wxString& blockName : blockNames )
        {
            if( maxResults > 0 && count >= maxResults )
                break;

            bool matches = false;

            // Check if name matches
            if( blockName.Lower().Contains( query ) )
                matches = true;

            // Check description and keywords if we can get the design block
            if( !matches )
            {
                try
                {
                    const DESIGN_BLOCK* block = libAdapter->GetEnumeratedDesignBlock( libName, blockName );
                    if( block )
                    {
                        if( block->GetLibDescription().Lower().Contains( query ) ||
                            block->GetKeywords().Lower().Contains( query ) )
                        {
                            matches = true;
                        }
                    }
                }
                catch( const IO_ERROR& )
                {
                    // Ignore
                }
            }

            if( matches )
            {
                auto* info = response.add_design_blocks();
                info->set_lib_id( fmt::format( "{}:{}", libName.ToStdString(), blockName.ToStdString() ) );
                info->set_name( blockName.ToStdString() );
                info->set_library_nickname( libName.ToStdString() );

                try
                {
                    const DESIGN_BLOCK* block = libAdapter->GetEnumeratedDesignBlock( libName, blockName );
                    if( block )
                    {
                        info->set_description( block->GetLibDescription().ToStdString() );
                        info->set_keywords( block->GetKeywords().ToStdString() );
                    }
                }
                catch( const IO_ERROR& )
                {
                    // Ignore
                }

                count++;
            }
        }

        if( maxResults > 0 && count >= maxResults )
            break;
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SaveDesignBlockResponse>
API_HANDLER_SCH::handleSaveSelectionAsDesignBlock(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SaveSelectionAsDesignBlock>& aCtx )
{
    kiapi::schematic::commands::SaveDesignBlockResponse response;

    // This operation requires user interaction through the design block control tool
    // For API purposes, we'll indicate this is not directly supported
    response.set_success( false );
    response.set_error_message( "SaveSelectionAsDesignBlock requires user interaction. "
                                 "Use the SCH_DESIGN_BLOCK_CONTROL tool action instead." );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SaveDesignBlockResponse>
API_HANDLER_SCH::handleSaveSheetAsDesignBlock(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SaveSheetAsDesignBlock>& aCtx )
{
    kiapi::schematic::commands::SaveDesignBlockResponse response;

    // This operation requires user interaction through the design block control tool
    response.set_success( false );
    response.set_error_message( "SaveSheetAsDesignBlock requires user interaction. "
                                 "Use the SCH_DESIGN_BLOCK_CONTROL tool action instead." );

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::DeleteDesignBlockResponse>
API_HANDLER_SCH::handleDeleteDesignBlock(
        const HANDLER_CONTEXT<kiapi::schematic::commands::DeleteDesignBlock>& aCtx )
{
    kiapi::schematic::commands::DeleteDesignBlockResponse response;

    LIB_ID libId;

    if( !libId.Parse( aCtx.Request.lib_id() ) )
    {
        response.set_success( false );
        response.set_error_message( "Invalid lib_id format. Expected 'Library:BlockName'" );
        return response;
    }

    DESIGN_BLOCK_LIBRARY_ADAPTER* libAdapter = m_frame->Prj().DesignBlockLibs();

    if( !libAdapter )
    {
        response.set_success( false );
        response.set_error_message( "No design block library adapter available" );
        return response;
    }

    wxString libName = libId.GetLibNickname();
    wxString blockName = libId.GetLibItemName();

    if( !libAdapter->HasLibrary( libName ) )
    {
        response.set_success( false );
        response.set_error_message( fmt::format( "Library '{}' not found", libName.ToStdString() ) );
        return response;
    }

    try
    {
        libAdapter->DeleteDesignBlock( libName, blockName );
        response.set_success( true );
    }
    catch( const IO_ERROR& e )
    {
        response.set_success( false );
        response.set_error_message( e.What().ToStdString() );
    }

    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::PlaceDesignBlockResponse>
API_HANDLER_SCH::handlePlaceDesignBlock(
        const HANDLER_CONTEXT<kiapi::schematic::commands::PlaceDesignBlock>& aCtx )
{
    kiapi::schematic::commands::PlaceDesignBlockResponse response;

    // Placing design blocks requires interaction through the design block control tool
    response.set_success( false );
    response.set_error_message( "PlaceDesignBlock requires user interaction. "
                                 "Use the SCH_DESIGN_BLOCK_CONTROL tool action instead." );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSaveDocument(
        const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    m_frame->SaveProject();
    return Empty();
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_SCH::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SavedDocumentResponse response;
    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    SCHEMATIC& schematic = m_frame->Schematic();

    // Get the current sheet's file path
    wxString filePath = schematic.Root().GetFileName();

    if( filePath.IsEmpty() || !wxFileExists( filePath ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Schematic file not found on disk. Save the document first." );
        return tl::unexpected( e );
    }

    try
    {
        wxFFileInputStream fileStream( filePath );

        if( !fileStream.IsOk() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "Could not open file: {}", filePath.ToStdString() ) );
            return tl::unexpected( e );
        }

        wxStringOutputStream stringStream;
        fileStream.Read( stringStream );

        response.set_contents( stringStream.GetString().ToUTF8().data() );
    }
    catch( const std::exception& e )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Error reading schematic file: {}", e.what() ) );
        return tl::unexpected( err );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleRefreshEditor(
        const HANDLER_CONTEXT<RefreshEditor>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    m_frame->RefreshCanvas();
    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSaveCopyOfDocument(
        const HANDLER_CONTEXT<SaveCopyOfDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    wxFileName schPath( m_frame->Prj().AbsolutePath( wxString::FromUTF8( aCtx.Request.path() ) ) );

    if( !schPath.IsOk() || !schPath.IsDirWritable() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' could not be opened",
                                          schPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( schPath.FileExists()
        && ( !schPath.IsFileWritable() || !aCtx.Request.options().overwrite() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' exists and cannot be overwritten",
                                          schPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    if( schPath.GetExt() != FILEEXT::KiCadSchematicFileExtension )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "save path '{}' must have a kicad_sch extension",
                                          schPath.GetFullPath().ToStdString() ) );
        return tl::unexpected( e );
    }

    SCHEMATIC& schematic = m_frame->Schematic();
    SCH_SHEET* currentSheet = m_frame->GetCurrentSheet().Last();

    if( currentSheet->GetFileName().Matches( schPath.GetFullPath() ) )
    {
        m_frame->SaveProject();
        return Empty();
    }

    // Save current sheet to the new path using the public SCH_IO interface
    SCH_IO_MGR::SCH_FILE_T pluginType = SCH_IO_MGR::GuessPluginTypeFromSchPath(
            schPath.GetFullPath() );

    if( pluginType == SCH_IO_MGR::SCH_FILE_UNKNOWN )
        pluginType = SCH_IO_MGR::SCH_KICAD;

    IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( pluginType ) );

    try
    {
        pi->SaveSchematicFile( schPath.GetFullPath(), currentSheet, &schematic );
    }
    catch( const IO_ERROR& ioe )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Error saving schematic file: {}",
                                          ioe.What().ToStdString() ) );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleRevertDocument(
        const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SCHEMATIC& schematic = m_frame->Schematic();
    SCH_SHEET& root = schematic.Root();

    // Navigate to root sheet if not already there
    if( m_frame->GetCurrentSheet().Last() != &root )
    {
        SCH_SHEET_PATH rootSheetPath;
        rootSheetPath.push_back( &root );
        m_frame->SetCurrentSheet( rootSheetPath );
    }

    wxFileName fn = m_frame->Prj().AbsolutePath( schematic.GetFileName() );

    // Mark all screens as not modified so we don't get prompted
    SCH_SCREENS screenList( schematic.Root() );

    for( SCH_SCREEN* screen = screenList.GetFirst(); screen; screen = screenList.GetNext() )
        screen->SetContentModified( false );

    m_frame->ReleaseFile();
    m_frame->OpenProjectFiles( std::vector<wxString>( 1, fn.GetFullPath() ), KICTL_REVERT );

    return Empty();
}


HANDLER_RESULT<SavedSelectionResponse> API_HANDLER_SCH::handleSaveSelectionToString(
        const HANDLER_CONTEXT<SaveSelectionToString>& aCtx )
{
    SavedSelectionResponse response;

    TOOL_MANAGER* mgr = m_frame->GetToolManager();
    SCH_SELECTION_TOOL* selectionTool = mgr->GetTool<SCH_SELECTION_TOOL>();
    SCH_SELECTION& selection = selectionTool->GetSelection();

    if( selection.Empty() )
    {
        // Return empty response if nothing is selected
        response.set_contents( "" );
        return response;
    }

    SCHEMATIC& schematic = m_frame->Schematic();
    SCH_SHEET_PATH selPath = m_frame->GetCurrentSheet();

    // Set the screen on the selection - required for Format() to work properly
    selection.SetScreen( m_frame->GetScreen() );

    STRING_FORMATTER formatter;
    SCH_IO_KICAD_SEXPR plugin;

    try
    {
        plugin.Format( &selection, &selPath, schematic, &formatter, true );

        std::string prettyData = formatter.GetString();
        KICAD_FORMAT::Prettify( prettyData, KICAD_FORMAT::FORMAT_MODE::COMPACT_TEXT_PROPERTIES );

        response.set_contents( prettyData );
    }
    catch( const IO_ERROR& ioe )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Error formatting selection: {}",
                                          ioe.What().ToStdString() ) );
        return tl::unexpected( e );
    }

    return response;
}


HANDLER_RESULT<CreateItemsResponse> API_HANDLER_SCH::handleParseAndCreateItemsFromString(
        const HANDLER_CONTEXT<ParseAndCreateItemsFromString>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    // TODO: Implement parsing and creating items from string
    // This would require implementing the inverse of SaveSelectionToString
    CreateItemsResponse response;
    return response;
}
