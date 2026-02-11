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

#include "length_matcher.h"
#include "../search/shape_search_tree.h"
#include <board.h>
#include <algorithm>
#include <cmath>


LENGTH_MATCHER::LENGTH_MATCHER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
    : m_board( aBoard )
    , m_control( aControl )
{
}


int64_t LENGTH_MATCHER::CalculateLength( const ROUTING_PATH& aPath ) const
{
    int64_t total = 0;

    for( const auto& seg : aPath.segments )
    {
        total += CalculateSegmentLength( seg );
    }

    return total;
}


int64_t LENGTH_MATCHER::CalculateSegmentLength( const PATH_SEGMENT& aSegment ) const
{
    int64_t total = 0;

    for( size_t i = 1; i < aSegment.points.size(); ++i )
    {
        int64_t dx = aSegment.points[i].x - aSegment.points[i - 1].x;
        int64_t dy = aSegment.points[i].y - aSegment.points[i - 1].y;
        total += static_cast<int64_t>( std::sqrt( double( dx * dx + dy * dy ) ) );
    }

    return total;
}


LENGTH_MATCH_RESULT LENGTH_MATCHER::MatchLengths( std::vector<ROUTING_PATH>& aPaths )
{
    LENGTH_MATCH_RESULT result;

    if( aPaths.empty() )
        return result;

    // Calculate lengths and find maximum
    result.max_length = 0;

    for( size_t i = 0; i < aPaths.size(); ++i )
    {
        LENGTH_MATCH_TARGET target;
        target.path = aPaths[i];
        target.current_length = CalculateLength( aPaths[i] );

        if( target.current_length > result.max_length )
        {
            result.max_length = target.current_length;
        }

        result.targets.push_back( target );
    }

    // Use target length if specified, otherwise use max
    int64_t targetLength = m_config.target_length > 0 ?
                           m_config.target_length : result.max_length;

    // Calculate length to add for each path
    for( auto& target : result.targets )
    {
        target.length_to_add = targetLength - target.current_length;
        target.needs_matching = target.length_to_add > m_config.tolerance;
    }

    // Add meanders to shorter paths
    for( size_t i = 0; i < result.targets.size(); ++i )
    {
        auto& target = result.targets[i];

        if( target.needs_matching )
        {
            if( AddMeanders( target.path, targetLength ) )
            {
                // Update the original path
                aPaths[i] = target.path;
                target.current_length = CalculateLength( target.path );
                result.matched_count++;
            }
            else
            {
                result.failed_count++;
            }
        }
    }

    // Calculate final max difference
    result.max_difference = 0;
    for( const auto& target : result.targets )
    {
        int64_t diff = std::abs( targetLength - target.current_length );
        if( diff > result.max_difference )
        {
            result.max_difference = diff;
        }
    }

    result.success = ( result.max_difference <= m_config.tolerance );

    return result;
}


bool LENGTH_MATCHER::AddMeanders( ROUTING_PATH& aPath, int64_t aTargetLength )
{
    int64_t currentLength = CalculateLength( aPath );
    int64_t lengthToAdd = aTargetLength - currentLength;

    if( lengthToAdd <= 0 )
        return true;  // Already long enough

    // Find best segment for meanders
    size_t segIdx = FindBestSegmentForMeanders( aPath );

    if( segIdx >= aPath.segments.size() )
        return false;

    PATH_SEGMENT& segment = aPath.segments[segIdx];

    if( segment.points.size() < 2 )
        return false;

    // Find a suitable subsegment (prefer middle)
    size_t bestSubSeg = segment.points.size() / 2;
    if( bestSubSeg < 1 )
        bestSubSeg = 1;
    if( bestSubSeg >= segment.points.size() )
        bestSubSeg = segment.points.size() - 1;

    VECTOR2I start = segment.points[bestSubSeg - 1];
    VECTOR2I end = segment.points[bestSubSeg];

    // Generate meanders
    std::vector<VECTOR2I> meanderPoints;

    switch( m_config.style )
    {
    case MEANDER_STYLE::TRAPEZOIDAL:
        meanderPoints = GenerateTrapezoidalMeander( start, end,
                                                     m_config.max_amplitude,
                                                     m_config.spacing,
                                                     static_cast<int>( lengthToAdd ),
                                                     m_config.single_sided );
        break;

    case MEANDER_STYLE::RECTANGULAR:
        meanderPoints = GenerateRectangularMeander( start, end,
                                                     m_config.max_amplitude,
                                                     m_config.spacing,
                                                     static_cast<int>( lengthToAdd ),
                                                     m_config.single_sided );
        break;

    case MEANDER_STYLE::ROUNDED:
        // For rounded, use trapezoidal as base
        meanderPoints = GenerateTrapezoidalMeander( start, end,
                                                     m_config.max_amplitude,
                                                     m_config.spacing,
                                                     static_cast<int>( lengthToAdd ),
                                                     m_config.single_sided );
        break;
    }

    if( meanderPoints.empty() )
        return false;

    // Check validity
    if( !IsValidMeander( meanderPoints, segment.layer, segment.width, 0 ) )
        return false;

    // Insert meander points into segment
    std::vector<VECTOR2I> newPoints;

    for( size_t i = 0; i < bestSubSeg; ++i )
    {
        newPoints.push_back( segment.points[i] );
    }

    for( const auto& pt : meanderPoints )
    {
        newPoints.push_back( pt );
    }

    for( size_t i = bestSubSeg; i < segment.points.size(); ++i )
    {
        newPoints.push_back( segment.points[i] );
    }

    segment.points = std::move( newPoints );

    return true;
}


