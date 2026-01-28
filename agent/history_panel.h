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

#ifndef HISTORY_PANEL_H
#define HISTORY_PANEL_H

#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <vector>
#include <string>

class AGENT_FRAME;

/**
 * Panel for displaying full chat history list.
 *
 * Shows all saved conversations in a scrollable list, overlaying the chat area.
 * Each entry shows the title and last updated timestamp.
 */
class HISTORY_PANEL : public wxPanel
{
public:
    HISTORY_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame );

    /**
     * Refresh the history list from the database.
     */
    void RefreshHistory();

private:
    void OnHistoryItemClick( wxMouseEvent& aEvent );
    void OnCloseClick( wxCommandEvent& aEvent );
    void OnSearchText( wxCommandEvent& aEvent );

    void CreateHistoryItem( wxWindow* aParent, wxSizer* aSizer,
                            const std::string& aId, const std::string& aTitle,
                            const std::string& aLastUpdated, int aIndex );
    void PopulateHistoryList( const wxString& aFilter );

    AGENT_FRAME*       m_agentFrame;
    wxScrolledWindow*  m_scrollWindow;
    wxBoxSizer*        m_listSizer;
    wxTextCtrl*        m_searchCtrl;
    wxButton*          m_closeBtn;

    // Store conversation IDs for click handling
    std::vector<std::string> m_conversationIds;
};

#endif // HISTORY_PANEL_H
