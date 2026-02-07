/*
* This program source code file is part of KiCad, a free EDA CAD application.
*
* Copyright (C) 2020 Mark Roszko <mark.roszko@gmail.com>
* Copyright The KiCad Developers, see AUTHORS.txt for contributors.
*
* This program is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <kiplatform/app.h>

#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/string.h>
#include <wx/sysopt.h>
#include <wx/utils.h>

// Global log file - must persist for lifetime of app
static wxFFile* s_logFile = nullptr;

// Custom log target that writes to both file and stderr
class wxLogDual : public wxLog
{
public:
    wxLogDual( FILE* logFile ) : m_logFile( logFile ) {}

protected:
    void DoLogText( const wxString& msg ) override
    {
        const wxScopedCharBuffer utf8 = msg.utf8_str();

        if( m_logFile )
        {
            fprintf( m_logFile, "%s\n", utf8.data() );
            fflush( m_logFile );
        }

        fprintf( stderr, "%s\n", utf8.data() );
    }

private:
    FILE* m_logFile;
};


bool KIPLATFORM::APP::Init()
{
    // KiCad relies on showing the file type selector in a few places; force it to be shown
    wxSystemOptions::SetOption( wxS( "osx.openfiledialog.always-show-types" ), 1 );

    // Set up logging
    wxLog::EnableLogging( true );
    wxLog::SetLogLevel( wxLOG_Trace );

    // Always log to file
    wxString logDir = wxFileName::GetHomeDir() + wxS( "/Library/Logs/Zener" );

    if( !wxDir::Exists( logDir ) )
        wxFileName::Mkdir( logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    wxString timestamp = wxDateTime::Now().Format( wxS( "%Y-%m-%d-%H%M%S" ) );
    wxString logPath = logDir + wxS( "/agent-" ) + timestamp + wxS( ".log" );

    s_logFile = new wxFFile( logPath, wxS( "w" ) );

    // Check if WXTRACE is set for stderr output (development mode)
    wxString traceVars;
    bool useStderr = wxGetEnv( wxS( "WXTRACE" ), &traceVars ) && !traceVars.empty();

    if( useStderr && s_logFile->IsOpened() )
    {
        // Log to both file and stderr
        wxLog::SetActiveTarget( new wxLogDual( s_logFile->fp() ) );
    }
    else if( s_logFile->IsOpened() )
    {
        // Log to file only
        wxLog::SetActiveTarget( new wxLogStderr( s_logFile->fp() ) );
    }

    // Enable the "Agent" trace mask so wxLogTrace("Agent", ...) calls produce output
    wxLog::AddTraceMask( wxS( "Agent" ) );

    wxLogInfo( wxS( "Zener session started, logging to %s" ), logPath );

    // Clean up old log files: remove empty files and cap at 20
    wxDir logDirObj( logDir );

    if( logDirObj.IsOpened() )
    {
        wxString      filename;
        wxArrayString logFiles;
        bool          cont = logDirObj.GetFirst( &filename, wxS( "agent-*.log" ) );

        while( cont )
        {
            wxString fullPath = logDir + wxS( "/" ) + filename;

            if( fullPath != logPath )
            {
                wxFileName fn( fullPath );

                if( fn.GetSize() == 0 )
                    wxRemoveFile( fullPath );
                else
                    logFiles.Add( fullPath );
            }

            cont = logDirObj.GetNext( &filename );
        }

        logFiles.Sort();

        while( logFiles.GetCount() > 20 )
        {
            wxRemoveFile( logFiles[0] );
            logFiles.RemoveAt( 0 );
        }
    }

    return true;
}


bool KIPLATFORM::APP::AttachConsole( bool aTryAlloc )
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::IsOperatingSystemUnsupported()
{
    // Not implemented on this platform
    return false;
}


bool KIPLATFORM::APP::RegisterApplicationRestart( const wxString& aCommandLine )
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::UnregisterApplicationRestart()
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::SupportsShutdownBlockReason()
{
    return false;
}


void KIPLATFORM::APP::RemoveShutdownBlockReason( wxWindow* aWindow )
{
}


void KIPLATFORM::APP::SetShutdownBlockReason( wxWindow* aWindow, const wxString& aReason )
{
}


void KIPLATFORM::APP::ForceTimerMessagesToBeCreatedIfNecessary()
{
}


void KIPLATFORM::APP::AddDynamicLibrarySearchPath( const wxString& aPath )
{
}
