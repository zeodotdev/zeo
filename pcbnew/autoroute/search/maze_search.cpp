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
#include "../autoroute_engine.h"
#include "../expansion/expansion_room.h"
#include "../expansion/expansion_door.h"
#include "../expansion/expansion_drill.h"
#include <pad.h>
#include <cmath>


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
    m_resultElement = MAZE_LIST_ELEMENT();
}


bool MAZE_SEARCH::InitializeSearch()
{
    // Convert source items (pads) to their expansion rooms
    m_sourceRooms.clear();

    for( BOARD_ITEM* item : m_sourceItems )
    {
        PAD* pad = dynamic_cast<PAD*>( item );
        if( pad )
        {
            // Find expansion rooms for this pad
            std::vector<EXPANSION_ROOM*> padRooms = m_engine.CreatePadRooms( pad );
            for( EXPANSION_ROOM* room : padRooms )
            {
                m_sourceRooms.insert( room );
            }
        }
    }

    if( m_sourceRooms.empty() )
        return false;

    // For each source room, add all doors to the priority queue with cost 0
    for( EXPANSION_ROOM* sourceRoom : m_sourceRooms )
    {
        // Add target door for the source room itself (to start expansion)
        for( EXPANSION_DOOR* door : sourceRoom->GetDoors() )
        {
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
                elem.next_room = door->GetOtherRoom( sourceRoom );

                AddToQueue( elem );
            }
        }
    }

    return !m_queue.empty();
}


std::optional<MAZE_SEARCH_RESULT> MAZE_SEARCH::FindConnection()
{
    Reset();

    if( !InitializeSearch() )
        return std::nullopt;

    // Main A* loop
    while( !m_queue.empty() )
    {
        if( OccupyNextElement() )
        {
            // Found path to destination
            MAZE_SEARCH_RESULT result;
            result.destination_door = m_resultElement.door;
            result.section_no = m_resultElement.section_no;
            result.destination_room = m_resultElement.next_room;
            result.total_cost = m_resultElement.expansion_value;
            return result;
        }
    }

    return std::nullopt;  // No path found
}


bool MAZE_SEARCH::OccupyNextElement()
{
    if( m_queue.empty() )
        return false;

    MAZE_LIST_ELEMENT element = m_queue.top();
    m_queue.pop();
    m_nodesExpanded++;

    // Check if this door section is already occupied
    if( element.door->IsOccupied( element.section_no ) )
        return false;

    // Mark as occupied
    element.door->SetOccupied( element.section_no, true );

    // Store backtrack information
    StoreBacktrack( element.door, element.section_no,
                    element.backtrack_door, element.backtrack_section,
                    element.entry_point, element.layer );

    // Check if we've reached a destination
    if( element.next_room && m_destDistance.IsDestination( element.next_room ) )
    {
        m_resultElement = element;
        m_foundPath = true;
        return true;
    }

    // Expand to doors in the next room
    if( element.next_room )
    {
        ExpandToRoomDoors( element );
    }

    // If this is a drill, expand to other layers
    EXPANSION_DRILL* drill = dynamic_cast<EXPANSION_DRILL*>( element.door );
    if( drill && m_control.vias_allowed )
    {
        ExpandToOtherLayers( element );
    }

    return false;
}


void MAZE_SEARCH::ExpandToRoomDoors( const MAZE_LIST_ELEMENT& aElement )
{
    EXPANSION_ROOM* room = aElement.next_room;
    if( !room )
        return;

    // Check room type
    if( room->GetType() == ROOM_TYPE::OBSTACLE )
    {
        OBSTACLE_ROOM* obstRoom = dynamic_cast<OBSTACLE_ROOM*>( room );

        // Check if this is our own net (can pass through)
        if( obstRoom && obstRoom->GetNetCode() == m_netCode )
        {
            // Can pass through our own net's obstacles
        }
        else if( obstRoom && m_control.allow_ripup )
        {
            // Try ripup
            CheckAndHandleRipup( obstRoom, aElement );
            return;  // ExpandThroughObstacle handles the expansion
        }
        else
        {
            // Can't enter this obstacle room
            return;
        }
    }

    room->SetVisited( true );

    // Expand to all doors of this room
    for( EXPANSION_DOOR* door : room->GetDoors() )
    {
        // Don't go back through the door we came from
        if( door == aElement.door )
            continue;

        // Try each section of the door
        int sectionCount = door->GetSectionCount();
        for( int section = 0; section < sectionCount; ++section )
        {
            ExpandToDoorSection( door, section, aElement );
        }
    }

    // Also expand to any drills within this room (for layer transitions)
    if( m_control.vias_allowed )
    {
        ExpandToDrillsInRoom( room, aElement );
    }
}


void MAZE_SEARCH::ExpandToDoorSection( EXPANSION_DOOR* aDoor, int aSection,
                                        const MAZE_LIST_ELEMENT& aFromElement )
{
    // Check if already occupied
    if( aDoor->IsOccupied( aSection ) )
        return;

    // Calculate cost to reach this door section
    VECTOR2I fromPt = aFromElement.entry_point;
    VECTOR2I toPt = aDoor->GetSectionCenter( aSection );
    int layer = aFromElement.layer;

    double traceCost = CalculateTraceCost( fromPt, toPt, layer );
    double newExpansion = aFromElement.expansion_value + traceCost;

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
    newElem.next_room = aDoor->GetOtherRoom( aFromElement.next_room );

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
        newElem.next_room = drill->GetRoomForLayer( toLayer );

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

    // Add direction change penalty if applicable
    // (This could be more sophisticated based on previous direction)

    return distance * m_control.trace_cost;
}


void MAZE_SEARCH::AddToQueue( const MAZE_LIST_ELEMENT& aElement )
{
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

    // Check if ripup is beneficial
    RIPUP_RESULT result = m_ripupChecker.CheckRipup( aRoom, throughCost, aroundCost );

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
