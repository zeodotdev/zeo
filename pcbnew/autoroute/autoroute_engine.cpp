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

#include "autoroute_engine.h"
#include "search/maze_search.h"
#include "search/room_completion.h"
#include "locate/locate_connection.h"
#include "insert/insert_connection.h"
#include "geometry/tile_shape.h"
#include "expansion/drill_page.h"

#include <board.h>
#include <pad.h>
#include <pcb_track.h>
#include <footprint.h>
#include <zone.h>
#include <connectivity/connectivity_data.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

// Debug macro for autorouter - uses std::cerr for unbuffered output
#define AUTOROUTE_DEBUG( msg ) std::cerr << "[AUTOROUTE] " << msg << std::endl


AUTOROUTE_ENGINE::AUTOROUTE_ENGINE()
{
}


AUTOROUTE_ENGINE::~AUTOROUTE_ENGINE()
{
    ClearRoomModel();
}


void AUTOROUTE_ENGINE::Initialize( BOARD* aBoard, const AUTOROUTE_CONTROL& aControl )
{
    m_board = aBoard;
    m_control = aControl;
    m_result = AUTOROUTE_RESULT();
    m_cancelled.store( false );

    if( m_board )
    {
        // Get layer count from board
        m_layerCount = m_board->GetCopperLayerCount();
    }
}


bool AUTOROUTE_ENGINE::ReportProgress( const AUTOROUTE_PROGRESS& aProgress )
{
    if( m_cancelled.load() )
        return false;

    if( m_progressCallback )
    {
        if( !m_progressCallback( aProgress ) )
        {
            m_cancelled.store( true );
            return false;
        }
    }

    return true;
}


void AUTOROUTE_ENGINE::ClearRoomModel()
{
    m_rooms.clear();
    m_doors.clear();
    m_drills.clear();
    m_incompleteRooms.clear();
    m_roomsByLayer.clear();
    m_searchTree.Clear();
    m_itemInfo.clear();
    m_drillPageArray.reset();
}


void AUTOROUTE_ENGINE::BuildRoomModel()
{
    AUTOROUTE_DEBUG( "BuildRoomModel: START" );

    ClearRoomModel();

    if( !m_board )
    {
        AUTOROUTE_DEBUG( "BuildRoomModel: No board, returning" );
        return;
    }

    // Initialize the search tree
    BOX2I bounds = GetBoardBounds();
    AUTOROUTE_DEBUG( "BuildRoomModel: bounds valid=" << bounds.IsValid()
                     << " w=" << bounds.GetWidth() << " h=" << bounds.GetHeight() );

    if( bounds.IsValid() )
    {
        // Cell size = clearance * 2 for good balance of resolution vs performance
        int cellSize = m_control.clearance * 2;
        AUTOROUTE_DEBUG( "BuildRoomModel: Initializing search tree, cellSize=" << cellSize
                         << " layers=" << m_layerCount );
        m_searchTree.Initialize( bounds, cellSize, m_layerCount );

        // Initialize congestion map with larger cells (1mm for efficiency)
        int congestionCellSize = 1000000;  // 1mm cells
        m_congestionMap = std::make_unique<CONGESTION_MAP>( bounds, congestionCellSize, m_layerCount );
        AUTOROUTE_DEBUG( "BuildRoomModel: Congestion map initialized" );

        // Initialize drill page array for lazy via location calculation
        int viaPageWidth = std::max( m_control.via_diameter * 5, 500000 );
        m_drillPageArray = std::make_unique<DRILL_PAGE_ARRAY>( bounds, viaPageWidth, 0, m_layerCount - 1 );
        AUTOROUTE_DEBUG( "BuildRoomModel: Drill page array initialized, pageWidth=" << viaPageWidth );
    }

    // Step 1: Insert board items directly into search tree (no obstacle rooms)
    AUTOROUTE_DEBUG( "BuildRoomModel: Calling InsertBoardItems" );
    InsertBoardItems();
    AUTOROUTE_DEBUG( "BuildRoomModel: InsertBoardItems done" );

    // Step 2: Create initial incomplete rooms adjacent to obstacles
    // These will be completed on-demand during maze search (FreeRouting-style dynamic expansion)
    AUTOROUTE_DEBUG( "BuildRoomModel: Calling BuildInitialIncompleteRooms" );
    BuildInitialIncompleteRooms();
    AUTOROUTE_DEBUG( "BuildRoomModel: BuildInitialIncompleteRooms done, incomplete="
                     << m_incompleteRooms.size() << " doors=" << m_doors.size() );

    // Step 3: Create potential via locations
    if( m_control.vias_allowed && m_layerCount > 1 )
    {
        AUTOROUTE_DEBUG( "BuildRoomModel: Calling BuildDrills" );
        BuildDrills();
        AUTOROUTE_DEBUG( "BuildRoomModel: BuildDrills done, drills=" << m_drills.size() );
    }

    AUTOROUTE_DEBUG( "BuildRoomModel: COMPLETE" );
}


