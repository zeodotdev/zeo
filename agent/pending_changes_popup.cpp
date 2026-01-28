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
#include <wx/msgdlg.h>

PENDING_CHANGES_PANEL::PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_agentFrame( aAgentFrame ),
        m_hasSchChanges( false ),
        m_hasPcbChanges( false ),
        m_targetingDifferentSheet( false )
{
    SetBackgroundColour( wxColour( "#1E1E1E" ) );

    m_mainSizer = new wxBoxSizer( wxVERTICAL );

    // === Target sheet indicator ===
    m_targetSheetPanel = new wxPanel( this, wxID_ANY );
    m_targetSheetPanel->SetBackgroundColour( wxColour( "#2D2D2D" ) );
    wxBoxSizer* targetSheetSizer = new wxBoxSizer( wxHORIZONTAL );

    m_targetSheetLabel = new wxStaticText( m_targetSheetPanel, wxID_ANY,
                                            "Agent working on: /Root" );
    m_targetSheetLabel->SetForegroundColour( wxColour( "#FFA500" ) ); // Orange for visibility
    targetSheetSizer->Add( m_targetSheetLabel, 1, wxALIGN_CENTER_VERTICAL | wxALL, 8 );

    m_targetSheetPanel->SetSizer( targetSheetSizer );
    m_mainSizer->Add( m_targetSheetPanel, 0, wxEXPAND | wxBOTTOM, 5 );
    m_targetSheetPanel->Hide();

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

    // === Conflict panel ===
    m_conflictPanel = new wxPanel( this, wxID_ANY );
    m_conflictPanel->SetBackgroundColour( wxColour( "#3D2020" ) ); // Darker red background
    wxBoxSizer* conflictSizer = new wxBoxSizer( wxVERTICAL );

    m_conflictHeaderLabel = new wxStaticText( m_conflictPanel, wxID_ANY,
                                               "Conflicts Detected" );
    m_conflictHeaderLabel->SetForegroundColour( wxColour( "#FF6B6B" ) );
    wxFont boldFont = m_conflictHeaderLabel->GetFont();
    boldFont.SetWeight( wxFONTWEIGHT_BOLD );
    m_conflictHeaderLabel->SetFont( boldFont );
    conflictSizer->Add( m_conflictHeaderLabel, 0, wxALL, 8 );

    // Conflict list
    m_conflictList = new wxListCtrl( m_conflictPanel, wxID_ANY, wxDefaultPosition,
                                      wxSize( -1, 100 ), wxLC_REPORT | wxLC_SINGLE_SEL );
    m_conflictList->SetBackgroundColour( wxColour( "#2D2020" ) );
    m_conflictList->SetForegroundColour( wxColour( "#d4d4d4" ) );

    m_conflictList->InsertColumn( 0, "Item", wxLIST_FORMAT_LEFT, 100 );
    m_conflictList->InsertColumn( 1, "Property", wxLIST_FORMAT_LEFT, 80 );
    m_conflictList->InsertColumn( 2, "Your Value", wxLIST_FORMAT_LEFT, 100 );
    m_conflictList->InsertColumn( 3, "Agent Value", wxLIST_FORMAT_LEFT, 100 );

    conflictSizer->Add( m_conflictList, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    // Conflict resolution buttons
    wxBoxSizer* conflictBtnSizer = new wxBoxSizer( wxHORIZONTAL );

    m_keepMineBtn = new wxButton( m_conflictPanel, wxID_ANY, "Keep Mine" );
    m_keepMineBtn->SetToolTip( "Discard agent's change for selected item" );
    conflictBtnSizer->Add( m_keepMineBtn, 0, wxRIGHT, 5 );

    m_keepAgentBtn = new wxButton( m_conflictPanel, wxID_ANY, "Keep Agent's" );
    m_keepAgentBtn->SetToolTip( "Discard your change for selected item" );
    conflictBtnSizer->Add( m_keepAgentBtn, 0, wxRIGHT, 5 );

    m_mergeBothBtn = new wxButton( m_conflictPanel, wxID_ANY, "Merge Both" );
    m_mergeBothBtn->SetToolTip( "Keep both changes (if possible)" );
    conflictBtnSizer->Add( m_mergeBothBtn, 0, wxRIGHT, 5 );

    conflictBtnSizer->AddStretchSpacer();

    m_resolveAllBtn = new wxButton( m_conflictPanel, wxID_ANY, "Auto-Resolve All" );
    m_resolveAllBtn->SetToolTip( "Automatically resolve all conflicts where possible" );
    conflictBtnSizer->Add( m_resolveAllBtn, 0 );

    conflictSizer->Add( conflictBtnSizer, 0, wxEXPAND | wxALL, 8 );

    m_conflictPanel->SetSizer( conflictSizer );
    m_mainSizer->Add( m_conflictPanel, 0, wxEXPAND | wxBOTTOM, 5 );
    m_conflictPanel->Hide();

    SetSizer( m_mainSizer );

    // Bind events
    m_viewSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnViewSch, this );
    m_acceptSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnAcceptSch, this );
    m_rejectSchBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnRejectSch, this );
    m_viewPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnViewPcb, this );
    m_acceptPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnAcceptPcb, this );
    m_rejectPcbBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnRejectPcb, this );

    // Conflict resolution events
    m_keepMineBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnKeepMine, this );
    m_keepAgentBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnKeepAgent, this );
    m_mergeBothBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnMergeBoth, this );
    m_resolveAllBtn->Bind( wxEVT_BUTTON, &PENDING_CHANGES_PANEL::OnResolveAll, this );

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

    // Target sheet panel is managed by SetTargetSheet()
    // Conflict panel is managed by updateConflictDisplay()

    // Disable accept buttons if there are unresolved conflicts
    bool hasConflicts = !m_conflicts.empty();
    m_acceptSchBtn->Enable( m_hasSchChanges && !hasConflicts );
    m_acceptPcbBtn->Enable( m_hasPcbChanges && !hasConflicts );

    if( hasConflicts )
    {
        m_acceptSchBtn->SetToolTip( "Resolve all conflicts before accepting" );
        m_acceptPcbBtn->SetToolTip( "Resolve all conflicts before accepting" );
    }
    else
    {
        m_acceptSchBtn->SetToolTip( wxEmptyString );
        m_acceptPcbBtn->SetToolTip( wxEmptyString );
    }

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


