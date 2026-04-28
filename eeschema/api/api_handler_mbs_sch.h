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
};

#endif // KICAD_API_HANDLER_MBS_SCH_H
