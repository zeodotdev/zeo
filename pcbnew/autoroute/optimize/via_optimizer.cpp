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

#include "via_optimizer.h"
#include "../search/shape_search_tree.h"
#include <board.h>
#include <algorithm>
#include <cmath>


VIA_OPTIMIZER::VIA_OPTIMIZER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
    : m_board( aBoard )
    , m_control( aControl )
{
    // Minimum segment length for considering merges (0.1mm)
    m_minSegmentLength = 100000;
}


VIA_OPTIMIZE_RESULT VIA_OPTIMIZER::Optimize( ROUTING_PATH& aPath )
{
    VIA_OPTIMIZE_RESULT result;

    if( !aPath.IsValid() || aPath.via_locations.empty() )
        return result;

    // Phase 1: Remove unnecessary vias
    result.vias_removed = RemoveUnnecessaryVias( aPath );

    // Phase 2: Optimize via positions
    result.vias_moved = OptimizeViaPositions( aPath );

    // Phase 3: Merge short segments across vias
    result.segments_merged = MergeShortSegments( aPath );

    result.improved = ( result.vias_removed > 0 ||
                        result.vias_moved > 0 ||
                        result.segments_merged > 0 );

    return result;
}


int VIA_OPTIMIZER::RemoveUnnecessaryVias( ROUTING_PATH& aPath )
{
    int viasRemoved = 0;

    // A via is unnecessary if the path continues on the same layer after it
    std::vector<PATH_POINT> newVias;

    for( size_t i = 0; i < aPath.via_locations.size(); ++i )
    {
        const PATH_POINT& via = aPath.via_locations[i];

        // Check if this via is at a layer change
        bool isLayerChange = false;

        // Find segments that connect to this via
        for( size_t j = 0; j < aPath.segments.size(); ++j )
        {
            const PATH_SEGMENT& seg = aPath.segments[j];

            for( size_t k = 0; k < seg.points.size(); ++k )
            {
                if( seg.points[k] == via.position )
                {
                    // Found a segment at this via position
                    // Check if next segment is on a different layer
                    if( j + 1 < aPath.segments.size() )
                    {
                        if( aPath.segments[j + 1].layer != seg.layer )
                        {
                            isLayerChange = true;
                        }
                    }
                    break;
                }
            }

            if( isLayerChange )
                break;
        }

        if( isLayerChange )
        {
            newVias.push_back( via );
        }
        else
        {
            viasRemoved++;
        }
    }

    aPath.via_locations = newVias;
    return viasRemoved;
}


int VIA_OPTIMIZER::OptimizeViaPositions( ROUTING_PATH& aPath )
{
    int viasMoved = 0;

    for( auto& via : aPath.via_locations )
    {
        // Find the segments connected to this via
        size_t segIdx1 = SIZE_MAX, segIdx2 = SIZE_MAX;
        size_t ptIdx1 = SIZE_MAX, ptIdx2 = SIZE_MAX;

        for( size_t i = 0; i < aPath.segments.size(); ++i )
        {
            for( size_t j = 0; j < aPath.segments[i].points.size(); ++j )
            {
                if( aPath.segments[i].points[j] == via.position )
                {
                    if( segIdx1 == SIZE_MAX )
                    {
                        segIdx1 = i;
                        ptIdx1 = j;
                    }
                    else
                    {
                        segIdx2 = i;
                        ptIdx2 = j;
                    }
                }
            }
        }

        // Need two segments to optimize
        if( segIdx1 == SIZE_MAX || segIdx2 == SIZE_MAX )
            continue;

        // Get the points before and after the via
        VECTOR2I prevPt, nextPt;
        bool havePrev = false, haveNext = false;

        if( ptIdx1 > 0 )
        {
            prevPt = aPath.segments[segIdx1].points[ptIdx1 - 1];
            havePrev = true;
        }

        if( ptIdx2 + 1 < aPath.segments[segIdx2].points.size() )
        {
            nextPt = aPath.segments[segIdx2].points[ptIdx2 + 1];
            haveNext = true;
        }

        if( !havePrev || !haveNext )
            continue;

        // Try moving the via toward the midpoint of prev-next line
        VECTOR2I midpoint( ( prevPt.x + nextPt.x ) / 2,
                           ( prevPt.y + nextPt.y ) / 2 );

        // Check if the midpoint position is valid
        if( IsValidViaPosition( midpoint, via.layer ) )
        {
            // Calculate old and new path lengths
            double oldLen = CalculateLength( prevPt, via.position ) +
                            CalculateLength( via.position, nextPt );
            double newLen = CalculateLength( prevPt, midpoint ) +
                            CalculateLength( midpoint, nextPt );

            // Only move if it reduces length
            if( newLen < oldLen - 1000 )  // 1nm tolerance
            {
                // Update via position
                VECTOR2I oldPos = via.position;
                via.position = midpoint;

                // Update segment points
                aPath.segments[segIdx1].points[ptIdx1] = midpoint;
                aPath.segments[segIdx2].points[ptIdx2] = midpoint;

                viasMoved++;
            }
        }
    }

    return viasMoved;
}


