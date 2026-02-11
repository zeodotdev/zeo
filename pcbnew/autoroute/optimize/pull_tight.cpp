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

#include "pull_tight.h"
#include <board.h>
#include <cmath>
#include <algorithm>


PULL_TIGHT::PULL_TIGHT( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
    : m_board( aBoard )
    , m_control( aControl )
{
}


PULL_TIGHT_RESULT PULL_TIGHT::Optimize( PATH_SEGMENT& aSegment )
{
    PULL_TIGHT_RESULT result;

    if( aSegment.points.size() < 3 )
        return result;  // Need at least 3 points to optimize

    result.length_before = CalculateLength( aSegment.points );

    bool improved = true;
    int iteration = 0;

    while( improved && iteration < m_maxIterations )
    {
        improved = false;
        iteration++;

        // Try to move each interior vertex
        for( size_t i = 1; i < aSegment.points.size() - 1; ++i )
        {
            if( TryMoveVertex( aSegment.points, i, aSegment.layer, aSegment.width ) )
            {
                improved = true;
                result.improved = true;
            }
        }

        // Remove collinear points after each iteration
        RemoveCollinearPoints( aSegment.points );
    }

    result.iterations = iteration;
    result.length_after = CalculateLength( aSegment.points );

    if( result.length_before > 0 )
    {
        result.improvement_pct = 100.0 * ( result.length_before - result.length_after )
                                 / result.length_before;
    }

    return result;
}


PULL_TIGHT_RESULT PULL_TIGHT::OptimizePath( ROUTING_PATH& aPath )
{
    PULL_TIGHT_RESULT totalResult;

    for( auto& segment : aPath.segments )
    {
        PULL_TIGHT_RESULT segResult = Optimize( segment );

        totalResult.improved |= segResult.improved;
        totalResult.iterations += segResult.iterations;
        totalResult.length_before += segResult.length_before;
        totalResult.length_after += segResult.length_after;
    }

    if( totalResult.length_before > 0 )
    {
        totalResult.improvement_pct = 100.0 * ( totalResult.length_before - totalResult.length_after )
                                      / totalResult.length_before;
    }

    return totalResult;
}


bool PULL_TIGHT::TryMoveVertex( std::vector<VECTOR2I>& aPoints, size_t aIndex,
                                 int aLayer, int aWidth )
{
    if( aIndex == 0 || aIndex >= aPoints.size() - 1 )
        return false;

    const VECTOR2I& prev = aPoints[aIndex - 1];
    const VECTOR2I& current = aPoints[aIndex];
    const VECTOR2I& next = aPoints[aIndex + 1];

    // Calculate optimal new position
    VECTOR2I newPos = CalculateOptimalPosition( prev, current, next );

    // If position hasn't changed significantly, skip
    if( std::abs( newPos.x - current.x ) < 1000 && std::abs( newPos.y - current.y ) < 1000 )
        return false;

    // Check if new segments are valid (no collisions)
    if( !IsValidPosition( prev, newPos, aLayer, aWidth ) ||
        !IsValidPosition( newPos, next, aLayer, aWidth ) )
    {
        return false;
    }

    // Calculate length change
    double oldLen = std::sqrt( double( current.x - prev.x ) * ( current.x - prev.x ) +
                               double( current.y - prev.y ) * ( current.y - prev.y ) ) +
                    std::sqrt( double( next.x - current.x ) * ( next.x - current.x ) +
                               double( next.y - current.y ) * ( next.y - current.y ) );

    double newLen = std::sqrt( double( newPos.x - prev.x ) * ( newPos.x - prev.x ) +
                               double( newPos.y - prev.y ) * ( newPos.y - prev.y ) ) +
                    std::sqrt( double( next.x - newPos.x ) * ( next.x - newPos.x ) +
                               double( next.y - newPos.y ) * ( next.y - newPos.y ) );

    // Only accept if it reduces length
    if( newLen >= oldLen - 1000 )  // 1nm tolerance
        return false;

    // Apply the move
    aPoints[aIndex] = newPos;
    return true;
}


VECTOR2I PULL_TIGHT::CalculateOptimalPosition( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                                const VECTOR2I& aNext )
{
    switch( m_mode )
    {
    case PULL_TIGHT_MODE::ORTHOGONAL:
        return CalculateOptimal90( aPrev, aCurrent, aNext );

    case PULL_TIGHT_MODE::FORTYFIVE:
        return CalculateOptimal45( aPrev, aCurrent, aNext );

    case PULL_TIGHT_MODE::ANY_ANGLE:
    default:
        return CalculateOptimalAny( aPrev, aCurrent, aNext );
    }
}


VECTOR2I PULL_TIGHT::CalculateOptimal90( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                          const VECTOR2I& aNext )
{
    // For 90-degree mode, the optimal position is where perpendicular lines
    // from prev and next intersect

    // If prev and next are on the same horizontal line
    if( aPrev.y == aNext.y )
    {
        return VECTOR2I( aCurrent.x, aPrev.y );
    }

    // If prev and next are on the same vertical line
    if( aPrev.x == aNext.x )
    {
        return VECTOR2I( aPrev.x, aCurrent.y );
    }

    // Otherwise, find the corner point
    // Try two options and pick the one closer to current
    VECTOR2I opt1( aPrev.x, aNext.y );
    VECTOR2I opt2( aNext.x, aPrev.y );

    double dist1 = std::sqrt( double( opt1.x - aCurrent.x ) * ( opt1.x - aCurrent.x ) +
                              double( opt1.y - aCurrent.y ) * ( opt1.y - aCurrent.y ) );
    double dist2 = std::sqrt( double( opt2.x - aCurrent.x ) * ( opt2.x - aCurrent.x ) +
                              double( opt2.y - aCurrent.y ) * ( opt2.y - aCurrent.y ) );

    return ( dist1 < dist2 ) ? opt1 : opt2;
}


VECTOR2I PULL_TIGHT::CalculateOptimal45( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                          const VECTOR2I& aNext )
{
    // For 45-degree mode, try to find a point that creates 45 or 90 degree angles

    // Direction from prev to next
    int64_t dx = aNext.x - aPrev.x;
    int64_t dy = aNext.y - aPrev.y;

    // If they're close to aligned, use the direct line approach
    double directDist = std::sqrt( double( dx ) * dx + double( dy ) * dy );

    // Check if a direct 45 or 90 degree path is possible
    int64_t absDx = std::abs( dx );
    int64_t absDy = std::abs( dy );

    // If one dimension is much larger, use 45-degree transition
    if( absDx > absDy * 2 )
    {
        // Horizontal dominant - create 45-degree entry/exit
        int64_t diagLen = absDy;
        int signX = ( dx > 0 ) ? 1 : -1;
        int signY = ( dy > 0 ) ? 1 : -1;

        // Put the corner at the 45-degree transition point
        return VECTOR2I( static_cast<int>( aPrev.x + signX * diagLen ),
                         static_cast<int>( aPrev.y + signY * diagLen ) );
    }
    else if( absDy > absDx * 2 )
    {
        // Vertical dominant
        int64_t diagLen = absDx;
        int signX = ( dx > 0 ) ? 1 : -1;
        int signY = ( dy > 0 ) ? 1 : -1;

        return VECTOR2I( static_cast<int>( aPrev.x + signX * diagLen ),
                         static_cast<int>( aPrev.y + signY * diagLen ) );
    }
    else
    {
        // Close to 45 degrees already - try direct line
        return VECTOR2I( ( aPrev.x + aNext.x ) / 2, ( aPrev.y + aNext.y ) / 2 );
    }
}


VECTOR2I PULL_TIGHT::CalculateOptimalAny( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                           const VECTOR2I& aNext )
{
    // For any-angle mode, the optimal is simply the midpoint
    // (which would make a straight line if valid)

    // First, try the direct midpoint
    VECTOR2I midpoint( ( aPrev.x + aNext.x ) / 2, ( aPrev.y + aNext.y ) / 2 );

    // If the line from prev to next is valid, use midpoint
    // Otherwise, try to move current closer to the line

    // Project current onto the line from prev to next
    double dx = aNext.x - aPrev.x;
    double dy = aNext.y - aPrev.y;
    double len2 = dx * dx + dy * dy;

    if( len2 < 1.0 )
        return aCurrent;  // prev and next are the same point

    // Parameter t for projection
    double t = ( ( aCurrent.x - aPrev.x ) * dx + ( aCurrent.y - aPrev.y ) * dy ) / len2;
    t = std::clamp( t, 0.0, 1.0 );

    // Projected point
    return VECTOR2I( static_cast<int>( aPrev.x + t * dx ),
                     static_cast<int>( aPrev.y + t * dy ) );
}


bool PULL_TIGHT::IsValidPosition( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                   int aLayer, int aWidth )
{
    if( !m_searchTree )
        return true;  // No collision checking without search tree

    int clearance = m_control.clearance;
    int halfWidth = aWidth / 2 + clearance;

    // Create bounding box for the segment
    int minX = std::min( aStart.x, aEnd.x ) - halfWidth;
    int maxX = std::max( aStart.x, aEnd.x ) + halfWidth;
    int minY = std::min( aStart.y, aEnd.y ) - halfWidth;
    int maxY = std::max( aStart.y, aEnd.y ) + halfWidth;

    BOX2I segBounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );

    // Check for overlaps (excluding same-net items)
    return !m_searchTree->HasOverlap( segBounds, aLayer, m_netCode );
}


