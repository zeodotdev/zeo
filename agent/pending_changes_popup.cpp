/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "pending_changes_popup.h"
#include "agent_frame.h"
#include <kiway.h>
#include <kiway_player.h>
#include <mail_type.h>

PENDING_CHANGES_PANEL::PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_agentFrame( aAgentFrame ),
        m_hasSchChanges( false ),
        m_hasPcbChanges( false )
{
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

    m_mainSizer = new wxBoxSizer( wxVERTICAL );

    // === Schematic row ===
    m_schRowPanel = new wxPanel( this, wxID_ANY );
    m_schRowPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    wxBoxSizer* schRowSizer = new wxBoxSizer( wxHORIZONTAL );

    m_schLabel = new wxStaticText( m_schRowPanel, wxID_ANY, "schematic.kicad_sch" );
    m_schLabel->SetForegroundColour( wxColour( "#d4d4d4" ) );
    schRowSizer->Add( m_schLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

    m_viewSchBtn = new wxButton( m_schRowPanel, wxID_ANY, "View" );
    schRowSizer->Add( m_viewSchBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_acceptSchBtn = new wxButton( m_schRowPanel, wxID_ANY, "Accept" );
    m_acceptSchBtn->SetForegroundColour( wxColour( "#44dd44" ) );
    schRowSizer->Add( m_acceptSchBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_rejectSchBtn = new wxButton( m_schRowPanel, wxID_ANY, "Reject" );
    m_rejectSchBtn->SetForegroundColour( wxColour( "#dd4444" ) );
    schRowSizer->Add( m_rejectSchBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    m_schRowPanel->SetSizer( schRowSizer );
    m_mainSizer->Add( m_schRowPanel, 0, wxEXPAND | wxTOP | wxBOTTOM, 5 );

    // === PCB row ===
    m_pcbRowPanel = new wxPanel( this, wxID_ANY );
    m_pcbRowPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    wxBoxSizer* pcbRowSizer = new wxBoxSizer( wxHORIZONTAL );

    m_pcbLabel = new wxStaticText( m_pcbRowPanel, wxID_ANY, "board.kicad_pcb" );
    m_pcbLabel->SetForegroundColour( wxColour( "#d4d4d4" ) );
    pcbRowSizer->Add( m_pcbLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

    m_viewPcbBtn = new wxButton( m_pcbRowPanel, wxID_ANY, "View" );
    pcbRowSizer->Add( m_viewPcbBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_acceptPcbBtn = new wxButton( m_pcbRowPanel, wxID_ANY, "Accept" );
    m_acceptPcbBtn->SetForegroundColour( wxColour( "#44dd44" ) );
    pcbRowSizer->Add( m_acceptPcbBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_rejectPcbBtn = new wxButton( m_pcbRowPanel, wxID_ANY, "Reject" );
    m_rejectPcbBtn->SetForegroundColour( wxColour( "#dd4444" ) );
    pcbRowSizer->Add( m_rejectPcbBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    m_pcbRowPanel->SetSizer( pcbRowSizer );
    m_mainSizer->Add( m_pcbRowPanel, 0, wxEXPAND | wxBOTTOM, 5 );

    SetSizer( m_mainSizer );

    // Bind events
    m_viewSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnViewSch, this );
    m_acceptSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnAcceptSch, this );
    m_rejectSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnRejectSch, this );
    m_viewPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnViewPcb, this );
    m_acceptPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnAcceptPcb, this );
    m_rejectPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnRejectPcb, this );

    // Initially hidden
    Hide();
}


void PENDING_CHANGES_PANEL::UpdateChanges( bool aHasSchChanges, bool aHasPcbChanges,
                                            const wxString& aSchFilename,
                                            const wxString& aPcbFilename )
{
    m_hasSchChanges = aHasSchChanges;
    m_hasPcbChanges = aHasPcbChanges;

    if( !aSchFilename.IsEmpty() )
        m_schLabel->SetLabel( aSchFilename );

    if( !aPcbFilename.IsEmpty() )
        m_pcbLabel->SetLabel( aPcbFilename );

    updateLayout();
}


void PENDING_CHANGES_PANEL::updateLayout()
{
    // Show/hide rows based on pending changes
    m_schRowPanel->Show( m_hasSchChanges );
    m_pcbRowPanel->Show( m_hasPcbChanges );

    Layout();
    GetParent()->Layout();
}


void PENDING_CHANGES_PANEL::OnViewSch( wxCommandEvent& event )
{
    // Activate schematic editor and zoom to changes
    m_agentFrame->Kiway().Player( FRAME_SCH, true );
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_VIEW_CHANGES, payload );
}


void PENDING_CHANGES_PANEL::OnAcceptSch( wxCommandEvent& event )
{
    // Send approve message to schematic editor
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_APPROVE, payload );

    m_hasSchChanges = false;
    m_agentFrame->OnSchematicChangeHandled( true );
}


void PENDING_CHANGES_PANEL::OnRejectSch( wxCommandEvent& event )
{
    // Send reject message to schematic editor
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REJECT, payload );

    m_hasSchChanges = false;
    m_agentFrame->OnSchematicChangeHandled( false );
}


void PENDING_CHANGES_PANEL::OnViewPcb( wxCommandEvent& event )
{
    // Activate PCB editor and zoom to changes
    m_agentFrame->Kiway().Player( FRAME_PCB_EDITOR, true );
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_VIEW_CHANGES, payload );
}


void PENDING_CHANGES_PANEL::OnAcceptPcb( wxCommandEvent& event )
{
    // Send approve message to PCB editor
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_APPROVE, payload );

    m_hasPcbChanges = false;
    m_agentFrame->OnPcbChangeHandled( true );
}


void PENDING_CHANGES_PANEL::OnRejectPcb( wxCommandEvent& event )
{
    // Send reject message to PCB editor
    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REJECT, payload );

    m_hasPcbChanges = false;
    m_agentFrame->OnPcbChangeHandled( false );
}
