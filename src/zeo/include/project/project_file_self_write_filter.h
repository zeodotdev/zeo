/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef KICAD_PROJECT_FILE_SELF_WRITE_FILTER_H
#define KICAD_PROJECT_FILE_SELF_WRITE_FILTER_H

#include <kicommon.h>
#include <wx/string.h>


/**
 * Process-wide registry of "we just wrote this file" stamps.
 *
 * Bridges T3 Phase 3 (PROJECT_FILE save guard) and Phase 4 (file
 * watcher). Every successful `PROJECT_FILE::SaveToFile` calls `Mark`
 * with the absolute path of the file it just wrote; the watcher in
 * Phase 4 calls `RecentlyWrote` before processing a modification
 * event and drops the event if our save is the cause.
 *
 * Stamps live for a short window (default 500 ms; tunable per call
 * via `RecentlyWrote(path, windowMs)`). Older entries are eventually
 * pruned by `PruneStale`, which the watcher should call on a 1–2 s
 * timer.
 *
 * Thread safety: callers are expected to operate on the wxWidgets
 * main thread. The implementation is a `std::map` guarded by a
 * `std::mutex` for defensive correctness in case anyone hooks a
 * watcher to a worker thread.
 *
 * Path canonicalisation: `Mark` and `RecentlyWrote` both resolve
 * their argument via `wxFileName::Normalize(wxPATH_NORM_ABSOLUTE)`
 * before comparing, so a `Mark("/abs/x.kicad_pro")` matches a
 * `RecentlyWrote("./x.kicad_pro")` from the same directory.
 */
namespace PROJECT_FILE_SELF_WRITE_FILTER
{
/**
 * Record that we just wrote to `aAbsPath`. Subsequent watcher events
 * for that file within the filter window are attributable to us.
 */
KICOMMON_API void Mark( const wxString& aAbsPath );

/**
 * Did we write to `aAbsPath` within the last `aWindowMs` ms?
 *
 * Default window is 500 ms — long enough to cover the typical
 * "save then watcher fires" lag on macOS / Linux / Windows native
 * filesystems, short enough that an external write within the same
 * second still propagates.
 *
 * Network mounts may need a larger window; raise per-call if you're
 * watching one.
 */
KICOMMON_API bool RecentlyWrote( const wxString& aAbsPath, int aWindowMs = 500 );

/**
 * Drop entries older than the given retention. Safe to call on a
 * timer. Default retention is 2000 ms — comfortably above the typical
 * watcher latency window.
 */
KICOMMON_API void PruneStale( int aRetentionMs = 2000 );

/**
 * Clear all entries. Test-only.
 */
KICOMMON_API void Clear();
} // namespace PROJECT_FILE_SELF_WRITE_FILTER


#endif   // KICAD_PROJECT_FILE_SELF_WRITE_FILTER_H
