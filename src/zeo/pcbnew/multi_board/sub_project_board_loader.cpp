/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "sub_project_board_loader.h"

#include <board.h>
#include <ki_exception.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <project.h>
#include <project/multi_board_scan.h>
#include <project/project_file.h>

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/string.h>


std::unique_ptr<BOARD> LoadSubProjectBoard( const PROJECT&          aContainer,
                                            const SUB_PROJECT_INFO& aSubProject )
{
    const PROJECT_FILE& projectFile = aContainer.GetProjectFile();

    if( !projectFile.IsMultiBoardContainer() )
    {
        wxLogTrace( wxT( "MULTI_BOARD" ),
                    wxT( "LoadSubProjectBoard: project '%s' is not a multi-board container" ),
                    aContainer.GetProjectFullName() );
        return nullptr;
    }

    wxFileName proFile = projectFile.ResolveSubProjectPath( aSubProject );

    if( !proFile.FileExists() )
    {
        wxLogTrace( wxT( "MULTI_BOARD" ),
                    wxT( "LoadSubProjectBoard: sub-project .kicad_pro not found at '%s'" ),
                    proFile.GetFullPath() );
        return nullptr;
    }

    wxFileName pcbFile = MultiBoardMainPcb( proFile );

    if( !pcbFile.FileExists() )
    {
        wxLogTrace( wxT( "MULTI_BOARD" ),
                    wxT( "LoadSubProjectBoard: sub-project .kicad_pcb not found at '%s'" ),
                    pcbFile.GetFullPath() );
        return nullptr;
    }

    PCB_IO_KICAD_SEXPR pi;
    BOARD*             rawBoard = nullptr;

    try
    {
        rawBoard = pi.LoadBoard( pcbFile.GetFullPath(), nullptr, nullptr, nullptr );
    }
    catch( const IO_ERROR& ioe )
    {
        wxLogTrace( wxT( "MULTI_BOARD" ),
                    wxT( "LoadSubProjectBoard: IO_ERROR loading '%s': %s" ),
                    pcbFile.GetFullPath(), ioe.What() );
        return nullptr;
    }

    return std::unique_ptr<BOARD>( rawBoard );
}


std::unique_ptr<BOARD> LoadSubProjectBoard( const PROJECT& aContainer,
                                            const KIID&    aSubProjectUuid )
{
    const PROJECT_FILE& projectFile = aContainer.GetProjectFile();
    const SUB_PROJECT_INFO* info = projectFile.GetSubProject( aSubProjectUuid );

    if( !info )
    {
        wxLogTrace( wxT( "MULTI_BOARD" ),
                    wxT( "LoadSubProjectBoard: no sub-project with UUID '%s'" ),
                    aSubProjectUuid.AsString() );
        return nullptr;
    }

    return LoadSubProjectBoard( aContainer, *info );
}
