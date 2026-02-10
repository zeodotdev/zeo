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

#ifndef MAZE_SEARCH_H
#define MAZE_SEARCH_H

#include "maze_list_element.h"
#include "destination_distance.h"
#include "../autoroute_control.h"
#include <queue>
#include <vector>
#include <set>
#include <map>
#include <optional>

// Forward declarations
class AUTOROUTE_ENGINE;
class EXPANSION_ROOM;
class EXPANSION_DOOR;
class EXPANSION_DRILL;
class BOARD_ITEM;


/**
 * Result of a successful maze search.
 */
struct MAZE_SEARCH_RESULT
{
    /// The door where we reached the destination
    EXPANDABLE_OBJECT* destination_door = nullptr;

    /// Section number of the destination door
    int section_no = 0;

    /// The destination room we reached
    EXPANSION_ROOM* destination_room = nullptr;

    /// Total path cost
    double total_cost = 0.0;
};


/**
 * A* maze search algorithm for PCB autorouting.
 *
 * This implements an A* search through expansion rooms and doors to find
 * optimal paths between source and destination pads. Based on FreeRouting's
 * MazeSearchAlgo.
 */
class MAZE_SEARCH
{
public:
    MAZE_SEARCH( AUTOROUTE_ENGINE& aEngine, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the source items (pads to route from).
     */
    void SetSources( const std::set<BOARD_ITEM*>& aSourceItems );

    /**
     * Set the destination items (pads to route to).
     */
    void SetDestinations( const std::set<BOARD_ITEM*>& aDestItems );

    /**
     * Find a connection from sources to destinations.
     *
     * @return Search result if a path was found, nullopt otherwise
     */
    std::optional<MAZE_SEARCH_RESULT> FindConnection();

    /**
     * Get the backtrack path from a successful search.
     * Call this after FindConnection() returns a valid result.
     *
     * @return Vector of (door, section) pairs from destination back to source
     */
    std::vector<std::pair<EXPANDABLE_OBJECT*, int>> GetBacktrackPath() const;

    /**
     * Clear all search state for a new search.
     */
    void Reset();

    /**
     * Get statistics from the last search.
     */
    int GetNodesExpanded() const { return m_nodesExpanded; }
    int GetMaxQueueSize() const { return m_maxQueueSize; }

private:
    /**
     * Initialize the search by creating expansion rooms for source items.
     */
    bool InitializeSearch();

    /**
     * Process the next element from the priority queue.
     *
     * @return true if destination was reached
     */
    bool OccupyNextElement();

    /**
     * Expand from a door to all doors in the next room.
     */
    void ExpandToRoomDoors( const MAZE_LIST_ELEMENT& aElement );

    /**
     * Try to expand through a specific door section.
     */
    void ExpandToDoorSection( EXPANSION_DOOR* aDoor, int aSection,
                              const MAZE_LIST_ELEMENT& aFromElement );

    /**
     * Expand to other layers through a drill (via).
     */
    void ExpandToOtherLayers( const MAZE_LIST_ELEMENT& aElement );

    /**
     * Calculate the cost of routing from one point to another on the same layer.
     */
    double CalculateTraceCost( const VECTOR2I& aFrom, const VECTOR2I& aTo,
                                int aLayer ) const;

    /**
     * Add an element to the priority queue.
     */
    void AddToQueue( const MAZE_LIST_ELEMENT& aElement );

    /**
     * Check if we can route through a room (not blocked by same-net obstacle).
     */
    bool CanEnterRoom( const EXPANSION_ROOM* aRoom, int aNetCode ) const;

    /**
     * Store backtrack information for path reconstruction.
     */
    void StoreBacktrack( EXPANDABLE_OBJECT* aDoor, int aSection,
                         EXPANDABLE_OBJECT* aBacktrackDoor, int aBacktrackSection,
                         const VECTOR2I& aEntryPoint, int aLayer );

private:
    AUTOROUTE_ENGINE&     m_engine;
    const AUTOROUTE_CONTROL& m_control;

    // Priority queue ordered by sorting_value (min-heap)
    std::priority_queue<MAZE_LIST_ELEMENT,
                        std::vector<MAZE_LIST_ELEMENT>,
                        std::greater<MAZE_LIST_ELEMENT>> m_queue;

    // Source and destination tracking
    std::set<BOARD_ITEM*>       m_sourceItems;
    std::set<EXPANSION_ROOM*>   m_sourceRooms;
    DESTINATION_DISTANCE        m_destDistance;

    // Backtrack information for path reconstruction
    struct BacktrackInfo
    {
        EXPANDABLE_OBJECT* backtrack_door;
        int                backtrack_section;
        VECTOR2I           entry_point;
        int                layer;
    };
    std::map<std::pair<EXPANDABLE_OBJECT*, int>, BacktrackInfo> m_backtrack;

    // Result from successful search
    MAZE_LIST_ELEMENT m_resultElement;
    bool              m_foundPath = false;

    // Statistics
    int m_nodesExpanded = 0;
    int m_maxQueueSize = 0;
};


#endif // MAZE_SEARCH_H
