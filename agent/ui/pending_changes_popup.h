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
#include <vector>
#include <set>

class AGENT_FRAME;

/**
 * Panel for displaying pending agent changes.
 *
 * Shows a tab-like panel with rounded top corners, containing a list of
 * sheets/files with pending changes. Each row is clickable and navigates
 * to that sheet/PCB to view the diff overlay.
 *
 * Visual style:
 * - Rounded top corners (16px radius)
 * - Lighter grey background (#3D3D3D)
 * - Margins from the sides of the agent window
 * - Clickable rows with hover effect
 */
class PENDING_CHANGES_PANEL : public wxPanel
{
public:
    PENDING_CHANGES_PANEL( wxWindow* aParent, AGENT_FRAME* aAgentFrame );

    /**
     * Refresh the panel by querying the editors for current pending changes.
     * This queries the schematic and PCB editors via ExpressMail to get
     * the list of sheets with changes.
     */
    void Refresh();

private:
    // Paint event handlers for custom rounded corners
    void OnPaint( wxPaintEvent& aEvent );
    void OnEraseBackground( wxEraseEvent& aEvent );

    void createUI();
    void rebuildSheetList();
    void createClickableRow( const wxString& aLabel, bool aIsPcb );
    void onRowClick( wxMouseEvent& aEvent );
    void onViewSheet( const wxString& aSheetPath );
    void onViewPcb();
    void onAcceptAll();
    void onRejectAll();

    AGENT_FRAME*  m_agentFrame;
    wxBoxSizer*   m_mainSizer;
    wxPanel*      m_contentPanel;
    wxBoxSizer*   m_contentSizer;
    wxButton*     m_acceptAllBtn;
    wxButton*     m_rejectAllBtn;

    // Current state
    std::set<wxString> m_schSheets;  // Sheets with schematic changes
    bool m_hasPcbChanges;
    wxString m_pcbFilename;
};

#endif // PENDING_CHANGES_PANEL_H
