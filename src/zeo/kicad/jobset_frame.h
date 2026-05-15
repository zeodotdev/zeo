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

#ifndef KICAD_JOBSET_FRAME_H
#define KICAD_JOBSET_FRAME_H

#include <wx/frame.h>
#include <memory>

class JOBSET;
class KICAD_MANAGER_FRAME;
class PANEL_JOBSET;
class wxCloseEvent;


/**
 * Standalone top-level frame that hosts a single PANEL_JOBSET. Each opened
 * `.kicad_jobs` file pops one of these so the user can dock/tile them
 * independently of the project launcher sidebar.
 *
 * Lifecycle: created by KICAD_MANAGER_FRAME::OpenJobsFile, owned by wx (it
 * Destroy()s itself on close). The manager tracks the live instances so it
 * can persist them to local settings, close them on project close, and
 * refocus an existing frame when the user reopens the same file.
 */
class JOBSET_FRAME : public wxFrame
{
public:
    JOBSET_FRAME( KICAD_MANAGER_FRAME* aManager, std::unique_ptr<JOBSET> aJobsFile );
    ~JOBSET_FRAME() override;

    wxString      GetFilePath() const;
    PANEL_JOBSET* GetPanel() const { return m_panel; }

    /// Re-read the panel's preferred title (jobset filename + dirty marker)
    /// and apply it to the frame caption. Called by PANEL_JOBSET when the
    /// underlying JOBSET goes dirty/clean.
    void RefreshTitle();

private:
    void onClose( wxCloseEvent& aEvent );

    KICAD_MANAGER_FRAME* m_manager;
    PANEL_JOBSET*        m_panel;
};

#endif // KICAD_JOBSET_FRAME_H