double PULL_TIGHT::CalculateLength( const std::vector<VECTOR2I>& aPoints )
{
    double total = 0.0;

    for( size_t i = 1; i < aPoints.size(); ++i )
    {
        double dx = aPoints[i].x - aPoints[i - 1].x;
        double dy = aPoints[i].y - aPoints[i - 1].y;
        total += std::sqrt( dx * dx + dy * dy );
    }

    return total;
}


VECTOR2I PULL_TIGHT::SnapToAngleGrid( const VECTOR2I& aPoint, const VECTOR2I& aReference )
{
    if( m_mode == PULL_TIGHT_MODE::ANY_ANGLE )
        return aPoint;

    double dx = aPoint.x - aReference.x;
    double dy = aPoint.y - aReference.y;
    double len = std::sqrt( dx * dx + dy * dy );

    if( len < 1.0 )
        return aPoint;

    // Calculate angle
    double angle = std::atan2( dy, dx );

    // Snap to allowed angles
    double snapAngle;

    if( m_mode == PULL_TIGHT_MODE::ORTHOGONAL )
    {
        // Snap to 0, 90, 180, 270 degrees
        snapAngle = std::round( angle / ( M_PI / 2 ) ) * ( M_PI / 2 );
    }
    else  // FORTYFIVE
    {
        // Snap to 0, 45, 90, 135, 180, 225, 270, 315 degrees
        snapAngle = std::round( angle / ( M_PI / 4 ) ) * ( M_PI / 4 );
    }

    return VECTOR2I( static_cast<int>( aReference.x + len * std::cos( snapAngle ) ),
                     static_cast<int>( aReference.y + len * std::sin( snapAngle ) ) );
}


void PULL_TIGHT::RemoveCollinearPoints( std::vector<VECTOR2I>& aPoints )
{
    if( aPoints.size() < 3 )
        return;

    std::vector<VECTOR2I> result;
    result.push_back( aPoints[0] );

    for( size_t i = 1; i < aPoints.size() - 1; ++i )
    {
        const VECTOR2I& prev = result.back();
        const VECTOR2I& curr = aPoints[i];
        const VECTOR2I& next = aPoints[i + 1];

        // Check if collinear using cross product
        int64_t dx1 = curr.x - prev.x;
        int64_t dy1 = curr.y - prev.y;
        int64_t dx2 = next.x - curr.x;
        int64_t dy2 = next.y - curr.y;

        int64_t cross = dx1 * dy2 - dy1 * dx2;

        // Keep point if not collinear (with small tolerance)
        if( std::abs( cross ) > 1000 )
        {
            result.push_back( curr );
        }
    }

    result.push_back( aPoints.back() );
    aPoints = result;
}
