#ifndef PCB_AUTOROUTE_HANDLER_H
#define PCB_AUTOROUTE_HANDLER_H

#include "../tool_handler.h"

/**
 * Handler for PCB autorouting via Freerouting.
 * Exports board to Specctra DSN, runs Freerouting headless, imports routed SES.
 * Runs asynchronously to avoid blocking the UI during routing.
 */
class PCB_AUTOROUTE_HANDLER : public TOOL_HANDLER
{
public:
    bool CanHandle( const std::string& aToolName ) const override;

    std::string Execute( const std::string& aToolName, const nlohmann::json& aInput ) override;

    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

    bool RequiresIPC( const std::string& aToolName ) const override { return false; }

    // Async execution - runs in background thread to avoid blocking UI
    bool IsAsync( const std::string& aToolName ) const override { return true; }

    void ExecuteAsync( const std::string& aToolName, const nlohmann::json& aInput,
                       const std::string& aToolUseId, wxEvtHandler* aEventHandler ) override;

private:
    /**
     * Execute the autoroute workflow:
     * 1. Export DSN via IPC
     * 2. Run Freerouting headless
     * 3. Import SES via IPC
     */
    std::string ExecuteAutoroute( const nlohmann::json& aInput );

    /**
     * Run Freerouting with the given DSN file.
     * @param aDsnPath Input DSN file path
     * @param aSesPath Output SES file path
     * @param aMaxPasses Maximum routing passes
     * @param aTimeoutSec Timeout in seconds
     * @return JSON result with status and statistics
     */
    std::string RunFreerouting( const std::string& aDsnPath, const std::string& aSesPath,
                                int aMaxPasses, int aTimeoutSec );

    /**
     * Run a shell command with timeout and capture output.
     * @param aCommand Command to run
     * @param aStdout Captured stdout
     * @param aStderr Captured stderr
     * @param aTimeoutSec Timeout in seconds
     * @return Exit code, or -1 on error/timeout
     */
    int RunCommand( const std::string& aCommand, std::string& aStdout,
                    std::string& aStderr, int aTimeoutSec = 300 );
};

#endif // PCB_AUTOROUTE_HANDLER_H
