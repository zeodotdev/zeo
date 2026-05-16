/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "check_status_handler.h"
#include "tools/tool_registry.h"
#include <frame_type.h>
#include <nlohmann/json.hpp>
#include <wx/log.h>


std::string CHECK_STATUS_HANDLER::BuildStatusJson()
{
    nlohmann::json status;

    const auto& reg = TOOL_REGISTRY::Instance();

    const auto& projectPath = reg.GetProjectPath();
    const auto& projectName = reg.GetProjectName();

    status["project_path"] = projectPath;

    bool schOpen = reg.IsSchematicEditorOpen();
    bool pcbOpen = reg.IsPcbEditorOpen();
    status["schematic_editor_open"] = schOpen;
    status["pcb_editor_open"] = pcbOpen;

    // Multi-board container metadata. Populated by AGENT_FRAME on every
    // check_status path. When non-empty, the project is a multi-board
    // container — sub_projects[] enumerates the children, and the
    // singular schematic_file/pcb_file fields below are misleading and
    // intentionally omitted in favor of the per-sub-project fields.
    const std::string& mbContainer = reg.GetMultiBoardContainerJson();
    bool isMultiBoard = false;

    if( !mbContainer.empty() )
    {
        auto cj = nlohmann::json::parse( mbContainer, nullptr, false );

        if( !cj.is_discarded() )
        {
            status["is_multi_board_container"] = true;
            status["container"] = cj;
            isMultiBoard = true;
        }
    }

    if( !isMultiBoard )
    {
        status["is_multi_board_container"] = false;

        if( !projectPath.empty() && !projectName.empty() )
        {
            status["schematic_file"] = projectPath + projectName + ".kicad_sch";
            status["pcb_file"] = projectPath + projectName + ".kicad_pcb";
        }
    }

    // Rich open-editor list (frame_type, file_path, sub_project_uuid, ...)
    // — preferred over the legacy open_editor_files string list which is
    // kept for backwards compat with other consumers.
    const std::string& openEditorsJson = reg.GetOpenEditorsJson();

    if( !openEditorsJson.empty() )
    {
        auto oej = nlohmann::json::parse( openEditorsJson, nullptr, false );

        if( !oej.is_discarded() )
            status["open_editors"] = oej;
    }

    const auto& openFiles = reg.GetOpenEditorFiles();
    if( !openFiles.empty() )
    {
        nlohmann::json arr = nlohmann::json::array();
        for( const auto& f : openFiles )
            arr.push_back( f );
        status["open_editor_files"] = arr;
    }

    // Query sheet hierarchy from schematic editor if open
    if( schOpen && reg.GetSendRequestFn() )
    {
        try
        {
            std::string resp = reg.GetSendRequestFn()( FRAME_SCH, "get_sch_sheets" );

            if( !resp.empty() )
            {
                auto respJson = nlohmann::json::parse( resp, nullptr, false );

                if( !respJson.is_discarded() && respJson.contains( "sheets" ) )
                {
                    status["schematic_sheets"] = respJson["sheets"];

                    wxLogInfo( "CHECK_STATUS_HANDLER: Got %zu sheets from hierarchy",
                               respJson["sheets"].size() );
                }
            }
        }
        catch( const std::exception& e )
        {
            wxLogInfo( "CHECK_STATUS_HANDLER: Failed to get sheet hierarchy: %s", e.what() );
        }
    }

    wxLogInfo( "CHECK_STATUS_HANDLER: project='%s', sch=%s, pcb=%s, open_files=%zu",
               projectPath, schOpen ? "open" : "closed", pcbOpen ? "open" : "closed",
               openFiles.size() );

    return status.dump( 2 );
}


std::string CHECK_STATUS_HANDLER::Execute( const std::string& aToolName,
                                            const nlohmann::json& aInput )
{
    return BuildStatusJson();
}


std::string CHECK_STATUS_HANDLER::GetDescription( const std::string& aToolName,
                                                    const nlohmann::json& aInput ) const
{
    return "Check Project Status";
}
