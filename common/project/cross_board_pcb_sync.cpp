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

#include <project/cross_board_pcb_sync.h>

#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/regex.h>

#include <map>
#include <string>


namespace
{

/**
 * Scan forward from `aStart` (which must point AT the opening `(`) to find
 * the matching close paren, skipping over quoted strings. Returns npos if
 * imbalanced.
 */
size_t findMatchingClose( const wxString& aText, size_t aStart )
{
    int  depth    = 0;
    bool inString = false;

    for( size_t i = aStart; i < aText.length(); ++i )
    {
        wxChar c = aText[i];

        if( inString )
        {
            if( c == '\\' && i + 1 < aText.length() )
            {
                ++i;
                continue;
            }

            if( c == '"' )
                inString = false;
        }
        else
        {
            if( c == '"' )
                inString = true;
            else if( c == '(' )
                ++depth;
            else if( c == ')' )
            {
                --depth;

                if( depth == 0 )
                    return i;
            }
        }
    }

    return wxString::npos;
}


/**
 * Locate the block `(footprint ... (property "Reference" "<aRef>" ...) ...)`.
 * Search starts at `aSearchFrom`. Returns { begin, endExclusive } or
 * { npos, npos } if not found.
 */
std::pair<size_t, size_t> findFootprintBlock( const wxString& aText, const wxString& aRef,
                                              size_t aSearchFrom )
{
    const wxString fpOpen = wxT( "(footprint" );
    size_t         pos    = aSearchFrom;

    while( pos < aText.length() )
    {
        size_t fpStart = aText.find( fpOpen, pos );

        if( fpStart == wxString::npos )
            return { wxString::npos, wxString::npos };

        size_t fpEnd = findMatchingClose( aText, fpStart );

        if( fpEnd == wxString::npos )
            return { wxString::npos, wxString::npos };

        wxString block = aText.Mid( fpStart, fpEnd - fpStart + 1 );

        wxRegEx refRe( wxT( "\\(property\\s+\"Reference\"\\s+\"([^\"]*)\"" ),
                       wxRE_DEFAULT );

        if( refRe.IsValid() && refRe.Matches( block )
            && refRe.GetMatch( block, 1 ) == aRef )
        {
            return { fpStart, fpEnd + 1 };
        }

        pos = fpEnd + 1;
    }

    return { wxString::npos, wxString::npos };
}


/**
 * Find the `(pad "<aPadNum>" ...)` block within the given [begin,end) slice.
 * Returns { padStart, padEndExclusive } or { npos, npos }.
 */
std::pair<size_t, size_t> findPadBlock( const wxString& aText, const wxString& aPadNum,
                                        size_t aBlockBegin, size_t aBlockEnd )
{
    // Match "(pad "<num>"" — the closing quote + space guards against
    // matching "12" when looking for "1".
    wxString needle = wxT( "(pad \"" ) + aPadNum + wxT( "\"" );
    size_t   pos    = aBlockBegin;

    while( pos < aBlockEnd )
    {
        size_t padStart = aText.find( needle, pos );

        if( padStart == wxString::npos || padStart >= aBlockEnd )
            return { wxString::npos, wxString::npos };

        size_t padEnd = findMatchingClose( aText, padStart );

        if( padEnd == wxString::npos || padEnd >= aBlockEnd )
            return { wxString::npos, wxString::npos };

        return { padStart, padEnd + 1 };
    }

    return { wxString::npos, wxString::npos };
}


/**
 * Within the pad block [begin,end), either replace the existing
 * `(net "...")` token with the target name or insert a new one just before
 * the pad's closing paren. Returns the (possibly-updated) end offset of the
 * pad block so the caller can continue after it.
 */
size_t writePadNet( wxString& aText, size_t aPadBegin, size_t aPadEnd,
                    const wxString& aNetName )
{
    // Escape any " or \ in the net name for s-expression embedding.
    wxString escaped = aNetName;
    escaped.Replace( wxT( "\\" ), wxT( "\\\\" ) );
    escaped.Replace( wxT( "\"" ), wxT( "\\\"" ) );

    wxString newEntry = wxString::Format( wxT( "(net \"%s\")" ), escaped );

    wxRegEx netRe( wxT( "\\(net\\s+\"[^\"]*\"\\)" ), wxRE_DEFAULT );

    if( !netRe.IsValid() )
        return aPadEnd;

    wxString block = aText.Mid( aPadBegin, aPadEnd - aPadBegin );

    if( netRe.Matches( block ) )
    {
        size_t matchStart, matchLen;
        netRe.GetMatch( &matchStart, &matchLen, 0 );
        aText.replace( aPadBegin + matchStart, matchLen, newEntry );
        return aPadBegin + block.length() + ( newEntry.length() - matchLen );
    }

    // No existing net — insert before the pad's closing paren, matching
    // the block's indentation style (tab-indented).
    wxString insertText = wxT( "\n\t\t\t" ) + newEntry;
    aText.insert( aPadEnd - 1, insertText );
    return aPadEnd + insertText.length();
}


/**
 * Heuristic: is this net name one that should be overridden by a meaningful
 * name from another source? True for:
 *   - empty
 *   - KiCad auto-generated "Net-(<Ref>-Pad<N>)"
 *   - Explicitly unconnected "unconnected-*"
 *   - Our own auto-generated "Net-<8-hex-digits>"
 */
bool isAutoGeneratedNetName( const wxString& aName )
{
    if( aName.IsEmpty() )
        return true;

    if( aName.StartsWith( wxT( "Net-(" ) ) )
        return true;

    if( aName.StartsWith( wxT( "unconnected-" ) ) )
        return true;

    if( aName.StartsWith( wxT( "Net-" ) ) && aName.length() == 12 )
    {
        for( size_t i = 4; i < 12; ++i )
        {
            wxChar c = aName[i];

            if( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' )
                   || ( c >= 'A' && c <= 'F' ) ) )
                return false;
        }

        return true;
    }

