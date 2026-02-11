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

#ifndef AUTOROUTE_ENGINE_H
#define AUTOROUTE_ENGINE_H

#include "autoroute_control.h"
#include "expansion/expansion_room.h"
#include "expansion/expansion_door.h"
#include "expansion/expansion_drill.h"
#include "search/shape_search_tree.h"
#include "search/congestion_map.h"
#include <math/vector2d.h>
#include <math/box2.h>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <atomic>
#include <chrono>

// Forward declarations
class BOARD;
class BOARD_ITEM;
class PAD;
class PCB_TRACK;
class PCB_VIA;
class COMMIT;


/**
 * Progress update for autorouting.
 */
struct AUTOROUTE_PROGRESS
{
    int    current_net = 0;        ///< Current net being routed (1-based)
    int    total_nets = 0;         ///< Total nets to route
    int    current_pass = 0;       ///< Current optimization pass (1-based)
    int    total_passes = 1;       ///< Total number of passes
    int    nets_routed = 0;        ///< Nets successfully routed so far
    int    nets_failed = 0;        ///< Nets failed to route so far
    std::string current_net_name;  ///< Name of current net
    std::string phase;             ///< Current phase ("routing", "optimizing", etc.)
    double elapsed_seconds = 0;    ///< Time elapsed since start
    double percent_complete = 0;   ///< Overall completion percentage (0-100)
};


/**
 * Progress callback type for autorouting.
 * Return false to cancel the operation.
 */
using AUTOROUTE_PROGRESS_CALLBACK = std::function<bool( const AUTOROUTE_PROGRESS& )>;


/**
 * Represents a net connection to be routed.
 */
struct NET_CONNECTION
{
    std::string net_name;
    int         net_code;
    std::set<BOARD_ITEM*> source_pads;
    std::set<BOARD_ITEM*> dest_pads;
};


/**
 * Core autorouting engine that manages expansion rooms and coordinates routing.
 *
 * This class builds the expansion room model from the board state, manages
 * the room database, and coordinates the maze search algorithm to route
 * connections between pads.
 */
class AUTOROUTE_ENGINE
{
public:
    AUTOROUTE_ENGINE();
    ~AUTOROUTE_ENGINE();

    /**
     * Initialize the engine with a board.
     */
    void Initialize( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl );

    /**
     * Set the commit to use for adding tracks/vias.
     * This must be set before routing for changes to persist properly.
     */
    void SetCommit( COMMIT* aCommit ) { m_commit = aCommit; }

    /**
     * Set the progress callback for routing updates.
     * The callback is called periodically during routing.
     * Return false from the callback to cancel the operation.
     */
    void SetProgressCallback( AUTOROUTE_PROGRESS_CALLBACK aCallback )
    {
        m_progressCallback = aCallback;
    }

    /**
     * Request cancellation of the current routing operation.
     * This is thread-safe and can be called from any thread.
     */
    void Cancel() { m_cancelled.store( true ); }

    /**
     * Check if routing has been cancelled.
     */
    bool IsCancelled() const { return m_cancelled.load(); }

    /**
     * Build the expansion room model from the current board state.
     * Must be called before routing.
     */
    void BuildRoomModel();

    /**
     * Clear all expansion rooms and doors.
     */
    void ClearRoomModel();

    /**
     * Get all connections that need to be routed.
     */
    std::vector<NET_CONNECTION> GetConnectionsToRoute() const;

    /**
     * Route a single net connection.
     *
     * @param aConnection The connection to route
     * @param aPass Optimization pass number (0 = first pass, higher = more aggressive)
     * @return Python code to insert the route (for IPC execution)
     */
    std::string RouteConnection( const NET_CONNECTION& aConnection, int aPass = 0 );

    /**
     * Route all unconnected nets.
     *
     * @return Python code to insert all routes
     */
    std::string RouteAll();

    /**
     * Get statistics from the last routing operation.
     */
    AUTOROUTE_RESULT GetResult() const { return m_result; }

    /**
     * Get the board bounds.
     */
    BOX2I GetBoardBounds() const;

    /**
     * Get the number of copper layers.
     */
    int GetLayerCount() const { return m_layerCount; }

    /**
     * Create expansion rooms for a pad.
     */
    std::vector<EXPANSION_ROOM*> CreatePadRooms( PAD* aPad );

    /**
     * Create expansion rooms for an obstacle.
     */
    EXPANSION_ROOM* CreateObstacleRoom( BOARD_ITEM* aItem, int aLayer );

    /**
     * Get all rooms on a specific layer.
     */
    std::vector<EXPANSION_ROOM*> GetRoomsOnLayer( int aLayer ) const;

    /**
     * Find or create doors between adjacent rooms.
     */
    void BuildDoors();

    /**
     * Create potential drill (via) locations.
     */
    void BuildDrills();

    /**
     * Reset all rooms for a new search.
     */
    void ResetSearchState();

    /**
     * Get all drills.
     */
    const std::vector<std::unique_ptr<EXPANSION_DRILL>>& GetDrills() const { return m_drills; }