void PENDING_CHANGES_PANEL::SetTargetSheet( const wxString& aSheetName,
                                             const wxString& aCurrentSheetName )
{
    m_targetSheetName = aSheetName;
    m_currentSheetName = aCurrentSheetName;
    m_targetingDifferentSheet = ( aSheetName != aCurrentSheetName && !aSheetName.IsEmpty() );

    if( m_targetingDifferentSheet )
    {
        m_targetSheetLabel->SetLabel( wxString::Format(
                "Agent working on: %s (you're viewing: %s)",
                aSheetName, aCurrentSheetName ) );
        m_targetSheetPanel->Show();
    }
    else
    {
        m_targetSheetPanel->Hide();
    }

    updateLayout();
}


void PENDING_CHANGES_PANEL::UpdateConflicts( const std::vector<CONFLICT_INFO>& aConflicts )
{
    m_conflicts = aConflicts;
    updateConflictDisplay();
    updateLayout();
}


void PENDING_CHANGES_PANEL::ClearConflicts()
{
    m_conflicts.clear();
    m_conflictList->DeleteAllItems();
    m_conflictPanel->Hide();
    updateLayout();
}


void PENDING_CHANGES_PANEL::updateConflictDisplay()
{
    m_conflictList->DeleteAllItems();

    if( m_conflicts.empty() )
    {
        m_conflictPanel->Hide();
        return;
    }

    m_conflictPanel->Show();

    for( size_t i = 0; i < m_conflicts.size(); i++ )
    {
        const auto& conflict = m_conflicts[i];

        long itemIndex = m_conflictList->InsertItem( i, conflict.m_itemId.AsString() );
        m_conflictList->SetItem( itemIndex, 1, conflict.m_propertyName );
        m_conflictList->SetItem( itemIndex, 2, conflict.m_userValue );
        m_conflictList->SetItem( itemIndex, 3, conflict.m_agentValue );

        // Store the item ID in the item data for later retrieval
        m_conflictList->SetItemData( itemIndex, i );
    }

    // Update header with count
    m_conflictHeaderLabel->SetLabel( wxString::Format(
            "Conflicts Detected (%zu)", m_conflicts.size() ) );

    // Enable/disable merge button based on whether auto-merge is possible
    bool anyCanMerge = false;
    for( const auto& conflict : m_conflicts )
    {
        if( conflict.m_canAutoMerge )
        {
            anyCanMerge = true;
            break;
        }
    }
    m_mergeBothBtn->Enable( anyCanMerge );
}


