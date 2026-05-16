/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include <project/project_file_self_write_filter.h>

#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/longlong.h>

#include <map>
#include <mutex>


namespace
{
// Process-wide stamp store. Keyed by normalised absolute path.
// Guarded by `g_mutex` for defensive correctness — callers are
// expected to be on the main thread, but a stray watcher thread
// shouldn't tear the map.
std::map<wxString, wxLongLong> g_stamps;
std::mutex                     g_mutex;


// Normalise to absolute + dots-resolved so equivalent paths
// (./x.kicad_pro, /abs/x.kicad_pro, ./../dir/x.kicad_pro) all key
// the same entry.
wxString canonicalise( const wxString& aPath )
{
    wxFileName fn( aPath );
    fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    return fn.GetFullPath();
}
}   // anonymous namespace


namespace PROJECT_FILE_SELF_WRITE_FILTER
{
void Mark( const wxString& aAbsPath )
{
    if( aAbsPath.IsEmpty() )
        return;

    wxString key = canonicalise( aAbsPath );
    wxLongLong now = wxGetUTCTimeMillis();

    std::lock_guard<std::mutex> lock( g_mutex );
    g_stamps[key] = now;
}


bool RecentlyWrote( const wxString& aAbsPath, int aWindowMs )
{
    if( aAbsPath.IsEmpty() )
        return false;

    wxString key = canonicalise( aAbsPath );
    wxLongLong now = wxGetUTCTimeMillis();

    std::lock_guard<std::mutex> lock( g_mutex );
    auto it = g_stamps.find( key );

    if( it == g_stamps.end() )
        return false;

    return ( now - it->second ).ToLong() <= aWindowMs;
}


void PruneStale( int aRetentionMs )
{
    wxLongLong now = wxGetUTCTimeMillis();

    std::lock_guard<std::mutex> lock( g_mutex );

    for( auto it = g_stamps.begin(); it != g_stamps.end(); )
    {
        if( ( now - it->second ).ToLong() > aRetentionMs )
            it = g_stamps.erase( it );
        else
            ++it;
    }
}


void Clear()
{
    std::lock_guard<std::mutex> lock( g_mutex );
    g_stamps.clear();
}
} // namespace PROJECT_FILE_SELF_WRITE_FILTER