void AUTOROUTE_ENGINE::InsertBoardItems()
{
    AUTOROUTE_DEBUG( "InsertBoardItems: START" );

    if( !m_board )
        return;

    int clearance = m_control.clearance;
    int padCount = 0;
    int trackCount = 0;
    int zoneCount = 0;

    // Insert pads directly into search tree
    AUTOROUTE_DEBUG( "InsertBoardItems: Processing pads" );
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            LSET layers = pad->GetLayerSet();

            for( int layer = 0; layer < m_layerCount; ++layer )
            {
                PCB_LAYER_ID pcbLayer = ( layer == 0 ) ? F_Cu :
                                        ( layer == m_layerCount - 1 ) ? B_Cu :
                                        static_cast<PCB_LAYER_ID>( In1_Cu + layer - 1 );

                if( !layers.test( pcbLayer ) )
                    continue;

                BOX2I padBox = pad->GetBoundingBox();
                padBox.Inflate( clearance );
                m_searchTree.Insert( pad, padBox, layer );
                padCount++;
            }
        }
    }
    AUTOROUTE_DEBUG( "InsertBoardItems: Inserted " << padCount << " pad entries" );

    // Insert tracks directly into search tree
    AUTOROUTE_DEBUG( "InsertBoardItems: Processing tracks" );
    for( PCB_TRACK* track : m_board->Tracks() )
    {
        int pcbLayer = track->GetLayer();

        // Simple layer mapping
        int layer = ( pcbLayer == F_Cu ) ? 0 :
                    ( pcbLayer == B_Cu ) ? m_layerCount - 1 :
                    ( pcbLayer - In1_Cu + 1 );

        if( layer < 0 || layer >= m_layerCount )
            continue;

        BOX2I trackBox = track->GetBoundingBox();
        trackBox.Inflate( clearance );
        m_searchTree.Insert( track, trackBox, layer );
        trackCount++;
    }
    AUTOROUTE_DEBUG( "InsertBoardItems: Inserted " << trackCount << " track entries" );

    // Insert zones (keepouts and copper zones) directly into search tree
    AUTOROUTE_DEBUG( "InsertBoardItems: Processing zones" );
    for( ZONE* zone : m_board->Zones() )
    {
        // Check if this is a keepout zone or a copper zone
        bool isKeepout = zone->GetIsRuleArea();
        bool isCopper = !isKeepout && zone->IsOnCopperLayer();

        // Skip zones that don't affect routing
        if( !isKeepout && !isCopper )
            continue;

        // For keepout zones, check what's actually being kept out
        if( isKeepout )
        {
            // Only create obstacles if traces or vias are not allowed
            if( !zone->GetDoNotAllowTracks() && !zone->GetDoNotAllowVias() )
                continue;
        }

        // Get zone bounding box with clearance
        BOX2I zoneBox = zone->GetBoundingBox();
        zoneBox.Inflate( clearance );

        // Get zone layers
        LSET layers = zone->GetLayerSet();

        for( int layer = 0; layer < m_layerCount; ++layer )
        {
            PCB_LAYER_ID pcbLayer = ( layer == 0 ) ? F_Cu :
                                    ( layer == m_layerCount - 1 ) ? B_Cu :
                                    static_cast<PCB_LAYER_ID>( In1_Cu + layer - 1 );

            if( !layers.test( pcbLayer ) )
                continue;

            m_searchTree.Insert( zone, zoneBox, layer );
            zoneCount++;
        }
    }
    AUTOROUTE_DEBUG( "InsertBoardItems: Inserted " << zoneCount << " zone entries" );

    // Verification: check if items are in the tree
    AUTOROUTE_DEBUG( "InsertBoardItems: Verifying - itemCount=" << m_searchTree.GetItemCount()
                     << " roomCount=" << m_searchTree.GetRoomCount()
                     << " layerCount=" << m_searchTree.GetLayerCount() );

    // Test query for first pad to verify insertion worked
    if( padCount > 0 && !m_board->Footprints().empty() )
    {
        for( FOOTPRINT* fp : m_board->Footprints() )
        {
            if( fp->Pads().empty() )
                continue;

            PAD* testPad = fp->Pads()[0];
            BOX2I testBox = testPad->GetBoundingBox();
            testBox.Inflate( clearance );

            AUTOROUTE_DEBUG( "InsertBoardItems: Test pad at " << testPad->GetPosition().x
                             << "," << testPad->GetPosition().y
                             << " box [" << testBox.GetX() << "," << testBox.GetY()
                             << "] size " << testBox.GetWidth() << "x" << testBox.GetHeight() );

            LSET layers = testPad->GetLayerSet();
            for( int layer = 0; layer < m_layerCount; ++layer )
            {
                PCB_LAYER_ID pcbLayer = ( layer == 0 ) ? F_Cu :
                                        ( layer == m_layerCount - 1 ) ? B_Cu :
                                        static_cast<PCB_LAYER_ID>( In1_Cu + layer - 1 );

                if( !layers.test( pcbLayer ) )
                    continue;

                int itemsFound = 0;
                int roomsFound = 0;
                bool padFound = false;

                m_searchTree.QueryOverlapping( testBox, layer,
                    [&]( const TREE_ENTRY& entry ) -> bool
                    {
                        if( entry.item )
                        {
                            itemsFound++;
                            if( entry.item == testPad )
                                padFound = true;
                        }
                        if( entry.room )
                            roomsFound++;
                        return true;
                    } );

                AUTOROUTE_DEBUG( "InsertBoardItems: Layer " << layer << " query: items="
                                 << itemsFound << " rooms=" << roomsFound
                                 << " padFound=" << padFound );
            }
            break;  // Only test first footprint with pads
        }
    }

    AUTOROUTE_DEBUG( "InsertBoardItems: COMPLETE" );
}


ITEM_AUTOROUTE_INFO* AUTOROUTE_ENGINE::GetItemAutorouteInfo( BOARD_ITEM* aItem )
{
    if( !aItem )
        return nullptr;

    auto it = m_itemInfo.find( aItem );
    if( it != m_itemInfo.end() )
        return it->second.get();

    // Create new info for this item
    auto info = std::make_unique<ITEM_AUTOROUTE_INFO>( aItem );
    ITEM_AUTOROUTE_INFO* infoPtr = info.get();
    m_itemInfo[aItem] = std::move( info );

    return infoPtr;
}


void AUTOROUTE_ENGINE::BuildFreeSpaceRooms()
{
    // DEPRECATED: This method builds a pre-computed grid of free space rooms.
    // The new approach uses dynamic room expansion via BuildInitialIncompleteRooms().
    // Keeping this for reference/fallback.

    BOX2I boardBounds = GetBoardBounds();
    if( !boardBounds.IsValid() )
        return;

    // Create a grid of free space rooms
    // Grid size balances resolution vs. performance (O(n²) door building)
    // Smaller grid = better routing quality but slower (clearance * 2 recommended)
    int gridSize = m_control.clearance * 2;

    for( int layer = 0; layer < m_layerCount; ++layer )
    {
        for( int x = boardBounds.GetLeft(); x < boardBounds.GetRight(); x += gridSize )
        {
            for( int y = boardBounds.GetTop(); y < boardBounds.GetBottom(); y += gridSize )
            {
                VECTOR2I min( x, y );
                VECTOR2I max( std::min( x + gridSize, boardBounds.GetRight() ),
                              std::min( y + gridSize, boardBounds.GetBottom() ) );

                // Check if this cell truly overlaps any obstacle (not just touches)
                // Use OverlapsBox() instead of IntersectsBox() so that cells adjacent
                // to obstacles are still created - they will connect via doors
                INT_BOX cellBox( min, max );
                bool blocked = false;

                for( EXPANSION_ROOM* room : m_roomsByLayer[layer] )
                {
                    if( room->GetType() == ROOM_TYPE::OBSTACLE )
                    {
                        const INT_BOX* obstBox = dynamic_cast<const INT_BOX*>( &room->GetShape() );
                        if( obstBox && cellBox.OverlapsBox( *obstBox ) )
                        {
                            blocked = true;
                            break;
                        }
                    }
                }

                if( !blocked )
                {
                    auto shape = std::make_unique<INT_BOX>( min, max );
                    auto room = std::make_unique<FREE_SPACE_ROOM>( std::move( shape ), layer );

                    m_roomsByLayer[layer].push_back( room.get() );
                    m_rooms.push_back( std::move( room ) );
                }
            }
        }
    }
}


void AUTOROUTE_ENGINE::BuildInitialIncompleteRooms()
{
    AUTOROUTE_DEBUG( "BuildInitialIncompleteRooms: START" );

    // With the new architecture, incomplete rooms are created on-demand
    // during maze search when expanding from pad connection points.
    // This avoids pre-creating thousands of incomplete rooms for every obstacle.
    //
    // The CreatePadRooms() method creates initial incomplete rooms starting
    // from each pad's connection point, which are then completed dynamically
    // as the maze search expands through the board.
    //
    // This follows FreeRouting's lazy room expansion approach more closely.

    AUTOROUTE_DEBUG( "BuildInitialIncompleteRooms: Using on-demand room creation" );
    AUTOROUTE_DEBUG( "BuildInitialIncompleteRooms: COMPLETE (no pre-created rooms)" );
}


