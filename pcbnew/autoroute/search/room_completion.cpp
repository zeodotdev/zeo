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

#include "room_completion.h"
#include "../autoroute_engine.h"
#include "../expansion/target_door.h"
#include <board_item.h>
#include <pad.h>
#include <algorithm>
#include <cmath>
#include <iostream>

// Debug macro for room completion
#define COMPLETION_DEBUG( msg ) std::cerr << "[COMPLETION] " << msg << std::endl


//-----------------------------------------------------------------------------
// ROOM_NEIGHBOUR Implementation
//-----------------------------------------------------------------------------

bool ROOM_NEIGHBOUR::operator<( const ROOM_NEIGHBOUR& other ) const
{
    // First compare by touching side
    if( touching_side_no_of_room != other.touching_side_no_of_room )
        return touching_side_no_of_room < other.touching_side_no_of_room;

    // Then compare by distance along the side
    // (This is a simplification - FreeRouting does more complex corner handling)
    return intersection.Left() < other.intersection.Left() ||
           ( intersection.Left() == other.intersection.Left() &&
             intersection.Top() < other.intersection.Top() );
}


VECTOR2I ROOM_NEIGHBOUR::FirstCorner( const INT_BOX& roomShape ) const
{
    // Get the first corner of the intersection based on side direction
    switch( touching_side_no_of_room )
    {
    case 0: // Top edge (left to right)
        return VECTOR2I( intersection.Left(), intersection.Top() );
    case 1: // Right edge (top to bottom)
        return VECTOR2I( intersection.Right(), intersection.Top() );
    case 2: // Bottom edge (right to left)
        return VECTOR2I( intersection.Right(), intersection.Bottom() );
    case 3: // Left edge (bottom to top)
        return VECTOR2I( intersection.Left(), intersection.Bottom() );
    default:
        return intersection.Center();
    }
}


VECTOR2I ROOM_NEIGHBOUR::LastCorner( const INT_BOX& roomShape ) const
{
    // Get the last corner of the intersection based on side direction
    switch( touching_side_no_of_room )
    {
    case 0: // Top edge (left to right)
        return VECTOR2I( intersection.Right(), intersection.Top() );
    case 1: // Right edge (top to bottom)
        return VECTOR2I( intersection.Right(), intersection.Bottom() );
    case 2: // Bottom edge (right to left)
        return VECTOR2I( intersection.Left(), intersection.Bottom() );
    case 3: // Left edge (bottom to top)
        return VECTOR2I( intersection.Left(), intersection.Top() );
    default:
        return intersection.Center();
    }
}


//-----------------------------------------------------------------------------
// ROOM_COMPLETION Implementation
//-----------------------------------------------------------------------------

ROOM_COMPLETION::ROOM_COMPLETION( AUTOROUTE_ENGINE& aEngine, SHAPE_SEARCH_TREE& aSearchTree )
    : m_engine( aEngine )
    , m_searchTree( aSearchTree )
{
}


COMPLETION_RESULT ROOM_COMPLETION::Complete( INCOMPLETE_FREE_SPACE_ROOM& aRoom, int aNetCode )
{
    COMPLETION_DEBUG( "Complete: START, layer=" << aRoom.GetLayer() << " netCode=" << aNetCode );

    COMPLETION_RESULT result;

    const INT_BOX* roomShape = dynamic_cast<const INT_BOX*>( &aRoom.GetShape() );
    if( !roomShape )
    {
        COMPLETION_DEBUG( "Complete: non-box shape, returning" );
        // Non-box shapes not yet supported
        return result;
    }

    int layer = aRoom.GetLayer();

    // FreeRouting approach: Grow from the contained shape's center outward
    // This creates unique rooms per starting point, rather than shrinking from board bounds
    const TILE_SHAPE* containedShape = aRoom.GetContainedShape();
    VECTOR2I center;

    if( containedShape )
    {
        center = containedShape->Center();
        COMPLETION_DEBUG( "Complete: growing from contained center " << center.x << "," << center.y );
    }
    else
    {
        center = roomShape->Center();
        COMPLETION_DEBUG( "Complete: growing from shape center " << center.x << "," << center.y );
    }

    // Start with a small box around the center
    int initialSize = m_searchTree.GetCellSize();
    INT_BOX workingShape( VECTOR2I( center.x - initialSize, center.y - initialSize ),
                          VECTOR2I( center.x + initialSize, center.y + initialSize ) );

    // Grow outward until we hit obstacles on all 4 sides
    COMPLETION_DEBUG( "Complete: calling GrowFromCenter" );
    GrowFromCenter( workingShape, center, layer, aNetCode );
    COMPLETION_DEBUG( "Complete: after grow [" << workingShape.Left() << "," << workingShape.Top()
                      << "] to [" << workingShape.Right() << "," << workingShape.Bottom() << "]" );

    // Find all neighbors (should now be edge touches, not 2D overlaps)
    COMPLETION_DEBUG( "Complete: calling FindNeighbours" );
    auto neighbours = FindNeighbours( workingShape, layer, aNetCode );
    COMPLETION_DEBUG( "Complete: FindNeighbours returned " << neighbours.size() << " neighbours" );

    // Sort neighbors counterclockwise
    COMPLETION_DEBUG( "Complete: sorting neighbours" );
    SortNeighbours( neighbours, workingShape );

    // Try to enlarge the room if edges have no neighbors
    // (Iteratively until no more edges can be removed)
    COMPLETION_DEBUG( "Complete: starting TryRemoveEdge loop" );
    int maxIterations = 4;  // At most 4 edges
    int iteration = 0;
    while( maxIterations-- > 0 && TryRemoveEdge( workingShape, neighbours, layer, aNetCode ) )
    {
        COMPLETION_DEBUG( "Complete: TryRemoveEdge iteration " << iteration++ );
        neighbours = FindNeighbours( workingShape, layer, aNetCode );
        SortNeighbours( neighbours, workingShape );
    }
    COMPLETION_DEBUG( "Complete: TryRemoveEdge loop done" );

    // Create the complete room
    COMPLETION_DEBUG( "Complete: creating completed room" );
    result.completed_room = std::make_unique<FREE_SPACE_ROOM>(
        std::make_unique<INT_BOX>( workingShape ), layer );

    // Calculate doors and new incomplete rooms
    COMPLETION_DEBUG( "Complete: calling CalculateDoorsAndRooms" );
    CalculateDoorsAndRooms( workingShape, layer, neighbours, result );
    COMPLETION_DEBUG( "Complete: CalculateDoorsAndRooms done, newDoors=" << result.new_doors.size()
                      << " newIncomplete=" << result.new_incomplete_rooms.size() );

    COMPLETION_DEBUG( "Complete: done" );
    return result;
}


