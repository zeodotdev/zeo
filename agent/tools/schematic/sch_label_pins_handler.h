#ifndef SCH_LABEL_PINS_HANDLER_H
#define SCH_LABEL_PINS_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for batch pin labeling (sch_label_pins).
 * Places labels at pin tips with auto-justified text based on pin orientation.
 * Requires KiCad's schematic editor to be open with a document loaded.
 */
class SCH_LABEL_PINS_HANDLER : public TOOL_HANDLER
{
public:
    SCH_LABEL_PINS_HANDLER() = default;
    ~SCH_LABEL_PINS_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool RequiresIPC( const std::string& aToolName ) const override;
    std::string GetIPCCommand( const std::string& aToolName,
                               const nlohmann::json& aInput ) const override;

private:
    std::string GenerateLabelPinsCode( const nlohmann::json& aInput ) const;

    std::string EscapePythonString( const std::string& aStr ) const;
};

#endif // SCH_LABEL_PINS_HANDLER_H
