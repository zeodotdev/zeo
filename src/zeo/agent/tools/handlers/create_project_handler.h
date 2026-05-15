#ifndef CREATE_PROJECT_HANDLER_H
#define CREATE_PROJECT_HANDLER_H

#include "tools/tool_handler.h"

class CREATE_PROJECT_HANDLER : public TOOL_HANDLER
{
public:
    std::vector<std::string> GetToolNames() const override { return { "create_project" }; }
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;
};

#endif // CREATE_PROJECT_HANDLER_H
