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

#include "maze_search.h"
#include "congestion_map.h"
#include "../autoroute_engine.h"
#include "../expansion/expansion_room.h"
#include "../expansion/expansion_door.h"
#include "../expansion/expansion_drill.h"
#include <pad.h>
#include <cmath>
#include <iostream>

// Debug macro for maze search - uses std::cerr for unbuffered output
#define MAZE_DEBUG( msg ) std::cerr << "[MAZE] " << msg << std::endl


MAZE_SEARCH::MAZE_SEARCH( AUTOROUTE_ENGINE& aEngine, const AUTOROUTE_CONTROL& aControl ) :
    m_engine( aEngine ),
    m_control( aControl ),
    m_ripupChecker( aControl )
{
}


void MAZE_SEARCH::SetSources( const std::set<BOARD_ITEM*>& aSourceItems )
{
    m_sourceItems = aSourceItems;
}


void MAZE_SEARCH::SetDestinations( const std::set<BOARD_ITEM*>& aDestItems )
{
    m_destDistance.Initialize( aDestItems );

    // Convert destination items (pads) to their expansion rooms
    for( BOARD_ITEM* item : aDestItems )
    {
        PAD* pad = dynamic_cast<PAD*>( item );
        if( pad )
        {
            std::vector<EXPANSION_ROOM*> padRooms = m_engine.CreatePadRooms( pad );
            for( EXPANSION_ROOM* room : padRooms )
            {
                m_destDistance.AddTargetRoom( room );
            }
        }
    }
}


void MAZE_SEARCH::Reset()
{
    // Clear the priority queue
    while( !m_queue.empty() )
        m_queue.pop();

    m_sourceRooms.clear();
    m_backtrack.clear();
    m_foundPath = false;
    m_nodesExpanded = 0;
    m_maxQueueSize = 0;
    m_roomCompletions = 0;
    m_resultElement = MAZE_LIST_ELEMENT();
    m_delayedDoors.clear();
    m_currentRetry = 0;
}


bool MAZE_SEARCH::InitializeSearch()
{
    MAZE_DEBUG( "InitializeSearch: START, sourceItems=" << m_sourceItems.size() );

    // Convert source items (pads) to their expansion rooms
    m_sourceRooms.clear();

    for( BOARD_ITEM* item : m_sourceItems )
    {
        PAD* pad = dynamic_cast<PAD*>( item );
        if( pad )
        {
            // Find expansion rooms for this pad
            std::vector<EXPANSION_ROOM*> padRooms = m_engine.CreatePadRooms( pad );
            MAZE_DEBUG( "InitializeSearch: pad has " << padRooms.size() << " rooms" );
            for( EXPANSION_ROOM* room : padRooms )
            {
                m_sourceRooms.insert( room );
            }
        }
    }

    MAZE_DEBUG( "InitializeSearch: sourceRooms=" << m_sourceRooms.size() );

    if( m_sourceRooms.empty() )
    {
        MAZE_DEBUG( "InitializeSearch: No source rooms, returning false" );
        return false;
    }

    // For each source room, add all doors to the priority queue with cost 0
    int totalDoors = 0;
    for( EXPANSION_ROOM* sourceRoom : m_sourceRooms )
    {
        MAZE_DEBUG( "InitializeSearch: sourceRoom has " << sourceRoom->GetDoors().size() << " doors" );

        // Add target door for the source room itself (to start expansion)
        for( EXPANSION_DOOR* door : sourceRoom->GetDoors() )
        {
            // Get the room on the other side of the door
            EXPANSION_ROOM* nextRoom = door->GetOtherRoom( sourceRoom );
            if( !nextRoom )
                continue;  // Skip doors that don't lead anywhere

            int sectionCount = door->GetSectionCount();
            for( int section = 0; section < sectionCount; ++section )
            {
                MAZE_LIST_ELEMENT elem;
                elem.door = door;
                elem.section_no = section;
                elem.backtrack_door = nullptr;
                elem.backtrack_section = 0;
                elem.expansion_value = 0.0;
                elem.layer = sourceRoom->GetLayer();
                elem.entry_point = door->GetSectionCenter( section );

                // Calculate heuristic
                double heuristic = m_destDistance.Calculate( elem.entry_point,
                                                              elem.layer,
                                                              m_control.via_cost );
                elem.sorting_value = elem.expansion_value + heuristic;
                elem.next_room = nextRoom;

                AddToQueue( elem );
                totalDoors++;
            }
        }
    }

    MAZE_DEBUG( "InitializeSearch: queued " << totalDoors << " doors, queueSize=" << m_queue.size() );
    return !m_queue.empty();
}


