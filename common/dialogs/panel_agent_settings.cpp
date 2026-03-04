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

#include <dialogs/panel_agent_settings.h>
#include <diff_manager.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>


PANEL_AGENT_SETTINGS::PANEL_AGENT_SETTINGS( wxWindow* aParent ) :
        RESETTABLE_PANEL( aParent )
{
    wxBoxSizer* bPanelSizer = new wxBoxSizer( wxVERTICAL );

    // Section header: "Diff View"
    wxStaticText* diffViewLabel = new wxStaticText( this, wxID_ANY, _( "Diff View" ),
                                                     wxDefaultPosition, wxDefaultSize, 0 );
    diffViewLabel->Wrap( -1 );
    bPanelSizer->Add( diffViewLabel, 0, wxTOP | wxRIGHT | wxLEFT, 13 );

    wxStaticLine* separator = new wxStaticLine( this, wxID_ANY, wxDefaultPosition,
                                                 wxDefaultSize, wxLI_HORIZONTAL );
    bPanelSizer->Add( separator, 0, wxEXPAND | wxTOP | wxBOTTOM, 2 );

    wxBoxSizer* contentBox = new wxBoxSizer( wxVERTICAL );

    wxStaticText* description = new wxStaticText( this, wxID_ANY,
            _( "When enabled, colored overlays are shown on the canvas around items "
               "the agent has added or modified." ),
            wxDefaultPosition, wxDefaultSize, 0 );
    description->Wrap( -1 );
    contentBox->Add( description, 0, wxALL, 5 );

    m_cbEnableDiffView = new wxCheckBox( this, wxID_ANY, _( "Show diff overlays on canvas" ),
                                          wxDefaultPosition, wxDefaultSize, 0 );
    m_cbEnableDiffView->SetToolTip(
            _( "Show colored bounding boxes on the canvas when the agent modifies items. "
               "The pending changes bar in the agent sidebar is not affected." ) );
    contentBox->Add( m_cbEnableDiffView, 0, wxALL, 5 );

    bPanelSizer->Add( contentBox, 0, wxEXPAND | wxALL, 5 );

    this->SetSizer( bPanelSizer );
    this->Layout();
    bPanelSizer->Fit( this );
}


void PANEL_AGENT_SETTINGS::ResetPanel()
{
    m_cbEnableDiffView->SetValue( true );
}


bool PANEL_AGENT_SETTINGS::TransferDataToWindow()
{
    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();
    COMMON_SETTINGS*  settings = mgr.GetCommonSettings();

    m_cbEnableDiffView->SetValue( settings->m_Agent.enable_diff_view );

    return true;
}


bool PANEL_AGENT_SETTINGS::TransferDataFromWindow()
{
    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();
    COMMON_SETTINGS*  settings = mgr.GetCommonSettings();

    bool newValue = m_cbEnableDiffView->GetValue();
    bool oldValue = settings->m_Agent.enable_diff_view;

    settings->m_Agent.enable_diff_view = newValue;

    // Clear any active overlays when disabling diff views
    if( oldValue && !newValue )
        DIFF_MANAGER::GetInstance().ClearDiff();

    return true;
}
