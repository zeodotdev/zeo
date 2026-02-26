#ifndef NETCLASS_GENERATOR_H
#define NETCLASS_GENERATOR_H

#include "tools/tool_handler.h"
#include "../agent_events.h"
#include <string>
#include <vector>

class AGENT_AUTH;
class wxEvtHandler;

/**
 * Generates net class definitions by calling the /api/llm/netclasses endpoint.
 *
 * Workflow:
 * 1. Gather net names via pcb_get_nets
 * 2. Gather existing net classes, assignments, and component data via pcb_setup
 * 3. POST to /api/llm/netclasses (Sonnet for grouping + deterministic electrical values)
 * 4. Apply resulting net classes and assignments via pcb_setup
 *
 * Runs async since the LLM call can take 10-30s.
 */
class NETCLASS_GENERATOR : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "generate_net_classes" }; }

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool IsAsync( const std::string& aToolName ) const override { return true; }

    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

private:
    /**
     * Full generate workflow (runs on background thread).
     * Returns JSON string with status/error.
     */
    std::string DoGenerate( bool aApply );

    /**
     * Gather net names from pcb_get_nets.
     */
    nlohmann::json GatherNetNames();

    /**
     * Gather existing net classes, assignments, and component data from pcb_setup.
     */
    nlohmann::json GatherSetupData();

    /**
     * Build the design_context JSON with grouped component data.
     */
    nlohmann::json BuildDesignContext( const nlohmann::json& aSetupData );

    /**
     * Call the /api/llm/netclasses endpoint.
     */
    std::string CallNetclassEndpoint( const nlohmann::json& aRequestBody );

    /**
     * Apply net classes via pcb_setup.
     */
    std::string ApplyNetclasses( const nlohmann::json& aResponse );

    /**
     * Try to apply net class assignments via sch_setup (requires schematic editor open).
     * Returns true if assignments were applied successfully.
     */
    bool ApplyAssignments( const nlohmann::json& aAssignments );
};

#endif // NETCLASS_GENERATOR_H