std::optional<MAZE_SEARCH_RESULT> MAZE_SEARCH::FindConnection()
{
    MAZE_DEBUG( "FindConnection: START" );
    Reset();
    m_currentRetry = 0;

    // Main search loop with delayed occupation retry support
    while( m_currentRetry <= m_maxRetries )
    {
        MAZE_DEBUG( "FindConnection: retry=" << m_currentRetry );

        if( !InitializeSearch() )
        {
            MAZE_DEBUG( "FindConnection: InitializeSearch failed" );
            return std::nullopt;
        }

        MAZE_DEBUG( "FindConnection: Starting A* loop, queueSize=" << m_queue.size() );

        // Main A* loop - FreeRouting approach
        // Loop until: queue empty, destination found, or cancelled
        // OccupyNextElement handles skipping already-occupied duplicates
        while( OccupyNextElement() == false && !m_queue.empty() )
        {
            // Check for external cancellation
            if( m_cancelled && m_cancelled->load() )
            {
                MAZE_DEBUG( "FindConnection: Cancelled externally, stopping" );
                break;
            }

            // Periodic progress logging (every 1000 nodes)
            if( m_nodesExpanded % 1000 == 0 )
            {
                MAZE_DEBUG( "FindConnection: progress nodes=" << m_nodesExpanded
                            << " queue=" << m_queue.size() );
            }
        }

        // Check if we found the destination
        if( m_foundPath )
        {
            MAZE_DEBUG( "FindConnection: FOUND PATH, nodes=" << m_nodesExpanded );
            // Found path to destination
            MAZE_SEARCH_RESULT result;
            result.destination_door = m_resultElement.door;
            result.section_no = m_resultElement.section_no;
            result.destination_room = m_resultElement.next_room;
            result.total_cost = m_resultElement.expansion_value;
            return result;
        }

        MAZE_DEBUG( "FindConnection: A* loop ended, nodes=" << m_nodesExpanded
                    << " queueEmpty=" << m_queue.empty() );

        // Path not found - try with delayed occupation if enabled
        if( !m_delayedOccupationEnabled || m_delayedDoors.empty() )
        {
            MAZE_DEBUG( "FindConnection: No retry, delayedEnabled=" << m_delayedOccupationEnabled
                        << " delayedDoors=" << m_delayedDoors.size() );
            break;
        }

        // Prepare for retry: clear delayed doors and allow them with higher cost
        m_currentRetry++;

        // Clear queue but keep delayed doors in search state
        while( !m_queue.empty() )
            m_queue.pop();

        m_backtrack.clear();
        m_foundPath = false;

        // Re-add delayed doors as available for retry
        for( const auto& [door, section] : m_delayedDoors )
        {
            if( door )
                door->SetOccupied( section, false );
        }
        m_delayedDoors.clear();
    }

    MAZE_DEBUG( "FindConnection: NO PATH FOUND, nodes=" << m_nodesExpanded );
    return std::nullopt;  // No path found even with retries
}


