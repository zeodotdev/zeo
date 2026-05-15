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

#ifndef AGENT_SNAPSHOT_SESSION_H
#define AGENT_SNAPSHOT_SESSION_H

#include <map>
#include <set>
#include <wx/string.h>


/**
 * AGENT_SNAPSHOT_SESSION manages whole-file snapshots for agent change tracking.
 *
 * Before agent tool execution, the editor serializes the schematic/PCB state
 * to temp files via this session. This provides a clean "before" state for:
 *   - Diff viewing (compare snapshot to current in-memory state)
 *   - Rejection (reload from snapshot to revert all agent changes)
 *   - Acceptance (simply discard the snapshot)
 *
 * This class manages the temp directory lifecycle and file path bookkeeping.
 * The actual serialization/deserialization is performed by the editor frames
 * (SCH_EDIT_FRAME, PCB_EDIT_FRAME) which have access to the IO plugins.
 */
class AGENT_SNAPSHOT_SESSION
{
public:
    enum class State
    {
        IDLE,               ///< No snapshot taken
        SNAPSHOT_TAKEN,     ///< Snapshot exists, no changes detected yet
        CHANGES_PENDING     ///< Snapshot exists and agent has made changes
    };

    AGENT_SNAPSHOT_SESSION();
    ~AGENT_SNAPSHOT_SESSION();

    // --- Temp directory management ---

    /**
     * Create the temp directory for storing snapshot files.
     * Must be called before registering any snapshot paths.
     * @return true if directory was created successfully.
     */
    bool CreateTempDir();

    /**
     * Get the temp directory path. Empty if not yet created.
     */
    const wxString& GetSnapshotDir() const { return m_snapshotDir; }

    // --- Snapshot path registration ---

    /**
     * Register a schematic sheet snapshot: maps original file path to temp path.
     * Called by the editor after it serializes a sheet to the temp directory.
     *
     * @param aOriginalPath The original on-disk filename of the sheet.
     * @param aSnapshotPath The temp file path where the snapshot was saved.
     */
    void RegisterSchematicSnapshot( const wxString& aOriginalPath,
                                    const wxString& aSnapshotPath );

    /**
     * Register a PCB board snapshot path.
     * Called by the editor after it serializes the board to the temp directory.
     *
     * @param aSnapshotPath The temp file path where the snapshot was saved.
     */
    void RegisterPcbSnapshot( const wxString& aSnapshotPath );

    /**
     * Mark the snapshot as complete and transition to SNAPSHOT_TAKEN state.
     */
    void FinalizeSnapshot();

    // --- State queries ---

    State GetState() const { return m_state; }
    bool HasSnapshot() const { return m_state != State::IDLE; }
    bool HasChanges() const { return m_state == State::CHANGES_PENDING; }

    /**
     * Mark that agent modifications have been detected since the snapshot.
     */
    void SetChangesDetected();

    // --- Snapshot file access ---

    /**
     * Get the snapshot (before) path for a specific sheet's original file path.
     *
     * @param aOriginalPath The original on-disk filename of the sheet.
     * @return The temp file path, or empty if not found.
     */
    wxString GetSnapshotPathForSheet( const wxString& aOriginalPath ) const;

    /**
     * Get the snapshot path for the PCB board.
     */
    const wxString& GetPcbSnapshotPath() const { return m_pcbSnapshotPath; }

    /**
     * Get the set of all original file paths that were snapshotted.
     */
    std::set<wxString> GetSnapshotOriginalPaths() const;

    /**
     * Get all snapshot path mappings (original -> temp).
     */
    const std::map<wxString, wxString>& GetSchematicSnapshotPaths() const
    {
        return m_schSnapshotPaths;
    }

    // --- After-state file management ---

    /**
     * Set the "after" state temp file path.
     * Called by the editor after serializing current in-memory state for the diff viewer.
     */
    void SetAfterPath( const wxString& aPath );

    /**
     * Get the "after" state temp file path (most recently serialized).
     */
    const wxString& GetAfterPath() const { return m_afterFilePath; }

    // --- Session lifecycle ---

    /**
     * Discard all snapshot and after-state temp files.
     * Called on accept — changes are kept, snapshots no longer needed.
     */
    void DiscardSnapshot();

    /**
     * End the session entirely — discard files and reset to IDLE.
     */
    void EndSession();

private:
    void cleanupTempDir();

    State    m_state = State::IDLE;
    wxString m_snapshotDir;     ///< mkdtemp-created temp directory

    /// Map: original file path -> snapshot temp file path (schematic sheets)
    std::map<wxString, wxString> m_schSnapshotPaths;

    /// Snapshot path for PCB board
    wxString m_pcbSnapshotPath;

    /// Serialized "after" state temp file for diff viewer
    wxString m_afterFilePath;
};

#endif // AGENT_SNAPSHOT_SESSION_H
