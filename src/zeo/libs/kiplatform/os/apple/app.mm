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
#include <wx/tokenzr.h>
#include <wx/utils.h>

// Global log file - must persist for lifetime of app
static wxFFile* s_logFile = nullptr;

// Custom log target that timestamps every line (not just the first) and writes
// to file, stderr, or both.  Overrides DoLogRecord to bypass wxLog's default
// single-line timestamp formatting.
class wxLogTimestamped : public wxLog
{
public:
    wxLogTimestamped( FILE* logFile, bool alsoStderr )
        : m_logFile( logFile ), m_alsoStderr( alsoStderr ) {}

protected:
    void DoLogRecord( wxLogLevel level, const wxString& msg,
                      const wxLogRecordInfo& info ) override
    {
        // Build HH:MM:SS.mmm timestamp from the record's epoch time
        wxDateTime dt( (time_t) info.timestamp );
        wxDateTime now = wxDateTime::UNow();
        int ms = now.GetMillisecond();
        wxString ts = dt.Format( wxS( "%H:%M:%S" ) )
                    + wxString::Format( wxS( ".%03d" ), ms );
        const wxScopedCharBuffer tsUtf8 = ts.utf8_str();

        // Split message into lines — every line gets the timestamp
        wxStringTokenizer tokenizer( msg, wxS( "\n" ), wxTOKEN_RET_EMPTY );

        while( tokenizer.HasMoreTokens() )
        {
            wxString line = tokenizer.GetNextToken();
            const wxScopedCharBuffer lineUtf8 = line.utf8_str();

            if( m_logFile )
                fprintf( m_logFile, "%s %s\n", tsUtf8.data(), lineUtf8.data() );

            if( m_alsoStderr )
                fprintf( stderr, "%s %s\n", tsUtf8.data(), lineUtf8.data() );
        }

        if( m_logFile )
            fflush( m_logFile );
    }

private:
    FILE* m_logFile;
    bool  m_alsoStderr;
};


bool KIPLATFORM::APP::Init()
{
    // KiCad relies on showing the file type selector in a few places; force it to be shown
    wxSystemOptions::SetOption( wxS( "osx.openfiledialog.always-show-types" ), 1 );

    // Set up logging
    wxLog::EnableLogging( true );
    wxLog::SetLogLevel( wxLOG_Trace );

    // Always log to file
    wxString logDir = wxFileName::GetHomeDir() + wxS( "/Library/Logs/Zeo" );

    if( !wxDir::Exists( logDir ) )
        wxFileName::Mkdir( logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    wxString timestamp = wxDateTime::Now().Format( wxS( "%Y-%m-%d-%H%M%S" ) );
    wxString logPath = logDir + wxS( "/agent-" ) + timestamp + wxS( ".log" );

    s_logFile = new wxFFile( logPath, wxS( "w" ) );

    // Check if WXTRACE is set for stderr output (development mode)
    wxString traceVars;
    bool useStderr = wxGetEnv( wxS( "WXTRACE" ), &traceVars ) && !traceVars.empty();

    if( s_logFile->IsOpened() )
    {
        wxLog::SetActiveTarget( new wxLogTimestamped( s_logFile->fp(), useStderr ) );
    }

    // Enable the "Agent" trace mask so wxLogTrace("Agent", ...) calls produce output
    wxLog::AddTraceMask( wxS( "Agent" ) );

    wxLogInfo( wxS( "Zeo session started, logging to %s" ), logPath );

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
