/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include <agent_snapshot_session.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

#include <vector>


AGENT_SNAPSHOT_SESSION::AGENT_SNAPSHOT_SESSION()
{
}


AGENT_SNAPSHOT_SESSION::~AGENT_SNAPSHOT_SESSION()
{
    cleanupTempDir();
}


bool AGENT_SNAPSHOT_SESSION::CreateTempDir()
{
    if( !m_snapshotDir.IsEmpty() )
        return true;  // Already created

#ifdef _WIN32
    wchar_t tempPath[MAX_PATH];
    GetTempPathW( MAX_PATH, tempPath );

    LARGE_INTEGER counter;
    QueryPerformanceCounter( &counter );

    wxString dirPath = wxString( tempPath ) + wxString::Format( "zeo_snapshot_%lld",
                                                                 counter.QuadPart );

    if( !wxFileName::Mkdir( dirPath, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
    {
        wxLogWarning( "AGENT_SNAPSHOT_SESSION: Failed to create temp directory" );
        return false;
    }

    m_snapshotDir = dirPath;
#else
    std::string tempTemplate = ( wxFileName::GetTempDir() + wxFileName::GetPathSeparator()
                                 + "zeo_snapshot_XXXXXX" ).ToStdString();
    std::vector<char> tempBuf( tempTemplate.begin(), tempTemplate.end() );
    tempBuf.push_back( '\0' );

    if( !mkdtemp( tempBuf.data() ) )
    {
        wxLogWarning( "AGENT_SNAPSHOT_SESSION: Failed to create temp directory" );
        return false;
    }

    m_snapshotDir = wxString::FromUTF8( tempBuf.data() );
#endif

    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Created temp dir: %s", m_snapshotDir );
    return true;
}


void AGENT_SNAPSHOT_SESSION::RegisterSchematicSnapshot( const wxString& aOriginalPath,
                                                        const wxString& aSnapshotPath )
{
    m_schSnapshotPaths[aOriginalPath] = aSnapshotPath;
    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Registered schematic snapshot: %s -> %s",
               aOriginalPath, aSnapshotPath );
}


void AGENT_SNAPSHOT_SESSION::RegisterPcbSnapshot( const wxString& aSnapshotPath )
{
    m_pcbSnapshotPath = aSnapshotPath;
    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Registered PCB snapshot: %s", aSnapshotPath );
}


void AGENT_SNAPSHOT_SESSION::FinalizeSnapshot()
{
    m_state = State::SNAPSHOT_TAKEN;
    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Snapshot finalized (%zu schematic sheets, PCB=%s)",
               m_schSnapshotPaths.size(),
               m_pcbSnapshotPath.IsEmpty() ? "no" : "yes" );
}


void AGENT_SNAPSHOT_SESSION::SetChangesDetected()
{
    if( m_state == State::SNAPSHOT_TAKEN )
    {
        m_state = State::CHANGES_PENDING;
        wxLogInfo( "AGENT_SNAPSHOT_SESSION: Changes detected, state -> CHANGES_PENDING" );
    }
}


wxString AGENT_SNAPSHOT_SESSION::GetSnapshotPathForSheet( const wxString& aOriginalPath ) const
{
    auto it = m_schSnapshotPaths.find( aOriginalPath );
    if( it != m_schSnapshotPaths.end() )
        return it->second;

    return wxEmptyString;
}


std::set<wxString> AGENT_SNAPSHOT_SESSION::GetSnapshotOriginalPaths() const
{
    std::set<wxString> paths;
    for( const auto& [origPath, snapPath] : m_schSnapshotPaths )
        paths.insert( origPath );

    return paths;
}


void AGENT_SNAPSHOT_SESSION::SetAfterPath( const wxString& aPath )
{
    m_afterFilePath = aPath;
}


void AGENT_SNAPSHOT_SESSION::DiscardSnapshot()
{
    cleanupTempDir();
    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Snapshot discarded" );
}


void AGENT_SNAPSHOT_SESSION::EndSession()
{
    cleanupTempDir();

    m_state = State::IDLE;
    m_schSnapshotPaths.clear();
    m_pcbSnapshotPath.Clear();
    m_afterFilePath.Clear();

    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Session ended" );
}


void AGENT_SNAPSHOT_SESSION::cleanupTempDir()
{
    // Clean up "after" temp file if it exists outside the snapshot dir
    if( !m_afterFilePath.IsEmpty() && wxFileName::FileExists( m_afterFilePath ) )
    {
        if( !m_snapshotDir.IsEmpty() && !m_afterFilePath.StartsWith( m_snapshotDir ) )
            wxRemoveFile( m_afterFilePath );
    }
    m_afterFilePath.Clear();

    if( m_snapshotDir.IsEmpty() )
        return;

    // Remove all files in the temp directory, then the directory itself
    wxDir dir( m_snapshotDir );
    if( dir.IsOpened() )
    {
        wxString entry;
        bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
        while( cont )
        {
            wxRemoveFile( m_snapshotDir + wxFileName::GetPathSeparator() + entry );
            cont = dir.GetNext( &entry );
        }
    }

    wxRmdir( m_snapshotDir );
    wxLogInfo( "AGENT_SNAPSHOT_SESSION: Cleaned up temp dir: %s", m_snapshotDir );
    m_snapshotDir.Clear();
}
