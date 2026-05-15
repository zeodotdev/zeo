#ifndef AUTH_CALLBACK_SERVER_H
#define AUTH_CALLBACK_SERVER_H

#ifndef __WXMAC__

#include <wx/event.h>
#include <wx/socket.h>
#include <wx/timer.h>

#include <memory>
#include <string>

wxDECLARE_EVENT( EVT_AUTH_CALLBACK, wxCommandEvent );

/**
 * Local HTTP server for OAuth callbacks on Linux.
 *
 * Listens on 127.0.0.1 with an OS-assigned port. The auth web page redirects
 * the browser to http://127.0.0.1:PORT/callback/SOURCE?access_token=...
 * after authentication. The server reads the request, sends a success page,
 * and fires EVT_AUTH_CALLBACK with the full callback URL.
 *
 * Modeled on REMOTE_LOGIN_SERVER (common/remote_login_server.cpp).
 */
class AUTH_CALLBACK_SERVER : public wxEvtHandler
{
public:
    AUTH_CALLBACK_SERVER( wxEvtHandler* aOwner );
    ~AUTH_CALLBACK_SERVER() override;

    /**
     * Start listening on 127.0.0.1 with an OS-assigned port.
     * @return true if the server started successfully
     */
    bool Start();

    /**
     * @return the port the server is listening on (0 if not started)
     */
    unsigned short GetPort() const { return m_port; }

    /**
     * Build the full callback URL for the OAuth redirect_uri parameter.
     * @param aSource e.g. "agent" — appended as /callback/SOURCE
     * @return e.g. "http://127.0.0.1:12345/callback/agent"
     */
    std::string GetCallbackUrl( const std::string& aSource = "" ) const;

private:
    void OnSocketEvent( wxSocketEvent& aEvent );
    void OnTimeout( wxTimerEvent& aEvent );
    void HandleClient( wxSocketBase* aClient );
    void SendSuccessResponse( wxSocketBase* aClient );
    void Finish( const wxString& aCallbackUrl );
    void Shutdown();

    wxEvtHandler*                   m_owner;
    std::unique_ptr<wxSocketServer> m_server;
    wxTimer                         m_timeout;
    unsigned short                  m_port;
    bool                            m_done;
};

#endif // !__WXMAC__

#endif // AUTH_CALLBACK_SERVER_H
