/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "insert_connection.h"
#include <sstream>


INSERT_CONNECTION::INSERT_CONNECTION()
{
}


INSERT_RESULT INSERT_CONNECTION::Insert( const ROUTING_PATH& aPath )
{
    INSERT_RESULT result;

    if( !aPath.IsValid() )
    {
        result.error_message = "Invalid path";
        return result;
    }

    // Insert track segments
    for( const auto& seg : aPath.segments )
    {
        if( InsertTrackSegment( seg ) )
            result.tracks_added++;
    }

    // Insert vias
    for( const auto& via : aPath.via_locations )
    {
        if( InsertVia( via ) )
            result.vias_added++;
    }

    result.success = ( result.tracks_added > 0 || result.vias_added > 0 );
    return result;
}


bool INSERT_CONNECTION::InsertTrackSegment( const PATH_SEGMENT& aSegment )
{
    // Direct board manipulation would go here
    // For now, we use the IPC code generation approach
    return false;
}


bool INSERT_CONNECTION::InsertVia( const PATH_POINT& aViaPoint )
{
    // Direct board manipulation would go here
    return false;
}


std::string INSERT_CONNECTION::GenerateInsertCode( const ROUTING_PATH& aPath ) const
{
    std::ostringstream code;

    code << "import json\n";
    code << "from kipy.geometry import Vector2\n";
    code << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    code << "\n";
    code << "# Insert autorouted path\n";
    code << "tracks_added = 0\n";
    code << "vias_added = 0\n";
    code << "\n";

    // Generate code for each segment
    for( const auto& seg : aPath.segments )
    {
        code << GenerateTrackCode( seg );
    }

    // Generate code for vias
    for( const auto& via : aPath.via_locations )
    {
        code << GenerateViaCode( via );
    }

    code << "\n";
    code << "print(json.dumps({'tracks_added': tracks_added, 'vias_added': vias_added}))\n";

    return code.str();
}


std::string INSERT_CONNECTION::GenerateTrackCode( const PATH_SEGMENT& aSegment ) const
{
    std::ostringstream code;

    if( aSegment.points.size() < 2 )
        return "";

    std::string layerName = LayerToName( aSegment.layer );

    code << "# Track segment on " << layerName << "\n";
    code << "points = [\n";

    for( const auto& pt : aSegment.points )
    {
        code << "    Vector2.from_xy(" << pt.x << ", " << pt.y << "),\n";
    }

    code << "]\n";
    code << "try:\n";
    code << "    tracks = board.route_track(\n";
    code << "        points=points,\n";
    code << "        width=" << aSegment.width << ",\n";
    code << "        layer=BoardLayer.BL_" << layerName << ",\n";

    if( !m_netName.empty() )
    {
        code << "        net='" << m_netName << "'\n";
    }
    else
    {
        code << "        net=None\n";
    }

    code << "    )\n";
    code << "    tracks_added += len(tracks)\n";
    code << "except Exception as e:\n";
    code << "    print(f'Track error: {e}')\n";
    code << "\n";

    return code.str();
}


std::string INSERT_CONNECTION::GenerateViaCode( const PATH_POINT& aViaPoint ) const
{
    std::ostringstream code;

    code << "# Via at (" << aViaPoint.position.x / 1000000.0 << "mm, "
         << aViaPoint.position.y / 1000000.0 << "mm)\n";
    code << "try:\n";
    code << "    via = board.add_via(\n";
    code << "        position=Vector2.from_xy(" << aViaPoint.position.x << ", "
         << aViaPoint.position.y << "),\n";
    code << "        diameter=" << m_control.via_diameter << ",\n";
    code << "        drill=" << m_control.via_drill << ",\n";

    if( !m_netName.empty() )
    {
        code << "        net='" << m_netName << "'\n";
    }
    else
    {
        code << "        net=None\n";
    }

    code << "    )\n";
    code << "    vias_added += 1\n";
    code << "except Exception as e:\n";
    code << "    print(f'Via error: {e}')\n";
    code << "\n";

    return code.str();
}


std::string INSERT_CONNECTION::LayerToName( int aLayer ) const
{
    // KiCad layer numbering: F.Cu = 0, In1.Cu = 1, ..., B.Cu = 31 (for 2-layer board)
    // Simplified for now
    switch( aLayer )
    {
    case 0:  return "F_Cu";
    case 1:  return "In1_Cu";
    case 2:  return "In2_Cu";
    case 3:  return "In3_Cu";
    case 4:  return "In4_Cu";
    case 31: return "B_Cu";
    default:
        // For inner layers
        if( aLayer >= 1 && aLayer <= 30 )
            return "In" + std::to_string( aLayer ) + "_Cu";
        return "F_Cu";
    }
}
