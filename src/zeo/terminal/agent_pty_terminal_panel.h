#ifndef AGENT_PTY_TERMINAL_PANEL_H
#define AGENT_PTY_TERMINAL_PANEL_H

#include "pty_terminal_panel.h"

class AGENT_PTY_TERMINAL_PANEL : public PTY_TERMINAL_PANEL
{
public:
    AGENT_PTY_TERMINAL_PANEL( wxWindow* aParent );

    wxString GetTitle() const override;
};

#endif // AGENT_PTY_TERMINAL_PANEL_H