void AUTOROUTE_ENGINE::BuildDoors()
{
    // Create doors between adjacent rooms on the same layer
    for( int layer = 0; layer < m_layerCount; ++layer )
    {
        const auto& rooms = m_roomsByLayer[layer];

        for( size_t i = 0; i < rooms.size(); ++i )
        {
            for( size_t j = i + 1; j < rooms.size(); ++j )
            {
                if( AreRoomsAdjacent( rooms[i], rooms[j] ) )
                {
                    EXPANSION_DOOR* door = CreateDoor( rooms[i], rooms[j] );
                    if( door )
                    {
                        rooms[i]->AddDoor( door );
                        rooms[j]->AddDoor( door );
                    }
                }
            }
        }
    }
}


bool AUTOROUTE_ENGINE::AreRoomsAdjacent( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2 )
{
    if( !aRoom1 || !aRoom2 )
        return false;

    const INT_BOX* box1 = dynamic_cast<const INT_BOX*>( &aRoom1->GetShape() );
    const INT_BOX* box2 = dynamic_cast<const INT_BOX*>( &aRoom2->GetShape() );

    if( !box1 || !box2 )
        return false;

    // Check if boxes touch exactly (share an edge)
    auto touching = box1->TouchingSegment( *box2 );
    if( touching.has_value() && touching->Length() > 0 )
        return true;

    // For obstacle rooms connecting to free space, we need to be more lenient
    // because grid cells don't align with obstacle boundaries.
    // Check if boxes are "close enough" using minimum distance between them.
    int tolerance = m_control.clearance * 2;  // Allow gaps up to grid size

    // Calculate the gap in each direction (negative means overlap)
    int horizGap = std::max( box1->Left() - box2->Right(), box2->Left() - box1->Right() );
    int vertGap = std::max( box1->Top() - box2->Bottom(), box2->Top() - box1->Bottom() );

    // If both gaps are negative, boxes overlap - shouldn't happen for non-blocked cells
    if( horizGap < 0 && vertGap < 0 )
        return false;

    // If one gap is negative (overlap in that direction), check the other gap
    if( horizGap < 0 )
    {
        // Boxes overlap horizontally, check if vertical gap is within tolerance
        return vertGap <= tolerance;
    }

    if( vertGap < 0 )
    {
        // Boxes overlap vertically, check if horizontal gap is within tolerance
        return horizGap <= tolerance;
    }

    // Both gaps are positive (boxes don't overlap in either direction)
    // Consider adjacent if the boxes are close enough (within tolerance in both directions)
    // This handles diagonal gaps created by grid misalignment
    // Use a Chebyshev distance check: max(horizGap, vertGap) <= tolerance
    return std::max( horizGap, vertGap ) <= tolerance;
}


EXPANSION_DOOR* AUTOROUTE_ENGINE::CreateDoor( EXPANSION_ROOM* aRoom1, EXPANSION_ROOM* aRoom2 )
{
    const INT_BOX* box1 = dynamic_cast<const INT_BOX*>( &aRoom1->GetShape() );
    const INT_BOX* box2 = dynamic_cast<const INT_BOX*>( &aRoom2->GetShape() );

    if( !box1 || !box2 )
        return nullptr;

    // Try exact touching first
    auto touching = box1->TouchingSegment( *box2 );
    if( touching && touching->Length() > 0 )
    {
        auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, *touching );
        EXPANSION_DOOR* doorPtr = door.get();
        m_doors.push_back( std::move( door ) );
        return doorPtr;
    }

    // For nearly-adjacent rooms, create a door at the closest points between them
    int tolerance = m_control.clearance * 2;

    // Calculate gaps in each direction
    int horizGap = std::max( box1->Left() - box2->Right(), box2->Left() - box1->Right() );
    int vertGap = std::max( box1->Top() - box2->Bottom(), box2->Top() - box1->Bottom() );

    // Determine which box is on which side
    bool box2IsRight = box2->Left() >= box1->Right();
    bool box2IsLeft = box1->Left() >= box2->Right();
    bool box2IsBelow = box2->Top() >= box1->Bottom();
    bool box2IsAbove = box1->Top() >= box2->Bottom();

    // Case 1: Boxes overlap horizontally (vertGap is the relevant gap)
    if( horizGap < 0 && vertGap >= 0 && vertGap <= tolerance )
    {
        // Vertical gap only - create horizontal door segment
        int left = std::max( box1->Left(), box2->Left() );
        int right = std::min( box1->Right(), box2->Right() );
        int midY = box2IsBelow ? ( box1->Bottom() + box2->Top() ) / 2
                               : ( box2->Bottom() + box1->Top() ) / 2;
        SEG doorSeg( VECTOR2I( left, midY ), VECTOR2I( right, midY ) );
        auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
        EXPANSION_DOOR* doorPtr = door.get();
        m_doors.push_back( std::move( door ) );
        return doorPtr;
    }

    // Case 2: Boxes overlap vertically (horizGap is the relevant gap)
    if( vertGap < 0 && horizGap >= 0 && horizGap <= tolerance )
    {
        // Horizontal gap only - create vertical door segment
        int top = std::max( box1->Top(), box2->Top() );
        int bottom = std::min( box1->Bottom(), box2->Bottom() );
        int midX = box2IsRight ? ( box1->Right() + box2->Left() ) / 2
                               : ( box2->Right() + box1->Left() ) / 2;
        SEG doorSeg( VECTOR2I( midX, top ), VECTOR2I( midX, bottom ) );
        auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
        EXPANSION_DOOR* doorPtr = door.get();
        m_doors.push_back( std::move( door ) );
        return doorPtr;
    }

    // Case 3: Diagonal gap (both horizGap and vertGap are positive)
    // Create a door at the closest corner between the boxes
    if( horizGap >= 0 && vertGap >= 0 && std::max( horizGap, vertGap ) <= tolerance )
    {
        VECTOR2I pt1, pt2;

        // Find the closest corners
        if( box2IsRight && box2IsBelow )
        {
            // box2 is to the bottom-right of box1
            pt1 = VECTOR2I( box1->Right(), box1->Bottom() );
            pt2 = VECTOR2I( box2->Left(), box2->Top() );
        }
        else if( box2IsRight && box2IsAbove )
        {
            // box2 is to the top-right of box1
            pt1 = VECTOR2I( box1->Right(), box1->Top() );
            pt2 = VECTOR2I( box2->Left(), box2->Bottom() );
        }
        else if( box2IsLeft && box2IsBelow )
        {
            // box2 is to the bottom-left of box1
            pt1 = VECTOR2I( box1->Left(), box1->Bottom() );
            pt2 = VECTOR2I( box2->Right(), box2->Top() );
        }
        else if( box2IsLeft && box2IsAbove )
        {
            // box2 is to the top-left of box1
            pt1 = VECTOR2I( box1->Left(), box1->Top() );
            pt2 = VECTOR2I( box2->Right(), box2->Bottom() );
        }
        else
        {
            // Fallback: use centers
            pt1 = box1->Center();
            pt2 = box2->Center();
        }

        // Create door at midpoint between closest corners
        VECTOR2I midPt( ( pt1.x + pt2.x ) / 2, ( pt1.y + pt2.y ) / 2 );
        // Create a small door segment (point-like) at the midpoint
        SEG doorSeg( midPt, midPt );
        auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
        EXPANSION_DOOR* doorPtr = door.get();
        m_doors.push_back( std::move( door ) );
        return doorPtr;
    }

    return nullptr;
}


