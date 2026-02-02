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

#ifndef KICAD_API_HANDLER_PCB_H
#define KICAD_API_HANDLER_PCB_H

#include <google/protobuf/empty.pb.h>

#include <api/api_handler_editor.h>
#include <api/board/board_commands.pb.h>
#include <api/board/board_types.pb.h>
#include <api/common/commands/editor_commands.pb.h>
#include <api/common/commands/project_commands.pb.h>
#include <kiid.h>
#include <properties/property_mgr.h>
#include <reporter.h>

using namespace kiapi;
using namespace kiapi::common;
using namespace kiapi::board::commands;

using google::protobuf::Empty;


class BOARD_COMMIT;
class BOARD_ITEM;
class BOARD_ITEM_CONTAINER;
class EDA_ITEM;
class PCB_EDIT_FRAME;
class PCB_TRACK;
class PROPERTY_BASE;


/**
 * Structure to hold a single update change from the netlist updater
 */
struct API_UPDATE_CHANGE
{
    PCBUpdateChange::ChangeType type;
    wxString                    reference;
    wxString                    message;
    KIID                        itemId;
};


/**
 * Reporter class that captures update changes for the API response
 */
class API_UPDATE_REPORTER : public REPORTER
{
public:
    API_UPDATE_REPORTER() = default;

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override;
    bool      HasMessage() const override { return !m_changes.empty(); }

    int GetAddedCount() const { return m_addedCount; }
    int GetReplacedCount() const { return m_replacedCount; }
    int GetDeletedCount() const { return m_deletedCount; }
    int GetUpdatedCount() const { return m_updatedCount; }
    int GetNetsChangedCount() const { return m_netsChangedCount; }
    int GetWarningCount() const { return m_warningCount; }
    int GetErrorCount() const { return m_errorCount; }

    const std::vector<API_UPDATE_CHANGE>& GetChanges() const { return m_changes; }

private:
    std::vector<API_UPDATE_CHANGE> m_changes;
    int                            m_addedCount = 0;
    int                            m_replacedCount = 0;
    int                            m_deletedCount = 0;
    int                            m_updatedCount = 0;
    int                            m_netsChangedCount = 0;
    int                            m_warningCount = 0;
    int                            m_errorCount = 0;
};


class API_HANDLER_PCB : public API_HANDLER_EDITOR
{
public:
    API_HANDLER_PCB( PCB_EDIT_FRAME* aFrame );

private:
    typedef std::map<std::string, PROPERTY_BASE*> PROTO_PROPERTY_MAP;

    static HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> createItemForType( KICAD_T aType,
                                                          BOARD_ITEM_CONTAINER* aContainer );

    HANDLER_RESULT<commands::RunActionResponse> handleRunAction( const HANDLER_CONTEXT<commands::RunAction>& aCtx );

    HANDLER_RESULT<commands::GetOpenDocumentsResponse> handleGetOpenDocuments(
            const HANDLER_CONTEXT<commands::GetOpenDocuments>& aCtx );

    HANDLER_RESULT<Empty> handleSaveDocument( const HANDLER_CONTEXT<commands::SaveDocument>& aCtx );

    HANDLER_RESULT<Empty> handleSaveCopyOfDocument(
            const HANDLER_CONTEXT<commands::SaveCopyOfDocument>& aCtx );

    HANDLER_RESULT<Empty> handleRevertDocument(
            const HANDLER_CONTEXT<commands::RevertDocument>& aCtx );

    HANDLER_RESULT<commands::GetItemsResponse> handleGetItems(
            const HANDLER_CONTEXT<commands::GetItems>& aCtx );

    HANDLER_RESULT<commands::GetItemsResponse> handleGetItemsById(
            const HANDLER_CONTEXT<commands::GetItemsById>& aCtx );

    HANDLER_RESULT<commands::SelectionResponse> handleGetSelection(
            const HANDLER_CONTEXT<commands::GetSelection>& aCtx );

    HANDLER_RESULT<Empty> handleClearSelection(
            const HANDLER_CONTEXT<commands::ClearSelection>& aCtx );

    HANDLER_RESULT<commands::SelectionResponse> handleAddToSelection(
            const HANDLER_CONTEXT<commands::AddToSelection>& aCtx );

    HANDLER_RESULT<commands::SelectionResponse> handleRemoveFromSelection(
            const HANDLER_CONTEXT<commands::RemoveFromSelection>& aCtx );

    HANDLER_RESULT<BoardStackupResponse> handleGetStackup(
            const HANDLER_CONTEXT<GetBoardStackup>& aCtx );

    HANDLER_RESULT<BoardEnabledLayersResponse> handleGetBoardEnabledLayers(
            const HANDLER_CONTEXT<GetBoardEnabledLayers>& aCtx );

    HANDLER_RESULT<BoardEnabledLayersResponse> handleSetBoardEnabledLayers(
            const HANDLER_CONTEXT<SetBoardEnabledLayers>& aCtx );

