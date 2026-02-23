/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <wx/string.h>
#include <wx/event.h>
#include <memory>
#include <zeo/agent_auth.h>

class KICAD_MANAGER_FRAME;

/**
 * Manages user session state and UI updates for the Kicad Manager Frame.
 */
class SESSION_MANAGER
{
public:
    SESSION_MANAGER( KICAD_MANAGER_FRAME* aFrame );
    ~SESSION_MANAGER();

    friend class SESSION_DROPDOWN;

    /**
     * Initializes the auth subsystem and checks for existing session.
     */
    void Initialize();

    /**
     * Handles the click event on the session status button.
     */
    void OnSessionButtonClick( wxMouseEvent& aEvent );

    /**
     * Handles deep link callback from OAuth provider.
     * @return true if handled successfully
     */
    bool HandleDeepLink( const wxString& aUrl );

    /**
     * Takes ownership of the AGENT_AUTH module.
     */
    AGENT_AUTH* GetAuth() const { return m_auth.get(); }

    /**
     * Signs out the current user and notifies the agent frame.
     */
    void SignOut();

private:
    /**
     * Starts the OAuth flow by opening the system browser.
     */
    void StartOAuthFlow();

    /**
     * Updates the UI (Status Bar Button) to reflect current state.
     */
    void UpdateUI();

private:
    KICAD_MANAGER_FRAME*        m_frame;
    std::unique_ptr<AGENT_AUTH> m_auth;
};

#endif // SESSION_MANAGER_H
