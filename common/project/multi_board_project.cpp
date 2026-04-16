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

#include <project/multi_board_project.h>

#include <nlohmann/json.hpp>
#include <wx/ffile.h>
#include <wx/log.h>
#include <wx/regex.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>


using nlohmann::json;


MULTI_BOARD_PROJECT::MULTI_BOARD_PROJECT()
{
}


bool MULTI_BOARD_PROJECT::LoadFromFile( const wxString& aPath )
{
    // Reset to a defined-empty state before attempting load
    m_uuid = KIID();
    m_name.clear();
    m_mbsFileName.clear();
    m_3dAssemblyFileName.clear();
    m_subProjects.clear();

    wxFileName fn( aPath );
    m_rootDir = fn;
    m_rootDir.SetFullName( wxEmptyString );

    std::ifstream stream( aPath.ToStdString() );

    if( !stream.is_open() )
    {
        wxLogWarning( wxT( "MULTI_BOARD_PROJECT: cannot open %s" ), aPath );
        return false;
    }

    json j;

    try
    {
        stream >> j;
    }
    catch( const std::exception& e )
    {
        wxLogError( wxT( "MULTI_BOARD_PROJECT: parse error in %s: %s" ),
                    aPath, wxString::FromUTF8( e.what() ) );
        return false;
    }

    try
    {
        if( j.contains( "uuid" ) )
            m_uuid = KIID( wxString::FromUTF8( j.at( "uuid" ).get<std::string>() ) );

        if( j.contains( "name" ) )
            m_name = wxString::FromUTF8( j.at( "name" ).get<std::string>() );

        if( j.contains( "multi_board_schematic" ) )
        {
            m_mbsFileName = wxString::FromUTF8(
                    j.at( "multi_board_schematic" ).get<std::string>() );
        }

        if( j.contains( "3d_assembly" ) )
        {
            m_3dAssemblyFileName = wxString::FromUTF8(
                    j.at( "3d_assembly" ).get<std::string>() );
        }

        if( j.contains( "sub_projects" ) && j.at( "sub_projects" ).is_array() )
        {
            for( const auto& entry : j.at( "sub_projects" ) )
            {
                SUB_PROJECT_INFO info;

                if( entry.contains( "uuid" ) )
                    info.uuid = KIID( wxString::FromUTF8( entry.at( "uuid" ).get<std::string>() ) );

                if( entry.contains( "name" ) )
                    info.name = wxString::FromUTF8( entry.at( "name" ).get<std::string>() );

                if( entry.contains( "path" ) )
                {
                    info.relativePath =
                            wxString::FromUTF8( entry.at( "path" ).get<std::string>() );
                }

                if( entry.contains( "display_name" ) )
                {
                    info.displayName =
                            wxString::FromUTF8( entry.at( "display_name" ).get<std::string>() );
                }

                if( entry.contains( "role" ) )
                    info.role = wxString::FromUTF8( entry.at( "role" ).get<std::string>() );

                m_subProjects.push_back( info );
            }
        }

        m_crossBoardNets.clear();

        if( j.contains( "cross_board_nets" ) && j.at( "cross_board_nets" ).is_array() )
        {
            for( const auto& entry : j.at( "cross_board_nets" ) )
            {
                CROSS_BOARD_NET net;

                if( entry.contains( "uuid" ) )
                    net.uuid = KIID( wxString::FromUTF8( entry.at( "uuid" ).get<std::string>() ) );

                if( entry.contains( "name" ) )
                    net.name = wxString::FromUTF8( entry.at( "name" ).get<std::string>() );

                if( entry.contains( "endpoints" ) && entry.at( "endpoints" ).is_array() )
                {
                    for( const auto& ep : entry.at( "endpoints" ) )
                    {
                        CROSS_BOARD_NET_ENDPOINT endpoint;

                        if( ep.contains( "sub_project_uuid" ) )
                        {
                            endpoint.subProjectUuid = KIID(
                                    wxString::FromUTF8( ep.at( "sub_project_uuid" )
                                                                .get<std::string>() ) );
                        }

                        if( ep.contains( "component" ) )
                        {
                            endpoint.componentRef = wxString::FromUTF8(
                                    ep.at( "component" ).get<std::string>() );
                        }

                        if( ep.contains( "pin" ) )
                        {
                            endpoint.pinNumber = wxString::FromUTF8(
                                    ep.at( "pin" ).get<std::string>() );
                        }

                        if( ep.contains( "pin_name" ) )
                        {
                            endpoint.pinName = wxString::FromUTF8(
                                    ep.at( "pin_name" ).get<std::string>() );
                        }

                        net.endpoints.push_back( endpoint );
                    }
                }

                m_crossBoardNets.push_back( net );
            }
        }
    }
    catch( const std::exception& e )
    {
        wxLogError( wxT( "MULTI_BOARD_PROJECT: schema error in %s: %s" ),
                    aPath, wxString::FromUTF8( e.what() ) );
        return false;
    }

    return true;
}