void AUTOROUTE_ENGINE::ConnectOrphanObstacles()
{
    // Find obstacle rooms with no doors and connect them to nearest free space
    for( auto& room : m_rooms )
    {
        if( room->GetType() != ROOM_TYPE::OBSTACLE )
            continue;

        if( !room->GetDoors().empty() )
            continue;  // Already has doors

        EXPANSION_ROOM* nearest = FindNearestFreeSpace( room.get() );
        if( nearest )
        {
            EXPANSION_DOOR* door = CreateDoor( room.get(), nearest );
            if( door )
            {
                room->AddDoor( door );
                nearest->AddDoor( door );
            }
        }
    }
}


EXPANSION_ROOM* AUTOROUTE_ENGINE::FindNearestFreeSpace( EXPANSION_ROOM* aObstacle )
{
    if( !aObstacle )
        return nullptr;

    int layer = aObstacle->GetLayer();
    VECTOR2I obstCenter = aObstacle->GetCenter();

    EXPANSION_ROOM* nearest = nullptr;
    int64_t minDist = std::numeric_limits<int64_t>::max();

    for( EXPANSION_ROOM* room : m_roomsByLayer[layer] )
    {
        if( room->GetType() != ROOM_TYPE::FREE_SPACE )
            continue;

        // Calculate distance between centers
        VECTOR2I roomCenter = room->GetCenter();
        int64_t dx = roomCenter.x - obstCenter.x;
        int64_t dy = roomCenter.y - obstCenter.y;
        int64_t dist = dx * dx + dy * dy;  // Squared distance

        if( dist < minDist )
        {
            minDist = dist;
            nearest = room;
        }
    }

    return nearest;
}


void AUTOROUTE_ENGINE::BuildDrills()
{
    AUTOROUTE_DEBUG( "BuildDrills: START" );

    // Create potential via locations across layers.
    // With dynamic room expansion, we check for obstacle-free positions
    // rather than relying on pre-computed free space rooms.

    // Use larger grid for faster processing - vias are created dynamically during routing
    int gridSize = std::max( m_control.clearance * 5, 500000 );  // At least 0.5mm grid
    BOX2I bounds = GetBoardBounds();

    if( !bounds.IsValid() || m_layerCount < 2 )
    {
        AUTOROUTE_DEBUG( "BuildDrills: Invalid bounds or single layer, returning" );
        return;
    }

    AUTOROUTE_DEBUG( "BuildDrills: gridSize=" << gridSize
                     << " bounds=" << bounds.GetWidth() << "x" << bounds.GetHeight() );

    // Limit total drills to prevent memory/performance issues
    static constexpr int MAX_DRILLS = 5000;
    int drillCount = 0;
    int iterations = 0;

    // Via clearance area for checking obstacles
    int viaClearance = m_control.via_diameter / 2 + m_control.clearance;

    for( int x = bounds.GetLeft() + gridSize; x < bounds.GetRight(); x += gridSize )
    {
        if( m_cancelled.load() || drillCount >= MAX_DRILLS )
            break;

        for( int y = bounds.GetTop() + gridSize; y < bounds.GetBottom(); y += gridSize )
        {
            iterations++;
            if( iterations % 10000 == 0 )
            {
                AUTOROUTE_DEBUG( "BuildDrills: iteration " << iterations << " drills=" << drillCount );
            }

            if( drillCount >= MAX_DRILLS )
                break;
            VECTOR2I pos( x, y );

            // Check if this position is clear of obstacles on all layers
            BOX2I viaBox( VECTOR2I( x - viaClearance, y - viaClearance ),
                          VECTOR2I( viaClearance * 2, viaClearance * 2 ) );

            int layersClear = 0;

            for( int layer = 0; layer < m_layerCount; ++layer )
            {
                // Check if the via position overlaps any obstacles on this layer
                // Using net code -1 means we check against all obstacles
                if( !m_searchTree.HasOverlap( viaBox, layer, -1 ) )
                {
                    layersClear++;
                }
            }

            // Need at least 2 layers clear for a via to be useful
            if( layersClear >= 2 )
            {
                auto drill = std::make_unique<EXPANSION_DRILL>( pos, 0, m_layerCount - 1 );
                drill->SetViaDiameter( m_control.via_diameter );
                drill->SetDrillDiameter( m_control.via_drill );

                // Add drill to overlapping pages in the drill page array
                if( m_drillPageArray )
                {
                    auto pages = m_drillPageArray->GetOverlappingPages( viaBox );
                    for( DRILL_PAGE* page : pages )
                    {
                        page->AddDrill( drill.get() );
                    }
                }

                m_drills.push_back( std::move( drill ) );
                drillCount++;
            }
        }
    }
}


void AUTOROUTE_ENGINE::ResetSearchState()
{
    for( auto& room : m_rooms )
    {
        room->SetVisited( false );
    }

    for( auto& door : m_doors )
    {
        door->ClearOccupied();
    }

    for( auto& drill : m_drills )
    {
        drill->ClearOccupied();
    }

    // Reset target doors' occupied flags
    for( auto& targetDoor : m_targetDoors )
    {
        targetDoor->ClearOccupied();
    }

    // Reset is_start_info flags from previous route
    for( auto& [item, info] : m_itemInfo )
    {
        info->SetStartInfo( false );
    }
}


std::vector<EXPANSION_DRILL*> AUTOROUTE_ENGINE::GetDrillsInRoom( EXPANSION_ROOM* aRoom, int aLayer ) const
{
    std::vector<EXPANSION_DRILL*> result;

    if( !aRoom )
        return result;

    for( const auto& drill : m_drills )
    {
        // Check if drill is on the specified layer and contained in the room
        if( drill->CanReachLayer( aLayer ) && aRoom->Contains( drill->GetLocation() ) )
        {
            result.push_back( drill.get() );
        }
    }

    return result;
}


BOX2I AUTOROUTE_ENGINE::GetBoardBounds() const
{
    if( !m_board )
        return BOX2I();

    return m_board->GetBoundingBox();
}