bool MAZE_SEARCH::OccupyNextElement()
{
    MAZE_DEBUG( "OccupyNextElement: START, queueSize=" << m_queue.size()
                << " nodes=" << m_nodesExpanded );

    if( m_queue.empty() )
    {
        MAZE_DEBUG( "OccupyNextElement: queue empty, returning false" );
        return false;
    }

    // Check for external cancellation (like FreeRouting's is_stop_requested())
    if( m_cancelled && m_cancelled->load() )
    {
        MAZE_DEBUG( "OccupyNextElement: cancelled, returning false" );
        return false;
    }

    // FreeRouting approach: Loop to find next non-occupied element
    // (duplicates in queue are skipped when already occupied)
    MAZE_LIST_ELEMENT element;
    bool found = false;

    while( !m_queue.empty() )
    {
        // Check cancellation in inner loop like FreeRouting
        if( m_cancelled && m_cancelled->load() )
        {
            MAZE_DEBUG( "OccupyNextElement: cancelled in inner loop" );
            return false;
        }

        element = m_queue.top();
        m_queue.pop();

        // Skip if already occupied (duplicate in queue)
        if( !element.door->IsOccupied( element.section_no ) )
        {
            found = true;
            break;
        }
        // Element already occupied, continue to next
    }

    if( !found )
    {
        MAZE_DEBUG( "OccupyNextElement: no unoccupied element found" );
        return false;
    }

    m_nodesExpanded++;

    MAZE_DEBUG( "OccupyNextElement: door=" << (void*)element.door
                << " section=" << element.section_no
                << " layer=" << element.layer
                << " nextRoom=" << (void*)element.next_room );

    // Store backtrack information (FreeRouting does this before expansion)
    MAZE_DEBUG( "OccupyNextElement: storing backtrack" );
    StoreBacktrack( element.door, element.section_no,
                    element.backtrack_door, element.backtrack_section,
                    element.entry_point, element.layer );

    // Check if we've reached a destination
    MAZE_DEBUG( "OccupyNextElement: checking if destination" );
    if( element.next_room && m_destDistance.IsDestination( element.next_room ) )
    {
        MAZE_DEBUG( "OccupyNextElement: REACHED DESTINATION!" );
        m_resultElement = element;
        m_foundPath = true;
        // Mark as occupied before returning
        element.door->SetOccupied( element.section_no, true );
        return true;
    }

    // Expand to doors in the next room
    if( element.next_room )
    {
        MAZE_DEBUG( "OccupyNextElement: calling ExpandToRoomDoors" );
        ExpandToRoomDoors( element );
        MAZE_DEBUG( "OccupyNextElement: ExpandToRoomDoors done" );
    }

    // If this is a drill, expand to other layers
    MAZE_DEBUG( "OccupyNextElement: checking for drill" );
    EXPANSION_DRILL* drill = dynamic_cast<EXPANSION_DRILL*>( element.door );
    if( drill && m_control.vias_allowed )
    {
        MAZE_DEBUG( "OccupyNextElement: calling ExpandToOtherLayers" );
        ExpandToOtherLayers( element );
        MAZE_DEBUG( "OccupyNextElement: ExpandToOtherLayers done" );
    }

    // FreeRouting: Mark as occupied AFTER expansion (line 290)
    // This allows failed expansions to leave the section available for other paths
    element.door->SetOccupied( element.section_no, true );

    MAZE_DEBUG( "OccupyNextElement: done, returning false" );
    return false;
}


