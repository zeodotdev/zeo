/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
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

    m_cbFollowNavigation = new wxCheckBox( this, wxID_ANY,
            _( "Follow agent sheet navigation" ),
            wxDefaultPosition, wxDefaultSize, 0 );
    m_cbFollowNavigation->SetToolTip(
            _( "When the agent navigates to a different sheet, also change the "
               "editor view to that sheet. When disabled, the agent operates on "
               "sheets in the background without disrupting your view." ) );
    contentBox->Add( m_cbFollowNavigation, 0, wxALL, 5 );

    bPanelSizer->Add( contentBox, 0, wxEXPAND | wxALL, 5 );

    this->SetSizer( bPanelSizer );
    this->Layout();
    bPanelSizer->Fit( this );
}


void PANEL_AGENT_SETTINGS::ResetPanel()
{
    m_cbEnableDiffView->SetValue( true );
    m_cbFollowNavigation->SetValue( false );
}


bool PANEL_AGENT_SETTINGS::TransferDataToWindow()
{
    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();
    COMMON_SETTINGS*  settings = mgr.GetCommonSettings();

    m_cbEnableDiffView->SetValue( settings->m_Agent.enable_diff_view );
    m_cbFollowNavigation->SetValue( settings->m_Agent.follow_agent_navigation );

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

    settings->m_Agent.follow_agent_navigation = m_cbFollowNavigation->GetValue();

    return true;
}
