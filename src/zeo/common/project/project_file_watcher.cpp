/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "project_file_watcher.h"

#include <project/project_file.h>
#include <project/project_file_self_write_filter.h>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/log.h>


PROJECT_FILE_WATCHER::PROJECT_FILE_WATCHER( PROJECT_FILE& aOwner ) :
        m_owner( aOwner )
{
}


PROJECT_FILE_WATCHER::~PROJECT_FILE_WATCHER()
{
    Stop();
}


bool PROJECT_FILE_WATCHER::Start( const wxString& aAbsPath )
{
    Stop();

    if( aAbsPath.IsEmpty() )
        return false;

    // Stash the path immediately so callers see the right WatchedPath
    // even if the actual `Add()` is deferred (see below). Cleared
    // again if the deferred attach gives up.
    m_path = aAbsPath;

#if wxUSE_FSWATCHER
    if( !attachWatcher() )
    {
        // Likely cause: wxApp event loop not yet running (typical when
        // PROJECT_FILE is loaded from wxApp::OnInit, before OnRun
        // enters the loop). On macOS the FSEvents backend needs an
        // active CFRunLoop; without it Add() returns false. Defer
        // to the next idle tick — by then the loop is alive and the
        // attach succeeds.
        if( wxTheApp )
        {
            wxLogTrace( wxT( "PROJECT_FILE" ),
                        wxT( "PROJECT_FILE_WATCHER::Start: deferring attach for "
                             "'%s' (event loop not yet running?)" ),
                        aAbsPath );

            wxTheApp->CallAfter( [this]()
            {
                if( m_path.IsEmpty() )
                    return;   // Stop() ran between schedule and dispatch

                if( !attachWatcher() )
                {
                    wxLogTrace( wxT( "PROJECT_FILE" ),
                                wxT( "PROJECT_FILE_WATCHER::Start deferred attach "
                                     "still failed for '%s'" ),
                                m_path );
                    m_path.Clear();
                }
            } );

            // Tell the caller "we're trying" — the deferred path will
            // log success / failure on the next idle.
            return true;
        }

        // No wxApp at all (headless / CLI). Genuine failure.
        m_path.Clear();
        return false;
    }

    return true;
#else
    wxLogTrace( wxT( "PROJECT_FILE" ),
                wxT( "PROJECT_FILE_WATCHER::Start: wxUSE_FSWATCHER unavailable, no live "
                     "updates for '%s'" ),
                aAbsPath );
    m_path.Clear();
    return false;
#endif
}


#if wxUSE_FSWATCHER
bool PROJECT_FILE_WATCHER::attachWatcher()
{
    // Construct lazily so PROJECT_FILEs that never enable watching
    // don't pay the kernel-resource cost.
    if( !m_watcher )
    {
        m_watcher = std::make_unique<wxFileSystemWatcher>();

        // Bind before adding the path so the very first kernel event
        // is delivered to us, not the default (no-op) handler.
        m_watcher->Bind( wxEVT_FSWATCHER, &PROJECT_FILE_WATCHER::onFileSystemEvent, this );
    }

    wxFileName fn( m_path );

    if( !fn.IsOk() )
    {
        wxLogTrace( wxT( "PROJECT_FILE" ),
                    wxT( "PROJECT_FILE_WATCHER::attachWatcher invalid path '%s'" ),
                    m_path );
        m_watcher.reset();
        return false;
    }

    // Watch the parent directory and filter events by filename in our
    // handler. wxFileSystemWatcher::Add(file) is supported on most
    // backends but coalescing is platform-specific; watching the
    // directory + filtering is more uniform across macOS FSEvents,
    // inotify, and ReadDirectoryChangesW.
    if( !m_watcher->Add( wxFileName( fn.GetPath() ), wxFSW_EVENT_MODIFY ) )
    {
        wxLogTrace( wxT( "PROJECT_FILE" ),
                    wxT( "PROJECT_FILE_WATCHER::attachWatcher could not add watch on '%s'" ),
                    fn.GetPath() );
        m_watcher.reset();
        return false;
    }

    wxLogTrace( wxT( "PROJECT_FILE" ),
                wxT( "PROJECT_FILE_WATCHER::Start watching '%s'" ), m_path );

    return true;
}
#endif


void PROJECT_FILE_WATCHER::Stop()
{
    m_path.Clear();

#if wxUSE_FSWATCHER
    if( m_watcher )
    {
        m_watcher->Unbind( wxEVT_FSWATCHER,
                           &PROJECT_FILE_WATCHER::onFileSystemEvent, this );
        m_watcher.reset();
    }
#endif
}


#if wxUSE_FSWATCHER
void PROJECT_FILE_WATCHER::onFileSystemEvent( wxFileSystemWatcherEvent& aEvent )
{
    // We watch the parent directory — filter to our specific file.
    wxFileName eventPath = aEvent.GetPath();
    wxFileName watched( m_path );

    eventPath.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    watched.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

    if( eventPath.GetFullPath() != watched.GetFullPath() )
        return;

    // Drop modify events that originated from our own SaveToFile.
    // Phase 3 stamps the self-write filter on every successful save.
    if( PROJECT_FILE_SELF_WRITE_FILTER::RecentlyWrote( m_path ) )
        return;

    // We only care about MODIFY for now. CREATE / DELETE / RENAME
    // would need a different reconciliation strategy (orphan the
    // PROJECT_FILE? track-rename?) — out of scope for this phase.
    int change = aEvent.GetChangeType();

    if( !( change & wxFSW_EVENT_MODIFY ) )
        return;

    // Hand off to PROJECT_FILE for reload + observer dispatch. Runs
    // on the wxApp main thread (fswatcher events are posted to the
    // event loop), so PROJECT_FILE state mutation is single-threaded.
    m_owner.OnExternalFileChange();
}
#endif