void MAZE_SEARCH::ExpandToRoomDoors( const MAZE_LIST_ELEMENT& aElement )
{
    MAZE_DEBUG( "ExpandToRoomDoors: START" );

    EXPANSION_ROOM* room = aElement.next_room;
    if( !room )
    {
        MAZE_DEBUG( "ExpandToRoomDoors: room is null, returning" );
        return;
    }

    MAZE_DEBUG( "ExpandToRoomDoors: room=" << (void*)room
                << " type=" << static_cast<int>( room->GetType() ) );

    // Check room type
    if( room->GetType() == ROOM_TYPE::OBSTACLE )
    {
        MAZE_DEBUG( "ExpandToRoomDoors: room is OBSTACLE" );
        OBSTACLE_ROOM* obstRoom = dynamic_cast<OBSTACLE_ROOM*>( room );

        // Check if this is our own net (can pass through)
        if( obstRoom && obstRoom->GetNetCode() == m_netCode )
        {
            MAZE_DEBUG( "ExpandToRoomDoors: our own net, can pass through" );
            // Can pass through our own net's obstacles
        }
        else if( obstRoom && m_control.allow_ripup )
        {
            MAZE_DEBUG( "ExpandToRoomDoors: trying ripup" );
            // Try ripup
            CheckAndHandleRipup( obstRoom, aElement );
            MAZE_DEBUG( "ExpandToRoomDoors: ripup done, returning" );
            return;  // ExpandThroughObstacle handles the expansion
        }
        else
        {
            MAZE_DEBUG( "ExpandToRoomDoors: can't enter obstacle, returning" );
            // Can't enter this obstacle room
            return;
        }
    }

    // Handle incomplete rooms - complete them on-demand (FreeRouting-style dynamic expansion)
    if( room->GetType() == ROOM_TYPE::FREE_SPACE_INCOMPLETE )
    {
        MAZE_DEBUG( "ExpandToRoomDoors: room is INCOMPLETE, completions=" << m_roomCompletions );

        // Check room completion limit to prevent runaway expansion
        if( m_roomCompletions >= MAX_ROOM_COMPLETIONS )
        {
            MAZE_DEBUG( "ExpandToRoomDoors: MAX_ROOM_COMPLETIONS limit, returning" );
            return;  // Too many room completions, stop expanding
        }

        INCOMPLETE_FREE_SPACE_ROOM* incompleteRoom =
            dynamic_cast<INCOMPLETE_FREE_SPACE_ROOM*>( room );

        if( incompleteRoom )
        {
            MAZE_DEBUG( "ExpandToRoomDoors: calling CompleteExpansionRoom" );
            // Complete the room dynamically
            std::vector<FREE_SPACE_ROOM*> completedRooms =
                m_engine.CompleteExpansionRoom( incompleteRoom, m_netCode );
            MAZE_DEBUG( "ExpandToRoomDoors: CompleteExpansionRoom returned "
                        << completedRooms.size() << " rooms" );

            m_roomCompletions++;  // Track completions

            if( completedRooms.empty() )
            {
                MAZE_DEBUG( "ExpandToRoomDoors: no completed rooms, returning" );
                // Room completion failed - can't expand through here
                return;
            }

            // Use the first completed room (typically there's only one)
            // Note: The completed room will have new doors created by CompleteExpansionRoom()
            room = completedRooms[0];
            MAZE_DEBUG( "ExpandToRoomDoors: using completed room=" << (void*)room );
        }
    }

    MAZE_DEBUG( "ExpandToRoomDoors: setting visited, getting doors" );
    room->SetVisited( true );

    const auto& doors = room->GetDoors();
    MAZE_DEBUG( "ExpandToRoomDoors: room has " << doors.size() << " doors" );

    // Expand to all doors of this room
    int doorIdx = 0;
    for( EXPANSION_DOOR* door : doors )
    {
        // Don't go back through the door we came from
        if( door == aElement.door )
        {
            doorIdx++;
            continue;
        }

        // Try each section of the door
        int sectionCount = door->GetSectionCount();
        MAZE_DEBUG( "ExpandToRoomDoors: door[" << doorIdx << "] has " << sectionCount << " sections" );

        for( int section = 0; section < sectionCount; ++section )
        {
            ExpandToDoorSection( door, section, aElement );
        }
        doorIdx++;
    }

    // NOTE: Drill expansion within rooms is disabled for now.
    // FreeRouting uses DrillPage abstraction to avoid queue explosion.
    // Without DrillPages, expanding to all drills in each room causes
    // the queue to overflow (hundreds of drills per room).
    // Layer transitions via drills still work through ExpandToOtherLayers
    // when we reach a drill that was added as a door's next_room target.
    //
    // TODO: Implement DrillPage like FreeRouting to enable efficient
    // drill expansion within rooms.

    MAZE_DEBUG( "ExpandToRoomDoors: done" );
}


