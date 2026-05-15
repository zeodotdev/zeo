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

#ifndef KICAD_PROJECT_FILE_WATCHER_H
#define KICAD_PROJECT_FILE_WATCHER_H

#include <wx/event.h>
#include <wx/string.h>

#include <memory>


// `wxFileSystemWatcher` may be unavailable in headless / minimal builds.
// The watcher class still compiles in that case but `Start` returns false
// and no events ever fire.
#if wxUSE_FSWATCHER
#include <wx/fswatcher.h>
#endif


class PROJECT_FILE;


/**
 * Watches a single `.kicad_pro` (or any single file) for external
 * modification events and routes them to a `PROJECT_FILE` for
 * reconciliation.
 *
 * Owned by `PROJECT_FILE` via opaque `std::unique_ptr` so the
 * `wxFileSystemWatcher` conditional doesn't leak into headers that
 * include `project_file.h`. Subclasses `wxEvtHandler` so it can be
 * the binding target for the file-system watcher's events.
 *
 * Lifetime: started by `PROJECT_FILE::EnableFileWatcher`, stopped on
 * `DisableFileWatcher` or `PROJECT_FILE` destruction. Restartable —
 * targeting a new path stops any prior watch first.
 *
 * Self-write filter: events whose path was recently `Mark`ed by
 * `PROJECT_FILE_SELF_WRITE_FILTER` are dropped. Phase 3's save guard
 * stamps every successful save; this watcher consults the filter.
 */
class PROJECT_FILE_WATCHER : public wxEvtHandler
{
public:
    explicit PROJECT_FILE_WATCHER( PROJECT_FILE& aOwner );
    ~PROJECT_FILE_WATCHER();

    /**
     * Begin watching `aAbsPath`. Stops any prior watch first.
     *
     * Returns false on platforms without `wxUSE_FSWATCHER`, when no
     * wxApp event loop is available, or when the OS rejects the
     * watch (permissions, missing file, watcher resource limits).
     * Failure is logged via `wxLogTrace("PROJECT_FILE")` but does
     * not throw — callers can ignore the return value if they're
     * happy degrading to "no live updates".
     */
    bool Start( const wxString& aAbsPath );

    /**
     * Stop watching. Idempotent.
     */
    void Stop();

    /**
     * Currently watched absolute path, or empty if not watching.
     */
    const wxString& WatchedPath() const { return m_path; }

private:
#if wxUSE_FSWATCHER
    void onFileSystemEvent( wxFileSystemWatcherEvent& aEvent );

    /// Construct + bind + Add the underlying wxFileSystemWatcher
    /// against the current `m_path`. May be called from `Start` or
    /// from a deferred `CallAfter` continuation when the event loop
    /// wasn't running on the first attempt. Returns true if the
    /// watcher is now active.
    bool attachWatcher();
#endif

    PROJECT_FILE& m_owner;
    wxString      m_path;

#if wxUSE_FSWATCHER
    std::unique_ptr<wxFileSystemWatcher> m_watcher;
#endif
};


#endif   // KICAD_PROJECT_FILE_WATCHER_H
