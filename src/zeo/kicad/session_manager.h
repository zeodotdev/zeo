/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <wx/string.h>
#include <wx/event.h>
#include <memory>
#include <zeo/agent_auth.h>

#ifndef __WXMAC__
class AUTH_CALLBACK_SERVER;
#endif

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
     * Handles deep link callback specifically for VCS OAuth (GitHub/GitLab).
     */
    void HandleVcsCallback( const wxString& aUrl );

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

#ifndef __WXMAC__
    void OnAuthCallback( wxCommandEvent& aEvent );
    bool m_authCallbackBound = false;
#endif
};

#endif // SESSION_MANAGER_H
