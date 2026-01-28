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

#include "history_panel.h"
#include "agent_frame.h"
#include "agent_chat_history.h"


HISTORY_PANEL::HISTORY_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_agentFrame( aAgentFrame )
{
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Header row with title and close button (same dark background)
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticText* titleLabel = new wxStaticText( this, wxID_ANY, "Chat History" );
    titleLabel->SetForegroundColour( wxColour( "#888888" ) );
    wxFont titleFont = titleLabel->GetFont();
    titleFont.SetWeight( wxFONTWEIGHT_BOLD );
    titleLabel->SetFont( titleFont );
    headerSizer->Add( titleLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

    m_closeBtn = new wxButton( this, wxID_ANY, "Close" );
    headerSizer->Add( m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    mainSizer->Add( headerSizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );

    // Search bar
    m_searchCtrl = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                   wxTE_PROCESS_ENTER | wxBORDER_NONE );
    m_searchCtrl->SetBackgroundColour( wxColour( "#2d2d2d" ) );
    m_searchCtrl->SetForegroundColour( wxColour( "#ffffff" ) );
    m_searchCtrl->SetHint( "Search chats..." );
    mainSizer->Add( m_searchCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    // Scrollable area for history items
    m_scrollWindow = new wxScrolledWindow( this, wxID_ANY );
    m_scrollWindow->SetBackgroundColour( wxColour( "#1E1E1E" ) );
    m_scrollWindow->SetScrollRate( 0, 10 );

    m_listSizer = new wxBoxSizer( wxVERTICAL );
    m_scrollWindow->SetSizer( m_listSizer );

    mainSizer->Add( m_scrollWindow, 1, wxEXPAND );

    SetSizer( mainSizer );

    // Bind events
    m_closeBtn->Bind( wxEVT_BUTTON, &HISTORY_PANEL::OnCloseClick, this );
    m_searchCtrl->Bind( wxEVT_TEXT, &HISTORY_PANEL::OnSearchText, this );

    // Initially hidden
    Hide();
}


void HISTORY_PANEL::RefreshHistory()
{
    // Clear search and populate full list
    m_searchCtrl->Clear();
    PopulateHistoryList( "" );
}


void HISTORY_PANEL::PopulateHistoryList( const wxString& aFilter )
{
    // Clear existing items
    m_listSizer->Clear( true );
    m_conversationIds.clear();

    // Get history list from the frame's chat history database
    auto historyList = m_agentFrame->GetChatHistoryDb().GetHistoryList();

    wxString filterLower = aFilter.Lower();
    int index = 0;
    int matchCount = 0;

    for( const auto& entry : historyList )
    {
        // Filter by title (case-insensitive)
        if( !filterLower.IsEmpty() )
        {
            wxString titleLower = wxString::FromUTF8( entry.title ).Lower();
            if( titleLower.Find( filterLower ) == wxNOT_FOUND )
            {
                index++;
                continue;
            }
        }

        CreateHistoryItem( m_scrollWindow, m_listSizer,
                           entry.id, entry.title, entry.lastUpdated, index );
        m_conversationIds.push_back( entry.id );
        index++;
        matchCount++;
    }

    if( matchCount == 0 )
    {
        wxString msg = aFilter.IsEmpty() ? "No chat history yet" : "No matching chats";
        wxStaticText* emptyLabel = new wxStaticText( m_scrollWindow, wxID_ANY, msg );
        emptyLabel->SetForegroundColour( wxColour( "#888888" ) );
        m_listSizer->Add( emptyLabel, 0, wxALL | wxALIGN_CENTER, 20 );
    }

    m_scrollWindow->FitInside();
    Layout();
}


void HISTORY_PANEL::OnSearchText( wxCommandEvent& aEvent )
{
    PopulateHistoryList( m_searchCtrl->GetValue() );
}


void HISTORY_PANEL::CreateHistoryItem( wxWindow* aParent, wxSizer* aSizer,
                                        const std::string& aId, const std::string& aTitle,
                                        const std::string& aLastUpdated, int aIndex )
{
    wxPanel* itemPanel = new wxPanel( aParent, wxID_ANY );
    itemPanel->SetBackgroundColour( wxColour( "#1E1E1E" ) );

    wxBoxSizer* itemSizer = new wxBoxSizer( wxHORIZONTAL );

    // Title
    wxString displayTitle = wxString::FromUTF8( aTitle );
    if( displayTitle.IsEmpty() )
        displayTitle = "Untitled Chat";

    wxStaticText* titleText = new wxStaticText( itemPanel, wxID_ANY, displayTitle );
    titleText->SetForegroundColour( wxColour( "#ffffff" ) );
    wxFont font = titleText->GetFont();
    font.SetWeight( wxFONTWEIGHT_BOLD );
    titleText->SetFont( font );
    itemSizer->Add( titleText, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );

    // Timestamp (formatted shorter for single line)
    wxString timestamp = wxString::FromUTF8( aLastUpdated );
    // Format: "2025-01-27T10:30:00" -> "Jan 27 10:30"
    if( timestamp.length() >= 16 )
    {
        wxString month = timestamp.Mid( 5, 2 );
        wxString day = timestamp.Mid( 8, 2 );
        wxString time = timestamp.Mid( 11, 5 );

        wxString monthNames[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        long monthNum = 0;
        month.ToLong( &monthNum );
        if( monthNum >= 1 && monthNum <= 12 )
        {
            timestamp = wxString::Format( "%s %s %s",
                                          monthNames[monthNum - 1], day, time );
        }
    }

    wxStaticText* timeText = new wxStaticText( itemPanel, wxID_ANY, timestamp );
    timeText->SetForegroundColour( wxColour( "#666666" ) );
    itemSizer->Add( timeText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    itemPanel->SetSizer( itemSizer );

    // Store index in client data for click handling
    itemPanel->SetClientData( reinterpret_cast<void*>( static_cast<intptr_t>( aIndex ) ) );

    // Bind click events to the panel and its children
    itemPanel->Bind( wxEVT_LEFT_DOWN, &HISTORY_PANEL::OnHistoryItemClick, this );
    titleText->Bind( wxEVT_LEFT_DOWN, &HISTORY_PANEL::OnHistoryItemClick, this );
    timeText->Bind( wxEVT_LEFT_DOWN, &HISTORY_PANEL::OnHistoryItemClick, this );

    // Hover effect
    auto setHover = [itemPanel]( bool hover ) {
        itemPanel->SetBackgroundColour( hover ? wxColour( "#2d2d2d" ) : wxColour( "#1E1E1E" ) );
        itemPanel->Refresh();
    };

    itemPanel->Bind( wxEVT_ENTER_WINDOW, [setHover]( wxMouseEvent& ) { setHover( true ); } );
    itemPanel->Bind( wxEVT_LEAVE_WINDOW, [setHover]( wxMouseEvent& ) { setHover( false ); } );

    aSizer->Add( itemPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 2 );
}


void HISTORY_PANEL::OnHistoryItemClick( wxMouseEvent& aEvent )
{
    // Find the panel that was clicked (could be the panel itself or a child)
    wxWindow* window = dynamic_cast<wxWindow*>( aEvent.GetEventObject() );
    wxPanel* itemPanel = nullptr;

    // Walk up to find the panel with client data
    while( window && !itemPanel )
    {
        if( window->GetClientData() != nullptr )
        {
            itemPanel = dynamic_cast<wxPanel*>( window );
            break;
        }
        window = window->GetParent();
    }

    if( !itemPanel )
        return;

    int index = static_cast<int>( reinterpret_cast<intptr_t>( itemPanel->GetClientData() ) );

    if( index >= 0 && index < static_cast<int>( m_conversationIds.size() ) )
    {
        std::string conversationId = m_conversationIds[index];

        // Hide the overlay panel first
        Hide();

        // Load the selected conversation
        m_agentFrame->LoadConversation( conversationId );
    }
}


void HISTORY_PANEL::OnCloseClick( wxCommandEvent& aEvent )
{
    Hide();
}
