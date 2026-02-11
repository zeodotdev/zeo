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

#include "shape_search_tree.h"
#include "../expansion/expansion_room.h"
#include <board_item.h>
#include <algorithm>


SHAPE_SEARCH_TREE::SHAPE_SEARCH_TREE()
    : m_cellSize( 1000000 )  // 1mm default
    , m_gridWidth( 0 )
    , m_gridHeight( 0 )
    , m_layerCount( 2 )
{
}


SHAPE_SEARCH_TREE::~SHAPE_SEARCH_TREE()
{
    Clear();
}


void SHAPE_SEARCH_TREE::Initialize( const BOX2I& aBounds, int aCellSize, int aLayerCount )
{
    Clear();

    m_bounds = aBounds;
    m_cellSize = std::max( aCellSize, 100000 );  // Minimum 0.1mm cells
    m_layerCount = std::max( aLayerCount, 1 );

    // Calculate grid dimensions
    m_gridWidth = ( m_bounds.GetWidth() + m_cellSize - 1 ) / m_cellSize + 1;
    m_gridHeight = ( m_bounds.GetHeight() + m_cellSize - 1 ) / m_cellSize + 1;

    // Pre-allocate grid cells
    int totalCells = m_gridWidth * m_gridHeight * m_layerCount;
    m_cells.resize( totalCells );
}


void SHAPE_SEARCH_TREE::Clear()
{
    m_cells.clear();
    m_roomToCells.clear();
    m_itemToCells.clear();
}


std::pair<int, int> SHAPE_SEARCH_TREE::WorldToCell( int aX, int aY ) const
{
    int cellX = ( aX - m_bounds.GetX() ) / m_cellSize;
    int cellY = ( aY - m_bounds.GetY() ) / m_cellSize;

    // Clamp to valid range
    cellX = std::max( 0, std::min( cellX, m_gridWidth - 1 ) );
    cellY = std::max( 0, std::min( cellY, m_gridHeight - 1 ) );

    return { cellX, cellY };
}


int SHAPE_SEARCH_TREE::CellIndex( int aCellX, int aCellY, int aLayer ) const
{
    return ( aLayer * m_gridHeight + aCellY ) * m_gridWidth + aCellX;
}


void SHAPE_SEARCH_TREE::GetCellRange( const BOX2I& aBounds, int& aMinCellX, int& aMinCellY,
                                       int& aMaxCellX, int& aMaxCellY ) const
{
    auto [minX, minY] = WorldToCell( aBounds.GetX(), aBounds.GetY() );
    auto [maxX, maxY] = WorldToCell( aBounds.GetRight(), aBounds.GetBottom() );

    aMinCellX = minX;
    aMinCellY = minY;
    aMaxCellX = maxX;
    aMaxCellY = maxY;
}


void SHAPE_SEARCH_TREE::Insert( EXPANSION_ROOM* aRoom )
{
    if( !aRoom || m_cells.empty() )
        return;

    int layer = aRoom->GetLayer();
    if( layer < 0 || layer >= m_layerCount )
        return;

    BOX2I bounds = aRoom->GetBoundingBox();

    int minCellX, minCellY, maxCellX, maxCellY;
    GetCellRange( bounds, minCellX, minCellY, maxCellX, maxCellY );

    TREE_ENTRY entry;
    entry.room = aRoom;
    entry.layer = layer;

    std::vector<int>& cellList = m_roomToCells[aRoom];

    for( int cellY = minCellY; cellY <= maxCellY; ++cellY )
    {
        for( int cellX = minCellX; cellX <= maxCellX; ++cellX )
        {
            int idx = CellIndex( cellX, cellY, layer );
            m_cells[idx].insert( entry );
            cellList.push_back( idx );
        }
    }
}


void SHAPE_SEARCH_TREE::Insert( BOARD_ITEM* aItem, const BOX2I& aShape, int aLayer )
{
    if( !aItem || m_cells.empty() )
        return;

    if( aLayer < 0 || aLayer >= m_layerCount )
        return;

    int minCellX, minCellY, maxCellX, maxCellY;
    GetCellRange( aShape, minCellX, minCellY, maxCellX, maxCellY );

    TREE_ENTRY entry;
    entry.item = aItem;
    entry.layer = aLayer;

    auto key = std::make_pair( aItem, aLayer );
    std::vector<int>& cellList = m_itemToCells[key];

    for( int cellY = minCellY; cellY <= maxCellY; ++cellY )
    {
        for( int cellX = minCellX; cellX <= maxCellX; ++cellX )
        {
            int idx = CellIndex( cellX, cellY, aLayer );
            m_cells[idx].insert( entry );
            cellList.push_back( idx );
        }
    }
}


void SHAPE_SEARCH_TREE::Remove( EXPANSION_ROOM* aRoom )
{
    if( !aRoom )
        return;

    auto it = m_roomToCells.find( aRoom );
    if( it == m_roomToCells.end() )
        return;

    TREE_ENTRY entry;
    entry.room = aRoom;
    entry.layer = aRoom->GetLayer();

    for( int idx : it->second )
    {
        if( idx >= 0 && idx < static_cast<int>( m_cells.size() ) )
        {
            m_cells[idx].erase( entry );
        }
    }

    m_roomToCells.erase( it );
}


