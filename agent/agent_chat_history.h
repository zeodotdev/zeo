#ifndef AGENT_CHAT_HISTORY_H
#define AGENT_CHAT_HISTORY_H

#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * Simple JSON-based chat history persistence.
 * Saves conversation history to individual JSON files with metadata.
 */
class AGENT_CHAT_HISTORY
{
public:
    AGENT_CHAT_HISTORY();

    /**
     * Set the conversation ID (used as filename).
     * @param aId Unique identifier (UUID format).
     */
    void SetConversationId( const std::string& aId );

    /**
     * Get the current conversation ID.
     */
    std::string GetConversationId() const { return m_conversationId; }

    /**
     * Set the chat title.
     * @param aTitle Human-readable title for the chat.
     */
    void SetTitle( const std::string& aTitle );

    /**
     * Set the project path associated with this conversation.
     * @param aPath Absolute path to the project directory.
     */
    void SetProjectPath( const std::string& aPath ) { m_projectPath = aPath; }

    /**
     * Get the current chat title.
     */
    std::string GetTitle() const { return m_title; }
    std::string GetCreatedAt() const { return m_createdAt; }
    std::string GetLastUpdated() const { return m_lastUpdated; }
    std::string GetProjectPath() const { return m_projectPath; }

    /**
     * Save the current chat history to disk.
     * @param aChatHistory The full conversation history as JSON array.
     */
    void Save( const nlohmann::json& aChatHistory );

    /**
     * Load a conversation from disk.
     * @param aConversationId The conversation ID to load.
     * @return The chat history messages, or empty array if not found.
     */
    nlohmann::json Load( const std::string& aConversationId );

    struct HistoryEntry
    {
        std::string id;
        std::string title;
        std::string createdAt;
        std::string lastUpdated;
        std::string projectPath;
    };

    /**
     * Get a list of all saved conversations, optionally filtered by project path.
     * @param aProjectPath If non-empty, only return conversations for this project.
     * @return Vector of history entries, sorted by lastUpdated descending (newest first).
     */
    std::vector<HistoryEntry> GetHistoryList( const std::string& aProjectPath = "" );

    /**
     * Start a new conversation with a fresh UUID.
     */
    void StartNewConversation();

private:
    wxString    GetHistoryDir();
    wxString    GetFilePath();
    std::string GenerateUUID();
    std::string GetCurrentTimestamp();

    std::string m_conversationId;
    std::string m_title;
    std::string m_projectPath;
    std::string m_createdAt;
    std::string m_lastUpdated;
};

#endif // AGENT_CHAT_HISTORY_H
