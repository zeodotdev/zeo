#ifndef AGENT_MONITOR_LOG_H
#define AGENT_MONITOR_LOG_H

#include <string>
#include <fstream>
#include <mutex>

/**
 * Singleton JSONL logger for agent command events.
 * Writes timestamped JSON lines to ~/Library/Logs/Zeo/agent-monitor.jsonl.
 * Used by `zeo monitor` to watch agent shell calls in real-time.
 */
class AGENT_MONITOR_LOG
{
public:
    static AGENT_MONITOR_LOG& Instance();

    void LogCommandStart( const std::string& aType, const std::string& aMode,
                          const std::string& aCmd );

    void LogCommandEnd( const std::string& aType, const std::string& aMode,
                        const std::string& aOutput, bool aSuccess, long aDurationMs );

    void LogError( const std::string& aContext, const std::string& aError );

private:
    AGENT_MONITOR_LOG();
    ~AGENT_MONITOR_LOG();

    AGENT_MONITOR_LOG( const AGENT_MONITOR_LOG& ) = delete;
    AGENT_MONITOR_LOG& operator=( const AGENT_MONITOR_LOG& ) = delete;

    void EnsureOpen();
    void WriteLine( const std::string& aJson );
    void TruncateIfNeeded();
    std::string GetTimestamp();
    std::string EscapeJson( const std::string& aStr );

    std::string   m_logPath;
    std::ofstream m_file;
    std::mutex    m_mutex;
};

#endif // AGENT_MONITOR_LOG_H
