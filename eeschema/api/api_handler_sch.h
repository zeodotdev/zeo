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

#ifndef KICAD_API_HANDLER_SCH_H
#define KICAD_API_HANDLER_SCH_H

#include <google/protobuf/empty.pb.h>

#include <api/api_handler_editor.h>
#include <api/common/commands/editor_commands.pb.h>
#include <api/schematic/schematic_commands.pb.h>
#include <kiid.h>

using namespace kiapi;
using namespace kiapi::common;
using google::protobuf::Empty;

// Forward declarations for schematic-specific types
namespace kiapi { namespace schematic { namespace commands {
    class GetSheetHierarchy;
    class GetSheetHierarchyResponse;
    class SheetHierarchyNode;
    class GetCurrentSheet;
    class GetCurrentSheetResponse;
    class NavigateToSheet;
    class CreateSheet;
    class CreateSheetResponse;
    class DeleteSheet;
    class GetSheetProperties;
    class GetSheetPropertiesResponse;
    class SetSheetProperties;
    class CreateSheetPin;
    class CreateSheetPinResponse;
    class DeleteSheetPin;
    class GetSheetPins;
    class GetSheetPinsResponse;
    class SyncSheetPins;
    class SyncSheetPinsResponse;
    // Annotation commands
    class AnnotateSymbols;
    class AnnotateSymbolsResponse;
    class ClearAnnotation;
    class ClearAnnotationResponse;
    class CheckAnnotation;
    class CheckAnnotationResponse;
    // ERC commands
    class RunERC;
    class RunERCResponse;
    class GetERCViolations;
    class GetERCViolationsResponse;
    class ClearERCMarkers;
    class ClearERCMarkersResponse;
    class ExcludeERCViolation;
    // Connectivity commands
    class GetNets;
    class GetNetsResponse;
    class GetBuses;
    class GetBusesResponse;
    class GetNetForItem;
    class GetNetForItemResponse;
    class GetBusMembers;
    class GetBusMembersResponse;
    class GetNetItems;
    class GetNetItemsResponse;
    // Grid settings commands
    class GetGridSettings;
    class GetGridSettingsResponse;
    class SetGridSettings;
    // ERC settings commands
    class GetERCSettings;
    class GetERCSettingsResponse;
    class SetERCSettings;
    // Net class commands
    class AssignNetToClass;
    class GetNetClasses;
    class GetNetClassesResponse;
    class SetNetClass;
    class DeleteNetClass;
    class GetNetClassAssignments;
    class GetNetClassAssignmentsResponse;
    class SetNetClassAssignments;
    class AddNetClassAssignment;
    class RemoveNetClassAssignment;
    // Bus alias commands
    class BusAliasData;
    class GetBusAliases;
    class GetBusAliasesResponse;
    class SetBusAlias;
    class DeleteBusAlias;
    class SetBusAliases;
    // Editor preferences commands
    class GetEditorPreferences;
    class GetEditorPreferencesResponse;
    class SetEditorPreferences;
    // Formatting settings commands
    class GetFormattingSettings;
    class GetFormattingSettingsResponse;
    class SetFormattingSettings;
    // Field name templates commands
    class GetFieldNameTemplates;
    class GetFieldNameTemplatesResponse;
    class SetFieldNameTemplates;
    // Annotation settings commands
    class GetAnnotationSettings;
    class GetAnnotationSettingsResponse;
    class SetAnnotationSettings;
    // Simulation settings commands
    class GetSimulationSettings;
    class GetSimulationSettingsResponse;
    class SetSimulationSettings;
    // Library query commands
    class GetLibrarySymbols;
    class GetLibrarySymbolsResponse;
    class SearchLibrarySymbols;
    class SearchLibrarySymbolsResponse;
    class GetSymbolInfo;
    class GetSymbolInfoResponse;
    class GetTransformedPinPosition;
    class GetTransformedPinPositionResponse;
    // Simulation commands
    class RunSimulation;
    class RunSimulationResponse;
    class GetSimulationResults;
    class GetSimulationResultsResponse;
    // Export commands
    class ExportNetlist;
    class ExportNetlistResponse;
    class ExportBOM;
    class ExportBOMResponse;
    class ExportPlot;
    class ExportPlotResponse;
    // Undo/Redo commands
    class GetUndoHistory;
    class GetUndoHistoryResponse;
    class Undo;
    class UndoResponse;
    class Redo;
    class RedoResponse;
    // Viewport commands
    class GetViewport;
    class GetViewportResponse;
    class SetViewport;
    class ZoomToFit;
    class ZoomToItems;
    // Highlighting commands
    class HighlightNet;
    class ClearHighlight;
    // Cross-probe commands
    class CrossProbeToBoard;
    class CrossProbeResponse;
    class CrossProbeFromBoard;
    class CrossProbeFromBoardResponse;
    // ERC Pin Type Matrix commands
    class GetPinTypeMatrix;
    class GetPinTypeMatrixResponse;
    class SetPinTypeMatrix;
    // Design Block commands
    class GetDesignBlocks;
    class GetDesignBlocksResponse;
    class SearchDesignBlocks;
    class SearchDesignBlocksResponse;
    class SaveSelectionAsDesignBlock;
    class SaveSheetAsDesignBlock;
    class SaveDesignBlockResponse;
    class DeleteDesignBlock;
    class DeleteDesignBlockResponse;
    class PlaceDesignBlock;
    class PlaceDesignBlockResponse;
} } }

