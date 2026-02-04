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
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

// Colors
static const wxColour PANEL_BG_COLOR( "#3d3d3d" );      // Panel background (lighter grey)
static const wxColour TEXT_COLOR( "#ffffff" );          // White text
static const wxColour MUTED_TEXT_COLOR( "#aaaaaa" );    // Muted text for paths
static const wxColour HOVER_COLOR( "#4d4d4d" );         // Hover state

// Corner radius for top corners
static const int CORNER_RADIUS = 16;

PENDING_CHANGES_PANEL::PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxNO_BORDER ),
        m_agentFrame( aAgentFrame ),
        m_contentPanel( nullptr ),
        m_contentSizer( nullptr ),
        m_acceptAllBtn( nullptr ),
        m_rejectAllBtn( nullptr ),
        m_hasPcbChanges( false )
{
    // Use transparent background - we'll paint our own
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    m_mainSizer = new wxBoxSizer( wxVERTICAL );

    // Add top padding for the rounded corners
    m_mainSizer->AddSpacer( 4 );

    // Header row with title and Accept All / Reject All buttons
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticText* header = new wxStaticText( this, wxID_ANY, "Pending Changes" );
    header->SetForegroundColour( TEXT_COLOR );
    wxFont boldFont = header->GetFont();
    boldFont.SetWeight( wxFONTWEIGHT_BOLD );
    header->SetFont( boldFont );
    headerSizer->Add( header, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 16 );

    // Accept All button
    m_acceptAllBtn = new wxButton( this, wxID_ANY, "Accept All", wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT );
    m_acceptAllBtn->SetToolTip( "Accept all pending changes" );
    headerSizer->Add( m_acceptAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    // Reject All button
    m_rejectAllBtn = new wxButton( this, wxID_ANY, "Reject All", wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT );
    m_rejectAllBtn->SetToolTip( "Reject all pending changes" );
    headerSizer->Add( m_rejectAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16 );

    m_mainSizer->Add( headerSizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 8 );

    // Bind button events
    m_acceptAllBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onAcceptAll(); } );
    m_rejectAllBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onRejectAll(); } );

    // Content panel for sheet list
    m_contentPanel = new wxPanel( this, wxID_ANY );
    m_contentPanel->SetBackgroundColour( PANEL_BG_COLOR );
    m_contentSizer = new wxBoxSizer( wxVERTICAL );
    m_contentPanel->SetSizer( m_contentSizer );
    m_mainSizer->Add( m_contentPanel, 0, wxEXPAND | wxBOTTOM, 12 );

    SetSizer( m_mainSizer );

    // Bind paint event for custom rounded corners
    Bind( wxEVT_PAINT, &PENDING_CHANGES_PANEL::OnPaint, this );
    Bind( wxEVT_ERASE_BACKGROUND, &PENDING_CHANGES_PANEL::OnEraseBackground, this );

    // Initially hidden
    Hide();
}


void PENDING_CHANGES_PANEL::OnPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    wxSize size = GetClientSize();

    // Get the parent's background color for the corners
    wxColour parentBg = GetParent()->GetBackgroundColour();

    // Fill entire area with parent background first
    dc.SetBrush( wxBrush( parentBg ) );
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.DrawRectangle( 0, 0, size.x, size.y );

    // Now draw the rounded rectangle for the panel
    wxGraphicsContext* gc = wxGraphicsContext::Create( dc );
    if( gc )
    {
        gc->SetBrush( wxBrush( PANEL_BG_COLOR ) );
        gc->SetPen( *wxTRANSPARENT_PEN );

        // Create path with rounded top corners only
        wxGraphicsPath path = gc->CreatePath();

        // Start from bottom-left
        path.MoveToPoint( 0, size.y );
        // Left edge up to top-left corner
        path.AddLineToPoint( 0, CORNER_RADIUS );
        // Top-left rounded corner
        path.AddArc( CORNER_RADIUS, CORNER_RADIUS, CORNER_RADIUS, M_PI, M_PI * 1.5, true );
        // Top edge to top-right corner
        path.AddLineToPoint( size.x - CORNER_RADIUS, 0 );
        // Top-right rounded corner
        path.AddArc( size.x - CORNER_RADIUS, CORNER_RADIUS, CORNER_RADIUS, M_PI * 1.5, 0, true );
        // Right edge down to bottom-right
        path.AddLineToPoint( size.x, size.y );
        // Bottom edge back to start
        path.AddLineToPoint( 0, size.y );
        path.CloseSubpath();

        gc->FillPath( path );
        delete gc;
    }
}