COMPLETION_RESULT ROOM_COMPLETION::CompleteObstacle( OBSTACLE_ROOM& aRoom, int aNetCode )
{
    COMPLETION_RESULT result;

    const INT_BOX* roomShape = dynamic_cast<const INT_BOX*>( &aRoom.GetShape() );
    if( !roomShape )
        return result;

    int layer = aRoom.GetLayer();

    // For obstacle rooms, create incomplete rooms for each edge
    // that doesn't have a touching neighbor

    std::vector<SEG> edges = roomShape->GetEdges();

    for( int i = 0; i < 4; ++i )
    {
        const SEG& edge = edges[i];

        // Create a half-plane extending from this edge
        VECTOR2I edgeDir = edge.B - edge.A;
        VECTOR2I normal( -edgeDir.y, edgeDir.x );  // Perpendicular, pointing outward

        // Normalize and extend
        double len = std::sqrt( double( normal.x ) * normal.x + double( normal.y ) * normal.y );
        if( len < 1 )
            continue;

        // Create incomplete room shape as a large box extending from this edge
        int extension = m_searchTree.GetBounds().GetWidth();  // Large value

        VECTOR2I pt1 = edge.A;
        VECTOR2I pt2 = edge.B;
        VECTOR2I pt3, pt4;

        // Extend outward
        switch( i )
        {
        case 0: // Top edge - extend upward
            pt3 = pt2 + VECTOR2I( 0, -extension );
            pt4 = pt1 + VECTOR2I( 0, -extension );
            break;
        case 1: // Right edge - extend rightward
            pt3 = pt2 + VECTOR2I( extension, 0 );
            pt4 = pt1 + VECTOR2I( extension, 0 );
            break;
        case 2: // Bottom edge - extend downward
            pt3 = pt2 + VECTOR2I( 0, extension );
            pt4 = pt1 + VECTOR2I( 0, extension );
            break;
        case 3: // Left edge - extend leftward
            pt3 = pt2 + VECTOR2I( -extension, 0 );
            pt4 = pt1 + VECTOR2I( -extension, 0 );
            break;
        }

        // Create bounding box for the incomplete room
        int minX = std::min( { pt1.x, pt2.x, pt3.x, pt4.x } );
        int maxX = std::max( { pt1.x, pt2.x, pt3.x, pt4.x } );
        int minY = std::min( { pt1.y, pt2.y, pt3.y, pt4.y } );
        int maxY = std::max( { pt1.y, pt2.y, pt3.y, pt4.y } );

        // Clip to board bounds
        const BOX2I& bounds = m_searchTree.GetBounds();
        minX = std::max( minX, bounds.GetX() );
        maxX = std::min( maxX, bounds.GetRight() );
        minY = std::max( minY, bounds.GetY() );
        maxY = std::min( maxY, bounds.GetBottom() );

        if( minX >= maxX || minY >= maxY )
            continue;

        auto incompleteShape = std::make_unique<INT_BOX>(
            VECTOR2I( minX, minY ), VECTOR2I( maxX, maxY ) );

        // Contained shape is the edge itself (as a thin box)
        auto containedShape = std::make_unique<INT_BOX>( edge.A, edge.B );

        auto incompleteRoom = std::make_unique<INCOMPLETE_FREE_SPACE_ROOM>(
            std::move( incompleteShape ), layer, std::move( containedShape ) );

        // Create door between obstacle and incomplete room
        auto door = std::make_unique<EXPANSION_DOOR>( &aRoom, incompleteRoom.get(), edge );
        aRoom.AddDoor( door.get() );
        incompleteRoom->AddDoor( door.get() );

        result.new_doors.push_back( std::move( door ) );
        result.new_incomplete_rooms.push_back( std::move( incompleteRoom ) );
    }

    return result;
}


