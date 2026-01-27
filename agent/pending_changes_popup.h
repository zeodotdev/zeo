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

#ifndef PENDING_CHANGES_PANEL_H
#define PENDING_CHANGES_PANEL_H

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

class AGENT_FRAME;

/**
 * Panel for displaying and managing pending agent changes.
 *
 * Shows pending schematic and/or PCB changes inline between the chat
 * and input areas. Each row shows filename with view/accept/reject buttons.
 */
class PENDING_CHANGES_PANEL : public wxPanel
{
public:
    PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame );

    /**
     * Update the panel to show current pending changes state.
     * @param aHasSchChanges True if schematic has pending changes.
     * @param aHasPcbChanges True if PCB has pending changes.
     * @param aSchFilename Schematic filename to display.
     * @param aPcbFilename PCB filename to display.
     */
    void UpdateChanges( bool aHasSchChanges, bool aHasPcbChanges,
                        const wxString& aSchFilename = wxEmptyString,
                        const wxString& aPcbFilename = wxEmptyString );

private:
    void OnViewSch( wxCommandEvent& event );
    void OnAcceptSch( wxCommandEvent& event );
    void OnRejectSch( wxCommandEvent& event );
    void OnViewPcb( wxCommandEvent& event );
    void OnAcceptPcb( wxCommandEvent& event );
    void OnRejectPcb( wxCommandEvent& event );

    void updateLayout();

    AGENT_FRAME*  m_agentFrame;

    // Schematic row
    wxPanel*      m_schRowPanel;
    wxStaticText* m_schLabel;
    wxButton*     m_viewSchBtn;
    wxButton*     m_acceptSchBtn;
    wxButton*     m_rejectSchBtn;

    // PCB row
    wxPanel*      m_pcbRowPanel;
    wxStaticText* m_pcbLabel;
    wxButton*     m_viewPcbBtn;
    wxButton*     m_acceptPcbBtn;
    wxButton*     m_rejectPcbBtn;

    wxBoxSizer*   m_mainSizer;

    bool m_hasSchChanges;
    bool m_hasPcbChanges;
};

#endif // PENDING_CHANGES_PANEL_H