std::vector<NET_CONNECTION> AUTOROUTE_ENGINE::GetConnectionsToRoute() const
{
    std::vector<NET_CONNECTION> connections;

    if( !m_board )
        return connections;

    // Get connectivity data
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = m_board->GetConnectivity();
    if( !connectivity )
        return connections;

    // Get all nets - NetsByName() returns a map<wxString, NETINFO_ITEM*>
    const NETINFO_LIST& netInfo = m_board->GetNetInfo();

    for( NETINFO_ITEM* net : netInfo )
    {
        if( net->GetNetCode() == 0 )  // Skip unconnected net
            continue;

        // Check if we should route this net
        if( !m_control.nets_to_route.empty() &&
            m_control.nets_to_route.find( net->GetNetname().ToStdString() ) ==
            m_control.nets_to_route.end() )
        {
            continue;
        }

        // Get all pads for this net
        std::set<BOARD_ITEM*> pads;
        for( FOOTPRINT* fp : m_board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->GetNetCode() == net->GetNetCode() )
                {
                    pads.insert( pad );
                }
            }
        }

        if( pads.size() < 2 )
            continue;  // Nothing to route

        // Create a connection for each pair of unconnected pads
        // For now, just create one connection from first pad to rest
        auto it = pads.begin();
        BOARD_ITEM* firstPad = *it;
        ++it;

        NET_CONNECTION conn;
        conn.net_name = net->GetNetname().ToStdString();
        conn.net_code = net->GetNetCode();
        conn.source_pads.insert( firstPad );

        while( it != pads.end() )
        {
            conn.dest_pads.insert( *it );
            ++it;
        }

        connections.push_back( conn );
    }

    return connections;
}


double AUTOROUTE_ENGINE::CalculateNetPriority( const NET_CONNECTION& aConnection ) const
{
    // Priority calculation based on FreeRouting's approach:
    // - Net class priority (user-specified)
    // - Fewer pads = simpler net = higher priority (route first)
    // - Shorter total wire length = simpler = higher priority
    // - Power/ground nets (GND, VCC, VDD, etc.) route last

    double priority = 0.0;

    // Check for net class priority
    const NET_CLASS_CONFIG* netClass = m_control.GetNetClass( aConnection.net_name );
    if( netClass )
    {
        priority += netClass->priority;
    }

    // Check for power/ground nets (route these last)
    std::string netName = aConnection.net_name;
    std::transform( netName.begin(), netName.end(), netName.begin(), ::toupper );
    if( netName.find( "GND" ) != std::string::npos ||
        netName.find( "VCC" ) != std::string::npos ||
        netName.find( "VDD" ) != std::string::npos ||
        netName.find( "PWR" ) != std::string::npos ||
        netName.find( "POWER" ) != std::string::npos )
    {
        priority += 10000.0;  // Large penalty to route last
    }

    // Pad count factor: fewer pads = simpler
    size_t totalPads = aConnection.source_pads.size() + aConnection.dest_pads.size();
    priority += totalPads * 100.0;

    // Calculate minimum spanning tree length estimate
    // This gives preference to shorter/simpler nets
    double totalLength = 0.0;
    std::vector<VECTOR2I> padPositions;

    for( BOARD_ITEM* item : aConnection.source_pads )
    {
        if( PAD* pad = dynamic_cast<PAD*>( item ) )
        {
            padPositions.push_back( pad->GetPosition() );
        }
    }
    for( BOARD_ITEM* item : aConnection.dest_pads )
    {
        if( PAD* pad = dynamic_cast<PAD*>( item ) )
        {
            padPositions.push_back( pad->GetPosition() );
        }
    }

    // Simple estimate: sum of distances from first pad to all others
    // A true MST would be better but this is faster
    if( padPositions.size() >= 2 )
    {
        VECTOR2I center( 0, 0 );
        for( const auto& pos : padPositions )
        {
            center.x += pos.x / static_cast<int>( padPositions.size() );
            center.y += pos.y / static_cast<int>( padPositions.size() );
        }

        for( const auto& pos : padPositions )
        {
            int64_t dx = pos.x - center.x;
            int64_t dy = pos.y - center.y;
            totalLength += std::sqrt( double( dx * dx + dy * dy ) );
        }
    }

    // Convert length to mm and add to priority (shorter = lower priority value)
    priority += totalLength / 1000000.0;

    return priority;
}


void AUTOROUTE_ENGINE::OrderConnections( std::vector<NET_CONNECTION>& aConnections ) const
{
    // Calculate priorities for all connections
    std::vector<std::pair<double, size_t>> priorities;
    priorities.reserve( aConnections.size() );

    for( size_t i = 0; i < aConnections.size(); ++i )
    {
        double priority = CalculateNetPriority( aConnections[i] );
        priorities.emplace_back( priority, i );
    }

    // Sort by priority (lower = higher priority = route first)
    std::sort( priorities.begin(), priorities.end(),
               []( const auto& a, const auto& b ) { return a.first < b.first; } );

    // Reorder connections
    std::vector<NET_CONNECTION> ordered;
    ordered.reserve( aConnections.size() );

    for( const auto& [priority, index] : priorities )
    {
        ordered.push_back( std::move( aConnections[index] ) );
    }

    aConnections = std::move( ordered );
}