std::vector<ROOM_NEIGHBOUR> ROOM_COMPLETION::FindNeighbours( const INT_BOX& aShape,
                                                              int aLayer,
                                                              int aNetCode )
{
    COMPLETION_DEBUG( "FindNeighbours: START, layer=" << aLayer << " netCode=" << aNetCode );

    std::vector<ROOM_NEIGHBOUR> neighbours;
    m_ownNetItems.clear();  // Clear own-net items from previous call

    BOX2I queryBounds = aShape.BoundingBox();
    COMPLETION_DEBUG( "FindNeighbours: queryBounds [" << queryBounds.GetX() << "," << queryBounds.GetY()
                      << "] size=" << queryBounds.GetWidth() << "x" << queryBounds.GetHeight() );

    int queryCount = 0;
    int skipSameNet = 0;
    int skipNoItem = 0;
    int skipNoIntersect = 0;
    int skipPointTouch = 0;
    int skipAreaOverlap = 0;

    m_searchTree.QueryOverlapping( queryBounds, aLayer,
        [&]( const TREE_ENTRY& entry ) -> bool
        {
            queryCount++;

            // Skip if no item (might be a free space room - not an obstacle)
            if( !entry.item )
            {
                skipNoItem++;
                return true;
            }

            // Check if same net - save for target doors, don't treat as obstacle
            if( !entry.IsTraceObstacle( aNetCode ) )
            {
                m_ownNetItems.push_back( entry.item );
                skipSameNet++;
                return true;
            }

            // Get item bounds and inflate with clearance (must match insertion)
            BOX2I entryBounds = entry.item->GetBoundingBox();
            entryBounds.Inflate( m_engine.GetControl().clearance );

            // Check intersection with our shape
            auto intersection = aShape.IntersectionBox(
                INT_BOX( entryBounds.GetOrigin(),
                         entryBounds.GetOrigin() + VECTOR2I( entryBounds.GetWidth(),
                                                              entryBounds.GetHeight() ) ) );

            if( !intersection || intersection->IsEmpty() )
            {
                skipNoIntersect++;
                return true;
            }

            // Determine dimension of intersection
            int intersectWidth = intersection->Width();
            int intersectHeight = intersection->Height();
            int dimension = ( intersectWidth > 0 && intersectHeight > 0 ) ? 2 :
                           ( intersectWidth > 0 || intersectHeight > 0 ) ? 1 : 0;

            if( dimension < 1 )
            {
                skipPointTouch++;
                return true;  // Point touch, skip
            }

            if( dimension > 1 )
            {
                // 2D area overlap - obstacle overlaps with our room interior.
                // Only add this as a neighbor if it touches a room edge (distance < threshold).
                // This prevents door explosion from interior obstacles while still handling
                // obstacles that are near the room boundary.

                static constexpr int EDGE_THRESHOLD = 100000;  // 0.1mm - must be very close to edge

                int distToTop = std::abs( intersection->Top() - aShape.Top() );
                int distToBottom = std::abs( intersection->Bottom() - aShape.Bottom() );
                int distToLeft = std::abs( intersection->Left() - aShape.Left() );
                int distToRight = std::abs( intersection->Right() - aShape.Right() );

                int minDist = std::min( { distToTop, distToBottom, distToLeft, distToRight } );

                // Only add as neighbor if obstacle is close to a room edge
                if( minDist > EDGE_THRESHOLD )
                {
                    skipAreaOverlap++;
                    return true;  // Skip interior obstacles
                }

                ROOM_NEIGHBOUR neighbour;
                neighbour.neighbour_room = nullptr;  // Room created lazily when needed for doors
                neighbour.neighbour_item = entry.item;

                // Create a 1D intersection at the closest room edge
                if( minDist == distToTop )
                {
                    neighbour.intersection = INT_BOX(
                        VECTOR2I( intersection->Left(), aShape.Top() ),
                        VECTOR2I( intersection->Right(), aShape.Top() ) );
                    neighbour.touching_side_no_of_room = 0;  // Top
                }
                else if( minDist == distToBottom )
                {
                    neighbour.intersection = INT_BOX(
                        VECTOR2I( intersection->Left(), aShape.Bottom() ),
                        VECTOR2I( intersection->Right(), aShape.Bottom() ) );
                    neighbour.touching_side_no_of_room = 2;  // Bottom
                }
                else if( minDist == distToLeft )
                {
                    neighbour.intersection = INT_BOX(
                        VECTOR2I( aShape.Left(), intersection->Top() ),
                        VECTOR2I( aShape.Left(), intersection->Bottom() ) );
                    neighbour.touching_side_no_of_room = 3;  // Left
                }
                else
                {
                    neighbour.intersection = INT_BOX(
                        VECTOR2I( aShape.Right(), intersection->Top() ),
                        VECTOR2I( aShape.Right(), intersection->Bottom() ) );
                    neighbour.touching_side_no_of_room = 1;  // Right
                }

                neighbour.room_touch_is_corner = false;
                neighbour.neighbour_touch_is_corner = false;
                neighbour.touching_side_no_of_neighbour = 0;

                neighbours.push_back( neighbour );
                return true;
            }

            // 1D intersection - determine which side(s) touch
            ROOM_NEIGHBOUR neighbour;
            neighbour.neighbour_room = nullptr;  // Room created lazily when needed for doors
            neighbour.neighbour_item = entry.item;
            neighbour.intersection = *intersection;

            // Determine touching side of our room
            if( intersectHeight > 0 )
            {
                // Vertical intersection
                if( std::abs( intersection->Left() - aShape.Left() ) < 10 )
                    neighbour.touching_side_no_of_room = 3;  // Left
                else
                    neighbour.touching_side_no_of_room = 1;  // Right
            }
            else
            {
                // Horizontal intersection
                if( std::abs( intersection->Top() - aShape.Top() ) < 10 )
                    neighbour.touching_side_no_of_room = 0;  // Top
                else
                    neighbour.touching_side_no_of_room = 2;  // Bottom
            }

            neighbour.room_touch_is_corner = false;  // Simplified
            neighbour.neighbour_touch_is_corner = false;
            neighbour.touching_side_no_of_neighbour = 0;  // Simplified

            neighbours.push_back( neighbour );
            return true;
        } );

    COMPLETION_DEBUG( "FindNeighbours: done, queryCount=" << queryCount
                      << " neighbours=" << neighbours.size()
                      << " skipSameNet=" << skipSameNet
                      << " skipNoItem=" << skipNoItem
                      << " skipNoIntersect=" << skipNoIntersect
                      << " skipPointTouch=" << skipPointTouch
                      << " skipAreaOverlap=" << skipAreaOverlap );
    return neighbours;
}


