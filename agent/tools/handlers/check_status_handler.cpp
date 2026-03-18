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

    if( !projectPath.empty() && !projectName.empty() )
    {
        status["schematic_file"] = projectPath + projectName + ".kicad_sch";
        status["pcb_file"] = projectPath + projectName + ".kicad_pcb";
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
