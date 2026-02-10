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

#ifndef ROOM_COMPLETION_H
#define ROOM_COMPLETION_H

#include "../expansion/expansion_room.h"
#include "../expansion/expansion_door.h"
#include "shape_search_tree.h"
#include <vector>
#include <memory>
#include <set>

// Forward declarations
class AUTOROUTE_ENGINE;


/**
 * Represents a neighbor of an expansion room during room completion.
 *
 * Neighbors are sorted counterclockwise around the room's border.
 */
struct ROOM_NEIGHBOUR
{
    const INT_BOX*   neighbour_shape;
    INT_BOX          intersection;
    int              touching_side_no_of_room;       ///< Side index of our room where touch occurs
    int              touching_side_no_of_neighbour;  ///< Side index of neighbor where touch occurs
    bool             room_touch_is_corner;           ///< True if touch is at a corner of our room
    bool             neighbour_touch_is_corner;      ///< True if touch is at a corner of neighbor
    EXPANSION_ROOM*  neighbour_room;                 ///< The neighbor room (may be null for raw items)
    BOARD_ITEM*      neighbour_item;                 ///< The neighbor item (for obstacles)

    /**
     * Compare for sorting counterclockwise around room border.
     */
    bool operator<( const ROOM_NEIGHBOUR& other ) const;

    /**
     * Get the first corner of the intersection.
     */
    VECTOR2I FirstCorner( const INT_BOX& roomShape ) const;

    /**
     * Get the last corner of the intersection.
     */
    VECTOR2I LastCorner( const INT_BOX& roomShape ) const;
};


/**
 * Result of room completion.
 */
struct COMPLETION_RESULT
{
    std::unique_ptr<FREE_SPACE_ROOM>                   completed_room;
    std::vector<std::unique_ptr<EXPANSION_DOOR>>       new_doors;
    std::vector<std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM>> new_incomplete_rooms;
};


/**
 * Algorithm to complete an incomplete expansion room.
 *
 * This is based on FreeRouting's SortedRoomNeighbours.calculate() method.
 * It takes an incomplete room and:
 * 1. Queries the search tree for overlapping obstacles
 * 2. Sorts neighbors counterclockwise around room boundary
 * 3. Creates doors to existing obstacle rooms
 * 4. Creates new incomplete rooms in gaps between neighbors
 * 5. Returns a complete room with all doors attached
 */
class ROOM_COMPLETION
{
public:
    ROOM_COMPLETION( AUTOROUTE_ENGINE& aEngine, SHAPE_SEARCH_TREE& aSearchTree );

    /**
     * Complete an incomplete room.
     *
     * @param aRoom The incomplete room to complete.
     * @param aNetCode The net code being routed (to exclude from obstacles).
     * @return Completion result with the complete room, doors, and new incomplete rooms.
     */
    COMPLETION_RESULT Complete( INCOMPLETE_FREE_SPACE_ROOM& aRoom, int aNetCode );

    /**
     * Complete an obstacle room (creating doors to adjacent free space).
     *
     * @param aRoom The obstacle room to process.
     * @param aNetCode The net code being routed.
     * @return Completion result with doors to adjacent incomplete rooms.
     */
    COMPLETION_RESULT CompleteObstacle( OBSTACLE_ROOM& aRoom, int aNetCode );

private:
    /**
     * Find all neighbors of a room shape.
     */
    std::vector<ROOM_NEIGHBOUR> FindNeighbours( const INT_BOX& aShape, int aLayer, int aNetCode );

    /**
     * Sort neighbors counterclockwise around the room border.
     */
    void SortNeighbours( std::vector<ROOM_NEIGHBOUR>& aNeighbours, const INT_BOX& aRoomShape );

    /**
     * Calculate doors and new incomplete rooms from sorted neighbors.
     */
    void CalculateDoorsAndRooms( const INT_BOX& aRoomShape,
                                  int aLayer,
                                  const std::vector<ROOM_NEIGHBOUR>& aNeighbours,
                                  COMPLETION_RESULT& aResult );

    /**
     * Try to remove an edge with no touching neighbors and enlarge the room.
     *
     * @return True if the shape was changed and needs re-completion.
     */
    bool TryRemoveEdge( INT_BOX& aShape, const std::vector<ROOM_NEIGHBOUR>& aNeighbours );

    /**
     * Check if a door between two rooms is valid.
     */
    bool IsDoorValid( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2,
                      const INT_BOX& aDoorShape );

    /**
     * Create a door between two rooms.
     */
    std::unique_ptr<EXPANSION_DOOR> CreateDoor( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2,
                                                 const SEG& aDoorSegment );

    /**
     * Create an incomplete room for a gap in the room border.
     */
    std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM> CreateIncompleteRoom(
        const INT_BOX& aRoomShape,
        int aSide,
        const VECTOR2I& aStart,
        const VECTOR2I& aEnd,
        int aLayer );

    AUTOROUTE_ENGINE&  m_engine;
    SHAPE_SEARCH_TREE& m_searchTree;
};


#endif // ROOM_COMPLETION_H
