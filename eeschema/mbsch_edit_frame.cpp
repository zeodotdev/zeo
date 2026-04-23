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

#include "mbsch_edit_frame.h"

#include <multi_board_net_extractor.h>
#include <project/project_file.h>
#include <schematic.h>
#include <sch_screen.h>

#include <wx/dir.h>
#include <wx/filename.h>


MBSCH_EDIT_FRAME::MBSCH_EDIT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        SCH_EDIT_FRAME( aKiway, aParent, FRAME_MBSCH )
{
    m_aboutTitle = _HKI( "Zeo Multi-Board Schematic Editor" );
}


MBSCH_EDIT_FRAME::~MBSCH_EDIT_FRAME()
{
}


wxString MBSCH_EDIT_FRAME::windowTitleSuffix() const
{
    return _( "Multi-Board Schematic Editor" );
}


void MBSCH_EDIT_FRAME::onSchematicSaved()
{
    SCH_SCREEN* rootScreen = Schematic().RootScreen();

    if( !rootScreen )
        return;

    wxFileName schFn( rootScreen->GetFileName() );

    if( !schFn.IsAbsolute() )
        schFn.MakeAbsolute( Prj().GetProjectPath() );

    // Walk upward from the MBS's directory for a `.kicad_pro` with
    // `multi_board.container = true` whose `mbs_file` points at this
    // schematic. The container/MBS pairing isn't stored inside the
    // `.kicad_mbs` itself yet, so a targeted walk remains the link.
    wxFileName multiFile;
    wxFileName searchDir( schFn );
    searchDir.SetFullName( wxEmptyString );

    for( int depth = 0; depth < 6 && searchDir.GetPath().Length() > 1; ++depth )
    {
        wxArrayString files;
        wxDir::GetAllFiles( searchDir.GetPath(), &files, wxT( "*.kicad_pro" ),
                            wxDIR_FILES );

        for( const wxString& candidate : files )
        {
            PROJECT_FILE probe( candidate );

            if( !probe.LoadFromFile() )
                continue;

            if( !probe.IsMultiBoardContainer() )
                continue;

            wxFileName expectedMbs = probe.ResolveMbsPath();

            if( expectedMbs.IsOk()
                && expectedMbs.GetFullPath() == schFn.GetFullPath() )
            {
                multiFile = wxFileName( candidate );
                break;
            }
        }

        if( multiFile.IsOk() )
            break;

        searchDir.RemoveLastDir();
    }

    if( !multiFile.IsOk() || !multiFile.FileExists() )
        return;

    PROJECT_FILE multi( multiFile.GetFullPath() );

    if( !multi.LoadFromFile() )
        return;

    std::vector<MB_CROSS_BOARD_NET> nets = ExtractCrossBoardNets( Schematic(), multi );
    multi.SetCrossBoardNets( nets );
    multi.SaveToFile();

    // Surface extraction outcomes in the status bar so a user wiring the
    // MBS can immediately tell whether Sync will have anything to do —
    // without opening the project manager or inspecting the .kicad_pro.
    wxString msg = wxString::Format( _( "Multi-board: extracted %zu cross-board net(s)" ),
                                     nets.size() );
    SetStatusText( msg, 0 );
    wxLogTrace( wxT( "MULTI_BOARD" ), wxS( "%s → %s" ), msg, multiFile.GetFullPath() );
}