class SCH_EDIT_FRAME;
class SCH_ITEM;
class SCH_SHEET;
class SCH_SHEET_PATH;


class API_HANDLER_SCH : public API_HANDLER_EDITOR
{
public:
    API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame );

protected:
    std::unique_ptr<COMMIT> createCommit() override;

    kiapi::common::types::DocumentType thisDocumentType() const override
    {
        return kiapi::common::types::DOCTYPE_SCHEMATIC;
    }

    bool validateDocumentInternal( const DocumentSpecifier& aDocument ) const override;

    void pushCurrentCommit( const std::string& aClientName, const wxString& aMessage ) override;

    HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> createItemForType( KICAD_T aType, EDA_ITEM* aContainer );

    HANDLER_RESULT<common::types::ItemRequestStatus> handleCreateUpdateItemsInternal(
            bool aCreate, const std::string& aClientName, const common::types::ItemHeader& aHeader,
            const google::protobuf::RepeatedPtrField<google::protobuf::Any>&   aItems,
            std::function<void( common::commands::ItemStatus, google::protobuf::Any )> aItemHandler ) override;

    void deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                              const std::string&                  aClientName ) override;

    std::optional<EDA_ITEM*> getItemFromDocument( const DocumentSpecifier& aDocument, const KIID& aId ) override;