void MAZE_SEARCH::ExpandToDoorSection( EXPANSION_DOOR* aDoor, int aSection,
                                        const MAZE_LIST_ELEMENT& aFromElement )
{
    // Check if already occupied
    if( aDoor->IsOccupied( aSection ) )
    {
        // On retry, mark this as a delayed door for potential re-expansion
        if( m_delayedOccupationEnabled && m_currentRetry > 0 )
        {
            m_delayedDoors.insert( { aDoor, aSection } );
        }
        return;
    }

    // Get the next room - don't add if null (nowhere to expand to)
    EXPANSION_ROOM* nextRoom = aDoor->GetOtherRoom( aFromElement.next_room );
    if( !nextRoom )
    {
        // This door leads nowhere - skip it
        return;
    }

    // Calculate cost to reach this door section
    VECTOR2I fromPt = aFromElement.entry_point;
    VECTOR2I toPt = aDoor->GetSectionCenter( aSection );
    int layer = aFromElement.layer;

    double traceCost = CalculateTraceCost( fromPt, toPt, layer );
    double newExpansion = aFromElement.expansion_value + traceCost;

    // Apply cost penalty for retry attempts (delayed occupation penalty)
    // Each retry adds increasing cost to discourage using blocked paths
    if( m_currentRetry > 0 )
    {
        double retryPenalty = m_currentRetry * m_control.trace_cost * 1000.0;
        newExpansion += retryPenalty;
    }

    // Calculate heuristic
    double heuristic = m_destDistance.Calculate( toPt, layer, m_control.via_cost );
    double sortingValue = newExpansion + heuristic;

    // Create new element
    MAZE_LIST_ELEMENT newElem;
    newElem.door = aDoor;
    newElem.section_no = aSection;
    newElem.backtrack_door = aFromElement.door;
    newElem.backtrack_section = aFromElement.section_no;
    newElem.expansion_value = newExpansion;
    newElem.sorting_value = sortingValue;
    newElem.layer = layer;
    newElem.entry_point = toPt;
    newElem.shape_entry = aDoor->GetSectionSegment( aSection );
    newElem.next_room = nextRoom;

    AddToQueue( newElem );
}


void MAZE_SEARCH::ExpandToOtherLayers( const MAZE_LIST_ELEMENT& aElement )
{
    EXPANSION_DRILL* drill = dynamic_cast<EXPANSION_DRILL*>( aElement.door );
    if( !drill )
        return;

    int fromLayer = aElement.layer;
    int firstLayer = drill->GetFirstLayer();
    int lastLayer = drill->GetLastLayer();

    // Try each reachable layer
    for( int toLayer = firstLayer; toLayer <= lastLayer; ++toLayer )
    {
        if( toLayer == fromLayer )
            continue;

        int layerSection = toLayer - firstLayer;
        if( drill->IsOccupied( layerSection ) )
            continue;

        // Calculate via cost
        double viaCost = m_control.GetViaCost( fromLayer, toLayer );
        double newExpansion = aElement.expansion_value + viaCost;

        // Calculate heuristic for new layer
        VECTOR2I pos = drill->GetLocation();
        double heuristic = m_destDistance.Calculate( pos, toLayer, m_control.via_cost );
        double sortingValue = newExpansion + heuristic;

        // With dynamic room expansion, we create an initial incomplete room
        // at the drill location on the target layer
        EXPANSION_ROOM* targetRoom = drill->GetRoomForLayer( toLayer );

        if( !targetRoom )
        {
            // Create an initial incomplete room at the drill location on the target layer
            INCOMPLETE_FREE_SPACE_ROOM* incompleteRoom =
                m_engine.CreateInitialIncompleteRoom( toLayer, pos );
            targetRoom = incompleteRoom;
        }

        // Skip if we still don't have a valid target room
        if( !targetRoom )
            continue;

        // Create element for the layer transition
        MAZE_LIST_ELEMENT newElem;
        newElem.door = drill;
        newElem.section_no = layerSection;
        newElem.backtrack_door = aElement.door;
        newElem.backtrack_section = aElement.section_no;
        newElem.expansion_value = newExpansion;
        newElem.sorting_value = sortingValue;
        newElem.layer = toLayer;
        newElem.entry_point = pos;
        newElem.via_placed = true;
        newElem.next_room = targetRoom;

        AddToQueue( newElem );
    }
}


