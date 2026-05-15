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
