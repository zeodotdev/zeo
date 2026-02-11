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
#include <board_item.h>
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

    INT_BOX workingShape = *roomShape;
    int layer = aRoom.GetLayer();
    COMPLETION_DEBUG( "Complete: workingShape [" << workingShape.Left() << "," << workingShape.Top()
                      << "] to [" << workingShape.Right() << "," << workingShape.Bottom() << "]" );

    // Find all neighbors
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

    BOX2I queryBounds = aShape.BoundingBox();
    COMPLETION_DEBUG( "FindNeighbours: queryBounds [" << queryBounds.GetX() << "," << queryBounds.GetY()
                      << "] size=" << queryBounds.GetWidth() << "x" << queryBounds.GetHeight() );

    int queryCount = 0;
    int skipSameNet = 0;
    int skipNoRoomItem = 0;
    int skipNoIntersect = 0;
    int skipPointTouch = 0;
    int skipAreaOverlap = 0;

    m_searchTree.QueryOverlapping( queryBounds, aLayer,
        [&]( const TREE_ENTRY& entry ) -> bool
        {
            queryCount++;
            // Skip entries from the same net (they're not obstacles)
            if( entry.room && entry.room->GetNetCode() == aNetCode )
            {
                skipSameNet++;
                return true;
            }

            // Get the entry's bounding box
            BOX2I entryBounds;
            if( entry.room )
                entryBounds = entry.room->GetBoundingBox();
            else if( entry.item )
                entryBounds = entry.item->GetBoundingBox();
            else
            {
                skipNoRoomItem++;
                return true;
            }

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
                // 2D area overlap - the obstacle is inside our (very large) incomplete room.
                // This is normal when incomplete rooms extend to board bounds.
                // We need to create a door at the obstacle's boundary that's inside our room.
                //
                // Calculate which edge of the obstacle is most "inside" our room
                // and use that as the door location.
                ROOM_NEIGHBOUR neighbour;
                neighbour.neighbour_room = entry.room;
                neighbour.neighbour_item = entry.item;

                // Find which side of the obstacle faces our room's contained edge
                // For now, use the intersection - we'll create a door at the obstacle boundary
                neighbour.intersection = *intersection;

                // Determine touching side based on which edge of the obstacle is most central
                VECTOR2I roomCenter = aShape.Center();
                VECTOR2I obstCenter = intersection->Center();

                int dx = std::abs( obstCenter.x - roomCenter.x );
                int dy = std::abs( obstCenter.y - roomCenter.y );

                if( dx > dy )
                {
                    // Horizontal separation - left or right edge
                    if( obstCenter.x < roomCenter.x )
                        neighbour.touching_side_no_of_room = 3;  // Left
                    else
                        neighbour.touching_side_no_of_room = 1;  // Right
                }
                else
                {
                    // Vertical separation - top or bottom edge
                    if( obstCenter.y < roomCenter.y )
                        neighbour.touching_side_no_of_room = 0;  // Top
                    else
                        neighbour.touching_side_no_of_room = 2;  // Bottom
                }

                neighbour.room_touch_is_corner = false;
                neighbour.neighbour_touch_is_corner = false;
                neighbour.touching_side_no_of_neighbour = 0;

                neighbours.push_back( neighbour );
                return true;
            }

            // 1D intersection - determine which side(s) touch
            ROOM_NEIGHBOUR neighbour;
            neighbour.neighbour_room = entry.room;
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
                      << " skipNoRoomItem=" << skipNoRoomItem
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
        if( neighbour.neighbour_room &&
            neighbour.neighbour_room->GetType() == ROOM_TYPE::OBSTACLE )
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
                aResult.completed_room.get(), neighbour.neighbour_room, doorSeg );
            aResult.completed_room->AddDoor( door.get() );
            neighbour.neighbour_room->AddDoor( door.get() );

            aResult.new_doors.push_back( std::move( door ) );
        }
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
            // (Simplified - a full implementation would handle all gap cases)
        }
    }
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
