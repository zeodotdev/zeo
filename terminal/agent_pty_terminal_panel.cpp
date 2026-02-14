#include "agent_pty_terminal_panel.h"


AGENT_PTY_TERMINAL_PANEL::AGENT_PTY_TERMINAL_PANEL( wxWindow* aParent ) :
        PTY_TERMINAL_PANEL( aParent )
{
}


wxString AGENT_PTY_TERMINAL_PANEL::GetTitle() const
{
    return "Agent: Shell";
}
