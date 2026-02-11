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
#include "../optimize/pull_tight.h"
#include "../optimize/via_optimizer.h"
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

    // Simplify and optimize with 45-degree corners
    SimplifyPath( path );
    OptimizePath( path, m_clearance );

    // Apply pull-tight optimization to reduce path length
    if( m_pullTightEnabled && m_board && m_control )
    {
        PULL_TIGHT pullTight( m_board, *m_control );
        pullTight.SetSearchTree( m_searchTree );
        pullTight.SetNetCode( m_netCode );
        pullTight.SetMode( PULL_TIGHT_MODE::FORTYFIVE );
        pullTight.OptimizePath( path );
    }

    // Apply via optimization to reduce via count and improve positions
    if( m_board && m_control && !path.via_locations.empty() )
    {
        VIA_OPTIMIZER viaOpt( m_board, *m_control );
        viaOpt.SetSearchTree( m_searchTree );
        viaOpt.SetNetCode( m_netCode );
        viaOpt.Optimize( path );
    }

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
        pt.via_first_layer = drill->GetFirstLayer();
        pt.via_last_layer = drill->GetLastLayer();
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
    // Implement 45-degree corner optimization (chamfering)
    // Instead of 90-degree corners, create 45-degree chamfered corners
    // This follows FreeRouting's LocateFoundConnectionAlgo45Degree approach

    for( auto& seg : aPath.segments )
    {
        if( seg.points.size() <= 2 )
            continue;

        std::vector<VECTOR2I> optimized;
        optimized.push_back( seg.points[0] );

        for( size_t i = 1; i < seg.points.size() - 1; ++i )
        {
            const VECTOR2I& prev = optimized.back();
            const VECTOR2I& curr = seg.points[i];
            const VECTOR2I& next = seg.points[i + 1];

            // Calculate direction vectors
            int64_t dx1 = curr.x - prev.x;
            int64_t dy1 = curr.y - prev.y;
            int64_t dx2 = next.x - curr.x;
            int64_t dy2 = next.y - curr.y;

            // Check if this is a corner (direction change)
            int64_t cross = dx1 * dy2 - dy1 * dx2;
            if( cross == 0 )
            {
                // Collinear - skip this point
                continue;
            }

            // Determine the angle change
            bool isHorizontalFirst = ( std::abs( dx1 ) > std::abs( dy1 ) );
            bool isVerticalSecond = ( std::abs( dy2 ) > std::abs( dx2 ) );
            bool is90DegreeTurn = ( isHorizontalFirst && isVerticalSecond ) ||
                                   ( !isHorizontalFirst && !isVerticalSecond );

            if( is90DegreeTurn )
            {
                // Create 45-degree chamfer
                // Calculate segment lengths
                double len1 = std::sqrt( double( dx1 * dx1 + dy1 * dy1 ) );
                double len2 = std::sqrt( double( dx2 * dx2 + dy2 * dy2 ) );

                // Chamfer distance (minimum of segment lengths or minSegmentLength)
                double chamferDist = std::min( { len1 * 0.4, len2 * 0.4,
                                                  double( aMinSegmentLength ) } );

                if( chamferDist >= aMinSegmentLength / 2 )
                {
                    // Calculate chamfer start point (on first segment)
                    double t1 = 1.0 - ( chamferDist / len1 );
                    VECTOR2I chamferStart(
                        static_cast<int>( prev.x + dx1 * t1 ),
                        static_cast<int>( prev.y + dy1 * t1 ) );

                    // Calculate chamfer end point (on second segment)
                    double t2 = chamferDist / len2;
                    VECTOR2I chamferEnd(
                        static_cast<int>( curr.x + dx2 * t2 ),
                        static_cast<int>( curr.y + dy2 * t2 ) );

                    // Add chamfer points
                    optimized.push_back( chamferStart );
                    optimized.push_back( chamferEnd );
                }
                else
                {
                    // Chamfer too small, keep original corner
                    optimized.push_back( curr );
                }
            }
            else
            {
                // Not a 90-degree turn, keep original corner point
                optimized.push_back( curr );
            }
        }

        // Always add the last point
        optimized.push_back( seg.points.back() );

        // Ensure minimum segment lengths
        std::vector<VECTOR2I> filtered;
        filtered.push_back( optimized[0] );

        for( size_t i = 1; i < optimized.size(); ++i )
        {
            const VECTOR2I& prev = filtered.back();
            const VECTOR2I& curr = optimized[i];

            int64_t dx = curr.x - prev.x;
            int64_t dy = curr.y - prev.y;
            int64_t len2 = dx * dx + dy * dy;

            // Keep point if segment is long enough or it's the last point
            if( len2 >= (int64_t)aMinSegmentLength * aMinSegmentLength / 4 ||
                i == optimized.size() - 1 )
            {
                filtered.push_back( curr );
            }
        }

        seg.points = filtered;
    }
}
