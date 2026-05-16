/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef ZEO_API_HANDLER_PROJECT_H
#define ZEO_API_HANDLER_PROJECT_H

#include <atomic>

#include <api/api_handler.h>
#include <api/common/commands/project_commands.pb.h>

class KICAD_MANAGER_FRAME;

using namespace kiapi::common;


/**
 * API handler for project-level commands that require access to the project
 * manager's KIWAY. Runs on the project manager's API server.
 *
 * Provides:
 * - LaunchEditor: Open schematic/PCB editors in non-standalone mode via KIWAY::Player()
 * - ExecuteTool: Run MCP tool scripts via the embedded Python executor with
 *   proper timeouts, cancellation, and undo transaction management
 */
class API_HANDLER_PROJECT : public API_HANDLER
{
public:
    API_HANDLER_PROJECT( KICAD_MANAGER_FRAME* aFrame );

    ~API_HANDLER_PROJECT() override {}

private:
    HANDLER_RESULT<commands::GetInstructionsResponse> handleGetInstructions(
            const HANDLER_CONTEXT<commands::GetInstructions>& aCtx );

    HANDLER_RESULT<commands::GetToolSchemasResponse> handleGetToolSchemas(
            const HANDLER_CONTEXT<commands::GetToolSchemas>& aCtx );

    HANDLER_RESULT<commands::LaunchEditorResponse> handleLaunchEditor(
            const HANDLER_CONTEXT<commands::LaunchEditor>& aCtx );

    HANDLER_RESULT<commands::ExecuteToolResponse> handleExecuteTool(
            const HANDLER_CONTEXT<commands::ExecuteTool>& aCtx );

    KICAD_MANAGER_FRAME* m_frame;

    /// Cached core instructions markdown (loaded once from disk)
    std::string m_instructionsCache;

    /// Cached tool manifest JSON (loaded once from disk on first GetToolSchemas call)
    std::string m_manifestCache;

    /// Guard against concurrent tool execution (only one at a time)
    std::atomic<bool> m_toolExecutionPending{ false };
};

#endif // ZEO_API_HANDLER_PROJECT_H