void ROOM_COMPLETION::SortNeighbours( std::vector<ROOM_NEIGHBOUR>& aNeighbours,
                                       const INT_BOX& aRoomShape )
{
    std::sort( aNeighbours.begin(), aNeighbours.end() );
}


void ROOM_COMPLETION::CalculateDoorsAndRooms( const INT_BOX& aRoomShape,
                                               int aLayer,
                                               const std::vector<ROOM_NEIGHBOUR>& aNeighbours,
                                               COMPLETION_RESULT& aResult )
{
    if( aNeighbours.empty() )
    {
        // No neighbors - create incomplete rooms for each side
        std::vector<SEG> edges = aRoomShape.GetEdges();
        for( int i = 0; i < 4; ++i )
        {
            auto incompleteRoom = CreateIncompleteRoom( aRoomShape, i,
                                                         edges[i].A, edges[i].B, aLayer );
            if( incompleteRoom )
            {
                // Create door
                auto door = std::make_unique<EXPANSION_DOOR>(
                    aResult.completed_room.get(), incompleteRoom.get(), edges[i] );
                aResult.completed_room->AddDoor( door.get() );
                incompleteRoom->AddDoor( door.get() );

                aResult.new_doors.push_back( std::move( door ) );
                aResult.new_incomplete_rooms.push_back( std::move( incompleteRoom ) );
            }
        }
        return;
    }

    // Process neighbors and create doors
    for( const auto& neighbour : aNeighbours )
    {
        // Get or create obstacle room for this neighbor item
        OBSTACLE_ROOM* obstacleRoom = nullptr;

        if( neighbour.neighbour_room &&
            neighbour.neighbour_room->GetType() == ROOM_TYPE::OBSTACLE )
        {
            obstacleRoom = static_cast<OBSTACLE_ROOM*>( neighbour.neighbour_room );
        }
        else if( neighbour.neighbour_item )
        {
            // Create obstacle room on-demand via ItemAutorouteInfo
            ITEM_AUTOROUTE_INFO* itemInfo = m_engine.GetItemAutorouteInfo( neighbour.neighbour_item );
            if( itemInfo )
            {
                obstacleRoom = itemInfo->GetExpansionRoom( aLayer, m_engine.GetControl().clearance );
            }
        }

        if( obstacleRoom )
        {
            // Create door to obstacle room
            SEG doorSeg;
            if( neighbour.intersection.Width() > neighbour.intersection.Height() )
            {
                int y = ( neighbour.intersection.Top() + neighbour.intersection.Bottom() ) / 2;
                doorSeg = SEG( VECTOR2I( neighbour.intersection.Left(), y ),
                              VECTOR2I( neighbour.intersection.Right(), y ) );
            }
            else
            {
                int x = ( neighbour.intersection.Left() + neighbour.intersection.Right() ) / 2;
                doorSeg = SEG( VECTOR2I( x, neighbour.intersection.Top() ),
                              VECTOR2I( x, neighbour.intersection.Bottom() ) );
            }

            auto door = std::make_unique<EXPANSION_DOOR>(
                aResult.completed_room.get(), obstacleRoom, doorSeg );
            aResult.completed_room->AddDoor( door.get() );
            obstacleRoom->AddDoor( door.get() );

            aResult.new_doors.push_back( std::move( door ) );
        }
    }

    // Create target doors for ALL own-net items found in m_ownNetItems
    // This follows FreeRouting's calculate_target_doors() pattern:
    // - Target doors are created for BOTH start and destination items
    // - The door's IsDestinationDoor() distinguishes them based on is_start_info
    // - Start item doors are added to queue during init
    // - Destination item doors trigger "found" when reached
    FREE_SPACE_ROOM* completedFreeRoom =
        dynamic_cast<FREE_SPACE_ROOM*>( aResult.completed_room.get() );

    for( BOARD_ITEM* item : m_ownNetItems )
    {
        // Get item's bounding box with clearance
        BOX2I itemBounds = item->GetBoundingBox();
        itemBounds.Inflate( m_engine.GetControl().clearance );

        // Check if item intersects with our room
        auto intersection = aRoomShape.IntersectionBox(
            INT_BOX( itemBounds.GetOrigin(),
                     itemBounds.GetOrigin() + VECTOR2I( itemBounds.GetWidth(),
                                                         itemBounds.GetHeight() ) ) );

        if( !intersection || intersection->IsEmpty() )
            continue;

        // Create a connection segment at the intersection
        SEG connectionSeg;
        if( intersection->Width() > intersection->Height() )
        {
            // Horizontal connection
            int y = ( intersection->Top() + intersection->Bottom() ) / 2;
            connectionSeg = SEG( VECTOR2I( intersection->Left(), y ),
                                 VECTOR2I( intersection->Right(), y ) );
        }
        else
        {
            // Vertical connection
            int x = ( intersection->Left() + intersection->Right() ) / 2;
            connectionSeg = SEG( VECTOR2I( x, intersection->Top() ),
                                 VECTOR2I( x, intersection->Bottom() ) );
        }

        // Determine if item is a start or destination item
        // Start items were NOT in m_destItems (destinations)
        bool isStartItem = ( m_destItems.find( item ) == m_destItems.end() );

        // Create target door with start/dest flag
        auto targetDoor = std::make_unique<TARGET_EXPANSION_DOOR>(
            item, completedFreeRoom, connectionSeg, isStartItem );

        // Add to room's target_doors collection (separate from regular doors)
        if( completedFreeRoom )
        {
            completedFreeRoom->AddTargetDoor( targetDoor.get() );
        }

        aResult.new_target_doors.push_back( std::move( targetDoor ) );

        COMPLETION_DEBUG( "CalculateDoorsAndRooms: created TARGET_EXPANSION_DOOR for "
                          << ( isStartItem ? "start" : "destination" ) << " item" );
    }

    // Create incomplete rooms in gaps between neighbors
    // Group neighbors by side
    std::vector<std::vector<const ROOM_NEIGHBOUR*>> neighboursBySide( 4 );
    for( const auto& n : aNeighbours )
    {
        if( n.touching_side_no_of_room >= 0 && n.touching_side_no_of_room < 4 )
            neighboursBySide[n.touching_side_no_of_room].push_back( &n );
    }

    std::vector<SEG> edges = aRoomShape.GetEdges();

    for( int side = 0; side < 4; ++side )
    {
        const auto& sideNeighbours = neighboursBySide[side];
        const SEG& edge = edges[side];

        if( sideNeighbours.empty() )
        {
            // Entire side is free - create incomplete room
            auto incompleteRoom = CreateIncompleteRoom( aRoomShape, side,
                                                         edge.A, edge.B, aLayer );
            if( incompleteRoom )
            {
                auto door = std::make_unique<EXPANSION_DOOR>(
                    aResult.completed_room.get(), incompleteRoom.get(), edge );
                aResult.completed_room->AddDoor( door.get() );
                incompleteRoom->AddDoor( door.get() );

                aResult.new_doors.push_back( std::move( door ) );
                aResult.new_incomplete_rooms.push_back( std::move( incompleteRoom ) );
            }
        }
        else
        {
            // Check for gaps between neighbors on this side
            // Sort neighbors along this side to find gaps
            std::vector<const ROOM_NEIGHBOUR*> sorted = sideNeighbours;

            // Sort by position along the edge
            if( side == 0 || side == 2 )  // Top or bottom (horizontal edge)
            {
                std::sort( sorted.begin(), sorted.end(),
                    []( const ROOM_NEIGHBOUR* a, const ROOM_NEIGHBOUR* b ) {
                        return a->intersection.Left() < b->intersection.Left();
                    } );
            }
            else  // Left or right (vertical edge)
            {
                std::sort( sorted.begin(), sorted.end(),
                    []( const ROOM_NEIGHBOUR* a, const ROOM_NEIGHBOUR* b ) {
                        return a->intersection.Top() < b->intersection.Top();
                    } );
            }

            // Find gaps between consecutive neighbors
            int edgeStart, edgeEnd;
            if( side == 0 || side == 2 )
            {
                edgeStart = edge.A.x < edge.B.x ? edge.A.x : edge.B.x;
                edgeEnd = edge.A.x < edge.B.x ? edge.B.x : edge.A.x;
            }
            else
            {
                edgeStart = edge.A.y < edge.B.y ? edge.A.y : edge.B.y;
                edgeEnd = edge.A.y < edge.B.y ? edge.B.y : edge.A.y;
            }

            int currentPos = edgeStart;
            int edgeY = ( side == 0 || side == 2 ) ? edge.A.y : 0;
            int edgeX = ( side == 1 || side == 3 ) ? edge.A.x : 0;

            for( const ROOM_NEIGHBOUR* n : sorted )
            {
                int nStart, nEnd;
                if( side == 0 || side == 2 )
                {
                    nStart = n->intersection.Left();
                    nEnd = n->intersection.Right();
                }
                else
                {
                    nStart = n->intersection.Top();
                    nEnd = n->intersection.Bottom();
                }

                // Gap before this neighbor
                if( nStart > currentPos )
                {
                    VECTOR2I gapStart, gapEnd;
                    if( side == 0 || side == 2 )
                    {
                        gapStart = VECTOR2I( currentPos, edgeY );
                        gapEnd = VECTOR2I( nStart, edgeY );
                    }
                    else
                    {
                        gapStart = VECTOR2I( edgeX, currentPos );
                        gapEnd = VECTOR2I( edgeX, nStart );
                    }

                    auto incompleteRoom = CreateIncompleteRoom( aRoomShape, side,
                                                                 gapStart, gapEnd, aLayer );
                    if( incompleteRoom )
                    {
                        SEG gapSeg( gapStart, gapEnd );
                        auto door = std::make_unique<EXPANSION_DOOR>(
                            aResult.completed_room.get(), incompleteRoom.get(), gapSeg );
                        aResult.completed_room->AddDoor( door.get() );
                        incompleteRoom->AddDoor( door.get() );

                        aResult.new_doors.push_back( std::move( door ) );
                        aResult.new_incomplete_rooms.push_back( std::move( incompleteRoom ) );
                    }
                }

                currentPos = std::max( currentPos, nEnd );
            }

            // Gap after last neighbor to edge end
            if( currentPos < edgeEnd )
            {
                VECTOR2I gapStart, gapEnd;
                if( side == 0 || side == 2 )
                {
                    gapStart = VECTOR2I( currentPos, edgeY );
                    gapEnd = VECTOR2I( edgeEnd, edgeY );
                }
                else
                {
                    gapStart = VECTOR2I( edgeX, currentPos );
                    gapEnd = VECTOR2I( edgeX, edgeEnd );
                }

                auto incompleteRoom = CreateIncompleteRoom( aRoomShape, side,
                                                             gapStart, gapEnd, aLayer );
                if( incompleteRoom )
                {
                    SEG gapSeg( gapStart, gapEnd );
                    auto door = std::make_unique<EXPANSION_DOOR>(
                        aResult.completed_room.get(), incompleteRoom.get(), gapSeg );
                    aResult.completed_room->AddDoor( door.get() );
                    incompleteRoom->AddDoor( door.get() );

                    aResult.new_doors.push_back( std::move( door ) );
                    aResult.new_incomplete_rooms.push_back( std::move( incompleteRoom ) );
                }
            }
        }
    }
}


