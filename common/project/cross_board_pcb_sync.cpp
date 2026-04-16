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
 * Apply all endpoints targeting a single .kicad_pcb file. Returns stats.
 */
std::pair<int, int> applyEndpointsToOnePcb(
        const wxFileName& aPcbFile,
        const std::vector<std::pair<CROSS_BOARD_NET_ENDPOINT, wxString>>& aEndpointsAndNetNames )
{
    int applied = 0;
    int missing = 0;

    if( !aPcbFile.FileExists() )
        return { applied, (int) aEndpointsAndNetNames.size() };

    wxFFile in( aPcbFile.GetFullPath(), wxT( "r" ) );

    if( !in.IsOpened() )
        return { applied, (int) aEndpointsAndNetNames.size() };

    wxString text;

    if( !in.ReadAll( &text ) )
    {
        in.Close();
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

    for( const auto& [ref, padList] : byFootprint )
    {
        auto [fpBegin, fpEnd] = findFootprintBlock( text, ref, 0 );

        if( fpBegin == wxString::npos )
        {
            missing += padList.size();
            continue;
        }

        for( const auto& [padNum, netName] : padList )
        {
            auto [padBegin, padEnd] = findPadBlock( text, padNum, fpBegin, fpEnd );

            if( padBegin == wxString::npos )
            {
                missing++;
                continue;
            }

            size_t newPadEnd = writePadNet( text, padBegin, padEnd, netName );

            // Adjust fpEnd for any text length delta introduced by the edit.
            int delta = (int) newPadEnd - (int) padEnd;
            fpEnd = (size_t) ( (int) fpEnd + delta );
            applied++;
        }
    }

    wxFFile out( aPcbFile.GetFullPath(), wxT( "w" ) );

    if( !out.IsOpened() )
        return { applied, missing };

    out.Write( text );
    out.Close();

    return { applied, missing };
}


} // anonymous namespace


CROSS_BOARD_SYNC_RESULT
ApplyCrossBoardNetsToSubProjectPCBs( const MULTI_BOARD_PROJECT& aProject )
{
    CROSS_BOARD_SYNC_RESULT result;

    // Bucket endpoints by sub-project uuid → list of (endpoint, net name).
    std::map<KIID, std::vector<std::pair<CROSS_BOARD_NET_ENDPOINT, wxString>>> perSub;

    for( const CROSS_BOARD_NET& net : aProject.GetCrossBoardNets() )
    {
        for( const CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
            perSub[ep.subProjectUuid].emplace_back( ep, net.name );
    }

    for( const auto& [uuid, endpoints] : perSub )
    {
        const SUB_PROJECT_INFO* info = aProject.GetSubProject( uuid );

        if( !info )
        {
            result.endpointsMissing += endpoints.size();
            continue;
        }

        wxFileName proFile = aProject.ResolveSubProjectPath( *info );
        wxFileName pcbFile = proFile;
        pcbFile.SetExt( wxT( "kicad_pcb" ) );

        auto [applied, missing] = applyEndpointsToOnePcb( pcbFile, endpoints );

        result.endpointsApplied += applied;
        result.endpointsMissing += missing;

        if( applied > 0 )
            result.subProjectsTouched++;
    }

    result.summary = wxString::Format(
            wxT( "Updated %d sub-project PCB(s); applied %d pad net assignment(s); %d missing." ),
            result.subProjectsTouched,
            result.endpointsApplied,
            result.endpointsMissing );

    return result;
}
