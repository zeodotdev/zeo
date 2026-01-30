/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef MOCK_CHAT_HISTORY_H
#define MOCK_CHAT_HISTORY_H

#include <agent_chat_history.h>
#include <string>
#include <map>

/**
 * Mock chat history database for testing CHAT_CONTROLLER.
 * Stores chats in memory and records method calls.
 */
class MOCK_CHAT_HISTORY : public AGENT_CHAT_HISTORY
{
public:
    MOCK_CHAT_HISTORY() : AGENT_CHAT_HISTORY()
    {
        m_loadCalled = false;
        m_saveCalled = false;
    }

    void Save( const nlohmann::json& aChatHistory ) override
    {
        m_saveCalled = true;
        m_lastSavedHistory = aChatHistory;
        m_savedChats[AGENT_CHAT_HISTORY::GetConversationId()] = aChatHistory;
    }

    nlohmann::json Load( const std::string& aConversationId ) override
    {
        m_loadCalled = true;
        m_lastLoadedId = aConversationId;

        auto it = m_savedChats.find( aConversationId );
        if( it != m_savedChats.end() )
        {
            AGENT_CHAT_HISTORY::SetConversationId( aConversationId );
            if( m_titles.find( aConversationId ) != m_titles.end() )
                AGENT_CHAT_HISTORY::SetTitle( m_titles[aConversationId] );
            return it->second;
        }

        return nlohmann::json();  // Return null if not found
    }

    // Test setup methods
    void AddChat( const std::string& aId, const nlohmann::json& aHistory,
                  const std::string& aTitle = "" )
    {
        m_savedChats[aId] = aHistory;
        m_titles[aId] = aTitle;
    }

    // Query methods for assertions
    bool WasLoadCalled() const { return m_loadCalled; }
    bool WasSaveCalled() const { return m_saveCalled; }
    const std::string& GetLastLoadedId() const { return m_lastLoadedId; }
    const nlohmann::json& GetLastSavedHistory() const { return m_lastSavedHistory; }

    void Reset()
    {
        m_loadCalled = false;
        m_saveCalled = false;
        m_lastLoadedId.clear();
        m_lastSavedHistory = nlohmann::json();
        m_savedChats.clear();
        m_titles.clear();
    }

private:
    bool m_loadCalled;
    bool m_saveCalled;
    std::string m_lastLoadedId;
    nlohmann::json m_lastSavedHistory;
    std::map<std::string, nlohmann::json> m_savedChats;
    std::map<std::string, std::string> m_titles;
};

#endif // MOCK_CHAT_HISTORY_H
