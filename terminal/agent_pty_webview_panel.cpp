#include "agent_pty_webview_panel.h"


AGENT_PTY_WEBVIEW_PANEL::AGENT_PTY_WEBVIEW_PANEL( wxWindow* aParent ) :
        PTY_WEBVIEW_PANEL( aParent )
{
}


wxString AGENT_PTY_WEBVIEW_PANEL::GetTitle() const
{
    return "Agent: Shell";
}
