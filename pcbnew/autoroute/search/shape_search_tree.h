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

#ifndef SHAPE_SEARCH_TREE_H
#define SHAPE_SEARCH_TREE_H

#include "../geometry/tile_shape.h"
#include <math/box2.h>
#include <math/vector2d.h>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Forward declarations
class EXPANSION_ROOM;
class BOARD_ITEM;
class BOARD_CONNECTED_ITEM;


// Hash function for pair<BOARD_ITEM*, int> - must be defined before use
namespace std {
template<>
struct hash<std::pair<BOARD_ITEM*, int>>
{
    std::size_t operator()( const std::pair<BOARD_ITEM*, int>& p ) const
    {
        return std::hash<void*>{}( p.first ) ^ ( std::hash<int>{}( p.second ) << 1 );
    }
};
}


/**
 * Entry in the search tree containing an object and its shape.
 *
 * Entries can represent either:
 * - A BOARD_ITEM directly (obstacle items stored without creating OBSTACLE_ROOM)
 * - An EXPANSION_ROOM (free space rooms, or lazily-created obstacle rooms)
 *
 * For obstacle detection, items are stored directly. OBSTACLE_ROOMs are only
 * created lazily when needed for door creation during expansion.
 */
struct TREE_ENTRY
{
    EXPANSION_ROOM* room = nullptr;
    BOARD_ITEM*     item = nullptr;  // Primary storage for obstacles (not wrapped in room)
    int             layer = 0;
    int             shape_index = 0;  // For items with multiple shapes

    bool operator==( const TREE_ENTRY& other ) const
    {
        return room == other.room && item == other.item &&
               layer == other.layer && shape_index == other.shape_index;
    }

    /**
     * Check if this entry is an item (not a room).
     */
    bool IsItem() const { return item != nullptr && room == nullptr; }

    /**
     * Check if this entry is a room.
     */
    bool IsRoom() const { return room != nullptr; }

    /**
     * Check if this entry represents a trace obstacle for a given net.
     *
     * An entry is a trace obstacle if:
     * - It has an item AND
     * - The item belongs to a different net than aNetCode
     *
     * Items with the same net code are not obstacles (same-net items can overlap).
     * Non-connected items (like board edges) are always obstacles.
     *
     * @param aNetCode The net code of the trace being routed.
     * @return True if this entry blocks routing for the given net.
     */
    bool IsTraceObstacle( int aNetCode ) const;
};


/**
 * Hash function for TREE_ENTRY.
 */
struct TreeEntryHash
{
    std::size_t operator()( const TREE_ENTRY& e ) const
    {
        std::size_t h1 = std::hash<void*>{}( e.room );
        std::size_t h2 = std::hash<void*>{}( e.item );
        std::size_t h3 = std::hash<int>{}( e.layer );
        return h1 ^ ( h2 << 1 ) ^ ( h3 << 2 );
    }
};


/**
 * Spatial search tree for efficient queries of overlapping shapes.
 *
 * Uses a grid-based spatial index. Each grid cell maintains a list
 * of objects that overlap with it. This provides O(1) average case
 * for queries with small regions.
 *
 * Inspired by FreeRouting's ShapeSearchTree but simplified for
 * axis-aligned boxes which are our primary shape type.
 */
class SHAPE_SEARCH_TREE
{
public:
    SHAPE_SEARCH_TREE();
    ~SHAPE_SEARCH_TREE();

    /**
     * Initialize the search tree with the board bounds.
     *
     * @param aBounds The bounding box of the board area.
     * @param aCellSize The size of each grid cell (typically 2-4x clearance).
     * @param aLayerCount Number of copper layers.
     */
    void Initialize( const BOX2I& aBounds, int aCellSize, int aLayerCount );

    /**
     * Clear all entries from the tree.
     */
    void Clear();

    /**
     * Insert a room into the search tree.
     */
    void Insert( EXPANSION_ROOM* aRoom );

    /**
     * Insert a board item (obstacle) into the search tree.
     *
     * This stores the item directly without wrapping it in an OBSTACLE_ROOM.
     * The item's net code is used for same-net exclusion during queries.
     *
     * @param aItem The board item to insert.
     * @param aBounds The bounding box of the item (with clearance applied).
     * @param aLayer The layer the item is on.
     */
    void Insert( BOARD_ITEM* aItem, const BOX2I& aBounds, int aLayer );

    /**
     * Insert a board item (obstacle) into the search tree.
     *
     * This is an explicit alias for Insert(BOARD_ITEM*, BOX2I&, int) to make
     * the intent clear when inserting obstacle items.
     *
     * @param aItem The board item to insert.
     * @param aBounds The bounding box of the item (with clearance applied).
     * @param aLayer The layer the item is on.
     */
    void InsertItem( BOARD_ITEM* aItem, const BOX2I& aBounds, int aLayer )
    {
        Insert( aItem, aBounds, aLayer );
    }