    HANDLER_RESULT<GraphicsDefaultsResponse> handleGetGraphicsDefaults(
            const HANDLER_CONTEXT<GetGraphicsDefaults>& aCtx );

    HANDLER_RESULT<types::Vector2> handleGetBoardOrigin(
            const HANDLER_CONTEXT<GetBoardOrigin>& aCtx );

    HANDLER_RESULT<Empty> handleSetBoardOrigin( const HANDLER_CONTEXT<SetBoardOrigin>& aCtx );

    HANDLER_RESULT<commands::GetBoundingBoxResponse> handleGetBoundingBox(
            const HANDLER_CONTEXT<commands::GetBoundingBox>& aCtx );

    HANDLER_RESULT<PadShapeAsPolygonResponse> handleGetPadShapeAsPolygon(
            const HANDLER_CONTEXT<GetPadShapeAsPolygon>& aCtx );

    HANDLER_RESULT<PadstackPresenceResponse> handleCheckPadstackPresenceOnLayers(
            const HANDLER_CONTEXT<CheckPadstackPresenceOnLayers>& aCtx );

    HANDLER_RESULT<types::TitleBlockInfo> handleGetTitleBlockInfo(
            const HANDLER_CONTEXT<commands::GetTitleBlockInfo>& aCtx );

    HANDLER_RESULT<commands::ExpandTextVariablesResponse> handleExpandTextVariables(
            const HANDLER_CONTEXT<commands::ExpandTextVariables>& aCtx );

    HANDLER_RESULT<Empty> handleInteractiveMoveItems( const HANDLER_CONTEXT<InteractiveMoveItems>& aCtx );

    HANDLER_RESULT<Empty> handleShowDiffOverlay( const HANDLER_CONTEXT<ShowDiffOverlay>& aCtx );

    HANDLER_RESULT<NetsResponse> handleGetNets( const HANDLER_CONTEXT<GetNets>& aCtx );

    HANDLER_RESULT<NetClassForNetsResponse> handleGetNetClassForNets(
            const HANDLER_CONTEXT<GetNetClassForNets>& aCtx );

    HANDLER_RESULT<Empty> handleRefillZones( const HANDLER_CONTEXT<RefillZones>& aCtx );

    HANDLER_RESULT<commands::SavedDocumentResponse> handleSaveDocumentToString(
                const HANDLER_CONTEXT<commands::SaveDocumentToString>& aCtx );

    HANDLER_RESULT<commands::SavedSelectionResponse> handleSaveSelectionToString(
                const HANDLER_CONTEXT<commands::SaveSelectionToString>& aCtx );

    HANDLER_RESULT<commands::CreateItemsResponse> handleParseAndCreateItemsFromString(
                const HANDLER_CONTEXT<commands::ParseAndCreateItemsFromString>& aCtx );

    HANDLER_RESULT<BoardLayers> handleGetVisibleLayers( const HANDLER_CONTEXT<GetVisibleLayers>& aCtx );
    HANDLER_RESULT<Empty> handleSetVisibleLayers( const HANDLER_CONTEXT<SetVisibleLayers>& aCtx );

    HANDLER_RESULT<BoardLayerResponse> handleGetActiveLayer( const HANDLER_CONTEXT<GetActiveLayer>& aCtx );
    HANDLER_RESULT<Empty> handleSetActiveLayer( const HANDLER_CONTEXT<SetActiveLayer>& aCtx );

    HANDLER_RESULT<BoardEditorAppearanceSettings> handleGetBoardEditorAppearanceSettings(
            const HANDLER_CONTEXT<GetBoardEditorAppearanceSettings>& aCtx );

    HANDLER_RESULT<Empty> handleSetBoardEditorAppearanceSettings(
            const HANDLER_CONTEXT<SetBoardEditorAppearanceSettings>& aCtx );

    HANDLER_RESULT<InjectDrcErrorResponse> handleInjectDrcError(
            const HANDLER_CONTEXT<InjectDrcError>& aCtx );

    // Board stackup update
    HANDLER_RESULT<BoardStackupResponse> handleUpdateBoardStackup(
            const HANDLER_CONTEXT<UpdateBoardStackup>& aCtx );

    // DRC handlers
    HANDLER_RESULT<RunDRCResponse> handleRunDRC( const HANDLER_CONTEXT<RunDRC>& aCtx );

    HANDLER_RESULT<DRCViolationsResponse> handleGetDRCViolations(
            const HANDLER_CONTEXT<GetDRCViolations>& aCtx );

    HANDLER_RESULT<Empty> handleClearDRCMarkers( const HANDLER_CONTEXT<ClearDRCMarkers>& aCtx );

    // Design rules handlers
    HANDLER_RESULT<DesignRulesResponse> handleGetDesignRules(
            const HANDLER_CONTEXT<GetDesignRules>& aCtx );

    HANDLER_RESULT<DesignRulesResponse> handleSetDesignRules(
            const HANDLER_CONTEXT<SetDesignRules>& aCtx );

