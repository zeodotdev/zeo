#ifndef TERMINAL_PANEL_AGENT_H
#define TERMINAL_PANEL_AGENT_H

#include "terminal_panel.h"

class AGENT_TERMINAL_PANEL : public TERMINAL_PANEL
{
public:
    AGENT_TERMINAL_PANEL( wxWindow* aParent, TERMINAL_MODE aMode = MODE_SYSTEM );
    virtual ~AGENT_TERMINAL_PANEL();

    wxString GetTitle() const override;

    // No event table needed unless specific overrides require new events
    // DECLARE_EVENT_TABLE() // Keeping it simple for now
};

#endif // TERMINAL_PANEL_AGENT_H
