/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
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
};

#endif // PANEL_AGENT_SETTINGS_H
