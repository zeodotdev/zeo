/*
 * This program source code file is part of Zeo, an AI-assisted EDA application
 * based on KiCad.
 *
 * Copyright (C) 2026 Zeo Developers
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include <fstream>
#include <sstream>
#include <unordered_set>

#include <wx/dir.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "api_handler_project.h"
#include "kicad_manager_frame.h"

#include <api/api_server.h>
#include <frame_type.h>
#include <kiway.h>
#include <kiway_player.h>
#include <mail_type.h>
#include <pgm_base.h>

#include <api/common/types/base_types.pb.h>
#include <nlohmann/json.hpp>

using namespace kiapi::common::commands;
using namespace kiapi::common::types;


API_HANDLER_PROJECT::API_HANDLER_PROJECT( KICAD_MANAGER_FRAME* aFrame ) :
        API_HANDLER(),
        m_frame( aFrame )
{
    registerHandler<LaunchEditor, LaunchEditorResponse>(
            &API_HANDLER_PROJECT::handleLaunchEditor );
    registerHandler<GetToolSchemas, GetToolSchemasResponse>(
            &API_HANDLER_PROJECT::handleGetToolSchemas );
    registerHandler<ExecuteTool, ExecuteToolResponse>(
            &API_HANDLER_PROJECT::handleExecuteTool );
}


HANDLER_RESULT<GetToolSchemasResponse> API_HANDLER_PROJECT::handleGetToolSchemas(
        const HANDLER_CONTEXT<GetToolSchemas>& aCtx )
{
    GetToolSchemasResponse response;

    // Read tool_manifest.json directly from disk — no need to go through the terminal frame.
    if( m_manifestCache.empty() )
    {
        // Locate tool_manifest.json using the same path logic as TOOL_SCRIPT_LOADER
        std::string pythonDir;
        const char* envDir = std::getenv( "AGENT_PYTHON_DIR" );

        if( envDir && envDir[0] )
        {
            pythonDir = envDir;
        }
        else
        {
            wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
            wxFileName dir( exePath.GetPath(), "" );
            dir.RemoveLastDir();
            dir.AppendDir( "SharedSupport" );
            dir.AppendDir( "agent" );
            dir.AppendDir( "python" );
            pythonDir = dir.GetPath().ToStdString();
        }

        std::string manifestPath = pythonDir + "/tool_manifest.json";
        std::ifstream file( manifestPath );

        if( !file.is_open() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Tool manifest not found at " + manifestPath );
            return tl::unexpected( e );
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        m_manifestCache = ss.str();

        wxLogMessage( "API_HANDLER_PROJECT: Loaded tool manifest from %s (%zu bytes)",
                      manifestPath, m_manifestCache.size() );
    }

    response.set_manifest_json( m_manifestCache );
    return response;
}


HANDLER_RESULT<LaunchEditorResponse> API_HANDLER_PROJECT::handleLaunchEditor(
        const HANDLER_CONTEXT<LaunchEditor>& aCtx )
{
    LaunchEditorResponse response;

    if( !m_frame->IsProjectActive() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No project is open. Open a project first." );
        return tl::unexpected( e );
    }

    // Map DocumentType to FRAME_T
    FRAME_T frameType;
    wxString filepath;

    switch( aCtx.Request.doc_type() )
    {
    case DocumentType::DOCTYPE_SCHEMATIC:
        frameType = FRAME_SCH;
        filepath = m_frame->SchFileName();
        break;

    case DocumentType::DOCTYPE_PCB:
        frameType = FRAME_PCB_EDITOR;
        filepath = m_frame->PcbFileName();
        break;

    default:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unsupported document type. Use DOCTYPE_SCHEMATIC or DOCTYPE_PCB." );
        return tl::unexpected( e );
    }
    }

    wxLogMessage( "API_HANDLER_PROJECT: LaunchEditor type=%d, file=%s",
                  (int) aCtx.Request.doc_type(), filepath );

    // Check if already open
    KIWAY_PLAYER* player = m_frame->Kiway().Player( frameType, false );

    if( player && player->IsVisible() )
    {
        wxLogMessage( "API_HANDLER_PROJECT: Editor already open, raising" );
        player->Raise();
        response.set_already_open( true );
    }
    else
    {
        // Create the editor via KIWAY — this runs in-process with KFCTL_CPP_PROJECT_SUITE
        try
        {
            player = m_frame->Kiway().Player( frameType, true );
        }
        catch( const IO_ERROR& err )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to create editor: " + err.What().ToStdString() );
            return tl::unexpected( e );
        }

        if( !player )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "Failed to create editor window" );
            return tl::unexpected( e );
        }

        // Open the project file if the editor isn't already showing a document
        if( !player->IsVisible() && !filepath.IsEmpty() )
        {
            std::vector<wxString> fileList{ filepath };

            if( !player->OpenProjectFiles( fileList ) )
            {
                player->Destroy();
                ApiResponseStatus e;
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( "Failed to open project file: " + filepath.ToStdString() );
                return tl::unexpected( e );
            }
        }

        player->Show( true );
        player->Raise();
        response.set_already_open( false );

        wxLogMessage( "API_HANDLER_PROJECT: Editor launched successfully" );
    }

    // Editors launched via KIWAY share the project manager's API server —
    // no separate socket to discover.
    response.set_socket_path( "" );

    return response;
}


HANDLER_RESULT<ExecuteToolResponse> API_HANDLER_PROJECT::handleExecuteTool(
        const HANDLER_CONTEXT<ExecuteTool>& aCtx )
{
    ExecuteToolResponse response;

    // Guard against concurrent execution
    bool expected = false;

    if( !m_toolExecutionPending.compare_exchange_strong( expected, true ) )
    {
        response.set_success( false );
        response.set_error_message( "Another tool is already executing. Please wait." );
        return response;
    }

    // RAII guard: always clear the flag when this scope exits (normal return or exception)
    struct PendingGuard
    {
        std::atomic<bool>& flag;
        ~PendingGuard() { flag.store( false ); }
    } guard{ m_toolExecutionPending };

    const std::string& toolName = aCtx.Request.tool_name();
    const std::string& toolArgsJson = aCtx.Request.tool_args_json();

    wxLogMessage( "API_HANDLER_PROJECT: ExecuteTool name=%s, args_json_len=%zu",
                  toolName, toolArgsJson.length() );

    // Handle open_project directly in the project manager (no frame routing needed)
    if( toolName == "open_project" )
    {
        try
        {
            nlohmann::json args = toolArgsJson.empty() ? nlohmann::json::object()
                                                        : nlohmann::json::parse( toolArgsJson );
            std::string projectPath = args.value( "project_path", "" );

            if( projectPath.empty() )
            {
                response.set_success( false );
                response.set_error_message( "project_path is required" );
                return response;
            }

            // Find the .kicad_pro file
            wxFileName proFile( wxString::FromUTF8( projectPath ) );

            if( proFile.IsDir() || proFile.GetExt().IsEmpty() )
            {
                // If a directory was given, look for a .kicad_pro inside it
                wxString dir = proFile.GetFullPath();
                if( dir.EndsWith( wxFileName::GetPathSeparator() ) )
                    dir.RemoveLast();

                wxString dirName = wxFileName( dir ).GetName();
                wxFileName candidate( dir, dirName, "kicad_pro" );

                if( candidate.FileExists() )
                {
                    proFile = candidate;
                }
                else
                {
                    // Search for any .kicad_pro in the directory
                    wxDir searchDir( dir );
                    wxString found;
                    if( searchDir.IsOpened()
                        && searchDir.GetFirst( &found, "*.kicad_pro", wxDIR_FILES ) )
                    {
                        proFile = wxFileName( dir, found );
                    }
                    else
                    {
                        response.set_success( false );
                        response.set_error_message( "No .kicad_pro file found in: " + dir.ToStdString() );
                        return response;
                    }
                }
            }

            if( !proFile.FileExists() )
            {
                response.set_success( false );
                response.set_error_message( "Project file not found: " + proFile.GetFullPath().ToStdString() );
                return response;
            }

            wxLogMessage( "API_HANDLER_PROJECT: Opening project '%s'", proFile.GetFullPath() );

            bool loaded = m_frame->LoadProject( proFile );

            if( loaded )
            {
                nlohmann::json result;
                result["status"] = "success";
                result["project_path"] = m_frame->Kiway().Prj().GetProjectPath().ToStdString();
                result["project_name"] = m_frame->Kiway().Prj().GetProjectName().ToStdString();
                response.set_success( true );
                response.set_result_json( result.dump( 2 ) );
            }
            else
            {
                response.set_success( false );
                response.set_error_message( "Failed to load project: " + proFile.GetFullPath().ToStdString() );
            }
        }
        catch( const std::exception& e )
        {
            response.set_success( false );
            response.set_error_message( std::string( "open_project failed: " ) + e.what() );
        }

        return response;
    }

    // Route tool execution: agent handler tools go to FRAME_AGENT via TOOL_REGISTRY,
    // Python-scripted tools go to FRAME_TERMINAL for headless Python execution.
    static const std::unordered_set<std::string> AGENT_TOOLS = {
        "check_status",
        "datasheet_query", "extract_datasheet", "generate_symbol",
        "generate_footprint", "sch_import_symbol", "create_project",
        "pcb_autoroute", "generate_net_classes"
    };

    nlohmann::json payloadJson;
    payloadJson["tool_name"] = toolName;
    payloadJson["tool_args_json"] = toolArgsJson;
    std::string payload = payloadJson.dump();

    bool isAgentTool = AGENT_TOOLS.count( toolName ) > 0;
    FRAME_T targetFrame = isAgentTool ? FRAME_AGENT : FRAME_TERMINAL;
    MAIL_T  mailType = isAgentTool ? MAIL_MCP_EXECUTE_AGENT_TOOL : MAIL_MCP_EXECUTE_TOOL;

    // Ensure the target frame exists
    KIWAY_PLAYER* targetPlayer = m_frame->Kiway().Player( targetFrame, true );

    if( !targetPlayer )
    {
        response.set_success( false );
        response.set_error_message( isAgentTool
            ? "Failed to create agent frame for tool execution"
            : "Failed to create terminal frame for tool execution" );
        return response;
    }

    wxLogMessage( "API_HANDLER_PROJECT: Sending %s to %s frame",
                  isAgentTool ? "MAIL_MCP_EXECUTE_AGENT_TOOL" : "MAIL_MCP_EXECUTE_TOOL",
                  isAgentTool ? "AGENT" : "TERMINAL" );

    // ExpressMail uses ProcessEvent() (synchronous) — the target frame executes
    // the tool and writes the result back into the payload string.
    m_frame->Kiway().ExpressMail( targetFrame, mailType, payload );

    wxLogMessage( "API_HANDLER_PROJECT: ExecuteTool completed, result_len=%zu", payload.length() );

    // After create_project succeeds, auto-open the project in the project manager
    if( toolName == "create_project" )
    {
        try
        {
            auto resultJson = nlohmann::json::parse( payload );

            if( resultJson.value( "status", "" ) == "success" )
            {
                std::string projPath = resultJson.value( "project_path", "" );

                if( !projPath.empty() )
                {
                    wxString dir = wxString::FromUTF8( projPath );
                    wxString dirName = wxFileName( dir ).GetName();
                    wxFileName proFile( dir, dirName, "kicad_pro" );

                    if( proFile.FileExists() )
                    {
                        wxLogMessage( "API_HANDLER_PROJECT: Auto-opening created project '%s'",
                                      proFile.GetFullPath() );

                        if( m_frame->LoadProject( proFile ) )
                        {
                            resultJson["project_opened"] = true;
                            payload = resultJson.dump( 2 );
                        }
                    }
                }
            }
        }
        catch( ... )
        {
            // Non-fatal — project was created but auto-open failed
            wxLogWarning( "API_HANDLER_PROJECT: Failed to auto-open created project" );
        }
    }

    // Check if the result indicates an error
    if( payload.find( "Error:" ) == 0 )
    {
        response.set_success( false );
        response.set_error_message( payload );
    }
    else
    {
        response.set_success( true );
        response.set_result_json( payload );
    }

    return response;
}