    /**
     * Remove a room from the search tree.
     */
    void Remove( EXPANSION_ROOM* aRoom );

    /**
     * Remove a board item from the search tree.
     */
    void Remove( BOARD_ITEM* aItem, int aLayer );

    /**
     * Find all entries that overlap with a given bounding box on a layer.
     *
     * @param aBounds The query bounding box.
     * @param aLayer The layer to query.
     * @param aCallback Callback function called for each overlapping entry.
     *                  Return false to stop iteration.
     */
    void QueryOverlapping( const BOX2I& aBounds, int aLayer,
                           std::function<bool( const TREE_ENTRY& )> aCallback ) const;

    /**
     * Find all entries that overlap with a given bounding box on a layer,
     * excluding entries from the specified net (same-net items are not obstacles).
     *
     * @param aBounds The query bounding box.
     * @param aLayer The layer to query.
     * @param aExcludeNet Net code to exclude from results (-1 = no exclusion).
     * @param aCallback Callback function called for each overlapping entry.
     *                  Return false to stop iteration.
     */
    void QueryOverlappingWithNet( const BOX2I& aBounds, int aLayer, int aExcludeNet,
                                   std::function<bool( const TREE_ENTRY& )> aCallback ) const;

    /**
     * Find all entries that overlap with a given bounding box on a layer.
     *
     * @param aBounds The query bounding box.
     * @param aLayer The layer to query.
     * @return Vector of overlapping entries.
     */
    std::vector<TREE_ENTRY> GetOverlapping( const BOX2I& aBounds, int aLayer ) const;

    /**
     * Find all obstacle entries that overlap with a given bounding box,
     * excluding same-net items.
     *
     * @param aBounds The query bounding box.
     * @param aLayer The layer to query.
     * @param aExcludeNet Net code to exclude from results.
     * @return Vector of overlapping obstacle entries.
     */
    std::vector<TREE_ENTRY> GetObstacles( const BOX2I& aBounds, int aLayer, int aExcludeNet ) const;

    /**
     * Check if a point on a layer is blocked by any obstacle.
     *
     * @param aPoint The point to check.
     * @param aLayer The layer to check.
     * @param aExcludeNet Net code to exclude from blocking check.
     * @return True if blocked by an obstacle.
     */
    bool IsBlocked( const VECTOR2I& aPoint, int aLayer, int aExcludeNet = -1 ) const;

    /**
     * Check if a bounding box on a layer overlaps any obstacle.
     */
    bool HasOverlap( const BOX2I& aBounds, int aLayer, int aExcludeNet = -1 ) const;

    /**
     * Get the bounds of the search tree.
     */
    const BOX2I& GetBounds() const { return m_bounds; }

    /**
     * Get the cell size.
     */
    int GetCellSize() const { return m_cellSize; }

    /**
     * Get the number of items in the tree (for debugging).
     */
    size_t GetItemCount() const { return m_itemToCells.size(); }

    /**
     * Get the number of rooms in the tree (for debugging).
     */
    size_t GetRoomCount() const { return m_roomToCells.size(); }

    /**
     * Get the layer count.
     */
    int GetLayerCount() const { return m_layerCount; }

private:
    /**
     * Convert world coordinates to grid cell coordinates.
     */
    std::pair<int, int> WorldToCell( int aX, int aY ) const;

    /**
     * Get the grid cell index for a given cell coordinate.
     */
    int CellIndex( int aCellX, int aCellY, int aLayer ) const;

    /**
     * Get the range of cells that overlap a bounding box.
     */
    void GetCellRange( const BOX2I& aBounds, int& aMinCellX, int& aMinCellY,
                       int& aMaxCellX, int& aMaxCellY ) const;

    BOX2I   m_bounds;        ///< Bounds of the indexed area
    int     m_cellSize;      ///< Size of each grid cell
    int     m_gridWidth;     ///< Number of cells in X direction
    int     m_gridHeight;    ///< Number of cells in Y direction
    int     m_layerCount;    ///< Number of layers

    // Grid cells - each cell contains a set of entries
    std::vector<std::unordered_set<TREE_ENTRY, TreeEntryHash>> m_cells;

    // Reverse mapping from room/item to cells for efficient removal
    std::unordered_map<EXPANSION_ROOM*, std::vector<int>> m_roomToCells;
    std::unordered_map<std::pair<BOARD_ITEM*, int>, std::vector<int>,
        std::hash<std::pair<BOARD_ITEM*, int>>> m_itemToCells;
};


#endif // SHAPE_SEARCH_TREE_H
