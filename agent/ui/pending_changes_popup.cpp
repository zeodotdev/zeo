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
#include <nlohmann/json.hpp>
#include <wx/log.h>

PENDING_CHANGES_PANEL::PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_agentFrame( aAgentFrame ),
        m_contentPanel( nullptr ),
        m_contentSizer( nullptr ),
        m_acceptAllBtn( nullptr ),
        m_rejectAllBtn( nullptr ),
        m_hasPcbChanges( false )
{
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

    m_mainSizer = new wxBoxSizer( wxVERTICAL );

    // Header row with title and Accept All / Reject All buttons
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticText* header = new wxStaticText( this, wxID_ANY, "Pending Changes" );
    header->SetForegroundColour( wxColour( "#d4d4d4" ) );
    wxFont boldFont = header->GetFont();
    boldFont.SetWeight( wxFONTWEIGHT_BOLD );
    header->SetFont( boldFont );
    headerSizer->Add( header, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 8 );

    // Accept All button
    m_acceptAllBtn = new wxButton( this, wxID_ANY, "Accept All", wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT );
    m_acceptAllBtn->SetForegroundColour( wxColour( "#00AA00" ) );
    m_acceptAllBtn->SetToolTip( "Accept all pending changes" );
    headerSizer->Add( m_acceptAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    // Reject All button
    m_rejectAllBtn = new wxButton( this, wxID_ANY, "Reject All", wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT );
    m_rejectAllBtn->SetForegroundColour( wxColour( "#AA0000" ) );
    m_rejectAllBtn->SetToolTip( "Reject all pending changes" );
    headerSizer->Add( m_rejectAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );

    m_mainSizer->Add( headerSizer, 0, wxEXPAND | wxTOP, 8 );

    // Bind button events
    m_acceptAllBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onAcceptAll(); } );
    m_rejectAllBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onRejectAll(); } );

    // Content panel (will be rebuilt on refresh)
    m_contentPanel = new wxPanel( this, wxID_ANY );
    m_contentPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    m_contentSizer = new wxBoxSizer( wxVERTICAL );
    m_contentPanel->SetSizer( m_contentSizer );
    m_mainSizer->Add( m_contentPanel, 0, wxEXPAND );

    SetSizer( m_mainSizer );

    // Initially hidden
    Hide();
}


void PENDING_CHANGES_PANEL::Refresh()
{
    wxLogInfo( "PENDING_CHANGES_PANEL::Refresh - querying editors" );

    m_schSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.clear();

    // Query schematic editor for pending changes
    KIWAY_PLAYER* schPlayer = m_agentFrame->Kiway().Player( FRAME_SCH, false );
    if( schPlayer )
    {
        std::string response;
        m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_HAS_CHANGES, response );

        try
        {
            nlohmann::json j = nlohmann::json::parse( response );
            bool hasChanges = j.value( "has_changes", false );

            if( hasChanges )
            {
                // Get affected sheets from tracker
                if( j.contains( "affected_sheets" ) && j["affected_sheets"].is_array() )
                {
                    for( const auto& sheet : j["affected_sheets"] )
                    {
                        if( sheet.is_string() )
                        {
                            wxString sheetPath = wxString::FromUTF8( sheet.get<std::string>() );
                            if( !sheetPath.IsEmpty() )
                            {
                                m_schSheets.insert( sheetPath );
                                wxLogInfo( "PENDING_CHANGES_PANEL: Found sheet with changes: %s",
                                           sheetPath );
                            }
                        }
                    }
                }
            }
        }
        catch( const std::exception& e )
        {
            wxLogInfo( "PENDING_CHANGES_PANEL: Failed to parse schematic response: %s", e.what() );
        }
    }

    // Query PCB editor for pending changes
    KIWAY_PLAYER* pcbPlayer = m_agentFrame->Kiway().Player( FRAME_PCB_EDITOR, false );
    if( pcbPlayer )
    {
        std::string response;
        m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_HAS_CHANGES, response );
        m_hasPcbChanges = ( response == "true" );

        if( m_hasPcbChanges )
        {
            wxLogInfo( "PENDING_CHANGES_PANEL: PCB has changes" );
            m_pcbFilename = "PCB";
        }
    }

    wxLogInfo( "PENDING_CHANGES_PANEL: Found %zu schematic sheets, PCB: %s",
               m_schSheets.size(), m_hasPcbChanges ? "yes" : "no" );

    // Rebuild the UI
    rebuildSheetList();

    // Show/hide based on whether we have any changes
    bool hasAnyChanges = !m_schSheets.empty() || m_hasPcbChanges;
    Show( hasAnyChanges );

    Layout();
    GetParent()->Layout();
}