private:
    HANDLER_RESULT<commands::GetOpenDocumentsResponse>
    handleGetOpenDocuments( const HANDLER_CONTEXT<commands::GetOpenDocuments>& aCtx );

    HANDLER_RESULT<commands::GetItemsResponse>
    handleGetItems( const HANDLER_CONTEXT<commands::GetItems>& aCtx );

    HANDLER_RESULT<commands::GetItemsResponse>
    handleGetItemsById( const HANDLER_CONTEXT<commands::GetItemsById>& aCtx );

    HANDLER_RESULT<commands::GetBoundingBoxResponse>
    handleGetBoundingBox( const HANDLER_CONTEXT<commands::GetBoundingBox>& aCtx );

    // Selection handlers
    HANDLER_RESULT<commands::SelectionResponse>
    handleGetSelection( const HANDLER_CONTEXT<commands::GetSelection>& aCtx );

    HANDLER_RESULT<commands::SelectionResponse>
    handleAddToSelection( const HANDLER_CONTEXT<commands::AddToSelection>& aCtx );

    HANDLER_RESULT<commands::SelectionResponse>
    handleRemoveFromSelection( const HANDLER_CONTEXT<commands::RemoveFromSelection>& aCtx );

    HANDLER_RESULT<Empty>
    handleClearSelection( const HANDLER_CONTEXT<commands::ClearSelection>& aCtx );

    // Library management handlers
    HANDLER_RESULT<commands::GetLibrariesResponse>
    handleGetLibraries( const HANDLER_CONTEXT<commands::GetLibraries>& aCtx );

    HANDLER_RESULT<commands::AddLibraryResponse>
    handleAddLibrary( const HANDLER_CONTEXT<commands::AddLibrary>& aCtx );

    HANDLER_RESULT<commands::RemoveLibraryResponse>
    handleRemoveLibrary( const HANDLER_CONTEXT<commands::RemoveLibrary>& aCtx );

    // Document management handlers
    HANDLER_RESULT<commands::CreateDocumentResponse>
    handleCreateDocument( const HANDLER_CONTEXT<commands::CreateDocument>& aCtx );

    HANDLER_RESULT<commands::OpenDocumentResponse>
    handleOpenDocument( const HANDLER_CONTEXT<commands::OpenDocument>& aCtx );

    HANDLER_RESULT<Empty>
    handleCloseDocument( const HANDLER_CONTEXT<commands::CloseDocument>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetActiveDocument( const HANDLER_CONTEXT<commands::SetActiveDocument>& aCtx );

    // Sheet hierarchy handlers
    HANDLER_RESULT<schematic::commands::GetSheetHierarchyResponse>
    handleGetSheetHierarchy( const HANDLER_CONTEXT<schematic::commands::GetSheetHierarchy>& aCtx );

    HANDLER_RESULT<schematic::commands::GetCurrentSheetResponse>
    handleGetCurrentSheet( const HANDLER_CONTEXT<schematic::commands::GetCurrentSheet>& aCtx );

    HANDLER_RESULT<Empty>
    handleNavigateToSheet( const HANDLER_CONTEXT<schematic::commands::NavigateToSheet>& aCtx );

    // Sheet CRUD handlers
    HANDLER_RESULT<schematic::commands::CreateSheetResponse>
    handleCreateSheet( const HANDLER_CONTEXT<schematic::commands::CreateSheet>& aCtx );

    HANDLER_RESULT<Empty>
    handleDeleteSheet( const HANDLER_CONTEXT<schematic::commands::DeleteSheet>& aCtx );

    HANDLER_RESULT<schematic::commands::GetSheetPropertiesResponse>
    handleGetSheetProperties( const HANDLER_CONTEXT<schematic::commands::GetSheetProperties>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetSheetProperties( const HANDLER_CONTEXT<schematic::commands::SetSheetProperties>& aCtx );

    // Sheet pin handlers
    HANDLER_RESULT<schematic::commands::CreateSheetPinResponse>
    handleCreateSheetPin( const HANDLER_CONTEXT<schematic::commands::CreateSheetPin>& aCtx );

    HANDLER_RESULT<Empty>
    handleDeleteSheetPin( const HANDLER_CONTEXT<schematic::commands::DeleteSheetPin>& aCtx );

    HANDLER_RESULT<schematic::commands::GetSheetPinsResponse>
    handleGetSheetPins( const HANDLER_CONTEXT<schematic::commands::GetSheetPins>& aCtx );

    HANDLER_RESULT<schematic::commands::SyncSheetPinsResponse>
    handleSyncSheetPins( const HANDLER_CONTEXT<schematic::commands::SyncSheetPins>& aCtx );

    // Annotation handlers
    HANDLER_RESULT<schematic::commands::AnnotateSymbolsResponse>
    handleAnnotateSymbols( const HANDLER_CONTEXT<schematic::commands::AnnotateSymbols>& aCtx );

    HANDLER_RESULT<schematic::commands::ClearAnnotationResponse>
    handleClearAnnotation( const HANDLER_CONTEXT<schematic::commands::ClearAnnotation>& aCtx );

    HANDLER_RESULT<schematic::commands::CheckAnnotationResponse>
    handleCheckAnnotation( const HANDLER_CONTEXT<schematic::commands::CheckAnnotation>& aCtx );

    // ERC handlers
    HANDLER_RESULT<schematic::commands::RunERCResponse>
    handleRunERC( const HANDLER_CONTEXT<schematic::commands::RunERC>& aCtx );

    HANDLER_RESULT<schematic::commands::GetERCViolationsResponse>
    handleGetERCViolations( const HANDLER_CONTEXT<schematic::commands::GetERCViolations>& aCtx );

    HANDLER_RESULT<schematic::commands::ClearERCMarkersResponse>
    handleClearERCMarkers( const HANDLER_CONTEXT<schematic::commands::ClearERCMarkers>& aCtx );

    HANDLER_RESULT<Empty>
    handleExcludeERCViolation( const HANDLER_CONTEXT<schematic::commands::ExcludeERCViolation>& aCtx );

    // Connectivity handlers
    HANDLER_RESULT<schematic::commands::GetNetsResponse>
    handleGetNets( const HANDLER_CONTEXT<schematic::commands::GetNets>& aCtx );

    HANDLER_RESULT<schematic::commands::GetBusesResponse>
    handleGetBuses( const HANDLER_CONTEXT<schematic::commands::GetBuses>& aCtx );

    HANDLER_RESULT<schematic::commands::GetNetForItemResponse>
    handleGetNetForItem( const HANDLER_CONTEXT<schematic::commands::GetNetForItem>& aCtx );

    HANDLER_RESULT<schematic::commands::GetBusMembersResponse>
    handleGetBusMembers( const HANDLER_CONTEXT<schematic::commands::GetBusMembers>& aCtx );

    HANDLER_RESULT<schematic::commands::GetNetItemsResponse>
    handleGetNetItems( const HANDLER_CONTEXT<schematic::commands::GetNetItems>& aCtx );

    // Grid settings handlers
    HANDLER_RESULT<schematic::commands::GetGridSettingsResponse>
    handleGetGridSettings( const HANDLER_CONTEXT<schematic::commands::GetGridSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetGridSettings( const HANDLER_CONTEXT<schematic::commands::SetGridSettings>& aCtx );

    // ERC settings handlers
    HANDLER_RESULT<schematic::commands::GetERCSettingsResponse>
    handleGetERCSettings( const HANDLER_CONTEXT<schematic::commands::GetERCSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetERCSettings( const HANDLER_CONTEXT<schematic::commands::SetERCSettings>& aCtx );

    // Net class handlers
    HANDLER_RESULT<Empty>
    handleAssignNetToClass( const HANDLER_CONTEXT<schematic::commands::AssignNetToClass>& aCtx );

    HANDLER_RESULT<schematic::commands::GetNetClassesResponse>
    handleGetNetClasses( const HANDLER_CONTEXT<schematic::commands::GetNetClasses>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetNetClass( const HANDLER_CONTEXT<schematic::commands::SetNetClass>& aCtx );

    HANDLER_RESULT<Empty>
    handleDeleteNetClass( const HANDLER_CONTEXT<schematic::commands::DeleteNetClass>& aCtx );

    HANDLER_RESULT<schematic::commands::GetNetClassAssignmentsResponse>
    handleGetNetClassAssignments( const HANDLER_CONTEXT<schematic::commands::GetNetClassAssignments>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetNetClassAssignments( const HANDLER_CONTEXT<schematic::commands::SetNetClassAssignments>& aCtx );

    HANDLER_RESULT<Empty>
    handleAddNetClassAssignment( const HANDLER_CONTEXT<schematic::commands::AddNetClassAssignment>& aCtx );

    HANDLER_RESULT<Empty>
    handleRemoveNetClassAssignment( const HANDLER_CONTEXT<schematic::commands::RemoveNetClassAssignment>& aCtx );

    // Bus alias handlers
    HANDLER_RESULT<schematic::commands::GetBusAliasesResponse>
    handleGetBusAliases( const HANDLER_CONTEXT<schematic::commands::GetBusAliases>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetBusAlias( const HANDLER_CONTEXT<schematic::commands::SetBusAlias>& aCtx );

    HANDLER_RESULT<Empty>
    handleDeleteBusAlias( const HANDLER_CONTEXT<schematic::commands::DeleteBusAlias>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetBusAliases( const HANDLER_CONTEXT<schematic::commands::SetBusAliases>& aCtx );

    // Editor preferences handlers
    HANDLER_RESULT<schematic::commands::GetEditorPreferencesResponse>
    handleGetEditorPreferences( const HANDLER_CONTEXT<schematic::commands::GetEditorPreferences>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetEditorPreferences( const HANDLER_CONTEXT<schematic::commands::SetEditorPreferences>& aCtx );

    // Formatting settings handlers (project-level settings from Schematic Setup)
    HANDLER_RESULT<schematic::commands::GetFormattingSettingsResponse>
    handleGetFormattingSettings( const HANDLER_CONTEXT<schematic::commands::GetFormattingSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetFormattingSettings( const HANDLER_CONTEXT<schematic::commands::SetFormattingSettings>& aCtx );

    // Field name templates handlers (project-level settings from Schematic Setup)
    HANDLER_RESULT<schematic::commands::GetFieldNameTemplatesResponse>
    handleGetFieldNameTemplates( const HANDLER_CONTEXT<schematic::commands::GetFieldNameTemplates>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetFieldNameTemplates( const HANDLER_CONTEXT<schematic::commands::SetFieldNameTemplates>& aCtx );

    // Annotation settings handlers (project-level settings from Schematic Setup)
    HANDLER_RESULT<schematic::commands::GetAnnotationSettingsResponse>
    handleGetAnnotationSettings( const HANDLER_CONTEXT<schematic::commands::GetAnnotationSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetAnnotationSettings( const HANDLER_CONTEXT<schematic::commands::SetAnnotationSettings>& aCtx );

    // Simulation settings handlers
    HANDLER_RESULT<schematic::commands::GetSimulationSettingsResponse>
    handleGetSimulationSettings( const HANDLER_CONTEXT<schematic::commands::GetSimulationSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetSimulationSettings( const HANDLER_CONTEXT<schematic::commands::SetSimulationSettings>& aCtx );

    // Library query handlers
    HANDLER_RESULT<schematic::commands::GetLibrarySymbolsResponse>
    handleGetLibrarySymbols( const HANDLER_CONTEXT<schematic::commands::GetLibrarySymbols>& aCtx );

    HANDLER_RESULT<schematic::commands::SearchLibrarySymbolsResponse>
    handleSearchLibrarySymbols( const HANDLER_CONTEXT<schematic::commands::SearchLibrarySymbols>& aCtx );

    HANDLER_RESULT<schematic::commands::GetSymbolInfoResponse>
    handleGetSymbolInfo( const HANDLER_CONTEXT<schematic::commands::GetSymbolInfo>& aCtx );

    HANDLER_RESULT<schematic::commands::GetTransformedPinPositionResponse>
    handleGetTransformedPinPosition( const HANDLER_CONTEXT<schematic::commands::GetTransformedPinPosition>& aCtx );

    // Simulation handlers
    HANDLER_RESULT<schematic::commands::RunSimulationResponse>
    handleRunSimulation( const HANDLER_CONTEXT<schematic::commands::RunSimulation>& aCtx );

    HANDLER_RESULT<schematic::commands::GetSimulationResultsResponse>
    handleGetSimulationResults( const HANDLER_CONTEXT<schematic::commands::GetSimulationResults>& aCtx );

    // Export handlers
    HANDLER_RESULT<schematic::commands::ExportNetlistResponse>
    handleExportNetlist( const HANDLER_CONTEXT<schematic::commands::ExportNetlist>& aCtx );

    HANDLER_RESULT<schematic::commands::ExportBOMResponse>
    handleExportBOM( const HANDLER_CONTEXT<schematic::commands::ExportBOM>& aCtx );

    HANDLER_RESULT<schematic::commands::ExportPlotResponse>
    handleExportPlot( const HANDLER_CONTEXT<schematic::commands::ExportPlot>& aCtx );

    // Undo/Redo handlers
    HANDLER_RESULT<schematic::commands::GetUndoHistoryResponse>
    handleGetUndoHistory( const HANDLER_CONTEXT<schematic::commands::GetUndoHistory>& aCtx );

    HANDLER_RESULT<schematic::commands::UndoResponse>
    handleUndo( const HANDLER_CONTEXT<schematic::commands::Undo>& aCtx );

    HANDLER_RESULT<schematic::commands::RedoResponse>
    handleRedo( const HANDLER_CONTEXT<schematic::commands::Redo>& aCtx );

    // Viewport handlers
    HANDLER_RESULT<schematic::commands::GetViewportResponse>
    handleGetViewport( const HANDLER_CONTEXT<schematic::commands::GetViewport>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetViewport( const HANDLER_CONTEXT<schematic::commands::SetViewport>& aCtx );

    HANDLER_RESULT<Empty>
    handleZoomToFit( const HANDLER_CONTEXT<schematic::commands::ZoomToFit>& aCtx );

    HANDLER_RESULT<Empty>
    handleZoomToItems( const HANDLER_CONTEXT<schematic::commands::ZoomToItems>& aCtx );

    // Highlighting handlers
    HANDLER_RESULT<Empty>
    handleHighlightNet( const HANDLER_CONTEXT<schematic::commands::HighlightNet>& aCtx );

    HANDLER_RESULT<Empty>
    handleClearHighlight( const HANDLER_CONTEXT<schematic::commands::ClearHighlight>& aCtx );

    // Cross-probe handlers
    HANDLER_RESULT<schematic::commands::CrossProbeResponse>
    handleCrossProbeToBoard( const HANDLER_CONTEXT<schematic::commands::CrossProbeToBoard>& aCtx );

    HANDLER_RESULT<schematic::commands::CrossProbeFromBoardResponse>
    handleCrossProbeFromBoard( const HANDLER_CONTEXT<schematic::commands::CrossProbeFromBoard>& aCtx );

    // ERC Pin Type Matrix handlers
    HANDLER_RESULT<schematic::commands::GetPinTypeMatrixResponse>
    handleGetPinTypeMatrix( const HANDLER_CONTEXT<schematic::commands::GetPinTypeMatrix>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetPinTypeMatrix( const HANDLER_CONTEXT<schematic::commands::SetPinTypeMatrix>& aCtx );

    // Design Block handlers
    HANDLER_RESULT<schematic::commands::GetDesignBlocksResponse>
    handleGetDesignBlocks( const HANDLER_CONTEXT<schematic::commands::GetDesignBlocks>& aCtx );

    HANDLER_RESULT<schematic::commands::SearchDesignBlocksResponse>
    handleSearchDesignBlocks( const HANDLER_CONTEXT<schematic::commands::SearchDesignBlocks>& aCtx );

    HANDLER_RESULT<schematic::commands::SaveDesignBlockResponse>
    handleSaveSelectionAsDesignBlock( const HANDLER_CONTEXT<schematic::commands::SaveSelectionAsDesignBlock>& aCtx );

    HANDLER_RESULT<schematic::commands::SaveDesignBlockResponse>
    handleSaveSheetAsDesignBlock( const HANDLER_CONTEXT<schematic::commands::SaveSheetAsDesignBlock>& aCtx );

    HANDLER_RESULT<schematic::commands::DeleteDesignBlockResponse>
    handleDeleteDesignBlock( const HANDLER_CONTEXT<schematic::commands::DeleteDesignBlock>& aCtx );

    HANDLER_RESULT<schematic::commands::PlaceDesignBlockResponse>
    handlePlaceDesignBlock( const HANDLER_CONTEXT<schematic::commands::PlaceDesignBlock>& aCtx );

    // Title block handlers
    HANDLER_RESULT<types::TitleBlockInfo>
    handleGetTitleBlockInfo( const HANDLER_CONTEXT<commands::GetTitleBlockInfo>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetTitleBlockInfo( const HANDLER_CONTEXT<commands::SetTitleBlockInfo>& aCtx );

    // Page settings handlers
    HANDLER_RESULT<types::PageInfo>
    handleGetPageSettings( const HANDLER_CONTEXT<commands::GetPageSettings>& aCtx );

    HANDLER_RESULT<Empty>
    handleSetPageSettings( const HANDLER_CONTEXT<commands::SetPageSettings>& aCtx );

    // Document management handlers
    HANDLER_RESULT<Empty>
    handleSaveDocument( const HANDLER_CONTEXT<commands::SaveDocument>& aCtx );

    HANDLER_RESULT<commands::SavedDocumentResponse>
    handleSaveDocumentToString( const HANDLER_CONTEXT<commands::SaveDocumentToString>& aCtx );

    HANDLER_RESULT<Empty>
    handleRefreshEditor( const HANDLER_CONTEXT<commands::RefreshEditor>& aCtx );

    HANDLER_RESULT<Empty>
    handleSaveCopyOfDocument( const HANDLER_CONTEXT<commands::SaveCopyOfDocument>& aCtx );

    HANDLER_RESULT<Empty>
    handleRevertDocument( const HANDLER_CONTEXT<commands::RevertDocument>& aCtx );

    HANDLER_RESULT<commands::SavedSelectionResponse>
    handleSaveSelectionToString( const HANDLER_CONTEXT<commands::SaveSelectionToString>& aCtx );

    HANDLER_RESULT<commands::CreateItemsResponse>
    handleParseAndCreateItemsFromString( const HANDLER_CONTEXT<commands::ParseAndCreateItemsFromString>& aCtx );

    // Helper to get item by KIID (searches all items including nested)
    std::optional<SCH_ITEM*> getItemById( const KIID& aId );

    // Helper to build sheet hierarchy node
    void buildSheetHierarchyNode( const SCH_SHEET_PATH& aPath,
                                  schematic::commands::SheetHierarchyNode* aNode );

    // Helper to find sheet by KIID
    SCH_SHEET* findSheetById( const KIID& aId );

    SCH_EDIT_FRAME* m_frame;
};


#endif //KICAD_API_HANDLER_SCH_H
