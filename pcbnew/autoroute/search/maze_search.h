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
#include "ripup_checker.h"
#include "../autoroute_control.h"
#include <atomic>
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
class CONGESTION_MAP;


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

    /**
     * Get items that need to be ripped up to complete the found route.
     */
    const std::set<BOARD_ITEM*>& GetRipupItems() const { return m_ripupChecker.GetMarkedItems(); }

    /**
     * Set the net code being routed (for ripup decisions).
     */
    void SetNetCode( int aNetCode ) { m_netCode = aNetCode; }

    /**
     * Enable delayed occupation strategy.
     * When enabled, failed searches will retry with previously blocked doors allowed.
     */
    void SetDelayedOccupation( bool aEnabled ) { m_delayedOccupationEnabled = aEnabled; }

    /**
     * Set maximum retry attempts for delayed occupation.
     */
    void SetMaxRetries( int aRetries ) { m_maxRetries = aRetries; }

    /**
     * Set the congestion map for congestion-aware routing.
     */
    void SetCongestionMap( const CONGESTION_MAP* aCongestionMap ) { m_congestionMap = aCongestionMap; }

    /**
     * Set a pointer to a cancellation flag.
     * The search will check this flag periodically and abort if set.
     */
    void SetCancelledFlag( const std::atomic<bool>* aCancelled ) { m_cancelled = aCancelled; }

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
     * Expand to drills within a room for potential layer transitions.
     */
    void ExpandToDrillsInRoom( EXPANSION_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement );

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
     * Check if we should consider ripping up an obstacle room.
     * This is called when the normal route is blocked.
     *
     * @param aRoom The obstacle room blocking the route.
     * @param aFromElement The element we're expanding from.
     * @return True if ripup was beneficial and expansion should continue.
     */
    bool CheckAndHandleRipup( OBSTACLE_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement );

    /**
     * Expand through an obstacle room (for ripup scenarios).
     * This adds the obstacle's doors to the queue with additional ripup cost.
     */
    void ExpandThroughObstacle( OBSTACLE_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement );

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

    // Ripup support
    RIPUP_CHECKER     m_ripupChecker;
    int               m_netCode = 0;  ///< Net code being routed

    // Statistics and limits
    int m_nodesExpanded = 0;
    int m_maxQueueSize = 0;
    static constexpr int MAX_NODES_EXPANDED = 50000;   ///< Safety limit to prevent infinite search
    static constexpr int MAX_QUEUE_SIZE = 100000;      ///< Max queue size to prevent memory issues
    static constexpr int MAX_ROOM_COMPLETIONS = 1000;  ///< Limit on dynamic room completions per search
    int m_roomCompletions = 0;                         ///< Track room completions

    // Delayed occupation strategy
    bool m_delayedOccupationEnabled = true;  ///< Enable retry with delayed occupation
    int  m_maxRetries = 3;                    ///< Maximum retry attempts
    int  m_currentRetry = 0;                  ///< Current retry count
    std::set<std::pair<EXPANDABLE_OBJECT*, int>> m_delayedDoors;  ///< Doors marked for delayed retry
    // Note: No m_inQueue - FreeRouting allows duplicates in queue, skips when popped if occupied

    // Congestion-aware routing
    const CONGESTION_MAP* m_congestionMap = nullptr;  ///< Optional congestion map for spread routing

    // Cancellation support
    const std::atomic<bool>* m_cancelled = nullptr;   ///< External cancellation flag
};


#endif // MAZE_SEARCH_H
