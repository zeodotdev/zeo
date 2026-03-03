#ifndef LLM_TOOL_H
#define LLM_TOOL_H

#include <string>
#include <nlohmann/json.hpp>

/**
 * Tool group for editor-state filtering.
 */
enum class ToolGroup
{
    GENERAL,    ///< Always included regardless of editor state
    SCHEMATIC,  ///< Included when schematic editor is open
    PCB         ///< Included when PCB editor is open
};

/**
 * Tool definition for native tool calling.
 */
struct LLM_TOOL
{
    std::string    name;
    std::string    description;
    nlohmann::json input_schema;
    bool           read_only = false;      ///< true = safe for plan mode (no modifications)
    ToolGroup      group = ToolGroup::GENERAL;
    bool           defer_loading = false;  ///< true = excluded from prompt, discoverable via tool search
};

#endif // LLM_TOOL_H