size_t LENGTH_MATCHER::FindBestSegmentForMeanders( const ROUTING_PATH& aPath ) const
{
    size_t bestIdx = 0;
    int64_t bestLength = 0;

    for( size_t i = 0; i < aPath.segments.size(); ++i )
    {
        int64_t segLen = CalculateSegmentLength( aPath.segments[i] );

        if( segLen > bestLength )
        {
            bestLength = segLen;
            bestIdx = i;
        }
    }

    return bestIdx;
}


std::vector<VECTOR2I> LENGTH_MATCHER::GenerateTrapezoidalMeander( const VECTOR2I& aStart,
                                                                    const VECTOR2I& aEnd,
                                                                    int aAmplitude, int aSpacing,
                                                                    int aLengthToAdd,
                                                                    bool aSingleSided )
{
    std::vector<VECTOR2I> points;

    // Calculate direction and perpendicular
    VECTOR2I dir = aEnd - aStart;
    double dirLen = std::sqrt( double( dir.x ) * dir.x + double( dir.y ) * dir.y );

    if( dirLen < 1.0 )
        return points;

    // Normalize direction
    double dirX = dir.x / dirLen;
    double dirY = dir.y / dirLen;

    // Perpendicular
    double perpX = -dirY;
    double perpY = dirX;

    // Calculate number of meanders needed
    // Each meander adds approximately 2 * amplitude to the path
    int meanderCount = static_cast<int>( aLengthToAdd / ( 2 * aAmplitude ) );
    meanderCount = std::max( 1, meanderCount );

    // Calculate actual spacing between meanders
    double actualSpacing = dirLen / ( meanderCount + 1 );

    // Entry angle for trapezoidal (45 degrees)
    double entryLen = aAmplitude / std::sqrt( 2.0 );

    points.push_back( aStart );

    for( int i = 0; i < meanderCount; ++i )
    {
        double t = ( i + 1.0 ) / ( meanderCount + 1 );
        VECTOR2I center( static_cast<int>( aStart.x + dir.x * t ),
                         static_cast<int>( aStart.y + dir.y * t ) );

        // Side to meander (alternating or single-sided)
        double side = 1.0;
        if( !aSingleSided && ( i % 2 == 1 ) )
        {
            side = -1.0;
        }

        // Entry point (45-degree entry)
        VECTOR2I entry( static_cast<int>( center.x - dirX * entryLen + perpX * entryLen * side ),
                        static_cast<int>( center.y - dirY * entryLen + perpY * entryLen * side ) );

        // Apex (full amplitude)
        VECTOR2I apex( static_cast<int>( center.x + perpX * aAmplitude * side ),
                       static_cast<int>( center.y + perpY * aAmplitude * side ) );

        // Exit point (45-degree exit)
        VECTOR2I exit( static_cast<int>( center.x + dirX * entryLen + perpX * entryLen * side ),
                       static_cast<int>( center.y + dirY * entryLen + perpY * entryLen * side ) );

        points.push_back( entry );
        points.push_back( apex );
        points.push_back( exit );
    }

    points.push_back( aEnd );

    return points;
}


