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

#ifndef CONGESTION_MAP_H
#define CONGESTION_MAP_H

#include <math/vector2d.h>
#include <math/box2.h>
#include <vector>
#include <cstdint>


/**
 * Grid-based congestion tracking for PCB routing.
 *
 * Divides the board into cells and tracks how many traces pass through
 * each cell. This information is used to:
 * - Penalize routes through congested areas
 * - Spread traces more evenly across the board
 * - Avoid creating routing bottlenecks
 *
 * Based on FreeRouting's congestion-aware routing approach.
 */
class CONGESTION_MAP
{
public:
    /**
     * Create a congestion map for the given board bounds.
     *
     * @param aBounds Board bounding box
     * @param aCellSize Size of each congestion cell in nanometers
     * @param aLayerCount Number of copper layers
     */
    CONGESTION_MAP( const BOX2I& aBounds, int aCellSize = 1000000, int aLayerCount = 2 );

    /**
     * Reset all congestion values to zero.
     */
    void Clear();

    /**
     * Record a trace segment passing through the map.
     * Increments congestion for all cells the segment crosses.
     *
     * @param aStart Segment start point
     * @param aEnd Segment end point
     * @param aLayer Layer index
     * @param aWidth Trace width (affects area of influence)
     */
    void AddSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer, int aWidth );

    /**
     * Record a via at the given location.
     * Increments congestion for all layers the via spans.
     *
     * @param aLocation Via center
     * @param aFromLayer Starting layer
     * @param aToLayer Ending layer
     * @param aDiameter Via diameter
     */
    void AddVia( const VECTOR2I& aLocation, int aFromLayer, int aToLayer, int aDiameter );

    /**
     * Get congestion value at a specific point.
     *
     * @param aPoint Location to check
     * @param aLayer Layer index
     * @return Congestion value (number of traces)
     */
    int GetCongestion( const VECTOR2I& aPoint, int aLayer ) const;

    /**
     * Get average congestion for a segment.
     *
     * @param aStart Segment start
     * @param aEnd Segment end
     * @param aLayer Layer index
     * @return Average congestion along the segment
     */
    double GetSegmentCongestion( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer ) const;

    /**
     * Get maximum congestion for a segment.
     *
     * @param aStart Segment start
     * @param aEnd Segment end
     * @param aLayer Layer index
     * @return Maximum congestion along the segment
     */
    int GetMaxSegmentCongestion( const VECTOR2I& aStart, const VECTOR2I& aEnd, int aLayer ) const;

    /**
     * Calculate cost penalty for routing through a segment.
     * Higher congestion = higher cost.
     *
     * @param aStart Segment start
     * @param aEnd Segment end
     * @param aLayer Layer index
     * @param aBaseCost Base routing cost
     * @return Adjusted cost with congestion penalty
     */
    double GetCongestionCost( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                               int aLayer, double aBaseCost ) const;

    /**
     * Set the congestion cost multiplier.
     * Higher values penalize congested areas more.
     */
    void SetCongestionMultiplier( double aMultiplier ) { m_congestionMultiplier = aMultiplier; }

    /**
     * Get the congestion threshold above which penalties are applied.
     */
    int GetCongestionThreshold() const { return m_congestionThreshold; }

    /**
     * Set the congestion threshold.
     */
    void SetCongestionThreshold( int aThreshold ) { m_congestionThreshold = aThreshold; }

    /**
     * Get statistics about congestion distribution.
     */
    void GetStatistics( int& aMaxCongestion, double& aAvgCongestion, int& aCellsAboveThreshold ) const;

private:
    /**
     * Convert a point to cell indices.
     */
    void PointToCell( const VECTOR2I& aPoint, int& aCellX, int& aCellY ) const;

    /**
     * Get the cell index for a layer and position.
     */
    size_t CellIndex( int aCellX, int aCellY, int aLayer ) const;

    /**
     * Check if cell indices are valid.
     */
    bool IsValidCell( int aCellX, int aCellY, int aLayer ) const;

    /**
     * Increment congestion for a cell.
     */
    void IncrementCell( int aCellX, int aCellY, int aLayer, int aAmount = 1 );

    BOX2I  m_bounds;           ///< Board bounds
    int    m_cellSize;         ///< Cell size in nanometers
    int    m_layerCount;       ///< Number of layers
    int    m_cellsX;           ///< Number of cells in X direction
    int    m_cellsY;           ///< Number of cells in Y direction

    std::vector<uint16_t> m_congestion;  ///< Congestion values per cell per layer

    double m_congestionMultiplier = 0.5;  ///< Cost multiplier for congestion
    int    m_congestionThreshold = 2;     ///< Congestion level above which penalties apply
};


#endif // CONGESTION_MAP_H
