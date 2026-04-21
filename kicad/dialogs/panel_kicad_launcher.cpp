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

#include <bitmaps.h>
#include <bitmap_store.h>
#include <kicad_manager_frame.h>
#include <kiplatform/policy.h>
#include <policy_keys.h>
#include <project/project_file.h>
#include <tool/tool_manager.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <widgets/bitmap_button.h>
#include <wx/filename.h>
#include <wx/stattext.h>

#include "panel_kicad_launcher.h"


PANEL_KICAD_LAUNCHER::PANEL_KICAD_LAUNCHER( wxWindow* aParent ) :
        PANEL_KICAD_LAUNCHER_BASE( aParent ),
        m_frame( static_cast<KICAD_MANAGER_FRAME*>( aParent->GetParent() ) )
{
    CreateLaunchers();

    Bind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


PANEL_KICAD_LAUNCHER::~PANEL_KICAD_LAUNCHER()
{
    m_frame->SetPcmButton( nullptr );

    for( wxWindow* window : m_scrolledWindow->GetChildren() )
    {
        if( dynamic_cast<BITMAP_BUTTON*>( window ) != nullptr )
            window->Unbind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
    }

    Unbind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


void PANEL_KICAD_LAUNCHER::onLauncherButtonClick( wxCommandEvent& aEvent )
{
    // Don't accept clicks processed during wxProgressReporter updating.  In particular, the wxSafeYield()
    // call below will puke.
    if( m_frame->GetToolManager()->GetTool<KICAD_MANAGER_CONTROL>()->InShowPlayer() )
        return;

    // Defocus the button because leaving the large buttons focused after a click looks out of place in
    // the launcher
    m_frame->SetFocus();

    // Gives a slice of time to update the button state (mandatory on GTK, useful on MSW to avoid some
    // cosmetic issues).
    wxSafeYield();

    BITMAP_BUTTON*     button = (BITMAP_BUTTON*) aEvent.GetEventObject();
    const TOOL_ACTION* action = static_cast<const TOOL_ACTION*>( button->GetClientData() );

    if( action == nullptr )
        return;

    OPT_TOOL_EVENT evt = action->MakeEvent();
    evt->SetHasPosition( false );
    m_frame->GetToolManager()->ProcessEvent( *evt );
}


void PANEL_KICAD_LAUNCHER::CreateLaunchers()
{
    m_frame->SetPcmButton( nullptr );

    if( m_toolsSizer->GetEffectiveRowsCount() > 0 )
    {
        m_toolsSizer->Clear( true );
        m_toolsSizer->SetRows( 0 );
    }

    wxFont titleFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
#ifndef __WXGTK__
    titleFont.SetPointSize( titleFont.GetPointSize() + 2 );
#endif
    titleFont.SetWeight( wxFONTWEIGHT_BOLD );

    wxFont helpFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    helpFont.SetStyle( wxFONTSTYLE_ITALIC );

    auto addLauncher =
            [&]( const TOOL_ACTION& aAction, BITMAPS aBitmaps, const wxString& aHelpText, bool enabled = true )
            {
                BITMAP_BUTTON* btn = new BITMAP_BUTTON( m_scrolledWindow, wxID_ANY );
                btn->SetBitmap( KiBitmapBundle( aBitmaps ) );
                btn->SetDisabledBitmap( KiDisabledBitmapBundle( aBitmaps ) );
                btn->SetPadding( 4 );
                btn->SetToolTip( aAction.GetTooltip() );

                m_scrolledWindow->SetFont( titleFont ); // Use font inheritance to avoid extra SetFont call.
                wxStaticText* label = new wxStaticText( m_scrolledWindow, wxID_ANY, aAction.GetFriendlyName() );
                label->SetToolTip( aAction.GetTooltip() );

                m_scrolledWindow->SetFont( helpFont ); // Use font inheritance to avoid extra SetFont call.
                wxStaticText* help = new wxStaticText( m_scrolledWindow, wxID_ANY, aHelpText );

                btn->Bind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
                btn->SetClientData( (void*) &aAction );

                // The bug fix below makes this handler active for the entire window width.  Without any visual
                // feedback that's a bit odd.  Disabling for now.
                // label->Bind( wxEVT_LEFT_UP, handler );

                m_toolsSizer->Add( btn, 1, wxALIGN_CENTER_VERTICAL );

                wxBoxSizer* textSizer = new wxBoxSizer( wxVERTICAL );

                textSizer->Add( label );
                textSizer->Add( help );

                m_toolsSizer->Add( textSizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );

                btn->Enable( enabled );

                if( !enabled )
                {
                    help->Disable();
                    label->Disable();
                }

                return btn;
            };

    // -------- Multi-board header --------
    // When a multi-board project is active, prepend a section that lists
    // each sub-project and lets the user switch between them with one click.
    // The section is hidden for single-board sessions so the classic launcher
    // layout is unchanged.
    PROJECT_FILE* multi = m_frame->GetMultiBoardProject();

    if( multi )
    {
        // Header label: "Multi-Board: <name>"  (spans col 2; col 1 gets a
        // small spacer so the grid stays aligned)
        m_toolsSizer->AddSpacer( 1 );

        m_scrolledWindow->SetFont( titleFont );
        wxStaticText* headerLbl = new wxStaticText(
                m_scrolledWindow, wxID_ANY,
                wxString::Format( _( "Multi-Board: %s" ),
                                  wxFileName( multi->GetFullFilename() ).GetName() ) );
        m_scrolledWindow->SetFont( helpFont );
        wxStaticText* headerHelp = new wxStaticText(
                m_scrolledWindow, wxID_ANY,
                _( "Click a board below to switch the active sub-project." ) );

        wxBoxSizer* headerText = new wxBoxSizer( wxVERTICAL );
        headerText->Add( headerLbl );
        headerText->Add( headerHelp );
        m_toolsSizer->Add( headerText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );

        // One clickable row per sub-project
        wxString activePath = m_frame->GetProjectFileName();

        for( const SUB_PROJECT_INFO& info : multi->GetSubProjects() )
        {
            wxFileName resolved = multi->ResolveSubProjectPath( info );
            bool       isActive = ( resolved.GetFullPath() == activePath );

            BITMAP_BUTTON* btn = new BITMAP_BUTTON( m_scrolledWindow, wxID_ANY );
            btn->SetBitmap( KiBitmapBundle( BITMAPS::project_kicad ) );
            btn->SetDisabledBitmap( KiDisabledBitmapBundle( BITMAPS::project_kicad ) );
            btn->SetPadding( 4 );
            btn->SetToolTip( resolved.GetFullPath() );

            wxString labelText = info.displayName.IsEmpty() ? info.name : info.displayName;

            if( isActive )
                labelText = wxT( "\u25CF  " ) + labelText;  // ● active marker
            else
                labelText = wxT( "\u25CB  " ) + labelText;  // ○ inactive marker

            m_scrolledWindow->SetFont( titleFont );
            wxStaticText* label = new wxStaticText( m_scrolledWindow, wxID_ANY, labelText );
            label->SetToolTip( resolved.GetFullPath() );

            m_scrolledWindow->SetFont( helpFont );
            wxStaticText* help = new wxStaticText( m_scrolledWindow, wxID_ANY,
                                                   info.relativePath );

            KIID uuid = info.uuid;
            btn->Bind( wxEVT_BUTTON,
                       [this, uuid]( wxCommandEvent& )
                       {
                           m_frame->SwitchActiveSubProject( uuid );
                           // Re-render the launcher so the active marker moves
                           CreateLaunchers();
                       } );

            m_toolsSizer->Add( btn, 1, wxALIGN_CENTER_VERTICAL );

            wxBoxSizer* textSizer = new wxBoxSizer( wxVERTICAL );
            textSizer->Add( label );
            textSizer->Add( help );
            m_toolsSizer->Add( textSizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );

            if( isActive )
            {
                // Disable the button for the currently-active board so the
                // user gets visual feedback that there's nothing to do here.
                btn->Disable();
            }
        }

        // "Multi-Board Schematic" — opens the MBS stub in eeschema
        addLauncher( KICAD_MANAGER_ACTIONS::editMultiBoardSchematic,
                     BITMAPS::icon_eeschema,
                     _( "Open the multi-board schematic (module blocks for each sub-project)" ) );

        // "Manage Sub-Boards…" button uses the standard action-button helper
        addLauncher( KICAD_MANAGER_ACTIONS::manageSubBoards,
                     BITMAPS::new_project_from_template,
                     _( "Add, remove, or inspect sub-boards in this multi-board project" ) );

        // Visual divider row: two empty spacers so the grid keeps rolling
        m_toolsSizer->AddSpacer( 1 );
        m_toolsSizer->AddSpacer( 12 );
    }

    // -------- Standard per-project tools --------
    addLauncher( KICAD_MANAGER_ACTIONS::showAiAssistant, BITMAPS::icon_agent_launcher,
                 _( "Edit the project with an AI assistant" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editSchematic, BITMAPS::icon_eeschema,
                 _( "Edit the project schematic" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editSymbols, BITMAPS::icon_libedit,
                 _( "Edit global and/or project schematic symbol libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editPCB, BITMAPS::icon_pcbnew,
                 _( "Edit the project PCB design" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editFootprints, BITMAPS::icon_modedit,
                 _( "Edit global and/or project PCB footprint libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::viewGerbers, BITMAPS::icon_gerbview,
                 _( "Preview Gerber files" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::showVersionControl, BITMAPS::icon_vcs,
                 _( "Manage project version history, commit, push, and pull changes" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::convertImage, BITMAPS::icon_bitmap2component,
                 _( "Convert bitmap images to schematic symbols or PCB footprints" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::showCalculator, BITMAPS::icon_pcbcalculator,
                 _( "Show tools for calculating resistance, current capacity, etc." ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editDrawingSheet, BITMAPS::icon_pagelayout_editor,
                 _( "Edit drawing sheet borders and title blocks for use in schematics and PCB designs" ) );

    BITMAP_BUTTON* bb = addLauncher( KICAD_MANAGER_ACTIONS::showPluginManager, BITMAPS::icon_pcm,
                                     _( "Manage downloadable packages from KiCad and 3rd party repositories" ),
                                     KIPLATFORM::POLICY::GetPolicyBool( POLICY_KEY_PCM )
                                            != KIPLATFORM::POLICY::PBOOL::DISABLED );

    m_frame->SetPcmButton( bb );

    Layout();
}


void PANEL_KICAD_LAUNCHER::onThemeChanged( wxSysColourChangedEvent& aEvent )
{
    GetBitmapStore()->ThemeChanged();
    CreateLaunchers();

    aEvent.Skip();
}