int VIA_OPTIMIZER::MergeShortSegments( ROUTING_PATH& aPath )
{
    int segmentsMerged = 0;

    // Look for very short segments on either side of a via
    std::vector<size_t> viasToRemove;

    for( size_t i = 0; i < aPath.via_locations.size(); ++i )
    {
        const PATH_POINT& via = aPath.via_locations[i];

        // Find segments connected to this via
        size_t segBefore = SIZE_MAX, segAfter = SIZE_MAX;
        double lenBefore = 0, lenAfter = 0;

        for( size_t j = 0; j < aPath.segments.size(); ++j )
        {
            const PATH_SEGMENT& seg = aPath.segments[j];

            // Check if segment ends at via
            if( !seg.points.empty() && seg.points.back() == via.position )
            {
                segBefore = j;
                if( seg.points.size() >= 2 )
                {
                    lenBefore = CalculateLength( seg.points[seg.points.size() - 2],
                                                  seg.points.back() );
                }
            }

            // Check if segment starts at via
            if( !seg.points.empty() && seg.points.front() == via.position )
            {
                segAfter = j;
                if( seg.points.size() >= 2 )
                {
                    lenAfter = CalculateLength( seg.points[0], seg.points[1] );
                }
            }
        }

        // If both segments are short, consider removing the via
        if( segBefore != SIZE_MAX && segAfter != SIZE_MAX &&
            lenBefore < m_minSegmentLength && lenAfter < m_minSegmentLength )
        {
            // Check if we can route on a single layer
            if( aPath.segments[segBefore].layer == aPath.segments[segAfter].layer )
            {
                viasToRemove.push_back( i );
                segmentsMerged++;
            }
        }
    }

    // Remove marked vias (in reverse order to preserve indices)
    for( auto it = viasToRemove.rbegin(); it != viasToRemove.rend(); ++it )
    {
        aPath.via_locations.erase( aPath.via_locations.begin() + *it );
    }

    return segmentsMerged;
}


bool VIA_OPTIMIZER::IsValidViaPosition( const VECTOR2I& aPosition, int aLayer )
{
    if( !m_searchTree )
        return true;

    int halfDia = m_control.via_diameter / 2 + m_control.clearance;

    BOX2I viaBounds( VECTOR2I( aPosition.x - halfDia, aPosition.y - halfDia ),
                     VECTOR2I( halfDia * 2, halfDia * 2 ) );

    return !m_searchTree->HasOverlap( viaBounds, aLayer, m_netCode );
}


double VIA_OPTIMIZER::CalculateLength( const VECTOR2I& aStart, const VECTOR2I& aEnd )
{
    double dx = aEnd.x - aStart.x;
    double dy = aEnd.y - aStart.y;
    return std::sqrt( dx * dx + dy * dy );
}


bool VIA_OPTIMIZER::FindViaInPath( const ROUTING_PATH& aPath, const PATH_POINT& aVia,
                                    size_t& aSegmentIndex, size_t& aPointIndex )
{
    for( size_t i = 0; i < aPath.segments.size(); ++i )
    {
        for( size_t j = 0; j < aPath.segments[i].points.size(); ++j )
        {
            if( aPath.segments[i].points[j] == aVia.position )
            {
                aSegmentIndex = i;
                aPointIndex = j;
                return true;
            }
        }
    }

    return false;
}