void PENDING_CHANGES_PANEL::rebuildSheetList()
{
    // Clear existing content
    m_contentSizer->Clear( true );  // true = delete windows

    // Add schematic sheet rows
    for( const wxString& sheetPath : m_schSheets )
    {
        wxPanel* rowPanel = new wxPanel( m_contentPanel, wxID_ANY );
        rowPanel->SetBackgroundColour( wxColour( "#252525" ) );
        wxBoxSizer* rowSizer = new wxBoxSizer( wxHORIZONTAL );

        // Extract display name from sheet path
        wxString displayName = sheetPath;
        if( displayName.IsEmpty() || displayName == "/" )
        {
            displayName = "/ (Root)";
        }
        else
        {
            // Get the last path component
            int lastSlash = displayName.Find( '/', true );  // true = from end
            if( lastSlash != wxNOT_FOUND && lastSlash < (int)displayName.Length() - 1 )
                displayName = displayName.Mid( lastSlash + 1 );
        }

        wxStaticText* label = new wxStaticText( rowPanel, wxID_ANY, displayName );
        label->SetForegroundColour( wxColour( "#d4d4d4" ) );
        label->SetToolTip( sheetPath );  // Full path in tooltip
        rowSizer->Add( label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

        // View button
        wxButton* viewBtn = new wxButton( rowPanel, wxID_ANY, "View" );
        viewBtn->SetToolTip( wxString::Format( "Navigate to %s and show diff overlay", sheetPath ) );
        rowSizer->Add( viewBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

        rowPanel->SetSizer( rowSizer );
        m_contentSizer->Add( rowPanel, 0, wxEXPAND | wxTOP, 2 );

        // Bind button with captured sheet path
        wxString capturedPath = sheetPath;
        viewBtn->Bind( wxEVT_BUTTON, [this, capturedPath]( wxCommandEvent& ) {
            onViewSheet( capturedPath );
        });
    }

    // Add PCB row if there are PCB changes
    if( m_hasPcbChanges )
    {
        wxPanel* rowPanel = new wxPanel( m_contentPanel, wxID_ANY );
        rowPanel->SetBackgroundColour( wxColour( "#252525" ) );
        wxBoxSizer* rowSizer = new wxBoxSizer( wxHORIZONTAL );

        wxStaticText* label = new wxStaticText( rowPanel, wxID_ANY, "PCB" );
        label->SetForegroundColour( wxColour( "#d4d4d4" ) );
        rowSizer->Add( label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

        // View button
        wxButton* viewBtn = new wxButton( rowPanel, wxID_ANY, "View" );
        viewBtn->SetToolTip( "Navigate to PCB editor and show diff overlay" );
        rowSizer->Add( viewBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

        rowPanel->SetSizer( rowSizer );
        m_contentSizer->Add( rowPanel, 0, wxEXPAND | wxTOP, 2 );

        viewBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) {
            onViewPcb();
        });
    }

    m_contentPanel->Layout();
}


void PENDING_CHANGES_PANEL::onViewSheet( const wxString& aSheetPath )
{
    wxLogInfo( "PENDING_CHANGES_PANEL::onViewSheet: %s", aSheetPath );

    // Activate schematic editor and navigate to the specified sheet
    m_agentFrame->Kiway().Player( FRAME_SCH, true );

    // Send view changes with sheet path
    nlohmann::json j;
    j["sheet_path"] = aSheetPath.ToStdString();
    std::string payload = j.dump();
    m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_VIEW_CHANGES, payload );
}


void PENDING_CHANGES_PANEL::onViewPcb()
{
    wxLogInfo( "PENDING_CHANGES_PANEL::onViewPcb" );

    // Activate PCB editor and zoom to changes
    m_agentFrame->Kiway().Player( FRAME_PCB_EDITOR, true );

    std::string payload;
    m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_VIEW_CHANGES, payload );
}


void PENDING_CHANGES_PANEL::onAcceptAll()
{
    wxLogInfo( "PENDING_CHANGES_PANEL::onAcceptAll" );

    // Accept schematic changes (all sheets)
    if( !m_schSheets.empty() )
    {
        KIWAY_PLAYER* schPlayer = m_agentFrame->Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            std::string payload;  // Empty payload means accept all
            m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_APPROVE, payload );
        }
    }

    // Accept PCB changes
    if( m_hasPcbChanges )
    {
        KIWAY_PLAYER* pcbPlayer = m_agentFrame->Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_APPROVE, payload );
        }
    }

    // Refresh the panel (should now be empty)
    Refresh();
}


void PENDING_CHANGES_PANEL::onRejectAll()
{
    wxLogInfo( "PENDING_CHANGES_PANEL::onRejectAll" );

    // Reject schematic changes (all sheets)
    if( !m_schSheets.empty() )
    {
        KIWAY_PLAYER* schPlayer = m_agentFrame->Kiway().Player( FRAME_SCH, false );
        if( schPlayer )
        {
            std::string payload;  // Empty payload means reject all
            m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_AGENT_REJECT, payload );
        }
    }

    // Reject PCB changes
    if( m_hasPcbChanges )
    {
        KIWAY_PLAYER* pcbPlayer = m_agentFrame->Kiway().Player( FRAME_PCB_EDITOR, false );
        if( pcbPlayer )
        {
            std::string payload;
            m_agentFrame->Kiway().ExpressMail( FRAME_PCB_EDITOR, MAIL_AGENT_REJECT, payload );
        }
    }

    // Refresh the panel (should now be empty)
    Refresh();
}