void MAZE_SEARCH::ExpandToDrillsInRoom( EXPANSION_ROOM* aRoom,
                                         const MAZE_LIST_ELEMENT& aFromElement )
{
    if( !aRoom )
        return;

    int layer = aFromElement.layer;

    // Get drills that are within this room
    std::vector<EXPANSION_DRILL*> drills = m_engine.GetDrillsInRoom( aRoom, layer );

    for( EXPANSION_DRILL* drill : drills )
    {
        // Don't go back through the drill we came from
        if( drill == aFromElement.door )
            continue;

        // Calculate cost to reach the drill location
        VECTOR2I fromPt = aFromElement.entry_point;
        VECTOR2I drillPt = drill->GetLocation();

        double traceCost = CalculateTraceCost( fromPt, drillPt, layer );
        double newExpansion = aFromElement.expansion_value + traceCost;

        // Add to queue for the current layer section of the drill
        int layerSection = layer - drill->GetFirstLayer();
        if( layerSection >= 0 && layerSection < drill->GetSectionCount()
            && !drill->IsOccupied( layerSection ) )
        {
            double heuristic = m_destDistance.Calculate( drillPt, layer, m_control.via_cost );

            MAZE_LIST_ELEMENT newElem;
            newElem.door = drill;
            newElem.section_no = layerSection;
            newElem.backtrack_door = aFromElement.door;
            newElem.backtrack_section = aFromElement.section_no;
            newElem.expansion_value = newExpansion;
            newElem.sorting_value = newExpansion + heuristic;
            newElem.layer = layer;
            newElem.entry_point = drillPt;
            newElem.next_room = aRoom;  // Stay in same room, but can now transition layers

            AddToQueue( newElem );
        }
    }
}


double MAZE_SEARCH::CalculateTraceCost( const VECTOR2I& aFrom, const VECTOR2I& aTo,
                                         int aLayer ) const
{
    // Manhattan distance cost
    int64_t dx = std::abs( aTo.x - aFrom.x );
    int64_t dy = std::abs( aTo.y - aFrom.y );

    double distance = static_cast<double>( dx + dy );
    double baseCost = distance * m_control.trace_cost;

    // Add congestion cost if congestion map is available
    if( m_congestionMap )
    {
        baseCost = m_congestionMap->GetCongestionCost( aFrom, aTo, aLayer, baseCost );
    }

    return baseCost;
}


void MAZE_SEARCH::AddToQueue( const MAZE_LIST_ELEMENT& aElement )
{
    // FreeRouting approach: Allow duplicates in queue.
    // The same (door, section) can be added via different paths.
    // When popped, if already occupied, it's simply skipped.
    // This allows the A* algorithm to find the optimal path.

    // Only enforce a safety limit to prevent memory exhaustion
    if( static_cast<int>( m_queue.size() ) >= MAX_QUEUE_SIZE )
    {
        MAZE_DEBUG( "AddToQueue: MAX_QUEUE_SIZE reached, dropping element" );
        return;
    }

    // Don't add if already occupied (optimization - skip elements that will be skipped anyway)
    if( aElement.door && aElement.door->IsOccupied( aElement.section_no ) )
        return;

    m_queue.push( aElement );

    // Track max queue size for statistics
    if( static_cast<int>( m_queue.size() ) > m_maxQueueSize )
        m_maxQueueSize = m_queue.size();
}


bool MAZE_SEARCH::CanEnterRoom( const EXPANSION_ROOM* aRoom, int aNetCode ) const
{
    if( !aRoom )
        return false;

    // Free space rooms can always be entered
    if( aRoom->GetType() == ROOM_TYPE::FREE_SPACE )
        return true;

    // Target rooms of the same net can be entered
    if( aRoom->GetType() == ROOM_TYPE::TARGET && aRoom->GetNetCode() == aNetCode )
        return true;

    // Obstacle rooms cannot be entered
    return false;
}


void MAZE_SEARCH::StoreBacktrack( EXPANDABLE_OBJECT* aDoor, int aSection,
                                   EXPANDABLE_OBJECT* aBacktrackDoor, int aBacktrackSection,
                                   const VECTOR2I& aEntryPoint, int aLayer )
{
    auto key = std::make_pair( aDoor, aSection );

    BacktrackInfo info;
    info.backtrack_door = aBacktrackDoor;
    info.backtrack_section = aBacktrackSection;
    info.entry_point = aEntryPoint;
    info.layer = aLayer;

    m_backtrack[key] = info;
}


