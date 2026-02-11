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

#include "congestion_map.h"
#include <algorithm>
#include <cmath>


CONGESTION_MAP::CONGESTION_MAP( const BOX2I& aBounds, int aCellSize, int aLayerCount )
    : m_bounds( aBounds )
    , m_cellSize( aCellSize )
    , m_layerCount( aLayerCount )
{
    // Calculate grid dimensions
    m_cellsX = static_cast<int>( std::max( 1LL, ( m_bounds.GetWidth() + m_cellSize - 1 ) / m_cellSize ) );
    m_cellsY = static_cast<int>( std::max( 1LL, ( m_bounds.GetHeight() + m_cellSize - 1 ) / m_cellSize ) );

    // Allocate congestion grid
    size_t totalCells = static_cast<size_t>( m_cellsX ) * m_cellsY * m_layerCount;
    m_congestion.resize( totalCells, 0 );
}


void CONGESTION_MAP::Clear()
{
    std::fill( m_congestion.begin(), m_congestion.end(), 0 );
}


void CONGESTION_MAP::PointToCell( const VECTOR2I& aPoint, int& aCellX, int& aCellY ) const
{
    int offsetX = aPoint.x - m_bounds.GetX();
    int offsetY = aPoint.y - m_bounds.GetY();

    aCellX = std::clamp( offsetX / m_cellSize, 0, m_cellsX - 1 );
    aCellY = std::clamp( offsetY / m_cellSize, 0, m_cellsY - 1 );
}


size_t CONGESTION_MAP::CellIndex( int aCellX, int aCellY, int aLayer ) const
{
    return static_cast<size_t>( aLayer ) * m_cellsX * m_cellsY +
           static_cast<size_t>( aCellY ) * m_cellsX +
           static_cast<size_t>( aCellX );
}


bool CONGESTION_MAP::IsValidCell( int aCellX, int aCellY, int aLayer ) const
{
    return aCellX >= 0 && aCellX < m_cellsX &&
           aCellY >= 0 && aCellY < m_cellsY &&
           aLayer >= 0 && aLayer < m_layerCount;
}


void CONGESTION_MAP::IncrementCell( int aCellX, int aCellY, int aLayer, int aAmount )
{
    if( IsValidCell( aCellX, aCellY, aLayer ) )
    {
        size_t idx = CellIndex( aCellX, aCellY, aLayer );
        m_congestion[idx] = static_cast<uint16_t>(
            std::min( static_cast<int>( m_congestion[idx] ) + aAmount, 65535 ) );
    }
}


void CONGESTION_MAP::AddSegment( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                  int aLayer, int aWidth )
{
    // Use Bresenham's line algorithm to trace through cells
    int startCellX, startCellY, endCellX, endCellY;
    PointToCell( aStart, startCellX, startCellY );
    PointToCell( aEnd, endCellX, endCellY );

    int dx = std::abs( endCellX - startCellX );
    int dy = std::abs( endCellY - startCellY );
    int sx = ( startCellX < endCellX ) ? 1 : -1;
    int sy = ( startCellY < endCellY ) ? 1 : -1;
    int err = dx - dy;

    int cellX = startCellX;
    int cellY = startCellY;

    // Calculate how many adjacent cells to affect based on trace width
    int affectedCells = std::max( 1, aWidth / m_cellSize );
    int halfAffected = affectedCells / 2;

    while( true )
    {
        // Increment this cell and adjacent cells based on trace width
        for( int offsetX = -halfAffected; offsetX <= halfAffected; ++offsetX )
        {
            for( int offsetY = -halfAffected; offsetY <= halfAffected; ++offsetY )
            {
                IncrementCell( cellX + offsetX, cellY + offsetY, aLayer );
            }
        }

        if( cellX == endCellX && cellY == endCellY )
            break;

        int e2 = 2 * err;

        if( e2 > -dy )
        {
            err -= dy;
            cellX += sx;
        }

        if( e2 < dx )
        {
            err += dx;
            cellY += sy;
        }
    }
}


