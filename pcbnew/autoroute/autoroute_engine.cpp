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
#include "locate/locate_connection.h"
#include "insert/insert_connection.h"
#include "geometry/tile_shape.h"

#include <board.h>
#include <pad.h>
#include <pcb_track.h>
#include <footprint.h>
#include <connectivity/connectivity_data.h>

#include <chrono>
#include <sstream>


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

    if( m_board )
    {
        // Get layer count from board
        m_layerCount = m_board->GetCopperLayerCount();
    }
}


void AUTOROUTE_ENGINE::ClearRoomModel()
{
    m_rooms.clear();
    m_doors.clear();
    m_drills.clear();
    m_roomsByLayer.clear();
}


void AUTOROUTE_ENGINE::BuildRoomModel()
{
    ClearRoomModel();

    if( !m_board )
        return;

    // Step 1: Build obstacle rooms from existing items
    BuildObstacleRooms();

    // Step 2: Build free space rooms
    BuildFreeSpaceRooms();

    // Step 3: Create doors between adjacent rooms
    BuildDoors();

    // Step 4: Create potential via locations
    if( m_control.vias_allowed && m_layerCount > 1 )
    {
        BuildDrills();
    }
}


void AUTOROUTE_ENGINE::BuildObstacleRooms()
{
    if( !m_board )
        return;

    int clearance = m_control.clearance;

    // Create obstacle rooms for pads
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            // Get pad layers
            LSET layers = pad->GetLayerSet();

            for( int layer = 0; layer < m_layerCount; ++layer )
            {
                PCB_LAYER_ID pcbLayer = ( layer == 0 ) ? F_Cu :
                                        ( layer == m_layerCount - 1 ) ? B_Cu :
                                        static_cast<PCB_LAYER_ID>( In1_Cu + layer - 1 );

                if( !layers.test( pcbLayer ) )
                    continue;

                // Get pad bounding box with clearance
                BOX2I padBox = pad->GetBoundingBox();
                padBox.Inflate( clearance );

                auto shape = std::make_unique<INT_BOX>( padBox );
                auto room = std::make_unique<OBSTACLE_ROOM>( std::move( shape ), pad, layer );
                room->SetNetCode( pad->GetNetCode() );

                m_roomsByLayer[layer].push_back( room.get() );
                m_rooms.push_back( std::move( room ) );
            }
        }
    }

    // Create obstacle rooms for existing tracks
    for( PCB_TRACK* track : m_board->Tracks() )
    {
        int layer = track->GetLayer();

        // Simple layer mapping for now
        int routeLayer = ( layer == F_Cu ) ? 0 :
                         ( layer == B_Cu ) ? m_layerCount - 1 :
                         ( layer - In1_Cu + 1 );

        if( routeLayer < 0 || routeLayer >= m_layerCount )
            continue;

        BOX2I trackBox = track->GetBoundingBox();
        trackBox.Inflate( clearance );

        auto shape = std::make_unique<INT_BOX>( trackBox );
        auto room = std::make_unique<OBSTACLE_ROOM>( std::move( shape ), track, routeLayer );
        room->SetNetCode( track->GetNetCode() );

        m_roomsByLayer[routeLayer].push_back( room.get() );
        m_rooms.push_back( std::move( room ) );
    }
}