bool MULTI_BOARD_PROJECT::SaveToFile( const wxString& aPath ) const
{
    json j;
    j["version"] = CURRENT_VERSION;
    j["uuid"] = m_uuid.AsString().ToStdString();
    j["name"] = m_name.ToStdString();
    j["multi_board_schematic"] = m_mbsFileName.ToStdString();
    j["3d_assembly"] = m_3dAssemblyFileName.ToStdString();

    json subArray = json::array();

    for( const SUB_PROJECT_INFO& info : m_subProjects )
    {
        json entry;
        entry["uuid"] = info.uuid.AsString().ToStdString();
        entry["name"] = info.name.ToStdString();
        entry["path"] = info.relativePath.ToStdString();
        entry["display_name"] = info.displayName.ToStdString();
        entry["role"] = info.role.ToStdString();
        subArray.push_back( entry );
    }

    j["sub_projects"] = subArray;

    json netsArray = json::array();

    for( const CROSS_BOARD_NET& net : m_crossBoardNets )
    {
        json entry;
        entry["uuid"] = net.uuid.AsString().ToStdString();
        entry["name"] = net.name.ToStdString();

        json epArray = json::array();

        for( const CROSS_BOARD_NET_ENDPOINT& endpoint : net.endpoints )
        {
            json ep;
            ep["sub_project_uuid"] = endpoint.subProjectUuid.AsString().ToStdString();
            ep["component"]        = endpoint.componentRef.ToStdString();
            ep["pin"]              = endpoint.pinNumber.ToStdString();
            ep["pin_name"]         = endpoint.pinName.ToStdString();
            epArray.push_back( ep );
        }

        entry["endpoints"] = epArray;
        netsArray.push_back( entry );
    }

    j["cross_board_nets"] = netsArray;

    std::ofstream stream( aPath.ToStdString() );

    if( !stream.is_open() )
    {
        wxLogError( wxT( "MULTI_BOARD_PROJECT: cannot write %s" ), aPath );
        return false;
    }

    stream << j.dump( 2 );
    stream.close();

    // Update root dir so subsequent path resolution works after save-as
    wxFileName fn( aPath );
    const_cast<MULTI_BOARD_PROJECT*>( this )->m_rootDir = fn;
    const_cast<MULTI_BOARD_PROJECT*>( this )->m_rootDir.SetFullName( wxEmptyString );

    return true;
}


SUB_PROJECT_INFO* MULTI_BOARD_PROJECT::GetSubProject( const KIID& aUuid )
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
            [&aUuid]( const SUB_PROJECT_INFO& sp ) { return sp.uuid == aUuid; } );

    return ( it != m_subProjects.end() ) ? &( *it ) : nullptr;
}


const SUB_PROJECT_INFO* MULTI_BOARD_PROJECT::GetSubProject( const KIID& aUuid ) const
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
            [&aUuid]( const SUB_PROJECT_INFO& sp ) { return sp.uuid == aUuid; } );

    return ( it != m_subProjects.end() ) ? &( *it ) : nullptr;
}


void MULTI_BOARD_PROJECT::AddSubProject( const SUB_PROJECT_INFO& aInfo )
{
    m_subProjects.push_back( aInfo );
}


bool MULTI_BOARD_PROJECT::RemoveSubProject( const KIID& aUuid )
{
    auto it = std::find_if( m_subProjects.begin(), m_subProjects.end(),
            [&aUuid]( const SUB_PROJECT_INFO& sp ) { return sp.uuid == aUuid; } );

    if( it == m_subProjects.end() )
        return false;

    m_subProjects.erase( it );
    return true;
}


wxFileName MULTI_BOARD_PROJECT::ResolveSubProjectPath( const SUB_PROJECT_INFO& aInfo ) const
{
    wxFileName result( aInfo.relativePath );

    if( result.IsRelative() )
        result.MakeAbsolute( m_rootDir.GetFullPath() );

    return result;
}