int ROOM_COMPLETION::FindClosestObstacleDistance( const VECTOR2I& aFrom, int aDirection,
                                                    int aWidth, int aLayer, int aNetCode )
{
    // Search for closest obstacle in the given direction
    // aDirection: 0=up, 1=right, 2=down, 3=left
    // aWidth: the perpendicular width of the search corridor

    const BOX2I& bounds = m_searchTree.GetBounds();
    int maxDist = std::max( bounds.GetWidth(), bounds.GetHeight() );
    int closestDist = maxDist;

    // Create search corridor based on direction
    BOX2I searchArea;
    int halfWidth = aWidth / 2;

    switch( aDirection )
    {
    case 0:  // Up
    {
        int height = aFrom.y - bounds.GetY();
        if( height <= 0 )
            return 0;  // Already at top edge
        searchArea = BOX2I( VECTOR2I( aFrom.x - halfWidth, bounds.GetY() ),
                            VECTOR2I( aWidth, height ) );
        break;
    }
    case 1:  // Right
    {
        int width = bounds.GetRight() - aFrom.x;
        if( width <= 0 )
            return 0;  // Already at right edge
        searchArea = BOX2I( VECTOR2I( aFrom.x, aFrom.y - halfWidth ),
                            VECTOR2I( width, aWidth ) );
        break;
    }
    case 2:  // Down
    {
        int height = bounds.GetBottom() - aFrom.y;
        if( height <= 0 )
            return 0;  // Already at bottom edge
        searchArea = BOX2I( VECTOR2I( aFrom.x - halfWidth, aFrom.y ),
                            VECTOR2I( aWidth, height ) );
        break;
    }
    case 3:  // Left
    {
        int width = aFrom.x - bounds.GetX();
        if( width <= 0 )
            return 0;  // Already at left edge
        searchArea = BOX2I( VECTOR2I( bounds.GetX(), aFrom.y - halfWidth ),
                            VECTOR2I( width, aWidth ) );
        break;
    }
    default:
        return maxDist;
    }

    m_searchTree.QueryOverlapping( searchArea, aLayer,
        [&]( const TREE_ENTRY& entry ) -> bool
        {
            // Skip same-net rooms
            if( entry.room && entry.room->GetNetCode() == aNetCode )
                return true;

            // Skip same-net items (they're not obstacles for this net)
            if( entry.item && !entry.IsTraceObstacle( aNetCode ) )
                return true;

            // Get obstacle bounds
            BOX2I entryBounds;
            if( entry.room )
                entryBounds = entry.room->GetBoundingBox();
            else if( entry.item )
            {
                entryBounds = entry.item->GetBoundingBox();
                entryBounds.Inflate( m_engine.GetControl().clearance );
            }
            else
                return true;

            // Calculate distance from aFrom to this obstacle
            int dist = 0;
            switch( aDirection )
            {
            case 0:  // Up - distance to bottom edge of obstacle
                if( entryBounds.GetBottom() <= aFrom.y )
                    dist = aFrom.y - entryBounds.GetBottom();
                else
                    return true;  // Obstacle is below us
                break;
            case 1:  // Right - distance to left edge of obstacle
                if( entryBounds.GetX() >= aFrom.x )
                    dist = entryBounds.GetX() - aFrom.x;
                else
                    return true;  // Obstacle is to our left
                break;
            case 2:  // Down - distance to top edge of obstacle
                if( entryBounds.GetY() >= aFrom.y )
                    dist = entryBounds.GetY() - aFrom.y;
                else
                    return true;  // Obstacle is above us
                break;
            case 3:  // Left - distance to right edge of obstacle
                if( entryBounds.GetRight() <= aFrom.x )
                    dist = aFrom.x - entryBounds.GetRight();
                else
                    return true;  // Obstacle is to our right
                break;
            }

            if( dist < closestDist )
                closestDist = dist;

            return true;
        } );

    return closestDist;
}


