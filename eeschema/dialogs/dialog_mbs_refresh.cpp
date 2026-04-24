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
 */

#include "dialog_mbs_refresh.h"

#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


DIALOG_MBS_REFRESH::DIALOG_MBS_REFRESH( wxWindow* aParent, std::vector<MBS_CHANGE>& aChanges ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Refresh Module Blocks" ),
                     wxDefaultPosition, wxSize( 620, 480 ),
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_changes( aChanges ),
        m_list( nullptr ),
        m_countsLabel( nullptr ),
        m_checkAllButton( nullptr ),
        m_checkNoneButton( nullptr ),
        m_applyButton( nullptr ),
        m_cancelButton( nullptr )
{
    buildUI();
    refreshCounts();

    // Default focus on Apply so Enter accepts.
    m_applyButton->SetDefault();
    m_applyButton->SetFocus();

    Layout();
    Centre();
}


void DIALOG_MBS_REFRESH::buildUI()
{
    auto* topSizer = new wxBoxSizer( wxVERTICAL );

    auto* headerLabel = new wxStaticText(
            this, wxID_ANY,
            _( "Review the planned changes before applying. Uncheck any row you "
               "want to skip. Deletes run before adds so nothing is orphaned." ) );
    headerLabel->Wrap( 580 );

    topSizer->Add( headerLabel, 0, wxALL | wxEXPAND, 8 );

    // ---- Check-all / Check-none row ---------------------------------
    auto* buttonRow = new wxBoxSizer( wxHORIZONTAL );

    m_checkAllButton  = new wxButton( this, wxID_ANY, _( "Check All" ) );
    m_checkNoneButton = new wxButton( this, wxID_ANY, _( "Check None" ) );

    buttonRow->Add( m_checkAllButton, 0, wxRIGHT, 4 );
    buttonRow->Add( m_checkNoneButton, 0 );
    buttonRow->AddStretchSpacer();

    m_countsLabel = new wxStaticText( this, wxID_ANY, wxEmptyString );
    buttonRow->Add( m_countsLabel, 0, wxALIGN_CENTER_VERTICAL );

    topSizer->Add( buttonRow, 0, wxLEFT | wxRIGHT | wxEXPAND, 8 );

    // ---- Change list ------------------------------------------------
    wxArrayString items;

    for( const MBS_CHANGE& ch : m_changes )
        items.Add( ch.Describe() );

    m_list = new wxCheckListBox( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, items );

    // Seed check state from the MBS_CHANGE defaults.
    for( size_t i = 0; i < m_changes.size(); ++i )
        m_list->Check( (unsigned int) i, m_changes[i].checked );

    topSizer->Add( m_list, 1, wxALL | wxEXPAND, 8 );

    // ---- Dialog buttons ---------------------------------------------
    auto* actionRow = new wxBoxSizer( wxHORIZONTAL );

    actionRow->AddStretchSpacer();

    m_cancelButton = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );
    m_applyButton  = new wxButton( this, wxID_OK, _( "Apply" ) );

    actionRow->Add( m_cancelButton, 0, wxRIGHT, 4 );
    actionRow->Add( m_applyButton, 0 );

    topSizer->Add( actionRow, 0, wxALL | wxEXPAND, 8 );

    SetSizer( topSizer );

    // Empty diff: swap the list out for a "nothing to do" message and
    // disable Apply.
    if( m_changes.empty() )
    {
        m_list->Append( _( "The multi-board schematic is already in sync." ) );
        m_list->Enable( false );
        m_applyButton->Enable( false );
        m_checkAllButton->Enable( false );
        m_checkNoneButton->Enable( false );
    }

    // Wire events.
    m_checkAllButton->Bind( wxEVT_BUTTON, &DIALOG_MBS_REFRESH::onCheckAll, this );
    m_checkNoneButton->Bind( wxEVT_BUTTON, &DIALOG_MBS_REFRESH::onCheckNone, this );
    m_list->Bind( wxEVT_CHECKLISTBOX, &DIALOG_MBS_REFRESH::onItemToggled, this );
    m_applyButton->Bind( wxEVT_BUTTON, &DIALOG_MBS_REFRESH::onApply, this );
}


void DIALOG_MBS_REFRESH::refreshCounts()
{
    int checkedCount = 0;

    for( const MBS_CHANGE& ch : m_changes )
    {
        if( ch.checked )
            checkedCount++;
    }

    m_countsLabel->SetLabel(
            wxString::Format( _( "%d of %zu changes selected" ),
                              checkedCount, m_changes.size() ) );
}


void DIALOG_MBS_REFRESH::onCheckAll( wxCommandEvent& aEvent )
{
    for( size_t i = 0; i < m_changes.size(); ++i )
    {
        m_changes[i].checked = true;
        m_list->Check( (unsigned int) i, true );
    }

    refreshCounts();
}


void DIALOG_MBS_REFRESH::onCheckNone( wxCommandEvent& aEvent )
{
    for( size_t i = 0; i < m_changes.size(); ++i )
    {
        m_changes[i].checked = false;
        m_list->Check( (unsigned int) i, false );
    }

    refreshCounts();
}


void DIALOG_MBS_REFRESH::onItemToggled( wxCommandEvent& aEvent )
{
    int idx = aEvent.GetInt();

    if( idx < 0 || idx >= (int) m_changes.size() )
        return;

    m_changes[idx].checked = m_list->IsChecked( (unsigned int) idx );
    refreshCounts();
}


void DIALOG_MBS_REFRESH::onApply( wxCommandEvent& aEvent )
{
    // Sync the final check state from the list widget back into the
    // change vector in case Bind didn't fire for the last toggle.
    for( size_t i = 0; i < m_changes.size(); ++i )
        m_changes[i].checked = m_list->IsChecked( (unsigned int) i );

    EndModal( wxID_OK );
}