void SHAPE_SEARCH_TREE::Remove( BOARD_ITEM* aItem, int aLayer )
{
    if( !aItem )
        return;

    auto key = std::make_pair( aItem, aLayer );
    auto it = m_itemToCells.find( key );
    if( it == m_itemToCells.end() )
        return;

    TREE_ENTRY entry;
    entry.item = aItem;
    entry.layer = aLayer;

    for( int idx : it->second )
    {
        if( idx >= 0 && idx < static_cast<int>( m_cells.size() ) )
        {
            m_cells[idx].erase( entry );
        }
    }

    m_itemToCells.erase( it );
}


void SHAPE_SEARCH_TREE::QueryOverlapping( const BOX2I& aBounds, int aLayer,
                                           std::function<bool( const TREE_ENTRY& )> aCallback ) const
{
    if( m_cells.empty() || aLayer < 0 || aLayer >= m_layerCount )
        return;

    int minCellX, minCellY, maxCellX, maxCellY;
    GetCellRange( aBounds, minCellX, minCellY, maxCellX, maxCellY );

    // Track which entries we've already reported to avoid duplicates
    std::unordered_set<TREE_ENTRY, TreeEntryHash> reported;

    for( int cellY = minCellY; cellY <= maxCellY; ++cellY )
    {
        for( int cellX = minCellX; cellX <= maxCellX; ++cellX )
        {
            int idx = CellIndex( cellX, cellY, aLayer );
            const auto& cell = m_cells[idx];

            for( const TREE_ENTRY& entry : cell )
            {
                if( reported.find( entry ) != reported.end() )
                    continue;

                reported.insert( entry );

                // Verify actual overlap with the query bounds
                BOX2I entryBounds;
                if( entry.room )
                {
                    entryBounds = entry.room->GetBoundingBox();
                }
                else if( entry.item )
                {
                    entryBounds = entry.item->GetBoundingBox();
                }
                else
                {
                    continue;
                }

                // Check if bounds actually intersect
                if( !aBounds.Intersects( entryBounds ) )
                    continue;

                if( !aCallback( entry ) )
                    return;  // Callback requested stop
            }
        }
    }
}


std::vector<TREE_ENTRY> SHAPE_SEARCH_TREE::GetOverlapping( const BOX2I& aBounds, int aLayer ) const
{
    std::vector<TREE_ENTRY> result;

    QueryOverlapping( aBounds, aLayer, [&result]( const TREE_ENTRY& entry ) {
        result.push_back( entry );
        return true;  // Continue
    } );

    return result;
}


void SHAPE_SEARCH_TREE::QueryOverlappingWithNet( const BOX2I& aBounds, int aLayer, int aExcludeNet,
                                                   std::function<bool( const TREE_ENTRY& )> aCallback ) const
{
    QueryOverlapping( aBounds, aLayer, [&aCallback, aExcludeNet]( const TREE_ENTRY& entry ) {
        // Skip entries from the same net (they're not obstacles)
        if( aExcludeNet >= 0 )
        {
            if( entry.room && entry.room->GetNetCode() == aExcludeNet )
                return true;  // Skip, continue searching
        }

        return aCallback( entry );
    } );
}


std::vector<TREE_ENTRY> SHAPE_SEARCH_TREE::GetObstacles( const BOX2I& aBounds, int aLayer,
                                                          int aExcludeNet ) const
{
    std::vector<TREE_ENTRY> result;

    QueryOverlappingWithNet( aBounds, aLayer, aExcludeNet, [&result]( const TREE_ENTRY& entry ) {
        // Only include obstacle rooms
        if( entry.room && entry.room->GetType() == ROOM_TYPE::OBSTACLE )
        {
            result.push_back( entry );
        }
        return true;  // Continue
    } );

    return result;
}


bool SHAPE_SEARCH_TREE::IsBlocked( const VECTOR2I& aPoint, int aLayer, int aExcludeNet ) const
{
    // Create a small query box around the point
    BOX2I queryBox( aPoint, VECTOR2I( 1, 1 ) );

    bool blocked = false;

    QueryOverlapping( queryBox, aLayer, [&blocked, &aPoint, aExcludeNet]( const TREE_ENTRY& entry ) {
        if( entry.room )
        {
            if( entry.room->GetType() == ROOM_TYPE::OBSTACLE )
            {
                if( aExcludeNet < 0 || entry.room->GetNetCode() != aExcludeNet )
                {
                    if( entry.room->Contains( aPoint ) )
                    {
                        blocked = true;
                        return false;  // Stop iteration
                    }
                }
            }
        }
        return true;  // Continue
    } );

    return blocked;
}


bool SHAPE_SEARCH_TREE::HasOverlap( const BOX2I& aBounds, int aLayer, int aExcludeNet ) const
{
    bool hasOverlap = false;

    QueryOverlapping( aBounds, aLayer, [&hasOverlap, &aBounds, aExcludeNet]( const TREE_ENTRY& entry ) {
        if( entry.room )
        {
            if( entry.room->GetType() == ROOM_TYPE::OBSTACLE )
            {
                if( aExcludeNet < 0 || entry.room->GetNetCode() != aExcludeNet )
                {
                    // Check if bounds truly overlap (not just touch)
                    BOX2I roomBounds = entry.room->GetBoundingBox();
                    if( aBounds.GetX() < roomBounds.GetRight() &&
                        aBounds.GetRight() > roomBounds.GetX() &&
                        aBounds.GetY() < roomBounds.GetBottom() &&
                        aBounds.GetBottom() > roomBounds.GetY() )
                    {
                        hasOverlap = true;
                        return false;  // Stop iteration
                    }
                }
            }
        }
        return true;  // Continue
    } );

    return hasOverlap;
}
