/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef KICAD_API_HANDLER_MBS_SCH_H
#define KICAD_API_HANDLER_MBS_SCH_H

#include "api_handler_sch.h"

namespace kiapi { namespace schematic { namespace commands {
    class GetModuleBlocks;
    class GetModuleBlocksResponse;
    class GetCrossBoardNets;
    class GetCrossBoardNetsResponse;
    class GetMultiBoardContainerInfo;
    class GetMultiBoardContainerInfoResponse;
    class RefreshMbsFromSubProjects;
    class RefreshMbsFromSubProjectsResponse;
    class SyncCrossBoardNetsToPcb;
    class SyncCrossBoardNetsToPcbResponse;
    class UpdateModulePin;
    class UpdateModulePinResponse;
    class DeleteModulePin;
    class DeleteModulePinResponse;
    class UpdateModuleBlock;
    class UpdateModuleBlockResponse;
    class GetMbsRules;
    class GetMbsRulesResponse;
    class SetMbsRules;
    class SetMbsRulesResponse;
    class GetMultiBoardNetClassReport;
    class GetMultiBoardNetClassReportResponse;
    class GetMultiBoardLibraryReport;
    class GetMultiBoardLibraryReportResponse;
    class SetMultiBoardNetClass;
    class SetMultiBoardNetClassResponse;
    class DeleteMultiBoardNetClass;
    class DeleteMultiBoardNetClassResponse;
    class AddMultiBoardLibrary;
    class AddMultiBoardLibraryResponse;
    class DeleteMultiBoardLibrary;
    class DeleteMultiBoardLibraryResponse;
    class ShareMultiBoardLibrary;
    class ShareMultiBoardLibraryResponse;
} } }


/**
 * API handler for the multi-board container schematic (`.kicad_mbs`).
 *
 * Responds to the `DOCTYPE_MBS_SCHEMATIC` document type so the API server's
 * first-handler-wins dispatch can deterministically route MBS commands to
 * MBSCH frames (and only MBSCH frames) when both a regular schematic and
 * an MBS are open simultaneously.
 *
 * Inherits the full schematic command surface from API_HANDLER_SCH —
 * existing operations (symbols.add, wiring.add_wire, labels, etc.) work
 * identically against an MBS canvas because MBSCH_EDIT_FRAME inherits
 * from SCH_EDIT_FRAME and the on-disk format is the same.
 *
 * Adds MBS-only commands for module blocks, cross-board nets, and
 * container metadata. MBS-specific edit operations (refresh from sub-
 * projects, sync to PCB, etc.) land in later slices.
 */
class API_HANDLER_MBS_SCH : public API_HANDLER_SCH
{
public:
    API_HANDLER_MBS_SCH( SCH_EDIT_FRAME* aFrame );

protected:
    kiapi::common::types::DocumentType thisDocumentType() const override
    {
        return kiapi::common::types::DOCTYPE_MBS_SCHEMATIC;
    }

    bool validateDocumentInternal( const DocumentSpecifier& aDocument ) const override;

private:
    HANDLER_RESULT<commands::GetOpenDocumentsResponse>
    handleGetOpenDocuments( const HANDLER_CONTEXT<commands::GetOpenDocuments>& aCtx );

    HANDLER_RESULT<schematic::commands::GetModuleBlocksResponse>
    handleGetModuleBlocks( const HANDLER_CONTEXT<schematic::commands::GetModuleBlocks>& aCtx );

    HANDLER_RESULT<schematic::commands::GetCrossBoardNetsResponse>
    handleGetCrossBoardNets( const HANDLER_CONTEXT<schematic::commands::GetCrossBoardNets>& aCtx );

    HANDLER_RESULT<schematic::commands::GetMultiBoardContainerInfoResponse>
    handleGetMultiBoardContainerInfo(
            const HANDLER_CONTEXT<schematic::commands::GetMultiBoardContainerInfo>& aCtx );

    HANDLER_RESULT<schematic::commands::RefreshMbsFromSubProjectsResponse>
    handleRefreshMbsFromSubProjects(
            const HANDLER_CONTEXT<schematic::commands::RefreshMbsFromSubProjects>& aCtx );

    HANDLER_RESULT<schematic::commands::SyncCrossBoardNetsToPcbResponse>
    handleSyncCrossBoardNetsToPcb(
            const HANDLER_CONTEXT<schematic::commands::SyncCrossBoardNetsToPcb>& aCtx );

    HANDLER_RESULT<schematic::commands::UpdateModulePinResponse>
    handleUpdateModulePin(
            const HANDLER_CONTEXT<schematic::commands::UpdateModulePin>& aCtx );

    HANDLER_RESULT<schematic::commands::DeleteModulePinResponse>
    handleDeleteModulePin(
            const HANDLER_CONTEXT<schematic::commands::DeleteModulePin>& aCtx );

    HANDLER_RESULT<schematic::commands::UpdateModuleBlockResponse>
    handleUpdateModuleBlock(
            const HANDLER_CONTEXT<schematic::commands::UpdateModuleBlock>& aCtx );

    HANDLER_RESULT<schematic::commands::GetMbsRulesResponse>
    handleGetMbsRules(
            const HANDLER_CONTEXT<schematic::commands::GetMbsRules>& aCtx );

    HANDLER_RESULT<schematic::commands::SetMbsRulesResponse>
    handleSetMbsRules(
            const HANDLER_CONTEXT<schematic::commands::SetMbsRules>& aCtx );

    HANDLER_RESULT<schematic::commands::GetMultiBoardNetClassReportResponse>
    handleGetMultiBoardNetClassReport(
            const HANDLER_CONTEXT<schematic::commands::GetMultiBoardNetClassReport>& aCtx );

    HANDLER_RESULT<schematic::commands::GetMultiBoardLibraryReportResponse>
    handleGetMultiBoardLibraryReport(
            const HANDLER_CONTEXT<schematic::commands::GetMultiBoardLibraryReport>& aCtx );

    HANDLER_RESULT<schematic::commands::SetMultiBoardNetClassResponse>
    handleSetMultiBoardNetClass(
            const HANDLER_CONTEXT<schematic::commands::SetMultiBoardNetClass>& aCtx );

    HANDLER_RESULT<schematic::commands::DeleteMultiBoardNetClassResponse>
    handleDeleteMultiBoardNetClass(
            const HANDLER_CONTEXT<schematic::commands::DeleteMultiBoardNetClass>& aCtx );

    HANDLER_RESULT<schematic::commands::AddMultiBoardLibraryResponse>
    handleAddMultiBoardLibrary(
            const HANDLER_CONTEXT<schematic::commands::AddMultiBoardLibrary>& aCtx );

    HANDLER_RESULT<schematic::commands::DeleteMultiBoardLibraryResponse>
    handleDeleteMultiBoardLibrary(
            const HANDLER_CONTEXT<schematic::commands::DeleteMultiBoardLibrary>& aCtx );

    HANDLER_RESULT<schematic::commands::ShareMultiBoardLibraryResponse>
    handleShareMultiBoardLibrary(
            const HANDLER_CONTEXT<schematic::commands::ShareMultiBoardLibrary>& aCtx );
};

#endif // KICAD_API_HANDLER_MBS_SCH_H