void AUTOROUTE_ENGINE::BuildFreeSpaceRooms()
{
    // This is a simplified implementation
    // A full implementation would use a more sophisticated space partitioning algorithm

    BOX2I boardBounds = GetBoardBounds();
    if( !boardBounds.IsValid() )
        return;

    // For now, create a simple grid of free space rooms
    int gridSize = m_control.clearance * 4;  // Reasonable grid cell size

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

    // Also check for "nearly adjacent" rooms - boxes that are close but not exactly touching
    // This handles cases where grid alignment causes small gaps
    int tolerance = m_control.clearance * 4;  // Allow gaps up to grid size

    // Check horizontal adjacency (right edge of box1 near left edge of box2 or vice versa)
    int horizGap1 = box2->Left() - box1->Right();
    int horizGap2 = box1->Left() - box2->Right();

    if( horizGap1 >= 0 && horizGap1 <= tolerance )
    {
        // Check vertical overlap
        int top = std::max( box1->Top(), box2->Top() );
        int bottom = std::min( box1->Bottom(), box2->Bottom() );
        if( top < bottom )
            return true;
    }

    if( horizGap2 >= 0 && horizGap2 <= tolerance )
    {
        int top = std::max( box1->Top(), box2->Top() );
        int bottom = std::min( box1->Bottom(), box2->Bottom() );
        if( top < bottom )
            return true;
    }

    // Check vertical adjacency (bottom edge of box1 near top edge of box2 or vice versa)
    int vertGap1 = box2->Top() - box1->Bottom();
    int vertGap2 = box1->Top() - box2->Bottom();

    if( vertGap1 >= 0 && vertGap1 <= tolerance )
    {
        int left = std::max( box1->Left(), box2->Left() );
        int right = std::min( box1->Right(), box2->Right() );
        if( left < right )
            return true;
    }

    if( vertGap2 >= 0 && vertGap2 <= tolerance )
    {
        int left = std::max( box1->Left(), box2->Left() );
        int right = std::min( box1->Right(), box2->Right() );
        if( left < right )
            return true;
    }

    return false;
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

    // For nearly-adjacent rooms, create a door at the midpoint between them
    // Use same tolerance as AreRoomsAdjacent() to ensure doors are created for all adjacent rooms
    int tolerance = m_control.clearance * 4;

    // Check horizontal adjacency
    int horizGap1 = box2->Left() - box1->Right();
    int horizGap2 = box1->Left() - box2->Right();

    if( horizGap1 >= 0 && horizGap1 <= tolerance )
    {
        int top = std::max( box1->Top(), box2->Top() );
        int bottom = std::min( box1->Bottom(), box2->Bottom() );
        if( top < bottom )
        {
            int midX = ( box1->Right() + box2->Left() ) / 2;
            SEG doorSeg( VECTOR2I( midX, top ), VECTOR2I( midX, bottom ) );
            auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
            EXPANSION_DOOR* doorPtr = door.get();
            m_doors.push_back( std::move( door ) );
            return doorPtr;
        }
    }

    if( horizGap2 >= 0 && horizGap2 <= tolerance )
    {
        int top = std::max( box1->Top(), box2->Top() );
        int bottom = std::min( box1->Bottom(), box2->Bottom() );
        if( top < bottom )
        {
            int midX = ( box2->Right() + box1->Left() ) / 2;
            SEG doorSeg( VECTOR2I( midX, top ), VECTOR2I( midX, bottom ) );
            auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
            EXPANSION_DOOR* doorPtr = door.get();
            m_doors.push_back( std::move( door ) );
            return doorPtr;
        }
    }

    // Check vertical adjacency
    int vertGap1 = box2->Top() - box1->Bottom();
    int vertGap2 = box1->Top() - box2->Bottom();

    if( vertGap1 >= 0 && vertGap1 <= tolerance )
    {
        int left = std::max( box1->Left(), box2->Left() );
        int right = std::min( box1->Right(), box2->Right() );
        if( left < right )
        {
            int midY = ( box1->Bottom() + box2->Top() ) / 2;
            SEG doorSeg( VECTOR2I( left, midY ), VECTOR2I( right, midY ) );
            auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
            EXPANSION_DOOR* doorPtr = door.get();
            m_doors.push_back( std::move( door ) );
            return doorPtr;
        }
    }

    if( vertGap2 >= 0 && vertGap2 <= tolerance )
    {
        int left = std::max( box1->Left(), box2->Left() );
        int right = std::min( box1->Right(), box2->Right() );
        if( left < right )
        {
            int midY = ( box2->Bottom() + box1->Top() ) / 2;
            SEG doorSeg( VECTOR2I( left, midY ), VECTOR2I( right, midY ) );
            auto door = std::make_unique<EXPANSION_DOOR>( aRoom1, aRoom2, doorSeg );
            EXPANSION_DOOR* doorPtr = door.get();
            m_doors.push_back( std::move( door ) );
            return doorPtr;
        }
    }

    return nullptr;
}