std::vector<VECTOR2I> LENGTH_MATCHER::GenerateRectangularMeander( const VECTOR2I& aStart,
                                                                    const VECTOR2I& aEnd,
                                                                    int aAmplitude, int aSpacing,
                                                                    int aLengthToAdd,
                                                                    bool aSingleSided )
{
    std::vector<VECTOR2I> points;

    VECTOR2I dir = aEnd - aStart;
    double dirLen = std::sqrt( double( dir.x ) * dir.x + double( dir.y ) * dir.y );

    if( dirLen < 1.0 )
        return points;

    double dirX = dir.x / dirLen;
    double dirY = dir.y / dirLen;
    double perpX = -dirY;
    double perpY = dirX;

    int meanderCount = static_cast<int>( aLengthToAdd / ( 2 * aAmplitude ) );
    meanderCount = std::max( 1, meanderCount );

    points.push_back( aStart );

    for( int i = 0; i < meanderCount; ++i )
    {
        double t = ( i + 1.0 ) / ( meanderCount + 1 );
        VECTOR2I center( static_cast<int>( aStart.x + dir.x * t ),
                         static_cast<int>( aStart.y + dir.y * t ) );

        double side = 1.0;
        if( !aSingleSided && ( i % 2 == 1 ) )
        {
            side = -1.0;
        }

        // Rectangular: straight up, across, and back down
        VECTOR2I up1( static_cast<int>( center.x - aSpacing / 4 ),
                      static_cast<int>( center.y ) );
        VECTOR2I top1( static_cast<int>( center.x - aSpacing / 4 + perpX * aAmplitude * side ),
                       static_cast<int>( center.y + perpY * aAmplitude * side ) );
        VECTOR2I top2( static_cast<int>( center.x + aSpacing / 4 + perpX * aAmplitude * side ),
                       static_cast<int>( center.y + perpY * aAmplitude * side ) );
        VECTOR2I down1( static_cast<int>( center.x + aSpacing / 4 ),
                        static_cast<int>( center.y ) );

        points.push_back( up1 );
        points.push_back( top1 );
        points.push_back( top2 );
        points.push_back( down1 );
    }

    points.push_back( aEnd );

    return points;
}


int64_t LENGTH_MATCHER::CalculateMeanderLength( int aAmplitude, int aSpacing,
                                                  int aMeanderCount ) const
{
    // For trapezoidal meander:
    // Each meander adds ~2*amplitude plus entry/exit segments
    double entryLen = aAmplitude / std::sqrt( 2.0 );
    double meanderLen = 2 * entryLen + aAmplitude;  // Per meander

    return static_cast<int64_t>( meanderLen * aMeanderCount );
}


bool LENGTH_MATCHER::IsValidMeander( const std::vector<VECTOR2I>& aMeanderPoints,
                                      int aLayer, int aWidth, int aNetCode ) const
{
    if( !m_searchTree )
        return true;

    if( aMeanderPoints.size() < 2 )
        return true;

    int halfWidth = aWidth / 2 + m_control.clearance;

    for( size_t i = 1; i < aMeanderPoints.size(); ++i )
    {
        const VECTOR2I& p1 = aMeanderPoints[i - 1];
        const VECTOR2I& p2 = aMeanderPoints[i];

        int minX = std::min( p1.x, p2.x ) - halfWidth;
        int minY = std::min( p1.y, p2.y ) - halfWidth;
        int maxX = std::max( p1.x, p2.x ) + halfWidth;
        int maxY = std::max( p1.y, p2.y ) + halfWidth;

        BOX2I bounds( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );

        if( m_searchTree->HasOverlap( bounds, aLayer, aNetCode ) )
            return false;
    }

    return true;
}


VECTOR2I LENGTH_MATCHER::GetPerpendicularDirection( const VECTOR2I& aStart,
                                                      const VECTOR2I& aEnd ) const
{
    VECTOR2I dir = aEnd - aStart;
    double len = std::sqrt( double( dir.x ) * dir.x + double( dir.y ) * dir.y );

    if( len < 1.0 )
        return VECTOR2I( 0, 1 );

    // Perpendicular, normalized to 1000 for precision
    return VECTOR2I( static_cast<int>( -dir.y * 1000 / len ),
                     static_cast<int>( dir.x * 1000 / len ) );
}
