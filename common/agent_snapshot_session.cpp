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

#include <agent_snapshot_session.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>

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
