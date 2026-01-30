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

#ifndef MOCK_LLM_CLIENT_H
#define MOCK_LLM_CLIENT_H

#include <agent_llm_client.h>
#include <string>
#include <vector>
#include <functional>

/**
 * Mock LLM client for testing CHAT_CONTROLLER.
 * Allows setting up canned responses and recording method calls.
 */
class MOCK_LLM_CLIENT : public AGENT_LLM_CLIENT
{
public:
    MOCK_LLM_CLIENT() : AGENT_LLM_CLIENT( nullptr )
    {
        m_asyncStarted = false;
        m_requestInProgress = false;
        m_cancelled = false;
    }

    void SetModel( const std::string& aModelName ) override
    {
        m_lastModelSet = aModelName;
    }

    bool AskStreamWithToolsAsync( const nlohmann::json& aMessages,
                                   const std::vector<LLM_TOOL>& aTools,
                                   wxEvtHandler* aHandler ) override
    {
        m_asyncStarted = true;
        m_lastMessages = aMessages;
        m_lastTools = aTools;
        m_lastHandler = aHandler;
        m_requestInProgress = true;
        return m_shouldStartSucceed;
    }

    void CancelRequest() override
    {
        m_cancelled = true;
        m_requestInProgress = false;
    }

    bool IsRequestInProgress() const override
    {
        return m_requestInProgress;
    }

    // Test control methods
    void SetShouldStartSucceed( bool aSucceed ) { m_shouldStartSucceed = aSucceed; }
    void SimulateRequestComplete() { m_requestInProgress = false; }

    // Query methods for assertions
    bool WasAsyncStarted() const { return m_asyncStarted; }
    bool WasCancelled() const { return m_cancelled; }
    const std::string& GetLastModelSet() const { return m_lastModelSet; }
    const nlohmann::json& GetLastMessages() const { return m_lastMessages; }
    const std::vector<LLM_TOOL>& GetLastTools() const { return m_lastTools; }
    wxEvtHandler* GetLastHandler() const { return m_lastHandler; }

    void Reset()
    {
        m_asyncStarted = false;
        m_requestInProgress = false;
        m_cancelled = false;
        m_lastModelSet.clear();
        m_lastMessages = nlohmann::json::array();
        m_lastTools.clear();
        m_lastHandler = nullptr;
    }

private:
    bool m_asyncStarted;
    bool m_requestInProgress;
    bool m_cancelled;
    bool m_shouldStartSucceed = true;

    std::string m_lastModelSet;
    nlohmann::json m_lastMessages;
    std::vector<LLM_TOOL> m_lastTools;
    wxEvtHandler* m_lastHandler = nullptr;
};

#endif // MOCK_LLM_CLIENT_H