std::string AUTOROUTE_ENGINE::RouteConnection( const NET_CONNECTION& aConnection, int aPass )
{
    AUTOROUTE_DEBUG( "RouteConnection: START net='" << aConnection.net_name
                     << "' pass=" << aPass
                     << " sources=" << aConnection.source_pads.size()
                     << " dests=" << aConnection.dest_pads.size() );

    // Check for cancellation before starting
    if( m_cancelled.load() )
    {
        AUTOROUTE_DEBUG( "RouteConnection: Cancelled" );
        return "";
    }

    ResetSearchState();

    // Create pass-specific control parameters
    // Later passes use more aggressive settings (lower via cost, ripup allowed)
    AUTOROUTE_CONTROL passControl = m_control;

    if( m_control.multi_pass_enabled && aPass > 0 )
    {
        // Reduce via cost in later passes to encourage finding solutions
        passControl.via_cost = m_control.GetPassViaCost( aPass );

        // Enable ripup in later passes
        if( aPass >= 2 )
        {
            passControl.allow_ripup = true;
        }
    }

    // Create maze search with pass-specific parameters
    AUTOROUTE_DEBUG( "RouteConnection: Creating MAZE_SEARCH" );
    MAZE_SEARCH search( *this, passControl );
    search.SetNetCode( aConnection.net_code );
    search.SetCongestionMap( m_congestionMap.get() );  // For congestion-aware routing
    search.SetCancelledFlag( &m_cancelled );  // Allow external cancellation

    AUTOROUTE_DEBUG( "RouteConnection: Setting sources" );
    search.SetSources( aConnection.source_pads );
    AUTOROUTE_DEBUG( "RouteConnection: Setting destinations" );
    search.SetDestinations( aConnection.dest_pads );

    // Debug: Check source rooms
    AUTOROUTE_DEBUG( "RouteConnection: Checking source rooms" );
    int sourceRoomCount = 0;
    int sourceDoorsTotal = 0;
    for( BOARD_ITEM* item : aConnection.source_pads )
    {
        PAD* pad = dynamic_cast<PAD*>( item );
        if( pad )
        {
            std::vector<EXPANSION_ROOM*> rooms = CreatePadRooms( pad );
            sourceRoomCount += rooms.size();
            for( EXPANSION_ROOM* room : rooms )
            {
                sourceDoorsTotal += room->GetDoors().size();
            }
        }
    }
    AUTOROUTE_DEBUG( "RouteConnection: sourceRooms=" << sourceRoomCount
                     << " sourceDoors=" << sourceDoorsTotal );

    // Find path
    AUTOROUTE_DEBUG( "RouteConnection: Calling FindConnection" );
    auto result = search.FindConnection();
    AUTOROUTE_DEBUG( "RouteConnection: FindConnection returned, found=" << result.has_value()
                     << " nodes=" << search.GetNodesExpanded() );

    if( !result )
    {
        m_result.nets_failed++;
        m_result.failed_nets.push_back( aConnection.net_name );

        // Store debug info for first failure only (to keep message short)
        if( m_result.nets_failed == 1 )
        {
            m_result.error_message += "First fail '" + aConnection.net_name + "': ";
            m_result.error_message += std::to_string( sourceRoomCount ) + " rooms, ";
            m_result.error_message += std::to_string( sourceDoorsTotal ) + " doors; ";
        }
        return "";
    }

    m_result.nets_routed++;

    // Check if any items need to be ripped up
    const auto& ripupItems = search.GetRipupItems();
    if( !ripupItems.empty() && m_board )
    {
        // Remove items marked for ripup
        for( BOARD_ITEM* item : ripupItems )
        {
            PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item );
            if( track )
            {
                m_board->Remove( track );
                // Note: In a real implementation, we'd need to rebuild
                // the room model to reflect removed items
            }
        }
    }

    // Locate the path with net-specific parameters
    int netTraceWidth = passControl.GetNetTraceWidth( aConnection.net_name, 0 );
    int netClearance = passControl.GetNetClearance( aConnection.net_name );

    LOCATE_CONNECTION locator;
    locator.SetMazeSearch( &search );
    locator.SetTrackWidth( netTraceWidth );
    locator.SetClearance( netClearance );
    locator.SetBoard( m_board );
    locator.SetSearchTree( &m_searchTree );
    locator.SetControl( &m_control );
    locator.SetNetCode( aConnection.net_code );

    ROUTING_PATH path = locator.LocatePath();

    if( !path.IsValid() )
        return "";

    // Insert tracks directly into board with net-specific parameters
    AUTOROUTE_CONTROL netControl = m_control;
    netControl.via_diameter = passControl.GetNetViaDiameter( aConnection.net_name );
    netControl.via_drill = passControl.GetNetViaDrill( aConnection.net_name );

    INSERT_CONNECTION inserter;
    inserter.SetBoard( m_board );
    inserter.SetCommit( m_commit );  // Use commit for proper persistence
    inserter.SetSearchTree( &m_searchTree );  // For collision detection
    inserter.SetCongestionMap( m_congestionMap.get() );  // For congestion tracking
    inserter.SetControl( netControl );
    inserter.SetNetName( aConnection.net_name );
    inserter.SetNetCode( aConnection.net_code );  // For same-net filtering

    INSERT_RESULT insertResult = inserter.Insert( path );

    m_result.tracks_added += insertResult.tracks_added;
    m_result.vias_added += insertResult.vias_added;

    return insertResult.success ? "ok" : "";
}