void ROOM_COMPLETION::GrowFromCenter( INT_BOX& aShape, const VECTOR2I& aCenter,
                                       int aLayer, int aNetCode )
{
    // FreeRouting approach: Start small and grow outward until hitting obstacles
    // This creates unique rooms per starting point

    const BOX2I& bounds = m_searchTree.GetBounds();
    int cellSize = m_searchTree.GetCellSize();
    int minSize = cellSize;

    // Clearance buffer: leave space for trace_width/2 + clearance
    // This ensures the room boundary doesn't violate clearance from obstacles
    int traceWidth = m_engine.GetControl().GetTraceWidth( aLayer );
    int clearanceBuffer = m_engine.GetControl().clearance + traceWidth / 2;

    // Find closest obstacle in each direction
    // Use trace width as corridor width to find obstacles in routing path
    int corridorWidth = std::max( traceWidth, cellSize );

    int distUp = FindClosestObstacleDistance( aCenter, 0, corridorWidth, aLayer, aNetCode );
    int distRight = FindClosestObstacleDistance( aCenter, 1, corridorWidth, aLayer, aNetCode );
    int distDown = FindClosestObstacleDistance( aCenter, 2, corridorWidth, aLayer, aNetCode );
    int distLeft = FindClosestObstacleDistance( aCenter, 3, corridorWidth, aLayer, aNetCode );

    COMPLETION_DEBUG( "GrowFromCenter: distances up=" << distUp << " right=" << distRight
                      << " down=" << distDown << " left=" << distLeft
                      << " clearanceBuffer=" << clearanceBuffer );

    // Calculate room boundaries with clearance buffer (clamped to board bounds)
    // Subtract clearance buffer from distances to leave space for routing
    int top = std::max( bounds.GetY(), aCenter.y - std::max( 0, distUp - clearanceBuffer ) );
    int bottom = std::min( bounds.GetBottom(), aCenter.y + std::max( 0, distDown - clearanceBuffer ) );
    int left = std::max( bounds.GetX(), aCenter.x - std::max( 0, distLeft - clearanceBuffer ) );
    int right = std::min( bounds.GetRight(), aCenter.x + std::max( 0, distRight - clearanceBuffer ) );

    // Ensure minimum size
    if( right - left < minSize )
    {
        int expand = ( minSize - ( right - left ) ) / 2 + 1;
        left = std::max( bounds.GetX(), left - expand );
        right = std::min( bounds.GetRight(), right + expand );
    }
    if( bottom - top < minSize )
    {
        int expand = ( minSize - ( bottom - top ) ) / 2 + 1;
        top = std::max( bounds.GetY(), top - expand );
        bottom = std::min( bounds.GetBottom(), bottom + expand );
    }

    // CRITICAL: Limit maximum room size to prevent massive rooms that cause
    // FindNeighbours to hang when querying huge areas. On empty layers, rooms
    // can grow to board-size (55mm x 41mm), causing O(n) spatial queries to stall.
    // Maximum 10mm x 10mm rooms provide good routing granularity without hanging.
    static constexpr int MAX_ROOM_SIZE = 10000000;  // 10mm in nm
    if( right - left > MAX_ROOM_SIZE )
    {
        int excess = ( right - left - MAX_ROOM_SIZE ) / 2;
        left += excess;
        right -= excess;
        COMPLETION_DEBUG( "GrowFromCenter: limiting width to MAX_ROOM_SIZE" );
    }
    if( bottom - top > MAX_ROOM_SIZE )
    {
        int excess = ( bottom - top - MAX_ROOM_SIZE ) / 2;
        top += excess;
        bottom -= excess;
        COMPLETION_DEBUG( "GrowFromCenter: limiting height to MAX_ROOM_SIZE" );
    }

    aShape = INT_BOX( VECTOR2I( left, top ), VECTOR2I( right, bottom ) );

    COMPLETION_DEBUG( "GrowFromCenter: result [" << left << "," << top
                      << "] to [" << right << "," << bottom << "]"
                      << " size " << ( right - left ) << "x" << ( bottom - top ) );
}


