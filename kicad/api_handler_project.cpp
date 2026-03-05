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

#include <wx/log.h>
#include <wx/filename.h>

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
    registerHandler<ExecuteTool, ExecuteToolResponse>(
            &API_HANDLER_PROJECT::handleExecuteTool );
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

    const std::string& toolName = aCtx.Request.tool_name();
    const std::string& app = aCtx.Request.app();
    const std::string& script = aCtx.Request.script();

    wxLogMessage( "API_HANDLER_PROJECT: ExecuteTool name=%s, app=%s, script_len=%zu",
                  toolName, app, script.length() );

    if( app.empty() || script.empty() )
    {
        m_toolExecutionPending.store( false );
        response.set_success( false );
        response.set_error_message( "Both 'app' and 'script' fields are required" );
        return response;
    }

    if( app != "sch" && app != "pcb" )
    {
        m_toolExecutionPending.store( false );
        response.set_success( false );
        response.set_error_message( "app must be 'sch' or 'pcb'" );
        return response;
    }

    // Ensure the terminal frame exists
    KIWAY_PLAYER* termPlayer = m_frame->Kiway().Player( FRAME_TERMINAL, true );

    if( !termPlayer )
    {
        m_toolExecutionPending.store( false );
        response.set_success( false );
        response.set_error_message( "Failed to create terminal frame" );
        return response;
    }

    // Build the run_shell command: "run_shell <app> <script>"
    std::string payload = "run_shell " + app + " " + script;

    wxLogMessage( "API_HANDLER_PROJECT: Sending MAIL_MCP_EXECUTE_TOOL to terminal frame" );

    // Use synchronous ExpressMail with MAIL_MCP_EXECUTE_TOOL.
    // ExpressMail uses ProcessEvent() (synchronous), and the payload is passed by
    // reference — the terminal frame's KiwayMailIn handler will execute the command
    // synchronously (with wxYield polling for Python completion) and write the result
    // back into the payload string. When ExpressMail returns, payload contains the result.
    m_frame->Kiway().ExpressMail( FRAME_TERMINAL, MAIL_MCP_EXECUTE_TOOL, payload );

    wxLogMessage( "API_HANDLER_PROJECT: ExecuteTool completed, result_len=%zu", payload.length() );

    m_toolExecutionPending.store( false );

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