    // DRC settings handlers
    HANDLER_RESULT<DRCSettingsResponse> handleGetDRCSettings(
            const HANDLER_CONTEXT<GetDRCSettings>& aCtx );

    HANDLER_RESULT<Empty> handleSetDRCSettings( const HANDLER_CONTEXT<SetDRCSettings>& aCtx );

    // Grid settings handlers
    HANDLER_RESULT<PCBGridSettingsResponse> handleGetPCBGridSettings(
            const HANDLER_CONTEXT<GetPCBGridSettings>& aCtx );

    HANDLER_RESULT<Empty> handleSetPCBGridSettings( const HANDLER_CONTEXT<SetPCBGridSettings>& aCtx );

    // Graphics defaults setter
    HANDLER_RESULT<GraphicsDefaultsResponse> handleSetGraphicsDefaults(
            const HANDLER_CONTEXT<SetGraphicsDefaults>& aCtx );

    // Update PCB from Schematic
    HANDLER_RESULT<UpdatePCBFromSchematicResponse> handleUpdatePCBFromSchematic(
            const HANDLER_CONTEXT<UpdatePCBFromSchematic>& aCtx );

    // Footprint library browsing
    HANDLER_RESULT<GetLibraryFootprintsResponse> handleGetLibraryFootprints(
            const HANDLER_CONTEXT<GetLibraryFootprints>& aCtx );

    HANDLER_RESULT<SearchLibraryFootprintsResponse> handleSearchLibraryFootprints(
            const HANDLER_CONTEXT<SearchLibraryFootprints>& aCtx );

    HANDLER_RESULT<GetFootprintInfoResponse> handleGetFootprintInfo(
            const HANDLER_CONTEXT<GetFootprintInfo>& aCtx );

    // Ratsnest query
    HANDLER_RESULT<GetRatsnestResponse> handleGetRatsnest(
            const HANDLER_CONTEXT<GetRatsnest>& aCtx );

    HANDLER_RESULT<GetUnroutedNetsResponse> handleGetUnroutedNets(
            const HANDLER_CONTEXT<GetUnroutedNets>& aCtx );

    HANDLER_RESULT<GetConnectivityStatusResponse> handleGetConnectivityStatus(
            const HANDLER_CONTEXT<GetConnectivityStatus>& aCtx );

    // Group operations
    HANDLER_RESULT<GetGroupsResponse> handleGetGroups(
            const HANDLER_CONTEXT<GetGroups>& aCtx );

    HANDLER_RESULT<CreateGroupResponse> handleCreateGroup(
            const HANDLER_CONTEXT<CreateGroup>& aCtx );

    HANDLER_RESULT<DeleteGroupResponse> handleDeleteGroup(
            const HANDLER_CONTEXT<DeleteGroup>& aCtx );

    HANDLER_RESULT<AddToGroupResponse> handleAddToGroup(
            const HANDLER_CONTEXT<AddToGroup>& aCtx );

    HANDLER_RESULT<RemoveFromGroupResponse> handleRemoveFromGroup(
            const HANDLER_CONTEXT<RemoveFromGroup>& aCtx );

    HANDLER_RESULT<GetGroupMembersResponse> handleGetGroupMembers(
            const HANDLER_CONTEXT<GetGroupMembers>& aCtx );

    // Document management handlers
    HANDLER_RESULT<commands::CreateDocumentResponse>
    handleCreateDocument( const HANDLER_CONTEXT<commands::CreateDocument>& aCtx );

    HANDLER_RESULT<commands::OpenDocumentResponse>
    handleOpenDocument( const HANDLER_CONTEXT<commands::OpenDocument>& aCtx );

    HANDLER_RESULT<Empty>
    handleCloseDocument( const HANDLER_CONTEXT<commands::CloseDocument>& aCtx );

protected:
    std::unique_ptr<COMMIT> createCommit() override;

    kiapi::common::types::DocumentType thisDocumentType() const override
    {
        return kiapi::common::types::DOCTYPE_PCB;
    }

    bool validateDocumentInternal( const DocumentSpecifier& aDocument ) const override;

    void deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                              const std::string& aClientName ) override;

    std::optional<EDA_ITEM*> getItemFromDocument( const DocumentSpecifier& aDocument, const KIID& aId ) override;

private:
    PCB_EDIT_FRAME* frame() const;

    void pushCurrentCommit( const std::string& aClientName, const wxString& aMessage ) override;

    std::optional<BOARD_ITEM*> getItemById( const KIID& aId ) const;

    HANDLER_RESULT<types::ItemRequestStatus> handleCreateUpdateItemsInternal( bool aCreate,
            const std::string& aClientName,
            const types::ItemHeader &aHeader,
            const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
            std::function<void(commands::ItemStatus, google::protobuf::Any)> aItemHandler )
            override;
};

#endif //KICAD_API_HANDLER_PCB_H
