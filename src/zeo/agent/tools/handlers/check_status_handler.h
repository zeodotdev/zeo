#ifndef CHECK_STATUS_HANDLER_H
#define CHECK_STATUS_HANDLER_H

#include "tools/tool_handler.h"

class CHECK_STATUS_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "check_status" }; }
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    /// Build status JSON from current TOOL_REGISTRY state.
    /// Used by both Execute() and the project context injection lambda.
    static std::string BuildStatusJson();
};

#endif // CHECK_STATUS_HANDLER_H