std::string AUTOROUTE_ENGINE::RouteAll()
{
    AUTOROUTE_DEBUG( "RouteAll: START" );

    m_startTime = std::chrono::steady_clock::now();
    m_cancelled.store( false );

    AUTOROUTE_DEBUG( "RouteAll: Calling BuildRoomModel" );
    BuildRoomModel();
    AUTOROUTE_DEBUG( "RouteAll: BuildRoomModel complete" );

    // Debug: Track room model build time
    auto buildEndTime = std::chrono::steady_clock::now();
    double buildTimeMs = std::chrono::duration<double, std::milli>( buildEndTime - m_startTime ).count();

    // Debug: Log room and door counts
    int totalRooms = m_rooms.size();
    int totalDoors = m_doors.size();
    int totalDrills = m_drills.size();

    int obstacleRooms = 0, freeSpaceRooms = 0;
    int roomsWithDoors = 0;
    for( const auto& room : m_rooms )
    {
        if( room->GetType() == ROOM_TYPE::OBSTACLE )
            obstacleRooms++;
        else if( room->GetType() == ROOM_TYPE::FREE_SPACE )
            freeSpaceRooms++;

        if( !room->GetDoors().empty() )
            roomsWithDoors++;
    }

    std::vector<NET_CONNECTION> connections = GetConnectionsToRoute();

    // Order connections for optimal routing (simpler nets first)
    OrderConnections( connections );

    // Get board bounds for debug
    BOX2I bounds = GetBoardBounds();
    bool boundsValid = bounds.GetWidth() > 0 && bounds.GetHeight() > 0;

    // Store debug stats in result for visibility
    std::ostringstream debugStats;
    debugStats << "Rooms:" << totalRooms << "(obst:" << obstacleRooms << ",free:" << freeSpaceRooms << ")";
    debugStats << " Doors:" << totalDoors << " WithDoors:" << roomsWithDoors;
    debugStats << " Bounds:" << (boundsValid ? "OK" : "INVALID");
    if( boundsValid )
    {
        debugStats << "(" << bounds.GetWidth() / 1000000.0 << "x" << bounds.GetHeight() / 1000000.0 << "mm)";
    }
    debugStats << " Layers:" << m_layerCount;
    debugStats << " IncompleteRooms:" << m_incompleteRooms.size();
    debugStats << " Drills:" << m_drills.size();
    debugStats << " Nets:" << connections.size();
    debugStats << " BuildTime:" << static_cast<int>( buildTimeMs ) << "ms; ";

    m_result.error_message = debugStats.str();

    // Check for cancellation after building room model
    if( m_cancelled.load() )
    {
        m_result.error_message += "Cancelled during setup; ";
        m_result.cancelled = true;
        return "";
    }

    std::ostringstream allCode;

    allCode << "# Autoroute generated code\n";
    allCode << "import json\n";
    allCode << "from kipy.geometry import Vector2\n";
    allCode << "from kipy.proto.board.board_types_pb2 import BoardLayer\n";
    allCode << "\n";
    allCode << "total_tracks = 0\n";
    allCode << "total_vias = 0\n";
    allCode << "nets_routed = 0\n";
    allCode << "nets_failed = 0\n";
    allCode << "\n";

    // Track failed nets for retry
    std::vector<NET_CONNECTION> failedNets;
    bool timeExceeded = false;
    bool cancelled = false;

    // Determine number of passes based on configuration
    int numPasses = m_control.multi_pass_enabled ? m_control.num_passes : 1;
    int totalNets = connections.size();
    int netIndex = 0;

    // First pass (pass 0): route all nets in priority order with basic settings
    allCode << "# Pass 1: Basic routing (via_cost=" << m_control.via_cost << ")\n";
    allCode << "\n";

    AUTOROUTE_DEBUG( "RouteAll: Starting pass 1, " << totalNets << " nets to route" );

    for( const auto& conn : connections )
    {
        netIndex++;
        AUTOROUTE_DEBUG( "RouteAll: Routing net " << netIndex << "/" << totalNets
                         << " '" << conn.net_name << "'" );

        // Report progress
        AUTOROUTE_PROGRESS progress;
        progress.current_net = netIndex;
        progress.total_nets = totalNets;
        progress.current_pass = 1;
        progress.total_passes = numPasses;
        progress.nets_routed = m_result.nets_routed;
        progress.nets_failed = m_result.nets_failed;
        progress.current_net_name = conn.net_name;
        progress.phase = "routing";
        progress.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_startTime ).count();
        progress.percent_complete = ( double( netIndex ) / totalNets ) * 100.0 / numPasses;

        if( !ReportProgress( progress ) )
        {
            cancelled = true;
            break;
        }

        std::string code = RouteConnection( conn, 0 );  // Pass 0

        if( !code.empty() )
        {
            allCode << "# Route net: " << conn.net_name << "\n";
            allCode << code;
            allCode << "nets_routed += 1\n";
            allCode << "\n";
        }
        else
        {
            // Track failed nets for retry
            failedNets.push_back( conn );
            allCode << "# Deferred net (pass 1): " << conn.net_name << "\n";
            allCode << "\n";
        }

        // Check time limit
        if( m_control.max_time_seconds > 0 )
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>( now - m_startTime ).count();

            if( elapsed > m_control.max_time_seconds )
            {
                m_result.error_message = "Time limit exceeded";
                timeExceeded = true;
                break;
            }
        }
    }

    // Multi-pass optimization: retry failed nets with progressively more aggressive settings
    int passNum = 1;  // Already did pass 0

    while( !failedNets.empty() && !timeExceeded && !cancelled && passNum < numPasses )
    {
        std::vector<NET_CONNECTION> stillFailed;
        int passNetIndex = 0;
        int passNets = failedNets.size();

        double passViaCost = m_control.GetPassViaCost( passNum );
        bool passRipupEnabled = ( passNum >= 2 ) && m_control.allow_ripup;

        allCode << "# Pass " << ( passNum + 1 ) << ": Aggressive routing";
        allCode << " (via_cost=" << passViaCost;
        if( passRipupEnabled )
            allCode << ", ripup=enabled";
        allCode << ")\n";
        allCode << "\n";

        for( const auto& conn : failedNets )
        {
            passNetIndex++;

            // Report progress
            AUTOROUTE_PROGRESS progress;
            progress.current_net = passNetIndex;
            progress.total_nets = passNets;
            progress.current_pass = passNum + 1;
            progress.total_passes = numPasses;
            progress.nets_routed = m_result.nets_routed;
            progress.nets_failed = m_result.nets_failed;
            progress.current_net_name = conn.net_name;
            progress.phase = "retry";
            progress.elapsed_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_startTime ).count();
            progress.percent_complete = ( double( passNum ) / numPasses +
                double( passNetIndex ) / passNets / numPasses ) * 100.0;

            if( !ReportProgress( progress ) )
            {
                cancelled = true;
                break;
            }

            std::string code = RouteConnection( conn, passNum );  // Use pass number

            if( !code.empty() )
            {
                allCode << "# Route net (pass " << ( passNum + 1 ) << "): " << conn.net_name << "\n";
                allCode << code;
                allCode << "nets_routed += 1\n";
                allCode << "\n";
            }
            else
            {
                stillFailed.push_back( conn );
            }

            // Check time limit
            if( m_control.max_time_seconds > 0 )
            {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>( now - m_startTime ).count();

                if( elapsed > m_control.max_time_seconds )
                {
                    m_result.error_message = "Time limit exceeded";
                    timeExceeded = true;
                    break;
                }
            }
        }

        failedNets = std::move( stillFailed );
        passNum++;  // Move to next pass
        m_result.passes_completed = passNum;
    }

    // Report remaining failed nets
    for( const auto& conn : failedNets )
    {
        allCode << "# Failed to route net: " << conn.net_name << "\n";
        allCode << "nets_failed += 1\n";
        allCode << "\n";
    }

    allCode << "\n";
    allCode << "print(json.dumps({\n";
    allCode << "    'status': 'success' if nets_failed == 0 else 'partial',\n";
    allCode << "    'nets_routed': nets_routed,\n";
    allCode << "    'nets_failed': nets_failed,\n";
    allCode << "    'tracks_added': total_tracks,\n";
    allCode << "    'vias_added': total_vias\n";
    allCode << "}))\n";

    auto endTime = std::chrono::steady_clock::now();
    m_result.time_seconds = std::chrono::duration<double>( endTime - m_startTime ).count();
    m_result.success = ( m_result.nets_failed == 0 && !cancelled );
    m_result.cancelled = cancelled;

    // Final progress report
    if( !cancelled )
    {
        AUTOROUTE_PROGRESS finalProgress;
        finalProgress.current_net = totalNets;
        finalProgress.total_nets = totalNets;
        finalProgress.current_pass = numPasses;
        finalProgress.total_passes = numPasses;
        finalProgress.nets_routed = m_result.nets_routed;
        finalProgress.nets_failed = m_result.nets_failed;
        finalProgress.phase = "complete";
        finalProgress.elapsed_seconds = m_result.time_seconds;
        finalProgress.percent_complete = 100.0;
        ReportProgress( finalProgress );
    }

    return allCode.str();
}


std::vector<EXPANSION_ROOM*> AUTOROUTE_ENGINE::CreatePadRooms( PAD* aPad )
{
    std::vector<EXPANSION_ROOM*> rooms;

    if( !aPad )
        return rooms;

    AUTOROUTE_DEBUG( "CreatePadRooms: pad at " << aPad->GetPosition().x << "," << aPad->GetPosition().y );

    // FreeRouting approach: Create incomplete free-space rooms from pad connection points,
    // then complete them. This gives us free space rooms with doors, not obstacle rooms.

    BOX2I boardBounds = GetBoardBounds();
    if( !boardBounds.IsValid() )
        return rooms;

    // Get pad layers
    LSET padLayers = aPad->GetLayerSet() & LSET::AllCuMask();

    for( int layer = 0; layer < m_layerCount; ++layer )
    {
        // Use same layer mapping as InsertBoardItems() for consistency
        PCB_LAYER_ID pcbLayer = ( layer == 0 ) ? F_Cu :
                                ( layer == m_layerCount - 1 ) ? B_Cu :
                                static_cast<PCB_LAYER_ID>( In1_Cu + layer - 1 );

        if( !padLayers.Contains( pcbLayer ) )
            continue;

        // Create incomplete room starting from pad center, extending to board bounds
        // The "contained shape" is the pad center - this must remain in the completed room
        VECTOR2I padCenter = aPad->GetPosition();

        // Unbounded shape (board bounds)
        auto shape = std::make_unique<INT_BOX>( boardBounds );

        // Contained shape is a small box around pad center (connection point)
        int padSize = std::min( aPad->GetSizeX(), aPad->GetSizeY() ) / 2;
        VECTOR2I cMin( padCenter.x - padSize, padCenter.y - padSize );
        VECTOR2I cMax( padCenter.x + padSize, padCenter.y + padSize );
        auto contained = std::make_unique<INT_BOX>( cMin, cMax );

        auto incompleteRoom = std::make_unique<INCOMPLETE_FREE_SPACE_ROOM>(
            std::move( shape ), layer, std::move( contained ) );

        AUTOROUTE_DEBUG( "CreatePadRooms: created incomplete room on layer " << layer );

        // Complete the room immediately to get doors
        int netCode = aPad->GetNetCode();
        std::vector<FREE_SPACE_ROOM*> completedRooms = CompleteExpansionRoom( incompleteRoom.get(), netCode );

        AUTOROUTE_DEBUG( "CreatePadRooms: completed " << completedRooms.size() << " rooms" );

        for( FREE_SPACE_ROOM* completedRoom : completedRooms )
        {
            AUTOROUTE_DEBUG( "CreatePadRooms: completed room has " << completedRoom->GetDoors().size() << " doors" );
            rooms.push_back( completedRoom );
        }
    }

    return rooms;
}


