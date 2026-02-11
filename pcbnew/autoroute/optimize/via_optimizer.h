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

#ifndef VIA_OPTIMIZER_H
#define VIA_OPTIMIZER_H

#include "../locate/locate_connection.h"
#include "../autoroute_control.h"
#include <math/vector2d.h>
#include <vector>

// Forward declarations
class BOARD;
class SHAPE_SEARCH_TREE;


/**
 * Result of via optimization.
 */
struct VIA_OPTIMIZE_RESULT
{
    int vias_removed = 0;      ///< Number of vias removed
    int vias_moved = 0;        ///< Number of vias repositioned
    int segments_merged = 0;   ///< Number of segments merged
    bool improved = false;     ///< True if any improvement was made
};


/**
 * Via optimization algorithm.
 *
 * Optimizes routing paths by:
 * 1. Removing unnecessary vias (where layer change isn't needed)
 * 2. Moving vias to better positions (shorter path)
 * 3. Merging short segments across vias
 */
class VIA_OPTIMIZER
{
public:
    VIA_OPTIMIZER( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aTree ) { m_searchTree = aTree; }

    /**
     * Set the net code being optimized.
     */
    void SetNetCode( int aNetCode ) { m_netCode = aNetCode; }

    /**
     * Optimize a routing path to reduce via count and improve routing.
     *
     * @param aPath The path to optimize (modified in place)
     * @return Optimization result
     */
    VIA_OPTIMIZE_RESULT Optimize( ROUTING_PATH& aPath );

private:
    /**
     * Try to remove unnecessary vias.
     * A via is unnecessary if the segments before and after it are on the same layer.
     */
    int RemoveUnnecessaryVias( ROUTING_PATH& aPath );

    /**
     * Try to move vias to better positions.
     * Better positions reduce total path length while maintaining clearance.
     */
    int OptimizeViaPositions( ROUTING_PATH& aPath );

    /**
     * Merge short segments across vias.
     * If a via has very short segments on both sides, try to eliminate it.
     */
    int MergeShortSegments( ROUTING_PATH& aPath );

    /**
     * Check if a via at the given position would be valid.
     */
    bool IsValidViaPosition( const VECTOR2I& aPosition, int aLayer );

    /**
     * Calculate the path length between two points.
     */
    double CalculateLength( const VECTOR2I& aStart, const VECTOR2I& aEnd );

    /**
     * Find the segment containing a via point.
     * Returns the segment index and point index within the segment.
     */
    bool FindViaInPath( const ROUTING_PATH& aPath, const PATH_POINT& aVia,
                        size_t& aSegmentIndex, size_t& aPointIndex );

    BOARD*              m_board;
    SHAPE_SEARCH_TREE*  m_searchTree = nullptr;
    AUTOROUTE_CONTROL   m_control;
    int                 m_netCode = 0;
    int                 m_minSegmentLength;  ///< Minimum segment length to consider for merging
};


#endif // VIA_OPTIMIZER_H
