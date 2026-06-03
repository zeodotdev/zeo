/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef TOOL_SCHEMAS_H
#define TOOL_SCHEMAS_H

#include <vector>
#include "llm_tool.h"

namespace ToolSchemas
{
    /**
     * Get the JSON tool definitions (schemas) sent to the LLM.
     */
    std::vector<LLM_TOOL> GetToolDefinitions();
}

#endif // TOOL_SCHEMAS_H
