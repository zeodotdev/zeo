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

#include "jobset_frame.h"

#include <bitmaps.h>
#include <jobs/jobset.h>
#include <kicad_manager_frame.h>
#include <dialogs/panel_jobset.h>

#include <wx/sizer.h>


JOBSET_FRAME::JOBSET_FRAME( KICAD_MANAGER_FRAME* aManager,
                            std::unique_ptr<JOBSET> aJobsFile ) :
        wxFrame( aManager, wxID_ANY, wxEmptyString, wxDefaultPosition,
                 aManager->FromDIP( wxSize( 760, 620 ) ),
                 wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT ),
        m_manager( aManager ),
        m_panel( nullptr )
{
    SetIcon( KiBitmapBundle( BITMAPS::jobset ).GetIcon( FromDIP( wxSize( 16, 16 ) ) ) );

    // PANEL_JOBSET takes ownership of the JOBSET. Pass the raw wxWindow*
    // parent — the panel detects standalone-vs-notebook hosting by
    // dynamic_cast on its parent.
    m_panel = new PANEL_JOBSET( this, m_manager, std::move( aJobsFile ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( m_panel, 1, wxEXPAND );
    SetSizer( sizer );

    RefreshTitle();

    Bind( wxEVT_CLOSE_WINDOW, &JOBSET_FRAME::onClose, this );
}


JOBSET_FRAME::~JOBSET_FRAME() = default;


wxString JOBSET_FRAME::GetFilePath() const
{
    return m_panel ? m_panel->GetFilePath() : wxString();
}


void JOBSET_FRAME::RefreshTitle()
{
    if( !m_panel )
        return;

    JOBSET*  jobs  = m_panel->GetJobsFile();
    wxString title = jobs->GetFullName();

    if( jobs->GetDirty() )
        title = wxS( "*" ) + title;

    SetTitle( title );
}


void JOBSET_FRAME::onClose( wxCloseEvent& aEvent )
{
    // Honor the panel's "can close" gate (prompts to save unsaved edits).
    // If the user cancels, veto so the frame stays open.
    if( m_panel && !m_panel->GetCanClose() )
    {
        if( aEvent.CanVeto() )
        {
            aEvent.Veto();
            return;
        }
    }

    if( m_manager )
        m_manager->NotifyJobsetFrameClosing( this );

    aEvent.Skip();
    Destroy();
}