EXPANSION_ROOM* AUTOROUTE_ENGINE::CreateObstacleRoom( BOARD_ITEM* aItem, int aLayer )
{
    if( !aItem )
        return nullptr;

    BOX2I itemBox = aItem->GetBoundingBox();
    itemBox.Inflate( m_control.clearance );

    auto shape = std::make_unique<INT_BOX>( itemBox );
    auto room = std::make_unique<OBSTACLE_ROOM>( std::move( shape ), aItem, aLayer );

    EXPANSION_ROOM* roomPtr = room.get();
    m_roomsByLayer[aLayer].push_back( roomPtr );
    m_rooms.push_back( std::move( room ) );

    return roomPtr;
}


std::vector<EXPANSION_ROOM*> AUTOROUTE_ENGINE::GetRoomsOnLayer( int aLayer ) const
{
    auto it = m_roomsByLayer.find( aLayer );
    if( it != m_roomsByLayer.end() )
        return it->second;

    return {};
}


INCOMPLETE_FREE_SPACE_ROOM* AUTOROUTE_ENGINE::AddIncompleteRoom(
    std::unique_ptr<INCOMPLETE_FREE_SPACE_ROOM> aRoom )
{
    if( !aRoom )
        return nullptr;

    INCOMPLETE_FREE_SPACE_ROOM* roomPtr = aRoom.get();
    m_incompleteRooms.push_back( std::move( aRoom ) );

    return roomPtr;
}


FREE_SPACE_ROOM* AUTOROUTE_ENGINE::CompleteRoom( INCOMPLETE_FREE_SPACE_ROOM* aRoom, int aNetCode )
{
    if( !aRoom )
        return nullptr;

    // Use the room completion algorithm
    ROOM_COMPLETION completion( *this, m_searchTree );
    COMPLETION_RESULT result = completion.Complete( *aRoom, aNetCode );

    if( !result.completed_room )
        return nullptr;

    FREE_SPACE_ROOM* roomPtr = result.completed_room.get();

    // Add the completed room to our storage
    m_roomsByLayer[roomPtr->GetLayer()].push_back( roomPtr );
    m_rooms.push_back( std::move( result.completed_room ) );

    // Add the new doors
    for( auto& door : result.new_doors )
    {
        m_doors.push_back( std::move( door ) );
    }

    // Add the new incomplete rooms
    for( auto& incompleteRoom : result.new_incomplete_rooms )
    {
        m_incompleteRooms.push_back( std::move( incompleteRoom ) );
    }

    // Insert the completed room into the search tree
    m_searchTree.Insert( roomPtr );

    return roomPtr;
}


std::vector<FREE_SPACE_ROOM*> AUTOROUTE_ENGINE::CompleteExpansionRoom(
    INCOMPLETE_FREE_SPACE_ROOM* aIncompleteRoom, int aNetCode )
{
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: START, room=" << (void*)aIncompleteRoom
                     << " netCode=" << aNetCode );

    std::vector<FREE_SPACE_ROOM*> result;

    if( !aIncompleteRoom )
    {
        AUTOROUTE_DEBUG( "CompleteExpansionRoom: null room, returning" );
        return result;
    }

    // Use the room completion algorithm
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: calling completion.Complete" );
    ROOM_COMPLETION completion( *this, m_searchTree );

    // Pass destination items so TARGET_EXPANSION_DOORs can be created
    // when same-net items that are destinations are encountered
    if( !m_currentDestItems.empty() )
    {
        AUTOROUTE_DEBUG( "CompleteExpansionRoom: setting " << m_currentDestItems.size() << " destinations" );
        completion.SetDestinations( m_currentDestItems );
    }

    COMPLETION_RESULT completionResult = completion.Complete( *aIncompleteRoom, aNetCode );
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: completion.Complete returned" );

    if( !completionResult.completed_room )
    {
        AUTOROUTE_DEBUG( "CompleteExpansionRoom: no completed room, returning" );
        return result;
    }

    FREE_SPACE_ROOM* roomPtr = completionResult.completed_room.get();
    result.push_back( roomPtr );
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: got completed room=" << (void*)roomPtr );

    // Add the completed room to our storage
    int layer = roomPtr->GetLayer();
    m_roomsByLayer[layer].push_back( roomPtr );
    m_rooms.push_back( std::move( completionResult.completed_room ) );

    // Add the new doors (doors are already connected to rooms in CalculateDoorsAndRooms)
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: adding " << completionResult.new_doors.size() << " doors" );
    for( auto& door : completionResult.new_doors )
    {
        m_doors.push_back( std::move( door ) );
    }

    // Add the new target doors (doors to own-net items)
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: adding " << completionResult.new_target_doors.size()
                     << " target doors" );
    for( auto& targetDoor : completionResult.new_target_doors )
    {
        m_targetDoors.push_back( std::move( targetDoor ) );
    }

    // Add the new incomplete rooms for expansion frontier
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: adding " << completionResult.new_incomplete_rooms.size()
                     << " incomplete rooms" );
    for( auto& incompleteRoom : completionResult.new_incomplete_rooms )
    {
        m_incompleteRooms.push_back( std::move( incompleteRoom ) );
    }

    // Insert the completed room into the search tree for future queries
    AUTOROUTE_DEBUG( "CompleteExpansionRoom: inserting into search tree" );
    m_searchTree.Insert( roomPtr );

    AUTOROUTE_DEBUG( "CompleteExpansionRoom: done" );
    return result;
}


INCOMPLETE_FREE_SPACE_ROOM* AUTOROUTE_ENGINE::CreateInitialIncompleteRoom(
    int aLayer, const VECTOR2I& aContainedPoint )
{
    BOX2I bounds = GetBoardBounds();
    if( !bounds.IsValid() )
        return nullptr;

    // Create a shape from the board bounds
    auto shape = std::make_unique<INT_BOX>( bounds );

    // Create a small contained shape around the starting point
    // This ensures the completed room will include this point
    int smallSize = m_control.clearance;
    VECTOR2I minPt( aContainedPoint.x - smallSize, aContainedPoint.y - smallSize );
    VECTOR2I maxPt( aContainedPoint.x + smallSize, aContainedPoint.y + smallSize );
    auto containedShape = std::make_unique<INT_BOX>( minPt, maxPt );

    auto room = std::make_unique<INCOMPLETE_FREE_SPACE_ROOM>(
        std::move( shape ), aLayer, std::move( containedShape ) );

    INCOMPLETE_FREE_SPACE_ROOM* roomPtr = room.get();
    m_incompleteRooms.push_back( std::move( room ) );

    return roomPtr;
}