wxFileName MULTI_BOARD_PROJECT::ResolveMbsPath() const
{
    if( m_mbsFileName.IsEmpty() )
        return wxFileName();

    wxFileName result( m_mbsFileName );

    if( result.IsRelative() )
        result.MakeAbsolute( m_rootDir.GetFullPath() );

    return result;
}


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
            wxT( "\\(property\\s+\"Sheetfile\"\\s+\"([^\"]+)\"" ),
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
            wxT( "\\(property\\s+\"Reference\"\\s+\"([^\"]*)\"" ),
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


wxFileName MULTI_BOARD_PROJECT::EnsureMbsFile( const wxString& aContainerBasename )
{
    if( m_mbsFileName.IsEmpty() )
    {
        // Default: <basename>_mbs.kicad_sch (a regular s-expression schematic
        // under a suggestive name; Phase B-2 may promote this to .kicad_mbs).
        m_mbsFileName = aContainerBasename + wxT( "_mbs.kicad_sch" );
    }

    wxFileName mbsPath = ResolveMbsPath();

    if( mbsPath.FileExists() )
        return mbsPath;

    // Write a minimal valid KiCad s-expression schematic so eeschema can
    // open the file. UUID is intentionally random; version matches the
    // current KiCad schematic format version used elsewhere in the tree.
    wxFFile out( mbsPath.GetFullPath(), wxT( "w" ) );

    if( !out.IsOpened() )
    {
        wxLogError( wxT( "MULTI_BOARD_PROJECT: cannot create MBS file %s" ),
                    mbsPath.GetFullPath() );
        m_mbsFileName.clear();
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

    if( m_subProjects.empty() )
    {
        // No sub-projects yet — drop in a single placeholder so the MBS
        // isn't blank on first open.
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
        for( const SUB_PROJECT_INFO& info : m_subProjects )
        {
            wxString blockName = info.displayName.IsEmpty() ? info.name : info.displayName;

            // Scan the sub-project's main .kicad_sch for connector-class
            // symbols. Each one becomes a port on this block.
            wxFileName proFile = ResolveSubProjectPath( info );
            wxFileName schFile = mainSchematicForSubProject( proFile );
            std::vector<wxString> connectors = scanConnectorReferences( schFile );

            // Dynamically grow the block so tall connector lists fit.
            double       thisHeight = blockHeight;
            const double minHeight  = 32 * grid;   // ~40.6 mm
            const double perPin     = 5 * grid;    // 6.35 mm per pin (on-grid)
            const double padTop     = 8 * grid;    // 10.16 mm
            const double padBot     = 8 * grid;    // 10.16 mm

            // Half the pins go on the left edge, half on the right.
            size_t pinCount  = connectors.size();
            size_t leftCount  = ( pinCount + 1 ) / 2;
            size_t rightCount = pinCount - leftCount;
            size_t maxSide   = std::max( leftCount, rightCount );

            double needed = padTop + padBot + perPin * std::max<size_t>( maxSide, 1 );
            if( needed > thisHeight )
                thisHeight = needed;
            if( thisHeight < minHeight )
                thisHeight = minHeight;

            wxString pinsSection;
            auto emitPin =
                    [&]( const wxString& aPinName, double aLocalX, double aLocalY )
                    {
                        pinsSection += wxString::Format(
                                wxT( "\t\t(pin\n"
                                     "\t\t\t(uuid \"%s\")\n"
                                     "\t\t\t(number \"%s\")\n"
                                     "\t\t\t(name \"%s\")\n"
                                     "\t\t\t(at %.2f %.2f)\n"
                                     "\t\t)\n" ),
                                KIID().AsString(),
                                aPinName,
                                aPinName,
                                aLocalX, aLocalY );
                    };

            // Distribute pins evenly along the left edge (x=0) and right
            // edge (x=width), top-to-bottom.
            for( size_t i = 0; i < leftCount; ++i )
            {
                double localY = padTop + perPin * (double) i;
                emitPin( connectors[i], 0.0, localY );
            }

            for( size_t i = 0; i < rightCount; ++i )
            {
                double localY = padTop + perPin * (double) i;
                emitPin( connectors[leftCount + i], blockWidth, localY );
            }

            blocksSection += wxString::Format(
                    wxT( "\t(module_block\n"
                         "\t\t(at %.2f %.2f)\n"
                         "\t\t(size %.2f %.2f)\n"
                         "\t\t(sub_project \"%s\")\n"
                         "\t\t(name \"%s\")\n"
                         "\t\t(uuid \"%s\")\n"
                         "%s"
                         "\t)\n" ),
                    cursorX, startY, blockWidth, thisHeight,
                    info.relativePath,
                    blockName,
                    KIID().AsString(),
                    pinsSection );

            cursorX += blockWidth + blockSpacing;
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