bool ROOM_COMPLETION::TryRemoveEdge( INT_BOX& aShape,
                                      const std::vector<ROOM_NEIGHBOUR>& aNeighbours,
                                      int aLayer, int aNetCode )
{
    // Check which sides have no touching neighbors
    bool hasTouchOnSide[4] = { false, false, false, false };

    for( const auto& n : aNeighbours )
    {
        if( n.touching_side_no_of_room >= 0 && n.touching_side_no_of_room < 4 )
            hasTouchOnSide[n.touching_side_no_of_room] = true;
    }

    // Find first side without touch and try to extend
    for( int side = 0; side < 4; ++side )
    {
        if( !hasTouchOnSide[side] )
        {
            // Extend this side outward
            const BOX2I& bounds = m_searchTree.GetBounds();
            int extension = m_searchTree.GetCellSize() * 2;

            INT_BOX newShape;
            BOX2I extensionArea;

            switch( side )
            {
            case 0: // Top
                if( aShape.Top() > bounds.GetY() )
                {
                    newShape = INT_BOX( VECTOR2I( aShape.Left(), aShape.Top() - extension ),
                                        aShape.Max() );
                    // Area being added
                    extensionArea = BOX2I( VECTOR2I( aShape.Left(), aShape.Top() - extension ),
                                           VECTOR2I( aShape.Width(), extension ) );
                }
                else
                    continue;
                break;
            case 1: // Right
                if( aShape.Right() < bounds.GetRight() )
                {
                    newShape = INT_BOX( aShape.Min(),
                                        VECTOR2I( aShape.Right() + extension, aShape.Bottom() ) );
                    extensionArea = BOX2I( VECTOR2I( aShape.Right(), aShape.Top() ),
                                           VECTOR2I( extension, aShape.Height() ) );
                }
                else
                    continue;
                break;
            case 2: // Bottom
                if( aShape.Bottom() < bounds.GetBottom() )
                {
                    newShape = INT_BOX( aShape.Min(),
                                        VECTOR2I( aShape.Right(), aShape.Bottom() + extension ) );
                    extensionArea = BOX2I( VECTOR2I( aShape.Left(), aShape.Bottom() ),
                                           VECTOR2I( aShape.Width(), extension ) );
                }
                else
                    continue;
                break;
            case 3: // Left
                if( aShape.Left() > bounds.GetX() )
                {
                    newShape = INT_BOX( VECTOR2I( aShape.Left() - extension, aShape.Top() ),
                                        aShape.Max() );
                    extensionArea = BOX2I( VECTOR2I( aShape.Left() - extension, aShape.Top() ),
                                           VECTOR2I( extension, aShape.Height() ) );
                }
                else
                    continue;
                break;
            default:
                continue;
            }

            // Check bounds
            if( newShape.Top() < bounds.GetY() ||
                newShape.Bottom() > bounds.GetBottom() ||
                newShape.Left() < bounds.GetX() ||
                newShape.Right() > bounds.GetRight() )
            {
                continue;
            }

            // Check if extension area overlaps any obstacles
            // Skip same-net obstacles (they're not blocking)
            if( !m_searchTree.HasOverlap( extensionArea, aLayer, aNetCode ) )
            {
                aShape = newShape;
                return true;
            }
        }
    }

    return false;
}


