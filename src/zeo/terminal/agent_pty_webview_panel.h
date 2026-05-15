#ifndef AGENT_PTY_WEBVIEW_PANEL_H
#define AGENT_PTY_WEBVIEW_PANEL_H

#include "pty_webview_panel.h"

class AGENT_PTY_WEBVIEW_PANEL : public PTY_WEBVIEW_PANEL
{
public:
    AGENT_PTY_WEBVIEW_PANEL( wxWindow* aParent );

    wxString GetTitle() const override;
};

#endif // AGENT_PTY_WEBVIEW_PANEL_H