    return false;
}


/**
 * Scan a .kicad_pcb text for current `(net "...")` assignments on connector
 * pads. Returns map from (ref, padNumber) to the current net name.
 */
std::map<std::pair<wxString, wxString>, wxString>
scanPadCurrentNets( const wxString& aText )
{
    std::map<std::pair<wxString, wxString>, wxString> out;

    const wxString fpOpen = wxT( "(footprint" );
    size_t         pos    = 0;

    wxRegEx refRe( wxT( "\\(property\\s+\"Reference\"\\s+\"([^\"]*)\"" ), wxRE_DEFAULT );
    wxRegEx padRe( wxT( "\\(pad\\s+\"([^\"]*)\"" ), wxRE_DEFAULT );
    wxRegEx netRe( wxT( "\\(net\\s+\"([^\"]*)\"\\)" ), wxRE_DEFAULT );

    if( !refRe.IsValid() || !padRe.IsValid() || !netRe.IsValid() )
        return out;

    while( pos < aText.length() )
    {
        size_t fpStart = aText.find( fpOpen, pos );

        if( fpStart == wxString::npos )
            break;

        size_t fpEnd = findMatchingClose( aText, fpStart );

        if( fpEnd == wxString::npos )
            break;

        wxString fpBlock = aText.Mid( fpStart, fpEnd - fpStart + 1 );
        pos = fpEnd + 1;

        if( !refRe.Matches( fpBlock ) )
            continue;

        wxString ref = refRe.GetMatch( fpBlock, 1 );

        // Walk pads inside this footprint.
        size_t scan = 0;

        while( scan < fpBlock.length() )
        {
            wxString remaining = fpBlock.Mid( scan );

            if( !padRe.Matches( remaining ) )
                break;

            size_t padRelStart, padRelLen;
            padRe.GetMatch( &padRelStart, &padRelLen, 0 );

            size_t padAbsStart = scan + padRelStart;
            size_t padAbsEnd   = findMatchingClose( fpBlock, padAbsStart );

            if( padAbsEnd == wxString::npos )
                break;

            wxString padBlock = fpBlock.Mid( padAbsStart, padAbsEnd - padAbsStart + 1 );
            wxString padNum   = padRe.GetMatch( remaining, 1 );

            if( netRe.Matches( padBlock ) )
                out[{ ref, padNum }] = netRe.GetMatch( padBlock, 1 );
            else
                out[{ ref, padNum }] = wxEmptyString;

            scan = padAbsEnd + 1;
        }
    }

    return out;
}