bool ROOM_COMPLETION::IsDoorValid( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2,
                                    const INT_BOX& aDoorShape )
{
    // Check if door already exists
    for( EXPANSION_DOOR* door : aRoom1->GetDoors() )
    {
        if( door->GetOtherRoom( aRoom1 ) == aRoom2 )
            return false;
    }

    return true;
}


std::unique_ptr<EXPANSION_DOOR> ROOM_COMPLETION::CreateDoor( EXPANSION_ROOM* aRoom1,
                                                              EXPANSION_ROOM* aRoom2,
                                                              const SEG& aDoorSegment )
{
    return std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, aDoorSegment );
}


std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM> ROOM_COMPLETION::CreateIncompleteRoom(
    const INT_BOX& aRoomShape,
    int aSide,
    const VECTOR2I& aStart,
    const VECTOR2I& aEnd,
    int aLayer )
{
    const BOX2I& bounds = m_searchTree.GetBounds();
    int extension = bounds.GetWidth();  // Large value to extend to board edge

    VECTOR2I min, max;

    switch( aSide )
    {
    case 0: // Top edge - incomplete room extends upward
        min = VECTOR2I( std::min( aStart.x, aEnd.x ), bounds.GetY() );
        max = VECTOR2I( std::max( aStart.x, aEnd.x ), aRoomShape.Top() );
        break;
    case 1: // Right edge - incomplete room extends rightward
        min = VECTOR2I( aRoomShape.Right(), std::min( aStart.y, aEnd.y ) );
        max = VECTOR2I( bounds.GetRight(), std::max( aStart.y, aEnd.y ) );
        break;
    case 2: // Bottom edge - incomplete room extends downward
        min = VECTOR2I( std::min( aStart.x, aEnd.x ), aRoomShape.Bottom() );
        max = VECTOR2I( std::max( aStart.x, aEnd.x ), bounds.GetBottom() );
        break;
    case 3: // Left edge - incomplete room extends leftward
        min = VECTOR2I( bounds.GetX(), std::min( aStart.y, aEnd.y ) );
        max = VECTOR2I( aRoomShape.Left(), std::max( aStart.y, aEnd.y ) );
        break;
    default:
        return nullptr;
    }

    if( min.x >= max.x || min.y >= max.y )
        return nullptr;

    auto shape = std::make_unique<INT_BOX>( min, max );
    auto contained = std::make_unique<INT_BOX>( aStart, aEnd );

    return std::make_unique<INCOMPLETE_FREE_SPACE_ROOM>(
        std::move( shape ), aLayer, std::move( contained ) );
}
