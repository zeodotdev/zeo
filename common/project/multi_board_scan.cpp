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

#include <project/multi_board_scan.h>
#include <project/project_file.h>

#include <kiid.h>
#include <string_utils.h>
#include <wildcards_and_files_ext.h>

#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/log.h>
#include <wx/regex.h>

#include <algorithm>
#include <set>


namespace
{

/**
 * Heuristic: is this reference designator a connector?
 *
 * Matches refs starting with J / P / CN / CON (case-insensitive) followed
 * by a digit sequence (possibly with a trailing letter, to accommodate
 * multi-unit symbols like `J1A`) OR a single `?` for unannotated symbols.
 */
bool isConnectorRef( const wxString& aRef )
{
    static wxRegEx re(
            wxT( "^(J|P|CN|CON)([[:digit:]]+[[:alpha:]]?|\\?)$" ),
            wxRE_DEFAULT | wxRE_ICASE );

    return re.IsValid() && re.Matches( aRef );
}


/**
 * Extract every sub-sheet filename referenced by a root .kicad_sch.
 *
 * KiCad stores hierarchical sheet links as:
 *     (sheet ...
 *         (property "Sheetfile" "subsheet.kicad_sch" ...)
 *         ...
 *     )
 *
 * We match the Sheetfile property value and return paths relative to the
 * directory containing aSchFile.
 */
std::vector<wxFileName> findHierarchicalSubSheets( const wxFileName& aSchFile,
                                                    const wxString& aFileContents )
{
    std::vector<wxFileName> result;

    static wxRegEx sheetFileRe(
            wxT( "\\(property[[:space:]]+\"Sheetfile\"[[:space:]]+\"([^\"]+)\"" ),
            wxRE_DEFAULT );

    if( !sheetFileRe.IsValid() )
        return result;

    size_t pos = 0;
    size_t len = aFileContents.length();

    while( pos < len )
    {
        wxString remaining = aFileContents.Mid( pos );

        if( !sheetFileRe.Matches( remaining ) )
            break;

        size_t start, matchLen;
        sheetFileRe.GetMatch( &start, &matchLen, 0 );

        wxString subPath = sheetFileRe.GetMatch( remaining, 1 );

        wxFileName sub( subPath );

        if( sub.IsRelative() )
            sub.MakeAbsolute( aSchFile.GetPath() );

        result.push_back( sub );

        pos += start + matchLen;
    }

    return result;
}


/**
 * Find the .kicad_sch associated with a single-board sub-project. The
 * standalone project layout always pairs `<name>.kicad_pro` with
 * `<name>.kicad_sch` in the same directory.
 */
wxFileName mainSchematicForSubProject( const wxFileName& aProFile )
{
    wxFileName sch = aProFile;
    sch.SetExt( wxT( "kicad_sch" ) );
    return sch;
}


wxFileName mainPcbForSubProject( const wxFileName& aProFile )
{
    wxFileName pcb = aProFile;
    pcb.SetExt( wxT( "kicad_pcb" ) );
    return pcb;
}


/**
 * Locate the matching closing paren for an s-expression that begins at
 * `aStart` (which must point AT the opening `(`). Returns std::string::npos
 * on imbalance.
 */
size_t findMatchingClose( const wxString& aText, size_t aStart )
{
    int depth = 0;
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
 * Scan a .kicad_pcb file for connector footprints and return, per reference,
 * the ordered list of pad info (number + current net). The pad numbers are
 * strings (e.g. "1", "A12", "GND") to accommodate non-integer pad names
 * used by USB-C / edge connectors. Order preserves the order in the file.
 */
std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>>
scanConnectorPads( const wxFileName& aPcbFile )
{
    std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>> out;

    if( !aPcbFile.FileExists() )
        return out;

    wxFFile f( aPcbFile.GetFullPath(), wxT( "r" ) );

    if( !f.IsOpened() )
        return out;

    wxString text;

    if( !f.ReadAll( &text ) )
        return out;

    f.Close();

    static wxRegEx refRe( wxT( "\\(property[[:space:]]+\"Reference\"[[:space:]]+\"([^\"]*)\"" ),
                          wxRE_DEFAULT );
    static wxRegEx padRe( wxT( "\\(pad[[:space:]]+\"([^\"]*)\"" ), wxRE_DEFAULT );
    static wxRegEx netRe( wxT( "\\(net[[:space:]]+\"([^\"]*)\"\\)" ), wxRE_DEFAULT );

    if( !refRe.IsValid() || !padRe.IsValid() || !netRe.IsValid() )
        return out;

    size_t pos = 0;
    const wxString fpOpen = wxT( "(footprint" );

    while( pos < text.length() )
    {
        size_t fpStart = text.find( fpOpen, pos );

        if( fpStart == wxString::npos )
            break;

        size_t fpEnd = findMatchingClose( text, fpStart );

        if( fpEnd == wxString::npos )
            break;

        wxString block = text.Mid( fpStart, fpEnd - fpStart + 1 );
        pos = fpEnd + 1;

        if( !refRe.Matches( block ) )
            continue;

        wxString ref = refRe.GetMatch( block, 1 );

        if( !isConnectorRef( ref ) )
            continue;

        std::vector<MULTI_BOARD_PAD_INFO>& pads = out[ref];

        size_t scan = 0;

        while( scan < block.length() )
        {
            wxString remaining = block.Mid( scan );

            if( !padRe.Matches( remaining ) )
                break;

            size_t matchStart, matchLen;
            padRe.GetMatch( &matchStart, &matchLen, 0 );

            size_t padAbsStart = scan + matchStart;
            size_t padAbsEnd   = findMatchingClose( block, padAbsStart );

            if( padAbsEnd == wxString::npos )
                break;

            wxString padBlock = block.Mid( padAbsStart, padAbsEnd - padAbsStart + 1 );
            wxString padNum   = padRe.GetMatch( remaining, 1 );

            // Deduplicate: multiple "(pad \"N\"" may appear if the footprint
            // has several pads sharing a number (e.g. GND). Keep the first.
            auto existing = std::find_if( pads.begin(), pads.end(),
                                          [&]( const MULTI_BOARD_PAD_INFO& p )
                                          { return p.padNumber == padNum; } );

            if( existing == pads.end() )
            {
                MULTI_BOARD_PAD_INFO info;
                info.padNumber = padNum;

                if( netRe.Matches( padBlock ) )
                    info.netName = netRe.GetMatch( padBlock, 1 );

                // Heuristic electrical type from pad net name.
                // Proper extraction would walk the connector symbol's
                // pin definitions; this v1 covers the common power /
                // ground case which is what triggers the ERC matrix
                // for the GND-to-5V short scenario. Anything that
                // doesn't match these prefixes stays at PT_PASSIVE,
                // which is the no-op for the matrix.
                static const wxRegEx powerRe(
                        wxT( "^(\\+?[0-9]+(\\.[0-9]+)?V[A-Z0-9_]*"
                             "|GND|VCC|VDD|VSS|VBUS|VBAT|VREF|VOUT|VIN"
                             "|AGND|DGND|PGND|SGND"
                             "|\\+?V[A-Z0-9_]+)$" ),
                        wxRE_DEFAULT | wxRE_ICASE );

                if( !info.netName.IsEmpty() && powerRe.IsValid()
                    && powerRe.Matches( info.netName ) )
                {
                    info.electricalType = ELECTRICAL_PINTYPE::PT_POWER_IN;
                }

                pads.push_back( info );
            }

            scan = padAbsEnd + 1;
        }
    }

    return out;
}


/**
 * Scan a single .kicad_sch for Reference properties matching the connector
 * heuristic. Does NOT recurse — caller handles sub-sheets.
 */
void scanOneSchForConnectors( const wxFileName& aSchFile,
                               std::vector<wxString>& aOut,
                               std::vector<wxFileName>& aSubSheetsOut )
{
    if( !aSchFile.FileExists() )
        return;

    wxFFile f( aSchFile.GetFullPath(), wxT( "r" ) );

    if( !f.IsOpened() )
        return;

    wxString text;

    if( !f.ReadAll( &text ) )
        return;

    f.Close();

    static wxRegEx propRe(
            wxT( "\\(property[[:space:]]+\"Reference\"[[:space:]]+\"([^\"]*)\"" ),
            wxRE_DEFAULT );

    if( !propRe.IsValid() )
        return;

    size_t pos = 0;
    size_t len = text.length();

    while( pos < len )
    {
        wxString remaining = text.Mid( pos );

        if( !propRe.Matches( remaining ) )
            break;

        size_t start, matchLen;
        propRe.GetMatch( &start, &matchLen, 0 );

        wxString ref = propRe.GetMatch( remaining, 1 );

        if( isConnectorRef( ref ) )
        {
            if( std::find( aOut.begin(), aOut.end(), ref ) == aOut.end() )
                aOut.push_back( ref );
        }

        pos += start + matchLen;
    }

    // Also collect hierarchical sub-sheets so the caller can recurse.
    std::vector<wxFileName> subSheets = findHierarchicalSubSheets( aSchFile, text );
    aSubSheetsOut.insert( aSubSheetsOut.end(), subSheets.begin(), subSheets.end() );
}


/**
 * Scan a sub-project's main .kicad_sch and all hierarchical sub-sheets it
 * references for connector symbol references. Returns the union, sorted
 * alphabetically for stable MBS layout.
 */
std::vector<wxString> scanConnectorReferences( const wxFileName& aSchFile )
{
    std::vector<wxString>    result;
    std::vector<wxFileName>  queue{ aSchFile };
    std::set<wxString>       visited;

    while( !queue.empty() )
    {
        wxFileName cur = queue.back();
        queue.pop_back();

        wxString key = cur.GetFullPath();

        if( visited.find( key ) != visited.end() )
            continue;

        visited.insert( key );

        std::vector<wxFileName> discoveredSubSheets;
        scanOneSchForConnectors( cur, result, discoveredSubSheets );

        for( const wxFileName& sub : discoveredSubSheets )
            queue.push_back( sub );
    }

    std::sort( result.begin(), result.end() );

    return result;
}


} // end anonymous namespace


wxFileName MultiBoardMainSchematic( const wxFileName& aProFile )
{
    return mainSchematicForSubProject( aProFile );
}


wxFileName MultiBoardMainPcb( const wxFileName& aProFile )
{
    return mainPcbForSubProject( aProFile );
}


std::vector<wxString> MultiBoardScanConnectorReferences( const wxFileName& aSchFile )
{
    return scanConnectorReferences( aSchFile );
}


std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>>
MultiBoardScanConnectorPads( const wxFileName& aPcbFile )
{
    return scanConnectorPads( aPcbFile );
}


namespace
{

/**
 * Shared core: walks up to the container, finds this sub-project's
 * UUID entry, then invokes `aPerEndpoint` once per cross-board net
 * endpoint that targets this sub-project. Both public collectors are
 * thin wrappers around this helper.
 */
template <typename PerEndpointFn>
void forEachCrossBoardEndpoint( const wxFileName& aSubProjectPro,
                                 PerEndpointFn aPerEndpoint )
{
    if( !aSubProjectPro.IsOk() || !aSubProjectPro.FileExists() )
        return;

    wxFileName subPro( aSubProjectPro );
    subPro.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    wxString subProAbs = subPro.GetFullPath();

    wxFileName searchDir( subPro );
    searchDir.SetFullName( wxEmptyString );

    for( int depth = 0; depth < 6 && searchDir.GetPath().Length() > 1; ++depth )
    {
        wxArrayString files;
        wxDir::GetAllFiles( searchDir.GetPath(), &files, wxT( "*.kicad_pro" ),
                            wxDIR_FILES );

        for( const wxString& candidate : files )
        {
            wxFileName candFn( candidate );

            if( candFn.GetFullPath() == subProAbs )
                continue;

            PROJECT_FILE probe( candidate );

            if( !probe.LoadFromFile() )
                continue;

            if( !probe.IsMultiBoardContainer() )
                continue;

            KIID matchedUuid;
            bool matched = false;

            for( const SUB_PROJECT_INFO& info : probe.GetSubProjects() )
            {
                wxFileName subRel( info.relativePath );

                if( !subRel.IsAbsolute() )
                    subRel.MakeAbsolute( candFn.GetPath() );

                subRel.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

                if( subRel.GetFullPath() == subProAbs )
                {
                    matchedUuid = info.uuid;
                    matched = true;
                    break;
                }
            }

            if( !matched )
                continue;

            for( const MB_CROSS_BOARD_NET& net : probe.GetCrossBoardNets() )
            {
                for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
                {
                    if( ep.subProjectUuid == matchedUuid )
                        aPerEndpoint( net, ep );
                }
            }

            return;   // first match wins
        }

        searchDir.RemoveLastDir();
    }
}

} // anonymous namespace


std::set<std::pair<wxString, wxString>>
MultiBoardCollectCrossBoardEndpointsForSubProject( const wxFileName& aSubProjectPro )
{
    std::set<std::pair<wxString, wxString>> result;

    forEachCrossBoardEndpoint( aSubProjectPro,
            [&]( const MB_CROSS_BOARD_NET&, const MB_CROSS_BOARD_NET_ENDPOINT& ep )
            {
                result.insert( { ep.componentRef, ep.pinNumber } );
            } );

    return result;
}


std::vector<MULTI_BOARD_CROSS_BOARD_BINDING>
MultiBoardCollectCrossBoardBindingsForSubProject( const wxFileName& aSubProjectPro )
{
    std::vector<MULTI_BOARD_CROSS_BOARD_BINDING> result;

    forEachCrossBoardEndpoint( aSubProjectPro,
            [&]( const MB_CROSS_BOARD_NET& net, const MB_CROSS_BOARD_NET_ENDPOINT& ep )
            {
                MULTI_BOARD_CROSS_BOARD_BINDING b;
                b.componentRef = ep.componentRef;
                b.pinNumber    = ep.pinNumber;
                b.netName      = net.name;
                result.push_back( std::move( b ) );
            } );

    return result;
}


namespace
{

/**
 * Normalise a net name for comparison: strip KiCad escape sequences,
 * sheet-path prefix, and CONNECTION_GRAPH disambig suffix. Same logic
 * the DRC binding check and the MBS extractor use.
 */
wxString normaliseNetForMatch( wxString aName )
{
    aName = UnescapeString( aName );

    if( aName.StartsWith( wxT( "/" ) ) )
        aName = aName.AfterFirst( '/' );

    static wxRegEx re( wxT( "_[0-9]+$" ) );

    if( re.IsValid() )
        re.Replace( &aName, wxEmptyString );

    return aName;
}

} // anonymous namespace


std::vector<MULTI_BOARD_CROSS_BOARD_PROBE>
MultiBoardCollectCrossBoardProbesForLocalNet( const wxFileName& aSubProjectPro,
                                              const wxString& aLocalNetName )
{
    std::vector<MULTI_BOARD_CROSS_BOARD_PROBE> result;

    if( !aSubProjectPro.IsOk() || !aSubProjectPro.FileExists() || aLocalNetName.IsEmpty() )
        return result;

    wxFileName subPro( aSubProjectPro );
    subPro.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    wxString subProAbs = subPro.GetFullPath();

    wxString normLocalNet = normaliseNetForMatch( aLocalNetName );

    // Walk up looking for the enclosing container .kicad_pro. Same
    // 6-level search as the other lookups; M5.2 will replace this with
    // an explicit parent reference stored in the .kicad_mbs.
    wxFileName searchDir( subPro );
    searchDir.SetFullName( wxEmptyString );

    for( int depth = 0; depth < 6 && searchDir.GetPath().Length() > 1; ++depth )
    {
        wxArrayString files;
        wxDir::GetAllFiles( searchDir.GetPath(), &files, wxT( "*.kicad_pro" ),
                            wxDIR_FILES );

        for( const wxString& candidate : files )
        {
            wxFileName candFn( candidate );

            if( candFn.GetFullPath() == subProAbs )
                continue;

            PROJECT_FILE container( candidate );

            if( !container.LoadFromFile() )
                continue;

            if( !container.IsMultiBoardContainer() )
                continue;

            // Locate this sender's sub-project entry to get its UUID
            // and so we can compute peer absolute paths relative to
            // the container's directory.
            KIID senderUuid;
            bool matched = false;

            for( const SUB_PROJECT_INFO& info : container.GetSubProjects() )
            {
                wxFileName subRel( info.relativePath );

                if( !subRel.IsAbsolute() )
                    subRel.MakeAbsolute( candFn.GetPath() );

                subRel.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

                if( subRel.GetFullPath() == subProAbs )
                {
                    senderUuid = info.uuid;
                    matched = true;
                    break;
                }
            }

            if( !matched )
                continue;

            // Build a quick lookup from sub-project UUID → absolute
            // .kicad_pro path so peer probes can be scoped properly.
            std::map<KIID, wxString> subProjectAbsPaths;

            for( const SUB_PROJECT_INFO& info : container.GetSubProjects() )
            {
                wxFileName subRel( info.relativePath );

                if( !subRel.IsAbsolute() )
                    subRel.MakeAbsolute( candFn.GetPath() );

                subRel.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
                subProjectAbsPaths[info.uuid] = subRel.GetFullPath();
            }

            // For each cross-board net, decide if it matches the
            // sender's local net. Two ways:
            //   (a) one of THIS sub-project's endpoints has a pinName
            //       that normalises to the same form as aLocalNetName
            //   (b) the net's canonical name normalises to the same
            //       form (covers broadcasts that already use the
            //       canonical name, e.g. MBSCH originating)
            for( const MB_CROSS_BOARD_NET& net : container.GetCrossBoardNets() )
            {
                bool netMatches = false;

                if( normaliseNetForMatch( net.name ) == normLocalNet )
                    netMatches = true;

                if( !netMatches )
                {
                    for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
                    {
                        if( ep.subProjectUuid != senderUuid )
                            continue;

                        if( normaliseNetForMatch( ep.pinName ) == normLocalNet )
                        {
                            netMatches = true;
                            break;
                        }
                    }
                }

                if( !netMatches )
                    continue;

                // Emit a probe for every endpoint that DOESN'T live on
                // the sender's own sub-project — those are the peers
                // we need to fan out to.
                for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
                {
                    if( ep.subProjectUuid == senderUuid )
                        continue;

                    auto pathIt = subProjectAbsPaths.find( ep.subProjectUuid );

                    if( pathIt == subProjectAbsPaths.end() )
                        continue;

                    MULTI_BOARD_CROSS_BOARD_PROBE probe;
                    probe.targetSubProjectAbsPath = pathIt->second;
                    probe.componentRef            = ep.componentRef;
                    probe.pinNumber               = ep.pinNumber;
                    result.push_back( std::move( probe ) );
                }
            }

            return result;   // first matching container wins
        }

        searchDir.RemoveLastDir();
    }

    return result;
}


wxString MultiBoardFormatCrossBoardProbe( const MULTI_BOARD_CROSS_BOARD_PROBE& aProbe )
{
    // Mirrors the format MBSCH_EDIT_FRAME::crossProbeHighlightNet
    // emits. The receivers in pcbnew/cross-probing.cpp and
    // eeschema/cross-probing.cpp already parse this exact shape.
    return wxString::Format( wxT( "$PART: \"%s\" $PAD: \"%s\" $PROJECT: \"%s\"" ),
                             aProbe.componentRef,
                             aProbe.pinNumber,
                             aProbe.targetSubProjectAbsPath );
}


MULTI_BOARD_CONTAINER_VIEW MultiBoardBuildContainerView( const wxFileName& aSubProjectPro )
{
    MULTI_BOARD_CONTAINER_VIEW view;

    if( !aSubProjectPro.IsOk() || !aSubProjectPro.FileExists() )
        return view;

    wxFileName subPro( aSubProjectPro );
    subPro.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    wxString subProAbs = subPro.GetFullPath();

    wxFileName searchDir( subPro );
    searchDir.SetFullName( wxEmptyString );

    for( int depth = 0; depth < 6 && searchDir.GetPath().Length() > 1; ++depth )
    {
        wxArrayString files;
        wxDir::GetAllFiles( searchDir.GetPath(), &files, wxT( "*.kicad_pro" ),
                            wxDIR_FILES );

        for( const wxString& candidate : files )
        {
            wxFileName candFn( candidate );

            if( candFn.GetFullPath() == subProAbs )
                continue;

            PROJECT_FILE container( candidate );

            if( !container.LoadFromFile() )
                continue;

            if( !container.IsMultiBoardContainer() )
                continue;

            // Resolve every sub-project's absolute path once. We need this
            // both to find ourselves in the container and to label each
            // sibling endpoint with its on-disk path so the caller can
            // load sibling boards by path (no PROJECT object required).
            std::map<KIID, wxString>           subProjectAbsByUuid;
            std::map<KIID, wxString>           subProjectNameByUuid;

            for( const SUB_PROJECT_INFO& info : container.GetSubProjects() )
            {
                wxFileName subRel( info.relativePath );

                if( !subRel.IsAbsolute() )
                    subRel.MakeAbsolute( candFn.GetPath() );

                subRel.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
                subProjectAbsByUuid[info.uuid]  = subRel.GetFullPath();
                subProjectNameByUuid[info.uuid] = info.displayName.IsEmpty() ? info.name
                                                                             : info.displayName;
            }

            KIID matchedUuid;
            bool matched = false;

            for( const auto& [uuid, absPath] : subProjectAbsByUuid )
            {
                if( absPath == subProAbs )
                {
                    matchedUuid = uuid;
                    matched     = true;
                    break;
                }
            }

            if( !matched )
                continue;

            view.containerProAbsPath = candFn.GetFullPath();
            view.mySubProjectUuid    = matchedUuid;

            for( const MB_CROSS_BOARD_NET& net : container.GetCrossBoardNets() )
            {
                MULTI_BOARD_CROSS_BOARD_NET_VIEW netView;
                bool touchesUs = false;

                for( const MB_CROSS_BOARD_NET_ENDPOINT& ep : net.endpoints )
                {
                    MULTI_BOARD_NET_ENDPOINT_VIEW epView;
                    epView.subProjectUuid = ep.subProjectUuid;
                    epView.componentRef   = ep.componentRef;
                    epView.pinNumber      = ep.pinNumber;
                    epView.pinName        = ep.pinName;

                    auto nameIt = subProjectNameByUuid.find( ep.subProjectUuid );

                    if( nameIt != subProjectNameByUuid.end() )
                        epView.subProjectName = nameIt->second;

                    auto pathIt = subProjectAbsByUuid.find( ep.subProjectUuid );

                    if( pathIt != subProjectAbsByUuid.end() )
                        epView.subProjectAbsPath = pathIt->second;

                    if( ep.subProjectUuid == matchedUuid )
                    {
                        netView.myEndpoints.push_back( std::move( epView ) );
                        touchesUs = true;
                    }
                    else
                    {
                        netView.siblingEndpoints.push_back( std::move( epView ) );
                    }
                }

                if( !touchesUs )
                    continue;

                netView.netName = net.name;
                netView.netUuid = net.uuid;

                view.crossBoardNets.push_back( std::move( netView ) );
            }

            return view;   // first matching container wins
        }

        searchDir.RemoveLastDir();
    }

    return view;
}


/**
 * Heuristic matching the one in cross_board_pcb_sync.cpp — returns true
 * for empty / KiCad auto-generated / unconnected- / our "Net-<hex>" names.
 */
static bool isAutoGeneratedNetName_local( const wxString& aName )
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
 * Pick a human-friendly display label for an MBS pin: prefer the pad's
 * local net name when it's meaningful; fall back to "<ref>.<padNum>" or
 * just the ref if no pad number is known.
 */
wxString MultiBoardPinLabel( const wxString& aRef, const MULTI_BOARD_PAD_INFO& aPad )
{
    if( !isAutoGeneratedNetName_local( aPad.netName ) )
        return aPad.netName;

    if( aPad.padNumber.IsEmpty() )
        return aRef;

    return aRef + wxT( "." ) + aPad.padNumber;
}


namespace
{
/**
 * Heuristic: is the given file an auto-generated MBS scaffold rather
 * than user-edited content? We tag every file we write with a distinct
 * generator string (`(generator "zeo")`), so its presence in the first
 * few hundred bytes is a reliable signal.
 */
bool isAutoGeneratedMbsScaffold( const wxFileName& aPath )
{
    if( !aPath.FileExists() )
        return false;

    wxFFile f( aPath.GetFullPath(), wxT( "rb" ) );

    if( !f.IsOpened() )
        return false;

    char   buf[512];
    size_t nread = f.Read( buf, sizeof( buf ) - 1 );
    buf[nread] = 0;

    wxString head( buf, wxConvUTF8, nread );
    return head.Contains( wxT( "(generator \"zeo\")" ) );
}


/**
 * Detect an empty/stub `.kicad_sch` — the bare skeleton that
 * KICAD_MANAGER_FRAME::CreateNewProject writes when a new project is
 * minted. It contains no wires, labels, symbols, or junctions, and is
 * tagged `(generator "eeschema")`. If we find one at the container
 * root of a multi-board project, it's the leftover stub from project
 * creation — safe to delete because the canonical container
 * schematic lives in `<basename>.kicad_mbs`.
 */
bool isStubSchematic( const wxFileName& aPath )
{
    if( !aPath.FileExists() )
        return false;

    wxFFile f( aPath.GetFullPath(), wxT( "rb" ) );

    if( !f.IsOpened() )
        return false;

    // Stub files written by the manager are under ~400 bytes. Reading
    // a couple kilobytes is enough to tell stub from real content.
    char   buf[4096];
    size_t nread = f.Read( buf, sizeof( buf ) - 1 );
    buf[nread] = 0;

    wxString head( buf, wxConvUTF8, nread );

    if( !head.Contains( wxT( "(generator \"eeschema\")" ) ) )
        return false;

    // Any of these tokens means the schematic holds user content. A
    // stub has only (lib_symbols) and (sheet_instances).
    const wxChar* contentMarkers[] = {
        wxT( "(wire " ),       wxT( "(symbol " ),            wxT( "(label " ),
        wxT( "(junction " ),   wxT( "(global_label " ),      wxT( "(hierarchical_label " ),
        wxT( "(sheet " ),      wxT( "(no_connect " ),        wxT( "(text " ),
        wxT( "(bus " ),        wxT( "(bus_entry " ),         wxT( "(image " ),
        wxT( "(module_block " ),
    };

    for( const wxChar* m : contentMarkers )
    {
        if( head.Contains( m ) )
            return false;
    }

    return true;
}
}   // anonymous namespace


wxFileName EnsureMbsFile( PROJECT_FILE& aContainer, const wxString& aContainerBasename )
{
    wxString mbsFileName = aContainer.GetMbsFileName();

    wxFileName containerDir( aContainer.GetFullFilename() );
    containerDir.SetFullName( wxEmptyString );

    const wxString mbsExt = wxString::FromUTF8( FILEEXT::MbsFileExtension );
    const wxString schExt = wxString::FromUTF8( FILEEXT::KiCadSchematicFileExtension );

    // Canonical MBS filename is always `<basename>.kicad_mbs` — that
    // extension matches the dedicated MBSCH editor and mirrors Altium's
    // `.MbsDoc`/`.SchDoc` split so the two file types stay visually and
    // semantically distinct.
    wxFileName canonicalMbs( containerDir.GetPath(),
                             aContainerBasename + wxT( "." ) + mbsExt );

    // One-time migration: earlier versions let users edit the container-
    // root `<basename>.kicad_sch` because the MBS redirect wasn't in
    // place. If that file still exists alongside (or instead of) the
    // canonical `.kicad_mbs`, preserve the user's edits by promoting the
    // `.kicad_sch` to the canonical path. Only overwrite an existing
    // `.kicad_mbs` when we can prove it's a freshly-generated scaffold
    // with no user content — otherwise leave both alone and let the user
    // resolve the conflict.
    wxFileName legacySchFn( containerDir.GetPath(),
                            aContainerBasename + wxT( "." ) + schExt );

    if( legacySchFn.FileExists() )
    {
        bool canOverwrite =
                !canonicalMbs.FileExists() || isAutoGeneratedMbsScaffold( canonicalMbs );

        if( canOverwrite )
        {
            if( canonicalMbs.FileExists() )
                wxRemoveFile( canonicalMbs.GetFullPath() );

            if( wxRenameFile( legacySchFn.GetFullPath(), canonicalMbs.GetFullPath() ) )
            {
                wxLogMessage( wxT( "Multi-board: migrated '%s' → '%s'" ),
                              legacySchFn.GetFullName(), canonicalMbs.GetFullName() );
            }
        }
        else if( isStubSchematic( legacySchFn ) )
        {
            // Both files exist but the `.kicad_sch` is the empty stub
            // template wrote on project creation. Deleting it tidies
            // the container root so the tree doesn't advertise a
            // non-editable duplicate next to the canonical MBS.
            wxLogMessage( wxT( "Multi-board: removing stub '%s' (content is in '%s')" ),
                          legacySchFn.GetFullName(), canonicalMbs.GetFullName() );
            wxRemoveFile( legacySchFn.GetFullPath() );
        }
    }

    // Force mbs_file to the canonical `.kicad_mbs` filename regardless
    // of what was previously stored. This corrects any projects that
    // picked up the earlier (reversed) fallback and pointed mbs_file at
    // a `.kicad_sch`.
    wxString canonicalName = canonicalMbs.GetFullName();

    if( mbsFileName != canonicalName )
    {
        mbsFileName = canonicalName;
        aContainer.SetMbsFileName( mbsFileName );
    }

    wxFileName mbsPath = aContainer.ResolveMbsPath();

    if( mbsPath.FileExists() )
        return mbsPath;

    wxFFile out( mbsPath.GetFullPath(), wxT( "w" ) );

    if( !out.IsOpened() )
    {
        wxLogError( wxT( "EnsureMbsFile: cannot create %s" ), mbsPath.GetFullPath() );
        aContainer.SetMbsFileName( wxEmptyString );
        return wxFileName();
    }

    // Build the schematic header. Coordinates in a KiCad s-expression
    // schematic are expressed in MILLIMETRES (parsed as doubles, then scaled
    // by IU_PER_MM). We use mm throughout this generator.
    wxString uuidStr = KIID().AsString();

    wxString header = wxString::Format(
            wxT( "(kicad_sch\n"
                 "\t(version 20250610)\n"
                 "\t(generator \"zeo\")\n"
                 "\t(generator_version \"9.99\")\n"
                 "\t(uuid \"%s\")\n"
                 "\t(paper \"A3\")\n"
                 "\t(title_block\n"
                 "\t\t(title \"%s - Multi-Board Schematic\")\n"
                 "\t)\n"
                 "\t(lib_symbols)\n" ),
            uuidStr,
            aContainerBasename );

    // Lay out one module_block per sub-project in a left-to-right row.
    // Positions and sizes are in millimetres.
    // Positions and sizes are in mm. The KiCad schematic grid is
    // 50 mils = 1.27 mm, so all coordinates must be multiples of 1.27
    // for wires to snap cleanly to pin connection points.
    constexpr double grid         = 1.27;
    constexpr double blockWidth   = 40 * grid;   // 50.8 mm
    constexpr double blockHeight  = 48 * grid;   // 60.96 mm
    constexpr double blockSpacing = 12 * grid;   // 15.24 mm
    constexpr double startX       = 40 * grid;   // 50.8 mm
    constexpr double startY       = 60 * grid;   // 76.2 mm

    wxString blocksSection;
    double   cursorX = startX;

    const auto& subProjects = aContainer.GetSubProjects();

    if( subProjects.empty() )
    {
        blocksSection += wxString::Format(
                wxT( "\t(module_block\n"
                     "\t\t(at %.2f %.2f)\n"
                     "\t\t(size %.2f %.2f)\n"
                     "\t\t(name \"%s\")\n"
                     "\t\t(uuid \"%s\")\n"
                     "\t)\n" ),
                cursorX, startY, blockWidth, blockHeight,
                wxT( "Module" ), KIID().AsString() );
    }
    else
    {
        // One module_block per CONNECTOR (not per sub-project) so that each
        // cross-board interface gets its own visual anchor. Blocks are
        // grouped vertically: one row per sub-project, connectors in that
        // sub-project lined up left-to-right within the row.
        const double perPin      = 5 * grid;   // 6.35 mm per pin (on-grid)
        const double padTop      = 8 * grid;   // 10.16 mm
        const double padBot      = 8 * grid;   // 10.16 mm
        const double minHeight   = 32 * grid;  // ~40.6 mm
        const double rowSpacing  = 20 * grid;  // 25.4 mm between sub-project rows

        double cursorY = startY;

        for( const SUB_PROJECT_INFO& info : subProjects )
        {
            wxString subName = info.displayName.IsEmpty() ? info.name : info.displayName;

            wxFileName proFile = aContainer.ResolveSubProjectPath( info );
            wxFileName schFile = mainSchematicForSubProject( proFile );
            wxFileName pcbFile = mainPcbForSubProject( proFile );

            std::vector<wxString> connectors = scanConnectorReferences( schFile );

            // No connectors → no cross-board interface to represent. Skip
            // entirely rather than emitting an empty placeholder block;
            // a later refresh will add a block once the user introduces
            // a connector to this sub-project.
            if( connectors.empty() )
                continue;

            std::map<wxString, std::vector<MULTI_BOARD_PAD_INFO>> pads =
                    scanConnectorPads( pcbFile );

            double rowCursorX  = startX;
            double rowMaxHeight = 0;

            for( const wxString& ref : connectors )
            {
                std::vector<MULTI_BOARD_PAD_INFO> padList;

                auto it = pads.find( ref );

                if( it != pads.end() )
                    padList = it->second;

                if( padList.empty() )
                    padList.push_back( MULTI_BOARD_PAD_INFO{} );  // placeholder pin

                size_t pinCount   = padList.size();
                size_t leftCount  = ( pinCount + 1 ) / 2;
                size_t rightCount = pinCount - leftCount;
                size_t maxSide    = std::max( leftCount, rightCount );

                double thisHeight = padTop + padBot + perPin * std::max<size_t>( maxSide, 1 );
                thisHeight        = std::max( thisHeight, minHeight );

                wxString pinsSection;
                auto emitPin =
                        [&]( const MULTI_BOARD_PAD_INFO& aPad, double aLocalX, double aLocalY )
                        {
                            wxString label = MultiBoardPinLabel( ref, aPad );

                            pinsSection += wxString::Format(
                                    wxT( "\t\t(pin\n"
                                         "\t\t\t(uuid \"%s\")\n"
                                         "\t\t\t(component \"%s\")\n"
                                         "\t\t\t(number \"%s\")\n"
                                         "\t\t\t(name \"%s\")\n"
                                         "\t\t\t(at %.2f %.2f)\n"
                                         "\t\t)\n" ),
                                    KIID().AsString(), ref, aPad.padNumber, label,
                                    aLocalX, aLocalY );
                        };

                for( size_t i = 0; i < leftCount; ++i )
                    emitPin( padList[i], 0.0, padTop + perPin * (double) i );

                for( size_t i = 0; i < rightCount; ++i )
                    emitPin( padList[leftCount + i], blockWidth,
                             padTop + perPin * (double) i );

                wxString blockDisplayName = subName + wxT( " / " ) + ref;

                blocksSection += wxString::Format(
                        wxT( "\t(module_block\n"
                             "\t\t(at %.2f %.2f)\n"
                             "\t\t(size %.2f %.2f)\n"
                             "\t\t(sub_project \"%s\")\n"
                             "\t\t(component \"%s\")\n"
                             "\t\t(name \"%s\")\n"
                             "\t\t(uuid \"%s\")\n"
                             "%s"
                             "\t)\n" ),
                        rowCursorX, cursorY, blockWidth, thisHeight,
                        info.relativePath, ref, blockDisplayName,
                        KIID().AsString(), pinsSection );

                rowCursorX += blockWidth + blockSpacing;
                rowMaxHeight = std::max( rowMaxHeight, thisHeight );
            }

            cursorY += rowMaxHeight + rowSpacing;
        }
    }

    wxString footer =
            wxT( "\t(sheet_instances\n"
                 "\t\t(path \"/\"\n"
                 "\t\t\t(page \"1\")\n"
                 "\t\t)\n"
                 "\t)\n"
                 ")\n" );

    out.Write( header + blocksSection + footer );
    out.Close();

    return mbsPath;
}