void PENDING_CHANGES_PANEL::OnKeepMine( wxCommandEvent& event )
{
    long selected = m_conflictList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( selected == -1 )
        return;

    size_t conflictIndex = m_conflictList->GetItemData( selected );

    if( conflictIndex < m_conflicts.size() )
    {
        const CONFLICT_INFO& conflict = m_conflicts[conflictIndex];

        // Send resolution to editors
        nlohmann::json j;
        j["item_uuid"] = conflict.m_itemId.AsStdString();
        j["resolution"] = "keep_user";
        std::string payload = j.dump();

        m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_CONFLICT_RESOLVED, payload );

        // Remove from conflicts list
        m_conflicts.erase( m_conflicts.begin() + conflictIndex );
        updateConflictDisplay();
    }
}


void PENDING_CHANGES_PANEL::OnKeepAgent( wxCommandEvent& event )
{
    long selected = m_conflictList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( selected == -1 )
        return;

    size_t conflictIndex = m_conflictList->GetItemData( selected );

    if( conflictIndex < m_conflicts.size() )
    {
        const CONFLICT_INFO& conflict = m_conflicts[conflictIndex];

        // Send resolution to editors
        nlohmann::json j;
        j["item_uuid"] = conflict.m_itemId.AsStdString();
        j["resolution"] = "keep_agent";
        std::string payload = j.dump();

        m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_CONFLICT_RESOLVED, payload );

        // Remove from conflicts list
        m_conflicts.erase( m_conflicts.begin() + conflictIndex );
        updateConflictDisplay();
    }
}


void PENDING_CHANGES_PANEL::OnMergeBoth( wxCommandEvent& event )
{
    long selected = m_conflictList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( selected == -1 )
        return;

    size_t conflictIndex = m_conflictList->GetItemData( selected );

    if( conflictIndex < m_conflicts.size() )
    {
        const CONFLICT_INFO& conflict = m_conflicts[conflictIndex];

        if( !conflict.m_canAutoMerge )
        {
            // Can't auto-merge this conflict
            wxMessageBox( wxT( "This conflict cannot be automatically merged. "
                               "Please choose Keep Mine or Keep Agent's." ),
                          wxT( "Cannot Merge" ), wxOK | wxICON_WARNING );
            return;
        }

        // Send resolution to editors
        nlohmann::json j;
        j["item_uuid"] = conflict.m_itemId.AsStdString();
        j["resolution"] = "merge";
        std::string payload = j.dump();

        m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_CONFLICT_RESOLVED, payload );

        // Remove from conflicts list
        m_conflicts.erase( m_conflicts.begin() + conflictIndex );
        updateConflictDisplay();
    }
}


void PENDING_CHANGES_PANEL::OnResolveAll( wxCommandEvent& event )
{
    // Auto-resolve all conflicts where possible
    std::vector<CONFLICT_INFO> remainingConflicts;

    for( const auto& conflict : m_conflicts )
    {
        nlohmann::json j;
        j["item_uuid"] = conflict.m_itemId.AsStdString();

        if( conflict.m_canAutoMerge )
        {
            // Auto-merge where possible
            j["resolution"] = "merge";
            std::string payload = j.dump();
            m_agentFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_CONFLICT_RESOLVED, payload );
        }
        else
        {
            // Keep conflicts that can't be auto-resolved
            remainingConflicts.push_back( conflict );
        }
    }

    m_conflicts = remainingConflicts;
    updateConflictDisplay();

    if( !m_conflicts.empty() )
    {
        wxMessageBox( wxString::Format(
                wxT( "%zu conflict(s) could not be automatically resolved. "
                     "Please resolve them manually." ), m_conflicts.size() ),
                wxT( "Some Conflicts Remain" ), wxOK | wxICON_INFORMATION );
    }
}