void AUTOROUTE_ENGINE::BuildDrills()
{
    // Create potential via locations at room intersections across layers
    // This is a simplified approach - a full implementation would be more sophisticated

    int gridSize = m_control.clearance * 2;
    BOX2I bounds = GetBoardBounds();

    if( !bounds.IsValid() )
        return;

    for( int x = bounds.GetLeft() + gridSize; x < bounds.GetRight(); x += gridSize )
    {
        for( int y = bounds.GetTop() + gridSize; y < bounds.GetBottom(); y += gridSize )
        {
            VECTOR2I pos( x, y );

            // Check if this position is in free space on all layers
            bool validOnAllLayers = true;

            for( int layer = 0; layer < m_layerCount; ++layer )
            {
                bool inFreeSpace = false;

                for( EXPANSION_ROOM* room : m_roomsByLayer[layer] )
                {
                    if( room->GetType() == ROOM_TYPE::FREE_SPACE &&
                        room->Contains( pos ) )
                    {
                        inFreeSpace = true;
                        break;
                    }
                }

                if( !inFreeSpace )
                {
                    validOnAllLayers = false;
                    break;
                }
            }

            if( validOnAllLayers )
            {
                auto drill = std::make_unique<EXPANSION_DRILL>( pos, 0, m_layerCount - 1 );
                drill->SetViaDiameter( m_control.via_diameter );
                drill->SetDrillDiameter( m_control.via_drill );

                m_drills.push_back( std::move( drill ) );
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


std::string AUTOROUTE_ENGINE::RouteConnection( const NET_CONNECTION& aConnection )
{
    ResetSearchState();

    // Create maze search
    MAZE_SEARCH search( *this, m_control );
    search.SetSources( aConnection.source_pads );
    search.SetDestinations( aConnection.dest_pads );

    // Debug: Check source rooms
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

    // Find path
    auto result = search.FindConnection();

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

    // Locate the path
    LOCATE_CONNECTION locator;
    locator.SetMazeSearch( &search );
    locator.SetTrackWidth( m_control.GetTraceWidth( 0 ) );
    locator.SetClearance( m_control.clearance );

    ROUTING_PATH path = locator.LocatePath();

    if( !path.IsValid() )
        return "";

    // Insert tracks directly into board
    INSERT_CONNECTION inserter;
    inserter.SetBoard( m_board );
    inserter.SetControl( m_control );
    inserter.SetNetName( aConnection.net_name );

    INSERT_RESULT insertResult = inserter.Insert( path );

    m_result.tracks_added += insertResult.tracks_added;
    m_result.vias_added += insertResult.vias_added;

    return insertResult.success ? "ok" : "";
}


std::string AUTOROUTE_ENGINE::RouteAll()
{
    auto startTime = std::chrono::steady_clock::now();

    BuildRoomModel();

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
    debugStats << " Nets:" << connections.size() << "; ";

    m_result.error_message = debugStats.str();

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

    for( const auto& conn : connections )
    {
        std::string code = RouteConnection( conn );

        if( !code.empty() )
        {
            allCode << "# Route net: " << conn.net_name << "\n";
            allCode << code;
            allCode << "nets_routed += 1\n";
            allCode << "\n";
        }
        else
        {
            allCode << "# Failed to route net: " << conn.net_name << "\n";
            allCode << "nets_failed += 1\n";
            allCode << "\n";
        }

        // Check time limit
        if( m_control.max_time_seconds > 0 )
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>( now - startTime ).count();

            if( elapsed > m_control.max_time_seconds )
            {
                m_result.error_message = "Time limit exceeded";
                break;
            }
        }
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
    m_result.time_seconds = std::chrono::duration<double>( endTime - startTime ).count();
    m_result.success = ( m_result.nets_failed == 0 );

    return allCode.str();
}


std::vector<EXPANSION_ROOM*> AUTOROUTE_ENGINE::CreatePadRooms( PAD* aPad )
{
    std::vector<EXPANSION_ROOM*> rooms;

    if( !aPad )
        return rooms;

    // Already created during BuildObstacleRooms
    // Find existing rooms for this pad
    for( auto& room : m_rooms )
    {
        OBSTACLE_ROOM* obstRoom = dynamic_cast<OBSTACLE_ROOM*>( room.get() );
        if( obstRoom && obstRoom->GetItem() == aPad )
        {
            rooms.push_back( obstRoom );
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
