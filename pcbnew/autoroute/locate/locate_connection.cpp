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

#include "locate_connection.h"
#include "../search/maze_search.h"
#include "../expansion/expansion_door.h"
#include "../expansion/expansion_drill.h"
#include <cmath>


//-----------------------------------------------------------------------------
// ROUTING_PATH Implementation
//-----------------------------------------------------------------------------

int64_t ROUTING_PATH::GetTotalLength() const
{
    int64_t total = 0;

    for( const auto& seg : segments )
    {
        for( size_t i = 1; i < seg.points.size(); ++i )
        {
            int64_t dx = seg.points[i].x - seg.points[i - 1].x;
            int64_t dy = seg.points[i].y - seg.points[i - 1].y;
            total += static_cast<int64_t>( std::sqrt( dx * dx + dy * dy ) );
        }
    }

    return total;
}


//-----------------------------------------------------------------------------
// LOCATE_CONNECTION Implementation
//-----------------------------------------------------------------------------

LOCATE_CONNECTION::LOCATE_CONNECTION()
{
}


ROUTING_PATH LOCATE_CONNECTION::LocatePath()
{
    ROUTING_PATH path;

    if( !m_search )
        return path;

    // Get backtrack path from maze search
    auto backtrackPath = m_search->GetBacktrackPath();

    if( backtrackPath.empty() )
        return path;

    // Convert backtrack elements to path points
    std::vector<PATH_POINT> points;

    for( const auto& [door, section] : backtrackPath )
    {
        ProcessBacktrackElement( door, section, points );
    }

    if( points.empty() )
        return path;

    // Mark start and end
    points.front().is_start = true;
    points.back().is_end = true;

    // Extract via locations
    ExtractVias( points, path );

    // Build path segments
    BuildSegments( points, path );

    // Set start/end info
    path.start_point = points.front().position;
    path.end_point = points.back().position;
    path.start_layer = points.front().layer;
    path.end_layer = points.back().layer;

    // Simplify and optimize
    SimplifyPath( path );

    return path;
}


void LOCATE_CONNECTION::ProcessBacktrackElement( EXPANDABLE_OBJECT* aDoor, int aSection,
                                                  std::vector<PATH_POINT>& aPoints )
{
    if( !aDoor )
        return;

    // Check if it's a drill (via)
    EXPANSION_DRILL* drill = dynamic_cast<EXPANSION_DRILL*>( aDoor );
    if( drill )
    {
        PATH_POINT pt;
        pt.position = drill->GetLocation();
        pt.layer = drill->GetFirstLayer() + aSection;
        pt.is_via = true;
        aPoints.push_back( pt );
        return;
    }

    // It's a regular door
    EXPANSION_DOOR* door = dynamic_cast<EXPANSION_DOOR*>( aDoor );
    if( door )
    {
        PATH_POINT pt;
        pt.position = door->GetSectionCenter( aSection );
        pt.layer = door->GetLayer();
        pt.is_via = false;
        aPoints.push_back( pt );
    }
}


void LOCATE_CONNECTION::BuildSegments( const std::vector<PATH_POINT>& aPoints,
                                        ROUTING_PATH& aPath )
{
    if( aPoints.size() < 2 )
        return;

    PATH_SEGMENT currentSeg;
    currentSeg.layer = aPoints[0].layer;
    currentSeg.width = m_trackWidth;
    currentSeg.points.push_back( aPoints[0].position );

    for( size_t i = 1; i < aPoints.size(); ++i )
    {
        const PATH_POINT& pt = aPoints[i];

        if( pt.layer != currentSeg.layer )
        {
            // Layer change - finish current segment and start new one
            if( currentSeg.points.size() >= 2 )
            {
                aPath.segments.push_back( currentSeg );
            }

            currentSeg = PATH_SEGMENT();
            currentSeg.layer = pt.layer;
            currentSeg.width = m_trackWidth;
        }

        currentSeg.points.push_back( pt.position );
    }

    // Add final segment
    if( currentSeg.points.size() >= 2 )
    {
        aPath.segments.push_back( currentSeg );
    }
}


void LOCATE_CONNECTION::ExtractVias( const std::vector<PATH_POINT>& aPoints,
                                      ROUTING_PATH& aPath )
{
    for( const auto& pt : aPoints )
    {
        if( pt.is_via )
        {
            aPath.via_locations.push_back( pt );
        }
    }
}


void LOCATE_CONNECTION::SimplifyPath( ROUTING_PATH& aPath )
{
    // Remove collinear points from each segment
    for( auto& seg : aPath.segments )
    {
        if( seg.points.size() <= 2 )
            continue;

        std::vector<VECTOR2I> simplified;
        simplified.push_back( seg.points[0] );

        for( size_t i = 1; i < seg.points.size() - 1; ++i )
        {
            const VECTOR2I& prev = simplified.back();
            const VECTOR2I& curr = seg.points[i];
            const VECTOR2I& next = seg.points[i + 1];

            // Check if points are collinear using cross product
            int64_t dx1 = curr.x - prev.x;
            int64_t dy1 = curr.y - prev.y;
            int64_t dx2 = next.x - curr.x;
            int64_t dy2 = next.y - curr.y;

            int64_t cross = dx1 * dy2 - dy1 * dx2;

            // Keep point if not collinear (direction changes)
            if( cross != 0 )
            {
                simplified.push_back( curr );
            }
        }

        simplified.push_back( seg.points.back() );
        seg.points = simplified;
    }
}


void LOCATE_CONNECTION::OptimizePath( ROUTING_PATH& aPath, int aMinSegmentLength )
{
    // This could implement:
    // - 45-degree chamfering of corners
    // - Arc corner optimization
    // - Length matching adjustments

    // For now, just ensure minimum segment lengths
    for( auto& seg : aPath.segments )
    {
        if( seg.points.size() <= 2 )
            continue;

        std::vector<VECTOR2I> optimized;
        optimized.push_back( seg.points[0] );

        for( size_t i = 1; i < seg.points.size(); ++i )
        {
            const VECTOR2I& prev = optimized.back();
            const VECTOR2I& curr = seg.points[i];

            int64_t dx = curr.x - prev.x;
            int64_t dy = curr.y - prev.y;
            int64_t len2 = dx * dx + dy * dy;

            // Keep point if segment is long enough or it's the last point
            if( len2 >= (int64_t)aMinSegmentLength * aMinSegmentLength ||
                i == seg.points.size() - 1 )
            {
                optimized.push_back( curr );
            }
        }

        seg.points = optimized;
    }
}
