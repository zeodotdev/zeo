/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef PANEL_AGENT_SETTINGS_H
#define PANEL_AGENT_SETTINGS_H

#include <widgets/resettable_panel.h>

class wxCheckBox;

class PANEL_AGENT_SETTINGS : public RESETTABLE_PANEL
{
public:
    PANEL_AGENT_SETTINGS( wxWindow* aParent );

    void ResetPanel() override;

protected:
    bool TransferDataFromWindow() override;
    bool TransferDataToWindow() override;

private:
    wxCheckBox* m_cbEnableDiffView;
    wxCheckBox* m_cbFollowNavigation;
};

#endif // PANEL_AGENT_SETTINGS_H