std::vector<std::pair<EXPANDABLE_OBJECT*, int>> MAZE_SEARCH::GetBacktrackPath() const
{
    std::vector<std::pair<EXPANDABLE_OBJECT*, int>> path;

    if( !m_foundPath )
        return path;

    // Start from destination and work backwards
    EXPANDABLE_OBJECT* currentDoor = m_resultElement.door;
    int currentSection = m_resultElement.section_no;

    while( currentDoor != nullptr )
    {
        path.emplace_back( currentDoor, currentSection );

        auto key = std::make_pair( currentDoor, currentSection );
        auto it = m_backtrack.find( key );

        if( it == m_backtrack.end() )
            break;

        currentDoor = it->second.backtrack_door;
        currentSection = it->second.backtrack_section;
    }

    // Reverse to get path from source to destination
    std::reverse( path.begin(), path.end() );

    return path;
}


bool MAZE_SEARCH::CheckAndHandleRipup( OBSTACLE_ROOM* aRoom, const MAZE_LIST_ELEMENT& aFromElement )
{
    if( !aRoom || !m_control.allow_ripup )
        return false;

    // Don't rip up items from the same net
    if( aRoom->GetNetCode() == m_netCode )
        return false;

    // Calculate the cost of routing through this obstacle (with ripup)
    VECTOR2I fromPt = aFromElement.entry_point;
    VECTOR2I toPt = aRoom->GetCenter();
    double throughCost = aFromElement.expansion_value +
                         CalculateTraceCost( fromPt, toPt, aFromElement.layer );

    // Estimate the cost of routing around (using heuristic)
    double aroundCost = throughCost * 2.0;  // Simplified estimate

    // Check if ripup or shove is beneficial
    RIPUP_RESULT result = m_ripupChecker.CheckRipup( aRoom, throughCost, aroundCost );

    if( result.should_shove )
    {
        // Push-and-shove: Apply the shoves
        // Note: Shoves will be applied via the ripup checker when the route is finalized
        // For now, just proceed through the obstacle - the shove makes room for us
        ExpandThroughObstacle( aRoom, aFromElement );
        return true;
    }

    if( result.should_ripup )
    {
        // Mark items for ripup
        m_ripupChecker.MarkForRipup( result.candidates );

        // Expand through the obstacle
        ExpandThroughObstacle( aRoom, aFromElement );

        return true;
    }

    return false;
}


void MAZE_SEARCH::ExpandThroughObstacle( OBSTACLE_ROOM* aRoom,
                                          const MAZE_LIST_ELEMENT& aFromElement )
{
    if( !aRoom )
        return;

    // Calculate the ripup cost for this obstacle
    double ripupCost = m_ripupChecker.CalculateRipupCost( aRoom );

    // Get the entry point (center of the obstacle)
    VECTOR2I entryPt = aRoom->GetCenter();
    int layer = aFromElement.layer;

    // Calculate base cost to reach the obstacle center
    double traceCost = CalculateTraceCost( aFromElement.entry_point, entryPt, layer );
    double baseCost = aFromElement.expansion_value + traceCost + ripupCost;

    // Expand to all doors of the obstacle room
    for( EXPANSION_DOOR* door : aRoom->GetDoors() )
    {
        int sectionCount = door->GetSectionCount();

        for( int section = 0; section < sectionCount; ++section )
        {
            if( door->IsOccupied( section ) )
                continue;

            VECTOR2I doorPt = door->GetSectionCenter( section );
            double doorCost = CalculateTraceCost( entryPt, doorPt, layer );
            double totalCost = baseCost + doorCost;

            // Calculate heuristic
            double heuristic = m_destDistance.Calculate( doorPt, layer, m_control.via_cost );

            MAZE_LIST_ELEMENT newElem;
            newElem.door = door;
            newElem.section_no = section;
            newElem.backtrack_door = aFromElement.door;
            newElem.backtrack_section = aFromElement.section_no;
            newElem.expansion_value = totalCost;
            newElem.sorting_value = totalCost + heuristic;
            newElem.layer = layer;
            newElem.entry_point = doorPt;
            newElem.shape_entry = door->GetSectionSegment( section );
            newElem.next_room = door->GetOtherRoom( aRoom );
            newElem.ripup_room = aRoom;  // Mark that this path goes through a ripup

            AddToQueue( newElem );
        }
    }
}