    /**
     * Get drills that are within a specific room on a layer.
     */
    std::vector<EXPANSION_DRILL*> GetDrillsInRoom( EXPANSION_ROOM* aRoom, int aLayer ) const;

private:
    /**
     * Build obstacle rooms from existing board items.
     */
    void BuildObstacleRooms();

    /**
     * Build free space rooms in gaps between obstacles.
     * DEPRECATED: Use BuildInitialIncompleteRooms() for dynamic expansion.
     */
    void BuildFreeSpaceRooms();

    /**
     * Build initial incomplete rooms adjacent to obstacles.
     * These rooms will be completed on-demand during maze search.
     * This is the FreeRouting-style dynamic room expansion approach.
     */
    void BuildInitialIncompleteRooms();

    /**
     * Check if two rooms are adjacent and can have a door between them.
     */
    bool AreRoomsAdjacent( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2 );

    /**
     * Create a door between two adjacent rooms.
     */
    EXPANSION_DOOR* CreateDoor( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2 );

    /**
     * Connect obstacle rooms that have no doors to nearby free space rooms.
     * This handles cases where grid alignment prevents normal door creation.
     */
    void ConnectOrphanObstacles();

    /**
     * Find the nearest free space room to a given obstacle room on the same layer.
     */
    EXPANSION_ROOM* FindNearestFreeSpace( EXPANSION_ROOM* aObstacle );

    /**
     * Calculate routing priority for a net connection.
     * Lower values = higher priority (route first).
     * Based on: pad count, total wire length, complexity.
     */
    double CalculateNetPriority( const NET_CONNECTION& aConnection ) const;

    /**
     * Order connections for optimal routing sequence.
     * Simpler nets (fewer pads, shorter distances) are routed first.
     */
    void OrderConnections( std::vector<NET_CONNECTION>& aConnections ) const;

    BOARD*                                  m_board = nullptr;
    COMMIT*                                 m_commit = nullptr;
    AUTOROUTE_CONTROL                       m_control;
    AUTOROUTE_RESULT                        m_result;
    int                                     m_layerCount = 2;

    // Spatial search tree for efficient obstacle queries
    SHAPE_SEARCH_TREE                       m_searchTree;

    // Congestion tracking for spread routing
    std::unique_ptr<CONGESTION_MAP>         m_congestionMap;

    // Expansion room storage
    std::vector<std::unique_ptr<EXPANSION_ROOM>> m_rooms;
    std::vector<std::unique_ptr<EXPANSION_DOOR>> m_doors;
    std::vector<std::unique_ptr<EXPANSION_DRILL>> m_drills;

    // Incomplete rooms waiting to be completed
    std::vector<std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM>> m_incompleteRooms;

    // Layer-indexed room lookup
    std::map<int, std::vector<EXPANSION_ROOM*>> m_roomsByLayer;

public:
    /**
     * Get the spatial search tree.
     */
    SHAPE_SEARCH_TREE& GetSearchTree() { return m_searchTree; }
    const SHAPE_SEARCH_TREE& GetSearchTree() const { return m_searchTree; }

    /**
     * Get the congestion map (may be nullptr if not initialized).
     */
    CONGESTION_MAP* GetCongestionMap() { return m_congestionMap.get(); }
    const CONGESTION_MAP* GetCongestionMap() const { return m_congestionMap.get(); }

    /**
     * Add an incomplete expansion room.
     * Returns the raw pointer for door connections.
     */
    INCOMPLETE_FREE_SPACE_ROOM* AddIncompleteRoom( std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM> aRoom );

    /**
     * Complete an incomplete room and add it to the room database.
     * Returns the completed room.
     */
    FREE_SPACE_ROOM* CompleteRoom( INCOMPLETE_FREE_SPACE_ROOM* aRoom, int aNetCode );

    /**
     * Complete expansion rooms on demand during maze search.
     * This is the key to dynamic room expansion like FreeRouting.
     *
     * @param aIncompleteRoom The incomplete room to complete
     * @return Vector of completed rooms (may be multiple if room is split)
     */
    std::vector<FREE_SPACE_ROOM*> CompleteExpansionRoom( INCOMPLETE_FREE_SPACE_ROOM* aIncompleteRoom,
                                                          int aNetCode );

    /**
     * Create an initial incomplete room for a layer that extends to board bounds.
     * Used to start the dynamic room expansion process.
     */
    INCOMPLETE_FREE_SPACE_ROOM* CreateInitialIncompleteRoom( int aLayer,
                                                               const VECTOR2I& aContainedPoint );

    /**
     * Get the routing control parameters.
     */
    const AUTOROUTE_CONTROL& GetControl() const { return m_control; }

    /**
     * Generate a new unique room ID.
     */
    int GenerateRoomId() { return m_nextRoomId++; }

    /**
     * Report progress to the callback if set.
     * @return False if cancelled, true to continue
     */
    bool ReportProgress( const AUTOROUTE_PROGRESS& aProgress );

private:
    int m_nextRoomId = 1;

    // Progress and cancellation
    AUTOROUTE_PROGRESS_CALLBACK m_progressCallback;
    std::atomic<bool>           m_cancelled{ false };
    std::chrono::steady_clock::time_point m_startTime;
};


#endif // AUTOROUTE_ENGINE_H
