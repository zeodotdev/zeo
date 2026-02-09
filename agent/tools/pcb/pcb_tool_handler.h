#ifndef PCB_TOOL_HANDLER_H
#define PCB_TOOL_HANDLER_H

#include "../tool_handler.h"
#include <string>

/**
 * Tool handler for PCB file operations (pcb_* tools).
 * Implements direct reading, modification, and writing of .kicad_pcb files.
 *
 * NOTE: This is a stub implementation for future development.
 */
class PCB_TOOL_HANDLER : public TOOL_HANDLER
{
public:
    PCB_TOOL_HANDLER() = default;
    ~PCB_TOOL_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;
};

#endif // PCB_TOOL_HANDLER_H