void PENDING_CHANGES_PANEL::OnEraseBackground( wxEraseEvent& aEvent )
{
    // Do nothing - prevents flicker
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
    wxPanel::Refresh();  // Trigger repaint for custom drawing
    GetParent()->Layout();
}


void PENDING_CHANGES_PANEL::rebuildSheetList()
{
    // Clear existing content
    m_contentSizer->Clear( true );  // true = delete windows

    // Add schematic sheet rows (clickable text)
    for( const wxString& sheetPath : m_schSheets )
    {
        createClickableRow( sheetPath, false /* not PCB */ );
    }

    // Add PCB row if there are PCB changes
    if( m_hasPcbChanges )
    {
        createClickableRow( "PCB", true /* is PCB */ );
    }

    m_contentPanel->Layout();
}


void PENDING_CHANGES_PANEL::createClickableRow( const wxString& aLabel, bool aIsPcb )
{
    wxPanel* rowPanel = new wxPanel( m_contentPanel, wxID_ANY );
    rowPanel->SetBackgroundColour( PANEL_BG_COLOR );

    wxBoxSizer* rowSizer = new wxBoxSizer( wxHORIZONTAL );

    // Show the full path for schematic sheets
    wxString displayName = aLabel;
    if( !aIsPcb )
    {
        // Append "/" to indicate it's a sheet path if not already there
        if( !displayName.EndsWith( "/" ) )
            displayName += "/";
    }

    wxStaticText* label = new wxStaticText( rowPanel, wxID_ANY, displayName );
    label->SetForegroundColour( MUTED_TEXT_COLOR );
    label->SetCursor( wxCursor( wxCURSOR_HAND ) );

    if( !aIsPcb )
        label->SetToolTip( "Click to view changes on " + aLabel );
    else
        label->SetToolTip( "Click to view PCB changes" );

    rowSizer->Add( label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 16 );

    rowPanel->SetSizer( rowSizer );

    // Store the path/type in client data
    wxString* pathData = new wxString( aIsPcb ? "PCB" : aLabel );
    rowPanel->SetClientData( pathData );

    // Bind click events to the panel and label
    rowPanel->Bind( wxEVT_LEFT_DOWN, &PENDING_CHANGES_PANEL::onRowClick, this );
    label->Bind( wxEVT_LEFT_DOWN, &PENDING_CHANGES_PANEL::onRowClick, this );

    // Hover effect
    auto setHover = [rowPanel]( bool hover ) {
        rowPanel->SetBackgroundColour( hover ? HOVER_COLOR : PANEL_BG_COLOR );
        rowPanel->Refresh();
    };

    rowPanel->Bind( wxEVT_ENTER_WINDOW, [setHover]( wxMouseEvent& ) { setHover( true ); } );
    rowPanel->Bind( wxEVT_LEAVE_WINDOW, [setHover]( wxMouseEvent& ) { setHover( false ); } );
    label->Bind( wxEVT_ENTER_WINDOW, [setHover]( wxMouseEvent& e ) { setHover( true ); e.Skip(); } );

    m_contentSizer->Add( rowPanel, 0, wxEXPAND, 0 );
}


void PENDING_CHANGES_PANEL::onRowClick( wxMouseEvent& aEvent )
{
    // Find the panel that was clicked (could be the panel itself or a child)
    wxWindow* window = dynamic_cast<wxWindow*>( aEvent.GetEventObject() );
    wxPanel* rowPanel = nullptr;

    // Walk up to find the panel with client data
    while( window && !rowPanel )
    {
        if( window->GetClientData() != nullptr )
        {
            rowPanel = dynamic_cast<wxPanel*>( window );
            break;
        }
        window = window->GetParent();
    }

    if( !rowPanel || !rowPanel->GetClientData() )
        return;

    wxString* pathData = static_cast<wxString*>( rowPanel->GetClientData() );

    if( *pathData == "PCB" )
    {
        onViewPcb();
    }
    else
    {
        onViewSheet( *pathData );
    }
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

    // Clear local state directly - don't wait for re-query
    // (the editors will process the approve messages)
    m_schSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.clear();

    // Rebuild UI (will be empty) and hide the panel
    rebuildSheetList();
    Hide();

    Layout();
    wxPanel::Refresh();

    // Update the button visibility in the agent frame
    m_agentFrame->UpdatePendingChangesButtonVisibility();
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

    // Clear local state directly - don't wait for re-query
    // (the editors will process the reject messages)
    m_schSheets.clear();
    m_hasPcbChanges = false;
    m_pcbFilename.clear();

    // Rebuild UI (will be empty) and hide the panel
    rebuildSheetList();
    Hide();

    Layout();
    wxPanel::Refresh();

    // Update the button visibility in the agent frame
    m_agentFrame->UpdatePendingChangesButtonVisibility();
}