/**
 * Pre-pass: for each cross-board net, if any endpoint's pad already carries
 * a meaningful (non-auto-generated) local net name on its sub-project PCB,
 * adopt that name as the net's canonical name. Two meaningful names on the
 * same net = a conflict — pick alphabetically first as the deterministic
 * winner and record the ignored alternatives in `aConflictsOut` so the
 * caller can surface them in the sync summary.
 *
 * Mutates aProject in place. Returns the number of nets renamed.
 */
int resolveCrossBoardNetNames( PROJECT_FILE& aProject,
                               std::vector<MB_NET_NAME_CONFLICT>& aConflictsOut,
                               bool aDryRun )
{
    // Cache per-sub-project pad nets so we don't re-parse each PCB per endpoint.
    std::map<KIID, std::map<std::pair<wxString, wxString>, wxString>> perSub;

    for( const SUB_PROJECT_INFO& info : aProject.GetSubProjects() )
    {
        wxFileName proFile = aProject.ResolveSubProjectPath( info );
        wxFileName pcbFile = proFile;
        pcbFile.SetExt( wxT( "kicad_pcb" ) );

        if( !pcbFile.FileExists() )
            continue;

        wxFFile in( pcbFile.GetFullPath(), wxT( "r" ) );

        if( !in.IsOpened() )
            continue;

        wxString text;

        if( !in.ReadAll( &text ) )
        {
            in.Close();
            continue;
        }

        in.Close();

        perSub[info.uuid] = scanPadCurrentNets( text );
    }

    std::vector<MB_CROSS_BOARD_NET> nets = aProject.GetCrossBoardNets();
    int renamed = 0;

    for( MB_CROSS_BOARD_NET& net : nets )
    {
        // Only override the auto-generated placeholder; if the MBS already
        // has a meaningful name (from a label), respect it.
        if( !isAutoGeneratedNetName( net.name ) )
            continue;

        // Collect all meaningful endpoint names, dedup, sort alphabetically.
        std::set<wxString> candidates;

        for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
        {
            auto subIt = perSub.find( ep.subProjectUuid );

            if( subIt == perSub.end() )
                continue;

            auto padIt = subIt->second.find( { ep.componentRef, ep.pinNumber } );

            if( padIt == subIt->second.end() )
                continue;

            if( !isAutoGeneratedNetName( padIt->second ) )
                candidates.insert( padIt->second );
        }

        if( candidates.empty() )
            continue;

        // std::set is ordered → first entry is alphabetically first.
        wxString chosen = *candidates.begin();

        if( candidates.size() > 1 )
        {
            MB_NET_NAME_CONFLICT conflict;
            conflict.chosen = chosen;

            for( auto it = std::next( candidates.begin() ); it != candidates.end(); ++it )
                conflict.rejected.push_back( *it );

            aConflictsOut.push_back( std::move( conflict ) );
        }

        if( chosen != net.name )
        {
            net.name = chosen;
            renamed++;
        }
    }

    // Dry-run mode (preview pass): leave the on-disk + in-memory state
    // alone. The caller will re-invoke with aDryRun=false to commit. Without
    // this guard, opening the sync dialog and cancelling would silently
    // overwrite auto-generated names with sub-project local names — a
    // user-visible mutation with no apply click.
    if( !aDryRun )
        aProject.SetCrossBoardNets( std::move( nets ) );

    return renamed;
}


