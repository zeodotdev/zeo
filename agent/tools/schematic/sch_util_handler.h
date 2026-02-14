#ifndef SCH_UTIL_HANDLER_H
#define SCH_UTIL_HANDLER_H

#include "../tool_handler.h"
#include <nlohmann/json.hpp>
#include <string>

/**
 * Tool handler for schematic utility operations via kipy IPC.
 * Handles: sch_annotate
 *
 * These tools work on the LIVE schematic through the kipy Python API.
 * Requires KiCad's schematic editor to be open with a document loaded.
 */
class SCH_UTIL_HANDLER : public TOOL_HANDLER
{
public:
    SCH_UTIL_HANDLER() = default;
    ~SCH_UTIL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    // IPC-based execution
    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    /**
     * Generate Python code for sch_annotate operation.
     */
    std::string GenerateAnnotateCode( const nlohmann::json& aInput ) const;

};

#endif // SCH_UTIL_HANDLER_H
