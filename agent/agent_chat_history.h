/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AGENT_CHAT_HISTORY_H
#define AGENT_CHAT_HISTORY_H

#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * Simple JSON-based chat history persistence.
 * Saves conversation history to individual JSON files.
 */
class AGENT_CHAT_HISTORY
{
public:
    AGENT_CHAT_HISTORY();

    /**
     * Set the conversation ID (used as filename).
     * @param aId Unique identifier, typically a timestamp.
     */
    void SetConversationId( const std::string& aId );

    /**
     * Save the current chat history to disk.
     * @param aChatHistory The full conversation history as JSON array.
     */
    void Save( const nlohmann::json& aChatHistory );

    /**
     * Load a conversation from disk.
     * @param aConversationId The conversation ID to load.
     * @return The chat history, or empty array if not found.
     */
    nlohmann::json Load( const std::string& aConversationId );

    struct HistoryEntry
    {
        std::string id;
        std::string displayName;
    };

    /**
     * Get a list of all saved conversations.
     * @return Vector of history entries, sorted by ID descending (newest first).
     */
    std::vector<HistoryEntry> GetHistoryList();

    /**
     * Start a new conversation with a fresh timestamp-based ID.
     */
    void StartNewConversation();

private:
    wxString GetHistoryDir();
    wxString GetFilePath();

    std::string m_conversationId;
};

#endif // AGENT_CHAT_HISTORY_H