/**
 * Apply all endpoints targeting a single .kicad_pcb file. Returns stats.
 *
 * @param aDryRun         When true, performs every read + per-pad lookup,
 *                        records descriptive preview lines, but skips the
 *                        file write at the end. Stats still reflect what
 *                        would have been applied.
 * @param aSubProjectName Display name attached to every preview line
 *                        produced for this sub-project.
 * @param aLinesOut       Receives info lines for each pad assignment, a
 *                        warning line for each bulk net rename (the
 *                        whole-file string replace is the risky operation
 *                        we want the user to confirm before applying), and
 *                        error lines for missing footprints / pads.
 */
std::pair<int, int> applyEndpointsToOnePcb(
        const wxFileName& aPcbFile,
        const std::vector<std::pair<MB_CROSS_BOARD_NET_ENDPOINT, wxString>>& aEndpointsAndNetNames,
        bool aDryRun,
        const wxString& aSubProjectName,
        std::vector<MB_SYNC_PREVIEW_LINE>& aLinesOut )
{
    int applied = 0;
    int missing = 0;

    auto pushLine = [&]( const wxString& aText, MB_SYNC_PREVIEW_LINE::SEVERITY aSeverity )
    {
        MB_SYNC_PREVIEW_LINE line;
        line.subProjectDisplayName = aSubProjectName;
        line.text                  = aText;
        line.severity              = aSeverity;
        aLinesOut.push_back( std::move( line ) );
    };

    if( !aPcbFile.FileExists() )
    {
        pushLine( wxString::Format( _( "PCB file not found: %s" ),
                                    aPcbFile.GetFullPath() ),
                  MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
        return { applied, (int) aEndpointsAndNetNames.size() };
    }

    wxFFile in( aPcbFile.GetFullPath(), wxT( "r" ) );

    if( !in.IsOpened() )
    {
        pushLine( wxString::Format( _( "Cannot open PCB for read: %s" ),
                                    aPcbFile.GetFullPath() ),
                  MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
        return { applied, (int) aEndpointsAndNetNames.size() };
    }

    wxString text;

    if( !in.ReadAll( &text ) )
    {
        in.Close();
        pushLine( wxString::Format( _( "Read failed on PCB: %s" ),
                                    aPcbFile.GetFullPath() ),
                  MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
        return { applied, (int) aEndpointsAndNetNames.size() };
    }

    in.Close();

    // Group endpoints by componentRef so we locate each footprint once.
    std::map<wxString, std::vector<std::pair<wxString, wxString>>> byFootprint;

    for( const auto& [ep, netName] : aEndpointsAndNetNames )
    {
        if( ep.componentRef.IsEmpty() || ep.pinNumber.IsEmpty() )
        {
            missing++;
            continue;
        }

        byFootprint[ep.componentRef].emplace_back( ep.pinNumber, netName );
    }

    // First pass: rename each directly-named pad; record (oldNet, newNet)
    // so we can propagate to other pads that shared the old net below.
    std::map<wxString, wxString> renameMap;
    wxRegEx netRe( wxT( "\\(net\\s+\"([^\"]*)\"\\)" ), wxRE_DEFAULT );

    for( const auto& [ref, padList] : byFootprint )
    {
        auto [fpBegin, fpEnd] = findFootprintBlock( text, ref, 0 );

        if( fpBegin == wxString::npos )
        {
            missing += padList.size();
            pushLine( wxString::Format( _( "Footprint '%s' not found on this PCB "
                                            "(%zu endpoint(s) skipped)" ),
                                        ref, padList.size() ),
                      MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
            continue;
        }

        for( const auto& [padNum, netName] : padList )
        {
            auto [padBegin, padEnd] = findPadBlock( text, padNum, fpBegin, fpEnd );

            if( padBegin == wxString::npos )
            {
                missing++;
                pushLine( wxString::Format( _( "Pad %s.%s not found "
                                                "(endpoint for net '%s')" ),
                                            ref, padNum, netName ),
                          MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
                continue;
            }

            // Capture the pad's current net so we can propagate the rename
            // to other pads that share it (e.g. USB-C A6/B6 pair pads).
            wxString padBlock = text.Mid( padBegin, padEnd - padBegin );
            wxString oldNet;

            if( netRe.IsValid() && netRe.Matches( padBlock ) )
                oldNet = netRe.GetMatch( padBlock, 1 );

            size_t newPadEnd = writePadNet( text, padBegin, padEnd, netName );

            // Adjust fpEnd for any text length delta introduced by the edit.
            int delta = (int) newPadEnd - (int) padEnd;
            fpEnd = (size_t) ( (int) fpEnd + delta );
            applied++;

            if( oldNet.IsEmpty() )
            {
                pushLine( wxString::Format( _( "Set %s.%s net = '%s' (was unset)" ),
                                            ref, padNum, netName ),
                          MB_SYNC_PREVIEW_LINE::SEVERITY::INFO );
            }
            else if( oldNet == netName )
            {
                pushLine( wxString::Format( _( "Pad %s.%s already on net '%s' "
                                                "(no change)" ),
                                            ref, padNum, netName ),
                          MB_SYNC_PREVIEW_LINE::SEVERITY::INFO );
            }
            else
            {
                pushLine( wxString::Format( _( "Set %s.%s net: '%s' → '%s'" ),
                                            ref, padNum, oldNet, netName ),
                          MB_SYNC_PREVIEW_LINE::SEVERITY::INFO );
            }

            // Only propagate rename when the old net was *shared* in the sense
            // that it might appear on other pads. Empty + nothing-to-rename.
            // Conflicts: first rename wins per oldNet.
            if( !oldNet.IsEmpty() && oldNet != netName
                && renameMap.find( oldNet ) == renameMap.end() )
            {
                renameMap[oldNet] = netName;
            }
        }
    }

    // Second pass: for every (oldNet, newNet), rewrite remaining
    // (net "oldNet") occurrences AND top-level (net N "oldNet")
    // declarations. Preserves electrical continuity for pads that shared
    // a net with a directly-renamed pad.
    for( const auto& [oldNet, newNet] : renameMap )
    {
        // Escape regex metacharacters in oldNet for safe substitution.
        wxString escaped = oldNet;
        escaped.Replace( wxT( "\\" ), wxT( "\\\\" ) );
        escaped.Replace( wxT( "\"" ), wxT( "\\\"" ) );

        // Whole string replace: `"oldNet"` -> `"newNet"`. The quoted form is
        // specific enough that unrelated matches on the same bytes are
        // extremely unlikely within .kicad_pcb content.
        wxString needle      = wxT( "\"" ) + oldNet + wxT( "\"" );
        wxString replacement = wxT( "\"" ) + newNet + wxT( "\"" );

        // Count occurrences so the preview can convey the blast radius of
        // this bulk replace — this is the "danger" call-out for the user.
        int occurrences = 0;
        size_t pos = 0;

        while( ( pos = text.find( needle, pos ) ) != wxString::npos )
        {
            ++occurrences;
            pos += needle.length();
        }

        text.Replace( needle, replacement );

        pushLine( wxString::Format( _( "Bulk rename '%s' → '%s' (%d occurrence(s) "
                                        "in this PCB)" ),
                                    oldNet, newNet, occurrences ),
                  MB_SYNC_PREVIEW_LINE::SEVERITY::WARNING );
    }

    if( aDryRun )
        return { applied, missing };

    wxFFile out( aPcbFile.GetFullPath(), wxT( "w" ) );

    if( !out.IsOpened() )
    {
        pushLine( wxString::Format( _( "Cannot open PCB for write: %s" ),
                                    aPcbFile.GetFullPath() ),
                  MB_SYNC_PREVIEW_LINE::SEVERITY::ERR );
        return { applied, missing };
    }

    out.Write( text );
    out.Close();

    return { applied, missing };
}


} // anonymous namespace


MB_CROSS_BOARD_SYNC_RESULT
ApplyCrossBoardNetsToSubProjectPCBs( PROJECT_FILE& aProject, bool aDryRun )
{
    MB_CROSS_BOARD_SYNC_RESULT result;

    // Reverse propagation: if any endpoint's current PCB pad already has a
    // meaningful net name, adopt it as the cross-board net's canonical name
    // so every endpoint on every board ends up with the same name. In
    // dry-run mode the in-memory mutation is suppressed; conflicts and
    // rename counts are still computed.
    result.netsRenamed = resolveCrossBoardNetNames( aProject, result.conflicts, aDryRun );

    // Surface every conflict as a project-level warning line so the user
    // sees them at the top of the report panel.
    for( const MB_NET_NAME_CONFLICT& c : result.conflicts )
    {
        wxString rejected;

        for( size_t i = 0; i < c.rejected.size(); ++i )
        {
            if( i > 0 )
                rejected += wxT( ", " );

            rejected += c.rejected[i];
        }

        MB_SYNC_PREVIEW_LINE line;
        line.text = wxString::Format( _( "Naming conflict: chose '%s' "
                                          "(also seen: %s). Place a label on "
                                          "the MBS wire to override." ),
                                      c.chosen, rejected );
        line.severity = MB_SYNC_PREVIEW_LINE::SEVERITY::WARNING;
        result.previewLines.push_back( std::move( line ) );
    }

    // Bucket endpoints by sub-project uuid → list of (endpoint, net name).
    std::map<KIID, std::vector<std::pair<MB_CROSS_BOARD_NET_ENDPOINT, wxString>>> perSub;

    for( const MB_CROSS_BOARD_NET& net : aProject.GetCrossBoardNets() )
    {
        for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
            perSub[ep.subProjectUuid].emplace_back( ep, net.name );
    }

    for( const auto& [uuid, endpoints] : perSub )
    {
        const SUB_PROJECT_INFO* info = aProject.GetSubProject( uuid );

        if( !info )
        {
            result.endpointsMissing += endpoints.size();

            MB_SYNC_PREVIEW_LINE line;
            line.text = wxString::Format(
                    _( "Unknown sub-project UUID — %zu endpoint(s) skipped" ),
                    endpoints.size() );
            line.severity = MB_SYNC_PREVIEW_LINE::SEVERITY::ERR;
            result.previewLines.push_back( std::move( line ) );
            continue;
        }

        wxString subName = info->displayName.IsEmpty() ? info->name : info->displayName;

        wxFileName proFile = aProject.ResolveSubProjectPath( *info );
        wxFileName pcbFile = proFile;
        pcbFile.SetExt( wxT( "kicad_pcb" ) );

        auto [applied, missing] = applyEndpointsToOnePcb( pcbFile, endpoints, aDryRun,
                                                          subName, result.previewLines );

        result.endpointsApplied += applied;
        result.endpointsMissing += missing;

        if( applied > 0 )
            result.subProjectsTouched++;
    }

    result.summary = wxString::Format(
            aDryRun
                ? _( "Would update %d sub-project PCB(s); would apply %d pad "
                     "net assignment(s); %d missing; %d net(s) would be "
                     "renamed from local PCB nets; %zu naming conflict(s) "
                     "detected." )
                : _( "Updated %d sub-project PCB(s); applied %d pad net "
                     "assignment(s); %d missing; %d net(s) renamed from "
                     "local PCB nets; %zu naming conflict(s) detected." ),
            result.subProjectsTouched,
            result.endpointsApplied,
            result.endpointsMissing,
            result.netsRenamed,
            result.conflicts.size() );

    if( !result.conflicts.empty() )
    {
        result.summary += wxT( "\n\nConflicting net names (chose alphabetically first, "
                               "place a label on the MBS wire to override):\n" );

        for( const MB_NET_NAME_CONFLICT& c : result.conflicts )
        {
            wxString rejected;

            for( size_t i = 0; i < c.rejected.size(); ++i )
            {
                if( i > 0 )
                    rejected += wxT( ", " );

                rejected += c.rejected[i];
            }

            result.summary += wxString::Format( wxT( "  • %s (also seen: %s)\n" ),
                                                c.chosen, rejected );
        }
    }

    return result;
}
