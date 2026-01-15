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