void CONGESTION_MAP::AddVia( const VECTOR2I& aLocation, int aFromLayer, int aToLayer,
                              int aDiameter )
{
    int cellX, cellY;
    PointToCell( aLocation, cellX, cellY );

    // Calculate how many cells the via affects
    int affectedCells = std::max( 1, aDiameter / m_cellSize );
    int halfAffected = affectedCells / 2;

    // Vias increment congestion on all layers they span
    int minLayer = std::min( aFromLayer, aToLayer );
    int maxLayer = std::max( aFromLayer, aToLayer );

    for( int layer = minLayer; layer <= maxLayer; ++layer )
    {
        for( int offsetX = -halfAffected; offsetX <= halfAffected; ++offsetX )
        {
            for( int offsetY = -halfAffected; offsetY <= halfAffected; ++offsetY )
            {
                IncrementCell( cellX + offsetX, cellY + offsetY, layer );
            }
        }
    }
}


int CONGESTION_MAP::GetCongestion( const VECTOR2I& aPoint, int aLayer ) const
{
    int cellX, cellY;
    PointToCell( aPoint, cellX, cellY );

    if( !IsValidCell( cellX, cellY, aLayer ) )
        return 0;

    return m_congestion[CellIndex( cellX, cellY, aLayer )];
}


double CONGESTION_MAP::GetSegmentCongestion( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                              int aLayer ) const
{
    // Sample congestion along the segment
    double dx = aEnd.x - aStart.x;
    double dy = aEnd.y - aStart.y;
    double length = std::sqrt( dx * dx + dy * dy );

    if( length < 1.0 )
        return GetCongestion( aStart, aLayer );

    int numSamples = std::max( 2, static_cast<int>( length / m_cellSize ) + 1 );
    double totalCongestion = 0.0;

    for( int i = 0; i < numSamples; ++i )
    {
        double t = static_cast<double>( i ) / ( numSamples - 1 );
        VECTOR2I sample( static_cast<int>( aStart.x + dx * t ),
                         static_cast<int>( aStart.y + dy * t ) );
        totalCongestion += GetCongestion( sample, aLayer );
    }

    return totalCongestion / numSamples;
}


int CONGESTION_MAP::GetMaxSegmentCongestion( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                              int aLayer ) const
{
    // Find maximum congestion along the segment
    double dx = aEnd.x - aStart.x;
    double dy = aEnd.y - aStart.y;
    double length = std::sqrt( dx * dx + dy * dy );

    if( length < 1.0 )
        return GetCongestion( aStart, aLayer );

    int numSamples = std::max( 2, static_cast<int>( length / m_cellSize ) + 1 );
    int maxCongestion = 0;

    for( int i = 0; i < numSamples; ++i )
    {
        double t = static_cast<double>( i ) / ( numSamples - 1 );
        VECTOR2I sample( static_cast<int>( aStart.x + dx * t ),
                         static_cast<int>( aStart.y + dy * t ) );
        maxCongestion = std::max( maxCongestion, GetCongestion( sample, aLayer ) );
    }

    return maxCongestion;
}


double CONGESTION_MAP::GetCongestionCost( const VECTOR2I& aStart, const VECTOR2I& aEnd,
                                           int aLayer, double aBaseCost ) const
{
    double avgCongestion = GetSegmentCongestion( aStart, aEnd, aLayer );

    // Only apply penalty if congestion exceeds threshold
    if( avgCongestion <= m_congestionThreshold )
        return aBaseCost;

    // Penalty increases quadratically with congestion above threshold
    double excessCongestion = avgCongestion - m_congestionThreshold;
    double penalty = m_congestionMultiplier * excessCongestion * excessCongestion;

    return aBaseCost * ( 1.0 + penalty );
}


void CONGESTION_MAP::GetStatistics( int& aMaxCongestion, double& aAvgCongestion,
                                     int& aCellsAboveThreshold ) const
{
    aMaxCongestion = 0;
    aAvgCongestion = 0.0;
    aCellsAboveThreshold = 0;

    int64_t totalCongestion = 0;
    int nonZeroCells = 0;

    for( size_t i = 0; i < m_congestion.size(); ++i )
    {
        int value = m_congestion[i];

        if( value > 0 )
        {
            nonZeroCells++;
            totalCongestion += value;

            if( value > aMaxCongestion )
                aMaxCongestion = value;

            if( value > m_congestionThreshold )
                aCellsAboveThreshold++;
        }
    }

    if( nonZeroCells > 0 )
        aAvgCongestion = static_cast<double>( totalCongestion ) / nonZeroCells;
}
