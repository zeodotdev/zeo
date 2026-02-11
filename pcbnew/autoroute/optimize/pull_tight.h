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

#ifndef PULL_TIGHT_H
#define PULL_TIGHT_H

#include "../locate/locate_connection.h"
#include "../search/shape_search_tree.h"
#include "../autoroute_control.h"
#include <math/vector2d.h>
#include <vector>

// Forward declarations
class BOARD;


/**
 * Mode for pull-tight optimization.
 */
enum class PULL_TIGHT_MODE
{
    ORTHOGONAL,   ///< 90-degree angles only
    FORTYFIVE,    ///< 45-degree angles allowed
    ANY_ANGLE     ///< Any angle allowed
};


/**
 * Result of pull-tight optimization.
 */
struct PULL_TIGHT_RESULT
{
    bool   improved = false;       ///< True if path was improved
    int    iterations = 0;         ///< Number of optimization iterations
    double length_before = 0.0;    ///< Path length before optimization
    double length_after = 0.0;     ///< Path length after optimization
    double improvement_pct = 0.0;  ///< Percentage improvement
};


/**
 * Pull-tight optimization algorithm.
 *
 * Based on FreeRouting's PullTightAlgo. This algorithm iteratively moves
 * vertices in a routing path to reduce the total path length while
 * maintaining clearance from obstacles.
 *
 * Three modes are supported:
 * - ORTHOGONAL: Only 90-degree angles (horizontal/vertical)
 * - FORTYFIVE: 45-degree angles allowed (octagonal grid)
 * - ANY_ANGLE: Any angle allowed (smooth curves)
 */
class PULL_TIGHT
{
public:
    PULL_TIGHT( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the search tree for collision detection.
     */
    void SetSearchTree( SHAPE_SEARCH_TREE* aTree ) { m_searchTree = aTree; }

    /**
     * Set the net code being optimized (for same-net filtering).
     */
    void SetNetCode( int aNetCode ) { m_netCode = aNetCode; }

    /**
     * Set the optimization mode.
     */
    void SetMode( PULL_TIGHT_MODE aMode ) { m_mode = aMode; }

    /**
     * Set maximum iterations for optimization.
     */
    void SetMaxIterations( int aIterations ) { m_maxIterations = aIterations; }

    /**
     * Optimize a routing path segment.
     *
     * @param aSegment The path segment to optimize (modified in place)
     * @return Result of optimization
     */
    PULL_TIGHT_RESULT Optimize( PATH_SEGMENT& aSegment );

    /**
     * Optimize an entire routing path.
     *
     * @param aPath The routing path to optimize (modified in place)
     * @return Result of optimization
     */
    PULL_TIGHT_RESULT OptimizePath( ROUTING_PATH& aPath );

private:
    /**
     * Try to move a single vertex to reduce path length.
     *
     * @param aPoints The point list
     * @param aIndex Index of the vertex to move
     * @param aLayer The layer for collision checking
     * @param aWidth The trace width
     * @return True if vertex was moved
     */
    bool TryMoveVertex( std::vector<VECTOR2I>& aPoints, size_t aIndex,
                        int aLayer, int aWidth );

    /**
     * Calculate the optimal position for a vertex.
     *
     * @param aPrev Previous vertex
     * @param aCurrent Current vertex (to be moved)
     * @param aNext Next vertex
     * @return Optimal new position
     */
    VECTOR2I CalculateOptimalPosition( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                        const VECTOR2I& aNext );

    /**
     * Calculate optimal position for 90-degree mode.
     */
    VECTOR2I CalculateOptimal90( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                  const VECTOR2I& aNext );

    /**
     * Calculate optimal position for 45-degree mode.
     */
    VECTOR2I CalculateOptimal45( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                  const VECTOR2I& aNext );

    /**
     * Calculate optimal position for any-angle mode.
     */
    VECTOR2I CalculateOptimalAny( const VECTOR2I& aPrev, const VECTOR2I& aCurrent,
                                   const VECTOR2I& aNext );

    /**
     * Check if a trace segment at given position would collide with obstacles.
     *
     * @param aStart Segment start
     * @param aEnd Segment end
     * @param aLayer The layer
     * @param aWidth The trace width
     * @return True if position is valid (no collision)
     */
    bool IsValidPosition( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                          int aLayer, int aWidth );

    /**
     * Calculate path length for a segment.
     */
    double CalculateLength( const std::vector<VECTOR2I>& aPoints );

    /**
     * Snap a point to the allowed angle grid.
     *
     * @param aPoint The point to snap
     * @param aReference Reference point for angle calculation
     * @return Snapped point
     */
    VECTOR2I SnapToAngleGrid( const VECTOR2I& aPoint, const VECTOR2I& aReference );

    /**
     * Remove collinear points from a path.
     */
    void RemoveCollinearPoints( std::vector<VECTOR2I>& aPoints );

    BOARD*              m_board;
    SHAPE_SEARCH_TREE*  m_searchTree = nullptr;
    AUTOROUTE_CONTROL   m_control;
    int                 m_netCode = 0;
    PULL_TIGHT_MODE     m_mode = PULL_TIGHT_MODE::FORTYFIVE;
    int                 m_maxIterations = 50;
};


#endif // PULL_TIGHT_H
